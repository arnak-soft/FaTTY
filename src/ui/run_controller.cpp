#include "ui/run_controller.hpp"

#include "core/util.hpp"
#include "ui/theme.hpp"

#include <wx/app.h>
#include <wx/textctrl.h>

#include <thread>

namespace fatty {

RunController::RunController(Host host) : host_(std::move(host)) {}

bool RunController::is_occupied() const {
  if (host_.busy && *host_.busy) return true;
  if (host_.bundle_active && host_.bundle_active()) return true;
  return false;
}

std::string RunController::queue_suffix() const {
  if (run_queue_.empty()) return {};
  return "  •  очередь: " + std::to_string(run_queue_.size());
}

void RunController::clear_run_queue() {
  run_queue_.clear();
}

void RunController::enqueue_job(QueuedJob job, const std::string& label) {
  run_queue_.push_back(std::move(job));
  const wxColour meta = Theme::meta();
  if (host_.append_output) {
    host_.append_output("В очередь: " + label + " (" + std::to_string(run_queue_.size()) + ")\n", &meta);
  }
  if (host_.set_status && host_.busy_label) {
    host_.set_status(*host_.busy_label + queue_suffix());
  }
}

void RunController::pump_run_queue() {
  if (host_.bundle_active && host_.bundle_active()) return;
  if (run_queue_.empty()) {
    if (host_.set_busy) host_.set_busy(false);
    return;
  }
  auto job = std::move(run_queue_.front());
  run_queue_.pop_front();
  if (job.type == QueuedJob::Type::Bundle) {
    if (host_.start_bundle) {
      host_.start_bundle(job.server, job.bundle_id, job.bundle_name, std::move(job.bundle_cmds), job.interval_sec);
    } else if (host_.set_busy && run_queue_.empty()) {
      host_.set_busy(false);
    } else {
      pump_run_queue();
    }
    return;
  }
  start_ssh_run(std::move(job.server), std::move(job.command), job.timeout, job.login_shell, std::move(job.title),
                std::move(job.command_id), std::move(job.kind), {}, std::move(job.working_dir), job.cd_before_run,
                std::move(job.remote_shell));
}

void RunController::run_bundle(const Server& server, const std::string& bundle_id, const std::string& bundle_name,
                               std::vector<Command> cmds, int interval_sec) {
  if (cmds.empty()) return;
  if (is_occupied()) {
    QueuedJob job;
    job.type = QueuedJob::Type::Bundle;
    job.server = server;
    job.bundle_id = bundle_id;
    job.bundle_name = bundle_name;
    job.bundle_cmds = std::move(cmds);
    job.interval_sec = interval_sec;
    enqueue_job(std::move(job), "связка «" + bundle_name + "»  •  " + server.name);
    return;
  }
  if (host_.start_bundle) {
    host_.start_bundle(server, bundle_id, bundle_name, std::move(cmds), interval_sec);
  }
}

void RunController::run_command(const Server& server, const std::string& command, int timeout, bool login_shell,
                                const std::string& title, const std::string& command_id, const std::string& kind,
                                std::function<void(int code, std::string status)> on_done, std::string working_dir,
                                bool cd_before_run, std::string_view remote_shell) {
  const bool chained = static_cast<bool>(on_done);
  if (is_occupied() && !chained) {
    QueuedJob job;
    job.type = QueuedJob::Type::Command;
    job.server = server;
    job.command = command;
    job.timeout = timeout;
    job.login_shell = login_shell;
    job.title = title;
    job.command_id = command_id;
    job.kind = kind;
    job.working_dir = std::move(working_dir);
    job.cd_before_run = cd_before_run;
    job.remote_shell = std::string(remote_shell);
    enqueue_job(std::move(job), title + "  •  " + server.name);
    return;
  }
  start_ssh_run(server, command, timeout, login_shell, title, command_id, kind, std::move(on_done),
                std::move(working_dir), cd_before_run, std::string(remote_shell));
}

void RunController::start_ssh_run(Server server, std::string command, int timeout, bool login_shell, std::string title,
                                  std::string command_id, std::string kind,
                                  std::function<void(int code, std::string status)> on_done, std::string working_dir,
                                  bool cd_before_run, std::string remote_shell) {
  const bool chained = static_cast<bool>(on_done);
  if (host_.settings && host_.settings->clear_output_before_run && !chained && host_.output && host_.busy &&
      !*host_.busy) {
    host_.output->Clear();
  }
  if (host_.busy_label) *host_.busy_label = "Выполняется: " + title + " → " + server.name;
  if (host_.busy_server_id) *host_.busy_server_id = server.id;
  if (!chained && host_.set_busy) host_.set_busy(true);
  std::string cwd;
  if (host_.remote_cwd) cwd = (*host_.remote_cwd)[server.id];
  if (cd_before_run) {
    auto dir = trim(working_dir);
    if (!dir.empty()) cwd = dir;
  }
  if (host_.set_status && host_.busy_label) host_.set_status(*host_.busy_label + queue_suffix());
  const wxColour meta = Theme::meta();
  if (host_.append_output) {
    host_.append_output("\n" + std::string(60, '-') + "\n", &meta);
    host_.append_output(title + "  •  " + server.name + "\n", &meta);
  }
  if (host_.session) *host_.session = std::make_shared<SSHSession>();
  auto started = now_iso();
  auto t0 = std::chrono::steady_clock::now();
  Server srv = std::move(server);
  const std::string shell = normalize_remote_shell(
      remote_shell.empty() ? srv.remote_shell : remote_shell);
  auto alive = host_.alive;
  auto session = host_.session ? *host_.session : nullptr;
  auto journal = host_.journal;
  auto running = host_.worker_running;
  if (running) running->store(true);
  std::thread([this, host = host_, alive, session, journal, running, srv, command = std::move(command), timeout,
               login_shell, title = std::move(title), command_id = std::move(command_id), kind = std::move(kind), cwd,
               started, t0, shell = std::move(shell), on_done = std::move(on_done)] {
    struct RunningGuard {
      std::shared_ptr<std::atomic<bool>> flag;
      ~RunningGuard() {
        if (flag) flag->store(false);
      }
    } guard{running};
    auto post = [alive](std::function<void()> fn) {
      if (!alive || !alive->load()) return;
      wxTheApp->CallAfter([alive, fn = std::move(fn)] {
        if (!alive->load()) return;
        fn();
      });
    };
    int code = 1;
    std::string status = "error";
    std::string error;
    std::string new_cwd = cwd;
    std::string captured;
    captured.reserve(64 * 1024);
    bool truncated = false;
    try {
      auto result = session->run(srv, command, timeout, login_shell,
                                 [&captured, &truncated, post, append = host.append_output](const std::string& chunk) {
                                   captured.append(chunk);
                                   if (captured.size() > kJournalOutputMax) {
                                     truncated = true;
                                     captured.erase(0, captured.size() - kJournalOutputMax);
                                   }
                                   if (append) {
                                     post([append, chunk] { append(chunk, nullptr); });
                                   }
                                 },
                                 cwd, shell);
      code = result.exit_code;
      status = status_from_exit(code);
      if (!result.cwd.empty()) new_cwd = result.cwd;
      if (host.append_output) {
        auto col = code == 0 ? Theme::ok() : Theme::err();
        auto when = now_iso();
        if (auto p = when.find('T'); p != std::string::npos) when[p] = ' ';
        post([append = host.append_output, code, col, when = std::move(when)] {
          append("\n← код выхода " + std::to_string(code) + "  •  " + when + "\n", &col);
        });
      }
    } catch (const std::exception& exc) {
      error = exc.what();
      if (host.append_output) {
        const wxColour err = Theme::err();
        post([append = host.append_output, error, err] { append("\n" + error + "\n", &err); });
      }
    }
    auto duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (journal) {
      JournalEntry e;
      e.started_at = started;
      e.finished_at = now_iso();
      e.duration_sec = duration;
      e.server_id = srv.id;
      e.server_name = srv.name;
      e.host = srv.host;
      e.port = srv.port;
      e.username = srv.username;
      e.command_id = command_id;
      e.title = title;
      e.command = command;
      e.cwd = new_cwd;
      e.login_shell = login_shell;
      e.timeout_sec = timeout;
      if (error.empty()) e.exit_code = code;
      e.status = status;
      e.kind = kind;
      e.error = error;
      e.output = truncated ? ("…\n" + captured) : captured;
      journal->append(e);
    }
    post([this, alive, host, srv, title, code, status, error, new_cwd, on_done, kind] {
      if (!alive || !alive->load()) return;
      if (host.remote_cwd && !new_cwd.empty()) (*host.remote_cwd)[srv.id] = new_cwd;
      if (host.update_cwd_label) host.update_cwd_label();
      if (host.session) host.session->reset();
      if (on_done) {
        on_done(code, status);
        return;
      }
      if (host.on_command_finished) host.on_command_finished(title, srv.name, status, kind);
      if (!run_queue_.empty()) {
        if (host.set_status && host.busy_label) {
          host.set_status(
              "Готово  •  код " +
              (status == "error" && code == 1 && !error.empty() ? std::string("—") : std::to_string(code)) + "  •  " +
              title + queue_suffix());
        }
        pump_run_queue();
        return;
      }
      if (host.set_busy) host.set_busy(false);
      if (host.set_status && host.busy_label) {
        host.set_status("Готово  •  код " +
                        (status == "error" && code == 1 && !error.empty() ? std::string("—") : std::to_string(code)) +
                        "  •  " + title);
      }
    });
  }).detach();
}

}  // namespace fatty

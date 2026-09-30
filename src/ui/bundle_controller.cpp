#include "ui/bundle_controller.hpp"

#include "core/util.hpp"
#include "ui/theme.hpp"

#include <wx/textctrl.h>

namespace fatty {

BundleController::BundleController(Host host) : host_(std::move(host)) {}

void BundleController::cancel() {
  cancel_ = true;
  waiting_ = false;
}

void BundleController::tick_waiting() {
  if (!active_ || !waiting_) return;
  if (cancel_) {
    waiting_ = false;
    finish("прервано");
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now >= wait_until_) {
    waiting_ = false;
    run_step();
    return;
  }
  auto left_ms = std::chrono::duration_cast<std::chrono::milliseconds>(wait_until_ - now).count();
  const int left_sec = static_cast<int>((left_ms + 999) / 1000);
  if (host_.busy_label) {
    *host_.busy_label = "Связка «" + name_ + "»: пауза " + std::to_string(std::max(1, left_sec)) + " с до " +
                        std::to_string(index_ + 1) + "/" + std::to_string(cmds_.size());
  }
  if (host_.set_status && host_.busy_label && host_.runs) {
    host_.set_status(*host_.busy_label + host_.runs->queue_suffix());
  }
}

bool BundleController::start(const Server& server, const std::string& bundle_id, const std::string& bundle_name,
                             std::vector<Command> cmds, int interval_sec) {
  if (cmds.empty()) return false;
  active_ = true;
  cancel_ = false;
  waiting_ = false;
  index_ = 0;
  last_code_ = 1;
  last_status_.clear();
  interval_sec_ = interval_sec;
  bundle_id_ = bundle_id;
  name_ = bundle_name;
  started_at_ = now_iso();
  started_ = std::chrono::steady_clock::now();
  server_ = server;
  cmds_ = std::move(cmds);
  if (host_.busy_label) *host_.busy_label = "Связка «" + name_ + "»";
  if (host_.set_busy) host_.set_busy(true);
  if (host_.settings && host_.settings->clear_output_before_run && host_.output) host_.output->Clear();
  const wxColour meta = Theme::meta();
  if (host_.append_output) {
    host_.append_output("\n" + std::string(60, '=') + "\n", &meta);
    host_.append_output("Связка «" + name_ + "»  •  " + std::to_string(cmds_.size()) + " команд, пауза " +
                            std::to_string(interval_sec_) + " с\n",
                        &meta);
  }
  run_step();
  return true;
}

void BundleController::run_step() {
  if (!active_) return;
  if (cancel_ || index_ >= static_cast<int>(cmds_.size())) {
    finish(cancel_ ? "прервано" : "готово");
    return;
  }
  const auto& c = cmds_[static_cast<std::size_t>(index_)];
  if (host_.busy_label) {
    *host_.busy_label = "Связка «" + name_ + "» (" + std::to_string(index_ + 1) + "/" +
                        std::to_string(cmds_.size()) + "): " + c.name;
  }
  if (host_.set_status && host_.busy_label) host_.set_status(*host_.busy_label);
  if (!host_.run_step) return;
  host_.run_step(server_, c, [this](int code, std::string status) {
    if (!active_) return;
    if (cancel_ || status == "cancelled") {
      finish("прервано");
      return;
    }
    if (status != "ok") {
      last_code_ = code;
      last_status_ = status;
      if (host_.append_output) {
        const wxColour err = Theme::err();
        host_.append_output("\nСвязка остановлена: команда завершилась с ошибкой (код " + std::to_string(code) +
                                ").\n",
                            &err);
      }
      finish("ошибка");
      return;
    }
    ++index_;
    if (index_ >= static_cast<int>(cmds_.size())) {
      finish("готово");
      return;
    }
    schedule_wait();
  });
}

void BundleController::schedule_wait() {
  if (!active_) return;
  if (cancel_) {
    finish("прервано");
    return;
  }
  if (interval_sec_ <= 0) {
    run_step();
    return;
  }
  waiting_ = true;
  wait_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(interval_sec_);
  if (host_.busy_label) {
    *host_.busy_label = "Связка «" + name_ + "»: пауза " + std::to_string(interval_sec_) + " с до " +
                        std::to_string(index_ + 1) + "/" + std::to_string(cmds_.size());
  }
  if (host_.set_status && host_.busy_label && host_.runs) {
    host_.set_status(*host_.busy_label + host_.runs->queue_suffix());
  }
}

void BundleController::append_journal(const std::string& reason) {
  if (!host_.journal) return;
  JournalEntry e;
  e.started_at = started_at_.empty() ? now_iso() : started_at_;
  e.finished_at = now_iso();
  e.duration_sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
  if (e.duration_sec < 0) e.duration_sec = 0;
  e.server_id = server_.id;
  e.server_name = server_.name;
  e.host = server_.host;
  e.port = server_.port;
  e.username = server_.username;
  e.command_id = bundle_id_;
  e.title = name_;
  std::string steps;
  for (const auto& c : cmds_) {
    auto label = trim(c.name);
    if (label.empty()) label = trim(c.command);
    if (label.empty()) continue;
    if (!steps.empty()) steps += " → ";
    steps += label;
  }
  e.command = steps.empty() ? name_ : name_ + ": " + steps;
  e.kind = "bundle";
  e.login_shell = false;
  if (reason == "готово") {
    e.status = "ok";
    e.exit_code = 0;
  } else if (reason == "ошибка") {
    e.status = last_status_.empty() ? "failed" : last_status_;
    if (e.status != "error") e.exit_code = last_code_;
  } else if (reason == "прервано") {
    e.status = "cancelled";
  } else {
    e.status = "error";
    e.error = reason;
  }
  e.output = "Связка «" + name_ + "»: " + reason + "\nШагов: " + std::to_string(cmds_.size());
  host_.journal->append(std::move(e));
}

void BundleController::finish(const std::string& reason) {
  if (active_) append_journal(reason);
  waiting_ = false;
  active_ = false;
  cancel_ = false;
  cmds_.clear();
  const wxColour meta = Theme::meta();
  if (host_.append_output) host_.append_output("Связка «" + name_ + "»: " + reason + "\n", &meta);
  if (host_.runs && host_.runs->has_queue() && reason != "прервано") {
    if (host_.set_status) host_.set_status("Связка «" + name_ + "»: " + reason + host_.runs->queue_suffix());
    host_.runs->pump_run_queue();
    return;
  }
  if (host_.set_busy) host_.set_busy(false);
  if (host_.set_status) host_.set_status("Связка «" + name_ + "»: " + reason);
}

}  // namespace fatty

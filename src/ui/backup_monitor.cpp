#include "ui/backup_monitor.hpp"

#include "core/health.hpp"
#include "core/journal.hpp"
#include "core/vps_backup.hpp"

#include <wx/app.h>

#include <chrono>
#include <thread>

namespace fatty {

BackupMonitor::BackupMonitor(Deps deps) : deps_(std::move(deps)) {
  Bind(wxEVT_TIMER, &BackupMonitor::on_timer, this);
  timer_.SetOwner(this);
}

BackupMonitor::~BackupMonitor() { stop(); }

void BackupMonitor::start() {
  if (!timer_.IsRunning()) timer_.Start(30000);
  wake();
}

void BackupMonitor::stop() { timer_.Stop(); }

void BackupMonitor::wake() { tick(); }

void BackupMonitor::run_now(const std::string& job_id) {
  {
    std::lock_guard lock(mutex_);
    force_job_id_ = job_id;
  }
  tick();
}

std::string BackupMonitor::running_job_id() const {
  std::lock_guard lock(mutex_);
  return running_job_id_;
}

void BackupMonitor::on_timer(wxTimerEvent&) { tick(); }

void BackupMonitor::tick() {
  if (running_->load()) return;
  std::string force;
  {
    std::lock_guard lock(mutex_);
    force = force_job_id_;
    force_job_id_.clear();
  }
  if (!force.empty()) {
    start_job(force, true);
    return;
  }
  if (!deps_.config) return;
  Config& cfg = deps_.config();
  const double now = unix_now();
  const auto busy = deps_.busy_server_id ? deps_.busy_server_id() : std::string{};
  for (const auto& job : cfg.backup_jobs) {
    if (!backup_job_is_due(job, now)) continue;
    if (!busy.empty() && (job.source_server_id == busy || job.dest_server_id == busy)) continue;
    start_job(job.id, false);
    return;
  }
}

void BackupMonitor::start_job(const std::string& job_id, bool manual) {
  if (running_->exchange(true)) return;
  {
    std::lock_guard lock(mutex_);
    running_job_id_ = job_id;
  }
  auto alive = deps_.alive;
  auto running = running_;
  auto journal = deps_.journal;
  std::thread([this, job_id, manual, alive, running, journal] {
    BackupRunResult result;
    std::string err;
    std::string log_buf;
    const auto started_iso = now_iso();
    const auto t0 = std::chrono::steady_clock::now();
    BackupJournalEntry entry;
    entry.started_at = started_iso;
    entry.job_id = job_id;
    entry.manual = manual;
    try {
      if (!alive || !*alive) {
        running->store(false);
        return;
      }
      Config& cfg = deps_.config();
      auto* job = cfg.backup_job_by_id(job_id);
      if (!job) {
        result.status = "error";
        result.error = "Задание не найдено";
        entry.job_name = job_id;
      } else if (!manual && !job->enabled) {
        result.skipped = true;
        result.status = "skipped";
        entry.job_name = job->name;
        entry.server_id = job->source_server_id;
        if (const Server* s = cfg.server_by_id(job->source_server_id)) {
          entry.server_name = s->name.empty() ? s->host : s->name;
        }
        entry.kind_label = backup_kind_label(job->kind);
        entry.dest_label = backup_dest_label(job->dest_kind);
      } else {
        entry.job_name = job->name;
        entry.server_id = job->source_server_id;
        if (const Server* s = cfg.server_by_id(job->source_server_id)) {
          entry.server_name = s->name.empty() ? s->host : s->name;
        }
        entry.kind_label = backup_kind_label(job->kind);
        entry.dest_label = backup_dest_label(job->dest_kind);
        BackupEngine engine;
        result = engine.run(
            *job, cfg,
            [this, job_id, alive, &log_buf](const std::string& line) {
              if (!alive || !*alive) return;
              log_buf += line;
              if (line.empty() || line.back() != '\n') log_buf.push_back('\n');
              if (deps_.on_log) {
                wxTheApp->CallAfter([this, job_id, line] {
                  if (deps_.on_log) deps_.on_log(job_id, line);
                });
              }
            });
      }
    } catch (const std::exception& exc) {
      err = exc.what();
      result.status = "error";
      result.error = err;
    }
    const auto t1 = std::chrono::steady_clock::now();
    entry.finished_at = now_iso();
    entry.duration_sec =
        std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0).count();
    entry.status = result.status.empty() ? (result.ok ? "ok" : "error") : result.status;
    entry.error = result.error;
    entry.archive_name = result.archive_name;
    entry.archive_bytes = result.archive_bytes;
    entry.log = std::move(log_buf);

    wxTheApp->CallAfter([this, running, alive, result, job_id, journal, entry = std::move(entry)]() mutable {
      if (alive && *alive) {
        if (auto* job = deps_.config().backup_job_by_id(job_id)) {
          if (result.status == "error" && !result.error.empty()) {
            job->last_status = result.status;
            job->last_error = result.error;
            job->last_run_unix = unix_now();
          }
        }
        if (journal && entry.status != "skipped") {
          journal->append(std::move(entry));
        }
        if (deps_.persist) deps_.persist();
        if (deps_.on_change) deps_.on_change();
      }
      {
        std::lock_guard lock(mutex_);
        running_job_id_.clear();
      }
      running->store(false);
    });
  }).detach();
}

}  // namespace fatty

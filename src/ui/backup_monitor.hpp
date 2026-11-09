#pragma once

#include "core/store.hpp"
#include "core/vps_backup_engine.hpp"

#include <wx/event.h>
#include <wx/timer.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace fatty {

class BackupMonitor : public wxEvtHandler {
 public:
  struct Deps {
    std::function<Config&()> config;
    std::function<std::string()> busy_server_id;
    std::shared_ptr<std::atomic<bool>> alive;
    std::function<void()> on_change;
    std::function<void()> persist;
    std::function<void(const std::string& job_id, const std::string& line)> on_log;
  };

  explicit BackupMonitor(Deps deps);
  ~BackupMonitor() override;

  void start();
  void stop();
  void wake();
  void run_now(const std::string& job_id);
  bool worker_busy() const { return running_->load(); }
  std::string running_job_id() const;

 private:
  void on_timer(wxTimerEvent&);
  void tick();
  void start_job(const std::string& job_id, bool manual);

  Deps deps_;
  wxTimer timer_;
  std::shared_ptr<std::atomic<bool>> running_ = std::make_shared<std::atomic<bool>>(false);
  mutable std::mutex mutex_;
  std::string running_job_id_;
  std::string force_job_id_;
};

}  // namespace fatty

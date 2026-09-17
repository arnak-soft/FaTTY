#pragma once

#include "core/health.hpp"
#include "core/store.hpp"
#include "net/ssh_session.hpp"

#include <wx/event.h>
#include <wx/timer.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace fatty {

class HealthMonitor : public wxEvtHandler {
 public:
  struct Deps {
    std::function<std::vector<Server>()> servers;
    std::function<AppSettings()> settings;
    std::function<std::string()> busy_server_id;
    std::shared_ptr<std::atomic<bool>> alive;
    std::function<void()> on_change;
  };

  explicit HealthMonitor(Deps deps);
  ~HealthMonitor() override;

  void start();
  void stop();
  void wake();
  void refresh(const std::string& server_id);
  void refresh_all();

  HealthSnapshot snapshot(const std::string& server_id) const;
  std::map<std::string, HealthSnapshot> snapshots() const;
  bool worker_busy() const { return running_->load(); }
  std::string checking_id() const;

 private:
  void on_timer(wxTimerEvent&);
  void tick();
  void start_check(Server server);
  void store_snapshot(HealthSnapshot snap, bool persist_now);
  HealthThresholds thresholds_from(const AppSettings& st) const;
  HealthCollect collect_from(const AppSettings& st) const;

  Deps deps_;
  wxTimer timer_;
  std::shared_ptr<std::atomic<bool>> running_ = std::make_shared<std::atomic<bool>>(false);
  std::shared_ptr<SSHSession> session_;
  mutable std::mutex mutex_;
  std::map<std::string, HealthSnapshot> snaps_;
  std::set<std::string> force_;
  std::size_t next_ = 0;
  std::string checking_id_;
};

}  // namespace fatty

#include "ui/health_monitor.hpp"

#include "core/paths.hpp"
#include "core/util.hpp"

#include <wx/app.h>

#include <functional>
#include <thread>

namespace fatty {

HealthMonitor::HealthMonitor(Deps deps) : deps_(std::move(deps)) {
  snaps_ = load_health_cache(health_cache_path());
  timer_.SetOwner(this);
  Bind(wxEVT_TIMER, &HealthMonitor::on_timer, this);
}

HealthMonitor::~HealthMonitor() { stop(); }

void HealthMonitor::start() {
  if (!timer_.IsRunning()) timer_.Start(15000);
  wake();
}

void HealthMonitor::stop() {
  timer_.Stop();
  if (session_) session_->cancel();
}

void HealthMonitor::wake() { tick(); }

void HealthMonitor::refresh(const std::string& server_id) {
  if (server_id.empty()) {
    refresh_all();
    return;
  }
  {
    std::lock_guard lock(mutex_);
    force_.insert(server_id);
  }
  tick();
}

void HealthMonitor::refresh_all() {
  auto servers = deps_.servers ? deps_.servers() : std::vector<Server>{};
  {
    std::lock_guard lock(mutex_);
    for (const auto& s : servers) {
      if (s.health_enabled) force_.insert(s.id);
    }
  }
  tick();
}

HealthSnapshot HealthMonitor::snapshot(const std::string& server_id) const {
  std::lock_guard lock(mutex_);
  auto it = snaps_.find(server_id);
  if (it == snaps_.end()) return {};
  return it->second;
}

std::map<std::string, HealthSnapshot> HealthMonitor::snapshots() const {
  std::lock_guard lock(mutex_);
  return snaps_;
}

std::string HealthMonitor::checking_id() const {
  std::lock_guard lock(mutex_);
  return checking_id_;
}

void HealthMonitor::on_timer(wxTimerEvent&) { tick(); }

HealthThresholds HealthMonitor::thresholds_from(const AppSettings& st) const {
  HealthThresholds t;
  t.disk_warn = st.health_disk_warn;
  t.disk_crit = st.health_disk_crit;
  t.ram_warn = st.health_ram_warn;
  t.ram_crit = st.health_ram_crit;
  t.cpu_warn = st.health_cpu_warn;
  t.cpu_crit = st.health_cpu_crit;
  return t;
}

HealthCollect HealthMonitor::collect_from(const AppSettings& st) const {
  return {st.health_show_cpu, st.health_show_ram, st.health_show_disk, st.health_show_load};
}

void HealthMonitor::tick() {
  if (running_->load()) return;
  if (!deps_.servers || !deps_.settings) return;
  auto servers = deps_.servers();
  if (servers.empty()) return;
  const auto settings = deps_.settings();
  const std::string busy = deps_.busy_server_id ? deps_.busy_server_id() : std::string{};
  std::string force_pick;
  {
    std::lock_guard lock(mutex_);
    for (const auto& s : servers) {
      if (!s.health_enabled) continue;
      if (s.id == busy) continue;
      if (force_.count(s.id)) {
        force_pick = s.id;
        break;
      }
    }
  }
  Server chosen;
  bool found = false;
  if (!force_pick.empty()) {
    for (const auto& s : servers) {
      if (s.id == force_pick) {
        chosen = s;
        found = true;
        break;
      }
    }
  } else if (settings.health_auto) {
    if (servers.empty()) return;
    const double now = unix_now();
    const int n = static_cast<int>(servers.size());
    for (int i = 0; i < n; ++i) {
      const auto& s = servers[(next_ + static_cast<std::size_t>(i)) % servers.size()];
      if (!s.health_enabled) continue;
      if (s.id == busy) continue;
      HealthSnapshot prev;
      {
        std::lock_guard lock(mutex_);
        auto it = snaps_.find(s.id);
        if (it != snaps_.end()) prev = it->second;
      }
      if (!health_is_due(prev, settings.health_interval_sec, now)) continue;
      chosen = s;
      found = true;
      next_ = (next_ + static_cast<std::size_t>(i) + 1) % servers.size();
      break;
    }
  }
  if (!found) return;
  start_check(std::move(chosen));
}

void HealthMonitor::start_check(Server server) {
  if (running_->exchange(true)) return;
  {
    std::lock_guard lock(mutex_);
    checking_id_ = server.id;
    auto& snap = snaps_[server.id];
    snap.server_id = server.id;
    snap.server_name = server.name;
    snap.checking = true;
    snap.level = HealthLevel::Checking;
  }
  if (deps_.on_change) deps_.on_change();

  session_ = std::make_shared<SSHSession>();
  auto session = session_;
  auto running = running_;
  auto alive = deps_.alive;
  const auto settings = deps_.settings ? deps_.settings() : AppSettings{};
  const auto collect = collect_from(settings);
  const auto thresholds = thresholds_from(settings);
  const int timeout = settings.health_timeout_sec > 0 ? settings.health_timeout_sec : kHealthTimeoutDefault;
  const std::string script = health_remote_script(collect);
  const std::string shell = normalize_remote_shell(server.remote_shell);

  std::thread([this, alive, running, session, server = std::move(server), script, timeout, shell, thresholds] {
    struct Guard {
      std::shared_ptr<std::atomic<bool>> flag;
      ~Guard() {
        if (flag) flag->store(false);
      }
    } guard{running};

    HealthSnapshot snap;
    snap.server_id = server.id;
    snap.server_name = server.name;
    std::string captured;
    captured.reserve(16 * 1024);
    try {
      session->run(
          server, script, timeout, false,
          [&captured](const std::string& chunk) { captured.append(chunk); }, "", shell);
      snap = parse_health_output(captured);
      snap.server_id = server.id;
      snap.server_name = server.name;
      snap.checked_at = unix_now();
      if (snap.level != HealthLevel::Unsupported) {
        apply_health_thresholds(snap, thresholds);
      }
      if (snap.level == HealthLevel::Unknown && !snap.error.empty() && captured.find("FATTYHEALTH") == std::string::npos) {
        snap.level = HealthLevel::Unknown;
      }
    } catch (const std::exception& exc) {
      snap.level = HealthLevel::Offline;
      snap.error = exc.what();
      snap.checked_at = unix_now();
    }
    snap.checking = false;

    auto post = [alive](std::function<void()> fn) {
      if (!alive || !alive->load()) return;
      wxTheApp->CallAfter([alive, fn = std::move(fn)] {
        if (!alive || !alive->load()) return;
        fn();
      });
    };
    post([this, snap = std::move(snap)]() mutable {
      {
        std::lock_guard lock(mutex_);
        force_.erase(snap.server_id);
        checking_id_.clear();
        session_.reset();
      }
      store_snapshot(std::move(snap), true);
      if (deps_.on_change) deps_.on_change();
      tick();
    });
  }).detach();
}

void HealthMonitor::store_snapshot(HealthSnapshot snap, bool persist_now) {
  std::map<std::string, HealthSnapshot> copy;
  {
    std::lock_guard lock(mutex_);
    snaps_[snap.server_id] = std::move(snap);
    copy = snaps_;
  }
  if (!persist_now) return;
  if (deps_.servers) {
    std::set<std::string> live;
    for (const auto& s : deps_.servers()) live.insert(s.id);
    for (auto it = copy.begin(); it != copy.end();) {
      if (!live.count(it->first)) {
        it = copy.erase(it);
      } else {
        ++it;
      }
    }
  }
  try {
    save_health_cache(health_cache_path(), copy);
  } catch (...) {
  }
}

}  // namespace fatty

#include "ui/health_monitor.hpp"

#include "core/paths.hpp"
#include "core/util.hpp"

#include <wx/app.h>

#include <cmath>
#include <functional>
#include <thread>

namespace fatty {

HealthMonitor::HealthMonitor(Deps deps) : deps_(std::move(deps)) {
  entries_ = load_health_cache(health_cache_path());
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
  auto it = entries_.find(server_id);
  if (it == entries_.end()) return {};
  return it->second.latest;
}

HealthSnapshot HealthMonitor::previous(const std::string& server_id) const {
  std::lock_guard lock(mutex_);
  auto it = entries_.find(server_id);
  if (it == entries_.end() || it->second.history.empty()) return {};
  return it->second.history.front();
}

std::map<std::string, HealthSnapshot> HealthMonitor::snapshots() const {
  std::lock_guard lock(mutex_);
  std::map<std::string, HealthSnapshot> out;
  for (const auto& [id, entry] : entries_) out[id] = entry.latest;
  return out;
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
  t.swap_warn = st.health_swap_warn;
  t.swap_crit = st.health_swap_crit;
  t.cpu_warn = st.health_cpu_warn;
  t.cpu_crit = st.health_cpu_crit;
  t.load_warn = st.health_load_warn;
  t.load_crit = st.health_load_crit;
  return t;
}

HealthCollect HealthMonitor::collect_from(const AppSettings& st) const {
  return {st.health_show_cpu, st.health_show_ram, st.health_show_disk, st.health_show_load,
          st.health_show_docker_disks, st.health_show_swap};
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
        auto it = entries_.find(s.id);
        if (it != entries_.end()) prev = it->second.latest;
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
    auto& snap = entries_[server.id].latest;
    snap.server_id = server.id;
    snap.server_name = server.name;
    snap.checking = true;
    // level не трогаем: UI рисует «проверка…» по checking_id_, а метрики остаются для истории
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
      auto result = session->run(
          server, script, timeout, false,
          [&captured](const std::string& chunk) { captured.append(chunk); }, "", shell, false);
      snap = parse_health_output(captured);
      snap.server_id = server.id;
      snap.server_name = server.name;
      snap.checked_at = unix_now();
      if (snap.level != HealthLevel::Unsupported) {
        apply_health_thresholds(snap, thresholds);
      }
      if (snap.cpu_pct < 0 && snap.mem_pct < 0 && snap.swap_pct < 0 && snap.disks.empty() && snap.load1 < 0 &&
          snap.uptime_sec < 0) {
        if (result.exit_code == 124) {
          snap.level = HealthLevel::Unknown;
          snap.error = "таймаут проверки";
        } else if (result.exit_code == 130) {
          snap.level = HealthLevel::Unknown;
          snap.error = "проверка прервана";
        }
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
  std::map<std::string, HealthCacheEntry> copy;
  {
    std::lock_guard lock(mutex_);
    auto& entry = entries_[snap.server_id];
    HealthSnapshot prior = entry.latest;
    prior.checking = false;
    if (prior.checked_at > 0 && snap.checked_at > 0 &&
        std::fabs(prior.checked_at - snap.checked_at) >= 0.5) {
      health_history_push(entry, std::move(prior));
    }
    snap.checking = false;
    entry.latest = std::move(snap);
    copy = entries_;
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

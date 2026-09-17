#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fatty {

inline constexpr int kHealthIntervalMin = 300;
inline constexpr int kHealthIntervalMax = 30 * 24 * 3600;
inline constexpr int kHealthIntervalDefault = 24 * 3600;
inline constexpr int kHealthTimeoutMin = 5;
inline constexpr int kHealthTimeoutMax = 120;
inline constexpr int kHealthTimeoutDefault = 20;

enum class HealthLevel {
  Unknown = 0,
  Ok,
  Warn,
  Crit,
  Offline,
  Unsupported,
  Checking,
};

struct HealthDisk {
  std::string mount;
  long long used_kb = 0;
  long long total_kb = 0;
  double pct() const;
};

struct HealthThresholds {
  int disk_warn = 80;
  int disk_crit = 90;
  int ram_warn = 80;
  int ram_crit = 90;
  int cpu_warn = 80;
  int cpu_crit = 95;
};

struct HealthCollect {
  bool cpu = true;
  bool ram = true;
  bool disk = true;
  bool load = true;
  bool docker_disks = false;
};

struct HealthSnapshot {
  std::string server_id;
  std::string server_name;
  HealthLevel level = HealthLevel::Unknown;
  bool checking = false;
  std::string error;
  int nproc = 0;
  double load1 = -1;
  double load5 = -1;
  double load15 = -1;
  double cpu_pct = -1;
  long long mem_total_kb = 0;
  long long mem_avail_kb = 0;
  double mem_pct = -1;
  double uptime_sec = -1;
  std::vector<HealthDisk> disks;
  double checked_at = 0;
};

HealthLevel worse_health(HealthLevel a, HealthLevel b);
HealthLevel level_from_pct(double pct, int warn, int crit);
void apply_health_thresholds(HealthSnapshot& snap, const HealthThresholds& thresholds);

HealthSnapshot parse_health_output(std::string_view text);
std::string health_remote_script(const HealthCollect& collect);

bool health_is_due(const HealthSnapshot& snap, int interval_sec, double now_unix);
double unix_now();

const HealthDisk* health_root_or_worst(const HealthSnapshot& snap);
bool health_mount_is_virtual(std::string_view mount);
void health_apply_virtual_disks(HealthSnapshot& snap, bool show_virtual, const HealthThresholds& thresholds);

std::string health_level_id(HealthLevel level);
HealthLevel health_level_from_id(std::string_view id);
std::string health_level_label(HealthLevel level);

std::string format_kib(long long kb);
std::string format_pct(double pct);
std::string format_uptime_sec(double seconds);
std::string format_health_when(double unix_ts, double now_unix = 0);
std::string format_interval_label(int seconds);

std::map<std::string, HealthSnapshot> load_health_cache(const std::filesystem::path& path);
void save_health_cache(const std::filesystem::path& path, const std::map<std::string, HealthSnapshot>& snaps);

}  // namespace fatty

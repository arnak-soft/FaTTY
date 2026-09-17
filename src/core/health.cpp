#include "core/health.hpp"

#include "core/util.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

namespace fatty {
using json = nlohmann::json;

namespace {

std::vector<std::string> split_ws(std::string_view line) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : line) {
    if (ch == ' ' || ch == '\t') {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else {
      cur += ch;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool parse_double(std::string_view raw, double& out) {
  if (raw.empty()) return false;
  try {
    size_t idx = 0;
    std::string s(raw);
    out = std::stod(s, &idx);
    return idx > 0;
  } catch (...) {
    return false;
  }
}

bool parse_ll(std::string_view raw, long long& out) {
  if (raw.empty()) return false;
  try {
    size_t idx = 0;
    std::string s(raw);
    out = std::stoll(s, &idx);
    return idx > 0;
  } catch (...) {
    return false;
  }
}

std::tm local_from_unix(double unix_ts) {
  const auto sec = static_cast<std::time_t>(unix_ts);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &sec);
#else
  localtime_r(&sec, &tm);
#endif
  return tm;
}

bool health_has_readings(const HealthSnapshot& snap) {
  return snap.nproc > 0 || snap.cpu_pct >= 0 || snap.mem_pct >= 0 || snap.mem_total_kb > 0 || !snap.disks.empty() ||
         snap.load1 >= 0 || snap.uptime_sec >= 0;
}

}  // namespace

double HealthDisk::pct() const {
  if (total_kb <= 0) return -1;
  return 100.0 * static_cast<double>(used_kb) / static_cast<double>(total_kb);
}

HealthLevel worse_health(HealthLevel a, HealthLevel b) {
  auto rank = [](HealthLevel l) {
    switch (l) {
      case HealthLevel::Crit:
      case HealthLevel::Offline:
        return 4;
      case HealthLevel::Warn:
        return 3;
      case HealthLevel::Ok:
        return 2;
      case HealthLevel::Unsupported:
        return 1;
      case HealthLevel::Checking:
      case HealthLevel::Unknown:
      default:
        return 0;
    }
  };
  return rank(a) >= rank(b) ? a : b;
}

HealthLevel level_from_pct(double pct, int warn, int crit) {
  if (pct < 0) return HealthLevel::Unknown;
  if (pct >= static_cast<double>(crit)) return HealthLevel::Crit;
  if (pct >= static_cast<double>(warn)) return HealthLevel::Warn;
  return HealthLevel::Ok;
}

void apply_health_thresholds(HealthSnapshot& snap, const HealthThresholds& thresholds) {
  if (snap.level == HealthLevel::Offline || snap.level == HealthLevel::Unsupported ||
      snap.level == HealthLevel::Checking) {
    return;
  }
  HealthLevel lvl = HealthLevel::Unknown;
  if (snap.cpu_pct >= 0) {
    lvl = worse_health(lvl, level_from_pct(snap.cpu_pct, thresholds.cpu_warn, thresholds.cpu_crit));
  }
  if (snap.mem_pct >= 0) {
    lvl = worse_health(lvl, level_from_pct(snap.mem_pct, thresholds.ram_warn, thresholds.ram_crit));
  }
  for (const auto& disk : snap.disks) {
    lvl = worse_health(lvl, level_from_pct(disk.pct(), thresholds.disk_warn, thresholds.disk_crit));
  }
  if (lvl == HealthLevel::Unknown && snap.checked_at > 0 && snap.error.empty() && health_has_readings(snap)) {
    lvl = HealthLevel::Ok;
  }
  snap.level = lvl;
}

HealthSnapshot parse_health_output(std::string_view text) {
  HealthSnapshot snap;
  std::vector<std::string> lines;
  std::string cur;
  for (char ch : text) {
    if (ch == '\n') {
      lines.push_back(std::move(cur));
      cur.clear();
    } else {
      cur += ch;
    }
  }
  if (!cur.empty() || (!text.empty() && text.back() != '\n')) {
    lines.push_back(std::move(cur));
  }

  auto line_text = [](const std::string& raw) {
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return trim(line);
  };

  int start = -1;
  int last_start = -1;
  int last_end = -1;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    const auto t = line_text(lines[static_cast<std::size_t>(i)]);
    if (t == "FATTYHEALTH v1") {
      start = i;
    } else if (t == "FATTYHEALTH_END" && start >= 0) {
      last_start = start;
      last_end = i;
    }
  }
  if (last_start < 0) {
    if (start >= 0) {
      last_start = start;
      last_end = static_cast<int>(lines.size());
    } else {
      snap.error = "нет метки FATTYHEALTH";
      snap.level = HealthLevel::Unknown;
      return snap;
    }
  }

  bool other_os = false;
  for (int i = last_start; i < last_end; ++i) {
    auto line = line_text(lines[static_cast<std::size_t>(i)]);
    if (line.empty() || line == "FATTYHEALTH v1" || line == "FATTYHEALTH_END") continue;
    if (line == "os=other") {
      other_os = true;
      continue;
    }
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    auto key = line.substr(0, eq);
    auto val = line.substr(eq + 1);
    if (key == "nproc") {
      int n = 0;
      if (parse_int(val, n) && n > 0) snap.nproc = n;
    } else if (key == "load1") {
      parse_double(val, snap.load1);
    } else if (key == "load5") {
      parse_double(val, snap.load5);
    } else if (key == "load15") {
      parse_double(val, snap.load15);
    } else if (key == "cpu") {
      parse_double(val, snap.cpu_pct);
    } else if (key == "mem_total_kb") {
      parse_ll(val, snap.mem_total_kb);
    } else if (key == "mem_avail_kb") {
      parse_ll(val, snap.mem_avail_kb);
    } else if (key == "uptime") {
      parse_double(val, snap.uptime_sec);
    } else if (key == "disk") {
      auto parts = split_ws(val);
      if (parts.size() < 3) continue;
      HealthDisk disk;
      if (!parse_ll(parts[parts.size() - 2], disk.used_kb)) continue;
      if (!parse_ll(parts.back(), disk.total_kb)) continue;
      disk.mount.clear();
      for (std::size_t j = 0; j + 2 < parts.size(); ++j) {
        if (!disk.mount.empty()) disk.mount += " ";
        disk.mount += parts[j];
      }
      if (disk.mount.empty()) continue;
      snap.disks.push_back(std::move(disk));
    }
  }
  if (snap.mem_total_kb > 0) {
    const auto used = snap.mem_total_kb - std::min(snap.mem_avail_kb, snap.mem_total_kb);
    snap.mem_pct = 100.0 * static_cast<double>(used) / static_cast<double>(snap.mem_total_kb);
  }
  if (other_os) {
    snap.level = HealthLevel::Unsupported;
    snap.error = "не Linux";
  }
  return snap;
}

std::string health_remote_script(const HealthCollect& collect) {
  std::string s;
  s += "if [ ! -r /proc/meminfo ] || [ ! -r /proc/loadavg ]; then\n";
  s += "printf '%s\\n' 'FATTYHEALTH v1'\n";
  s += "printf '%s\\n' 'os=other'\n";
  s += "printf '%s\\n' 'FATTYHEALTH_END'\n";
  s += "exit 0\n";
  s += "fi\n";
  s += "nproc=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)\n";
  s += "printf '%s\\n' 'FATTYHEALTH v1'\n";
  s += "printf 'nproc=%s\\n' \"$nproc\"\n";
  if (collect.load) {
    s += "read load1 load5 load15 _rest < /proc/loadavg\n";
    s += "printf 'load1=%s\\n' \"$load1\"\n";
    s += "printf 'load5=%s\\n' \"$load5\"\n";
    s += "printf 'load15=%s\\n' \"$load15\"\n";
  }
  s += "read uptime _idle < /proc/uptime\n";
  s += "printf 'uptime=%s\\n' \"$uptime\"\n";
  if (collect.ram) {
    s += "mem_total_kb=$(awk '/^MemTotal:/ {print $2; exit}' /proc/meminfo 2>/dev/null)\n";
    s += "mem_avail_kb=$(awk '/^MemAvailable:/ {print $2; exit}' /proc/meminfo 2>/dev/null)\n";
    s += "if [ -z \"$mem_avail_kb\" ]; then\n";
    s += "mem_avail_kb=$(awk '/^MemFree:/ {f=$2} /^Buffers:/ {b=$2} /^Cached:/ {c=$2} END {print f+b+c}' /proc/meminfo 2>/dev/null)\n";
    s += "fi\n";
    s += "printf 'mem_total_kb=%s\\n' \"$mem_total_kb\"\n";
    s += "printf 'mem_avail_kb=%s\\n' \"$mem_avail_kb\"\n";
  }
  if (collect.cpu) {
    s += "if [ -r /proc/stat ]; then\n";
    s += "set -- $(head -n 1 /proc/stat)\n";
    s += "shift\n";
    s += "u1=$1 n1=$2 sy1=$3 i1=$4 w1=${5:-0} irq1=${6:-0} sirq1=${7:-0} st1=${8:-0}\n";
    s += "tot1=$((u1+n1+sy1+i1+w1+irq1+sirq1+st1))\n";
    s += "idle1=$((i1+w1))\n";
    s += "sleep 0.2 2>/dev/null || sleep 1\n";
    s += "set -- $(head -n 1 /proc/stat)\n";
    s += "shift\n";
    s += "u2=$1 n2=$2 sy2=$3 i2=$4 w2=${5:-0} irq2=${6:-0} sirq2=${7:-0} st2=${8:-0}\n";
    s += "tot2=$((u2+n2+sy2+i2+w2+irq2+sirq2+st2))\n";
    s += "idle2=$((i2+w2))\n";
    s += "d=$((tot2-tot1))\n";
    s += "di=$((idle2-idle1))\n";
    s += "if [ \"$d\" -gt 0 ]; then\n";
    s += "cpu=$(awk -v d=\"$d\" -v di=\"$di\" 'BEGIN { printf \"%.1f\", (1-di/d)*100 }')\n";
    s += "printf 'cpu=%s\\n' \"$cpu\"\n";
    s += "fi\n";
    s += "fi\n";
  }
  if (collect.disk) {
    s += "df -Pk 2>/dev/null | awk 'NR>1 {\n";
    s += "  fs=$1; total=$2; used=$3; mp=$6;\n";
    s += "  if (fs ~ /^(tmpfs|devtmpfs|devfs|squashfs|proc|sysfs|cgroup)/) next;\n";
    s += "  if (mp ~ /^(\\/run|\\/sys|\\/proc|\\/dev)(\\/|$)/) next;\n";
    if (!collect.docker_disks) {
      s += "  if (fs ~ /^(overlay|fuse\\.overlayfs)/) next;\n";
      s += "  if (mp ~ /\\/overlay2\\//) next;\n";
      s += "  if (mp ~ /^(\\/var\\/lib\\/containerd|\\/var\\/lib\\/containers\\/storage\\/overlay|\\/run\\/containerd|\\/snap)(\\/|$)/) next;\n";
    }
    s += "  if (mp==\"\") next;\n";
    s += "  printf \"disk=%s %s %s\\n\", mp, used, total;\n";
    s += "}'\n";
  }
  s += "printf '%s\\n' 'FATTYHEALTH_END'\n";
  return s;
}

bool health_is_due(const HealthSnapshot& snap, int interval_sec, double now_unix) {
  if (interval_sec < 1) interval_sec = kHealthIntervalDefault;
  if (snap.checked_at <= 0) return true;
  return (now_unix - snap.checked_at) >= static_cast<double>(interval_sec);
}

double unix_now() {
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

const HealthDisk* health_root_or_worst(const HealthSnapshot& snap) {
  const HealthDisk* root = nullptr;
  const HealthDisk* worst = nullptr;
  double worst_pct = -1;
  for (const auto& d : snap.disks) {
    if (d.mount == "/") root = &d;
    const auto p = d.pct();
    if (p > worst_pct) {
      worst_pct = p;
      worst = &d;
    }
  }
  if (root) return root;
  return worst;
}

bool health_mount_is_virtual(std::string_view mount) {
  if (mount.find("/overlay2/") != std::string_view::npos) return true;
  if (mount.find("/overlay/") != std::string_view::npos &&
      (mount.find("docker") != std::string_view::npos || mount.find("container") != std::string_view::npos)) {
    return true;
  }
  const std::string_view prefixes[] = {
      "/var/lib/docker/overlay", "/var/lib/containerd", "/var/lib/containers/storage/overlay",
      "/run/containerd",         "/snap",
  };
  for (auto prefix : prefixes) {
    if (mount.size() < prefix.size()) continue;
    if (mount.substr(0, prefix.size()) != prefix) continue;
    if (mount.size() == prefix.size() || mount[prefix.size()] == '/') return true;
  }
  return false;
}

void health_apply_virtual_disks(HealthSnapshot& snap, bool show_virtual, const HealthThresholds& thresholds) {
  if (show_virtual) return;
  const auto before = snap.disks.size();
  snap.disks.erase(std::remove_if(snap.disks.begin(), snap.disks.end(),
                                  [](const HealthDisk& d) { return health_mount_is_virtual(d.mount); }),
                   snap.disks.end());
  if (snap.disks.size() == before) return;
  apply_health_thresholds(snap, thresholds);
}

std::string health_level_id(HealthLevel level) {
  switch (level) {
    case HealthLevel::Ok:
      return "ok";
    case HealthLevel::Warn:
      return "warn";
    case HealthLevel::Crit:
      return "crit";
    case HealthLevel::Offline:
      return "offline";
    case HealthLevel::Unsupported:
      return "unsupported";
    case HealthLevel::Checking:
      return "checking";
    case HealthLevel::Unknown:
    default:
      return "unknown";
  }
}

HealthLevel health_level_from_id(std::string_view id) {
  if (id == "ok") return HealthLevel::Ok;
  if (id == "warn") return HealthLevel::Warn;
  if (id == "crit") return HealthLevel::Crit;
  if (id == "offline") return HealthLevel::Offline;
  if (id == "unsupported") return HealthLevel::Unsupported;
  if (id == "checking") return HealthLevel::Checking;
  return HealthLevel::Unknown;
}

std::string health_level_label(HealthLevel level) {
  switch (level) {
    case HealthLevel::Ok:
      return "норма";
    case HealthLevel::Warn:
      return "средне";
    case HealthLevel::Crit:
      return "высокое";
    case HealthLevel::Offline:
      return "нет связи";
    case HealthLevel::Unsupported:
      return "не Linux";
    case HealthLevel::Checking:
      return "проверка…";
    case HealthLevel::Unknown:
    default:
      return "нет данных";
  }
}

std::string format_kib(long long kb) {
  if (kb < 0) kb = 0;
  std::ostringstream ss;
  ss.setf(std::ios::fixed);
  if (kb < 1024) {
    return std::to_string(kb) + " КБ";
  }
  const double mb = static_cast<double>(kb) / 1024.0;
  if (mb < 1024.0) {
    ss.precision(mb >= 10 ? 0 : 1);
    ss << mb << " МБ";
    return ss.str();
  }
  const double gb = mb / 1024.0;
  ss.precision(gb >= 10 ? 1 : 2);
  ss << gb << " ГБ";
  return ss.str();
}

std::string format_pct(double pct) {
  if (pct < 0) return "—";
  std::ostringstream ss;
  ss.setf(std::ios::fixed);
  ss.precision(pct >= 10 ? 0 : 1);
  ss << pct << "%";
  return ss.str();
}

std::string format_uptime_sec(double seconds) {
  if (seconds < 0) return "—";
  const auto total = static_cast<long long>(seconds);
  const auto days = total / 86400;
  const auto hours = (total % 86400) / 3600;
  const auto mins = (total % 3600) / 60;
  if (days > 0) return std::to_string(days) + " д " + std::to_string(hours) + " ч";
  if (hours > 0) return std::to_string(hours) + " ч " + std::to_string(mins) + " мин";
  return std::to_string(mins) + " мин";
}

std::string format_health_when(double unix_ts, double now_unix) {
  if (unix_ts <= 0) return "ещё не проверялся";
  if (now_unix <= 0) now_unix = unix_now();
  const double ago = now_unix - unix_ts;
  if (ago < 45) return "только что";
  if (ago < 90) return "минуту назад";
  if (ago < 3600) return std::to_string(static_cast<int>(ago / 60)) + " мин назад";
  if (ago < 90 * 60) return "час назад";
  if (ago < 24 * 3600) return std::to_string(static_cast<int>(ago / 3600)) + " ч назад";
  auto tm = local_from_unix(unix_ts);
  std::ostringstream ss;
  ss << std::put_time(&tm, "%d.%m.%Y %H:%M");
  return ss.str();
}

std::string format_interval_label(int seconds) {
  seconds = clamp_int(seconds, kHealthIntervalMin, kHealthIntervalMax);
  if (seconds % 86400 == 0) {
    const int days = seconds / 86400;
    if (days == 1) return "1 день";
    if (days == 2 || days == 3 || days == 4) return std::to_string(days) + " дня";
    return std::to_string(days) + " дней";
  }
  if (seconds % 3600 == 0) {
    const int hours = seconds / 3600;
    if (hours == 1) return "1 час";
    if (hours == 2 || hours == 3 || hours == 4) return std::to_string(hours) + " часа";
    return std::to_string(hours) + " часов";
  }
  if (seconds % 60 == 0) return std::to_string(seconds / 60) + " мин";
  return std::to_string(seconds) + " с";
}

namespace {

HealthSnapshot snapshot_from_json(const json& raw, const std::string& id) {
  HealthSnapshot snap;
  snap.server_id = id;
  snap.server_name = raw.value("name", "");
  snap.level = health_level_from_id(raw.value("level", std::string("unknown")));
  snap.error = raw.value("error", "");
  snap.nproc = raw.value("nproc", 0);
  snap.load1 = raw.value("load1", -1.0);
  snap.load5 = raw.value("load5", -1.0);
  snap.load15 = raw.value("load15", -1.0);
  snap.cpu_pct = raw.value("cpu", -1.0);
  snap.mem_total_kb = raw.value("mem_total_kb", 0LL);
  snap.mem_avail_kb = raw.value("mem_avail_kb", 0LL);
  snap.mem_pct = raw.value("mem_pct", -1.0);
  snap.uptime_sec = raw.value("uptime", -1.0);
  snap.checked_at = raw.value("checked_at", 0.0);
  if (snap.mem_pct < 0 && snap.mem_total_kb > 0) {
    const auto used = snap.mem_total_kb - std::min(snap.mem_avail_kb, snap.mem_total_kb);
    snap.mem_pct = 100.0 * static_cast<double>(used) / static_cast<double>(snap.mem_total_kb);
  }
  if (raw.contains("disks") && raw["disks"].is_array()) {
    for (const auto& d : raw["disks"]) {
      if (!d.is_object()) continue;
      HealthDisk disk;
      disk.mount = d.value("mp", "");
      disk.used_kb = d.value("used", 0LL);
      disk.total_kb = d.value("total", 0LL);
      if (!disk.mount.empty()) snap.disks.push_back(std::move(disk));
    }
  }
  if (snap.level == HealthLevel::Ok && !health_has_readings(snap)) {
    snap.level = HealthLevel::Unknown;
  }
  return snap;
}

json snapshot_to_json(const HealthSnapshot& snap) {
  json disks = json::array();
  for (const auto& d : snap.disks) {
    disks.push_back({{"mp", d.mount}, {"used", d.used_kb}, {"total", d.total_kb}});
  }
  return {
      {"name", snap.server_name},
      {"level", health_level_id(snap.level)},
      {"error", snap.error},
      {"nproc", snap.nproc},
      {"load1", snap.load1},
      {"load5", snap.load5},
      {"load15", snap.load15},
      {"cpu", snap.cpu_pct},
      {"mem_total_kb", snap.mem_total_kb},
      {"mem_avail_kb", snap.mem_avail_kb},
      {"mem_pct", snap.mem_pct},
      {"uptime", snap.uptime_sec},
      {"checked_at", snap.checked_at},
      {"disks", std::move(disks)},
  };
}

}  // namespace

std::map<std::string, HealthSnapshot> load_health_cache(const std::filesystem::path& path) {
  std::map<std::string, HealthSnapshot> out;
  if (!std::filesystem::exists(path)) return out;
  json data;
  try {
    data = json::parse(read_text_file(path), nullptr, true, true);
  } catch (...) {
    return out;
  }
  if (!data.is_object()) return out;
  json servers = data.value("servers", json::object());
  if (!servers.is_object()) return out;
  for (auto it = servers.begin(); it != servers.end(); ++it) {
    if (!it.value().is_object()) continue;
    auto snap = snapshot_from_json(it.value(), it.key());
    if (!snap.server_id.empty()) out[snap.server_id] = std::move(snap);
  }
  return out;
}

void save_health_cache(const std::filesystem::path& path, const std::map<std::string, HealthSnapshot>& snaps) {
  json servers = json::object();
  for (const auto& [id, snap] : snaps) {
    if (id.empty()) continue;
    servers[id] = snapshot_to_json(snap);
  }
  json payload = {{"v", 1}, {"servers", std::move(servers)}};
  atomic_write_text(path, payload.dump(2));
}

}  // namespace fatty

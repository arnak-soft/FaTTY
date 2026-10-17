#include "core/health.hpp"

#include "core/util.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
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
  return snap.nproc > 0 || snap.cpu_pct >= 0 || snap.iowait_pct >= 0 || snap.steal_pct >= 0 || snap.mem_pct >= 0 ||
         snap.mem_total_kb > 0 || snap.swap_total_kb > 0 || snap.swap_pct >= 0 || !snap.disks.empty() ||
         snap.load1 >= 0 || snap.uptime_sec >= 0 || snap.reboot_required || !snap.failed_units.empty() ||
         snap.docker_present || snap.docker_running >= 0 || snap.oom_count > 0 || snap.net_rx_bytes >= 0 ||
         snap.ssh_fail_count >= 0 || snap.time_synced >= 0 || !snap.top_mem.empty() || !snap.top_cpu.empty();
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

double health_load_pct(const HealthSnapshot& snap) {
  if (snap.nproc <= 0) return -1;
  double load = -1;
  if (snap.load15 >= 0) load = snap.load15;
  else if (snap.load5 >= 0) load = snap.load5;
  else if (snap.load1 >= 0) load = snap.load1;
  if (load < 0) return -1;
  return 100.0 * load / static_cast<double>(snap.nproc);
}

void apply_health_thresholds(HealthSnapshot& snap, const HealthThresholds& thresholds,
                             const HealthAlertIgnore& ignore) {
  if (snap.level == HealthLevel::Offline || snap.level == HealthLevel::Unsupported ||
      snap.level == HealthLevel::Checking) {
    return;
  }
  HealthLevel lvl = HealthLevel::Unknown;
  if (snap.cpu_pct >= 0) {
    lvl = worse_health(lvl, level_from_pct(snap.cpu_pct, thresholds.cpu_warn, thresholds.cpu_crit));
  }
  if (snap.iowait_pct >= 0) {
    lvl = worse_health(lvl, level_from_pct(snap.iowait_pct, thresholds.iowait_warn, thresholds.iowait_crit));
  }
  if (snap.steal_pct >= 0) {
    lvl = worse_health(lvl, level_from_pct(snap.steal_pct, thresholds.steal_warn, thresholds.steal_crit));
  }
  if (snap.mem_pct >= 0) {
    lvl = worse_health(lvl, level_from_pct(snap.mem_pct, thresholds.ram_warn, thresholds.ram_crit));
  }
  if (snap.swap_pct >= 0) {
    lvl = worse_health(lvl, level_from_pct(snap.swap_pct, thresholds.swap_warn, thresholds.swap_crit));
  }
  for (const auto& disk : snap.disks) {
    lvl = worse_health(lvl, level_from_pct(disk.pct(), thresholds.disk_warn, thresholds.disk_crit));
  }
  const double load_pct = health_load_pct(snap);
  if (load_pct >= 0) {
    lvl = worse_health(lvl, level_from_pct(load_pct, thresholds.load_warn, thresholds.load_crit));
  }
  if (!ignore.reboot && snap.reboot_required) {
    lvl = worse_health(lvl, HealthLevel::Warn);
  }
  if (!ignore.failed_units && !snap.failed_units.empty()) {
    const auto n = static_cast<int>(snap.failed_units.size());
    lvl = worse_health(lvl, n >= 3 ? HealthLevel::Crit : HealthLevel::Warn);
  }
  if (!ignore.docker && snap.docker_unhealthy > 0) {
    lvl = worse_health(lvl, snap.docker_unhealthy >= 3 ? HealthLevel::Crit : HealthLevel::Warn);
  }
  if (!ignore.oom && snap.oom_count > 0) {
    lvl = worse_health(lvl, snap.oom_count >= 3 ? HealthLevel::Crit : HealthLevel::Warn);
  }
  if (!ignore.ssh && snap.ssh_fail_count >= 0) {
    if (snap.ssh_fail_count >= thresholds.ssh_crit) {
      lvl = worse_health(lvl, HealthLevel::Crit);
    } else if (snap.ssh_fail_count >= thresholds.ssh_warn) {
      lvl = worse_health(lvl, HealthLevel::Warn);
    }
  }
  if (!ignore.time) {
    if (snap.time_synced == 0) {
      lvl = worse_health(lvl, HealthLevel::Warn);
    }
    if (snap.time_offset_known) {
      const double off = std::fabs(snap.time_offset_sec);
      if (off >= kHealthTimeOffsetCritSec) {
        lvl = worse_health(lvl, HealthLevel::Crit);
      } else if (off >= kHealthTimeOffsetWarnSec) {
        lvl = worse_health(lvl, HealthLevel::Warn);
      }
    }
  }
  if (!ignore.net) {
    if (snap.net_err_delta >= kHealthNetErrCrit) {
      lvl = worse_health(lvl, HealthLevel::Crit);
    } else if (snap.net_err_delta >= kHealthNetErrWarn) {
      lvl = worse_health(lvl, HealthLevel::Warn);
    }
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
    } else if (key == "iowait") {
      parse_double(val, snap.iowait_pct);
    } else if (key == "steal") {
      parse_double(val, snap.steal_pct);
    } else if (key == "mem_total_kb") {
      parse_ll(val, snap.mem_total_kb);
    } else if (key == "mem_avail_kb") {
      parse_ll(val, snap.mem_avail_kb);
    } else if (key == "swap_total_kb") {
      parse_ll(val, snap.swap_total_kb);
    } else if (key == "swap_free_kb") {
      parse_ll(val, snap.swap_free_kb);
    } else if (key == "uptime") {
      parse_double(val, snap.uptime_sec);
    } else if (key == "reboot_required") {
      int flag = 0;
      if (parse_int(val, flag)) snap.reboot_required = flag != 0;
    } else if (key == "failed_unit") {
      auto name = trim(val);
      if (!name.empty() && static_cast<int>(snap.failed_units.size()) < kHealthFailedUnitsMax) {
        snap.failed_units.push_back(std::move(name));
      }
    } else if (key == "docker") {
      int flag = 0;
      if (parse_int(val, flag)) {
        snap.docker_present = flag != 0;
        if (snap.docker_present) {
          if (snap.docker_running < 0) snap.docker_running = 0;
          if (snap.docker_exited < 0) snap.docker_exited = 0;
          if (snap.docker_unhealthy < 0) snap.docker_unhealthy = 0;
        }
      }
    } else if (key == "docker_running") {
      int n = 0;
      if (parse_int(val, n) && n >= 0) snap.docker_running = n;
    } else if (key == "docker_exited") {
      int n = 0;
      if (parse_int(val, n) && n >= 0) snap.docker_exited = n;
    } else if (key == "docker_unhealthy") {
      int n = 0;
      if (parse_int(val, n) && n >= 0) snap.docker_unhealthy = n;
    } else if (key == "docker_exited_name") {
      auto name = trim(val);
      if (!name.empty() && static_cast<int>(snap.docker_exited_names.size()) < kHealthDockerNamesMax) {
        snap.docker_exited_names.push_back(std::move(name));
      }
    } else if (key == "docker_unhealthy_name") {
      auto name = trim(val);
      if (!name.empty() && static_cast<int>(snap.docker_unhealthy_names.size()) < kHealthDockerNamesMax) {
        snap.docker_unhealthy_names.push_back(std::move(name));
      }
    } else if (key == "oom_count") {
      int n = 0;
      if (parse_int(val, n) && n >= 0) snap.oom_count = n;
    } else if (key == "oom_last") {
      snap.oom_last = trim(val);
    } else if (key == "net_rx_bytes") {
      long long n = 0;
      if (parse_ll(val, n) && n >= 0) snap.net_rx_bytes = n;
    } else if (key == "net_tx_bytes") {
      long long n = 0;
      if (parse_ll(val, n) && n >= 0) snap.net_tx_bytes = n;
    } else if (key == "net_rx_errs") {
      long long n = 0;
      if (parse_ll(val, n) && n >= 0) snap.net_rx_errs = n;
    } else if (key == "net_tx_errs") {
      long long n = 0;
      if (parse_ll(val, n) && n >= 0) snap.net_tx_errs = n;
    } else if (key == "net_rx_drop") {
      long long n = 0;
      if (parse_ll(val, n) && n >= 0) snap.net_rx_drop = n;
    } else if (key == "net_tx_drop") {
      long long n = 0;
      if (parse_ll(val, n) && n >= 0) snap.net_tx_drop = n;
    } else if (key == "net_if") {
      snap.net_if = trim(val);
    } else if (key == "ssh_fail_count") {
      int n = 0;
      if (parse_int(val, n) && n >= 0) snap.ssh_fail_count = n;
    } else if (key == "time_synced") {
      int n = 0;
      if (parse_int(val, n) && (n == 0 || n == 1)) snap.time_synced = n;
    } else if (key == "time_offset_sec") {
      double off = 0;
      if (parse_double(val, off)) {
        snap.time_offset_known = true;
        snap.time_offset_sec = off;
      }
    } else if (key == "top_mem" || key == "top_cpu") {
      auto parts = split_ws(val);
      if (parts.size() >= 4) {
        HealthProc proc;
        if (!parse_int(parts[0], proc.pid) || proc.pid <= 0) continue;
        if (!parse_ll(parts[1], proc.rss_kb) || proc.rss_kb < 0) continue;
        if (!parse_double(parts[2], proc.cpu_pct)) continue;
        proc.name = parts[3];
        for (std::size_t j = 4; j < parts.size(); ++j) {
          proc.name += " ";
          proc.name += parts[j];
        }
        auto& dest = key == "top_mem" ? snap.top_mem : snap.top_cpu;
        if (static_cast<int>(dest.size()) < kHealthTopProcsMax) dest.push_back(std::move(proc));
      }
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
  if (snap.swap_total_kb > 0) {
    const auto free = std::min(snap.swap_free_kb, snap.swap_total_kb);
    const auto used = snap.swap_total_kb - free;
    snap.swap_pct = 100.0 * static_cast<double>(used) / static_cast<double>(snap.swap_total_kb);
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
  if (collect.swap) {
    s += "swap_total_kb=$(awk '/^SwapTotal:/ {print $2; exit}' /proc/meminfo 2>/dev/null)\n";
    s += "swap_free_kb=$(awk '/^SwapFree:/ {print $2; exit}' /proc/meminfo 2>/dev/null)\n";
    s += "printf 'swap_total_kb=%s\\n' \"${swap_total_kb:-0}\"\n";
    s += "printf 'swap_free_kb=%s\\n' \"${swap_free_kb:-0}\"\n";
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
    s += "dw=$((w2-w1))\n";
    s += "dst=$((st2-st1))\n";
    s += "if [ \"$d\" -gt 0 ]; then\n";
    s += "cpu=$(awk -v d=\"$d\" -v di=\"$di\" 'BEGIN { printf \"%.1f\", (1-di/d)*100 }')\n";
    s += "iowait=$(awk -v d=\"$d\" -v dw=\"$dw\" 'BEGIN { printf \"%.1f\", (dw/d)*100 }')\n";
    s += "steal=$(awk -v d=\"$d\" -v dst=\"$dst\" 'BEGIN { printf \"%.1f\", (dst/d)*100 }')\n";
    s += "printf 'cpu=%s\\n' \"$cpu\"\n";
    s += "printf 'iowait=%s\\n' \"$iowait\"\n";
    s += "printf 'steal=%s\\n' \"$steal\"\n";
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
  s += "if [ -f /var/run/reboot-required ] || [ -f /run/reboot-required ]; then\n";
  s += "printf 'reboot_required=1\\n'\n";
  s += "else\n";
  s += "printf 'reboot_required=0\\n'\n";
  s += "fi\n";
  s += "if command -v systemctl >/dev/null 2>&1; then\n";
  s += "systemctl list-units --state=failed --no-legend --no-pager 2>/dev/null | ";
  s += "awk 'NF { gsub(/^[ \\t]+|[ \\t]+$/, \"\", $1); if ($1 != \"\") { print \"failed_unit=\" $1; if (++c >= ";
  s += std::to_string(kHealthFailedUnitsMax);
  s += ") exit } }'\n";
  s += "fi\n";
  if (collect.docker) {
    s += "if command -v docker >/dev/null 2>&1 && docker ps -q >/dev/null 2>&1; then\n";
    s += "printf 'docker=1\\n'\n";
    s += "docker_running=$(docker ps -q 2>/dev/null | wc -l | tr -d ' \\t')\n";
    s += "docker_exited=$(docker ps -aq -f status=exited 2>/dev/null | wc -l | tr -d ' \\t')\n";
    s += "docker_unhealthy=$(docker ps -aq -f health=unhealthy 2>/dev/null | wc -l | tr -d ' \\t')\n";
    s += "printf 'docker_running=%s\\n' \"${docker_running:-0}\"\n";
    s += "printf 'docker_exited=%s\\n' \"${docker_exited:-0}\"\n";
    s += "printf 'docker_unhealthy=%s\\n' \"${docker_unhealthy:-0}\"\n";
    s += "docker ps -a --filter status=exited --format '{{.Names}}' 2>/dev/null | ";
    s += "awk 'NF { print \"docker_exited_name=\" $0; if (++c >= ";
    s += std::to_string(kHealthDockerNamesMax);
    s += ") exit }'\n";
    s += "docker ps -a --filter health=unhealthy --format '{{.Names}}' 2>/dev/null | ";
    s += "awk 'NF { print \"docker_unhealthy_name=\" $0; if (++c >= ";
    s += std::to_string(kHealthDockerNamesMax);
    s += ") exit }'\n";
    s += "else\n";
    s += "printf 'docker=0\\n'\n";
    s += "fi\n";
  }
  // Сеть: один проход /proc/net/dev, без внешних утилит.
  s += "if [ -r /proc/net/dev ]; then\n";
  s += "awk 'NR>2 {\n";
  s += "  name=$1; sub(/:$/,\"\",name);\n";
  s += "  if (name==\"\" || name==\"lo\") next;\n";
  s += "  rx=$2+0; re=$4+0; rd=$5+0; tx=$10+0; te=$12+0; td=$13+0;\n";
  s += "  RX+=rx; TX+=tx; RE+=re; TE+=te; RD+=rd; TD+=td;\n";
  s += "  if (iface==\"\" && (rx+tx)>0) iface=name;\n";
  s += "} END {\n";
  s += "  printf \"net_rx_bytes=%d\\n\", RX+0;\n";
  s += "  printf \"net_tx_bytes=%d\\n\", TX+0;\n";
  s += "  printf \"net_rx_errs=%d\\n\", RE+0;\n";
  s += "  printf \"net_tx_errs=%d\\n\", TE+0;\n";
  s += "  printf \"net_rx_drop=%d\\n\", RD+0;\n";
  s += "  printf \"net_tx_drop=%d\\n\", TD+0;\n";
  s += "  if (iface!=\"\") printf \"net_if=%s\\n\", iface;\n";
  s += "}' /proc/net/dev\n";
  s += "fi\n";

  // Топ процессов: два коротких ps (не чаще top -b).
  s += "if command -v ps >/dev/null 2>&1; then\n";
  s += "ps -eo pid=,rss=,pcpu=,comm= --sort=-rss 2>/dev/null | awk 'NF>=4 {\n";
  s += "  name=$4; for (i=5;i<=NF;i++) name=name \" \" $i;\n";
  s += "  printf \"top_mem=%s %s %s %s\\n\", $1, $2, $3, name;\n";
  s += "  if (++c >= ";
  s += std::to_string(kHealthTopProcsMax);
  s += ") exit\n";
  s += "}'\n";
  s += "ps -eo pid=,rss=,pcpu=,comm= --sort=-pcpu 2>/dev/null | awk 'NF>=4 {\n";
  s += "  name=$4; for (i=5;i<=NF;i++) name=name \" \" $i;\n";
  s += "  printf \"top_cpu=%s %s %s %s\\n\", $1, $2, $3, name;\n";
  s += "  if (++c >= ";
  s += std::to_string(kHealthTopProcsMax);
  s += ") exit\n";
  s += "}'\n";
  s += "fi\n";

  // OOM: journalctl -g, без прогона всего kern.log.
  s += "oom_count=0\n";
  s += "oom_last=\n";
  s += "if command -v journalctl >/dev/null 2>&1; then\n";
  s += "oom_lines=$(journalctl -k -b -g 'Out of memory|oom-kill|Killed process' -q --no-pager 2>/dev/null || true)\n";
  s += "elif [ -r /var/log/kern.log ]; then\n";
  s += "oom_lines=$(grep -E 'Out of memory|oom-kill|Killed process' /var/log/kern.log 2>/dev/null | tail -n 50 || true)\n";
  s += "else\n";
  s += "oom_lines=$(dmesg 2>/dev/null | grep -E 'Out of memory|oom-kill|Killed process' | tail -n 50 || true)\n";
  s += "fi\n";
  s += "if [ -n \"$oom_lines\" ]; then\n";
  s += "oom_count=$(printf '%s\\n' \"$oom_lines\" | grep -c . || true)\n";
  s += "oom_last=$(printf '%s\\n' \"$oom_lines\" | tail -n 1 | sed -n "
      "'s/.*Killed process [0-9][0-9]* (\\([^)]*\\)).*/\\1/p')\n";
  s += "if [ -z \"$oom_last\" ]; then\n";
  s += "oom_last=$(printf '%s\\n' \"$oom_lines\" | tail -n 1 | sed -n "
      "'s/.*task=\\([^, ]*\\).*/\\1/p')\n";
  s += "fi\n";
  s += "fi\n";
  s += "printf 'oom_count=%s\\n' \"${oom_count:-0}\"\n";
  s += "if [ -n \"$oom_last\" ]; then printf 'oom_last=%s\\n' \"$oom_last\"; fi\n";

  // SSH fail за сутки — узкий journalctl, без полного auth.log.
  s += "ssh_fail_count=0\n";
  s += "if command -v journalctl >/dev/null 2>&1; then\n";
  s += "ssh_fail_count=$(journalctl -u ssh -u sshd --since '24 hours ago' "
      "-g 'Failed password|Invalid user|authentication failure' -q --no-pager 2>/dev/null | "
      "head -n 200 | grep -c . || true)\n";
  s += "elif [ -r /var/log/auth.log ]; then\n";
  s += "ssh_fail_count=$(grep -E 'Failed password|Invalid user|authentication failure' "
      "/var/log/auth.log 2>/dev/null | tail -n 500 | grep -c . || true)\n";
  s += "elif [ -r /var/log/secure ]; then\n";
  s += "ssh_fail_count=$(grep -E 'Failed password|Invalid user|authentication failure' "
      "/var/log/secure 2>/dev/null | tail -n 500 | grep -c . || true)\n";
  s += "fi\n";
  s += "printf 'ssh_fail_count=%s\\n' \"${ssh_fail_count:-0}\"\n";

  // Время: timedatectl / chronyc, без долгого ожидания.
  s += "if command -v timedatectl >/dev/null 2>&1; then\n";
  s += "sync=$(timedatectl show -p NTPSynchronized --value 2>/dev/null || true)\n";
  s += "if [ \"$sync\" = \"yes\" ]; then printf 'time_synced=1\\n'\n";
  s += "elif [ \"$sync\" = \"no\" ]; then printf 'time_synced=0\\n'\n";
  s += "fi\n";
  s += "fi\n";
  s += "if command -v chronyc >/dev/null 2>&1; then\n";
  s += "off=$(chronyc tracking 2>/dev/null | awk -F: '/Last offset/ {\n";
  s += "  gsub(/^[ \\t]+|[ \\t]+$/, \"\", $2); split($2, a, /[ \\t]+/); print a[1]; exit\n";
  s += "}')\n";
  s += "if [ -n \"$off\" ]; then printf 'time_offset_sec=%s\\n' \"$off\"; fi\n";
  s += "fi\n";

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

void health_apply_virtual_disks(HealthSnapshot& snap, bool show_virtual, const HealthThresholds& thresholds,
                                const HealthAlertIgnore& ignore) {
  if (show_virtual) return;
  const auto before = snap.disks.size();
  snap.disks.erase(std::remove_if(snap.disks.begin(), snap.disks.end(),
                                  [](const HealthDisk& d) { return health_mount_is_virtual(d.mount); }),
                   snap.disks.end());
  if (snap.disks.size() == before) return;
  apply_health_thresholds(snap, thresholds, ignore);
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

std::vector<std::string> health_alert_reasons(const HealthSnapshot& snap, const HealthThresholds& thresholds,
                                              const HealthAlertIgnore& ignore) {
  std::vector<std::string> out;
  if (snap.level == HealthLevel::Offline) {
    out.push_back(snap.error.empty() ? "нет связи" : snap.error);
    return out;
  }
  if (snap.level == HealthLevel::Unsupported) {
    out.push_back("не Linux");
    return out;
  }
  if (snap.checking || snap.level == HealthLevel::Checking) return out;
  if (snap.level != HealthLevel::Warn && snap.level != HealthLevel::Crit) return out;

  auto add_pct = [&](const char* name, double pct, int warn, int crit) {
    if (pct < 0) return;
    const auto lvl = level_from_pct(pct, warn, crit);
    if (lvl == HealthLevel::Warn || lvl == HealthLevel::Crit) {
      out.push_back(std::string(name) + " " + format_pct(pct));
    }
  };
  add_pct("CPU", snap.cpu_pct, thresholds.cpu_warn, thresholds.cpu_crit);
  add_pct("iowait", snap.iowait_pct, thresholds.iowait_warn, thresholds.iowait_crit);
  add_pct("steal", snap.steal_pct, thresholds.steal_warn, thresholds.steal_crit);
  add_pct("RAM", snap.mem_pct, thresholds.ram_warn, thresholds.ram_crit);
  add_pct("Swap", snap.swap_pct, thresholds.swap_warn, thresholds.swap_crit);
  for (const auto& disk : snap.disks) {
    const double pct = disk.pct();
    const auto lvl = level_from_pct(pct, thresholds.disk_warn, thresholds.disk_crit);
    if (lvl == HealthLevel::Warn || lvl == HealthLevel::Crit) {
      out.push_back("диск " + disk.mount + " " + format_pct(pct));
    }
  }
  {
    const double load_pct = health_load_pct(snap);
    add_pct("нагрузка", load_pct, thresholds.load_warn, thresholds.load_crit);
  }
  if (!ignore.reboot && snap.reboot_required) out.push_back("нужна перезагрузка");
  if (!ignore.failed_units && !snap.failed_units.empty()) {
    out.push_back("службы ×" + std::to_string(snap.failed_units.size()));
  }
  if (!ignore.docker && snap.docker_unhealthy > 0) {
    out.push_back("Docker unhealthy ×" + std::to_string(snap.docker_unhealthy));
  }
  if (!ignore.oom && snap.oom_count > 0) out.push_back("OOM ×" + std::to_string(snap.oom_count));
  if (!ignore.ssh && snap.ssh_fail_count >= thresholds.ssh_warn) {
    out.push_back("SSH fail ×" + std::to_string(snap.ssh_fail_count));
  }
  if (!ignore.time) {
    if (snap.time_synced == 0) out.push_back("время не синхронизировано");
    if (snap.time_offset_known) {
      const double off = std::fabs(snap.time_offset_sec);
      if (off >= kHealthTimeOffsetWarnSec) {
        out.push_back("смещение часов " + std::to_string(static_cast<int>(off)) + " с");
      }
    }
  }
  if (!ignore.net && snap.net_err_delta >= kHealthNetErrWarn) {
    out.push_back("сеть +" + std::to_string(snap.net_err_delta) + " ошибок");
  }
  if (!snap.error.empty() && out.empty()) out.push_back(snap.error);
  return out;
}

std::string health_alert_summary(const HealthSnapshot& snap, const HealthThresholds& thresholds, int max_items,
                                 const HealthAlertIgnore& ignore) {
  auto reasons = health_alert_reasons(snap, thresholds, ignore);
  if (reasons.empty()) return {};
  if (max_items < 1) max_items = 1;
  std::string out;
  const int n = static_cast<int>(reasons.size());
  const int take = std::min(n, max_items);
  for (int i = 0; i < take; ++i) {
    if (i) out += " · ";
    out += reasons[static_cast<std::size_t>(i)];
  }
  if (n > take) out += " · +" + std::to_string(n - take);
  return out;
}

std::vector<HealthListBadge> health_list_badges(const HealthSnapshot& snap, const HealthThresholds& thresholds,
                                                bool enabled) {
  auto same = [](HealthLevel level, const std::string& tip) {
    return std::vector<HealthListBadge>{
        {tip, level},
        {tip, level},
        {tip, level},
        {tip, level},
    };
  };
  if (!enabled) return same(HealthLevel::Unknown, "Проверка состояния выключена");
  if (snap.checking || snap.level == HealthLevel::Checking) {
    return same(HealthLevel::Checking, "Проверка…");
  }
  if (snap.level == HealthLevel::Offline) {
    std::string tip = "Нет связи";
    if (!snap.error.empty()) tip += ": " + snap.error;
    return same(HealthLevel::Offline, tip);
  }
  if (snap.level == HealthLevel::Unsupported) {
    return same(HealthLevel::Unsupported, "Не Linux / не поддерживается");
  }
  if (snap.checked_at <= 0 && !health_has_readings(snap)) {
    return same(HealthLevel::Unknown, "Ещё не проверяли");
  }

  std::vector<HealthListBadge> out;
  out.reserve(4);

  {
    HealthListBadge b;
    if (snap.cpu_pct >= 0) {
      b.level = level_from_pct(snap.cpu_pct, thresholds.cpu_warn, thresholds.cpu_crit);
      b.tip = "CPU " + format_pct(snap.cpu_pct);
    } else {
      b.tip = "CPU: нет данных";
    }
    out.push_back(std::move(b));
  }
  {
    HealthListBadge b;
    if (snap.mem_pct >= 0) {
      b.level = level_from_pct(snap.mem_pct, thresholds.ram_warn, thresholds.ram_crit);
      b.tip = "RAM " + format_pct(snap.mem_pct);
      if (snap.mem_total_kb > 0) {
        b.tip += " · " + format_kib(health_mem_used_kb(snap)) + " / " + format_kib(snap.mem_total_kb);
      }
    } else {
      b.tip = "RAM: нет данных";
    }
    out.push_back(std::move(b));
  }
  {
    HealthListBadge b;
    if (const auto* disk = health_root_or_worst(snap)) {
      const double pct = disk->pct();
      b.level = level_from_pct(pct, thresholds.disk_warn, thresholds.disk_crit);
      b.tip = "Диск " + disk->mount + " " + format_pct(pct);
      b.tip += " · " + format_kib(disk->used_kb) + " / " + format_kib(disk->total_kb);
    } else {
      b.tip = "Диск: нет данных";
    }
    out.push_back(std::move(b));
  }
  {
    HealthListBadge b;
    const double load_pct = health_load_pct(snap);
    if (load_pct >= 0) {
      b.level = level_from_pct(load_pct, thresholds.load_warn, thresholds.load_crit);
      std::ostringstream tip;
      tip.setf(std::ios::fixed);
      tip.precision(load_pct >= 10 ? 0 : 1);
      tip << "Нагрузка " << load_pct << "% (load/ядра)";
      const double load = snap.load15 >= 0 ? snap.load15 : (snap.load5 >= 0 ? snap.load5 : snap.load1);
      if (load >= 0 && snap.nproc > 0) {
        tip.precision(2);
        tip << " · " << load << " / " << snap.nproc;
      }
      b.tip = tip.str();
    } else {
      b.tip = "Нагрузка: нет данных";
    }
    out.push_back(std::move(b));
  }
  return out;
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

std::string format_bytes(long long bytes) {
  if (bytes < 0) bytes = 0;
  if (bytes < 1024) return std::to_string(bytes) + " Б";
  return format_kib(bytes / 1024);
}

std::string format_bps(double bytes_per_sec) {
  if (bytes_per_sec < 0) return "—";
  std::ostringstream ss;
  ss.setf(std::ios::fixed);
  if (bytes_per_sec < 1024.0) {
    ss.precision(0);
    ss << bytes_per_sec << " Б/с";
    return ss.str();
  }
  const double kib = bytes_per_sec / 1024.0;
  if (kib < 1024.0) {
    ss.precision(kib >= 10 ? 0 : 1);
    ss << kib << " КБ/с";
    return ss.str();
  }
  const double mib = kib / 1024.0;
  ss.precision(mib >= 10 ? 1 : 2);
  ss << mib << " МБ/с";
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

std::string format_signed_kib(long long delta_kb) {
  if (delta_kb == 0) return "без изменений";
  const char* sign = delta_kb > 0 ? "+" : "−";
  return std::string(sign) + format_kib(delta_kb > 0 ? delta_kb : -delta_kb);
}

std::string format_signed_pct(double delta_pct) {
  if (std::fabs(delta_pct) < 0.05) return "без изменений";
  std::ostringstream ss;
  ss.setf(std::ios::fixed);
  ss.precision(std::fabs(delta_pct) >= 10 ? 0 : 1);
  if (delta_pct > 0) ss << "+";
  ss << delta_pct << "%";
  return ss.str();
}

long long health_mem_used_kb(const HealthSnapshot& snap) {
  if (snap.mem_total_kb <= 0) return -1;
  return snap.mem_total_kb - std::min(snap.mem_avail_kb, snap.mem_total_kb);
}

long long health_swap_used_kb(const HealthSnapshot& snap) {
  if (snap.swap_total_kb <= 0) return -1;
  return snap.swap_total_kb - std::min(snap.swap_free_kb, snap.swap_total_kb);
}

long long health_net_errs(const HealthSnapshot& snap) {
  if (snap.net_rx_errs < 0 && snap.net_tx_errs < 0 && snap.net_rx_drop < 0 && snap.net_tx_drop < 0) {
    return -1;
  }
  long long n = 0;
  if (snap.net_rx_errs >= 0) n += snap.net_rx_errs;
  if (snap.net_tx_errs >= 0) n += snap.net_tx_errs;
  if (snap.net_rx_drop >= 0) n += snap.net_rx_drop;
  if (snap.net_tx_drop >= 0) n += snap.net_tx_drop;
  return n;
}

void health_apply_net_delta(HealthSnapshot& snap, const HealthSnapshot& previous,
                            const HealthAlertIgnore& ignore) {
  snap.net_err_delta = -1;
  const auto cur = health_net_errs(snap);
  const auto prev = health_net_errs(previous);
  if (cur < 0 || prev < 0) return;
  snap.net_err_delta = cur - prev;
  if (snap.net_err_delta < 0) snap.net_err_delta = 0;
  if (ignore.net) return;
  if (snap.level == HealthLevel::Offline || snap.level == HealthLevel::Unsupported ||
      snap.level == HealthLevel::Checking) {
    return;
  }
  if (snap.net_err_delta >= kHealthNetErrCrit) {
    snap.level = worse_health(snap.level, HealthLevel::Crit);
  } else if (snap.net_err_delta >= kHealthNetErrWarn) {
    snap.level = worse_health(snap.level, HealthLevel::Warn);
  }
}

bool health_rebooted_since(const HealthSnapshot& current, const HealthSnapshot& previous) {
  if (current.uptime_sec < 0 || previous.uptime_sec < 0) return false;
  if (current.checked_at <= 0 || previous.checked_at <= 0) return false;
  const double elapsed = current.checked_at - previous.checked_at;
  if (elapsed < 120) return false;
  const double expected = previous.uptime_sec + elapsed;
  // Допуск на рассинхрон часов и длительность самой проверки.
  return current.uptime_sec + 180.0 < expected;
}

HealthTrend health_trend(const HealthSnapshot& current, const HealthSnapshot& previous) {
  HealthTrend t;
  if (previous.checked_at <= 0 || current.checked_at <= 0) return t;
  t.has_prev = true;
  t.prev_checked_at = previous.checked_at;
  const auto cur_mem = health_mem_used_kb(current);
  const auto prev_mem = health_mem_used_kb(previous);
  if (cur_mem >= 0 && prev_mem >= 0) {
    t.mem = true;
    t.mem_used_delta_kb = cur_mem - prev_mem;
  }
  const auto cur_swap = health_swap_used_kb(current);
  const auto prev_swap = health_swap_used_kb(previous);
  if (cur_swap >= 0 && prev_swap >= 0) {
    t.swap = true;
    t.swap_used_delta_kb = cur_swap - prev_swap;
  }
  const HealthDisk* cur_disk = health_root_or_worst(current);
  if (cur_disk) {
    const HealthDisk* prev_disk = nullptr;
    for (const auto& d : previous.disks) {
      if (d.mount == cur_disk->mount) {
        prev_disk = &d;
        break;
      }
    }
    if (!prev_disk) prev_disk = health_root_or_worst(previous);
    if (prev_disk && prev_disk->total_kb > 0 && cur_disk->total_kb > 0) {
      t.disk = true;
      t.disk_mount = cur_disk->mount;
      t.disk_used_delta_kb = cur_disk->used_kb - prev_disk->used_kb;
      const double cur_pct = cur_disk->pct();
      const double prev_pct = prev_disk->pct();
      if (cur_pct >= 0 && prev_pct >= 0) t.disk_pct_delta = cur_pct - prev_pct;
    }
  }
  if (current.net_rx_bytes >= 0 && previous.net_rx_bytes >= 0 && current.net_tx_bytes >= 0 &&
      previous.net_tx_bytes >= 0) {
    t.net = true;
    t.net_rx_delta = current.net_rx_bytes - previous.net_rx_bytes;
    t.net_tx_delta = current.net_tx_bytes - previous.net_tx_bytes;
    if (t.net_rx_delta < 0) t.net_rx_delta = 0;
    if (t.net_tx_delta < 0) t.net_tx_delta = 0;
    const auto cur_err = health_net_errs(current);
    const auto prev_err = health_net_errs(previous);
    if (cur_err >= 0 && prev_err >= 0) {
      t.net_err_delta = cur_err - prev_err;
      if (t.net_err_delta < 0) t.net_err_delta = 0;
    }
    const double span = current.checked_at - previous.checked_at;
    if (span >= 1.0) {
      t.net_rx_bps = static_cast<double>(t.net_rx_delta) / span;
      t.net_tx_bps = static_cast<double>(t.net_tx_delta) / span;
    }
  }
  return t;
}

HealthFleetAlert health_fleet_alert(const std::map<std::string, HealthSnapshot>& snaps,
                                    const std::vector<std::string>& enabled_ids) {
  HealthFleetAlert out;
  for (const auto& id : enabled_ids) {
    auto it = snaps.find(id);
    if (it == snaps.end()) continue;
    switch (it->second.level) {
      case HealthLevel::Crit:
        ++out.crit;
        break;
      case HealthLevel::Offline:
        ++out.offline;
        break;
      default:
        break;
    }
  }
  return out;
}

void health_history_push(HealthCacheEntry& entry, HealthSnapshot previous, int max_keep) {
  if (max_keep < 1) max_keep = kHealthHistoryMax;
  if (previous.checked_at <= 0) return;
  if (previous.level == HealthLevel::Checking || previous.checking) return;
  previous.checking = false;
  if (!entry.history.empty() &&
      std::fabs(entry.history.front().checked_at - previous.checked_at) < 0.5) {
    return;
  }
  entry.history.insert(entry.history.begin(), std::move(previous));
  if (static_cast<int>(entry.history.size()) > max_keep) {
    entry.history.resize(static_cast<std::size_t>(max_keep));
  }
}

bool health_level_is_alert(HealthLevel level) {
  return level == HealthLevel::Warn || level == HealthLevel::Crit || level == HealthLevel::Offline;
}

namespace {

std::vector<HealthSnapshot> spark_points(const HealthCacheEntry& entry, int max_points) {
  if (max_points < 2) max_points = kHealthSparkMax;
  std::vector<HealthSnapshot> points;
  // history: newest first → walk reverse for older-first
  const int hist_n = static_cast<int>(entry.history.size());
  const int take_hist = std::min(hist_n, max_points - 1);
  for (int i = take_hist - 1; i >= 0; --i) {
    points.push_back(entry.history[static_cast<std::size_t>(i)]);
  }
  if (entry.latest.checked_at > 0) points.push_back(entry.latest);
  return points;
}

}  // namespace

std::vector<double> health_spark_mem_pct(const HealthCacheEntry& entry, int max_points) {
  std::vector<double> out;
  for (const auto& s : spark_points(entry, max_points)) {
    if (s.mem_pct >= 0) out.push_back(s.mem_pct);
  }
  return out;
}

std::vector<double> health_spark_disk_pct(const HealthCacheEntry& entry, int max_points) {
  std::vector<double> out;
  for (const auto& s : spark_points(entry, max_points)) {
    if (auto* d = health_root_or_worst(s)) {
      const double p = d->pct();
      if (p >= 0) out.push_back(p);
    }
  }
  return out;
}

std::string format_health_span(double older_ts, double newer_ts) {
  if (older_ts <= 0 || newer_ts <= 0 || newer_ts < older_ts) return {};
  const double span = newer_ts - older_ts;
  if (span < 90) return "за минуту";
  if (span < 3600) return "за " + std::to_string(static_cast<int>(span / 60)) + " мин";
  if (span < 90 * 60) return "за час";
  if (span < 24 * 3600) return "за " + std::to_string(static_cast<int>(span / 3600)) + " ч";
  const int days = static_cast<int>(span / 86400);
  if (days == 1) return "за 1 день";
  if (days == 2 || days == 3 || days == 4) return "за " + std::to_string(days) + " дня";
  return "за " + std::to_string(days) + " дней";
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
  snap.iowait_pct = raw.value("iowait", -1.0);
  snap.steal_pct = raw.value("steal", -1.0);
  snap.mem_total_kb = raw.value("mem_total_kb", 0LL);
  snap.mem_avail_kb = raw.value("mem_avail_kb", 0LL);
  snap.mem_pct = raw.value("mem_pct", -1.0);
  snap.swap_total_kb = raw.value("swap_total_kb", 0LL);
  snap.swap_free_kb = raw.value("swap_free_kb", 0LL);
  snap.swap_pct = raw.value("swap_pct", -1.0);
  snap.uptime_sec = raw.value("uptime", -1.0);
  snap.reboot_required = raw.value("reboot_required", false);
  snap.rebooted = raw.value("rebooted", false);
  snap.checked_at = raw.value("checked_at", 0.0);
  if (raw.contains("failed_units") && raw["failed_units"].is_array()) {
    for (const auto& u : raw["failed_units"]) {
      if (!u.is_string()) continue;
      auto name = trim(u.get<std::string>());
      if (name.empty()) continue;
      if (static_cast<int>(snap.failed_units.size()) >= kHealthFailedUnitsMax) break;
      snap.failed_units.push_back(std::move(name));
    }
  }
  snap.docker_present = raw.value("docker", false);
  snap.docker_running = raw.value("docker_running", -1);
  snap.docker_exited = raw.value("docker_exited", -1);
  snap.docker_unhealthy = raw.value("docker_unhealthy", -1);
  if (snap.docker_present) {
    if (snap.docker_running < 0) snap.docker_running = 0;
    if (snap.docker_exited < 0) snap.docker_exited = 0;
    if (snap.docker_unhealthy < 0) snap.docker_unhealthy = 0;
  }
  auto load_names = [&](const char* key, std::vector<std::string>& out) {
    if (!raw.contains(key) || !raw[key].is_array()) return;
    for (const auto& u : raw[key]) {
      if (!u.is_string()) continue;
      auto name = trim(u.get<std::string>());
      if (name.empty()) continue;
      if (static_cast<int>(out.size()) >= kHealthDockerNamesMax) break;
      out.push_back(std::move(name));
    }
  };
  load_names("docker_exited_names", snap.docker_exited_names);
  load_names("docker_unhealthy_names", snap.docker_unhealthy_names);
  snap.oom_count = raw.value("oom_count", -1);
  snap.oom_last = raw.value("oom_last", "");
  snap.net_rx_bytes = raw.value("net_rx_bytes", -1LL);
  snap.net_tx_bytes = raw.value("net_tx_bytes", -1LL);
  snap.net_rx_errs = raw.value("net_rx_errs", -1LL);
  snap.net_tx_errs = raw.value("net_tx_errs", -1LL);
  snap.net_rx_drop = raw.value("net_rx_drop", -1LL);
  snap.net_tx_drop = raw.value("net_tx_drop", -1LL);
  snap.net_if = raw.value("net_if", "");
  snap.net_err_delta = raw.value("net_err_delta", -1LL);
  snap.ssh_fail_count = raw.value("ssh_fail_count", -1);
  snap.time_synced = raw.value("time_synced", -1);
  snap.time_offset_known = raw.value("time_offset_known", false);
  snap.time_offset_sec = raw.value("time_offset_sec", 0.0);
  auto load_procs = [&](const char* key, std::vector<HealthProc>& out) {
    if (!raw.contains(key) || !raw[key].is_array()) return;
    for (const auto& p : raw[key]) {
      if (!p.is_object()) continue;
      HealthProc proc;
      proc.pid = p.value("pid", 0);
      proc.rss_kb = p.value("rss_kb", 0LL);
      proc.cpu_pct = p.value("cpu", -1.0);
      proc.name = p.value("name", "");
      if (proc.pid <= 0 || proc.name.empty()) continue;
      if (static_cast<int>(out.size()) >= kHealthTopProcsMax) break;
      out.push_back(std::move(proc));
    }
  };
  load_procs("top_mem", snap.top_mem);
  load_procs("top_cpu", snap.top_cpu);
  if (snap.mem_pct < 0 && snap.mem_total_kb > 0) {
    const auto used = snap.mem_total_kb - std::min(snap.mem_avail_kb, snap.mem_total_kb);
    snap.mem_pct = 100.0 * static_cast<double>(used) / static_cast<double>(snap.mem_total_kb);
  }
  if (snap.swap_pct < 0 && snap.swap_total_kb > 0) {
    const auto free = std::min(snap.swap_free_kb, snap.swap_total_kb);
    const auto used = snap.swap_total_kb - free;
    snap.swap_pct = 100.0 * static_cast<double>(used) / static_cast<double>(snap.swap_total_kb);
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
  json failed = json::array();
  for (const auto& u : snap.failed_units) failed.push_back(u);
  json exited_names = json::array();
  for (const auto& u : snap.docker_exited_names) exited_names.push_back(u);
  json unhealthy_names = json::array();
  for (const auto& u : snap.docker_unhealthy_names) unhealthy_names.push_back(u);
  auto procs_json = [](const std::vector<HealthProc>& procs) {
    json arr = json::array();
    for (const auto& p : procs) {
      arr.push_back({{"pid", p.pid}, {"rss_kb", p.rss_kb}, {"cpu", p.cpu_pct}, {"name", p.name}});
    }
    return arr;
  };
  return {
      {"name", snap.server_name},
      {"level", health_level_id(snap.level)},
      {"error", snap.error},
      {"nproc", snap.nproc},
      {"load1", snap.load1},
      {"load5", snap.load5},
      {"load15", snap.load15},
      {"cpu", snap.cpu_pct},
      {"iowait", snap.iowait_pct},
      {"steal", snap.steal_pct},
      {"mem_total_kb", snap.mem_total_kb},
      {"mem_avail_kb", snap.mem_avail_kb},
      {"mem_pct", snap.mem_pct},
      {"swap_total_kb", snap.swap_total_kb},
      {"swap_free_kb", snap.swap_free_kb},
      {"swap_pct", snap.swap_pct},
      {"uptime", snap.uptime_sec},
      {"reboot_required", snap.reboot_required},
      {"rebooted", snap.rebooted},
      {"failed_units", std::move(failed)},
      {"docker", snap.docker_present},
      {"docker_running", snap.docker_running},
      {"docker_exited", snap.docker_exited},
      {"docker_unhealthy", snap.docker_unhealthy},
      {"docker_exited_names", std::move(exited_names)},
      {"docker_unhealthy_names", std::move(unhealthy_names)},
      {"oom_count", snap.oom_count},
      {"oom_last", snap.oom_last},
      {"net_rx_bytes", snap.net_rx_bytes},
      {"net_tx_bytes", snap.net_tx_bytes},
      {"net_rx_errs", snap.net_rx_errs},
      {"net_tx_errs", snap.net_tx_errs},
      {"net_rx_drop", snap.net_rx_drop},
      {"net_tx_drop", snap.net_tx_drop},
      {"net_if", snap.net_if},
      {"net_err_delta", snap.net_err_delta},
      {"ssh_fail_count", snap.ssh_fail_count},
      {"time_synced", snap.time_synced},
      {"time_offset_known", snap.time_offset_known},
      {"time_offset_sec", snap.time_offset_sec},
      {"top_mem", procs_json(snap.top_mem)},
      {"top_cpu", procs_json(snap.top_cpu)},
      {"checked_at", snap.checked_at},
      {"disks", std::move(disks)},
  };
}

}  // namespace

std::map<std::string, HealthCacheEntry> load_health_cache(const std::filesystem::path& path) {
  std::map<std::string, HealthCacheEntry> out;
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
    HealthCacheEntry entry;
    entry.latest = snapshot_from_json(it.value(), it.key());
    if (entry.latest.server_id.empty()) continue;
    if (it.value().contains("history") && it.value()["history"].is_array()) {
      for (const auto& h : it.value()["history"]) {
        if (!h.is_object()) continue;
        auto past = snapshot_from_json(h, it.key());
        past.server_id = it.key();
        if (past.checked_at <= 0) continue;
        entry.history.push_back(std::move(past));
      }
      if (static_cast<int>(entry.history.size()) > kHealthHistoryMax) {
        entry.history.resize(static_cast<std::size_t>(kHealthHistoryMax));
      }
    }
    out[entry.latest.server_id] = std::move(entry);
  }
  return out;
}

void save_health_cache(const std::filesystem::path& path, const std::map<std::string, HealthCacheEntry>& entries) {
  json servers = json::object();
  for (const auto& [id, entry] : entries) {
    if (id.empty()) continue;
    json raw = snapshot_to_json(entry.latest);
    json hist = json::array();
    for (const auto& h : entry.history) {
      hist.push_back(snapshot_to_json(h));
    }
    raw["history"] = std::move(hist);
    servers[id] = std::move(raw);
  }
  json payload = {{"v", 2}, {"servers", std::move(servers)}};
  atomic_write_text(path, payload.dump(2));
}

}  // namespace fatty

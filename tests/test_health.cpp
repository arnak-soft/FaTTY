#include "tests/test.hpp"
#include "core/health.hpp"
#include "core/util.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>

using namespace fatty;

namespace fatty::test {

void test_health() {
  const char* sample = R"(
Welcome to Ubuntu 22.04
FATTYHEALTH v1
nproc=2
load1=0.12
load5=0.08
load15=0.01
cpu=7.5
mem_total_kb=1024000
mem_avail_kb=512000
swap_total_kb=512000
swap_free_kb=256000
uptime=90061
disk=/ 123456 204800
disk=/var 80000 100000
FATTYHEALTH_END
FATTYCWD_abc:/root
)";
  auto snap = parse_health_output(sample);
  expect(snap.nproc == 2, "parse nproc");
  expect(snap.load1 > 0.1 && snap.load1 < 0.13, "parse load1");
  expect(snap.cpu_pct > 7.4 && snap.cpu_pct < 7.6, "parse cpu");
  expect(snap.mem_total_kb == 1024000, "parse mem total");
  expect(snap.mem_pct > 49 && snap.mem_pct < 51, "ram used pct");
  expect(snap.swap_total_kb == 512000, "parse swap total");
  expect(snap.swap_pct > 49 && snap.swap_pct < 51, "swap used pct");
  expect(health_load_pct(snap) >= 0 && health_load_pct(snap) < 1, "load15 << nproc");
  expect(snap.disks.size() == 2, "two disks");
  expect(snap.disks[0].mount == "/", "root mount");
  expect(snap.disks[1].mount == "/var", "var mount");
  expect(health_root_or_worst(snap) && health_root_or_worst(snap)->mount == "/", "prefer root disk");

  HealthThresholds t;
  t.disk_warn = 70;
  t.disk_crit = 90;
  apply_health_thresholds(snap, t);
  expect(snap.level == HealthLevel::Warn, "/var 80% is warn at 70");

  {
    auto badges = health_list_badges(snap, t, true);
    expect(badges.size() == 4, "list badges: cpu ram disk load");
    expect(badges[0].tip.find("CPU") == 0, "cpu tip");
    expect(badges[1].tip.find("RAM") == 0, "ram tip");
    expect(badges[2].tip.find("Диск") == 0, "disk tip");
    expect(badges[3].tip.find("Нагрузка") == 0, "load tip");
    auto off = health_list_badges({}, t, true);
    expect(off.size() == 4, "unknown still four cubes");
    expect(off[0].tip.find("Ещё не") != std::string::npos, "not checked tip");
    auto disabled = health_list_badges(snap, t, false);
    expect(disabled[0].tip.find("выключена") != std::string::npos, "disabled tip");
    auto reasons = health_alert_reasons(snap, t);
    expect(!reasons.empty(), "warn has reasons");
    expect(health_alert_summary(snap, t, 2).find("диск") != std::string::npos ||
               health_alert_summary(snap, t, 8).find("диск") != std::string::npos,
           "disk in summary");
  }

  HealthSnapshot load_only;
  load_only.nproc = 2;
  load_only.load15 = 2.5;
  load_only.checked_at = 1;
  HealthThresholds load_t;
  load_t.load_warn = 100;
  load_t.load_crit = 150;
  apply_health_thresholds(load_only, load_t);
  expect(load_only.level == HealthLevel::Warn, "load 125% of cores is warn");
  load_only.load15 = 3.2;
  apply_health_thresholds(load_only, load_t);
  expect(load_only.level == HealthLevel::Crit, "load 160% of cores is crit");

  HealthSnapshot swap_only;
  swap_only.swap_total_kb = 1000;
  swap_only.swap_free_kb = 200;
  swap_only.swap_pct = 80;
  swap_only.checked_at = 1;
  HealthThresholds swap_t;
  swap_t.swap_warn = 50;
  swap_t.swap_crit = 80;
  apply_health_thresholds(swap_only, swap_t);
  expect(swap_only.level == HealthLevel::Crit, "swap 80% is crit");

  auto no_swap = parse_health_output("FATTYHEALTH v1\nnproc=1\nswap_total_kb=0\nswap_free_kb=0\nFATTYHEALTH_END\n");
  expect(no_swap.swap_pct < 0, "zero swap is N/A");

  auto cpu_parts = parse_health_output(
      "FATTYHEALTH v1\nnproc=2\ncpu=10.0\niowait=30.0\nsteal=20.0\nFATTYHEALTH_END\n");
  expect(cpu_parts.iowait_pct > 29.9 && cpu_parts.iowait_pct < 30.1, "parse iowait");
  expect(cpu_parts.steal_pct > 19.9 && cpu_parts.steal_pct < 20.1, "parse steal");
  cpu_parts.checked_at = 1;
  HealthThresholds io_t;
  io_t.iowait_warn = 25;
  io_t.iowait_crit = 50;
  io_t.steal_warn = 15;
  io_t.steal_crit = 30;
  apply_health_thresholds(cpu_parts, io_t);
  expect(cpu_parts.level == HealthLevel::Warn, "iowait/steal warn");

  HealthSnapshot fleet_a;
  fleet_a.level = HealthLevel::Crit;
  HealthSnapshot fleet_b;
  fleet_b.level = HealthLevel::Offline;
  HealthSnapshot fleet_c;
  fleet_c.level = HealthLevel::Ok;
  auto fleet = health_fleet_alert({{"a", fleet_a}, {"b", fleet_b}, {"c", fleet_c}}, {"a", "b", "c"});
  expect(fleet.crit == 1 && fleet.offline == 1 && fleet.total() == 2, "fleet alert counts");
  expect(health_fleet_alert({{"a", fleet_a}}, {"c"}).total() == 0, "disabled not counted");

  auto signals = parse_health_output(
      "FATTYHEALTH v1\nnproc=1\nuptime=100\nreboot_required=1\n"
      "failed_unit=nginx.service\nfailed_unit=cron.service\nFATTYHEALTH_END\n");
  expect(signals.reboot_required, "parse reboot_required");
  expect(signals.failed_units.size() == 2, "parse failed units");
  expect(signals.failed_units[0] == "nginx.service", "first failed unit");
  signals.checked_at = 1;
  apply_health_thresholds(signals, {});
  expect(signals.level == HealthLevel::Warn, "reboot + 2 failed is warn");
  signals.failed_units.push_back("a.service");
  apply_health_thresholds(signals, {});
  expect(signals.level == HealthLevel::Crit, "3 failed is crit");

  HealthSnapshot before_reboot;
  before_reboot.uptime_sec = 86400;
  before_reboot.checked_at = 1000;
  HealthSnapshot after_reboot;
  after_reboot.uptime_sec = 300;
  after_reboot.checked_at = 1000 + 3600;
  expect(health_rebooted_since(after_reboot, before_reboot), "uptime drop is reboot");
  HealthSnapshot still_up = after_reboot;
  still_up.uptime_sec = 86400 + 3600;
  expect(!health_rebooted_since(still_up, before_reboot), "continuous uptime not reboot");

  auto spaced = parse_health_output("FATTYHEALTH v1\ndisk=/home/user data 10 20\nFATTYHEALTH_END\n");
  expect(spaced.disks.size() == 1, "disk with spaces");
  expect(spaced.disks[0].mount == "/home/user data", "mount keeps spaces");
  expect(spaced.disks[0].used_kb == 10 && spaced.disks[0].total_kb == 20, "disk numbers");

  auto other = parse_health_output("banner\nFATTYHEALTH v1\nos=other\nFATTYHEALTH_END\n");
  expect(other.level == HealthLevel::Unsupported, "os=other");
  expect(other.error == "не Linux", "os=other error");

  auto bad = parse_health_output("just motd");
  expect(bad.level == HealthLevel::Unknown, "no marker");
  expect(bad.error.find("FATTYHEALTH") != std::string::npos, "missing marker error");

  HealthSnapshot due;
  expect(health_is_due(due, 86400, 1000), "never checked is due");
  due.checked_at = 100;
  expect(!health_is_due(due, 86400, 100 + 86399), "not due yet");
  expect(health_is_due(due, 86400, 100 + 86400), "due at interval");

  auto script = health_remote_script({true, true, true, true});
  expect(script.find("FATTYHEALTH v1") != std::string::npos, "script marker");
  expect(script.find("/proc/stat") != std::string::npos, "script cpu");
  expect(script.find("iowait=") != std::string::npos, "script iowait");
  expect(script.find("steal=") != std::string::npos, "script steal");
  expect(script.find("df -Pk") != std::string::npos, "script df");
  expect(script.find("SwapTotal") != std::string::npos, "script swap");
  expect(script.find("reboot-required") != std::string::npos, "script reboot flag");
  expect(script.find("systemctl list-units") != std::string::npos, "script failed units");
  expect(script.find("docker ps") == std::string::npos, "docker off by default");
  expect(script.find("overlay2") != std::string::npos, "skip overlay by default");
  auto with_docker_disks = health_remote_script({true, true, true, true, true});
  expect(with_docker_disks.find("overlay2") == std::string::npos, "keep overlay when enabled");
  auto with_docker = health_remote_script({true, true, true, true, false, true, true});
  expect(with_docker.find("docker_running=") != std::string::npos, "script docker counts");
  expect(with_docker.find("health=unhealthy") != std::string::npos, "script unhealthy filter");
  auto slim = health_remote_script({false, false, false, false, false, false, false});
  expect(slim.find("/proc/stat") == std::string::npos, "cpu off");
  expect(slim.find("df -Pk") == std::string::npos, "disk off");
  expect(slim.find("SwapTotal") == std::string::npos, "swap off");
  expect(slim.find("docker ps") == std::string::npos, "docker off in slim");
  expect(slim.find("reboot-required") != std::string::npos, "reboot always collected");
  expect(slim.find("oom_count=") != std::string::npos, "oom always collected");
  expect(slim.find("/proc/net/dev") != std::string::npos, "net always collected");
  expect(slim.find("ssh_fail_count=") != std::string::npos, "ssh fail always");
  expect(slim.find("time_synced=") != std::string::npos || slim.find("timedatectl") != std::string::npos,
         "time always");
  expect(slim.find("top_mem=") != std::string::npos, "top procs always");
  expect(slim.find("journalctl -k -b -g") != std::string::npos, "oom uses journalctl -g");
  expect(slim.find("os=other") != std::string::npos, "still detects non-linux");

  auto docker_snap = parse_health_output(
      "FATTYHEALTH v1\nnproc=1\ndocker=1\ndocker_running=2\ndocker_exited=1\ndocker_unhealthy=1\n"
      "docker_exited_name=old-job\ndocker_unhealthy_name=api\nFATTYHEALTH_END\n");
  expect(docker_snap.docker_present, "docker present");
  expect(docker_snap.docker_running == 2 && docker_snap.docker_exited == 1, "docker counts");
  expect(docker_snap.docker_unhealthy == 1, "docker unhealthy count");
  expect(docker_snap.docker_unhealthy_names.size() == 1 && docker_snap.docker_unhealthy_names[0] == "api",
         "unhealthy name");
  docker_snap.checked_at = 1;
  apply_health_thresholds(docker_snap, {});
  expect(docker_snap.level == HealthLevel::Warn, "1 unhealthy is warn");
  docker_snap.docker_unhealthy = 3;
  apply_health_thresholds(docker_snap, {});
  expect(docker_snap.level == HealthLevel::Crit, "3 unhealthy is crit");
  auto no_docker = parse_health_output("FATTYHEALTH v1\nnproc=1\ndocker=0\nFATTYHEALTH_END\n");
  expect(!no_docker.docker_present && no_docker.docker_running < 0, "docker absent");

  auto oom_snap = parse_health_output(
      "FATTYHEALTH v1\nnproc=1\noom_count=2\noom_last=mysqld\nFATTYHEALTH_END\n");
  expect(oom_snap.oom_count == 2 && oom_snap.oom_last == "mysqld", "parse oom");
  oom_snap.checked_at = 1;
  apply_health_thresholds(oom_snap, {});
  expect(oom_snap.level == HealthLevel::Warn, "2 oom is warn");
  oom_snap.oom_count = 3;
  apply_health_thresholds(oom_snap, {});
  expect(oom_snap.level == HealthLevel::Crit, "3 oom is crit");
  expect(health_level_is_alert(HealthLevel::Warn) && health_level_is_alert(HealthLevel::Offline), "alert levels");
  expect(!health_level_is_alert(HealthLevel::Ok), "ok not alert");

  HealthCacheEntry spark_e;
  spark_e.latest.checked_at = 3;
  spark_e.latest.mem_pct = 70;
  spark_e.latest.disks.push_back({"/", 700, 1000});
  HealthSnapshot h1;
  h1.checked_at = 1;
  h1.mem_pct = 40;
  h1.disks.push_back({"/", 400, 1000});
  HealthSnapshot h2;
  h2.checked_at = 2;
  h2.mem_pct = 55;
  h2.disks.push_back({"/", 550, 1000});
  spark_e.history = {h2, h1};  // newest first
  auto ram_s = health_spark_mem_pct(spark_e);
  expect(ram_s.size() == 3 && ram_s[0] == 40 && ram_s[2] == 70, "ram spark order");
  auto disk_s = health_spark_disk_pct(spark_e);
  expect(disk_s.size() == 3 && disk_s[0] == 40 && disk_s[2] == 70, "disk spark order");

  auto extra = parse_health_output(
      "FATTYHEALTH v1\nnproc=1\n"
      "net_rx_bytes=1000\nnet_tx_bytes=2000\nnet_rx_errs=1\nnet_tx_errs=0\nnet_rx_drop=2\nnet_tx_drop=0\n"
      "net_if=eth0\nssh_fail_count=25\ntime_synced=0\ntime_offset_sec=12.5\n"
      "top_mem=10 204800 1.5 nginx\ntop_cpu=20 102400 40.0 php-fpm\n"
      "FATTYHEALTH_END\n");
  expect(extra.net_rx_bytes == 1000 && extra.net_if == "eth0", "parse net");
  expect(health_net_errs(extra) == 3, "net errs+drops");
  expect(extra.ssh_fail_count == 25 && extra.time_synced == 0 && extra.time_offset_known, "parse ssh/time");
  expect(extra.top_mem.size() == 1 && extra.top_mem[0].name == "nginx", "parse top mem");
  expect(extra.top_cpu.size() == 1 && extra.top_cpu[0].pid == 20, "parse top cpu");
  extra.checked_at = 2;
  apply_health_thresholds(extra, {});
  expect(extra.level == HealthLevel::Warn, "ssh fail + unsynced is warn");
  HealthSnapshot prev_net;
  prev_net.net_rx_bytes = 500;
  prev_net.net_tx_bytes = 1000;
  prev_net.net_rx_errs = 0;
  prev_net.net_tx_errs = 0;
  prev_net.net_rx_drop = 0;
  prev_net.net_tx_drop = 0;
  prev_net.checked_at = 1;
  HealthSnapshot cur_net = extra;
  cur_net.level = HealthLevel::Ok;
  cur_net.ssh_fail_count = 0;
  cur_net.time_synced = 1;
  cur_net.time_offset_known = false;
  cur_net.oom_count = 0;
  apply_health_thresholds(cur_net, {});
  health_apply_net_delta(cur_net, prev_net);
  expect(cur_net.net_err_delta == 3 && cur_net.level == HealthLevel::Warn, "net err growth warns");
  auto trend_net = health_trend(cur_net, prev_net);
  expect(trend_net.net && trend_net.net_rx_delta == 500, "net trend bytes");
  expect(format_bytes(1536) == "1.5 КБ" || format_bytes(1536).find("КБ") != std::string::npos, "format bytes");

  expect(format_pct(-1) == "—", "missing pct");
  expect(format_kib(512) == "512 КБ", "bytes kb");
  expect(format_uptime_sec(90061).find("д") != std::string::npos, "uptime days");
  expect(format_interval_label(86400) == "1 день", "interval day");
  expect(health_level_from_id(health_level_id(HealthLevel::Offline)) == HealthLevel::Offline, "level id roundtrip");

  expect(health_mount_is_virtual("/var/lib/docker/overlay2/abc/merged"), "docker overlay2");
  expect(health_mount_is_virtual("/mnt/data/docker/overlay2/xyz/merged"), "custom docker root");
  expect(health_mount_is_virtual("/snap/core/123"), "snap");
  expect(!health_mount_is_virtual("/"), "root is real");
  expect(!health_mount_is_virtual("/boot"), "boot is real");
  expect(!health_mount_is_virtual("/var/lib/docker/volumes/db/_data"), "named volume stays");
  HealthSnapshot virt;
  virt.checked_at = 1;
  virt.cpu_pct = 4;
  virt.disks.push_back({"/", 830, 1000});
  virt.disks.push_back({"/var/lib/docker/overlay2/a/merged", 830, 1000});
  virt.disks.push_back({"/boot", 13, 100});
  HealthThresholds warn80;
  warn80.disk_warn = 80;
  warn80.disk_crit = 90;
  apply_health_thresholds(virt, warn80);
  expect(virt.level == HealthLevel::Warn, "overlay kept in status");
  health_apply_virtual_disks(virt, false, warn80);
  expect(virt.disks.size() == 2, "overlay dropped");
  expect(virt.disks[0].mount == "/" && virt.disks[1].mount == "/boot", "kept real mounts");
  expect(virt.level == HealthLevel::Warn, "root 83% still warn");

  const auto dir = std::filesystem::temp_directory_path() / ("fatty-health-" + new_uuid());
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const auto path = dir / "health.json";
  snap.server_id = "s1";
  snap.server_name = "alpha";
  snap.checked_at = 12345;
  snap.level = HealthLevel::Ok;
  HealthCacheEntry e1;
  e1.latest = snap;
  HealthSnapshot older = snap;
  older.checked_at = 12000;
  older.mem_avail_kb = 600000;
  older.mem_pct = 100.0 * (1024000 - 600000) / 1024000.0;
  older.disks[0].used_kb = 100000;
  e1.history.push_back(older);
  std::map<std::string, HealthCacheEntry> cache{{"s1", e1}};
  save_health_cache(path, cache);
  auto loaded = load_health_cache(path);
  expect(loaded.count("s1") == 1, "cache load");
  expect(loaded["s1"].latest.nproc == 2, "cache nproc");
  expect(loaded["s1"].latest.swap_total_kb == 512000, "cache swap");
  expect(loaded["s1"].latest.disks.size() == 2, "cache disks");
  expect(loaded["s1"].latest.checked_at == 12345, "cache time");
  expect(loaded["s1"].history.size() == 1, "cache history");
  expect(loaded["s1"].history[0].checked_at == 12000, "history time");
  auto trend = health_trend(loaded["s1"].latest, loaded["s1"].history[0]);
  expect(trend.has_prev && trend.mem && trend.disk, "trend fields");
  expect(trend.mem_used_delta_kb > 0, "ram grew");
  expect(trend.disk_used_delta_kb > 0, "disk grew");
  expect(format_signed_kib(1024).find('+') != std::string::npos, "signed kib plus");
  expect(format_health_span(12000, 12345).find("за") != std::string::npos, "span label");

  HealthCacheEntry push_e;
  push_e.latest = snap;
  for (int i = 0; i < kHealthHistoryMax + 5; ++i) {
    HealthSnapshot p = snap;
    p.checked_at = 1000.0 + i;
    health_history_push(push_e, p, kHealthHistoryMax);
  }
  expect(static_cast<int>(push_e.history.size()) == kHealthHistoryMax, "history trim");

  HealthSnapshot bogus;
  bogus.server_id = "s2";
  bogus.level = HealthLevel::Ok;
  bogus.checked_at = 99;
  cache["s2"] = HealthCacheEntry{bogus, {}};
  save_health_cache(path, cache);
  loaded = load_health_cache(path);
  expect(loaded["s2"].latest.level == HealthLevel::Unknown, "empty ok cache demoted");
  expect(load_health_cache(dir / "missing.json").empty(), "missing cache");

  // Старый health.json без history (v1) читается как один latest.
  const auto v1_path = dir / "health_v1.json";
  atomic_write_text(v1_path, R"({"v":1,"servers":{"old":{"name":"o","level":"ok","checked_at":1,"cpu":1.0,"nproc":1}}})");
  auto v1 = load_health_cache(v1_path);
  expect(v1.count("old") == 1 && v1["old"].history.empty(), "v1 cache no history");
  expect(v1["old"].latest.cpu_pct > 0.9, "v1 latest cpu");

  std::filesystem::remove_all(dir, ec);

  HealthSnapshot empty;
  apply_health_thresholds(empty, {});
  expect(empty.level == HealthLevel::Unknown, "empty stays unknown");

  HealthSnapshot checked_empty;
  checked_empty.checked_at = 1;
  apply_health_thresholds(checked_empty, {});
  expect(checked_empty.level == HealthLevel::Unknown, "checked empty is not ok");

  const auto echoed = std::string("$ ") + health_remote_script({true, true, true, true}) +
                      "\n\nFATTYHEALTH v1\nnproc=4\nload1=0.25\nload5=0.2\nload15=0.1\ncpu=3.5\n"
                      "mem_total_kb=2048000\nmem_avail_kb=1024000\nuptime=3600\ndisk=/ 100 200\nFATTYHEALTH_END\n";
  auto from_echo = parse_health_output(echoed);
  expect(from_echo.nproc == 4, "real block after echoed script");
  expect(from_echo.cpu_pct > 3.4 && from_echo.cpu_pct < 3.6, "cpu from real block");
  expect(from_echo.disks.size() == 1 && from_echo.disks[0].mount == "/", "disk from real block");
  from_echo.checked_at = 1;
  apply_health_thresholds(from_echo, {});
  expect(from_echo.level == HealthLevel::Ok, "real metrics are ok");

  auto only_echo = parse_health_output("$ " + health_remote_script({true, true, true, true}) + "\n");
  expect(only_echo.nproc == 0 && only_echo.cpu_pct < 0 && only_echo.disks.empty(), "script echo is not a snapshot");
  expect(!only_echo.error.empty(), "missing real marker");
  only_echo.checked_at = 1;
  apply_health_thresholds(only_echo, {});
  expect(only_echo.level == HealthLevel::Unknown, "echo-only stays unknown");
}

}  // namespace fatty::test

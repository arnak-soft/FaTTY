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
  expect(snap.disks.size() == 2, "two disks");
  expect(snap.disks[0].mount == "/", "root mount");
  expect(snap.disks[1].mount == "/var", "var mount");
  expect(health_root_or_worst(snap) && health_root_or_worst(snap)->mount == "/", "prefer root disk");

  HealthThresholds t;
  t.disk_warn = 70;
  t.disk_crit = 90;
  apply_health_thresholds(snap, t);
  expect(snap.level == HealthLevel::Warn, "/var 80% is warn at 70");

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
  expect(script.find("df -Pk") != std::string::npos, "script df");
  auto slim = health_remote_script({false, false, false, false});
  expect(slim.find("/proc/stat") == std::string::npos, "cpu off");
  expect(slim.find("df -Pk") == std::string::npos, "disk off");
  expect(slim.find("os=other") != std::string::npos, "still detects non-linux");

  expect(format_pct(-1) == "—", "missing pct");
  expect(format_kib(512) == "512 КБ", "bytes kb");
  expect(format_uptime_sec(90061).find("д") != std::string::npos, "uptime days");
  expect(format_interval_label(86400) == "1 день", "interval day");
  expect(health_level_from_id(health_level_id(HealthLevel::Offline)) == HealthLevel::Offline, "level id roundtrip");

  const auto dir = std::filesystem::temp_directory_path() / ("fatty-health-" + new_uuid());
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const auto path = dir / "health.json";
  snap.server_id = "s1";
  snap.server_name = "alpha";
  snap.checked_at = 12345;
  snap.level = HealthLevel::Ok;
  std::map<std::string, HealthSnapshot> cache{{"s1", snap}};
  save_health_cache(path, cache);
  auto loaded = load_health_cache(path);
  expect(loaded.count("s1") == 1, "cache load");
  expect(loaded["s1"].nproc == 2, "cache nproc");
  expect(loaded["s1"].disks.size() == 2, "cache disks");
  expect(loaded["s1"].checked_at == 12345, "cache time");
  HealthSnapshot bogus;
  bogus.server_id = "s2";
  bogus.level = HealthLevel::Ok;
  bogus.checked_at = 99;
  cache["s2"] = bogus;
  save_health_cache(path, cache);
  loaded = load_health_cache(path);
  expect(loaded["s2"].level == HealthLevel::Unknown, "empty ok cache demoted");
  expect(load_health_cache(dir / "missing.json").empty(), "missing cache");
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

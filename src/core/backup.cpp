#include "core/backup.hpp"

#include "core/paths.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

namespace fatty {
namespace {

double unix_now() {
  using clock = std::chrono::system_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

std::string backup_stamp(double unix_ts) {
  auto tp = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(unix_ts));
  auto t = std::chrono::system_clock::to_time_t(tp);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream ss;
  ss << std::put_time(&tm, "%Y%m%d-%H%M%S");
  return ss.str();
}

std::filesystem::path unique_backup_path(const std::filesystem::path& dir, const std::string& stamp) {
  auto path = dir / ("config-" + stamp + ".json");
  if (!std::filesystem::exists(path)) return path;
  for (int n = 2; n < 1000; ++n) {
    auto candidate = dir / ("config-" + stamp + "-" + std::to_string(n) + ".json");
    if (!std::filesystem::exists(candidate)) return candidate;
  }
  return path;
}

bool is_backup_file(const std::filesystem::path& path) {
  auto name = path.filename().string();
  return name.rfind("config-", 0) == 0 && path.extension() == ".json";
}

}  // namespace

const std::filesystem::path& backups_dir() {
  static const auto path = app_dir() / "backups";
  return path;
}

void prune_backups(const std::filesystem::path& dest_dir, int keep) {
  if (keep < 1 || !std::filesystem::exists(dest_dir)) return;
  struct Item {
    std::filesystem::path path;
    std::filesystem::file_time_type mtime;
  };
  std::vector<Item> items;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dest_dir, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec) || !is_backup_file(entry.path())) continue;
    auto mtime = entry.last_write_time(ec);
    if (ec) continue;
    items.push_back({entry.path(), mtime});
  }
  if (static_cast<int>(items.size()) <= keep) return;
  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.mtime > b.mtime; });
  for (std::size_t i = static_cast<std::size_t>(keep); i < items.size(); ++i) {
    std::filesystem::remove(items[i].path, ec);
  }
}

BackupResult maybe_backup_config(const std::filesystem::path& source, const std::filesystem::path& dest_dir,
                                 AppSettings& settings, double now_unix, BackupOptions opts) {
  BackupResult result;
  if (!settings.backup_enabled) return result;
  if (opts.interval_sec < 0) opts.interval_sec = 0;
  if (opts.keep < 1) opts.keep = 1;
  if (settings.last_backup > 0 && now_unix - settings.last_backup < opts.interval_sec) {
    return result;
  }
  std::error_code ec;
  if (!std::filesystem::exists(source, ec) || !std::filesystem::is_regular_file(source, ec)) {
    result.error = "Нет файла конфига для копии.";
    return result;
  }
  std::filesystem::create_directories(dest_dir, ec);
  if (ec) {
    result.error = ec.message();
    return result;
  }
  auto dest = unique_backup_path(dest_dir, backup_stamp(now_unix));
  std::filesystem::copy_file(source, dest, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    result.error = ec.message();
    return result;
  }
  settings.last_backup = now_unix;
  result.made = true;
  result.path = dest;
  prune_backups(dest_dir, opts.keep);
  return result;
}

BackupResult maybe_backup_config(AppSettings& settings) {
  return maybe_backup_config(config_path(), backups_dir(), settings, unix_now());
}

}  // namespace fatty

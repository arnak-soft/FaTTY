#pragma once

#include "core/store.hpp"

#include <filesystem>
#include <string>

namespace fatty {

inline constexpr int kBackupIntervalSec = 24 * 3600;
inline constexpr int kBackupKeep = 14;

struct BackupOptions {
  int interval_sec = kBackupIntervalSec;
  int keep = kBackupKeep;
};

struct BackupResult {
  bool made = false;
  std::filesystem::path path;
  std::string error;
};

const std::filesystem::path& backups_dir();

// Копия source в dest_dir/config-YYYYMMDD-HHMMSS.json, если backup_enabled
// и с last_backup прошло interval_sec. Старые копии сверх keep удаляются.
// Пароли не расшифровываются: копируется тот же зашифрованный config.json.
BackupResult maybe_backup_config(const std::filesystem::path& source, const std::filesystem::path& dest_dir,
                                 AppSettings& settings, double now_unix, BackupOptions opts = {});

BackupResult maybe_backup_config(AppSettings& settings);

void prune_backups(const std::filesystem::path& dest_dir, int keep);

}  // namespace fatty

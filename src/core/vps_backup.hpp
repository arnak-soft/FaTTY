#pragma once

#include "core/store.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fatty {

inline constexpr int kBackupIntervalMinSec = 3600;
inline constexpr int kBackupIntervalMaxSec = 30 * 24 * 3600;
inline constexpr std::int64_t kBackupMaxTotalMinBytes = 100LL * 1024 * 1024;
inline constexpr std::int64_t kBackupDefaultMaxTotalBytes = 120LL * 1024 * 1024 * 1024;
inline constexpr std::int64_t kBackupDefaultDeleteBytes = 10LL * 1024 * 1024 * 1024;
inline constexpr std::int64_t kBackupDefaultMinFreeBytes = 1LL * 1024 * 1024 * 1024;
inline constexpr const char* kBackupStagingRel = ".fatty/backup-staging";
inline constexpr const char* kBackupCronDirRel = ".fatty/backup";

struct BackupArchiveInfo {
  std::string name;
  std::int64_t size = 0;
  double mtime = 0.0;
};

struct BackupValidation {
  bool ok = true;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  void fail(std::string msg);
  void warn(std::string msg);
};

struct PrunePlan {
  bool warned = false;
  std::vector<std::string> delete_names;
  std::string message;
  bool blocks_new = false;
  std::int64_t used_before = 0;
  std::int64_t used_after = 0;
};

std::string backup_kind_key(BackupKind kind);
BackupKind backup_kind_from_key(std::string_view key);
std::string backup_dest_key(BackupDestKind kind);
BackupDestKind backup_dest_from_key(std::string_view key);
std::string backup_schedule_key(BackupScheduleMode mode);
BackupScheduleMode backup_schedule_from_key(std::string_view key);
std::string backup_action_key(BackupOverflowAction action);
BackupOverflowAction backup_action_from_key(std::string_view key);
std::string backup_db_engine_key(BackupDbEngine engine);
BackupDbEngine backup_db_engine_from_key(std::string_view key);

std::string backup_kind_label(BackupKind kind);
std::string backup_dest_label(BackupDestKind kind);

void clamp_backup_job(BackupJob& job);
BackupValidation validate_backup_job(const BackupJob& job, const Config& config);

bool backup_job_is_due(const BackupJob& job, double now_unix);
std::string backup_archive_basename(const BackupJob& job, double unix_ts);
std::string backup_manifest_name(std::string_view archive_name);
bool is_backup_archive_name(std::string_view name, std::string_view job_id);
std::string backup_job_short_id(std::string_view job_id);

std::int64_t backup_archives_total_bytes(const std::vector<BackupArchiveInfo>& archives);
PrunePlan plan_backup_prune(const BackupJob& job, std::vector<BackupArchiveInfo> archives);
std::string describe_stride_policy(int delete_n, int keep_m);
std::string describe_overflow_preview(const BackupJob& job);

bool path_is_filesystem_root(std::string_view path);
bool path_is_absolute_posix(std::string_view path);
bool path_is_absolute_windows(std::string_view path);
bool path_contains_or_equals(std::string_view outer, std::string_view inner);

std::string build_files_tar_command(const BackupJob& job, const std::string& archive_path);
std::string build_db_dump_command(const BackupJob& job, const std::string& archive_path);
std::string build_cron_script(const BackupJob& job, const Server& source);
std::string cron_script_remote_path(const BackupJob& job);
std::string cron_entry_line(const BackupJob& job, int hour = 3, int minute = 15);

}  // namespace fatty

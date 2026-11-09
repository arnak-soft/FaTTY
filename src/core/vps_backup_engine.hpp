#pragma once

#include "core/store.hpp"
#include "core/vps_backup.hpp"
#include "net/sftp_session.hpp"
#include "net/ssh_session.hpp"

#include <functional>
#include <string>

namespace fatty {

struct BackupRunResult {
  bool ok = false;
  bool skipped = false;
  std::string status;  // ok / error / skipped / warned
  std::string error;
  std::string archive_name;
  std::int64_t archive_bytes = 0;
  int deleted_count = 0;
  bool warned = false;
};

class BackupEngine {
 public:
  using LogCb = std::function<void(const std::string&)>;
  using ProgressCb = std::function<void(long long sent, long long total)>;

  BackupRunResult run(BackupJob& job, Config& config, LogCb on_log = {}, ProgressCb on_progress = {},
                      int timeout_sec = 3600);

  /// Установить/обновить cron-скрипт на исходном VPS (same_vps / other_vps).
  BackupRunResult install_cron(BackupJob& job, const Config& config, LogCb on_log = {});
  BackupRunResult uninstall_cron(BackupJob& job, const Config& config, LogCb on_log = {});

 private:
  std::vector<BackupArchiveInfo> list_archives(const BackupJob& job, const Server* dest_server,
                                               const std::filesystem::path* local_dir, LogCb on_log);
  void delete_archives(const BackupJob& job, const Server* dest_server,
                       const std::filesystem::path* local_dir, const std::vector<std::string>& names,
                       LogCb on_log);
  std::int64_t free_bytes_remote(const Server& server, const std::string& path, LogCb on_log);
  std::int64_t free_bytes_local(const std::filesystem::path& path);
  std::string run_ssh(const Server& server, const std::string& command, int timeout_sec, LogCb on_log);
  void ensure_remote_dir(const Server& server, const std::string& path, LogCb on_log);
  void write_manifest(const BackupJob& job, const std::string& archive_name, std::int64_t bytes,
                      const Server* dest_server, const std::filesystem::path* local_dir, LogCb on_log);
};

}  // namespace fatty

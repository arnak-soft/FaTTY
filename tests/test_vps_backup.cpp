#include "tests/test.hpp"
#include "core/vps_backup.hpp"

#include <algorithm>

using namespace fatty;

namespace fatty::test {

void test_vps_backup() {
  Config cfg;
  Server s = Server::make_new();
  s.name = "A";
  s.host = "1.1.1.1";
  cfg.servers.push_back(s);
  Server s2 = Server::make_new();
  s2.name = "B";
  s2.host = "2.2.2.2";
  cfg.servers.push_back(s2);

  BackupJob job = BackupJob::make_new(s.id);
  job.paths = {"/var/www"};
  job.dest_path = "/var/backups/fatty";
  job.interval_sec = 60;
  clamp_backup_job(job);
  expect(job.interval_sec == kBackupIntervalMinSec, "interval clamped to min 1h");

  auto bad = validate_backup_job(job, cfg);
  expect(bad.ok, "valid same_vps job");

  job.paths = {"var/www"};
  expect(!validate_backup_job(job, cfg).ok, "relative source rejected");

  job.paths = {"/var/www"};
  job.dest_path = "/var/www/backups";
  expect(!validate_backup_job(job, cfg).ok, "dest inside source rejected");

  job.dest_path = "/";
  expect(!validate_backup_job(job, cfg).ok, "root dest rejected");

  job.dest_path = "/var/backups/fatty";
  job.dest_kind = BackupDestKind::OtherVps;
  job.dest_server_id = s.id;
  expect(!validate_backup_job(job, cfg).ok, "same server as dest rejected");

  job.dest_server_id = s2.id;
  expect(validate_backup_job(job, cfg).ok, "other_vps ok");

  job.dest_kind = BackupDestKind::LocalPc;
  job.dest_path = "C:/Backups/fatty";
  job.schedule_mode = BackupScheduleMode::Cron;
  expect(!validate_backup_job(job, cfg).ok, "cron+local rejected");

  job.schedule_mode = BackupScheduleMode::Client;
  expect(validate_backup_job(job, cfg).ok, "local_pc client ok");

  job.overflow_at_pct = 100;
  job.overflow_actions = {BackupOverflowAction::Warn};
  expect(!validate_backup_job(job, cfg).ok, "100% without prune rejected");

  expect(path_contains_or_equals("/var/www", "/var/www/app"), "contains child");
  expect(path_contains_or_equals("/var/www", "/var/www"), "equals");
  expect(!path_contains_or_equals("/var/www", "/var/log"), "unrelated");

  BackupJob prune_job = BackupJob::make_new(s.id);
  prune_job.max_total_bytes = 400LL * 1024 * 1024;  // 400 МБ (> минимума 100 МБ)
  prune_job.overflow_at_pct = 80;
  prune_job.overflow_actions = {BackupOverflowAction::DeleteOldest};
  const std::int64_t chunk = 150LL * 1024 * 1024;
  std::vector<BackupArchiveInfo> archives = {
      {"a1", chunk, 1},
      {"a2", chunk, 2},
      {"a3", chunk, 3},
      {"a4", chunk, 4},
  };
  auto plan = plan_backup_prune(prune_job, archives);
  expect(!plan.delete_names.empty(), "oldest prune deletes");
  expect(std::find(plan.delete_names.begin(), plan.delete_names.end(), "a4") == plan.delete_names.end(),
         "newest kept");

  prune_job.overflow_actions = {BackupOverflowAction::DeleteStride};
  prune_job.stride_delete = 3;
  prune_job.stride_keep = 1;
  archives = {
      {"b1", chunk, 1}, {"b2", chunk, 2}, {"b3", chunk, 3}, {"b4", chunk, 4},
      {"b5", chunk, 5}, {"b6", chunk, 6}, {"b7", chunk, 7}, {"b8", chunk, 8},
  };
  plan = plan_backup_prune(prune_job, archives);
  // threshold 800, used 1600; stride deletes 3 keep 1 among all but newest
  expect(std::find(plan.delete_names.begin(), plan.delete_names.end(), "b8") == plan.delete_names.end(),
         "stride keeps newest");
  expect(!plan.delete_names.empty(), "stride deletes some");

  const auto preview = describe_overflow_preview(prune_job);
  expect(preview.find("80%") != std::string::npos, "preview mentions percent");

  job.enabled = true;
  job.last_run_unix = 0;
  job.schedule_mode = BackupScheduleMode::Client;
  expect(backup_job_is_due(job, 100), "enabled never-run is due");
  job.last_run_unix = 100;
  job.interval_sec = 3600;
  expect(!backup_job_is_due(job, 200), "not due yet");
  expect(backup_job_is_due(job, 100 + 3600), "due after interval");

  auto name = backup_archive_basename(job, 1'700'000'000);
  expect(is_backup_archive_name(name, job.id), "archive name matches job");
  expect(backup_manifest_name(name).find(".json") != std::string::npos, "manifest suffix");

  auto tar = build_files_tar_command(job, "/tmp/a.tar.gz");
  expect(tar.find("tar -czf") != std::string::npos, "tar command");

  job.kind = BackupKind::Database;
  job.db_engine = BackupDbEngine::Postgres;
  job.db_name = "app";
  job.db_user = "u";
  job.db_password = "secret";
  auto dump = build_db_dump_command(job, "/tmp/a.sql.gz");
  expect(dump.find("pg_dump") != std::string::npos, "pg_dump");
  expect(dump.find("gzip") != std::string::npos, "gzip");

  job.kind = BackupKind::DockerDb;
  job.docker_container = "pg";
  dump = build_db_dump_command(job, "/tmp/a.sql.gz");
  expect(dump.find("docker exec") != std::string::npos, "docker exec wrap");

  auto cron = build_cron_script(job, s);
  expect(cron.find("#!/usr/bin/env bash") != std::string::npos, "cron shebang");
  expect(cron_entry_line(job).find("fatty-backup-") != std::string::npos, "cron entry tag");
}

}  // namespace fatty::test

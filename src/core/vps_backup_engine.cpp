#include "core/vps_backup_engine.hpp"

#include "core/health.hpp"
#include "core/quote.hpp"
#include "core/util.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fatty {
namespace {

void log_line(const BackupEngine::LogCb& on_log, const std::string& msg) {
  if (on_log) on_log(msg);
}

std::filesystem::path local_temp_dir() {
  auto dir = std::filesystem::temp_directory_path() / "FaTTY" / "backup";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::string last_non_empty_line(const std::string& text) {
  std::string line;
  std::istringstream in(text);
  std::string cur;
  while (std::getline(in, cur)) {
    auto t = trim(cur);
    if (!t.empty()) line = t;
  }
  return line;
}

std::int64_t parse_int64(const std::string& s) {
  try {
    return std::stoll(trim(s));
  } catch (...) {
    return -1;
  }
}

}  // namespace

std::string BackupEngine::run_ssh(const Server& server, const std::string& command, int timeout_sec,
                                  LogCb on_log) {
  SSHSession ssh;
  std::string out;
  auto result = ssh.run(
      server, command, timeout_sec, true,
      [&](const std::string& chunk) {
        out += chunk;
        log_line(on_log, chunk);
      },
      "", server.remote_shell, false);
  if (result.exit_code != 0) {
    throw SSHError("SSH команда завершилась с кодом " + std::to_string(result.exit_code));
  }
  return out;
}

void BackupEngine::ensure_remote_dir(const Server& server, const std::string& path, LogCb on_log) {
  run_ssh(server, "mkdir -p -- " + shlex_quote(path), 60, on_log);
}

std::int64_t BackupEngine::free_bytes_remote(const Server& server, const std::string& path, LogCb on_log) {
  try {
    ensure_remote_dir(server, path, {});
    auto out = run_ssh(server, "df -PB1 -- " + shlex_quote(path) + " | awk 'NR==2{print $4}'", 60, {});
    return parse_int64(last_non_empty_line(out));
  } catch (const std::exception& exc) {
    log_line(on_log, std::string("Не удалось узнать свободное место: ") + exc.what());
    return -1;
  }
}

std::int64_t BackupEngine::free_bytes_local(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  auto space = std::filesystem::space(path, ec);
  if (ec) return -1;
  return static_cast<std::int64_t>(space.available);
}

std::vector<BackupArchiveInfo> BackupEngine::list_archives(const BackupJob& job, const Server* dest_server,
                                                           const std::filesystem::path* local_dir,
                                                           LogCb on_log) {
  std::vector<BackupArchiveInfo> out;
  if (local_dir) {
    std::error_code ec;
    if (!std::filesystem::exists(*local_dir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(*local_dir, ec)) {
      if (ec || !entry.is_regular_file(ec)) continue;
      auto name = entry.path().filename().string();
      if (!is_backup_archive_name(name, job.id)) continue;
      BackupArchiveInfo info;
      info.name = name;
      info.size = static_cast<std::int64_t>(entry.file_size(ec));
      info.mtime = static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(
                                           entry.last_write_time(ec).time_since_epoch())
                                           .count());
      out.push_back(std::move(info));
    }
    return out;
  }
  if (!dest_server) return out;
  try {
    SFTPSession sftp;
    sftp.connect(*dest_server, job.dest_path);
    for (const auto& e : sftp.listdir()) {
      if (e.is_dir) continue;
      if (!is_backup_archive_name(e.name, job.id)) continue;
      BackupArchiveInfo info;
      info.name = e.name;
      info.size = e.size;
      info.mtime = static_cast<double>(e.mtime);
      out.push_back(std::move(info));
    }
    sftp.close();
  } catch (const std::exception& exc) {
    log_line(on_log, std::string("Список копий: ") + exc.what());
  }
  return out;
}

void BackupEngine::delete_archives(const BackupJob& job, const Server* dest_server,
                                   const std::filesystem::path* local_dir,
                                   const std::vector<std::string>& names, LogCb on_log) {
  if (names.empty()) return;
  if (local_dir) {
    for (const auto& name : names) {
      std::error_code ec;
      std::filesystem::remove(*local_dir / name, ec);
      std::filesystem::remove(*local_dir / backup_manifest_name(name), ec);
      log_line(on_log, "Удалено локально: " + name);
    }
    return;
  }
  if (!dest_server) return;
  SFTPSession sftp;
  sftp.connect(*dest_server, job.dest_path);
  auto entries = sftp.listdir();
  for (const auto& name : names) {
    for (const auto& e : entries) {
      if (e.name == name || e.name == backup_manifest_name(name)) {
        try {
          sftp.remove(e);
          log_line(on_log, "Удалено на сервере: " + e.name);
        } catch (const std::exception& exc) {
          log_line(on_log, std::string("Не удалось удалить ") + e.name + ": " + exc.what());
        }
      }
    }
  }
  sftp.close();
}

void BackupEngine::write_manifest(const BackupJob& job, const std::string& archive_name,
                                  std::int64_t bytes, const Server* dest_server,
                                  const std::filesystem::path* local_dir, LogCb on_log) {
  nlohmann::json man = {
      {"job_id", job.id},
      {"job_name", job.name},
      {"kind", backup_kind_key(job.kind)},
      {"archive", archive_name},
      {"bytes", bytes},
      {"created_unix", unix_now()},
      {"dest_kind", backup_dest_key(job.dest_kind)},
      {"paths", job.paths},
      {"db_name", job.db_name},
      {"docker_container", job.docker_container},
  };
  const auto text = man.dump(2);
  const auto man_name = backup_manifest_name(archive_name);
  if (local_dir) {
    std::ofstream f(*local_dir / man_name, std::ios::binary);
    f << text;
    return;
  }
  if (!dest_server) return;
  auto tmp = local_temp_dir() / man_name;
  {
    std::ofstream f(tmp, std::ios::binary);
    f << text;
  }
  try {
    SFTPSession sftp;
    sftp.connect(*dest_server, job.dest_path);
    sftp.upload(tmp, man_name, {});
    sftp.close();
  } catch (const std::exception& exc) {
    log_line(on_log, std::string("Манифест не записан: ") + exc.what());
  }
  std::error_code ec;
  std::filesystem::remove(tmp, ec);
}

BackupRunResult BackupEngine::run(BackupJob& job, Config& config, LogCb on_log, ProgressCb on_progress,
                                  int timeout_sec) {
  BackupRunResult result;
  clamp_backup_job(job);
  auto val = validate_backup_job(job, config);
  if (!val.ok) {
    result.status = "error";
    result.error = val.errors.empty() ? "Некорректные настройки" : val.errors.front();
    job.last_status = result.status;
    job.last_error = result.error;
    job.last_run_unix = unix_now();
    return result;
  }
  for (const auto& w : val.warnings) log_line(on_log, "Предупреждение: " + w);

  const Server* source = config.server_by_id(job.source_server_id);
  if (!source) {
    result.status = "error";
    result.error = "Исходный VPS не найден";
    return result;
  }
  const Server* dest_server = nullptr;
  std::filesystem::path local_dir;
  const std::filesystem::path* local_ptr = nullptr;
  if (job.dest_kind == BackupDestKind::LocalPc) {
    local_dir = std::filesystem::path(job.dest_path);
    local_ptr = &local_dir;
  } else if (job.dest_kind == BackupDestKind::SameVps) {
    dest_server = source;
  } else {
    dest_server = config.server_by_id(job.dest_server_id);
    if (!dest_server) {
      result.status = "error";
      result.error = "VPS назначения не найден";
      return result;
    }
  }

  try {
    std::int64_t free_b = -1;
    if (local_ptr)
      free_b = free_bytes_local(*local_ptr);
    else if (dest_server)
      free_b = free_bytes_remote(*dest_server, job.dest_path, on_log);
    if (free_b >= 0 && job.min_dest_free_bytes > 0 && free_b < job.min_dest_free_bytes) {
      result.status = "error";
      result.error = "Недостаточно свободного места в папке назначения";
      job.last_status = result.status;
      job.last_error = result.error;
      job.last_run_unix = unix_now();
      return result;
    }

    auto archives = list_archives(job, dest_server, local_ptr, on_log);
    auto plan = plan_backup_prune(job, archives);
    if (plan.warned) {
      result.warned = true;
      log_line(on_log, plan.message);
    }
    if (!plan.delete_names.empty()) {
      delete_archives(job, dest_server, local_ptr, plan.delete_names, on_log);
      result.deleted_count = static_cast<int>(plan.delete_names.size());
    }
    if (plan.blocks_new) {
      result.skipped = true;
      result.status = "skipped";
      result.error = "Лимит копий исчерпан даже после очистки";
      job.last_status = result.status;
      job.last_error = result.error;
      job.last_run_unix = unix_now();
      return result;
    }

    const double now = unix_now();
    const auto archive_name = backup_archive_basename(job, now);
    const auto home = trim(last_non_empty_line(run_ssh(*source, "printf %s \"$HOME\"", 30, {})));
    if (home.empty() || home.front() != '/') throw SSHError("Не удалось определить домашний каталог на VPS");
    const auto staging_dir = posix_join(home, kBackupStagingRel);
    const auto staging_path = posix_join(staging_dir, archive_name);

    log_line(on_log, "Создание архива на источнике…");
    ensure_remote_dir(*source, staging_dir, on_log);

    std::string create_cmd;
    if (job.kind == BackupKind::Files)
      create_cmd = build_files_tar_command(job, staging_path);
    else
      create_cmd = build_db_dump_command(job, staging_path);

    auto create_out = run_ssh(*source, create_cmd, timeout_sec, on_log);
    std::int64_t archive_bytes = parse_int64(last_non_empty_line(create_out));
    if (archive_bytes < 0) {
      // fallback size via stat
      auto sz = run_ssh(*source, "stat -c %s -- " + shlex_quote(staging_path), 60, {});
      archive_bytes = parse_int64(last_non_empty_line(sz));
    }
    if (archive_bytes < 0) throw SSHError("Не удалось определить размер архива");
    if (job.max_single_archive_bytes > 0 && archive_bytes > job.max_single_archive_bytes) {
      run_ssh(*source, "rm -f -- " + shlex_quote(staging_path), 60, {});
      result.status = "error";
      result.error = "Архив больше лимита одной копии";
      job.last_status = result.status;
      job.last_error = result.error;
      job.last_run_unix = now;
      return result;
    }

    if (job.dest_kind == BackupDestKind::SameVps) {
      ensure_remote_dir(*source, job.dest_path, on_log);
      run_ssh(*source,
              "mv -f -- " + shlex_quote(staging_path) + " " + shlex_quote(posix_join(job.dest_path, archive_name)),
              120, on_log);
      write_manifest(job, archive_name, archive_bytes, source, nullptr, on_log);
    } else if (job.dest_kind == BackupDestKind::LocalPc) {
      std::error_code ec;
      std::filesystem::create_directories(local_dir, ec);
      SFTPSession sftp;
      // expand ~ on remote via SSH first
      auto abs = trim(last_non_empty_line(
          run_ssh(*source, "readlink -f -- " + shlex_quote(staging_path) + " || realpath -- " + shlex_quote(staging_path),
                  60, {})));
      if (abs.empty()) abs = staging_path;
      const auto remote_dir = posix_parent(abs);
      const auto remote_name = abs.substr(remote_dir.size() + (remote_dir == "/" ? 0 : 1));
      sftp.connect(*source, remote_dir);
      const auto local_file = local_dir / archive_name;
      log_line(on_log, "Скачивание на этот компьютер…");
      sftp.download(remote_name, local_file, archive_bytes, on_progress);
      sftp.close();
      run_ssh(*source, "rm -f -- " + shlex_quote(staging_path), 60, {});
      write_manifest(job, archive_name, archive_bytes, nullptr, local_ptr, on_log);
    } else {
      // other_vps via local temp
      auto tmp = local_temp_dir() / archive_name;
      {
        SFTPSession sftp;
        auto abs = trim(last_non_empty_line(run_ssh(
            *source, "readlink -f -- " + shlex_quote(staging_path) + " || realpath -- " + shlex_quote(staging_path), 60,
            {})));
        if (abs.empty()) abs = staging_path;
        const auto remote_dir = posix_parent(abs);
        const auto remote_name = abs.substr(abs.find_last_of('/') + 1);
        sftp.connect(*source, remote_dir);
        log_line(on_log, "Скачивание во временную папку ПК…");
        sftp.download(remote_name, tmp, archive_bytes, on_progress);
        sftp.close();
      }
      run_ssh(*source, "rm -f -- " + shlex_quote(staging_path), 60, {});
      ensure_remote_dir(*dest_server, job.dest_path, on_log);
      {
        SFTPSession sftp;
        sftp.connect(*dest_server, job.dest_path);
        log_line(on_log, "Загрузка на VPS назначения…");
        sftp.upload(tmp, archive_name, on_progress);
        sftp.close();
      }
      std::error_code ec;
      std::filesystem::remove(tmp, ec);
      write_manifest(job, archive_name, archive_bytes, dest_server, nullptr, on_log);
    }

    // post prune if needed
    archives = list_archives(job, dest_server, local_ptr, on_log);
    plan = plan_backup_prune(job, archives);
    if (!plan.delete_names.empty()) {
      delete_archives(job, dest_server, local_ptr, plan.delete_names, on_log);
      result.deleted_count += static_cast<int>(plan.delete_names.size());
    }

    result.ok = true;
    result.status = result.warned ? "warned" : "ok";
    result.archive_name = archive_name;
    result.archive_bytes = archive_bytes;
    job.last_status = result.status;
    job.last_error.clear();
    job.last_archive_name = archive_name;
    job.last_archive_bytes = archive_bytes;
    job.last_run_unix = now;
    log_line(on_log, "Готово: " + archive_name);
  } catch (const std::exception& exc) {
    result.status = "error";
    result.error = exc.what();
    job.last_status = result.status;
    job.last_error = result.error;
    job.last_run_unix = unix_now();
    log_line(on_log, std::string("Ошибка: ") + exc.what());
  }
  return result;
}

BackupRunResult BackupEngine::install_cron(BackupJob& job, const Config& config, LogCb on_log) {
  BackupRunResult result;
  clamp_backup_job(job);
  if (job.dest_kind == BackupDestKind::LocalPc) {
    result.status = "error";
    result.error = "Cron недоступен для копирования на этот компьютер";
    return result;
  }
  auto val = validate_backup_job(job, config);
  if (!val.ok) {
    result.status = "error";
    result.error = val.errors.empty() ? "Некорректные настройки" : val.errors.front();
    return result;
  }
  const Server* source = config.server_by_id(job.source_server_id);
  if (!source) {
    result.status = "error";
    result.error = "Исходный VPS не найден";
    return result;
  }
  try {
    job.schedule_mode = BackupScheduleMode::Cron;
    const auto script = build_cron_script(job, *source);
    const auto tag = "fatty-backup-" + backup_job_short_id(job.id);
    auto tmp = local_temp_dir() / (backup_job_short_id(job.id) + ".sh");
    {
      std::ofstream f(tmp, std::ios::binary);
      f << script;
    }
    const auto cron_dir_abs = trim(last_non_empty_line(
        run_ssh(*source, "mkdir -p -- \"$HOME/" + std::string(kBackupCronDirRel) + "\" && cd \"$HOME/" +
                             std::string(kBackupCronDirRel) + "\" && pwd",
                60, on_log)));
    if (cron_dir_abs.empty() || cron_dir_abs.front() != '/') throw SSHError("Не удалось создать каталог cron-скриптов");
    const auto remote_script = posix_join(cron_dir_abs, backup_job_short_id(job.id) + ".sh");
    {
      SFTPSession sftp;
      sftp.connect(*source, cron_dir_abs);
      sftp.upload(tmp, backup_job_short_id(job.id) + ".sh", {});
      sftp.close();
    }
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    std::ostringstream entry;
    entry << cron_entry_line(job);
    // Подменяем относительный ~/ путь на абсолютный в crontab.
    auto entry_line = entry.str();
    const auto soft = cron_script_remote_path(job);
    auto pos = entry_line.find(soft);
    if (pos != std::string::npos) entry_line.replace(pos, soft.size(), remote_script);
    std::ostringstream cmd;
    cmd << "chmod 700 -- " << shlex_quote(remote_script) << "; ";
    cmd << "tmp=$(mktemp); crontab -l 2>/dev/null | grep -v " << shlex_quote(tag) << " > \"$tmp\" || true; ";
    cmd << "echo " << shlex_quote(entry_line) << " >> \"$tmp\"; ";
    cmd << "crontab \"$tmp\"; rm -f \"$tmp\"";
    run_ssh(*source, cmd.str(), 60, on_log);
    job.cron_installed = true;
    job.schedule_mode = BackupScheduleMode::Cron;
    result.ok = true;
    result.status = "ok";
    log_line(on_log, "Cron установлен: " + remote_script);
  } catch (const std::exception& exc) {
    result.status = "error";
    result.error = exc.what();
    log_line(on_log, std::string("Ошибка cron: ") + exc.what());
  }
  return result;
}

BackupRunResult BackupEngine::uninstall_cron(BackupJob& job, const Config& config, LogCb on_log) {
  BackupRunResult result;
  const Server* source = config.server_by_id(job.source_server_id);
  if (!source) {
    result.status = "error";
    result.error = "Исходный VPS не найден";
    return result;
  }
  try {
    const auto tag = "fatty-backup-" + backup_job_short_id(job.id);
    const auto remote_script = cron_script_remote_path(job);
    std::ostringstream cmd;
    cmd << "tmp=$(mktemp); crontab -l 2>/dev/null | grep -v " << shlex_quote(tag)
        << " > \"$tmp\" || true; crontab \"$tmp\" 2>/dev/null || true; rm -f \"$tmp\" "
        << shlex_quote(remote_script);
    run_ssh(*source, cmd.str(), 60, on_log);
    job.cron_installed = false;
    job.schedule_mode = BackupScheduleMode::Client;
    result.ok = true;
    result.status = "ok";
    log_line(on_log, "Cron снят");
  } catch (const std::exception& exc) {
    result.status = "error";
    result.error = exc.what();
  }
  return result;
}

}  // namespace fatty

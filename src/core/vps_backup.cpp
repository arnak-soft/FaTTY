#include "core/vps_backup.hpp"

#include "core/quote.hpp"
#include "core/util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <sstream>

namespace fatty {
namespace {

std::string normalize_slashes(std::string path) {
  for (char& c : path) {
    if (c == '\\') c = '/';
  }
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  return path;
}

std::string stamp_from_unix(double unix_ts) {
  const auto sec = static_cast<std::time_t>(unix_ts);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &sec);
#else
  gmtime_r(&sec, &tm);
#endif
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

bool has_action(const std::vector<BackupOverflowAction>& actions, BackupOverflowAction want) {
  return std::find(actions.begin(), actions.end(), want) != actions.end();
}

}  // namespace

void BackupValidation::fail(std::string msg) {
  ok = false;
  errors.push_back(std::move(msg));
}

void BackupValidation::warn(std::string msg) { warnings.push_back(std::move(msg)); }

BackupJob BackupJob::make_new(const std::string& source_server_id) {
  BackupJob j;
  j.id = new_uuid();
  j.name = "Резервная копия";
  j.source_server_id = source_server_id;
  j.dest_path = "/var/backups/fatty";
  return j;
}

BackupJob BackupJob::duplicate(const std::string& new_name) const {
  BackupJob j = *this;
  j.id = new_uuid();
  if (!new_name.empty()) j.name = new_name;
  j.cron_installed = false;
  j.last_run_unix = 0;
  j.last_status.clear();
  j.last_error.clear();
  j.last_archive_name.clear();
  j.last_archive_bytes = 0;
  j.db_password_blob.clear();
  return j;
}

std::string backup_kind_key(BackupKind kind) {
  switch (kind) {
    case BackupKind::Database:
      return "database";
    case BackupKind::DockerDb:
      return "docker_db";
    case BackupKind::Files:
    default:
      return "files";
  }
}

BackupKind backup_kind_from_key(std::string_view key) {
  if (key == "database") return BackupKind::Database;
  if (key == "docker_db") return BackupKind::DockerDb;
  return BackupKind::Files;
}

std::string backup_dest_key(BackupDestKind kind) {
  switch (kind) {
    case BackupDestKind::OtherVps:
      return "other_vps";
    case BackupDestKind::LocalPc:
      return "local_pc";
    case BackupDestKind::SameVps:
    default:
      return "same_vps";
  }
}

BackupDestKind backup_dest_from_key(std::string_view key) {
  if (key == "other_vps") return BackupDestKind::OtherVps;
  if (key == "local_pc") return BackupDestKind::LocalPc;
  return BackupDestKind::SameVps;
}

std::string backup_schedule_key(BackupScheduleMode mode) {
  return mode == BackupScheduleMode::Cron ? "cron" : "client";
}

BackupScheduleMode backup_schedule_from_key(std::string_view key) {
  return key == "cron" ? BackupScheduleMode::Cron : BackupScheduleMode::Client;
}

std::string backup_action_key(BackupOverflowAction action) {
  switch (action) {
    case BackupOverflowAction::DeleteOldest:
      return "delete_oldest";
    case BackupOverflowAction::DeleteBytes:
      return "delete_bytes";
    case BackupOverflowAction::DeleteStride:
      return "delete_stride";
    case BackupOverflowAction::Warn:
    default:
      return "warn";
  }
}

BackupOverflowAction backup_action_from_key(std::string_view key) {
  if (key == "delete_oldest") return BackupOverflowAction::DeleteOldest;
  if (key == "delete_bytes") return BackupOverflowAction::DeleteBytes;
  if (key == "delete_stride") return BackupOverflowAction::DeleteStride;
  return BackupOverflowAction::Warn;
}

std::string backup_db_engine_key(BackupDbEngine engine) {
  switch (engine) {
    case BackupDbEngine::Mysql:
      return "mysql";
    case BackupDbEngine::Mariadb:
      return "mariadb";
    case BackupDbEngine::Postgres:
    default:
      return "postgres";
  }
}

BackupDbEngine backup_db_engine_from_key(std::string_view key) {
  if (key == "mysql") return BackupDbEngine::Mysql;
  if (key == "mariadb") return BackupDbEngine::Mariadb;
  return BackupDbEngine::Postgres;
}

std::string backup_kind_label(BackupKind kind) {
  switch (kind) {
    case BackupKind::Database:
      return "База данных";
    case BackupKind::DockerDb:
      return "БД в Docker";
    case BackupKind::Files:
    default:
      return "Папки";
  }
}

std::string backup_dest_label(BackupDestKind kind) {
  switch (kind) {
    case BackupDestKind::OtherVps:
      return "Другая VPS";
    case BackupDestKind::LocalPc:
      return "Этот компьютер";
    case BackupDestKind::SameVps:
    default:
      return "Этот VPS";
  }
}

void clamp_backup_job(BackupJob& job) {
  job.name = trim(job.name);
  job.source_server_id = trim(job.source_server_id);
  job.dest_path = trim(job.dest_path);
  job.dest_server_id = trim(job.dest_server_id);
  job.db_name = trim(job.db_name);
  job.db_user = trim(job.db_user);
  job.docker_container = trim(job.docker_container);
  job.interval_sec = clamp_int(job.interval_sec, kBackupIntervalMinSec, kBackupIntervalMaxSec);
  job.overflow_at_pct = clamp_int(job.overflow_at_pct, 1, 100);
  job.stride_delete = clamp_int(job.stride_delete, 1, 100);
  job.stride_keep = clamp_int(job.stride_keep, 1, 100);
  job.db_port = clamp_int(job.db_port, 0, 65535);
  if (job.max_total_bytes < kBackupMaxTotalMinBytes) job.max_total_bytes = kBackupMaxTotalMinBytes;
  if (job.delete_bytes_amount < 0) job.delete_bytes_amount = 0;
  if (job.max_single_archive_bytes < 0) job.max_single_archive_bytes = 0;
  if (job.min_dest_free_bytes < 0) job.min_dest_free_bytes = 0;
  if (job.overflow_actions.empty()) {
    job.overflow_actions = {BackupOverflowAction::Warn, BackupOverflowAction::DeleteOldest};
  }
  std::vector<std::string> clean_paths;
  for (auto& p : job.paths) {
    auto t = trim(p);
    if (!t.empty()) clean_paths.push_back(std::move(t));
  }
  job.paths = std::move(clean_paths);
  std::vector<std::string> clean_ex;
  for (auto& p : job.exclude_globs) {
    auto t = trim(p);
    if (!t.empty()) clean_ex.push_back(std::move(t));
  }
  job.exclude_globs = std::move(clean_ex);
}

bool path_is_filesystem_root(std::string_view path) {
  auto p = normalize_slashes(std::string(path));
  if (p == "/" || p == "//") return true;
  if (p.size() == 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':') return true;
  if (p.size() == 3 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':' && p[2] == '/') return true;
  return false;
}

bool path_is_absolute_posix(std::string_view path) {
  return !path.empty() && path.front() == '/';
}

bool path_is_absolute_windows(std::string_view path) {
  if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
      (path[2] == '\\' || path[2] == '/')) {
    return true;
  }
  return path.size() >= 2 && path[0] == '\\' && path[1] == '\\';
}

bool path_contains_or_equals(std::string_view outer_raw, std::string_view inner_raw) {
  auto outer = normalize_slashes(std::string(outer_raw));
  auto inner = normalize_slashes(std::string(inner_raw));
  if (outer.empty() || inner.empty()) return false;
  if (outer == inner) return true;
  if (outer.back() != '/') outer.push_back('/');
  return inner.rfind(outer, 0) == 0;
}

BackupValidation validate_backup_job(const BackupJob& job_in, const Config& config) {
  BackupJob job = job_in;
  clamp_backup_job(job);
  BackupValidation v;
  if (job.name.empty()) v.fail("Укажите название задания");
  if (job.source_server_id.empty() || !config.server_by_id(job.source_server_id)) {
    v.fail("Выберите исходный VPS");
  }
  if (job.dest_path.empty()) v.fail("Укажите папку назначения");
  if (path_is_filesystem_root(job.dest_path)) v.fail("Нельзя писать копии в корень диска");

  if (job.kind == BackupKind::Files) {
    if (job.paths.empty()) v.fail("Добавьте хотя бы одну папку для копирования");
    for (const auto& p : job.paths) {
      if (!path_is_absolute_posix(p)) v.fail("Путь источника должен быть абсолютным (начинается с /): " + p);
      if (path_is_filesystem_root(p)) v.fail("Нельзя копировать весь корень ФС: " + p);
      if (job.dest_kind != BackupDestKind::LocalPc) {
        if (path_contains_or_equals(p, job.dest_path) || path_contains_or_equals(job.dest_path, p)) {
          v.fail("Папка назначения не должна совпадать с источником и не должна быть внутри него");
        }
      }
    }
  } else {
    if (job.kind == BackupKind::DockerDb && job.docker_container.empty()) {
      v.fail("Укажите Docker-контейнер с базой");
    }
  }

  if (job.dest_kind == BackupDestKind::SameVps || job.dest_kind == BackupDestKind::OtherVps) {
    if (!path_is_absolute_posix(job.dest_path)) {
      v.fail("Путь на VPS должен быть абсолютным (начинается с /)");
    }
  }
  if (job.dest_kind == BackupDestKind::LocalPc) {
    if (!path_is_absolute_windows(job.dest_path) && !path_is_absolute_posix(job.dest_path)) {
      v.fail("Укажите абсолютный путь на этом компьютере");
    }
  }
  if (job.dest_kind == BackupDestKind::OtherVps) {
    if (job.dest_server_id.empty() || !config.server_by_id(job.dest_server_id)) {
      v.fail("Выберите VPS назначения");
    } else if (job.dest_server_id == job.source_server_id) {
      v.fail("VPS назначения должен отличаться от источника");
    }
  } else {
    job.dest_server_id.clear();
  }

  if (job.schedule_mode == BackupScheduleMode::Cron && job.dest_kind == BackupDestKind::LocalPc) {
    v.fail("Расписание cron нельзя использовать для копирования на этот компьютер");
  }
  if (job.schedule_mode == BackupScheduleMode::Cron && job.dest_kind == BackupDestKind::OtherVps) {
    v.warn("Для cron на другую VPS нужен SSH-ключ между серверами; иначе оставьте режим «пока FaTTY открыт»");
  }

  const bool has_prune = has_action(job.overflow_actions, BackupOverflowAction::DeleteOldest) ||
                         has_action(job.overflow_actions, BackupOverflowAction::DeleteBytes) ||
                         has_action(job.overflow_actions, BackupOverflowAction::DeleteStride);
  if (job.overflow_actions.empty()) {
    v.fail("Выберите хотя бы одно действие при переполнении");
  } else if (!has_action(job.overflow_actions, BackupOverflowAction::Warn) && !has_prune) {
    v.fail("Нужно предупреждение или действие очистки");
  }
  if (job.overflow_at_pct >= 100 && !has_prune) {
    v.fail("При пороге 100% обязательно действие очистки (иначе новый бэкап сразу упрётся в лимит)");
  }
  if (job.interval_sec < 6 * 3600) {
    v.warn("Частый интервал повышает нагрузку на диск и CPU сервера");
  }
  if (job.dest_kind == BackupDestKind::OtherVps && job.schedule_mode == BackupScheduleMode::Client) {
    v.warn("Копия на другую VPS идёт через ваш ПК: трафик удваивается, нужен диск для временного файла");
  }
  return v;
}

bool backup_job_is_due(const BackupJob& job, double now_unix) {
  if (!job.enabled) return false;
  if (job.schedule_mode != BackupScheduleMode::Client) return false;
  if (job.last_run_unix <= 0) return true;
  return now_unix >= job.last_run_unix + static_cast<double>(job.interval_sec);
}

std::string backup_job_short_id(std::string_view job_id) {
  std::string out;
  for (char c : job_id) {
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) out.push_back(c);
    if (out.size() >= 8) break;
  }
  if (out.empty()) out = "job";
  return out;
}

std::string backup_archive_basename(const BackupJob& job, double unix_ts) {
  const char* ext = (job.kind == BackupKind::Files) ? "tar.gz" : "sql.gz";
  return "fatty-backup-" + backup_job_short_id(job.id) + "-" + stamp_from_unix(unix_ts) + "." + ext;
}

std::string backup_manifest_name(std::string_view archive_name) {
  return std::string(archive_name) + ".json";
}

bool is_backup_archive_name(std::string_view name, std::string_view job_id) {
  const auto prefix = "fatty-backup-" + backup_job_short_id(job_id) + "-";
  if (name.rfind(prefix, 0) != 0) return false;
  return name.size() > prefix.size() &&
         (name.find(".tar.gz") != std::string_view::npos || name.find(".sql.gz") != std::string_view::npos) &&
         name.find(".json") == std::string_view::npos;
}

std::int64_t backup_archives_total_bytes(const std::vector<BackupArchiveInfo>& archives) {
  std::int64_t sum = 0;
  for (const auto& a : archives) sum += std::max<std::int64_t>(0, a.size);
  return sum;
}

PrunePlan plan_backup_prune(const BackupJob& job_in, std::vector<BackupArchiveInfo> archives) {
  BackupJob job = job_in;
  clamp_backup_job(job);
  std::sort(archives.begin(), archives.end(), [](const BackupArchiveInfo& a, const BackupArchiveInfo& b) {
    if (a.mtime != b.mtime) return a.mtime < b.mtime;
    return a.name < b.name;
  });

  PrunePlan plan;
  plan.used_before = backup_archives_total_bytes(archives);
  plan.used_after = plan.used_before;
  if (job.max_total_bytes <= 0 || archives.empty()) return plan;

  const auto threshold =
      static_cast<std::int64_t>(std::floor(static_cast<double>(job.max_total_bytes) * (job.overflow_at_pct / 100.0)));
  if (plan.used_before < threshold) return plan;

  auto mark_delete = [&](std::size_t idx) {
    if (idx >= archives.size()) return;
    if (archives.size() <= 1) return;
    if (idx + 1 == archives.size()) return;  // never delete newest
    const auto& name = archives[idx].name;
    if (std::find(plan.delete_names.begin(), plan.delete_names.end(), name) != plan.delete_names.end()) return;
    plan.delete_names.push_back(name);
    plan.used_after -= archives[idx].size;
    archives[idx].size = 0;
  };

  auto recompute_used = [&] {
    plan.used_after = 0;
    for (const auto& a : archives) plan.used_after += a.size;
  };

  for (auto action : job.overflow_actions) {
    recompute_used();
    if (plan.used_after < threshold) break;
    switch (action) {
      case BackupOverflowAction::Warn:
        plan.warned = true;
        break;
      case BackupOverflowAction::DeleteOldest:
        while (plan.used_after >= threshold && archives.size() > 1) {
          std::size_t victim = archives.size();
          for (std::size_t i = 0; i + 1 < archives.size(); ++i) {
            if (archives[i].size > 0) {
              victim = i;
              break;
            }
          }
          if (victim >= archives.size()) break;
          mark_delete(victim);
          recompute_used();
        }
        break;
      case BackupOverflowAction::DeleteBytes: {
        std::int64_t need = job.delete_bytes_amount;
        for (std::size_t i = 0; i + 1 < archives.size() && need > 0; ++i) {
          if (archives[i].size <= 0) continue;
          need -= archives[i].size;
          mark_delete(i);
        }
        recompute_used();
        break;
      }
      case BackupOverflowAction::DeleteStride: {
        const int cycle = job.stride_delete + job.stride_keep;
        int pos = 0;
        for (std::size_t i = 0; i + 1 < archives.size(); ++i) {
          if (archives[i].size <= 0) continue;
          const int slot = pos % cycle;
          ++pos;
          if (slot < job.stride_delete) mark_delete(i);
        }
        recompute_used();
        break;
      }
    }
  }

  recompute_used();
  if (plan.used_after >= job.max_total_bytes) plan.blocks_new = true;

  std::ostringstream msg;
  msg << "Использовано " << plan.used_before << " из " << job.max_total_bytes << " байт (порог "
      << job.overflow_at_pct << "%). К удалению: " << plan.delete_names.size();
  if (plan.blocks_new) msg << ". Лимит всё ещё исчерпан — новый бэкап заблокирован";
  plan.message = msg.str();
  return plan;
}

std::string describe_stride_policy(int delete_n, int keep_m) {
  delete_n = clamp_int(delete_n, 1, 100);
  keep_m = clamp_int(keep_m, 1, 100);
  std::ostringstream ss;
  ss << "Удалить " << delete_n << ", затем оставить " << keep_m << ", и так по кругу "
     << "(самую новую копию не трогаем)";
  return ss.str();
}

std::string describe_overflow_preview(const BackupJob& job_in) {
  BackupJob job = job_in;
  clamp_backup_job(job);
  const double thr = static_cast<double>(job.max_total_bytes) * (job.overflow_at_pct / 100.0);
  const double gb = job.max_total_bytes / (1024.0 * 1024.0 * 1024.0);
  const double thr_gb = thr / (1024.0 * 1024.0 * 1024.0);
  std::ostringstream ss;
  ss.setf(std::ios::fixed);
  ss.precision(1);
  ss << "При " << job.overflow_at_pct << "% от " << gb << " ГБ (~" << thr_gb << " ГБ)";
  if (has_action(job.overflow_actions, BackupOverflowAction::DeleteStride)) {
    ss << " — " << describe_stride_policy(job.stride_delete, job.stride_keep);
  } else if (has_action(job.overflow_actions, BackupOverflowAction::DeleteOldest)) {
    ss << " — удалять самые старые";
  } else if (has_action(job.overflow_actions, BackupOverflowAction::DeleteBytes)) {
    ss << " — удалить заданный объём старых копий";
  } else {
    ss << " — только предупреждение";
  }
  return ss.str();
}

std::string build_files_tar_command(const BackupJob& job, const std::string& archive_path) {
  std::ostringstream ss;
  ss << "set -euo pipefail; mkdir -p -- " << shlex_quote(posix_parent(archive_path)) << "; ";
  ss << "tar -czf " << shlex_quote(archive_path);
  for (const auto& ex : job.exclude_globs) {
    ss << " --exclude=" << shlex_quote(ex);
  }
  for (const auto& p : job.paths) {
    ss << " -C / " << shlex_quote(p.front() == '/' ? p.substr(1) : p);
  }
  ss << "; test -f " << shlex_quote(archive_path) << "; stat -c %s " << shlex_quote(archive_path);
  return ss.str();
}

std::string build_db_dump_command(const BackupJob& job, const std::string& archive_path) {
  std::ostringstream inner;
  const bool docker = job.kind == BackupKind::DockerDb;
  auto wrap = [&](const std::string& cmd) {
    if (!docker) return cmd;
    return "docker exec " + shlex_quote(job.docker_container) + " sh -lc " + shlex_quote(cmd);
  };

  std::string dump_cmd;
  if (job.db_engine == BackupDbEngine::Postgres) {
    if (job.db_name.empty()) {
      std::ostringstream d;
      d << "PGPASSWORD=" << shlex_quote(job.db_password) << " pg_dumpall";
      if (!job.db_user.empty()) d << " -U " << shlex_quote(job.db_user);
      if (job.db_port > 0) d << " -p " << job.db_port;
      dump_cmd = d.str();
    } else {
      std::ostringstream d;
      d << "PGPASSWORD=" << shlex_quote(job.db_password) << " pg_dump";
      if (!job.db_user.empty()) d << " -U " << shlex_quote(job.db_user);
      if (job.db_port > 0) d << " -p " << job.db_port;
      d << " " << shlex_quote(job.db_name);
      dump_cmd = d.str();
    }
  } else {
    std::ostringstream d;
    d << "MYSQL_PWD=" << shlex_quote(job.db_password) << " mysqldump";
    if (!job.db_user.empty()) d << " -u" << shlex_quote(job.db_user);
    if (job.db_port > 0) d << " -P" << job.db_port;
    if (job.db_name.empty())
      d << " --all-databases";
    else
      d << " " << shlex_quote(job.db_name);
    dump_cmd = d.str();
  }

  std::ostringstream ss;
  ss << "set -euo pipefail; mkdir -p -- " << shlex_quote(posix_parent(archive_path)) << "; ";
  ss << wrap(dump_cmd) << " | gzip -c > " << shlex_quote(archive_path);
  ss << "; test -f " << shlex_quote(archive_path) << "; stat -c %s " << shlex_quote(archive_path);
  return ss.str();
}

std::string cron_script_remote_path(const BackupJob& job) {
  return std::string("~/") + kBackupCronDirRel + "/" + backup_job_short_id(job.id) + ".sh";
}

std::string cron_entry_line(const BackupJob& job, int hour, int minute) {
  // Approximate daily-ish from interval; for client we don't use this.
  int interval_h = std::max(1, job.interval_sec / 3600);
  std::ostringstream ss;
  if (interval_h >= 24) {
    ss << minute << " " << hour << " * * * ";
  } else {
    ss << minute << " */" << interval_h << " * * * ";
  }
  ss << cron_script_remote_path(job) << " # fatty-backup-" << backup_job_short_id(job.id);
  return ss.str();
}

std::string build_cron_script(const BackupJob& job, const Server& source) {
  (void)source;
  const auto archive = std::string("$DEST_DIR/") + "fatty-backup-" + backup_job_short_id(job.id) +
                       "-$(date -u +%Y%m%d-%H%M%S)." + (job.kind == BackupKind::Files ? "tar.gz" : "sql.gz");
  std::ostringstream ss;
  ss << "#!/usr/bin/env bash\nset -euo pipefail\n";
  ss << "DEST_DIR=" << shlex_quote(job.dest_path) << "\n";
  ss << "mkdir -p -- \"$DEST_DIR\"\n";
  ss << "ARCHIVE=" << archive << "\n";
  if (job.kind == BackupKind::Files) {
    ss << "tar -czf \"$ARCHIVE\"";
    for (const auto& ex : job.exclude_globs) ss << " --exclude=" << shlex_quote(ex);
    for (const auto& p : job.paths) {
      ss << " -C / " << shlex_quote(p.front() == '/' ? p.substr(1) : p);
    }
    ss << "\n";
  } else {
    // Reuse dump builder but replace archive path with $ARCHIVE via env is hard; inline simplified.
    ss << build_db_dump_command(job, "__ARCHIVE__") << "\n";
  }
  ss << "echo \"ok $ARCHIVE\"\n";
  auto body = ss.str();
  const auto needle = std::string("__ARCHIVE__");
  for (;;) {
    auto pos = body.find(needle);
    if (pos == std::string::npos) break;
    body.replace(pos, needle.size(), "$ARCHIVE");
  }
  return body;
}

}  // namespace fatty

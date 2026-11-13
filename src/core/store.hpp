#pragma once

#include "core/health.hpp"
#include "core/vault.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fatty {

struct Server {
  std::string id;
  std::string name;
  std::string host;
  int port = 22;
  std::string username = "root";
  std::string password;
  std::string password_blob;
  std::string key_path;
  std::string remote_shell = "bash";
  bool health_enabled = true;
  HealthAlertIgnore health_ignore;
  /// Акцент окна для тёмной / светлой темы: `#RRGGBB` или пусто.
  std::string ui_colour_dark;
  std::string ui_colour_light;
  /// Семейство шрифта для списков команд/связок; пусто — шрифт темы.
  std::string ui_font;
  /// Кегль списков; 0 — как в теме.
  int ui_font_pt = 0;

  static Server make_new();
  Server duplicate(const std::string& new_name) const;
};

/// Готовая пара акцента: насыщенный для тёмной темы, более глубокий — для светлой.
struct ServerUiColourPair {
  const char* dark = "";
  const char* light = "";
  const char* name = "";  // русское имя для палитры
};

/// 14 заметно разных оттенков для карточки VPS.
const ServerUiColourPair* server_ui_colour_palette(std::size_t& count);

/// Подбирает пару цветов (тёмная / светлая тема), ещё не занятую существующими VPS.
void suggest_server_ui_colours(const std::vector<Server>& existing, std::string& out_dark,
                               std::string& out_light);

struct Command {
  std::string id;
  std::string name;
  std::string comment;
  std::string server_id;
  std::string group_id;
  std::string command;
  std::string working_dir;
  int timeout_sec = 180;
  bool login_shell = true;
  bool confirm_before_run = true;
  bool cd_before_run = true;
  std::string remote_shell;

  static Command make_new(const std::string& server_id);
  Command duplicate(const std::string& new_name = "", const std::string& new_server_id = "") const;
  std::string effective_cwd(const std::string& session_cwd, std::string_view group_working_dir = {}) const;
};

struct CommandGroup {
  std::string id;
  std::string server_id;
  std::string name;
  std::string working_dir;

  static CommandGroup make_new(const std::string& server_id, const std::string& name);
};

struct ExtraProgram {
  std::string id;
  std::string name;
  std::string path;
  std::string args;

  static ExtraProgram make_new();
};

struct Bundle {
  std::string id;
  std::string name;
  std::string server_id;
  std::vector<std::string> command_ids;
  int interval_sec = 5;

  static Bundle make_new(const std::string& server_id);
};

/// Резервные копии VPS (не путать с локальными копиями config.json).
enum class BackupKind { Files = 0, Database = 1, DockerDb = 2 };
enum class BackupDestKind { SameVps = 0, OtherVps = 1, LocalPc = 2 };
enum class BackupScheduleMode { Client = 0, Cron = 1 };
enum class BackupOverflowAction { Warn = 0, DeleteOldest = 1, DeleteBytes = 2, DeleteStride = 3 };
enum class BackupDbEngine { Postgres = 0, Mysql = 1, Mariadb = 2 };

struct BackupJob {
  std::string id;
  std::string name;
  bool enabled = false;
  std::string source_server_id;
  BackupKind kind = BackupKind::Files;

  std::vector<std::string> paths;
  std::vector<std::string> exclude_globs;

  BackupDbEngine db_engine = BackupDbEngine::Postgres;
  std::string db_name;
  std::string db_user;
  std::string db_password;
  std::string db_password_blob;
  int db_port = 0;
  std::string docker_container;

  BackupDestKind dest_kind = BackupDestKind::SameVps;
  std::string dest_path;
  std::string dest_server_id;

  BackupScheduleMode schedule_mode = BackupScheduleMode::Client;
  int interval_sec = 86400;
  bool cron_installed = false;

  std::int64_t max_total_bytes = 120LL * 1024 * 1024 * 1024;
  int overflow_at_pct = 80;
  std::vector<BackupOverflowAction> overflow_actions{BackupOverflowAction::Warn,
                                                     BackupOverflowAction::DeleteOldest};
  int stride_delete = 3;
  int stride_keep = 1;
  std::int64_t delete_bytes_amount = 10LL * 1024 * 1024 * 1024;
  std::int64_t max_single_archive_bytes = 0;
  std::int64_t min_dest_free_bytes = 1LL * 1024 * 1024 * 1024;

  double last_run_unix = 0.0;
  std::string last_status;
  std::string last_error;
  std::string last_archive_name;
  std::int64_t last_archive_bytes = 0;

  static BackupJob make_new(const std::string& source_server_id);
  BackupJob duplicate(const std::string& new_name = "") const;
};

struct AppSettings {
  bool confirm_before_run = true;
  /// История разовой команды: отдельно на каждую вкладку групп (иначе только на VPS).
  bool quick_history_per_group = false;
  /// Ключ: id VPS или `idVPS\\x1fidГруппы`; значения — от старых к новым.
  std::map<std::string, std::vector<std::string>> quick_history;
  bool check_updates_on_start = true;
  double last_update_check = 0.0;
  std::string skipped_update_version;
  std::string last_seen_version;
  std::string window_geometry;
  std::string window_state = "normal";
  int sash_pos = 0;
  int vsash_pos = 0;
  std::string last_server_id;
  std::string last_command_id;
  std::map<std::string, std::string> dialog_geometry;
  std::map<std::string, std::map<std::string, int>> column_widths;
  std::map<std::string, std::vector<std::string>> column_order;
  std::string putty_path;
  std::string winscp_path;
  std::string ssh_path;
  std::vector<ExtraProgram> extra_programs;
  int default_command_timeout = 180;
  int journal_max_entries = 5000;
  bool clear_output_before_run = false;
  bool advance_command_after_run = true;
  // Если true — после подключения Shell сессионный переключатель «вывод команд в Shell» включается сам.
  bool automation_to_shell_when_connected = true;
  // После F5/связки активный Shell переходит в тот же каталог, что и команда.
  bool shell_follow_command_cwd = true;
  bool allow_short_master_password = false;
  int master_password_max_attempts = 5;
  int master_password_lockout_minutes = 20;
  std::string theme = "dark";
  int terminal_font_pt = 12;
  bool show_command_folder_column = true;
  bool backup_enabled = true;
  double last_backup = 0.0;
  std::map<std::string, std::string> last_group_by_server;
  /// Режим рабочей области: commands | health | backup | files.
  std::string workspace_mode = "commands";
  /// В окне резервных копий показывать задания всех VPS, а не только выбранного.
  bool backup_show_all_servers = false;
  bool health_auto = false;
  int health_interval_sec = 86400;
  int health_timeout_sec = 20;
  int health_disk_warn = 80;
  int health_disk_crit = 90;
  int health_ram_warn = 80;
  int health_ram_crit = 90;
  int health_swap_warn = 50;
  int health_swap_crit = 80;
  int health_cpu_warn = 80;
  int health_cpu_crit = 95;
  int health_iowait_warn = 25;
  int health_iowait_crit = 50;
  int health_steal_warn = 15;
  int health_steal_crit = 30;
  int health_load_warn = 100;
  int health_load_crit = 150;
  int health_ssh_warn = 20;
  int health_ssh_crit = 100;
  bool health_show_cpu = true;
  bool health_show_ram = true;
  bool health_show_swap = true;
  bool health_show_disk = true;
  bool health_show_load = true;
  bool health_show_docker = false;
  bool health_show_docker_disks = false;
};

struct Config {
  std::vector<Server> servers;
  std::vector<Command> commands;
  std::vector<CommandGroup> groups;
  std::vector<Bundle> bundles;
  std::vector<BackupJob> backup_jobs;
  AppSettings settings;
  VaultMeta vault;
  bool has_vault = false;
  bool needs_migration = false;

  Server* server_by_id(const std::string& id);
  const Server* server_by_id(const std::string& id) const;
  Command* command_by_id(const std::string& id);
  const Command* command_by_id(const std::string& id) const;
  std::vector<Command> commands_for(const std::string& server_id) const;
  std::vector<Command> commands_for(const std::string& server_id, const std::string& group_id) const;
  std::vector<CommandGroup> groups_for(const std::string& server_id) const;
  CommandGroup* group_by_id(const std::string& id);
  const CommandGroup* group_by_id(const std::string& id) const;
  void set_commands_for(const std::string& server_id, const std::vector<Command>& ordered);
  void set_commands_for(const std::string& server_id, const std::string& group_id, const std::vector<Command>& ordered);
  bool move_command(const std::string& command_id, int delta);
  void sort_commands_for(const std::string& server_id, const std::string& by, bool ascending = true);
  void sort_commands_for(const std::string& server_id, const std::string& group_id, const std::string& by,
                         bool ascending = true);
  void remove_group(const std::string& group_id);
  std::vector<Bundle> bundles_for(const std::string& server_id) const;
  Bundle* bundle_by_id(const std::string& id);
  const Bundle* bundle_by_id(const std::string& id) const;
  void drop_command_from_bundles(const std::string& command_id);
  void drop_server_bundles(const std::string& server_id);
  BackupJob* backup_job_by_id(const std::string& id);
  const BackupJob* backup_job_by_id(const std::string& id) const;
  std::vector<BackupJob> backup_jobs_for(const std::string& server_id) const;
  void drop_server_backup_jobs(const std::string& server_id);
};

std::string normalize_remote_shell(std::string_view shell);
std::string effective_remote_shell(const Server& server, const Command& command);
std::string command_run_working_dir(const Config& config, const Command& command);
std::string command_display_folder(const Config& config, const Command& command);

Config load_config();
Config load_config_from(const std::filesystem::path& path);
void unlock_secrets(Config& config, const SessionVault& vault);
void save_config(Config& config, SessionVault& vault);
void save_config_to(Config& config, SessionVault& vault, const std::filesystem::path& path);

}  // namespace fatty

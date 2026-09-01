#pragma once

#include "core/vault.hpp"

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

  static Server make_new();
  Server duplicate(const std::string& new_name) const;
};

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

struct AppSettings {
  bool confirm_before_run = true;
  bool check_updates_on_start = true;
  double last_update_check = 0.0;
  std::string skipped_update_version;
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
  bool allow_short_master_password = false;
  int master_password_max_attempts = 5;
  int master_password_lockout_minutes = 20;
  std::string theme = "dark";
  bool show_command_folder_column = true;
  bool backup_enabled = true;
  double last_backup = 0.0;
  std::map<std::string, std::string> last_group_by_server;
};

struct Config {
  std::vector<Server> servers;
  std::vector<Command> commands;
  std::vector<CommandGroup> groups;
  std::vector<Bundle> bundles;
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
  void sort_commands_for(const std::string& server_id, const std::string& by);
  void sort_commands_for(const std::string& server_id, const std::string& group_id, const std::string& by);
  void remove_group(const std::string& group_id);
  std::vector<Bundle> bundles_for(const std::string& server_id) const;
  Bundle* bundle_by_id(const std::string& id);
  const Bundle* bundle_by_id(const std::string& id) const;
  void drop_command_from_bundles(const std::string& command_id);
  void drop_server_bundles(const std::string& server_id);
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

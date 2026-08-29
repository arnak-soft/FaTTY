#pragma once

#include "core/vault.hpp"

#include <map>
#include <string>
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

  static Server make_new();
  Server duplicate(const std::string& new_name) const;
};

struct Command {
  std::string id;
  std::string name;
  std::string comment;
  std::string server_id;
  std::string folder_id;
  std::string command;
  int timeout_sec = 180;
  bool login_shell = true;
  bool confirm_before_run = true;

  static Command make_new(const std::string& server_id);
  Command duplicate(const std::string& new_name = "", const std::string& new_server_id = "") const;
};

struct Folder {
  std::string id;
  std::string server_id;
  std::string name;

  static Folder make_new(const std::string& server_id, const std::string& name);
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
  std::string ssh_path;
  int default_command_timeout = 180;
  int journal_max_entries = 5000;
  bool clear_output_before_run = false;
  bool allow_short_master_password = false;
  int master_password_max_attempts = 5;
  int master_password_lockout_minutes = 20;
  std::string theme = "dark";
  bool show_command_folder_column = true;
  bool backup_enabled = true;
  double last_backup = 0.0;
  std::map<std::string, std::string> last_folder_by_server;
};

struct Config {
  std::vector<Server> servers;
  std::vector<Command> commands;
  std::vector<Folder> folders;
  AppSettings settings;
  VaultMeta vault;
  bool has_vault = false;
  bool needs_migration = false;

  Server* server_by_id(const std::string& id);
  const Server* server_by_id(const std::string& id) const;
  Command* command_by_id(const std::string& id);
  const Command* command_by_id(const std::string& id) const;
  std::vector<Command> commands_for(const std::string& server_id) const;
  std::vector<Command> commands_for(const std::string& server_id, const std::string& folder_id) const;
  std::vector<Folder> folders_for(const std::string& server_id) const;
  Folder* folder_by_id(const std::string& id);
  const Folder* folder_by_id(const std::string& id) const;
  void set_commands_for(const std::string& server_id, const std::vector<Command>& ordered);
  void set_commands_for(const std::string& server_id, const std::string& folder_id, const std::vector<Command>& ordered);
  bool move_command(const std::string& command_id, int delta);
  void sort_commands_for(const std::string& server_id, const std::string& by);
  void sort_commands_for(const std::string& server_id, const std::string& folder_id, const std::string& by);
  void remove_folder(const std::string& folder_id);
};

Config load_config();
void unlock_secrets(Config& config, const SessionVault& vault);
void save_config(Config& config, SessionVault& vault);

}  // namespace fatty

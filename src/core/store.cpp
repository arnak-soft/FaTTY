#include "core/store.hpp"

#include "core/dpapi.hpp"
#include "core/paths.hpp"
#include "core/util.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace fatty {
using json = nlohmann::json;

Server Server::make_new() {
  Server s;
  s.id = new_uuid();
  return s;
}

Server Server::duplicate(const std::string& new_name) const {
  Server s = *this;
  s.id = new_uuid();
  s.name = new_name;
  s.password_blob.clear();
  return s;
}

Command Command::make_new(const std::string& server_id) {
  Command c;
  c.id = new_uuid();
  c.server_id = server_id;
  return c;
}

Folder Folder::make_new(const std::string& server_id, const std::string& name) {
  Folder f;
  f.id = new_uuid();
  f.server_id = server_id;
  f.name = name;
  return f;
}

ExtraProgram ExtraProgram::make_new() {
  ExtraProgram p;
  p.id = new_uuid();
  return p;
}

Bundle Bundle::make_new(const std::string& server_id) {
  Bundle b;
  b.id = new_uuid();
  b.server_id = server_id;
  return b;
}

Command Command::duplicate(const std::string& new_name, const std::string& new_server_id) const {
  Command c = *this;
  c.id = new_uuid();
  if (!new_name.empty()) {
    c.name = new_name;
  }
  if (!new_server_id.empty()) {
    c.server_id = new_server_id;
  }
  return c;
}

std::string Command::effective_cwd(const std::string& session_cwd) const {
  if (cd_before_run) {
    auto dir = trim(working_dir);
    if (!dir.empty()) return dir;
  }
  return session_cwd;
}

Server* Config::server_by_id(const std::string& id) {
  for (auto& s : servers) {
    if (s.id == id) {
      return &s;
    }
  }
  return nullptr;
}

const Server* Config::server_by_id(const std::string& id) const {
  for (const auto& s : servers) {
    if (s.id == id) {
      return &s;
    }
  }
  return nullptr;
}

Command* Config::command_by_id(const std::string& id) {
  for (auto& c : commands) {
    if (c.id == id) {
      return &c;
    }
  }
  return nullptr;
}

const Command* Config::command_by_id(const std::string& id) const {
  for (const auto& c : commands) {
    if (c.id == id) {
      return &c;
    }
  }
  return nullptr;
}

std::vector<Command> Config::commands_for(const std::string& server_id) const {
  std::vector<Command> out;
  for (const auto& c : commands) {
    if (c.server_id == server_id) {
      out.push_back(c);
    }
  }
  return out;
}

std::vector<Command> Config::commands_for(const std::string& server_id, const std::string& folder_id) const {
  std::vector<Command> out;
  for (const auto& c : commands) {
    if (c.server_id == server_id && c.folder_id == folder_id) {
      out.push_back(c);
    }
  }
  return out;
}

std::vector<Folder> Config::folders_for(const std::string& server_id) const {
  std::vector<Folder> out;
  for (const auto& f : folders) {
    if (f.server_id == server_id) {
      out.push_back(f);
    }
  }
  return out;
}

Folder* Config::folder_by_id(const std::string& id) {
  for (auto& f : folders) {
    if (f.id == id) {
      return &f;
    }
  }
  return nullptr;
}

const Folder* Config::folder_by_id(const std::string& id) const {
  for (const auto& f : folders) {
    if (f.id == id) {
      return &f;
    }
  }
  return nullptr;
}

void Config::set_commands_for(const std::string& server_id, const std::vector<Command>& ordered) {
  std::vector<Command> remaining = ordered;
  std::vector<Command> rebuilt;
  for (const auto& cmd : commands) {
    if (cmd.server_id != server_id) {
      rebuilt.push_back(cmd);
    } else if (!remaining.empty()) {
      rebuilt.push_back(remaining.front());
      remaining.erase(remaining.begin());
    }
  }
  rebuilt.insert(rebuilt.end(), remaining.begin(), remaining.end());
  commands = std::move(rebuilt);
}

void Config::set_commands_for(const std::string& server_id, const std::string& folder_id,
                             const std::vector<Command>& ordered) {
  std::vector<Command> remaining = ordered;
  std::vector<Command> rebuilt;
  for (const auto& cmd : commands) {
    if (cmd.server_id != server_id || cmd.folder_id != folder_id) {
      rebuilt.push_back(cmd);
    } else if (!remaining.empty()) {
      rebuilt.push_back(remaining.front());
      remaining.erase(remaining.begin());
    }
  }
  rebuilt.insert(rebuilt.end(), remaining.begin(), remaining.end());
  commands = std::move(rebuilt);
}

bool Config::move_command(const std::string& command_id, int delta) {
  auto* cmd = command_by_id(command_id);
  if (!cmd || delta == 0) {
    return false;
  }
  auto group = commands_for(cmd->server_id, cmd->folder_id);
  int idx = -1;
  for (int i = 0; i < static_cast<int>(group.size()); ++i) {
    if (group[static_cast<std::size_t>(i)].id == command_id) {
      idx = i;
      break;
    }
  }
  int new_idx = idx + delta;
  if (idx < 0 || new_idx < 0 || new_idx >= static_cast<int>(group.size())) {
    return false;
  }
  std::swap(group[static_cast<std::size_t>(idx)], group[static_cast<std::size_t>(new_idx)]);
  set_commands_for(cmd->server_id, cmd->folder_id, group);
  return true;
}

void Config::sort_commands_for(const std::string& server_id, const std::string& by) {
  sort_commands_for(server_id, "", by);
}

void Config::sort_commands_for(const std::string& server_id, const std::string& folder_id, const std::string& by) {
  auto group = commands_for(server_id, folder_id);
  auto primary = [&](const Command& c) {
    if (by == "command") return to_lower(trim(c.command));
    if (by == "comment") return to_lower(trim(c.comment));
    if (by == "folder") {
      if (c.cd_before_run) return to_lower(trim(c.working_dir));
      return {};
    }
    return to_lower(trim(c.name));
  };
  std::sort(group.begin(), group.end(), [&](const Command& a, const Command& b) {
    auto ka = primary(a);
    auto kb = primary(b);
    if (ka != kb) {
      return ka < kb;
    }
    auto na = to_lower(trim(a.name));
    auto nb = to_lower(trim(b.name));
    if (na != nb) {
      return na < nb;
    }
    auto ca = to_lower(trim(a.command));
    auto cb = to_lower(trim(b.command));
    if (ca != cb) {
      return ca < cb;
    }
    return a.id < b.id;
  });
  set_commands_for(server_id, folder_id, group);
}

void Config::remove_folder(const std::string& folder_id) {
  if (folder_id.empty()) return;
  for (auto& c : commands) {
    if (c.folder_id == folder_id) {
      c.folder_id.clear();
    }
  }
  folders.erase(std::remove_if(folders.begin(), folders.end(),
                               [&](const Folder& f) { return f.id == folder_id; }),
                folders.end());
}

std::vector<Bundle> Config::bundles_for(const std::string& server_id) const {
  std::vector<Bundle> out;
  for (const auto& b : bundles) {
    if (b.server_id == server_id) out.push_back(b);
  }
  return out;
}

Bundle* Config::bundle_by_id(const std::string& id) {
  for (auto& b : bundles) {
    if (b.id == id) return &b;
  }
  return nullptr;
}

const Bundle* Config::bundle_by_id(const std::string& id) const {
  for (const auto& b : bundles) {
    if (b.id == id) return &b;
  }
  return nullptr;
}

void Config::drop_command_from_bundles(const std::string& command_id) {
  if (command_id.empty()) return;
  for (auto& b : bundles) {
    b.command_ids.erase(std::remove(b.command_ids.begin(), b.command_ids.end(), command_id),
                        b.command_ids.end());
  }
}

void Config::drop_server_bundles(const std::string& server_id) {
  bundles.erase(std::remove_if(bundles.begin(), bundles.end(),
                               [&](const Bundle& b) { return b.server_id == server_id; }),
                bundles.end());
}

namespace {

int json_int(const json& j, const char* key, int def, int lo, int hi) {
  if (!j.contains(key)) {
    return def;
  }
  try {
    return clamp_int(j.at(key).get<int>(), lo, hi);
  } catch (...) {
    return def;
  }
}

VaultMeta parse_vault(const json& raw, bool& ok) {
  VaultMeta meta;
  ok = false;
  if (!raw.is_object()) {
    return meta;
  }
  meta.salt = raw.value("salt", "");
  meta.verifier = raw.value("verifier", "");
  if (meta.salt.empty() || meta.verifier.empty()) {
    return meta;
  }
  try {
    meta.iterations = raw.value("iterations", kKdfIterations);
  } catch (...) {
    meta.iterations = kKdfIterations;
  }
  meta.iterations = std::max(100000, meta.iterations);
  meta.kdf = raw.value("kdf", std::string(kKdfName));
  ok = true;
  return meta;
}

std::vector<ExtraProgram> parse_extra_programs(const json& raw) {
  std::vector<ExtraProgram> out;
  if (!raw.is_array()) return out;
  for (const auto& item : raw) {
    if (!item.is_object()) continue;
    ExtraProgram p;
    p.id = item.value("id", "");
    p.name = trim(item.value("name", ""));
    p.path = trim(item.value("path", ""));
    p.args = item.value("args", "");
    if (p.name.empty() || p.path.empty()) continue;
    if (p.id.empty()) p.id = new_uuid();
    out.push_back(std::move(p));
  }
  return out;
}

json extra_programs_json(const std::vector<ExtraProgram>& programs) {
  json arr = json::array();
  for (const auto& p : programs) {
    arr.push_back({
        {"id", p.id},
        {"name", p.name},
        {"path", p.path},
        {"args", p.args},
    });
  }
  return arr;
}

std::map<std::string, std::map<std::string, int>> parse_column_widths(const json& raw) {
  std::map<std::string, std::map<std::string, int>> result;
  if (!raw.is_object()) {
    return result;
  }
  for (auto it = raw.begin(); it != raw.end(); ++it) {
    if (!it.value().is_object()) {
      continue;
    }
    std::map<std::string, int> parsed;
    for (auto col = it.value().begin(); col != it.value().end(); ++col) {
      try {
        int width = col.value().get<int>();
        if (width > 0) {
          parsed[col.key()] = width;
        }
      } catch (...) {
      }
    }
    if (!parsed.empty()) {
      result[it.key()] = std::move(parsed);
    }
  }
  return result;
}

std::map<std::string, std::vector<std::string>> parse_column_order(const json& raw) {
  std::map<std::string, std::vector<std::string>> result;
  if (!raw.is_object()) {
    return result;
  }
  for (auto it = raw.begin(); it != raw.end(); ++it) {
    if (!it.value().is_array()) continue;
    std::vector<std::string> ids;
    for (const auto& item : it.value()) {
      if (!item.is_string()) continue;
      auto id = trim(item.get<std::string>());
      if (!id.empty()) ids.push_back(std::move(id));
    }
    if (!ids.empty()) result[it.key()] = std::move(ids);
  }
  return result;
}

}  // namespace

Config load_config() {
  Config cfg;
  if (!std::filesystem::exists(config_path())) {
    cfg.needs_migration = true;
    return cfg;
  }
  json data = json::parse(read_text_file(config_path()), nullptr, true, true);
  bool vault_ok = false;
  cfg.vault = parse_vault(data.value("vault", json::object()), vault_ok);
  cfg.has_vault = vault_ok;
  cfg.needs_migration = !vault_ok;
  for (const auto& raw : data.value("servers", json::array())) {
    Server s;
    s.id = raw.value("id", new_uuid());
    s.name = raw.value("name", "");
    s.host = raw.value("host", "");
    s.port = raw.value("port", 22);
    s.username = raw.value("username", "root");
    s.password_blob = raw.value("password_vault", "");
    s.key_path = raw.value("key_path", "");
    const std::string legacy = raw.value("password_encrypted", "");
    if (!vault_ok && !legacy.empty()) {
      try {
        s.password = dpapi_unprotect(legacy);
      } catch (...) {
        s.password.clear();
      }
      cfg.needs_migration = true;
    }
    if (!legacy.empty()) {
      cfg.needs_migration = true;
    }
    cfg.servers.push_back(std::move(s));
  }
  for (const auto& raw : data.value("commands", json::array())) {
    Command c;
    c.id = raw.value("id", new_uuid());
    c.name = raw.value("name", "");
    c.comment = raw.value("comment", "");
    c.server_id = raw.value("server_id", "");
    c.command = raw.value("command", "");
    c.folder_id = raw.value("folder_id", "");
    c.working_dir = trim(raw.value("working_dir", ""));
    c.timeout_sec = raw.value("timeout_sec", 180);
    c.login_shell = raw.value("login_shell", true);
    c.confirm_before_run = raw.value("confirm_before_run", true);
    c.cd_before_run = raw.value("cd_before_run", true);
    cfg.commands.push_back(std::move(c));
  }
  for (const auto& raw : data.value("folders", json::array())) {
    Folder f;
    f.id = raw.value("id", new_uuid());
    f.server_id = raw.value("server_id", "");
    f.name = trim(raw.value("name", ""));
    if (f.name.empty() || f.server_id.empty()) continue;
    cfg.folders.push_back(std::move(f));
  }
  for (const auto& raw : data.value("bundles", json::array())) {
    if (!raw.is_object()) continue;
    Bundle b;
    b.id = raw.value("id", new_uuid());
    if (b.id.empty()) b.id = new_uuid();
    b.name = trim(raw.value("name", ""));
    b.server_id = raw.value("server_id", "");
    b.interval_sec = json_int(raw, "interval_sec", 5, 0, 3600);
    if (raw.contains("command_ids") && raw["command_ids"].is_array()) {
      for (const auto& id : raw["command_ids"]) {
        if (!id.is_string()) continue;
        auto cid = trim(id.get<std::string>());
        if (!cid.empty()) b.command_ids.push_back(std::move(cid));
      }
    }
    if (b.server_id.empty()) continue;
    cfg.bundles.push_back(std::move(b));
  }
  json settings_raw = data.value("settings", json::object());
  auto& st = cfg.settings;
  st.confirm_before_run = settings_raw.value("confirm_before_run", true);
  st.check_updates_on_start = settings_raw.value("check_updates_on_start", true);
  try {
    st.last_update_check = std::max(0.0, settings_raw.value("last_update_check", 0.0));
  } catch (...) {
    st.last_update_check = 0.0;
  }
  st.skipped_update_version = settings_raw.value("skipped_update_version", "");
  st.window_geometry = settings_raw.value("window_geometry", "");
  st.window_state = settings_raw.value("window_state", "normal");
  if (st.window_state != "normal" && st.window_state != "zoomed") {
    st.window_state = "normal";
  }
  st.sash_pos = std::max(0, settings_raw.value("sash_pos", 0));
  st.vsash_pos = std::max(0, settings_raw.value("vsash_pos", 0));
  st.last_server_id = settings_raw.value("last_server_id", "");
  st.last_command_id = settings_raw.value("last_command_id", "");
  if (settings_raw.contains("dialog_geometry") && settings_raw["dialog_geometry"].is_object()) {
    for (auto it = settings_raw["dialog_geometry"].begin(); it != settings_raw["dialog_geometry"].end(); ++it) {
      if (it.value().is_string()) {
        std::string v = it.value().get<std::string>();
        if (v.find('x') != std::string::npos) {
          st.dialog_geometry[it.key()] = trim(v);
        }
      }
    }
  }
  st.column_widths = parse_column_widths(settings_raw.value("column_widths", json::object()));
  st.column_order = parse_column_order(settings_raw.value("column_order", json::object()));
  st.putty_path = settings_raw.value("putty_path", "");
  st.winscp_path = settings_raw.value("winscp_path", "");
  st.ssh_path = settings_raw.value("ssh_path", "");
  st.extra_programs = parse_extra_programs(settings_raw.value("extra_programs", json::array()));
  st.default_command_timeout = json_int(settings_raw, "default_command_timeout", 180, 1, 86400);
  st.journal_max_entries = json_int(settings_raw, "journal_max_entries", 5000, 100, 50000);
  st.clear_output_before_run = settings_raw.value("clear_output_before_run", false);
  st.allow_short_master_password = settings_raw.value("allow_short_master_password", false);
  st.master_password_max_attempts = json_int(settings_raw, "master_password_max_attempts", 5, 0, 100);
  st.master_password_lockout_minutes = json_int(settings_raw, "master_password_lockout_minutes", 20, 1, 24 * 60);
  st.theme = settings_raw.value("theme", std::string("dark"));
  if (st.theme != "light" && st.theme != "dark") {
    st.theme = "dark";
  }
  st.show_command_folder_column = settings_raw.value("show_command_folder_column", true);
  st.backup_enabled = settings_raw.value("backup_enabled", true);
  try {
    st.last_backup = std::max(0.0, settings_raw.value("last_backup", 0.0));
  } catch (...) {
    st.last_backup = 0.0;
  }
  if (settings_raw.contains("last_folder_by_server") && settings_raw["last_folder_by_server"].is_object()) {
    for (auto it = settings_raw["last_folder_by_server"].begin();
         it != settings_raw["last_folder_by_server"].end(); ++it) {
      if (it.value().is_string()) {
        st.last_folder_by_server[it.key()] = it.value().get<std::string>();
      }
    }
  }
  return cfg;
}

void unlock_secrets(Config& config, const SessionVault& vault) {
  if (!vault.unlocked()) {
    throw VaultLocked("Хранилище заблокировано");
  }
  for (auto& server : config.servers) {
    if (!server.password_blob.empty()) {
      server.password = vault.decrypt_secret(server.password_blob);
    }
  }
}

void save_config(Config& config, SessionVault& vault) {
  if (!vault.unlocked() || vault.meta() == nullptr) {
    throw VaultLocked("Нельзя сохранить конфиг без мастер-пароля");
  }
  json payload;
  payload["vault"] = {
      {"kdf", vault.meta()->kdf},
      {"iterations", vault.meta()->iterations},
      {"salt", vault.meta()->salt},
      {"verifier", vault.meta()->verifier},
  };
  payload["servers"] = json::array();
  for (auto& s : config.servers) {
    json item = {
        {"id", s.id},
        {"name", s.name},
        {"host", s.host},
        {"port", s.port},
        {"username", s.username},
        {"password_vault", s.password.empty() ? "" : vault.encrypt_secret(s.password)},
        {"key_path", s.key_path},
    };
    payload["servers"].push_back(item);
    s.password_blob = item["password_vault"].get<std::string>();
  }
  payload["commands"] = json::array();
  for (const auto& c : config.commands) {
    payload["commands"].push_back({
        {"id", c.id},
        {"name", c.name},
        {"comment", c.comment},
        {"server_id", c.server_id},
        {"command", c.command},
        {"folder_id", c.folder_id},
        {"working_dir", c.working_dir},
        {"timeout_sec", c.timeout_sec},
        {"login_shell", c.login_shell},
        {"confirm_before_run", c.confirm_before_run},
        {"cd_before_run", c.cd_before_run},
    });
  }
  payload["folders"] = json::array();
  for (const auto& f : config.folders) {
    payload["folders"].push_back({
        {"id", f.id},
        {"server_id", f.server_id},
        {"name", f.name},
    });
  }
  payload["bundles"] = json::array();
  for (const auto& b : config.bundles) {
    payload["bundles"].push_back({
        {"id", b.id},
        {"name", b.name},
        {"server_id", b.server_id},
        {"command_ids", b.command_ids},
        {"interval_sec", b.interval_sec},
    });
  }
  json settings = {
      {"confirm_before_run", config.settings.confirm_before_run},
      {"check_updates_on_start", config.settings.check_updates_on_start},
      {"last_update_check", config.settings.last_update_check},
      {"skipped_update_version", config.settings.skipped_update_version},
      {"window_geometry", config.settings.window_geometry},
      {"window_state", config.settings.window_state},
      {"sash_pos", config.settings.sash_pos},
      {"vsash_pos", config.settings.vsash_pos},
      {"last_server_id", config.settings.last_server_id},
      {"last_command_id", config.settings.last_command_id},
      {"dialog_geometry", config.settings.dialog_geometry},
      {"column_widths", config.settings.column_widths},
      {"column_order", config.settings.column_order},
      {"putty_path", config.settings.putty_path},
      {"winscp_path", config.settings.winscp_path},
      {"ssh_path", config.settings.ssh_path},
      {"extra_programs", extra_programs_json(config.settings.extra_programs)},
      {"default_command_timeout", config.settings.default_command_timeout},
      {"journal_max_entries", config.settings.journal_max_entries},
      {"clear_output_before_run", config.settings.clear_output_before_run},
      {"allow_short_master_password", config.settings.allow_short_master_password},
      {"master_password_max_attempts", config.settings.master_password_max_attempts},
      {"master_password_lockout_minutes", config.settings.master_password_lockout_minutes},
      {"theme", config.settings.theme},
      {"show_command_folder_column", config.settings.show_command_folder_column},
      {"backup_enabled", config.settings.backup_enabled},
      {"last_backup", config.settings.last_backup},
      {"last_folder_by_server", config.settings.last_folder_by_server},
  };
  payload["settings"] = settings;
  atomic_write_text(config_path(), payload.dump(2));
  config.vault = *vault.meta();
  config.has_vault = true;
  config.needs_migration = false;
}

}  // namespace fatty

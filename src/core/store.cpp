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

bool Config::move_command(const std::string& command_id, int delta) {
  auto* cmd = command_by_id(command_id);
  if (!cmd || delta == 0) {
    return false;
  }
  auto group = commands_for(cmd->server_id);
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
  set_commands_for(cmd->server_id, group);
  return true;
}

void Config::sort_commands_for(const std::string& server_id, const std::string& by) {
  auto group = commands_for(server_id);
  if (by == "command") {
    std::sort(group.begin(), group.end(), [](const Command& a, const Command& b) {
      auto ca = to_lower(trim(a.command));
      auto cb = to_lower(trim(b.command));
      if (ca != cb) {
        return ca < cb;
      }
      auto na = to_lower(trim(a.name));
      auto nb = to_lower(trim(b.name));
      if (na != nb) {
        return na < nb;
      }
      return a.id < b.id;
    });
  } else {
    std::sort(group.begin(), group.end(), [](const Command& a, const Command& b) {
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
  }
  set_commands_for(server_id, group);
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
    c.server_id = raw.value("server_id", "");
    c.command = raw.value("command", "");
    c.timeout_sec = raw.value("timeout_sec", 180);
    c.login_shell = raw.value("login_shell", true);
    cfg.commands.push_back(std::move(c));
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
  st.putty_path = settings_raw.value("putty_path", "");
  st.ssh_path = settings_raw.value("ssh_path", "");
  st.default_command_timeout = json_int(settings_raw, "default_command_timeout", 180, 1, 86400);
  st.journal_max_entries = json_int(settings_raw, "journal_max_entries", 5000, 100, 50000);
  st.clear_output_before_run = settings_raw.value("clear_output_before_run", false);
  st.allow_short_master_password = settings_raw.value("allow_short_master_password", false);
  st.master_password_max_attempts = json_int(settings_raw, "master_password_max_attempts", 5, 0, 100);
  st.master_password_lockout_minutes = json_int(settings_raw, "master_password_lockout_minutes", 20, 1, 24 * 60);
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
        {"server_id", c.server_id},
        {"command", c.command},
        {"timeout_sec", c.timeout_sec},
        {"login_shell", c.login_shell},
    });
  }
  json settings = {
      {"confirm_before_run", config.settings.confirm_before_run},
      {"check_updates_on_start", config.settings.check_updates_on_start},
      {"last_update_check", config.settings.last_update_check},
      {"window_geometry", config.settings.window_geometry},
      {"window_state", config.settings.window_state},
      {"sash_pos", config.settings.sash_pos},
      {"vsash_pos", config.settings.vsash_pos},
      {"last_server_id", config.settings.last_server_id},
      {"last_command_id", config.settings.last_command_id},
      {"dialog_geometry", config.settings.dialog_geometry},
      {"column_widths", config.settings.column_widths},
      {"putty_path", config.settings.putty_path},
      {"ssh_path", config.settings.ssh_path},
      {"default_command_timeout", config.settings.default_command_timeout},
      {"journal_max_entries", config.settings.journal_max_entries},
      {"clear_output_before_run", config.settings.clear_output_before_run},
      {"allow_short_master_password", config.settings.allow_short_master_password},
      {"master_password_max_attempts", config.settings.master_password_max_attempts},
      {"master_password_lockout_minutes", config.settings.master_password_lockout_minutes},
  };
  payload["settings"] = settings;
  atomic_write_text(config_path(), payload.dump(2));
  config.vault = *vault.meta();
  config.has_vault = true;
  config.needs_migration = false;
}

}  // namespace fatty

#include "core/config_io.hpp"

#include "app/version.hpp"
#include "core/journal.hpp"
#include "core/util.hpp"

#include <map>
#include <set>
#include <sstream>
#include <nlohmann/json.hpp>

namespace fatty {
using json = nlohmann::json;

namespace {

json portable_settings(const AppSettings& settings) {
  return {
      {"confirm_before_run", settings.confirm_before_run},
      {"check_updates_on_start", settings.check_updates_on_start},
      {"putty_path", settings.putty_path},
      {"ssh_path", settings.ssh_path},
      {"default_command_timeout", settings.default_command_timeout},
      {"journal_max_entries", settings.journal_max_entries},
      {"clear_output_before_run", settings.clear_output_before_run},
      {"allow_short_master_password", settings.allow_short_master_password},
      {"master_password_max_attempts", settings.master_password_max_attempts},
      {"master_password_lockout_minutes", settings.master_password_lockout_minutes},
  };
}

void apply_portable_settings(AppSettings& settings, const json& raw) {
  if (!raw.is_object()) return;
  settings.confirm_before_run = raw.value("confirm_before_run", settings.confirm_before_run);
  settings.check_updates_on_start = raw.value("check_updates_on_start", settings.check_updates_on_start);
  settings.putty_path = raw.value("putty_path", settings.putty_path);
  settings.ssh_path = raw.value("ssh_path", settings.ssh_path);
  settings.clear_output_before_run = raw.value("clear_output_before_run", settings.clear_output_before_run);
  settings.allow_short_master_password =
      raw.value("allow_short_master_password", settings.allow_short_master_password);
  settings.master_password_max_attempts =
      clamp_int(raw.value("master_password_max_attempts", settings.master_password_max_attempts), 0, 100);
  settings.master_password_lockout_minutes =
      clamp_int(raw.value("master_password_lockout_minutes", settings.master_password_lockout_minutes), 1, 24 * 60);
  int timeout = raw.value("default_command_timeout", settings.default_command_timeout);
  if (timeout >= 1 && timeout <= 86400) settings.default_command_timeout = timeout;
  int journal = raw.value("journal_max_entries", settings.journal_max_entries);
  if (journal >= 100 && journal <= 50000) settings.journal_max_entries = journal;
}

std::pair<std::string, std::string> server_key(const std::string& name, const std::string& host) {
  return {to_lower(trim(name)), to_lower(trim(host))};
}

}  // namespace

json build_export_payload(const Config& config, bool include_secrets, bool include_settings) {
  json servers = json::array();
  for (const auto& server : config.servers) {
    json item = {
        {"name", server.name},
        {"host", server.host},
        {"port", server.port},
        {"username", server.username},
        {"key_path", server.key_path},
    };
    if (include_secrets) {
      item["password"] = server.password;
    }
    servers.push_back(item);
  }
  json commands = json::array();
  for (const auto& cmd : config.commands) {
    auto* server = config.server_by_id(cmd.server_id);
    if (!server) continue;
    commands.push_back({
        {"server_name", server->name},
        {"server_host", server->host},
        {"name", cmd.name},
        {"command", cmd.command},
        {"timeout_sec", cmd.timeout_sec},
        {"login_shell", cmd.login_shell},
    });
  }
  json payload = {
      {"fatty_export", 1},
      {"app", kAppName},
      {"version", resolve_version()},
      {"exported_at", now_iso()},
      {"include_secrets", include_secrets},
      {"servers", servers},
      {"commands", commands},
  };
  if (include_settings) {
    payload["settings"] = portable_settings(config.settings);
  }
  return payload;
}

void write_export(const std::filesystem::path& path, const Config& config, bool include_secrets,
                  bool include_settings) {
  atomic_write_text(path, build_export_payload(config, include_secrets, include_settings).dump(2));
}

json read_export(const std::filesystem::path& path) {
  json data;
  try {
    data = json::parse(read_text_file(path), nullptr, true, true);
  } catch (const std::exception& exc) {
    throw ConfigIOError(std::string("Не удалось прочитать файл: ") + exc.what());
  }
  if (!data.is_object()) {
    throw ConfigIOError("Неверный формат файла.");
  }
  if (data.value("fatty_export", 0) != 1) {
    throw ConfigIOError("Неподдерживаемая версия файла экспорта FaTTY.");
  }
  auto app = data.value("app", std::string(kAppName));
  if (app != kAppName) {
    throw ConfigIOError("Файл создан другим приложением.");
  }
  return data;
}

ImportResult import_into_config(Config& config, const json& data, const std::string& mode, bool import_settings) {
  if (mode != "merge" && mode != "replace") {
    throw ConfigIOError("Неизвестный режим импорта.");
  }
  ImportResult result;
  json raw_servers = data.value("servers", json::array());
  json raw_commands = data.value("commands", json::array());
  std::vector<Server> imported_servers;
  if (raw_servers.is_array()) {
    for (const auto& raw : raw_servers) {
      if (!raw.is_object()) continue;
      Server s = Server::make_new();
      s.name = trim(raw.value("name", ""));
      s.host = trim(raw.value("host", ""));
      if (s.name.empty() || s.host.empty()) continue;
      s.port = raw.value("port", 22);
      if (s.port < 1 || s.port > 65535) s.port = 22;
      s.username = trim(raw.value("username", "root"));
      if (s.username.empty()) s.username = "root";
      s.password = raw.value("password", "");
      s.key_path = raw.value("key_path", "");
      imported_servers.push_back(std::move(s));
    }
  }
  struct ImpCmd {
    std::string server_name;
    std::string server_host;
    Command cmd;
  };
  std::vector<ImpCmd> imported_commands;
  if (raw_commands.is_array()) {
    for (const auto& raw : raw_commands) {
      if (!raw.is_object()) continue;
      ImpCmd item;
      item.server_name = trim(raw.value("server_name", ""));
      item.server_host = trim(raw.value("server_host", ""));
      item.cmd = Command::make_new("");
      item.cmd.name = trim(raw.value("name", ""));
      item.cmd.command = trim(raw.value("command", ""));
      item.cmd.timeout_sec = raw.value("timeout_sec", 180);
      if (item.cmd.timeout_sec < 1) item.cmd.timeout_sec = 180;
      item.cmd.login_shell = raw.value("login_shell", true);
      if (item.server_name.empty() || item.server_host.empty() || item.cmd.name.empty() ||
          item.cmd.command.empty()) {
        continue;
      }
      imported_commands.push_back(std::move(item));
    }
  }

  if (mode == "replace") {
    result.servers_replaced = static_cast<int>(config.servers.size());
    config.servers = imported_servers;
    config.commands.clear();
    std::map<std::pair<std::string, std::string>, std::string> id_by_key;
    for (const auto& s : imported_servers) {
      id_by_key[server_key(s.name, s.host)] = s.id;
    }
    for (auto& item : imported_commands) {
      auto it = id_by_key.find(server_key(item.server_name, item.server_host));
      if (it == id_by_key.end()) {
        result.commands_skipped++;
        continue;
      }
      item.cmd.server_id = it->second;
      config.commands.push_back(item.cmd);
      result.commands_added++;
    }
  } else {
    std::map<std::pair<std::string, std::string>, std::string> id_by_key;
    for (const auto& s : config.servers) {
      id_by_key[server_key(s.name, s.host)] = s.id;
    }
    for (auto& server : imported_servers) {
      auto key = server_key(server.name, server.host);
      if (id_by_key.count(key)) {
        result.servers_skipped++;
        continue;
      }
      config.servers.push_back(server);
      id_by_key[key] = server.id;
      result.servers_added++;
    }
    std::set<std::pair<std::string, std::string>> existing_cmds;
    for (const auto& cmd : config.commands) {
      existing_cmds.insert({cmd.server_id, to_lower(trim(cmd.name))});
    }
    for (auto& item : imported_commands) {
      auto it = id_by_key.find(server_key(item.server_name, item.server_host));
      if (it == id_by_key.end()) {
        result.commands_skipped++;
        continue;
      }
      auto cmd_key = std::make_pair(it->second, to_lower(trim(item.cmd.name)));
      if (existing_cmds.count(cmd_key)) {
        result.commands_skipped++;
        continue;
      }
      item.cmd.server_id = it->second;
      config.commands.push_back(item.cmd);
      existing_cmds.insert(cmd_key);
      result.commands_added++;
    }
  }
  if (import_settings && data.contains("settings") && data["settings"].is_object()) {
    apply_portable_settings(config.settings, data["settings"]);
    result.settings_applied = true;
  }
  return result;
}

std::string format_import_summary(const ImportResult& result, const std::string& mode) {
  std::ostringstream ss;
  if (mode == "replace") {
    ss << "VPS заменено: " << result.servers_replaced << "\n";
  } else {
    ss << "VPS добавлено: " << result.servers_added << "\n";
    if (result.servers_skipped) {
      ss << "VPS пропущено (уже есть): " << result.servers_skipped << "\n";
    }
  }
  ss << "Команд добавлено: " << result.commands_added;
  if (result.commands_skipped) {
    ss << "\nКоманд пропущено: " << result.commands_skipped;
  }
  if (result.settings_applied) {
    ss << "\nНастройки приложения импортированы.";
  }
  return ss.str();
}

}  // namespace fatty

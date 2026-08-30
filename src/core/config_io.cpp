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

json portable_settings(const AppSettings& settings) {
  return {
      {"confirm_before_run", settings.confirm_before_run},
      {"check_updates_on_start", settings.check_updates_on_start},
      {"putty_path", settings.putty_path},
      {"winscp_path", settings.winscp_path},
      {"ssh_path", settings.ssh_path},
      {"extra_programs", extra_programs_json(settings.extra_programs)},
      {"default_command_timeout", settings.default_command_timeout},
      {"journal_max_entries", settings.journal_max_entries},
      {"clear_output_before_run", settings.clear_output_before_run},
      {"allow_short_master_password", settings.allow_short_master_password},
      {"master_password_max_attempts", settings.master_password_max_attempts},
      {"master_password_lockout_minutes", settings.master_password_lockout_minutes},
      {"theme", settings.theme},
      {"show_command_folder_column", settings.show_command_folder_column},
      {"backup_enabled", settings.backup_enabled},
  };
}

void apply_portable_settings(AppSettings& settings, const json& raw) {
  if (!raw.is_object()) return;
  settings.confirm_before_run = raw.value("confirm_before_run", settings.confirm_before_run);
  settings.check_updates_on_start = raw.value("check_updates_on_start", settings.check_updates_on_start);
  settings.putty_path = raw.value("putty_path", settings.putty_path);
  settings.winscp_path = raw.value("winscp_path", settings.winscp_path);
  settings.ssh_path = raw.value("ssh_path", settings.ssh_path);
  if (raw.contains("extra_programs")) {
    settings.extra_programs = parse_extra_programs(raw["extra_programs"]);
  }
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
  auto theme = raw.value("theme", settings.theme);
  if (theme == "light" || theme == "dark") settings.theme = theme;
  settings.show_command_folder_column =
      raw.value("show_command_folder_column", settings.show_command_folder_column);
  settings.backup_enabled = raw.value("backup_enabled", settings.backup_enabled);
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
    std::string folder_name;
    if (!cmd.folder_id.empty()) {
      if (auto* f = config.folder_by_id(cmd.folder_id)) folder_name = f->name;
    }
    commands.push_back({
        {"server_name", server->name},
        {"server_host", server->host},
        {"name", cmd.name},
        {"comment", cmd.comment},
        {"command", cmd.command},
        {"timeout_sec", cmd.timeout_sec},
        {"login_shell", cmd.login_shell},
        {"confirm_before_run", cmd.confirm_before_run},
        {"cd_before_run", cmd.cd_before_run},
        {"working_dir", cmd.working_dir},
        {"folder", folder_name},
    });
  }
  json folders = json::array();
  for (const auto& folder : config.folders) {
    auto* server = config.server_by_id(folder.server_id);
    if (!server) continue;
    folders.push_back({
        {"server_name", server->name},
        {"server_host", server->host},
        {"name", folder.name},
    });
  }
  json bundles = json::array();
  for (const auto& bundle : config.bundles) {
    auto* server = config.server_by_id(bundle.server_id);
    if (!server) continue;
    json steps = json::array();
    for (const auto& cid : bundle.command_ids) {
      auto* cmd = config.command_by_id(cid);
      if (!cmd) continue;
      std::string folder_name;
      if (!cmd->folder_id.empty()) {
        if (auto* f = config.folder_by_id(cmd->folder_id)) folder_name = f->name;
      }
      steps.push_back({
          {"name", cmd->name},
          {"folder", folder_name},
      });
    }
    if (steps.empty()) continue;
    bundles.push_back({
        {"server_name", server->name},
        {"server_host", server->host},
        {"name", bundle.name},
        {"interval_sec", bundle.interval_sec},
        {"commands", steps},
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
      {"folders", folders},
      {"bundles", bundles},
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
    std::string folder_name;
    Command cmd;
  };
  std::vector<ImpCmd> imported_commands;
  if (raw_commands.is_array()) {
    for (const auto& raw : raw_commands) {
      if (!raw.is_object()) continue;
      ImpCmd item;
      item.server_name = trim(raw.value("server_name", ""));
      item.server_host = trim(raw.value("server_host", ""));
      item.folder_name = trim(raw.value("folder", raw.value("folder_name", "")));
      item.cmd = Command::make_new("");
      item.cmd.name = trim(raw.value("name", ""));
      item.cmd.comment = trim(raw.value("comment", ""));
      item.cmd.command = trim(raw.value("command", ""));
      item.cmd.timeout_sec = raw.value("timeout_sec", 180);
      if (item.cmd.timeout_sec < 1) item.cmd.timeout_sec = 180;
      item.cmd.login_shell = raw.value("login_shell", true);
      item.cmd.confirm_before_run = raw.value("confirm_before_run", true);
      item.cmd.cd_before_run = raw.value("cd_before_run", true);
      item.cmd.working_dir = trim(raw.value("working_dir", ""));
      if (item.server_name.empty() || item.server_host.empty() || item.cmd.name.empty() ||
          item.cmd.command.empty()) {
        continue;
      }
      imported_commands.push_back(std::move(item));
    }
  }
  struct ImpFolder {
    std::string server_name;
    std::string server_host;
    std::string name;
  };
  std::vector<ImpFolder> imported_folders;
  json raw_folders = data.value("folders", json::array());
  if (raw_folders.is_array()) {
    for (const auto& raw : raw_folders) {
      if (!raw.is_object()) continue;
      ImpFolder item;
      item.server_name = trim(raw.value("server_name", ""));
      item.server_host = trim(raw.value("server_host", ""));
      item.name = trim(raw.value("name", ""));
      if (item.server_name.empty() || item.server_host.empty() || item.name.empty()) continue;
      imported_folders.push_back(std::move(item));
    }
  }
  struct ImpBundle {
    std::string server_name;
    std::string server_host;
    Bundle bundle;
    std::vector<std::pair<std::string, std::string>> steps;  // folder, name
  };
  std::vector<ImpBundle> imported_bundles;
  json raw_bundles = data.value("bundles", json::array());
  if (raw_bundles.is_array()) {
    for (const auto& raw : raw_bundles) {
      if (!raw.is_object()) continue;
      ImpBundle item;
      item.server_name = trim(raw.value("server_name", ""));
      item.server_host = trim(raw.value("server_host", ""));
      item.bundle = Bundle::make_new("");
      item.bundle.name = trim(raw.value("name", ""));
      item.bundle.interval_sec = clamp_int(raw.value("interval_sec", 5), 0, 3600);
      json steps = raw.value("commands", json::array());
      if (steps.is_array()) {
        for (const auto& step : steps) {
          if (step.is_string()) {
            auto name = trim(step.get<std::string>());
            if (!name.empty()) item.steps.emplace_back("", name);
            continue;
          }
          if (!step.is_object()) continue;
          auto name = trim(step.value("name", ""));
          if (name.empty()) continue;
          item.steps.emplace_back(trim(step.value("folder", step.value("folder_name", ""))), name);
        }
      }
      if (item.server_name.empty() || item.server_host.empty() || item.bundle.name.empty() ||
          item.steps.empty()) {
        continue;
      }
      imported_bundles.push_back(std::move(item));
    }
  }

  auto ensure_folder = [&](const std::string& server_id, const std::string& name) -> std::string {
    auto want = to_lower(trim(name));
    if (want.empty() || server_id.empty()) return "";
    for (const auto& f : config.folders) {
      if (f.server_id == server_id && to_lower(trim(f.name)) == want) return f.id;
    }
    auto f = Folder::make_new(server_id, trim(name));
    auto id = f.id;
    config.folders.push_back(std::move(f));
    return id;
  };

  auto resolve_bundle_steps = [&](ImpBundle& item, const std::string& server_id) {
    item.bundle.server_id = server_id;
    item.bundle.command_ids.clear();
    for (const auto& [folder_name, cmd_name] : item.steps) {
      auto folder_id = folder_name.empty() ? std::string() : ensure_folder(server_id, folder_name);
      const Command* found = nullptr;
      for (const auto& c : config.commands) {
        if (c.server_id != server_id) continue;
        if (to_lower(trim(c.name)) != to_lower(cmd_name)) continue;
        if (c.folder_id != folder_id) continue;
        found = &c;
        break;
      }
      if (!found) {
        for (const auto& c : config.commands) {
          if (c.server_id == server_id && to_lower(trim(c.name)) == to_lower(cmd_name)) {
            found = &c;
            break;
          }
        }
      }
      if (found) item.bundle.command_ids.push_back(found->id);
    }
  };

  auto import_bundles = [&](const std::map<std::pair<std::string, std::string>, std::string>& id_by_key,
                            bool skip_existing) {
    std::set<std::pair<std::string, std::string>> existing;
    if (skip_existing) {
      for (const auto& b : config.bundles) {
        existing.insert({b.server_id, to_lower(trim(b.name))});
      }
    }
    for (auto& item : imported_bundles) {
      auto it = id_by_key.find(server_key(item.server_name, item.server_host));
      if (it == id_by_key.end()) {
        result.bundles_skipped++;
        continue;
      }
      if (skip_existing && existing.count({it->second, to_lower(item.bundle.name)})) {
        result.bundles_skipped++;
        continue;
      }
      resolve_bundle_steps(item, it->second);
      if (item.bundle.command_ids.empty()) {
        result.bundles_skipped++;
        continue;
      }
      if (skip_existing) existing.insert({it->second, to_lower(item.bundle.name)});
      config.bundles.push_back(std::move(item.bundle));
      result.bundles_added++;
    }
  };

  if (mode == "replace") {
    result.servers_replaced = static_cast<int>(config.servers.size());
    config.servers = imported_servers;
    config.commands.clear();
    config.folders.clear();
    config.bundles.clear();
    std::map<std::pair<std::string, std::string>, std::string> id_by_key;
    for (const auto& s : imported_servers) {
      id_by_key[server_key(s.name, s.host)] = s.id;
    }
    for (const auto& folder : imported_folders) {
      auto it = id_by_key.find(server_key(folder.server_name, folder.server_host));
      if (it != id_by_key.end()) ensure_folder(it->second, folder.name);
    }
    for (auto& item : imported_commands) {
      auto it = id_by_key.find(server_key(item.server_name, item.server_host));
      if (it == id_by_key.end()) {
        result.commands_skipped++;
        continue;
      }
      item.cmd.server_id = it->second;
      item.cmd.folder_id = ensure_folder(it->second, item.folder_name);
      config.commands.push_back(item.cmd);
      result.commands_added++;
    }
    import_bundles(id_by_key, false);
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
    for (const auto& folder : imported_folders) {
      auto it = id_by_key.find(server_key(folder.server_name, folder.server_host));
      if (it != id_by_key.end()) ensure_folder(it->second, folder.name);
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
      item.cmd.folder_id = ensure_folder(it->second, item.folder_name);
      config.commands.push_back(item.cmd);
      existing_cmds.insert(cmd_key);
      result.commands_added++;
    }
    import_bundles(id_by_key, true);
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
  if (result.bundles_added || result.bundles_skipped) {
    ss << "\nСвязок добавлено: " << result.bundles_added;
    if (result.bundles_skipped) {
      ss << "\nСвязок пропущено: " << result.bundles_skipped;
    }
  }
  if (result.settings_applied) {
    ss << "\nНастройки приложения импортированы.";
  }
  return ss.str();
}

}  // namespace fatty

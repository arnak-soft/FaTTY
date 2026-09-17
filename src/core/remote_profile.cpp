#include "core/remote_profile.hpp"

#include "app/version.hpp"
#include "core/journal.hpp"
#include "core/util.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace fatty {
using json = nlohmann::json;

namespace {

std::string group_name_of(const Config& config, const Command& cmd) {
  if (cmd.group_id.empty()) return {};
  if (auto* g = config.group_by_id(cmd.group_id)) return g->name;
  return {};
}

std::string ensure_group(Config& config, const std::string& server_id, const RemoteProfileGroup& group) {
  auto want = to_lower(trim(group.name));
  if (want.empty() || server_id.empty()) return {};
  for (auto& g : config.groups) {
    if (g.server_id == server_id && to_lower(trim(g.name)) == want) {
      if (!trim(group.working_dir).empty() && trim(g.working_dir).empty()) {
        g.working_dir = trim(group.working_dir);
      }
      return g.id;
    }
  }
  auto created = CommandGroup::make_new(server_id, trim(group.name));
  created.working_dir = trim(group.working_dir);
  auto id = created.id;
  config.groups.push_back(std::move(created));
  return id;
}

const Command* find_command(const Config& config, const std::string& server_id, const std::string& group_id,
                            const std::string& name) {
  auto want = to_lower(trim(name));
  if (want.empty()) return nullptr;
  for (const auto& c : config.commands) {
    if (c.server_id != server_id) continue;
    if (to_lower(trim(c.name)) != want) continue;
    if (c.group_id != group_id) continue;
    return &c;
  }
  for (const auto& c : config.commands) {
    if (c.server_id == server_id && to_lower(trim(c.name)) == want) return &c;
  }
  return nullptr;
}

bool command_exists(const Config& config, const std::string& server_id, const std::string& group_id,
                    const std::string& name) {
  auto want = to_lower(trim(name));
  if (want.empty()) return false;
  for (const auto& c : config.commands) {
    if (c.server_id == server_id && c.group_id == group_id && to_lower(trim(c.name)) == want) {
      return true;
    }
  }
  return false;
}

void drop_server_workspace(Config& config, const std::string& server_id, int& commands_removed) {
  commands_removed = 0;
  for (const auto& c : config.commands) {
    if (c.server_id == server_id) ++commands_removed;
  }
  config.drop_server_bundles(server_id);
  config.commands.erase(std::remove_if(config.commands.begin(), config.commands.end(),
                                       [&](const Command& c) { return c.server_id == server_id; }),
                        config.commands.end());
  config.groups.erase(std::remove_if(config.groups.begin(), config.groups.end(),
                                     [&](const CommandGroup& g) { return g.server_id == server_id; }),
                      config.groups.end());
  config.settings.last_group_by_server.erase(server_id);
}

Command to_command(const RemoteProfileCommand& item, const std::string& server_id, const std::string& group_id) {
  auto cmd = Command::make_new(server_id);
  cmd.name = trim(item.name);
  cmd.comment = trim(item.comment);
  cmd.command = trim(item.command);
  cmd.group_id = group_id;
  cmd.working_dir = trim(item.working_dir);
  cmd.timeout_sec = item.timeout_sec < 1 ? 180 : item.timeout_sec;
  cmd.login_shell = item.login_shell;
  cmd.confirm_before_run = item.confirm_before_run;
  cmd.cd_before_run = item.cd_before_run;
  cmd.remote_shell = trim(item.remote_shell);
  return cmd;
}

std::vector<RemoteProfileGroup> ordered_groups(const RemoteProfile& profile) {
  std::vector<RemoteProfileGroup> out;
  std::set<std::string> seen;
  auto add = [&](RemoteProfileGroup group) {
    auto key = to_lower(trim(group.name));
    if (key.empty() || seen.count(key)) return;
    seen.insert(key);
    group.name = trim(group.name);
    group.working_dir = trim(group.working_dir);
    out.push_back(std::move(group));
  };
  for (const auto& g : profile.groups) add(g);
  for (const auto& c : profile.commands) {
    if (!trim(c.group).empty()) add({trim(c.group), ""});
  }
  return out;
}

}  // namespace

json build_remote_profile(const Config& config, const std::string& server_id) {
  auto* server = config.server_by_id(server_id);
  if (!server) {
    throw ConfigIOError("VPS не найден.");
  }

  json groups = json::array();
  for (const auto& group : config.groups_for(server_id)) {
    if (trim(group.name).empty()) continue;
    groups.push_back({
        {"name", group.name},
        {"working_dir", group.working_dir},
    });
  }

  json commands = json::array();
  for (const auto& cmd : config.commands_for(server_id)) {
    if (trim(cmd.name).empty() || trim(cmd.command).empty()) continue;
    commands.push_back({
        {"name", cmd.name},
        {"comment", cmd.comment},
        {"command", cmd.command},
        {"group", group_name_of(config, cmd)},
        {"working_dir", cmd.working_dir},
        {"timeout_sec", cmd.timeout_sec},
        {"login_shell", cmd.login_shell},
        {"confirm_before_run", cmd.confirm_before_run},
        {"cd_before_run", cmd.cd_before_run},
        {"remote_shell", cmd.remote_shell},
    });
  }

  json bundles = json::array();
  for (const auto& bundle : config.bundles_for(server_id)) {
    json steps = json::array();
    for (const auto& cid : bundle.command_ids) {
      auto* cmd = config.command_by_id(cid);
      if (!cmd || cmd->server_id != server_id) continue;
      steps.push_back({
          {"name", cmd->name},
          {"group", group_name_of(config, *cmd)},
      });
    }
    if (steps.empty() || trim(bundle.name).empty()) continue;
    bundles.push_back({
        {"name", bundle.name},
        {"interval_sec", bundle.interval_sec},
        {"commands", steps},
    });
  }

  return {
      {"fatty_profile", kRemoteProfileVersion},
      {"app", kAppName},
      {"version", resolve_version()},
      {"updated_at", now_iso()},
      {"path", kRemoteProfileRelPath},
      {"server",
       {
           {"name", server->name},
           {"host", server->host},
           {"port", server->port},
           {"username", server->username},
           {"remote_shell", server->remote_shell},
           {"health_enabled", server->health_enabled},
       }},
      {"groups", groups},
      {"commands", commands},
      {"bundles", bundles},
  };
}

RemoteProfile parse_remote_profile(const json& data) {
  if (!data.is_object()) {
    throw ConfigIOError("Неверный формат профиля FaTTY.");
  }
  if (data.value("fatty_profile", 0) != kRemoteProfileVersion) {
    throw ConfigIOError("Неподдерживаемая версия профиля FaTTY.");
  }
  auto app = data.value("app", std::string(kAppName));
  if (app != kAppName) {
    throw ConfigIOError("Файл создан другим приложением.");
  }

  RemoteProfile profile;
  profile.format = kRemoteProfileVersion;
  profile.app = app;
  profile.version = data.value("version", "");
  profile.updated_at = data.value("updated_at", "");

  json server = data.value("server", json::object());
  if (!server.is_object()) server = json::object();
  profile.name = trim(server.value("name", ""));
  profile.host = trim(server.value("host", ""));
  profile.port = server.value("port", 22);
  if (profile.port < 1 || profile.port > 65535) profile.port = 22;
  profile.username = trim(server.value("username", "root"));
  if (profile.username.empty()) profile.username = "root";
  profile.remote_shell = trim(server.value("remote_shell", ""));
  profile.health_enabled = server.value("health_enabled", true);

  json raw_groups = data.value("groups", json::array());
  if (raw_groups.is_array()) {
    for (const auto& raw : raw_groups) {
      if (!raw.is_object()) continue;
      RemoteProfileGroup group;
      group.name = trim(raw.value("name", ""));
      group.working_dir = trim(raw.value("working_dir", ""));
      if (group.name.empty()) continue;
      profile.groups.push_back(std::move(group));
    }
  }

  json raw_commands = data.value("commands", json::array());
  if (raw_commands.is_array()) {
    for (const auto& raw : raw_commands) {
      if (!raw.is_object()) continue;
      RemoteProfileCommand cmd;
      cmd.name = trim(raw.value("name", ""));
      cmd.comment = trim(raw.value("comment", ""));
      cmd.command = trim(raw.value("command", ""));
      cmd.group = trim(raw.value("group", raw.value("folder", "")));
      cmd.working_dir = trim(raw.value("working_dir", ""));
      cmd.timeout_sec = raw.value("timeout_sec", 180);
      if (cmd.timeout_sec < 1) cmd.timeout_sec = 180;
      cmd.login_shell = raw.value("login_shell", true);
      cmd.confirm_before_run = raw.value("confirm_before_run", true);
      cmd.cd_before_run = raw.value("cd_before_run", true);
      cmd.remote_shell = trim(raw.value("remote_shell", ""));
      if (cmd.name.empty() || cmd.command.empty()) continue;
      profile.commands.push_back(std::move(cmd));
    }
  }

  json raw_bundles = data.value("bundles", json::array());
  if (raw_bundles.is_array()) {
    for (const auto& raw : raw_bundles) {
      if (!raw.is_object()) continue;
      RemoteProfileBundle bundle;
      bundle.name = trim(raw.value("name", ""));
      bundle.interval_sec = clamp_int(raw.value("interval_sec", 5), 0, 3600);
      json steps = raw.value("commands", json::array());
      if (steps.is_array()) {
        for (const auto& step : steps) {
          if (step.is_string()) {
            auto name = trim(step.get<std::string>());
            if (!name.empty()) bundle.steps.emplace_back("", name);
            continue;
          }
          if (!step.is_object()) continue;
          auto name = trim(step.value("name", ""));
          if (name.empty()) continue;
          bundle.steps.emplace_back(trim(step.value("group", step.value("folder", ""))), name);
        }
      }
      if (bundle.name.empty() || bundle.steps.empty()) continue;
      profile.bundles.push_back(std::move(bundle));
    }
  }

  return profile;
}

bool remote_profile_matches_server(const RemoteProfile& profile, const Server& server) {
  if (to_lower(trim(profile.host)) != to_lower(trim(server.host))) return false;
  if (to_lower(trim(profile.username)) != to_lower(trim(server.username))) return false;
  return profile.port == server.port;
}

RemoteProfileApplyResult apply_remote_profile(Config& config, const std::string& server_id,
                                              const RemoteProfile& profile, const std::string& mode) {
  if (mode != "merge" && mode != "replace") {
    throw ConfigIOError("Неизвестный режим загрузки профиля.");
  }
  auto* server = config.server_by_id(server_id);
  if (!server) {
    throw ConfigIOError("VPS не найден.");
  }

  RemoteProfileApplyResult result;
  result.identity_mismatch = !remote_profile_matches_server(profile, *server);

  server->remote_shell = profile.remote_shell.empty() ? server->remote_shell : profile.remote_shell;
  server->health_enabled = profile.health_enabled;
  result.server_meta_applied = true;

  const bool replace = mode == "replace";
  if (replace) {
    drop_server_workspace(config, server_id, result.commands_removed);
  }

  auto named_groups = ordered_groups(profile);
  std::set<std::string> existing_group_keys;
  for (const auto& g : config.groups) {
    if (g.server_id == server_id) existing_group_keys.insert(to_lower(trim(g.name)));
  }
  for (const auto& group : named_groups) {
    auto key = to_lower(trim(group.name));
    const bool existed = existing_group_keys.count(key) != 0;
    ensure_group(config, server_id, group);
    if (!existed) {
      result.groups_added++;
      existing_group_keys.insert(key);
    }
  }

  for (const auto& item : profile.commands) {
    RemoteProfileGroup group_spec;
    group_spec.name = item.group;
    for (const auto& g : named_groups) {
      if (to_lower(trim(g.name)) == to_lower(trim(item.group))) {
        group_spec = g;
        break;
      }
    }
    auto group_id = ensure_group(config, server_id, group_spec);
    if (!replace && command_exists(config, server_id, group_id, item.name)) {
      result.commands_skipped++;
      continue;
    }
    config.commands.push_back(to_command(item, server_id, group_id));
    result.commands_added++;
  }

  std::set<std::string> existing_bundles;
  if (!replace) {
    for (const auto& b : config.bundles) {
      if (b.server_id == server_id) existing_bundles.insert(to_lower(trim(b.name)));
    }
  }
  for (const auto& item : profile.bundles) {
    auto key = to_lower(trim(item.name));
    if (!replace && existing_bundles.count(key)) {
      result.bundles_skipped++;
      continue;
    }
    auto bundle = Bundle::make_new(server_id);
    bundle.name = trim(item.name);
    bundle.interval_sec = item.interval_sec;
    for (const auto& [group_name, cmd_name] : item.steps) {
      auto group_id = group_name.empty() ? std::string() : ensure_group(config, server_id, {group_name, ""});
      if (auto* found = find_command(config, server_id, group_id, cmd_name)) {
        bundle.command_ids.push_back(found->id);
      }
    }
    if (bundle.command_ids.empty()) {
      result.bundles_skipped++;
      continue;
    }
    existing_bundles.insert(key);
    config.bundles.push_back(std::move(bundle));
    result.bundles_added++;
  }

  return result;
}

RemoteProfileApplyResult apply_remote_profile(Config& config, const std::string& server_id, const json& data,
                                              const std::string& mode) {
  return apply_remote_profile(config, server_id, parse_remote_profile(data), mode);
}

std::string format_remote_profile_summary(const RemoteProfileApplyResult& result, const std::string& mode) {
  std::ostringstream ss;
  if (mode == "replace") {
    ss << "Команд заменено: снято " << result.commands_removed << ", записано " << result.commands_added;
  } else {
    ss << "Команд добавлено: " << result.commands_added;
    if (result.commands_skipped) {
      ss << "\nКоманд пропущено (уже есть): " << result.commands_skipped;
    }
  }
  if (result.groups_added) {
    ss << "\nГрупп добавлено: " << result.groups_added;
  }
  if (result.bundles_added || result.bundles_skipped) {
    ss << "\nСвязок добавлено: " << result.bundles_added;
    if (result.bundles_skipped) {
      ss << "\nСвязок пропущено: " << result.bundles_skipped;
    }
  }
  if (result.identity_mismatch) {
    ss << "\nХост или логин в файле не совпали с карточкой VPS.";
  }
  return ss.str();
}

}  // namespace fatty

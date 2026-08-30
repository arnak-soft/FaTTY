#include "tests/test.hpp"
#include "core/config_io.hpp"
#include "core/store.hpp"
#include "core/util.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <vector>

using namespace fatty;

namespace fatty::test {

void test_store() {
  Config cfg;
  auto s = Server::make_new();
  s.name = "alpha";
  s.host = "10.0.0.1";
  s.username = "root";
  cfg.servers.push_back(s);
  auto c1 = Command::make_new(s.id);
  c1.name = "b";
  c1.command = "echo b";
  auto c2 = Command::make_new(s.id);
  c2.name = "a";
  c2.command = "echo a";
  expect(c1.confirm_before_run, "new command warns by default");
  expect(c1.cd_before_run, "new command cds by default");
  expect(c1.effective_cwd("/home") == "/home", "empty working_dir keeps session cwd");
  c1.working_dir = "/var/www/app";
  expect(c1.effective_cwd("/home") == "/var/www/app", "working_dir overrides session");
  c1.cd_before_run = false;
  expect(c1.effective_cwd("/home") == "/home", "checkbox off keeps session cwd");
  c1.cd_before_run = true;
  c1.working_dir.clear();
  cfg.commands.push_back(c1);
  cfg.commands.push_back(c2);
  expect(cfg.commands_for(s.id).size() == 2, "commands_for");
  expect(cfg.move_command(c1.id, 1), "move down");
  expect(cfg.commands[1].id == c1.id, "moved");
  cfg.sort_commands_for(s.id, "name");
  expect(cfg.commands_for(s.id)[0].name == "a", "sort by name");

  c1.comment = "zeta";
  c2.comment = "alpha note";
  cfg.commands[0].comment = c2.comment;
  cfg.commands[1].comment = c1.comment;
  cfg.sort_commands_for(s.id, "comment");
  expect(cfg.commands_for(s.id)[0].comment == "alpha note", "sort by comment");

  cfg.commands[0].working_dir = "zzz";
  cfg.commands[1].working_dir = "aaa";
  cfg.sort_commands_for(s.id, "folder");
  expect(cfg.commands_for(s.id)[0].working_dir == "aaa", "sort by working_dir");

  expect(prefer_order({"a", "b", "c"}, {"c", "a"}) == std::vector<std::string>({"c", "a", "b"}),
         "prefer_order keeps leftovers");
  expect(prefer_order({"a", "b"}, {"x", "b"}) == std::vector<std::string>({"b", "a"}),
         "prefer_order drops unknown");
  expect(prefer_order({"a", "b"}, {}) == std::vector<std::string>({"a", "b"}), "prefer_order empty preferred");

  auto folder = Folder::make_new(s.id, "proj-a");
  cfg.folders.push_back(folder);
  cfg.commands[0].folder_id = folder.id;
  expect(cfg.commands_for(s.id, folder.id).size() == 1, "folder commands");
  expect(cfg.commands_for(s.id, "").size() == 1, "general folder");
  cfg.remove_folder(folder.id);
  expect(cfg.folders.empty(), "folder removed");
  expect(cfg.commands_for(s.id, "").size() == 2, "commands moved to general");

  auto bundle = Bundle::make_new(s.id);
  bundle.name = "deploy";
  bundle.interval_sec = 8;
  bundle.command_ids = {cfg.commands[0].id, cfg.commands[1].id};
  cfg.bundles.push_back(bundle);
  expect(cfg.bundles_for(s.id).size() == 1, "bundles_for");
  expect(cfg.bundle_by_id(bundle.id)->command_ids.size() == 2, "bundle commands");
  cfg.drop_command_from_bundles(cfg.commands[0].id);
  expect(cfg.bundle_by_id(bundle.id)->command_ids.size() == 1, "pruned deleted command");
  cfg.drop_server_bundles(s.id);
  expect(cfg.bundles_for(s.id).empty(), "server bundles dropped");

  auto clone = s.duplicate(copy_name(s.name, {"alpha"}));
  expect(clone.name == "alpha (копия)", "copy name");
  expect(clone.id != s.id, "new id");
  cfg.commands[0].confirm_before_run = false;
  cfg.commands[0].working_dir = "/opt/app";
  auto cmd_copy = cfg.commands[0].duplicate("copy");
  expect(!cmd_copy.confirm_before_run, "duplicate keeps confirm off");
  expect(cmd_copy.working_dir == "/opt/app", "duplicate keeps working_dir");
  expect(cmd_copy.cd_before_run, "duplicate keeps cd_before_run");

  SessionVault vault;
  vault.create("correct-horse");
  auto payload = nlohmann::json::parse(R"({"fatty_export":1,"app":"FaTTY","servers":[{"name":"beta","host":"1.2.3.4","username":"u"}],"commands":[]})");
  auto result = import_into_config(cfg, payload, "merge", false);
  expect(result.servers_added == 1, "import merge add");
  result = import_into_config(cfg, payload, "merge", false);
  expect(result.servers_skipped == 1, "import skip existing");

  auto with_comment = nlohmann::json::parse(
      R"({"fatty_export":1,"app":"FaTTY","servers":[{"name":"gamma","host":"9.9.9.9","username":"u"}],"commands":[{"server_name":"gamma","server_host":"9.9.9.9","name":"restart","command":"reboot now","comment":"осторожно"}]})");
  result = import_into_config(cfg, with_comment, "merge", false);
  expect(result.commands_added == 1, "import command with comment");
  bool found_comment = false;
  for (const auto& cmd : cfg.commands) {
    if (cmd.name == "restart" && cmd.comment == "осторожно") found_comment = true;
  }
  expect(found_comment, "imported comment kept");

  auto no_warn = nlohmann::json::parse(
      R"({"fatty_export":1,"app":"FaTTY","servers":[{"name":"delta","host":"8.8.8.8","username":"u"}],"commands":[{"server_name":"delta","server_host":"8.8.8.8","name":"uptime","command":"uptime","confirm_before_run":false}]})");
  result = import_into_config(cfg, no_warn, "merge", false);
  bool found_no_warn = false;
  for (const auto& cmd : cfg.commands) {
    if (cmd.name == "uptime" && !cmd.confirm_before_run) found_no_warn = true;
  }
  expect(found_no_warn, "imported confirm_before_run false");

  auto with_dir = nlohmann::json::parse(
      R"({"fatty_export":1,"app":"FaTTY","servers":[{"name":"zeta","host":"6.6.6.6","username":"u"}],"commands":[{"server_name":"zeta","server_host":"6.6.6.6","name":"pull","command":"git pull","working_dir":"/var/home/project1","cd_before_run":true}]})");
  result = import_into_config(cfg, with_dir, "merge", false);
  bool found_dir = false;
  for (const auto& cmd : cfg.commands) {
    if (cmd.name == "pull" && cmd.working_dir == "/var/home/project1" && cmd.cd_before_run) found_dir = true;
  }
  expect(found_dir, "imported working_dir");

  auto no_cd = nlohmann::json::parse(
      R"({"fatty_export":1,"app":"FaTTY","servers":[{"name":"eta","host":"5.5.5.5","username":"u"}],"commands":[{"server_name":"eta","server_host":"5.5.5.5","name":"who","command":"whoami","cd_before_run":false}]})");
  result = import_into_config(cfg, no_cd, "merge", false);
  bool found_no_cd = false;
  for (const auto& cmd : cfg.commands) {
    if (cmd.name == "who" && !cmd.cd_before_run) found_no_cd = true;
  }
  expect(found_no_cd, "imported cd_before_run false");

  auto with_tools = nlohmann::json::parse(
      R"({"fatty_export":1,"app":"FaTTY","servers":[],"commands":[],"settings":{"winscp_path":"C:\\WinSCP\\WinSCP.exe","extra_programs":[{"id":"1","name":"FileZilla","path":"C:\\fz.exe","args":"{sftp_url}"}]}})");
  result = import_into_config(cfg, with_tools, "merge", true);
  expect(result.settings_applied, "import settings with programs");
  expect(cfg.settings.winscp_path.find("WinSCP") != std::string::npos, "winscp path imported");
  expect(cfg.settings.extra_programs.size() == 1, "extra program imported");
  expect(cfg.settings.extra_programs[0].name == "FileZilla", "extra name");
  expect(cfg.settings.extra_programs[0].args == "{sftp_url}", "extra args");

  auto with_bundle = nlohmann::json::parse(
      R"({"fatty_export":1,"app":"FaTTY","servers":[{"name":"eps","host":"7.7.7.7","username":"u"}],"commands":[{"server_name":"eps","server_host":"7.7.7.7","name":"pull","command":"git pull","folder":"app"},{"server_name":"eps","server_host":"7.7.7.7","name":"restart","command":"pm2 restart x","folder":"app"}],"folders":[{"server_name":"eps","server_host":"7.7.7.7","name":"app"}],"bundles":[{"server_name":"eps","server_host":"7.7.7.7","name":"Deploy","interval_sec":12,"commands":[{"name":"pull","folder":"app"},{"name":"restart","folder":"app"}]}]})");
  result = import_into_config(cfg, with_bundle, "merge", false);
  expect(result.bundles_added == 1, "import bundle");
  bool found_bundle = false;
  for (const auto& b : cfg.bundles) {
    if (b.name == "Deploy" && b.interval_sec == 12 && b.command_ids.size() == 2) found_bundle = true;
  }
  expect(found_bundle, "imported bundle steps");
  result = import_into_config(cfg, with_bundle, "merge", false);
  expect(result.bundles_skipped == 1, "skip existing bundle");

  auto saved = Bundle::make_new(s.id);
  saved.name = "roundtrip";
  saved.interval_sec = 9;
  saved.command_ids = {"aa", "bb"};
  nlohmann::json item = nlohmann::json::object();
  item["id"] = saved.id;
  item["name"] = saved.name;
  item["server_id"] = saved.server_id;
  item["interval_sec"] = saved.interval_sec;
  item["command_ids"] = saved.command_ids;
  expect(item.is_object(), "bundle json object");
  expect(item["command_ids"].is_array() && item["command_ids"].size() == 2, "command_ids array");
  nlohmann::json saved_payload;
  saved_payload["bundles"] = nlohmann::json::array();
  saved_payload["bundles"].push_back(item);
  int loaded = 0;
  for (const auto& raw : saved_payload.value("bundles", nlohmann::json::array())) {
    if (!raw.is_object()) continue;
    if (raw.value("server_id", "").empty()) continue;
    if (raw.value("id", "") == saved.id && raw.value("name", "") == "roundtrip" &&
        raw.value("server_id", "") == s.id && raw.value("interval_sec", 0) == 9 &&
        raw["command_ids"].size() == 2) {
      loaded++;
    }
  }
  expect(loaded == 1, "bundle survives json roundtrip");

  auto without_tools = nlohmann::json::parse(
      R"({"fatty_export":1,"app":"FaTTY","servers":[],"commands":[],"settings":{"theme":"light"}})");
  result = import_into_config(cfg, without_tools, "merge", true);
  expect(cfg.settings.extra_programs.size() == 1, "extra programs kept if omitted");
  expect(cfg.settings.theme == "light", "other settings still applied");
}

}  // namespace fatty::test

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

  auto clone = s.duplicate(copy_name(s.name, {"alpha"}));
  expect(clone.name == "alpha (копия)", "copy name");
  expect(clone.id != s.id, "new id");
  cfg.commands[0].confirm_before_run = false;
  auto cmd_copy = cfg.commands[0].duplicate("copy");
  expect(!cmd_copy.confirm_before_run, "duplicate keeps confirm off");

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
}

}  // namespace fatty::test

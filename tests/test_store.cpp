#include "tests/test.hpp"
#include "core/config_io.hpp"
#include "core/store.hpp"
#include "core/util.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>

using namespace fatty;
using namespace fatty::test;

void fatty::test::test_store() {
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
  cfg.commands.push_back(c1);
  cfg.commands.push_back(c2);
  expect(cfg.commands_for(s.id).size() == 2, "commands_for");
  expect(cfg.move_command(c1.id, 1), "move down");
  expect(cfg.commands[1].id == c1.id, "moved");
  cfg.sort_commands_for(s.id, "name");
  expect(cfg.commands_for(s.id)[0].name == "a", "sort by name");

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

  SessionVault vault;
  vault.create("correct-horse");
  auto payload = nlohmann::json::parse(R"({"fatty_export":1,"app":"FaTTY","servers":[{"name":"beta","host":"1.2.3.4","username":"u"}],"commands":[]})");
  auto result = import_into_config(cfg, payload, "merge", false);
  expect(result.servers_added == 1, "import merge add");
  result = import_into_config(cfg, payload, "merge", false);
  expect(result.servers_skipped == 1, "import skip existing");
}

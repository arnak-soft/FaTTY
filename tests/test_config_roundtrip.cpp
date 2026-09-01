#include "tests/test.hpp"
#include "core/store.hpp"
#include "core/util.hpp"
#include "core/vault.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>

using namespace fatty;

namespace fatty::test {

void test_config_roundtrip() {
  const auto dir = std::filesystem::temp_directory_path() / ("fatty-test-" + new_uuid());
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const auto path = dir / "config.json";

  Config cfg;
  auto s = Server::make_new();
  s.name = "srv";
  s.host = "10.0.0.2";
  s.username = "root";
  s.remote_shell = "sh";
  cfg.servers.push_back(s);

  auto grp = CommandGroup::make_new(s.id, "deploy");
  cfg.groups.push_back(grp);

  auto c1 = Command::make_new(s.id);
  c1.name = "pull";
  c1.command = "git pull";
  c1.group_id = grp.id;
  c1.working_dir = "/var/www/app";
  c1.remote_shell = "bash";
  cfg.commands.push_back(c1);

  auto bundle = Bundle::make_new(s.id);
  bundle.name = "Deploy";
  bundle.interval_sec = 7;
  bundle.command_ids = {c1.id};
  cfg.bundles.push_back(bundle);

  ExtraProgram tool;
  tool.id = "t1";
  tool.name = "WinSCP";
  tool.path = "C:\\WinSCP\\WinSCP.exe";
  tool.args = "{sftp_url}";
  cfg.settings.extra_programs.push_back(tool);
  cfg.settings.last_group_by_server[s.id] = grp.id;

  SessionVault vault;
  vault.create("roundtrip-password");
  save_config_to(cfg, vault, path);

  Config loaded = load_config_from(path);
  expect(loaded.servers.size() == 1, "roundtrip server count");
  expect(loaded.servers[0].remote_shell == "sh", "roundtrip server shell");
  expect(loaded.groups.size() == 1, "roundtrip group count");
  expect(loaded.groups[0].name == "deploy", "roundtrip group name");
  expect(loaded.commands.size() == 1, "roundtrip command count");
  expect(loaded.commands[0].group_id == grp.id, "roundtrip command group");
  expect(loaded.commands[0].working_dir == "/var/www/app", "roundtrip working_dir");
  expect(loaded.commands[0].remote_shell == "bash", "roundtrip command shell");
  expect(loaded.bundles.size() == 1, "roundtrip bundle count");
  expect(loaded.bundles[0].command_ids.size() == 1, "roundtrip bundle steps");
  expect(loaded.bundles[0].interval_sec == 7, "roundtrip bundle pause");
  expect(loaded.settings.extra_programs.size() == 1, "roundtrip extra program");
  expect(loaded.settings.last_group_by_server[s.id] == grp.id, "roundtrip last group");

  nlohmann::json legacy = nlohmann::json::parse(R"({
    "vault": {},
    "servers": [{"id":"s1","name":"legacy","host":"1.1.1.1","username":"u"}],
    "commands": [{"id":"c1","server_id":"s1","name":"x","command":"whoami","folder_id":"g1"}],
    "folders": [{"id":"g1","server_id":"s1","name":"old-group"}],
    "bundles": ["bad"],
    "settings": {"last_folder_by_server":{"s1":"g1"}}
  })");
  std::filesystem::path legacy_path = dir / "legacy.json";
  atomic_write_text(legacy_path, legacy.dump(2));
  Config migrated = load_config_from(legacy_path);
  expect(migrated.groups.size() == 1, "legacy folders -> groups");
  expect(migrated.commands[0].group_id == "g1", "legacy folder_id -> group_id");
  expect(migrated.settings.last_group_by_server["s1"] == "g1", "legacy last_folder_by_server");

  std::filesystem::remove_all(dir, ec);
}

}  // namespace fatty::test

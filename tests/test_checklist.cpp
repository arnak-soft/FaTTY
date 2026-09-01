#include "tests/test.hpp"
#include "core/config_io.hpp"
#include "core/quote.hpp"
#include "core/store.hpp"
#include "core/util.hpp"

#include <filesystem>

namespace fatty::test {

void test_checklist() {
  Config cfg;
  auto s = Server::make_new();
  s.name = "vps";
  s.host = "8.8.8.8";
  cfg.servers.push_back(s);

  auto g = CommandGroup::make_new(s.id, "prod");
  cfg.groups.push_back(g);

  auto c1 = Command::make_new(s.id);
  c1.name = "step1";
  c1.command = "echo one";
  c1.group_id = g.id;
  auto c2 = Command::make_new(s.id);
  c2.name = "step2";
  c2.command = "echo two";
  c2.group_id = g.id;
  cfg.commands.push_back(c1);
  cfg.commands.push_back(c2);

  auto bundle = Bundle::make_new(s.id);
  bundle.name = "pair";
  bundle.command_ids = {c1.id, c2.id};
  bundle.interval_sec = 3;
  cfg.bundles.push_back(bundle);
  expect(cfg.bundles_for(s.id).size() == 1, "bundle listed for server");
  expect(cfg.bundle_by_id(bundle.id)->command_ids.size() == 2, "bundle keeps both steps");

  cfg.drop_command_from_bundles(c1.id);
  expect(cfg.bundle_by_id(bundle.id)->command_ids.size() == 1, "bundle prunes missing command");

  const auto dir = std::filesystem::temp_directory_path() / ("fatty-export-" + new_uuid());
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const auto export_path = dir / "export.json";
  write_export(export_path, cfg, false, false);
  auto exported = read_export(export_path);
  expect(exported["groups"].is_array() && exported["groups"].size() == 1, "export writes groups");
  expect(exported["bundles"].is_array() && exported["bundles"].size() == 1, "export writes bundles");

  Config merged;
  merged.servers = cfg.servers;
  merged.commands = cfg.commands;
  merged.groups = cfg.groups;
  merged.bundles = cfg.bundles;
  auto imp = import_into_config(merged, exported, "merge", false);
  expect(imp.bundles_skipped >= 1, "re-import skips duplicate bundle");
  std::filesystem::remove_all(dir, ec);

  expect(normalize_remote_shell("SH") == "sh", "shell normalize sh");
  expect(normalize_remote_shell("bash") == "bash", "shell normalize bash");
  expect(effective_remote_shell(s, c1) == "bash", "command shell override");
  s.remote_shell = "sh";
  c2.remote_shell.clear();
  expect(effective_remote_shell(s, c2) == "sh", "server shell default");

  auto [bash_wrap, mark] = wrap_remote_command("echo hi", "/tmp", true, "bash");
  expect(bash_wrap.rfind("bash -lc ", 0) == 0, "bash wrapper");
  auto [sh_wrap, mark2] = wrap_remote_command("echo hi", "/tmp", false, "sh");
  expect(sh_wrap.rfind("sh -c ", 0) == 0, "sh wrapper");
  expect(mark2.rfind("FATTYCWD_", 0) == 0, "sh wrapper mark");
}

}  // namespace fatty::test

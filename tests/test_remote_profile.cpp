#include "tests/test.hpp"
#include "core/remote_profile.hpp"
#include "core/util.hpp"

#include <nlohmann/json.hpp>

using namespace fatty;

namespace fatty::test {

void test_remote_profile() {
  Config cfg;
  auto other = Server::make_new();
  other.name = "other";
  other.host = "10.0.0.9";
  other.username = "root";
  other.password = "secret-other";
  cfg.servers.push_back(other);
  auto keep = Command::make_new(other.id);
  keep.name = "keep-me";
  keep.command = "uptime";
  cfg.commands.push_back(keep);

  auto s = Server::make_new();
  s.name = "prod";
  s.host = "10.0.0.2";
  s.port = 2222;
  s.username = "deploy";
  s.password = "do-not-copy";
  s.key_path = "C:\\keys\\id_ed25519";
  s.remote_shell = "bash";
  s.health_enabled = false;
  cfg.servers.push_back(s);

  auto grp = CommandGroup::make_new(s.id, "deploy");
  grp.working_dir = "/var/www/app";
  cfg.groups.push_back(grp);

  auto pull = Command::make_new(s.id);
  pull.name = "pull";
  pull.comment = "обновить код";
  pull.command = "git pull";
  pull.group_id = grp.id;
  pull.working_dir = "/var/www/app";
  pull.timeout_sec = 90;
  pull.remote_shell = "bash";
  cfg.commands.push_back(pull);

  auto restart = Command::make_new(s.id);
  restart.name = "restart";
  restart.command = "pm2 restart app";
  restart.group_id = grp.id;
  restart.confirm_before_run = false;
  cfg.commands.push_back(restart);

  auto loose = Command::make_new(s.id);
  loose.name = "who";
  loose.command = "whoami";
  loose.cd_before_run = false;
  cfg.commands.push_back(loose);

  auto bundle = Bundle::make_new(s.id);
  bundle.name = "Deploy";
  bundle.interval_sec = 8;
  bundle.command_ids = {pull.id, restart.id};
  cfg.bundles.push_back(bundle);

  auto payload = build_remote_profile(cfg, s.id);
  expect(payload.value("fatty_profile", 0) == 1, "profile version");
  expect(payload.value("path", "") == std::string(kRemoteProfileRelPath), "default remote path");
  expect(!payload.contains("password"), "no top-level password");
  expect(!payload["server"].contains("password"), "no server password");
  expect(!payload["server"].contains("key_path"), "no key path");
  expect(!payload["server"].contains("password_blob"), "no password blob");
  expect(payload.dump().find("do-not-copy") == std::string::npos, "password not serialized");
  expect(payload.dump().find("id_ed25519") == std::string::npos, "key path not serialized");
  expect(payload["server"].value("host", "") == "10.0.0.2", "host in meta");
  expect(payload["server"].value("username", "") == "deploy", "user in meta");
  expect(payload["server"].value("port", 0) == 2222, "port in meta");
  expect(payload["server"].value("health_enabled", true) == false, "health flag");
  expect(payload["commands"].size() == 3, "three commands");
  expect(payload["groups"].size() == 1, "one group");
  expect(payload["groups"][0].value("working_dir", "") == "/var/www/app", "group working_dir");
  expect(payload["bundles"].size() == 1, "one bundle");
  expect(payload["bundles"][0]["commands"].size() == 2, "bundle steps");

  auto parsed = parse_remote_profile(payload);
  expect(parsed.commands.size() == 3, "parsed commands");
  expect(parsed.groups[0].working_dir == "/var/www/app", "parsed group dir");
  expect(parsed.bundles[0].steps.size() == 2, "parsed bundle steps");
  expect(remote_profile_matches_server(parsed, *cfg.server_by_id(s.id)), "identity matches source");

  Config fresh;
  fresh.servers.push_back(other);
  fresh.commands.push_back(keep);
  auto local = Server::make_new();
  local.name = "prod-laptop";
  local.host = "10.0.0.2";
  local.port = 2222;
  local.username = "deploy";
  local.password = "other-device-secret";
  local.key_path = "D:\\keys\\prod";
  local.remote_shell = "sh";
  fresh.servers.push_back(local);
  auto leftover = Command::make_new(local.id);
  leftover.name = "old";
  leftover.command = "echo old";
  fresh.commands.push_back(leftover);
  auto leftover_group = CommandGroup::make_new(local.id, "old-group");
  fresh.groups.push_back(leftover_group);

  auto replaced = apply_remote_profile(fresh, local.id, payload, "replace");
  expect(replaced.commands_removed == 1, "replace drops local commands");
  expect(replaced.commands_added == 3, "replace adds profile commands");
  expect(replaced.groups_added == 1, "replace adds group");
  expect(replaced.bundles_added == 1, "replace adds bundle");
  expect(replaced.identity_mismatch == false, "same host/user/port");
  auto* applied = fresh.server_by_id(local.id);
  expect(applied != nullptr, "local card still present");
  expect(applied->password == "other-device-secret", "password untouched");
  expect(applied->key_path == "D:\\keys\\prod", "key path untouched");
  expect(applied->remote_shell == "bash", "shell taken from profile");
  expect(!applied->health_enabled, "health taken from profile");
  expect(fresh.commands_for(local.id).size() == 3, "three commands after replace");
  expect(fresh.commands_for(other.id).size() == 1, "other server commands kept");
  expect(fresh.commands_for(other.id)[0].name == "keep-me", "other command name kept");

  bool found_pull = false;
  bool found_restart = false;
  for (const auto& c : fresh.commands) {
    if (c.name == "pull" && c.comment == "обновить код" && c.working_dir == "/var/www/app" &&
        c.timeout_sec == 90 && c.remote_shell == "bash") {
      found_pull = true;
    }
    if (c.name == "restart" && !c.confirm_before_run) found_restart = true;
  }
  expect(found_pull, "pull fields roundtrip");
  expect(found_restart, "confirm_before_run roundtrip");
  expect(fresh.groups_for(local.id).size() == 1, "old group gone");
  expect(fresh.groups_for(local.id)[0].working_dir == "/var/www/app", "group dir applied");
  expect(fresh.bundles_for(local.id).size() == 1, "one bundle after replace");
  expect(fresh.bundles_for(local.id)[0].interval_sec == 8, "bundle pause");
  expect(fresh.bundles_for(local.id)[0].command_ids.size() == 2, "bundle steps resolved");

  auto merged_again = apply_remote_profile(fresh, local.id, payload, "merge");
  expect(merged_again.commands_skipped == 3, "merge skips existing commands");
  expect(merged_again.bundles_skipped == 1, "merge skips existing bundle");
  expect(fresh.commands_for(local.id).size() == 3, "merge does not duplicate");

  auto extra = payload;
  extra["commands"].push_back({
      {"name", "df"},
      {"command", "df -h"},
      {"group", ""},
  });
  extra["server"]["host"] = "9.9.9.9";
  auto merged_new = apply_remote_profile(fresh, local.id, extra, "merge");
  expect(merged_new.commands_added == 1, "merge adds new command");
  expect(merged_new.identity_mismatch, "host mismatch flagged");
  expect(fresh.commands_for(local.id).size() == 4, "four commands after merge add");

  Config isolated;
  isolated.servers.push_back(other);
  isolated.commands.push_back(keep);
  auto other_payload = build_remote_profile(cfg, s.id);
  apply_remote_profile(isolated, other.id, other_payload, "replace");
  expect(isolated.commands_for(other.id).size() == 3, "replace fills other card");
  bool kept_secret = false;
  for (const auto& srv : isolated.servers) {
    if (srv.id == other.id && srv.password == "secret-other") kept_secret = true;
  }
  expect(kept_secret, "replace never copies source password");

  bool threw = false;
  try {
    parse_remote_profile(nlohmann::json::parse(R"({"fatty_export":1,"app":"FaTTY"})"));
  } catch (const ConfigIOError&) {
    threw = true;
  }
  expect(threw, "export file is not a profile");

  threw = false;
  try {
    build_remote_profile(cfg, "missing-id");
  } catch (const ConfigIOError&) {
    threw = true;
  }
  expect(threw, "unknown server rejected");

  auto with_secrets = payload;
  with_secrets["server"]["password"] = "leaked";
  with_secrets["server"]["key_path"] = "/root/.ssh/id_rsa";
  Config sanitized;
  auto card = Server::make_new();
  card.host = "10.0.0.2";
  card.port = 2222;
  card.username = "deploy";
  card.password = "local-only";
  sanitized.servers.push_back(card);
  apply_remote_profile(sanitized, card.id, with_secrets, "replace");
  expect(sanitized.servers[0].password == "local-only", "incoming password ignored");
  expect(sanitized.servers[0].key_path.empty(), "incoming key_path ignored");
}

}  // namespace fatty::test

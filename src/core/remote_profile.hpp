#pragma once

#include "core/config_io.hpp"
#include "core/store.hpp"

#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace fatty {

// Профиль команд одного VPS для выгрузки на сам сервер (SFTP).
// UI пока не подключён — см. INTENT.md. Секреты в файл не попадают.

inline constexpr int kRemoteProfileVersion = 1;
inline constexpr const char* kRemoteProfileRelPath = ".fatty/profile.json";

struct RemoteProfileGroup {
  std::string name;
  std::string working_dir;
};

struct RemoteProfileCommand {
  std::string name;
  std::string comment;
  std::string command;
  std::string group;
  std::string working_dir;
  int timeout_sec = 180;
  bool login_shell = true;
  bool confirm_before_run = true;
  bool cd_before_run = true;
  std::string remote_shell;
};

struct RemoteProfileBundle {
  std::string name;
  int interval_sec = 5;
  std::vector<std::pair<std::string, std::string>> steps;  // group, name
};

struct RemoteProfile {
  int format = kRemoteProfileVersion;
  std::string app;
  std::string version;
  std::string updated_at;
  std::string name;
  std::string host;
  int port = 22;
  std::string username;
  std::string remote_shell;
  bool health_enabled = true;
  std::vector<RemoteProfileGroup> groups;
  std::vector<RemoteProfileCommand> commands;
  std::vector<RemoteProfileBundle> bundles;
};

struct RemoteProfileApplyResult {
  int groups_added = 0;
  int commands_added = 0;
  int commands_skipped = 0;
  int commands_removed = 0;
  int bundles_added = 0;
  int bundles_skipped = 0;
  bool server_meta_applied = false;
  bool identity_mismatch = false;
};

nlohmann::json build_remote_profile(const Config& config, const std::string& server_id);
RemoteProfile parse_remote_profile(const nlohmann::json& data);
bool remote_profile_matches_server(const RemoteProfile& profile, const Server& server);
RemoteProfileApplyResult apply_remote_profile(Config& config, const std::string& server_id,
                                              const RemoteProfile& profile, const std::string& mode);
RemoteProfileApplyResult apply_remote_profile(Config& config, const std::string& server_id,
                                              const nlohmann::json& data, const std::string& mode);
std::string format_remote_profile_summary(const RemoteProfileApplyResult& result, const std::string& mode);

}  // namespace fatty

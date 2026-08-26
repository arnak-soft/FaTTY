#pragma once

#include <string>
#include <vector>

namespace fatty {

struct Preset {
  std::string name;
  std::string command;
  int timeout_sec = 180;
  bool login_shell = true;
};

inline constexpr const char* kDefaultAppDir = "/var/www/app";
inline constexpr const char* kDefaultBranch = "main";
inline constexpr const char* kDefaultPm2 = "app";

std::vector<Preset> all_presets(const std::string& app_dir = kDefaultAppDir,
                                const std::string& branch = kDefaultBranch,
                                const std::string& pm2_name = kDefaultPm2,
                                bool include_server = true);

}  // namespace fatty

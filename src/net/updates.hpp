#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fatty {

class UpdateError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct UpdateCheckResult {
  std::string status;  // update | current | none
  std::string current;
  std::optional<std::string> latest;
  std::optional<std::string> page_url;
  std::optional<std::string> download_url;
};

UpdateCheckResult check_for_updates(const std::string& current = {});

std::optional<std::string> pick_github_setup_url(std::string_view assets_json);
std::vector<std::string> installer_download_urls(const std::string& version,
                                                 const std::optional<std::string>& preferred = {});
void download_installer(const std::string& version, const std::optional<std::string>& preferred_url,
                       const std::filesystem::path& dest);

}  // namespace fatty

#pragma once

#include <optional>
#include <stdexcept>
#include <string>

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

}  // namespace fatty

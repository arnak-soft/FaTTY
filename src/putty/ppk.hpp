#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace fatty {

class PPKError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

std::string openssh_to_ppk_text(const std::filesystem::path& source);
std::filesystem::path write_openssh_as_ppk(const std::filesystem::path& source,
                                           const std::filesystem::path& dest);

}  // namespace fatty

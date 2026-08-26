#pragma once

#include "core/store.hpp"
#include "net/ssh_session.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace fatty {

class PuttyNotFoundError : public SSHError {
 public:
  using SSHError::SSHError;
};

class PuttyLaunchError : public SSHError {
 public:
  using SSHError::SSHError;
};

std::optional<std::filesystem::path> find_ssh_executable(const std::string& custom_path = {});
std::optional<std::filesystem::path> find_putty_executable(const std::string& custom_path = {});
void open_putty_console(const Server& server, const std::string& putty_path = {});
void open_system_console(const Server& server, const std::string& ssh_path = {});

}  // namespace fatty

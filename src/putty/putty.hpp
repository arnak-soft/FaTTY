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

class WinSCPNotFoundError : public SSHError {
 public:
  using SSHError::SSHError;
};

class ExternalProgramNotFoundError : public SSHError {
 public:
  using SSHError::SSHError;
};

std::optional<std::filesystem::path> find_ssh_executable(const std::string& custom_path = {});
std::optional<std::filesystem::path> find_putty_executable(const std::string& custom_path = {});
std::optional<std::filesystem::path> find_winscp_executable(const std::string& custom_path = {});
void open_putty_console(const Server& server, const std::string& putty_path = {});
void open_winscp(const Server& server, const std::string& winscp_path = {});
void open_extra_program(const ExtraProgram& program, const Server& server);
void open_system_console(const Server& server, const std::string& ssh_path = {});

}  // namespace fatty

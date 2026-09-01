#pragma once

#include "core/store.hpp"

#include <atomic>
#include <functional>
#include <stdexcept>
#include <string>

namespace fatty {

class SSHError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct RunResult {
  int exit_code = 1;
  std::string cwd;
};

class SSHSession {
 public:
  using OutputCb = std::function<void(const std::string&)>;

  SSHSession();
  ~SSHSession();
  SSHSession(const SSHSession&) = delete;
  SSHSession& operator=(const SSHSession&) = delete;

  void cancel();
  RunResult run(const Server& server, const std::string& command, int timeout_sec, bool login_shell,
                const OutputCb& on_output, const std::string& cwd = "", std::string_view shell = "bash");

 private:
  std::atomic<bool> cancel_{false};
  std::atomic<void*> channel_{nullptr};
  std::atomic<void*> session_{nullptr};
};

void* ssh_connect_raw(const Server& server);  // opaque SessionOwner*
void ssh_close_raw(void* session);
void* ssh_libssh2_session(void* raw);  // LIBSSH2_SESSION*

}  // namespace fatty

#pragma once

#include "core/journal.hpp"
#include "core/store.hpp"
#include "net/ssh_session.hpp"

#include <wx/colour.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string>

class wxStaticText;
class wxTextCtrl;

namespace fatty {

struct QueuedRun {
  Server server;
  std::string command;
  int timeout = 180;
  bool login_shell = true;
  std::string title;
  std::string command_id;
  std::string kind;
  std::string working_dir;
  bool cd_before_run = false;
  std::string remote_shell;
};

class RunController {
 public:
  struct Host {
    AppSettings* settings = nullptr;
    std::map<std::string, std::string>* remote_cwd = nullptr;
    std::shared_ptr<Journal> journal;
    std::shared_ptr<SSHSession>* session = nullptr;
    std::shared_ptr<std::atomic<bool>> alive;
    std::shared_ptr<std::atomic<bool>> worker_running;
    wxTextCtrl* output = nullptr;
    bool* busy = nullptr;
    std::string* busy_label = nullptr;
    std::function<bool()> bundle_active;
    std::function<void(const std::string&, const wxColour*)> append_output;
    std::function<void(bool)> set_busy;
    std::function<void()> update_cwd_label;
    std::function<void(const std::string&)> set_status;
  };

  explicit RunController(Host host);

  void run_command(const Server& server, const std::string& command, int timeout, bool login_shell,
                   const std::string& title, const std::string& command_id, const std::string& kind,
                   std::function<void(int code, std::string status)> on_done, std::string working_dir,
                   bool cd_before_run, std::string_view remote_shell);
  void pump_run_queue();
  void clear_run_queue();
  std::string queue_suffix() const;
  bool has_queue() const { return !run_queue_.empty(); }

 private:
  Host host_;
  std::deque<QueuedRun> run_queue_;
  void start_ssh_run(Server server, std::string command, int timeout, bool login_shell, std::string title,
                     std::string command_id, std::string kind,
                     std::function<void(int code, std::string status)> on_done, std::string working_dir,
                     bool cd_before_run, std::string remote_shell);
};

}  // namespace fatty

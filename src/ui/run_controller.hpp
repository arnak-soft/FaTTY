#pragma once

#include "core/journal.hpp"
#include "core/store.hpp"
#include "net/ssh_session.hpp"

#include <wx/colour.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class wxStaticText;
class wxTextCtrl;

namespace fatty {

/// Элемент общей очереди: одиночная команда или связка целиком.
struct QueuedJob {
  enum class Type { Command, Bundle } type = Type::Command;
  Server server;
  // Command
  std::string command;
  int timeout = 180;
  bool login_shell = true;
  std::string title;
  std::string command_id;
  std::string kind;
  std::string working_dir;
  bool cd_before_run = false;
  std::string remote_shell;
  // Bundle
  std::string bundle_id;
  std::string bundle_name;
  std::vector<Command> bundle_cmds;
  int interval_sec = 5;
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
    std::string* busy_server_id = nullptr;
    std::string* busy_label = nullptr;
    std::function<bool()> bundle_active;
    std::function<void(const std::string&, const wxColour*)> append_output;
    std::function<void(bool)> set_busy;
    std::function<void()> update_cwd_label;
    std::function<void(const std::string&)> set_status;
    /// Запуск связки из очереди (или сразу, если контроллер свободен).
    std::function<void(const Server&, const std::string& bundle_id, const std::string& bundle_name,
                       std::vector<Command> cmds, int interval_sec)>
        start_bundle;
    /// Команда завершилась (не шаг связки с on_done). title / server / status / kind.
    std::function<void(const std::string& title, const std::string& server_name, const std::string& status,
                       const std::string& kind)>
        on_command_finished;
  };

  explicit RunController(Host host);

  void run_command(const Server& server, const std::string& command, int timeout, bool login_shell,
                   const std::string& title, const std::string& command_id, const std::string& kind,
                   std::function<void(int code, std::string status)> on_done, std::string working_dir,
                   bool cd_before_run, std::string_view remote_shell);
  /// Связка: сразу, если свободно; иначе целиком в общую очередь (не раскладывая на шаги).
  void run_bundle(const Server& server, const std::string& bundle_id, const std::string& bundle_name,
                  std::vector<Command> cmds, int interval_sec);
  void pump_run_queue();
  void clear_run_queue();
  std::string queue_suffix() const;
  bool has_queue() const { return !run_queue_.empty(); }

 private:
  Host host_;
  std::deque<QueuedJob> run_queue_;
  bool is_occupied() const;
  void enqueue_job(QueuedJob job, const std::string& label);
  void start_ssh_run(Server server, std::string command, int timeout, bool login_shell, std::string title,
                     std::string command_id, std::string kind,
                     std::function<void(int code, std::string status)> on_done, std::string working_dir,
                     bool cd_before_run, std::string remote_shell);
};

}  // namespace fatty

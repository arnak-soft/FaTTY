#pragma once

#include "core/journal.hpp"
#include "core/store.hpp"
#include "ui/run_controller.hpp"

#include <wx/textctrl.h>

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace fatty {

class BundleController {
 public:
  struct Host {
    AppSettings* settings = nullptr;
    wxTextCtrl* output = nullptr;
    bool* busy = nullptr;
    std::string* busy_label = nullptr;
    RunController* runs = nullptr;
    std::function<void(const std::string&, const wxColour*)> append_output;
    std::function<void(bool)> set_busy;
    std::function<void(const std::string&)> set_status;
    std::function<void(const Server&, const Command&, std::function<void(int, std::string)> on_done)> run_step;
    std::shared_ptr<Journal> journal;
  };

  explicit BundleController(Host host);

  bool active() const { return active_; }
  bool waiting() const { return waiting_; }
  void cancel();
  void tick_waiting();
  bool start(const Server& server, const std::string& bundle_id, const std::string& bundle_name,
             std::vector<Command> cmds, int interval_sec);
  void finish(const std::string& reason);

 private:
  Host host_;
  bool active_ = false;
  bool cancel_ = false;
  bool waiting_ = false;
  int index_ = 0;
  int interval_sec_ = 5;
  int last_code_ = 1;
  std::string last_status_;
  std::string bundle_id_;
  std::string name_;
  std::string started_at_;
  Server server_;
  std::vector<Command> cmds_;
  std::chrono::steady_clock::time_point started_{};
  std::chrono::steady_clock::time_point wait_until_{};
  void run_step();
  void schedule_wait();
  void append_journal(const std::string& reason);
};

}  // namespace fatty

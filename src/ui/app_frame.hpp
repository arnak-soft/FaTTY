#pragma once

#include "core/journal.hpp"
#include "core/store.hpp"
#include "core/vault.hpp"
#include "net/ssh_session.hpp"
#include "ui/chrome.hpp"
#include "ui/striped_list.hpp"

#include <wx/button.h>
#include <wx/frame.h>
#include <wx/gauge.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace fatty {

class FilesWindow;
class JournalWindow;
class HelpWindow;

class AppFrame : public wxFrame {
 public:
  AppFrame(Config config, SessionVault vault);
  bool is_busy() const { return busy_; }
  bool files_busy() const;
  void request_close_for_install();

 private:
  void build_menu();
  void build_ui();
  void persist();
  void maybe_run_backup();
  void apply_ui_theme();
  void refresh_servers(const std::string& keep_id = {});
  void refresh_commands();
  void refresh_bundles();
  void rebuild_folder_tabs();
  void setup_command_columns();
  void setup_server_columns();
  std::vector<std::string> command_column_ids() const;
  std::vector<std::string> server_column_ids() const;
  std::string folder_display_name(const std::string& folder_id) const;
  void attach_commands_page(int index);
  std::string current_folder_id() const;
  Server* selected_server();
  Command* selected_command();
  std::vector<Command*> selected_commands();
  Bundle* selected_bundle();
  void run_command(const Server& server, const std::string& command, int timeout, bool login_shell,
                   const std::string& title, const std::string& command_id, const std::string& kind,
                   std::function<void(int code, std::string status)> on_done = {});
  void request_saved_runs();
  void start_ssh_run(Server server, std::string command, int timeout, bool login_shell, std::string title,
                     std::string command_id, std::string kind,
                     std::function<void(int code, std::string status)> on_done);
  void pump_run_queue();
  void clear_run_queue();
  std::string queue_suffix() const;
  void start_bundle();
  void run_bundle_step();
  void schedule_bundle_wait();
  void on_bundle_wait_tick();
  void finish_bundle(const std::string& reason);
  void append_output(const std::string& text, const wxColour* colour = nullptr);
  void set_busy(bool busy);
  void update_cwd_label();
  void update_busy_indicator();
  void show_journal();
  void show_help(const std::string& tab = {});
  void check_updates_interactive();
  void check_updates_async(bool interactive);
  void open_settings();
  void restore_columns();
  void rebuild_extra_tools();

  Config config_;
  SessionVault vault_;
  // shared_ptr: фоновый поток команды удерживает журнал и сессию живыми, даже
  // если окно закрыли до её завершения.
  std::shared_ptr<Journal> journal_;
  std::shared_ptr<SSHSession> session_;
  std::map<std::string, std::string> remote_cwd_;
  std::map<std::string, CommandRunStats> command_stats_;
  bool busy_ = false;
  bool closing_for_install_ = false;
  bool restoring_ = true;
  bool updating_folders_ = false;
  bool checking_updates_ = false;
  std::string server_filter_;
  std::string busy_label_;
  std::chrono::steady_clock::time_point run_start_{};
  bool bundle_active_ = false;
  bool bundle_cancel_ = false;
  int bundle_index_ = 0;
  int bundle_interval_sec_ = 5;
  int bundle_wait_left_ = 0;
  std::string bundle_name_;
  Server bundle_server_;
  std::vector<Command> bundle_cmds_;
  wxTimer bundle_wait_timer_;
  // Живой-токен: воркеры проверяют его перед обращением к окну через CallAfter,
  // чтобы не работать по разрушенному AppFrame.
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
  // Взводится на время работы потока команды: при выходе ждём его завершения,
  // иначе поток может дёрнуть wxTheApp уже после разрушения приложения.
  std::shared_ptr<std::atomic<bool>> worker_running_ = std::make_shared<std::atomic<bool>>(false);

  struct QueuedRun {
    Server server;
    std::string command;
    int timeout = 180;
    bool login_shell = true;
    std::string title;
    std::string command_id;
    std::string kind;
  };
  std::deque<QueuedRun> run_queue_;

  wxSplitterWindow* vsplit_{};
  wxSplitterWindow* hsplit_{};
  wxTextCtrl* server_search_{};
  StripedListCtrl* servers_{};
  RoundedNotebook* folders_nb_{};
  StripedListCtrl* commands_{};
  StripedListCtrl* bundles_{};
  std::vector<std::string> folder_tab_ids_;
  wxTextCtrl* output_{};
  wxTextCtrl* quick_{};
  wxStaticText* cwd_label_{};
  RoundButton* cwd_reset_{};
  RoundButton* run_btn_{};
  RoundButton* stop_btn_{};
  wxGauge* busy_gauge_{};
  wxTimer busy_timer_;
  std::vector<wxWindow*> busy_disable_;
  std::vector<wxWindow*> extra_tool_btns_;
  wxWindow* extra_tools_parent_{};
  wxSizer* extra_tools_sizer_{};
  wxStaticText* status_{};
  std::map<std::string, FilesWindow*> files_windows_;
  JournalWindow* journal_window_ = nullptr;
  HelpWindow* help_window_ = nullptr;
};

}  // namespace fatty

#pragma once

#include "core/journal.hpp"
#include "core/store.hpp"
#include "core/vault.hpp"
#include "net/ssh_session.hpp"
#include "ui/bundle_controller.hpp"
#include "ui/chrome.hpp"
#include "ui/run_controller.hpp"
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
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace fatty {

class FilesWindow;
class JournalWindow;
class HelpWindow;
class BundleStepsWindow;

class AppFrame : public wxFrame {
 public:
  AppFrame(Config config, SessionVault vault);
  bool is_busy() const { return busy_; }
  bool files_busy() const;
  void request_close_for_install();

 private:
  void build_menu();
  void build_ui();
  void init_run_controllers();
  void persist();
  void maybe_run_backup();
  void apply_ui_theme();
  void refresh_servers(const std::string& keep_id = {});
  void refresh_commands();
  void refresh_bundles();
  void rebuild_group_tabs();
  void setup_command_columns();
  void setup_server_columns();
  std::vector<std::string> command_column_ids() const;
  std::vector<std::string> server_column_ids() const;
  void attach_commands_page(int index);
  std::string current_group_id() const;
  Server* selected_server();
  Command* selected_command();
  std::vector<Command*> selected_commands();
  Bundle* selected_bundle();
  void run_command(const Server& server, const std::string& command, int timeout, bool login_shell,
                   const std::string& title, const std::string& command_id, const std::string& kind,
                   std::function<void(int code, std::string status)> on_done = {},
                   std::string working_dir = {}, bool cd_before_run = false, std::string remote_shell = {});
  void request_saved_runs();
  void advance_command_selection();
  void start_bundle(const std::string& bundle_id_override = {});
  void open_bundle_steps();
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
  void track_window_state();
  void rebuild_extra_tools();
  void show_servers_context_menu(long row);
  void show_commands_context_menu(long row);
  void show_bundles_context_menu(long row);
  void show_group_tab_context_menu(int tab_index);
  void show_sections_tab_context_menu(int tab_index, wxWindow* groups_page, wxWindow* bundles_page);
  void edit_group_folder(const std::string& group_id);
  void add_group();
  void rename_group(const std::string& group_id);
  void delete_group(const std::string& group_id);

  Config config_;
  SessionVault vault_;
  std::shared_ptr<Journal> journal_;
  std::shared_ptr<SSHSession> session_;
  std::unique_ptr<RunController> runs_;
  std::unique_ptr<BundleController> bundle_run_;
  std::map<std::string, std::string> remote_cwd_;
  std::map<std::string, CommandRunStats> command_stats_;
  bool busy_ = false;
  bool closing_for_install_ = false;
  bool restoring_ = true;
  bool updating_groups_ = false;
  bool checking_updates_ = false;
  bool window_maximized_ = false;
  std::string normal_geometry_;
  std::string server_filter_;
  std::string busy_label_;
  std::chrono::steady_clock::time_point run_start_{};
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
  std::shared_ptr<std::atomic<bool>> worker_running_ = std::make_shared<std::atomic<bool>>(false);

  wxSplitterWindow* vsplit_{};
  wxSplitterWindow* hsplit_{};
  wxTextCtrl* server_search_{};
  StripedListCtrl* servers_{};
  RoundedNotebook* groups_nb_{};
  StripedListCtrl* commands_{};
  StripedListCtrl* bundles_{};
  std::vector<std::string> group_tab_ids_;
  wxTextCtrl* output_{};
  wxTextCtrl* quick_{};
  wxStaticText* cwd_label_{};
  RoundButton* cwd_reset_{};
  RoundButton* run_btn_{};
  RoundButton* stop_btn_{};
  RoundButton* bundles_stop_btn_{};
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
  BundleStepsWindow* bundle_steps_window_ = nullptr;
};

}  // namespace fatty
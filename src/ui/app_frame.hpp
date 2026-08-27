#pragma once

#include "core/journal.hpp"
#include "core/store.hpp"
#include "core/vault.hpp"
#include "net/ssh_session.hpp"

#include <wx/button.h>
#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <map>
#include <memory>

namespace fatty {

class FilesWindow;
class JournalWindow;
class HelpWindow;

class AppFrame : public wxFrame {
 public:
  AppFrame(Config config, SessionVault vault);

 private:
  void build_menu();
  void build_ui();
  void persist();
  void apply_ui_theme();
  void refresh_servers(const std::string& keep_id = {});
  void refresh_commands();
  void rebuild_folder_tabs();
  void attach_commands_page(int index);
  std::string current_folder_id() const;
  Server* selected_server();
  Command* selected_command();
  void run_command(const Server& server, const std::string& command, int timeout, bool login_shell,
                   const std::string& title, const std::string& command_id, const std::string& kind);
  void append_output(const std::string& text, const wxColour* colour = nullptr);
  void set_busy(bool busy);
  void update_cwd_label();
  void show_journal();
  void show_help(const std::string& tab = {});
  void check_updates_interactive();
  void open_settings();

  Config config_;
  SessionVault vault_;
  Journal journal_;
  std::unique_ptr<SSHSession> session_;
  std::map<std::string, std::string> remote_cwd_;
  std::map<std::string, JournalEntry> last_runs_;
  bool busy_ = false;
  bool restoring_ = true;
  bool updating_folders_ = false;

  wxSplitterWindow* vsplit_{};
  wxSplitterWindow* hsplit_{};
  wxListCtrl* servers_{};
  wxNotebook* folders_nb_{};
  wxListCtrl* commands_{};
  std::vector<std::string> folder_tab_ids_;
  wxTextCtrl* output_{};
  wxTextCtrl* quick_{};
  wxStaticText* cwd_label_{};
  wxButton* cwd_reset_{};
  wxButton* run_btn_{};
  wxButton* stop_btn_{};
  wxStaticText* status_{};
  std::map<std::string, FilesWindow*> files_windows_;
  JournalWindow* journal_window_ = nullptr;
  HelpWindow* help_window_ = nullptr;
};

}  // namespace fatty

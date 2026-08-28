#pragma once

#include "core/presets.hpp"
#include "core/store.hpp"
#include "core/vault.hpp"
#include "ui/layout.hpp"
#include "ui/chrome.hpp"

#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace fatty {

class ServerDialog : public PositionedDialog {
 public:
  ServerDialog(wxWindow* parent, const Server& server, const wxString& title, bool is_new);
  Server result;
  bool accepted = false;

 private:
  void on_ok(wxCommandEvent&);
  void mark_error(wxTextCtrl* field, const wxString& message);
  void clear_errors();
  Server server_;
  bool is_new_;
  wxTextCtrl* name_{};
  wxTextCtrl* host_{};
  wxTextCtrl* port_{};
  wxTextCtrl* user_{};
  wxTextCtrl* password_{};
  wxTextCtrl* key_{};
  wxCheckBox* show_pw_{};
  wxCheckBox* clear_pw_{};
  wxStaticText* error_{};
  std::string stored_password_;
};

class PresetDialog : public PositionedDialog {
 public:
  PresetDialog(wxWindow* parent, const Server& server);
  std::vector<Preset> result;
  bool accepted = false;

 private:
  void rebuild();
  void on_ok(wxCommandEvent&);
  wxTextCtrl* app_dir_{};
  wxTextCtrl* branch_{};
  wxTextCtrl* pm2_{};
  wxPanel* list_{};
  std::vector<wxCheckBox*> checks_;
  std::vector<Preset> presets_;
};

class CommandDialog : public PositionedDialog {
 public:
  CommandDialog(wxWindow* parent, const Command& command, const std::vector<Server>& servers,
                const std::vector<Folder>& folders, const wxString& title);
  Command result;
  bool accepted = false;

 private:
  void on_ok(wxCommandEvent&);
  void fill_folders();
  Command command_;
  std::vector<Server> servers_;
  std::vector<Folder> folders_;
  std::vector<Preset> presets_;
  std::vector<std::string> folder_ids_;
  wxTextCtrl* name_{};
  wxComboBox* server_{};
  wxComboBox* folder_{};
  wxTextCtrl* timeout_{};
  wxCheckBox* login_{};
  wxComboBox* preset_{};
  wxTextCtrl* text_{};
};

class MasterPasswordDialog : public PositionedDialog {
 public:
  MasterPasswordDialog(wxWindow* parent, Config& config, SessionVault& vault);
  bool ok = false;

 private:
  void on_submit(wxCommandEvent&);
  void refresh_lockout();
  Config& config_;
  SessionVault& vault_;
  bool setup_;
  wxTextCtrl* pw_{};
  wxTextCtrl* pw2_{};
  wxStaticText* error_{};
  RoundButton* continue_btn_{};
};

class ChangeMasterDialog : public PositionedDialog {
 public:
  ChangeMasterDialog(wxWindow* parent, SessionVault& vault, bool allow_short);
  bool ok = false;

 private:
  void on_submit(wxCommandEvent&);
  SessionVault& vault_;
  bool allow_short_;
  wxTextCtrl* old_{};
  wxTextCtrl* neu_{};
  wxTextCtrl* neu2_{};
  wxStaticText* error_{};
};

}  // namespace fatty

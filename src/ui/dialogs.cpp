#include "ui/dialogs.hpp"

#include "core/lockout.hpp"
#include "core/util.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/button.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#ifdef __WXMSW__
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fatty {
namespace {

wxTextCtrl* labeled_entry(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, const wxString& value,
                          long style = 0) {
  grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
  auto* e = new wxTextCtrl(parent, wxID_ANY, value, wxDefaultPosition, wxDefaultSize, style);
  style_text(e);
  grid->Add(e, 1, wxEXPAND);
  return e;
}

}  // namespace

ServerDialog::ServerDialog(wxWindow* parent, const Server& server, const wxString& title, bool is_new)
    : PositionedDialog(parent, title, wxSize(520, 420)), server_(server), is_new_(is_new) {
  stored_password_ = is_new ? "" : server.password;
  auto* body = new wxPanel(this);
  auto* grid = new wxFlexGridSizer(8, 2, 8, 8);
  grid->AddGrowableCol(1);
  name_ = labeled_entry(body, grid, "Имя", wxString::FromUTF8(server.name));
  host_ = labeled_entry(body, grid, "Хост / IP", wxString::FromUTF8(server.host));
  port_ = labeled_entry(body, grid, "Порт", wxString::FromUTF8(std::to_string(server.port ? server.port : 22)));
  user_ = labeled_entry(body, grid, "Логин", wxString::FromUTF8(server.username));
  grid->Add(new wxStaticText(body, wxID_ANY, "Пароль"), 0, wxALIGN_CENTER_VERTICAL);
  auto* pwrow = new wxBoxSizer(wxHORIZONTAL);
  password_ = new wxTextCtrl(body, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
  style_text(password_);
  pwrow->Add(password_, 1, wxEXPAND);
  show_pw_ = new wxCheckBox(body, wxID_ANY, "показать");
  pwrow->Add(show_pw_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
  grid->Add(pwrow, 1, wxEXPAND);
  wxString note;
  if (is_new) {
    note = "После сохранения пароль нельзя просмотреть — только заменить новым.";
  } else if (!stored_password_.empty()) {
    note = "Пароль сохранён, просмотр недоступен. Оставьте поле пустым, чтобы не менять.";
  } else {
    note = "Пароль не задан. После сохранения его нельзя будет просмотреть.";
  }
  grid->Add(new wxStaticText(body, wxID_ANY, ""), 0);
  auto* note_l = new wxStaticText(body, wxID_ANY, note);
  note_l->SetName("muted");
  note_l->SetForegroundColour(Theme::muted());
  grid->Add(note_l, 1, wxEXPAND);
  if (!stored_password_.empty()) {
    grid->Add(new wxStaticText(body, wxID_ANY, ""), 0);
    clear_pw_ = new wxCheckBox(body, wxID_ANY, "Удалить сохранённый пароль");
    grid->Add(clear_pw_, 1);
  }
  grid->Add(new wxStaticText(body, wxID_ANY, "SSH-ключ"), 0, wxALIGN_CENTER_VERTICAL);
  auto* keyrow = new wxBoxSizer(wxHORIZONTAL);
  key_ = new wxTextCtrl(body, wxID_ANY, wxString::FromUTF8(server.key_path));
  style_text(key_);
  auto* browse = new wxButton(body, wxID_ANY, "Обзор…");
  keyrow->Add(key_, 1, wxEXPAND);
  keyrow->Add(browse, 0, wxLEFT, 8);
  grid->Add(keyrow, 1, wxEXPAND);

  error_ = new wxStaticText(body, wxID_ANY, "");
  error_->SetName("error");
  error_->SetForegroundColour(Theme::err());

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  auto* save = accent_button(body, "Сохранить");
  auto* cancel = new wxButton(body, wxID_CANCEL, "Отмена");
  btns->Add(save, 0, wxRIGHT, 8);
  btns->Add(cancel);
  save->SetDefault();

  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(grid, 1, wxEXPAND | wxALL, 12);
  root->Add(error_, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
  root->Add(btns, 0, wxEXPAND | wxALL, 12);
  body->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(body, 1, wxEXPAND);
  SetSizer(outer);
  apply_dark(this);

  show_pw_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
#ifdef __WXMSW__
    // wxTE_PASSWORD не переключается через SetWindowStyleFlag на MSW —
    // меняем символ-маску напрямую.
    auto hwnd = static_cast<HWND>(password_->GetHWND());
    SendMessageW(hwnd, EM_SETPASSWORDCHAR, show_pw_->GetValue() ? 0 : 0x25CF, 0);
    password_->Refresh();
#endif
  });
  if (clear_pw_) {
    clear_pw_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
      bool c = clear_pw_->GetValue();
      password_->Enable(!c);
      if (c) password_->Clear();
    });
  }
  browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxFileDialog dlg(this, "SSH private key", wxStandardPaths::Get().GetUserConfigDir() + "/.ssh", "",
                     "All files|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) key_->SetValue(dlg.GetPath());
  });
  save->Bind(wxEVT_BUTTON, &ServerDialog::on_ok, this);
  for (wxTextCtrl* f : {name_, host_, user_, port_}) {
    f->Bind(wxEVT_TEXT, [this](wxCommandEvent& e) { clear_errors(); e.Skip(); });
  }
}

void ServerDialog::mark_error(wxTextCtrl* field, const wxString& message) {
  error_->SetLabel(message);
  if (field) {
    field->SetBackgroundColour(Theme::err());
    field->SetForegroundColour(*wxWHITE);
    field->Refresh();
    field->SetFocus();
  }
  Layout();
}

void ServerDialog::clear_errors() {
  if (error_->GetLabel().empty()) return;
  error_->SetLabel("");
  for (wxTextCtrl* f : {name_, host_, user_, port_}) {
    style_text(f);
    f->Refresh();
  }
  Layout();
}

void ServerDialog::on_ok(wxCommandEvent&) {
  clear_errors();
  auto name = trim(std::string(name_->GetValue().utf8_string()));
  auto host = trim(std::string(host_->GetValue().utf8_string()));
  auto user = trim(std::string(user_->GetValue().utf8_string()));
  if (name.empty()) { mark_error(name_, "Укажите имя VPS."); return; }
  if (host.empty()) { mark_error(host_, "Укажите хост или IP."); return; }
  if (user.empty()) { mark_error(user_, "Укажите логин."); return; }
  int port = 22;
  if (!parse_int(std::string(port_->GetValue().utf8_string()), port) || port < 1 || port > 65535) {
    mark_error(port_, "Порт должен быть числом 1–65535.");
    return;
  }
  std::string password;
  if (clear_pw_ && clear_pw_->GetValue()) {
    password.clear();
  } else {
    auto typed = std::string(password_->GetValue().utf8_string());
    password = typed.empty() ? stored_password_ : typed;
  }
  auto key_path = trim(std::string(key_->GetValue().utf8_string()));
  if (password.empty() && key_path.empty()) {
    if (wxMessageBox("Пароль и ключ пустые. Подключаться через ssh-agent / ключи по умолчанию?", "Без пароля и ключа",
                     wxYES_NO | wxICON_QUESTION, this) != wxYES) {
      return;
    }
  }
  result = server_;
  result.name = name;
  result.host = host;
  result.port = port;
  result.username = user;
  result.password = password;
  result.key_path = key_path;
  accepted = true;
  EndModal(wxID_OK);
}

PresetDialog::PresetDialog(wxWindow* parent, const Server& server)
    : PositionedDialog(parent, wxString::FromUTF8("Пресеты — " + server.name), wxSize(560, 520)) {
  auto* body = new wxPanel(this);
  auto* form = new wxFlexGridSizer(3, 2, 6, 8);
  form->AddGrowableCol(1);
  form->Add(new wxStaticText(body, wxID_ANY, "Каталог приложения"), 0, wxALIGN_CENTER_VERTICAL);
  app_dir_ = new wxTextCtrl(body, wxID_ANY, kDefaultAppDir);
  style_text(app_dir_);
  form->Add(app_dir_, 1, wxEXPAND);
  form->Add(new wxStaticText(body, wxID_ANY, "Ветка git"), 0, wxALIGN_CENTER_VERTICAL);
  branch_ = new wxTextCtrl(body, wxID_ANY, kDefaultBranch);
  style_text(branch_);
  form->Add(branch_, 1, wxEXPAND);
  form->Add(new wxStaticText(body, wxID_ANY, "Процесс pm2"), 0, wxALIGN_CENTER_VERTICAL);
  pm2_ = new wxTextCtrl(body, wxID_ANY, kDefaultPm2);
  style_text(pm2_);
  form->Add(pm2_, 1, wxEXPAND);

  auto* refresh = new wxButton(body, wxID_ANY, "Обновить список");
  list_ = new wxPanel(body);
  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  auto* add = accent_button(body, "Добавить выбранные");
  btns->Add(add, 0, wxRIGHT, 8);
  btns->Add(new wxButton(body, wxID_CANCEL, "Отмена"));
  add->SetDefault();

  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(form, 0, wxEXPAND | wxALL, 12);
  root->Add(refresh, 0, wxLEFT, 12);
  root->Add(list_, 1, wxEXPAND | wxALL, 12);
  root->Add(btns, 0, wxEXPAND | wxALL, 12);
  body->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(body, 1, wxEXPAND);
  SetSizer(outer);
  rebuild();
  apply_dark(this);
  refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { rebuild(); });
  add->Bind(wxEVT_BUTTON, &PresetDialog::on_ok, this);
}

void PresetDialog::rebuild() {
  list_->DestroyChildren();
  checks_.clear();
  auto* sizer = new wxBoxSizer(wxVERTICAL);
  presets_ = all_presets(std::string(app_dir_->GetValue().utf8_string()),
                         std::string(branch_->GetValue().utf8_string()),
                         std::string(pm2_->GetValue().utf8_string()), true);
  for (const auto& p : presets_) {
    auto preview = p.command.size() > 80 ? p.command.substr(0, 77) + "…" : p.command;
    auto* cb = new wxCheckBox(list_, wxID_ANY, wxString::FromUTF8(p.name + "  —  " + preview));
    cb->SetValue(true);
    sizer->Add(cb, 0, wxBOTTOM, 4);
    checks_.push_back(cb);
  }
  list_->SetSizer(sizer);
  list_->Layout();
  apply_dark(list_);
}

void PresetDialog::on_ok(wxCommandEvent&) {
  result.clear();
  for (std::size_t i = 0; i < checks_.size(); ++i) {
    if (checks_[i]->GetValue()) result.push_back(presets_[i]);
  }
  if (result.empty()) {
    wxMessageBox("Ничего не выбрано.", "Пресеты", wxOK | wxICON_WARNING, this);
    return;
  }
  accepted = true;
  EndModal(wxID_OK);
}

CommandDialog::CommandDialog(wxWindow* parent, const Command& command, const std::vector<Server>& servers,
                             const std::vector<Folder>& folders, const wxString& title)
    : PositionedDialog(parent, title, wxSize(640, 520)), command_(command), servers_(servers), folders_(folders) {
  auto* body = new wxPanel(this);
  auto* form = new wxFlexGridSizer(6, 2, 6, 8);
  form->AddGrowableCol(1);
  name_ = labeled_entry(body, form, "Название", wxString::FromUTF8(command.name));
  form->Add(new wxStaticText(body, wxID_ANY, "VPS"), 0, wxALIGN_CENTER_VERTICAL);
  wxArrayString names;
  wxString current;
  for (const auto& s : servers) {
    names.Add(wxString::FromUTF8(s.name));
    if (s.id == command.server_id) current = wxString::FromUTF8(s.name);
  }
  server_ = new wxComboBox(body, wxID_ANY, current, wxDefaultPosition, wxDefaultSize, names, wxCB_READONLY);
  form->Add(server_, 1, wxEXPAND);
  form->Add(new wxStaticText(body, wxID_ANY, "Папка"), 0, wxALIGN_CENTER_VERTICAL);
  folder_ = new wxComboBox(body, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxArrayString(), wxCB_READONLY);
  form->Add(folder_, 1, wxEXPAND);
  fill_folders();
  timeout_ = labeled_entry(body, form, "Таймаут, с", wxString::FromUTF8(std::to_string(command.timeout_sec)));
  form->Add(new wxStaticText(body, wxID_ANY, ""), 0);
  login_ = new wxCheckBox(body, wxID_ANY, "Login-shell (bash -lc) — подхватывает PATH из .bashrc");
  login_->SetValue(command.login_shell);
  form->Add(login_, 1);
  form->Add(new wxStaticText(body, wxID_ANY, "Пресет"), 0, wxALIGN_CENTER_VERTICAL);
  presets_ = all_presets();
  wxArrayString pname;
  for (const auto& p : presets_) pname.Add(wxString::FromUTF8(p.name));
  preset_ = new wxComboBox(body, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, pname, wxCB_READONLY);
  form->Add(preset_, 1, wxEXPAND);

  text_ = new wxTextCtrl(body, wxID_ANY, wxString::FromUTF8(command.command), wxDefaultPosition,
                         wxSize(-1, FromDIP(160)), wxTE_MULTILINE | wxTE_WORDWRAP);
  style_text(text_, true);
  text_->SetMinSize(wxSize(-1, FromDIP(120)));

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  auto* save = accent_button(body, "Сохранить");
  btns->Add(save, 0, wxRIGHT, 8);
  btns->Add(new wxButton(body, wxID_CANCEL, "Отмена"));

  save->SetDefault();
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(form, 0, wxEXPAND | wxALL, 12);
  root->Add(new wxStaticText(body, wxID_ANY, "Команда"), 0, wxLEFT | wxRIGHT, 12);
  root->Add(text_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
  // Buttons stay at the bottom and never shrink away.
  root->Add(btns, 0, wxEXPAND | wxALL, 12);
  body->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(body, 1, wxEXPAND);
  SetSizerAndFit(outer);
  SetMinSize(FromDIP(wxSize(520, 420)));
  if (GetSize().GetHeight() < FromDIP(420)) {
    SetSize(GetSize().GetWidth(), FromDIP(520));
  }
  apply_dark(this);
  server_->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { fill_folders(); });
  preset_->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
    auto chosen = std::string(preset_->GetValue().utf8_string());
    for (const auto& p : presets_) {
      if (p.name == chosen) {
        name_->SetValue(wxString::FromUTF8(p.name));
        timeout_->SetValue(std::to_string(p.timeout_sec));
        login_->SetValue(p.login_shell);
        text_->SetValue(wxString::FromUTF8(p.command));
        break;
      }
    }
  });
  save->Bind(wxEVT_BUTTON, &CommandDialog::on_ok, this);
}

void CommandDialog::fill_folders() {
  auto server_name = trim(std::string(server_->GetValue().utf8_string()));
  std::string sid;
  for (const auto& s : servers_) {
    if (s.name == server_name) {
      sid = s.id;
      break;
    }
  }
  folder_->Clear();
  folder_ids_.clear();
  folder_->Append("Общее");
  folder_ids_.push_back("");
  int sel = 0;
  for (const auto& f : folders_) {
    if (f.server_id != sid) continue;
    folder_->Append(wxString::FromUTF8(f.name));
    folder_ids_.push_back(f.id);
    if (f.id == command_.folder_id) sel = static_cast<int>(folder_ids_.size()) - 1;
  }
  folder_->SetSelection(sel);
}

void CommandDialog::on_ok(wxCommandEvent&) {
  auto name = trim(std::string(name_->GetValue().utf8_string()));
  auto server_name = trim(std::string(server_->GetValue().utf8_string()));
  auto cmd = trim(std::string(text_->GetValue().utf8_string()));
  if (name.empty() || cmd.empty() || server_name.empty()) {
    wxMessageBox("Заполните название, VPS и команду.", "Проверка", wxOK | wxICON_WARNING, this);
    return;
  }
  int timeout = 180;
  if (!parse_int(std::string(timeout_->GetValue().utf8_string()), timeout) || timeout < 1) {
    wxMessageBox("Таймаут должен быть положительным числом.", "Проверка", wxOK | wxICON_WARNING, this);
    return;
  }
  std::string sid;
  for (const auto& s : servers_) {
    if (s.name == server_name) {
      sid = s.id;
      break;
    }
  }
  result = command_;
  result.name = name;
  result.server_id = sid;
  result.command = cmd;
  result.timeout_sec = timeout;
  result.login_shell = login_->GetValue();
  int fsel = folder_->GetSelection();
  if (fsel >= 0 && fsel < static_cast<int>(folder_ids_.size())) {
    result.folder_id = folder_ids_[static_cast<std::size_t>(fsel)];
  } else {
    result.folder_id.clear();
  }
  accepted = true;
  EndModal(wxID_OK);
}

MasterPasswordDialog::MasterPasswordDialog(wxWindow* parent, Config& config, SessionVault& vault)
    : PositionedDialog(parent, "Мастер-пароль", wxSize(480, 280)),
      config_(config),
      vault_(vault),
      setup_(!config.has_vault) {
  auto* body = new wxPanel(this);
  auto* intro = new wxStaticText(
      body, wxID_ANY,
      setup_ ? "Задайте мастер-пароль. Им шифруются пароли VPS.\nБез него конфиг нельзя расшифровать — восстановить фразу нельзя."
             : "Введите мастер-пароль, чтобы открыть сохранённые пароли VPS.");
  intro->Wrap(420);
  auto* form = new wxFlexGridSizer(3, 2, 8, 8);
  form->AddGrowableCol(1);
  form->Add(new wxStaticText(body, wxID_ANY, "Мастер-пароль"), 0, wxALIGN_CENTER_VERTICAL);
  pw_ = new wxTextCtrl(body, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
  style_text(pw_);
  form->Add(pw_, 1, wxEXPAND);
  if (setup_) {
    form->Add(new wxStaticText(body, wxID_ANY, "Ещё раз"), 0, wxALIGN_CENTER_VERTICAL);
    pw2_ = new wxTextCtrl(body, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    style_text(pw2_);
    form->Add(pw2_, 1, wxEXPAND);
    auto* hint = new wxStaticText(body, wxID_ANY, wxString::Format("Не короче %d символов.", kMinPasswordLen));
    hint->SetName("muted");
    hint->SetForegroundColour(Theme::muted());
    form->Add(new wxStaticText(body, wxID_ANY, ""), 0);
    form->Add(hint, 1);
  }
  error_ = new wxStaticText(body, wxID_ANY, "");
  error_->SetName("error");
  error_->SetForegroundColour(Theme::err());
  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  continue_btn_ = accent_button(body, "Продолжить");
  btns->Add(continue_btn_, 0, wxRIGHT, 8);
  btns->Add(new wxButton(body, wxID_CANCEL, "Выход"));
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(intro, 0, wxALL, 16);
  root->Add(form, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);
  root->Add(error_, 0, wxLEFT | wxRIGHT | wxTOP, 16);
  root->Add(btns, 0, wxEXPAND | wxALL, 16);
  body->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(body, 1, wxEXPAND);
  SetSizer(outer);
  apply_dark(this);
  continue_btn_->SetDefault();
  continue_btn_->Bind(wxEVT_BUTTON, &MasterPasswordDialog::on_submit, this);
  if (!setup_) refresh_lockout();
}

void MasterPasswordDialog::refresh_lockout() {
  auto msg = lockout_message(config_.settings);
  if (!msg.empty()) {
    error_->SetLabel(wxString::FromUTF8(msg));
    pw_->Enable(false);
    continue_btn_->Enable(false);
    return;
  }
  error_->SetLabel("");
  pw_->Enable(true);
  continue_btn_->Enable(true);
}

void MasterPasswordDialog::on_submit(wxCommandEvent&) {
  auto password = std::string(pw_->GetValue().utf8_string());
  error_->SetLabel("");
  if (!setup_) {
    auto blocked = lockout_message(config_.settings);
    if (!blocked.empty()) {
      error_->SetLabel(wxString::FromUTF8(blocked));
      refresh_lockout();
      return;
    }
  }
  try {
    if (setup_) {
      if (password != std::string(pw2_->GetValue().utf8_string())) {
        error_->SetLabel("Пароли не совпадают.");
        return;
      }
      auto meta = vault_.create(password);
      config_.vault = meta;
      config_.has_vault = true;
    } else {
      if (!vault_.unlock(password, config_.vault)) {
        error_->SetLabel(wxString::FromUTF8(record_failed_attempt(config_.settings)));
        pw_->Clear();
        refresh_lockout();
        return;
      }
      record_success();
      unlock_secrets(config_, vault_);
    }
  } catch (const VaultError& exc) {
    error_->SetLabel(wxString::FromUTF8(exc.what()));
    return;
  } catch (...) {
    error_->SetLabel("Не удалось открыть хранилище. Проверьте пароль.");
    return;
  }
  ok = true;
  pw_->Clear();
  if (pw2_) pw2_->Clear();
  EndModal(wxID_OK);
}

ChangeMasterDialog::ChangeMasterDialog(wxWindow* parent, SessionVault& vault, bool allow_short)
    : PositionedDialog(parent, "Сменить мастер-пароль", wxSize(460, 300)), vault_(vault), allow_short_(allow_short) {
  auto* body = new wxPanel(this);
  auto* intro = new wxStaticText(body, wxID_ANY, "Текущие пароли VPS будут перешифрованы новым мастер-паролем.");
  intro->Wrap(400);
  auto* form = new wxFlexGridSizer(3, 2, 8, 8);
  form->AddGrowableCol(1);
  old_ = labeled_entry(body, form, "Текущий", "", wxTE_PASSWORD);
  neu_ = labeled_entry(body, form, "Новый", "", wxTE_PASSWORD);
  neu2_ = labeled_entry(body, form, "Новый ещё раз", "", wxTE_PASSWORD);
  error_ = new wxStaticText(body, wxID_ANY, "");
  error_->SetName("error");
  error_->SetForegroundColour(Theme::err());
  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  auto* go = accent_button(body, "Сменить");
  btns->Add(go, 0, wxRIGHT, 8);
  btns->Add(new wxButton(body, wxID_CANCEL, "Отмена"));
  go->SetDefault();
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(intro, 0, wxALL, 16);
  root->Add(form, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);
  root->Add(error_, 0, wxALL, 16);
  root->Add(btns, 0, wxEXPAND | wxALL, 16);
  body->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(body, 1, wxEXPAND);
  SetSizer(outer);
  apply_dark(this);
  go->Bind(wxEVT_BUTTON, &ChangeMasterDialog::on_submit, this);
}

void ChangeMasterDialog::on_submit(wxCommandEvent&) {
  auto old_pw = std::string(old_->GetValue().utf8_string());
  auto new_pw = std::string(neu_->GetValue().utf8_string());
  error_->SetLabel("");
  if (new_pw != std::string(neu2_->GetValue().utf8_string())) {
    error_->SetLabel("Новые пароли не совпадают.");
    return;
  }
  int min_len = allow_short_ ? kMinPasswordLenRelaxed : kMinPasswordLen;
  if (static_cast<int>(new_pw.size()) < min_len) {
    error_->SetLabel(wxString::Format("Новый пароль не короче %d символов.", min_len));
    return;
  }
  if (allow_short_ && static_cast<int>(new_pw.size()) < kMinPasswordLen) {
    if (wxMessageBox(wxString::Format("Пароль короче %d символов — его проще подобрать.\n\nВы уверены?", kMinPasswordLen),
                     "Мастер-пароль", wxYES_NO | wxICON_WARNING, this) != wxYES) {
      return;
    }
  }
  if (!vault_.meta() || !vault_.unlock(old_pw, *vault_.meta())) {
    error_->SetLabel("Неверный текущий мастер-пароль.");
    old_->Clear();
    return;
  }
  try {
    vault_.create(new_pw, min_len);
  } catch (const VaultError& exc) {
    error_->SetLabel(wxString::FromUTF8(exc.what()));
    return;
  }
  ok = true;
  EndModal(wxID_OK);
}

}  // namespace fatty

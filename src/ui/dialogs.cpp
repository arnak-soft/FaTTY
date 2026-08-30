#include "ui/dialogs.hpp"

#include "core/lockout.hpp"
#include "core/util.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
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
  name_ = labeled_entry(body, grid, L"Имя", wxString::FromUTF8(server.name));
  host_ = labeled_entry(body, grid, L"Хост / IP", wxString::FromUTF8(server.host));
  port_ = labeled_entry(body, grid, L"Порт", wxString::FromUTF8(std::to_string(server.port ? server.port : 22)));
  user_ = labeled_entry(body, grid, L"Логин", wxString::FromUTF8(server.username));
  grid->Add(new wxStaticText(body, wxID_ANY, L"Пароль"), 0, wxALIGN_CENTER_VERTICAL);
  auto* pwrow = new wxBoxSizer(wxHORIZONTAL);
  password_ = new wxTextCtrl(body, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
  style_text(password_);
  pwrow->Add(password_, 1, wxEXPAND);
  show_pw_ = new wxCheckBox(body, wxID_ANY, L"показать");
  pwrow->Add(show_pw_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
  grid->Add(pwrow, 1, wxEXPAND);
  wxString note;
  if (is_new) {
    note = L"После сохранения пароль нельзя просмотреть — только заменить новым.";
  } else if (!stored_password_.empty()) {
    note = L"Пароль сохранён, просмотр недоступен. Оставьте поле пустым, чтобы не менять.";
  } else {
    note = L"Пароль не задан. После сохранения его нельзя будет просмотреть.";
  }
  grid->Add(new wxStaticText(body, wxID_ANY, L""), 0);
  auto* note_l = new wxStaticText(body, wxID_ANY, note);
  note_l->SetName(L"muted");
  note_l->SetForegroundColour(Theme::muted());
  grid->Add(note_l, 1, wxEXPAND);
  if (!stored_password_.empty()) {
    grid->Add(new wxStaticText(body, wxID_ANY, L""), 0);
    clear_pw_ = new wxCheckBox(body, wxID_ANY, L"Удалить сохранённый пароль");
    grid->Add(clear_pw_, 1);
  }
  grid->Add(new wxStaticText(body, wxID_ANY, L"SSH-ключ"), 0, wxALIGN_CENTER_VERTICAL);
  auto* keyrow = new wxBoxSizer(wxHORIZONTAL);
  key_ = new wxTextCtrl(body, wxID_ANY, wxString::FromUTF8(server.key_path));
  style_text(key_);
  auto* browse = make_button(body, L"Обзор…", BtnIcon::Folder);
  keyrow->Add(key_, 1, wxEXPAND);
  keyrow->Add(browse, 0, wxLEFT, 8);
  grid->Add(keyrow, 1, wxEXPAND);

  error_ = new wxStaticText(body, wxID_ANY, L"");
  error_->SetName(L"error");
  error_->SetForegroundColour(Theme::err());

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  auto* save = accent_button(body, L"Сохранить", BtnIcon::Save);
  auto* cancel = make_button(body, L"Отмена", BtnIcon::Cancel, wxID_CANCEL);
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
    wxFileDialog dlg(this, L"SSH private key", wxStandardPaths::Get().GetUserConfigDir() + L"/.ssh", L"",
                     L"All files|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
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
  error_->SetLabel(L"");
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
  if (name.empty()) { mark_error(name_, L"Укажите имя VPS."); return; }
  if (host.empty()) { mark_error(host_, L"Укажите хост или IP."); return; }
  if (user.empty()) { mark_error(user_, L"Укажите логин."); return; }
  int port = 22;
  if (!parse_int(std::string(port_->GetValue().utf8_string()), port) || port < 1 || port > 65535) {
    mark_error(port_, L"Порт должен быть числом 1–65535.");
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
    if (wxMessageBox(L"Пароль и ключ пустые. Подключаться через ssh-agent / ключи по умолчанию?", L"Без пароля и ключа",
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
  form->Add(new wxStaticText(body, wxID_ANY, L"Каталог приложения"), 0, wxALIGN_CENTER_VERTICAL);
  app_dir_ = new wxTextCtrl(body, wxID_ANY, wxString::FromUTF8(kDefaultAppDir));
  style_text(app_dir_);
  form->Add(app_dir_, 1, wxEXPAND);
  form->Add(new wxStaticText(body, wxID_ANY, L"Ветка git"), 0, wxALIGN_CENTER_VERTICAL);
  branch_ = new wxTextCtrl(body, wxID_ANY, wxString::FromUTF8(kDefaultBranch));
  style_text(branch_);
  form->Add(branch_, 1, wxEXPAND);
  form->Add(new wxStaticText(body, wxID_ANY, L"Процесс pm2"), 0, wxALIGN_CENTER_VERTICAL);
  pm2_ = new wxTextCtrl(body, wxID_ANY, wxString::FromUTF8(kDefaultPm2));
  style_text(pm2_);
  form->Add(pm2_, 1, wxEXPAND);

  auto* refresh = make_button(body, L"Обновить список", BtnIcon::Refresh);
  list_ = new wxPanel(body);
  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  auto* add = accent_button(body, L"Добавить выбранные", BtnIcon::Plus);
  btns->Add(add, 0, wxRIGHT, 8);
  btns->Add(make_button(body, L"Отмена", BtnIcon::Cancel, wxID_CANCEL));
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
    std::string extra = !p.comment.empty() ? p.comment
                                           : (p.command.size() > 80 ? p.command.substr(0, 77) + "…" : p.command);
    auto* cb = new wxCheckBox(list_, wxID_ANY, wxString::FromUTF8(p.name + "  —  " + extra));
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
    wxMessageBox(L"Ничего не выбрано.", L"Пресеты", wxOK | wxICON_WARNING, this);
    return;
  }
  accepted = true;
  EndModal(wxID_OK);
}

CommandDialog::CommandDialog(wxWindow* parent, const Command& command, const std::vector<Server>& servers,
                             const std::vector<Folder>& folders, const wxString& title)
    : PositionedDialog(parent, title, wxSize(640, 620)), command_(command), servers_(servers), folders_(folders) {
  auto* body = new wxPanel(this);
  auto* form = new wxFlexGridSizer(10, 2, 6, 8);
  form->AddGrowableCol(1);
  name_ = labeled_entry(body, form, L"Название", wxString::FromUTF8(command.name));
  comment_ = labeled_entry(body, form, L"Комментарий", wxString::FromUTF8(command.comment));
  comment_->SetHint(L"Необязательно — видно в списке");
  form->Add(new wxStaticText(body, wxID_ANY, L"VPS"), 0, wxALIGN_CENTER_VERTICAL);
  wxArrayString names;
  wxString current;
  for (const auto& s : servers) {
    names.Add(wxString::FromUTF8(s.name));
    if (s.id == command.server_id) current = wxString::FromUTF8(s.name);
  }
  server_ = new wxComboBox(body, wxID_ANY, current, wxDefaultPosition, wxDefaultSize, names, wxCB_READONLY);
  form->Add(server_, 1, wxEXPAND);
  form->Add(new wxStaticText(body, wxID_ANY, L"Группа"), 0, wxALIGN_CENTER_VERTICAL);
  folder_ = new wxComboBox(body, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize, wxArrayString(), wxCB_READONLY);
  form->Add(folder_, 1, wxEXPAND);
  fill_folders();
  working_dir_ = labeled_entry(body, form, L"Папка", wxString::FromUTF8(command.working_dir));
  working_dir_->SetHint(L"/var/www/app или относительный путь");
  form->Add(new wxStaticText(body, wxID_ANY, L""), 0);
  cd_before_ = new wxCheckBox(body, wxID_ANY, L"Переходить в папку перед выполнением");
  cd_before_->SetValue(command.cd_before_run);
  form->Add(cd_before_, 1);
  timeout_ = labeled_entry(body, form, L"Таймаут, с", wxString::FromUTF8(std::to_string(command.timeout_sec)));
  form->Add(new wxStaticText(body, wxID_ANY, L""), 0);
  login_ = new wxCheckBox(body, wxID_ANY, L"Login-shell (bash -lc) — подхватывает PATH из .bashrc");
  login_->SetValue(command.login_shell);
  form->Add(login_, 1);
  form->Add(new wxStaticText(body, wxID_ANY, L""), 0);
  confirm_ = new wxCheckBox(body, wxID_ANY, L"Предупреждать перед запуском");
  confirm_->SetValue(command.confirm_before_run);
  form->Add(confirm_, 1);
  form->Add(new wxStaticText(body, wxID_ANY, L"Пресет"), 0, wxALIGN_CENTER_VERTICAL);
  presets_ = all_presets();
  wxArrayString pname;
  for (const auto& p : presets_) pname.Add(wxString::FromUTF8(p.name));
  preset_ = new wxComboBox(body, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize, pname, wxCB_READONLY);
  form->Add(preset_, 1, wxEXPAND);

  text_ = new wxTextCtrl(body, wxID_ANY, wxString::FromUTF8(command.command), wxDefaultPosition,
                         wxSize(-1, FromDIP(160)), wxTE_MULTILINE | wxTE_WORDWRAP);
  style_text(text_, true);
  text_->SetMinSize(wxSize(-1, FromDIP(120)));

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  auto* save = accent_button(body, L"Сохранить", BtnIcon::Save);
  btns->Add(save, 0, wxRIGHT, 8);
  btns->Add(make_button(body, L"Отмена", BtnIcon::Cancel, wxID_CANCEL));

  save->SetDefault();
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(form, 0, wxEXPAND | wxALL, 12);
  root->Add(new wxStaticText(body, wxID_ANY, L"Команда"), 0, wxLEFT | wxRIGHT, 12);
  root->Add(text_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
  // Buttons stay at the bottom and never shrink away.
  root->Add(btns, 0, wxEXPAND | wxALL, 12);
  body->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(body, 1, wxEXPAND);
  SetSizerAndFit(outer);
  SetMinSize(FromDIP(wxSize(520, 460)));
  if (GetSize().GetHeight() < FromDIP(460)) {
    SetSize(GetSize().GetWidth(), FromDIP(560));
  }
  apply_dark(this);
  sync_cd_ui();
  server_->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { fill_folders(); });
  cd_before_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { sync_cd_ui(); });
  preset_->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
    auto chosen = std::string(preset_->GetValue().utf8_string());
    for (const auto& p : presets_) {
      if (p.name == chosen) {
        name_->SetValue(wxString::FromUTF8(p.name));
        comment_->SetValue(wxString::FromUTF8(p.comment));
        timeout_->SetValue(std::to_wstring(p.timeout_sec));
        login_->SetValue(p.login_shell);
        text_->SetValue(wxString::FromUTF8(p.command));
        if (!p.working_dir.empty()) {
          working_dir_->SetValue(wxString::FromUTF8(p.working_dir));
          cd_before_->SetValue(true);
        }
        sync_cd_ui();
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
  folder_->Append(L"Общее");
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

void CommandDialog::sync_cd_ui() {
  working_dir_->Enable(cd_before_->GetValue());
}

void CommandDialog::on_ok(wxCommandEvent&) {
  auto name = trim(std::string(name_->GetValue().utf8_string()));
  auto server_name = trim(std::string(server_->GetValue().utf8_string()));
  auto cmd = trim(std::string(text_->GetValue().utf8_string()));
  if (name.empty() || cmd.empty() || server_name.empty()) {
    wxMessageBox(L"Заполните название, VPS и команду.", L"Проверка", wxOK | wxICON_WARNING, this);
    return;
  }
  int timeout = 180;
  if (!parse_int(std::string(timeout_->GetValue().utf8_string()), timeout) || timeout < 1) {
    wxMessageBox(L"Таймаут должен быть положительным числом.", L"Проверка", wxOK | wxICON_WARNING, this);
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
  result.comment = trim(std::string(comment_->GetValue().utf8_string()));
  result.server_id = sid;
  result.command = cmd;
  result.timeout_sec = timeout;
  result.login_shell = login_->GetValue();
  result.confirm_before_run = confirm_->GetValue();
  result.cd_before_run = cd_before_->GetValue();
  result.working_dir = trim(std::string(working_dir_->GetValue().utf8_string()));
  int fsel = folder_->GetSelection();
  if (fsel >= 0 && fsel < static_cast<int>(folder_ids_.size())) {
    result.folder_id = folder_ids_[static_cast<std::size_t>(fsel)];
  } else {
    result.folder_id.clear();
  }
  accepted = true;
  EndModal(wxID_OK);
}

BundleDialog::BundleDialog(wxWindow* parent, const Bundle& bundle, const Config& config, const wxString& title)
    : PositionedDialog(parent, title, wxSize(760, 560)), bundle_(bundle), config_(config),
      selected_ids_(bundle.command_ids) {
  auto* body = new wxPanel(this);
  auto* form = new wxFlexGridSizer(2, 2, 6, 8);
  form->AddGrowableCol(1);
  name_ = labeled_entry(body, form, L"Название", wxString::FromUTF8(bundle.name));
  interval_ = labeled_entry(body, form, L"Пауза по умолчанию, с",
                            wxString::FromUTF8(std::to_string(bundle.interval_sec)));

  auto* lists = new wxBoxSizer(wxHORIZONTAL);
  auto* left = new wxBoxSizer(wxVERTICAL);
  left->Add(new wxStaticText(body, wxID_ANY, L"Доступные команды (все группы)"), 0, wxBOTTOM, 4);
  available_ = new wxListCtrl(body, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                              wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES);
  available_->AppendColumn(L"Группа", wxLIST_FORMAT_LEFT, FromDIP(120));
  available_->AppendColumn(L"Команда", wxLIST_FORMAT_LEFT, FromDIP(200));
  left->Add(available_, 1, wxEXPAND);

  auto* mid = new wxBoxSizer(wxVERTICAL);
  mid->AddStretchSpacer();
  auto* add = make_button(body, L"→", BtnIcon::None);
  auto* rem = make_button(body, L"←", BtnIcon::None);
  mid->Add(add, 0, wxBOTTOM, 8);
  mid->Add(rem, 0);
  mid->AddStretchSpacer();

  auto* right = new wxBoxSizer(wxVERTICAL);
  right->Add(new wxStaticText(body, wxID_ANY, L"Порядок запуска"), 0, wxBOTTOM, 4);
  selected_ = new wxListCtrl(body, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES);
  selected_->AppendColumn(L"#", wxLIST_FORMAT_LEFT, FromDIP(36));
  selected_->AppendColumn(L"Группа", wxLIST_FORMAT_LEFT, FromDIP(110));
  selected_->AppendColumn(L"Команда", wxLIST_FORMAT_LEFT, FromDIP(180));
  right->Add(selected_, 1, wxEXPAND);
  auto* order = new wxBoxSizer(wxHORIZONTAL);
  auto* up = make_button(body, L"Вверх", BtnIcon::ArrowUp);
  auto* down = make_button(body, L"Вниз", BtnIcon::ArrowDown);
  auto* dup = make_button(body, L"Копировать", BtnIcon::Copy);
  order->Add(up, 0, wxRIGHT, 8);
  order->Add(down, 0, wxRIGHT, 8);
  order->Add(dup);
  right->Add(order, 0, wxTOP, 8);

  lists->Add(left, 1, wxEXPAND);
  lists->Add(mid, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
  lists->Add(right, 1, wxEXPAND);

  auto* hint = new wxStaticText(
      body, wxID_ANY,
      L"Добавленная команда исчезает слева. Чтобы повторить шаг, выберите его справа и нажмите «Копировать».");
  hint->SetName(L"muted");
  hint->SetForegroundColour(Theme::muted());

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  auto* save = accent_button(body, L"Сохранить", BtnIcon::Save);
  btns->Add(save, 0, wxRIGHT, 8);
  btns->Add(make_button(body, L"Отмена", BtnIcon::Cancel, wxID_CANCEL));
  save->SetDefault();

  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(form, 0, wxEXPAND | wxALL, 12);
  root->Add(hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  root->Add(lists, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);
  root->Add(btns, 0, wxEXPAND | wxALL, 12);
  body->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(body, 1, wxEXPAND);
  SetSizerAndFit(outer);
  SetMinSize(FromDIP(wxSize(640, 420)));
  apply_dark(this);
  rebuild_lists();

  add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { add_selected(); });
  rem->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { remove_selected(); });
  up->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { move_selected(-1); });
  down->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { move_selected(1); });
  dup->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { duplicate_selected(); });
  available_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { add_selected(); });
  selected_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { remove_selected(); });
  save->Bind(wxEVT_BUTTON, &BundleDialog::on_ok, this);
}

std::string BundleDialog::folder_label(const Command& cmd) const {
  if (cmd.folder_id.empty()) return "Общее";
  if (auto* f = config_.folder_by_id(cmd.folder_id)) return f->name;
  return "Общее";
}

void BundleDialog::rebuild_lists() {
  available_ids_.clear();
  for (const auto& c : config_.commands_for(bundle_.server_id)) {
    if (std::find(selected_ids_.begin(), selected_ids_.end(), c.id) == selected_ids_.end()) {
      available_ids_.push_back(c.id);
    }
  }
  available_->DeleteAllItems();
  for (std::size_t i = 0; i < available_ids_.size(); ++i) {
    auto* c = config_.command_by_id(available_ids_[i]);
    if (!c) continue;
    long row = available_->InsertItem(static_cast<long>(i), wxString::FromUTF8(folder_label(*c)));
    available_->SetItem(row, 1, wxString::FromUTF8(c->name));
  }
  selected_->DeleteAllItems();
  std::vector<std::string> kept;
  for (std::size_t i = 0; i < selected_ids_.size(); ++i) {
    auto* c = config_.command_by_id(selected_ids_[i]);
    if (!c) continue;
    kept.push_back(c->id);
    long row = selected_->InsertItem(selected_->GetItemCount(),
                                     wxString::FromUTF8(std::to_string(kept.size())));
    selected_->SetItem(row, 1, wxString::FromUTF8(folder_label(*c)));
    selected_->SetItem(row, 2, wxString::FromUTF8(c->name));
  }
  selected_ids_ = std::move(kept);
}

void BundleDialog::add_selected() {
  long i = available_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i < 0 || i >= static_cast<long>(available_ids_.size())) return;
  selected_ids_.push_back(available_ids_[static_cast<std::size_t>(i)]);
  rebuild_lists();
  if (selected_->GetItemCount() > 0) {
    long last = selected_->GetItemCount() - 1;
    selected_->SetItemState(last, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                            wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
  }
}

void BundleDialog::remove_selected() {
  long i = selected_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i < 0 || i >= static_cast<long>(selected_ids_.size())) return;
  selected_ids_.erase(selected_ids_.begin() + i);
  rebuild_lists();
}

void BundleDialog::duplicate_selected() {
  long i = selected_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i < 0 || i >= static_cast<long>(selected_ids_.size())) return;
  auto id = selected_ids_[static_cast<std::size_t>(i)];
  selected_ids_.insert(selected_ids_.begin() + static_cast<std::size_t>(i) + 1, id);
  rebuild_lists();
  long next = i + 1;
  if (next < selected_->GetItemCount()) {
    selected_->SetItemState(next, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                            wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
  }
}

void BundleDialog::move_selected(int delta) {
  long i = selected_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i < 0) return;
  int n = static_cast<int>(selected_ids_.size());
  int to = static_cast<int>(i) + delta;
  if (to < 0 || to >= n) return;
  std::swap(selected_ids_[static_cast<std::size_t>(i)], selected_ids_[static_cast<std::size_t>(to)]);
  rebuild_lists();
  selected_->SetItemState(to, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                          wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
}

void BundleDialog::on_ok(wxCommandEvent&) {
  auto name = trim(std::string(name_->GetValue().utf8_string()));
  if (name.empty()) {
    wxMessageBox(L"Укажите название связки.", L"Связка", wxOK | wxICON_WARNING, this);
    return;
  }
  if (selected_ids_.empty()) {
    wxMessageBox(L"Добавьте хотя бы одну команду.", L"Связка", wxOK | wxICON_WARNING, this);
    return;
  }
  int interval = bundle_.interval_sec;
  if (!parse_int(std::string(interval_->GetValue().utf8_string()), interval) || interval < 0) {
    wxMessageBox(L"Пауза — целое число секунд (0 и больше).", L"Связка", wxOK | wxICON_WARNING, this);
    return;
  }
  result = bundle_;
  result.name = name;
  result.interval_sec = clamp_int(interval, 0, 3600);
  result.command_ids = selected_ids_;
  accepted = true;
  EndModal(wxID_OK);
}

MasterPasswordDialog::MasterPasswordDialog(wxWindow* parent, Config& config, SessionVault& vault)
    : PositionedDialog(parent, L"Мастер-пароль", wxSize(480, 280)),
      config_(config),
      vault_(vault),
      setup_(!config.has_vault) {
  auto* body = new wxPanel(this);
  auto* intro = new wxStaticText(
      body, wxID_ANY,
      setup_ ? L"Задайте мастер-пароль. Им шифруются пароли VPS.\nБез него конфиг нельзя расшифровать — восстановить фразу нельзя."
             : L"Введите мастер-пароль, чтобы открыть сохранённые пароли VPS.");
  intro->Wrap(420);
  auto* form = new wxFlexGridSizer(3, 2, 8, 8);
  form->AddGrowableCol(1);
  form->Add(new wxStaticText(body, wxID_ANY, L"Мастер-пароль"), 0, wxALIGN_CENTER_VERTICAL);
  pw_ = new wxTextCtrl(body, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
  style_text(pw_);
  form->Add(pw_, 1, wxEXPAND);
  if (setup_) {
    form->Add(new wxStaticText(body, wxID_ANY, L"Ещё раз"), 0, wxALIGN_CENTER_VERTICAL);
    pw2_ = new wxTextCtrl(body, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    style_text(pw2_);
    form->Add(pw2_, 1, wxEXPAND);
    auto* hint = new wxStaticText(body, wxID_ANY, wxString::Format(L"Не короче %d символов.", kMinPasswordLen));
    hint->SetName(L"muted");
    hint->SetForegroundColour(Theme::muted());
    form->Add(new wxStaticText(body, wxID_ANY, L""), 0);
    form->Add(hint, 1);
  }
  error_ = new wxStaticText(body, wxID_ANY, L"");
  error_->SetName(L"error");
  error_->SetForegroundColour(Theme::err());
  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  continue_btn_ = accent_button(body, L"Продолжить", BtnIcon::Check);
  btns->Add(continue_btn_, 0, wxRIGHT, 8);
  btns->Add(make_button(body, L"Выход", BtnIcon::Cancel, wxID_CANCEL));
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
  error_->SetLabel(L"");
  pw_->Enable(true);
  continue_btn_->Enable(true);
}

void MasterPasswordDialog::on_submit(wxCommandEvent&) {
  auto password = std::string(pw_->GetValue().utf8_string());
  error_->SetLabel(L"");
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
        error_->SetLabel(L"Пароли не совпадают.");
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
    error_->SetLabel(L"Не удалось открыть хранилище. Проверьте пароль.");
    return;
  }
  ok = true;
  pw_->Clear();
  if (pw2_) pw2_->Clear();
  EndModal(wxID_OK);
}

ChangeMasterDialog::ChangeMasterDialog(wxWindow* parent, SessionVault& vault, bool allow_short)
    : PositionedDialog(parent, L"Сменить мастер-пароль", wxSize(460, 300)), vault_(vault), allow_short_(allow_short) {
  auto* body = new wxPanel(this);
  auto* intro = new wxStaticText(body, wxID_ANY, L"Текущие пароли VPS будут перешифрованы новым мастер-паролем.");
  intro->Wrap(400);
  auto* form = new wxFlexGridSizer(3, 2, 8, 8);
  form->AddGrowableCol(1);
  old_ = labeled_entry(body, form, L"Текущий", L"", wxTE_PASSWORD);
  neu_ = labeled_entry(body, form, L"Новый", L"", wxTE_PASSWORD);
  neu2_ = labeled_entry(body, form, L"Новый ещё раз", L"", wxTE_PASSWORD);
  error_ = new wxStaticText(body, wxID_ANY, L"");
  error_->SetName(L"error");
  error_->SetForegroundColour(Theme::err());
  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  auto* go = accent_button(body, L"Сменить", BtnIcon::Key);
  btns->Add(go, 0, wxRIGHT, 8);
  btns->Add(make_button(body, L"Отмена", BtnIcon::Cancel, wxID_CANCEL));
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
  error_->SetLabel(L"");
  if (new_pw != std::string(neu2_->GetValue().utf8_string())) {
    error_->SetLabel(L"Новые пароли не совпадают.");
    return;
  }
  int min_len = allow_short_ ? kMinPasswordLenRelaxed : kMinPasswordLen;
  if (static_cast<int>(new_pw.size()) < min_len) {
    error_->SetLabel(wxString::Format(L"Новый пароль не короче %d символов.", min_len));
    return;
  }
  if (allow_short_ && static_cast<int>(new_pw.size()) < kMinPasswordLen) {
    if (wxMessageBox(wxString::Format(L"Пароль короче %d символов — его проще подобрать.\n\nВы уверены?", kMinPasswordLen),
                     L"Мастер-пароль", wxYES_NO | wxICON_WARNING, this) != wxYES) {
      return;
    }
  }
  if (!vault_.meta() || !vault_.unlock(old_pw, *vault_.meta())) {
    error_->SetLabel(L"Неверный текущий мастер-пароль.");
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

UpdateAvailableDialog::UpdateAvailableDialog(wxWindow* parent, const std::string& current, const std::string& latest)
    : wxDialog(parent, wxID_ANY, L"Обновления", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE) {
  bind_escape_close(this);

  auto* body = new wxPanel(this);
  auto latest_s = wxString::FromUTF8(latest.empty() ? "?" : latest);
  auto current_s = wxString::FromUTF8(current.empty() ? "?" : current);
  auto* msg = new wxStaticText(
      body, wxID_ANY,
      wxString::Format(L"Доступна версия %s (у вас %s).\nСкачать установщик?", latest_s, current_s));
  msg->Wrap(FromDIP(400));
  skip_ = new wxCheckBox(body, wxID_ANY, L"Больше не напоминать об этой версии");
  skip_->SetValue(true);

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  auto* yes = accent_button(body, L"Да", BtnIcon::Check);
  auto* no = make_button(body, L"Нет", BtnIcon::Cancel);
  yes->SetDefault();
  btns->Add(yes, 0, wxRIGHT, 8);
  btns->Add(no);

  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(msg, 0, wxEXPAND | wxALL, 16);
  root->Add(skip_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);
  root->Add(btns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);
  body->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(body, 1, wxEXPAND);
  SetSizer(outer);
  apply_dark(this);
  outer->SetSizeHints(this);
  if (parent) CentreOnParent();

  yes->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_YES); });
  no->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_NO); });
}

bool UpdateAvailableDialog::dont_remind() const { return skip_ && skip_->GetValue(); }

}  // namespace fatty

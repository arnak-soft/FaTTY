#include "ui/settings_dialog.hpp"

#include "core/config_io.hpp"
#include "core/paths.hpp"
#include "core/util.hpp"
#include "putty/putty.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace fatty {

SettingsDialog::SettingsDialog(wxWindow* parent, Config& config, SessionVault& vault, std::function<void()> on_apply,
                               std::function<void()> on_change_master, std::function<void()> on_check_updates,
                               std::function<void()> on_import_done)
    : PositionedDialog(parent, L"Настройки", wxSize(560, 500)),
      config_(config),
      vault_(vault),
      on_apply_(std::move(on_apply)),
      on_change_master_(std::move(on_change_master)),
      on_check_updates_(std::move(on_check_updates)),
      on_import_done_(std::move(on_import_done)) {
  auto* nb = new RoundedNotebook(this);
  auto& st = config.settings;

  auto* general = new wxPanel(nb);
  general->SetName(L"card-page");
  auto* gsz = new wxBoxSizer(wxVERTICAL);
  confirm_ = new wxCheckBox(general, wxID_ANY, L"Спрашивать подтверждение перед запуском");
  confirm_->SetValue(st.confirm_before_run);
  clear_output_ = new wxCheckBox(general, wxID_ANY, L"Очищать панель вывода перед новым запуском");
  clear_output_->SetValue(st.clear_output_before_run);
  updates_ = new wxCheckBox(general, wxID_ANY, L"Проверять обновления при запуске (не чаще раза в сутки)");
  updates_->SetValue(st.check_updates_on_start);
  auto* theme_row = new wxBoxSizer(wxHORIZONTAL);
  theme_row->Add(new wxStaticText(general, wxID_ANY, L"Тема"), 0, wxALIGN_CENTER_VERTICAL);
  wxArrayString themes;
  themes.Add(L"Тёмная");
  themes.Add(L"Светлая");
  theme_ = new wxChoice(general, wxID_ANY, wxDefaultPosition, wxDefaultSize, themes);
  theme_->SetSelection(st.theme == "light" ? 1 : 0);
  theme_row->Add(theme_, 0, wxLEFT, 8);
  auto* trow = new wxBoxSizer(wxHORIZONTAL);
  trow->Add(new wxStaticText(general, wxID_ANY, L"Таймаут новых команд, с"), 0, wxALIGN_CENTER_VERTICAL);
  timeout_ = new wxTextCtrl(general, wxID_ANY, std::to_wstring(st.default_command_timeout));
  trow->Add(timeout_, 0, wxLEFT, 8);
  auto* jrow = new wxBoxSizer(wxHORIZONTAL);
  jrow->Add(new wxStaticText(general, wxID_ANY, L"Максимум записей журнала"), 0, wxALIGN_CENTER_VERTICAL);
  journal_ = new wxTextCtrl(general, wxID_ANY, std::to_wstring(st.journal_max_entries));
  jrow->Add(journal_, 0, wxLEFT, 8);
  auto* check_now = make_button(general, L"Проверить сейчас…");
  gsz->Add(confirm_, 0, wxALL, 8);
  gsz->Add(clear_output_, 0, wxALL, 8);
  gsz->Add(theme_row, 0, wxALL, 8);
  gsz->Add(trow, 0, wxALL, 8);
  gsz->Add(updates_, 0, wxALL, 8);
  gsz->Add(check_now, 0, wxALL, 8);
  gsz->Add(jrow, 0, wxALL, 8);
  general->SetSizer(gsz);
  nb->AddPage(general, L"Общие");

  auto* programs = new wxPanel(nb);
  programs->SetName(L"card-page");
  auto* psz = new wxFlexGridSizer(2, 3, 8, 8);
  psz->AddGrowableCol(1);
  psz->Add(new wxStaticText(programs, wxID_ANY, L"PuTTY"), 0, wxALIGN_CENTER_VERTICAL);
  putty_ = new wxTextCtrl(programs, wxID_ANY, wxString::FromUTF8(st.putty_path));
  psz->Add(putty_, 1, wxEXPAND);
  auto* pbrowse = make_button(programs, L"Обзор…");
  psz->Add(pbrowse);
  psz->Add(new wxStaticText(programs, wxID_ANY, L"ssh.exe"), 0, wxALIGN_CENTER_VERTICAL);
  ssh_ = new wxTextCtrl(programs, wxID_ANY, wxString::FromUTF8(st.ssh_path));
  psz->Add(ssh_, 1, wxEXPAND);
  auto* sbrowse = make_button(programs, L"Обзор…");
  psz->Add(sbrowse);
  auto* proot = new wxBoxSizer(wxVERTICAL);
  proot->Add(psz, 0, wxEXPAND | wxALL, 12);
  programs->SetSizer(proot);
  nb->AddPage(programs, L"Программы");

  auto* data = new wxPanel(nb);
  data->SetName(L"card-page");
  auto* dsz = new wxBoxSizer(wxVERTICAL);
  auto* open_dir = make_button(data, L"Открыть папку конфига");
  export_secrets_ = new wxCheckBox(data, wxID_ANY, L"Экспорт: включить пароли");
  export_settings_ = new wxCheckBox(data, wxID_ANY, L"Экспорт: включить настройки");
  export_settings_->SetValue(true);
  import_settings_ = new wxCheckBox(data, wxID_ANY, L"Импорт: применять настройки");
  import_settings_->SetValue(true);
  auto* exp = make_button(data, L"Экспорт…");
  auto* imp = make_button(data, L"Импорт…");
  dsz->Add(open_dir, 0, wxALL, 8);
  dsz->Add(export_secrets_, 0, wxALL, 8);
  dsz->Add(export_settings_, 0, wxALL, 8);
  dsz->Add(exp, 0, wxALL, 8);
  dsz->Add(import_settings_, 0, wxALL, 8);
  dsz->Add(imp, 0, wxALL, 8);
  data->SetSizer(dsz);
  nb->AddPage(data, L"Данные");

  auto* sec = new wxPanel(nb);
  sec->SetName(L"card-page");
  auto* ssz = new wxBoxSizer(wxVERTICAL);
  short_pw_ = new wxCheckBox(sec, wxID_ANY, L"Разрешить короткий мастер-пароль (от 4 символов)");
  short_pw_->SetValue(st.allow_short_master_password);
  auto* arow = new wxBoxSizer(wxHORIZONTAL);
  arow->Add(new wxStaticText(sec, wxID_ANY, L"Попыток до блокировки"), 0, wxALIGN_CENTER_VERTICAL);
  lockout_attempts_ = new wxTextCtrl(sec, wxID_ANY, std::to_wstring(st.master_password_max_attempts));
  arow->Add(lockout_attempts_, 0, wxLEFT, 8);
  auto* mrow = new wxBoxSizer(wxHORIZONTAL);
  mrow->Add(new wxStaticText(sec, wxID_ANY, L"Минут блокировки"), 0, wxALIGN_CENTER_VERTICAL);
  lockout_minutes_ = new wxTextCtrl(sec, wxID_ANY, std::to_wstring(st.master_password_lockout_minutes));
  mrow->Add(lockout_minutes_, 0, wxLEFT, 8);
  auto* chpw = make_button(sec, L"Сменить мастер-пароль…");
  ssz->Add(short_pw_, 0, wxALL, 8);
  ssz->Add(arow, 0, wxALL, 8);
  ssz->Add(mrow, 0, wxALL, 8);
  ssz->Add(chpw, 0, wxALL, 8);
  sec->SetSizer(ssz);
  nb->AddPage(sec, L"Безопасность");

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  auto* save = accent_button(this, L"Сохранить");
  btns->Add(save, 0, wxRIGHT, 8);
  btns->Add(make_button(this, L"Отмена", wxID_CANCEL));
  save->SetDefault();

  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(nb, 1, wxEXPAND | wxALL, 12);
  root->Add(btns, 0, wxEXPAND | wxALL, 12);
  SetSizer(root);
  apply_dark(this);

  check_now->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (on_check_updates_) on_check_updates_();
  });
  pbrowse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxFileDialog dlg(this, L"putty.exe", L"", L"putty.exe", L"PuTTY|putty.exe|EXE|*.exe", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) putty_->SetValue(dlg.GetPath());
  });
  sbrowse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxFileDialog dlg(this, L"ssh.exe", L"", L"ssh.exe", L"ssh|ssh.exe|EXE|*.exe", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) ssh_->SetValue(dlg.GetPath());
  });
  open_dir->Bind(wxEVT_BUTTON, [](wxCommandEvent&) { open_directory(app_dir()); });
  exp->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxFileDialog dlg(this, L"Экспорт FaTTY", L"", L"fatty-backup.json", L"JSON|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;
    try {
      write_export(std::filesystem::path(dlg.GetPath().utf8_string()), config_, export_secrets_->GetValue(),
                   export_settings_->GetValue());
      wxMessageBox(L"Сохранено.", L"Экспорт", wxOK | wxICON_INFORMATION, this);
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), L"Экспорт", wxOK | wxICON_ERROR, this);
    }
  });
  imp->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    int choice = wxMessageBox(L"Да — добавить к текущим\nНет — заменить все\nОтмена", L"Импорт",
                              wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
    if (choice == wxCANCEL) return;
    std::string mode = choice == wxYES ? "merge" : "replace";
    wxFileDialog dlg(this, L"Импорт FaTTY", L"", L"", L"JSON|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;
    try {
      auto data = read_export(std::filesystem::path(dlg.GetPath().utf8_string()));
      auto result = import_into_config(config_, data, mode, import_settings_->GetValue());
      if (on_apply_) on_apply_();
      if (on_import_done_) on_import_done_();
      wxMessageBox(wxString::FromUTF8(format_import_summary(result, mode)), L"Импорт", wxOK | wxICON_INFORMATION, this);
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), L"Импорт", wxOK | wxICON_ERROR, this);
    }
  });
  chpw->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (on_change_master_) on_change_master_();
  });
  save->Bind(wxEVT_BUTTON, &SettingsDialog::on_save, this);
}

void SettingsDialog::on_save(wxCommandEvent&) {
  int timeout = config_.settings.default_command_timeout;
  int journal = config_.settings.journal_max_entries;
  int attempts = config_.settings.master_password_max_attempts;
  int minutes = config_.settings.master_password_lockout_minutes;
  parse_int(std::string(timeout_->GetValue().utf8_string()), timeout);
  parse_int(std::string(journal_->GetValue().utf8_string()), journal);
  parse_int(std::string(lockout_attempts_->GetValue().utf8_string()), attempts);
  parse_int(std::string(lockout_minutes_->GetValue().utf8_string()), minutes);
  config_.settings.confirm_before_run = confirm_->GetValue();
  config_.settings.check_updates_on_start = updates_->GetValue();
  config_.settings.clear_output_before_run = clear_output_->GetValue();
  const std::string old_theme = config_.settings.theme;
  config_.settings.theme = theme_->GetSelection() == 1 ? "light" : "dark";
  if (config_.settings.theme != old_theme) {
    wxMessageBox(L"Тема применена. Системные элементы (меню, заголовки таблиц, скроллбары)\n"
                 L"полностью переключатся после перезапуска FaTTY.",
                 L"Тема", wxOK | wxICON_INFORMATION, this);
  }
  config_.settings.default_command_timeout = clamp_int(timeout, 1, 86400);
  config_.settings.journal_max_entries = clamp_int(journal, 100, 50000);
  config_.settings.putty_path = std::string(putty_->GetValue().utf8_string());
  config_.settings.ssh_path = std::string(ssh_->GetValue().utf8_string());
  config_.settings.allow_short_master_password = short_pw_->GetValue();
  config_.settings.master_password_max_attempts = clamp_int(attempts, 0, 100);
  config_.settings.master_password_lockout_minutes = clamp_int(minutes, 1, 24 * 60);
  if (on_apply_) on_apply_();
  EndModal(wxID_OK);
}

}  // namespace fatty

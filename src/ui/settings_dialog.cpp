#include "ui/settings_dialog.hpp"

#include "core/backup.hpp"
#include "core/config_io.hpp"
#include "core/paths.hpp"
#include "core/util.hpp"
#include "ui/chrome.hpp"
#include "ui/striped_list.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace fatty {
namespace {

class ExtraProgramDialog : public wxDialog {
 public:
  ExtraProgram extra;
  bool accepted = false;

  ExtraProgramDialog(wxWindow* parent, ExtraProgram program, AppSettings* settings = nullptr,
                     std::function<void()> persist = {})
      : wxDialog(parent, wxID_ANY, L"Программа", wxDefaultPosition, wxSize(520, 360)), extra(std::move(program)) {
    const bool had_geometry = settings && settings->dialog_geometry.count("extra_program");
    setup_frame_geometry(this, settings, "extra_program", true, std::move(persist));
    if (!had_geometry) CentreOnParent();
    auto* body = new wxPanel(this);
    auto* grid = new wxFlexGridSizer(3, 2, 8, 8);
    grid->AddGrowableCol(1);
    grid->Add(new wxStaticText(body, wxID_ANY, L"Название"), 0, wxALIGN_CENTER_VERTICAL);
    name_ = new wxTextCtrl(body, wxID_ANY, wxString::FromUTF8(extra.name));
    style_text(name_);
    grid->Add(name_, 1, wxEXPAND);
    grid->Add(new wxStaticText(body, wxID_ANY, L"Файл"), 0, wxALIGN_CENTER_VERTICAL);
    auto* path_row = new wxBoxSizer(wxHORIZONTAL);
    path_ = new wxTextCtrl(body, wxID_ANY, wxString::FromUTF8(extra.path));
    style_text(path_);
    auto* browse = make_button(body, L"Обзор…", BtnIcon::Folder);
    path_row->Add(path_, 1, wxEXPAND);
    path_row->Add(browse, 0, wxLEFT, 8);
    grid->Add(path_row, 1, wxEXPAND);
    grid->Add(new wxStaticText(body, wxID_ANY, L"Аргументы"), 0, wxALIGN_CENTER_VERTICAL);
    args_ = new wxTextCtrl(body, wxID_ANY, wxString::FromUTF8(extra.args));
    style_text(args_);
    grid->Add(args_, 1, wxEXPAND);

    auto* hint = new wxStaticText(
        body, wxID_ANY,
        L"Подстановки в аргументах: {host} {port} {user} {password} {key} {ppk} {name} {sftp_url} {ssh_target}.\n"
        L"Пути с пробелами заключайте в кавычки, например \"{ppk}\".");
    hint->SetName(L"muted");
    hint->SetForegroundColour(Theme::muted());
    hint->Wrap(FromDIP(460));

    error_ = new wxStaticText(body, wxID_ANY, L"");
    error_->SetName(L"error");
    error_->SetForegroundColour(Theme::err());

    auto* btns = new wxBoxSizer(wxHORIZONTAL);
    btns->AddStretchSpacer();
    auto* save = accent_button(body, L"Сохранить", BtnIcon::Save);
    btns->Add(save, 0, wxRIGHT, 8);
    btns->Add(make_button(body, L"Отмена", BtnIcon::Cancel, wxID_CANCEL));
    save->SetDefault();

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(grid, 0, wxEXPAND | wxALL, 12);
    root->Add(hint, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    root->Add(error_, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    root->Add(btns, 0, wxEXPAND | wxALL, 12);
    body->SetSizer(root);
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(body, 1, wxEXPAND);
    SetSizer(outer);
    apply_dark(this);
    bind_escape_close(this);

    browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      wxFileDialog dlg(this, L"Программа", L"", L"", L"EXE|*.exe|Все|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
      if (dlg.ShowModal() != wxID_OK) return;
      path_->SetValue(dlg.GetPath());
      if (name_->GetValue().IsEmpty()) {
        wxFileName fn(dlg.GetPath());
        name_->SetValue(fn.GetName());
      }
    });
    save->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      extra.name = trim(std::string(name_->GetValue().utf8_string()));
      extra.path = trim(std::string(path_->GetValue().utf8_string()));
      extra.args = std::string(args_->GetValue().utf8_string());
      if (extra.name.empty() || extra.path.empty()) {
        error_->SetLabel(L"Укажите название и путь к файлу.");
        return;
      }
      if (extra.id.empty()) extra.id = ExtraProgram::make_new().id;
      accepted = true;
      EndModal(wxID_OK);
    });
  }

 private:
  wxTextCtrl* name_{};
  wxTextCtrl* path_{};
  wxTextCtrl* args_{};
  wxStaticText* error_{};
};

constexpr int kHealthIntervalChoices[] = {300, 900, 3600, 6 * 3600, 12 * 3600, 86400, 3 * 86400, 7 * 86400};

}  // namespace

SettingsDialog::SettingsDialog(wxWindow* parent, Config& config, SessionVault& vault, std::function<void()> on_apply,
                               std::function<void()> on_change_master, std::function<void()> on_check_updates,
                               std::function<void()> on_import_done)
    : PositionedDialog(parent, L"Настройки", wxSize(620, 680)),
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
  confirm_ = new wxCheckBox(general, wxID_ANY, L"Спрашивать подтверждение перед разовой командой");
  confirm_->SetValue(st.confirm_before_run);
  clear_output_ = new wxCheckBox(general, wxID_ANY, L"Очищать панель вывода перед новым запуском");
  clear_output_->SetValue(st.clear_output_before_run);
  automation_to_shell_ =
      new wxCheckBox(general, wxID_ANY,
                     L"После подключения Shell направлять вывод команд туда (постоянно)");
  automation_to_shell_->SetValue(st.automation_to_shell_when_connected);
  shell_follow_cwd_ =
      new wxCheckBox(general, wxID_ANY, L"После команд переходить в Shell в ту же папку");
  shell_follow_cwd_->SetValue(st.shell_follow_command_cwd);
  shell_follow_cwd_->SetToolTip(
      L"Если команда или связка сменила каталог, активный Shell делает то же самое. "
      L"Приглашение появляется сразу под выводом, как после обычного ввода.");
  advance_command_ = new wxCheckBox(general, wxID_ANY, L"Переходить к следующей команде после запуска (F5 / двойной клик)");
  advance_command_->SetValue(st.advance_command_after_run);
  show_folder_col_ = new wxCheckBox(general, wxID_ANY, L"Показывать столбец «Папка» (рабочий каталог) в списке команд");
  show_folder_col_->SetValue(st.show_command_folder_column);
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
  auto* check_now = make_button(general, L"Проверить сейчас…", BtnIcon::Refresh);
  gsz->Add(confirm_, 0, wxALL, 8);
  gsz->Add(clear_output_, 0, wxALL, 8);
  gsz->Add(automation_to_shell_, 0, wxALL, 8);
  gsz->Add(shell_follow_cwd_, 0, wxALL, 8);
  gsz->Add(advance_command_, 0, wxALL, 8);
  gsz->Add(show_folder_col_, 0, wxALL, 8);
  gsz->Add(theme_row, 0, wxALL, 8);
  gsz->Add(trow, 0, wxALL, 8);
  gsz->Add(updates_, 0, wxALL, 8);
  gsz->Add(check_now, 0, wxALL, 8);
  gsz->Add(jrow, 0, wxALL, 8);
  general->SetSizer(gsz);
  nb->AddPage(general, L"Общие");

  auto* health_page = new wxScrolledWindow(nb, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
  health_page->SetName(L"card-page");
  health_page->SetScrollRate(0, 16);
  auto* hsz = new wxBoxSizer(wxVERTICAL);
  health_auto_ = new wxCheckBox(health_page, wxID_ANY, L"Автоматически опрашивать VPS по расписанию");
  health_auto_->SetValue(st.health_auto);
  auto* health_hint = new wxStaticText(
      health_page, wxID_ANY,
      L"По умолчанию выключено: окно «Состояние VPS» показывает последний замер, "
      L"обновить можно вручную. Автоопрос — не чаще выбранного интервала, по одному хосту за раз.");
  health_hint->SetName(L"muted");
  health_hint->SetForegroundColour(Theme::muted());
  health_hint->Wrap(FromDIP(520));
  auto* irow = new wxBoxSizer(wxHORIZONTAL);
  irow->Add(new wxStaticText(health_page, wxID_ANY, L"Интервал"), 0, wxALIGN_CENTER_VERTICAL);
  wxArrayString intervals;
  intervals.Add(L"5 минут");
  intervals.Add(L"15 минут");
  intervals.Add(L"1 час");
  intervals.Add(L"6 часов");
  intervals.Add(L"12 часов");
  intervals.Add(L"1 день");
  intervals.Add(L"3 дня");
  intervals.Add(L"7 дней");
  intervals.Add(L"свой…");
  health_interval_ = new wxChoice(health_page, wxID_ANY, wxDefaultPosition, wxDefaultSize, intervals);
  int interval_sel = 8;
  for (int i = 0; i < 8; ++i) {
    if (kHealthIntervalChoices[i] == st.health_interval_sec) {
      interval_sel = i;
      break;
    }
  }
  health_interval_->SetSelection(interval_sel);
  irow->Add(health_interval_, 0, wxLEFT, 8);
  irow->Add(new wxStaticText(health_page, wxID_ANY, L"секунд"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
  health_interval_sec_ = new wxTextCtrl(health_page, wxID_ANY, std::to_wstring(st.health_interval_sec),
                                        wxDefaultPosition, FromDIP(wxSize(80, -1)));
  irow->Add(health_interval_sec_, 0, wxLEFT, 8);
  auto* trow_h = new wxBoxSizer(wxHORIZONTAL);
  trow_h->Add(new wxStaticText(health_page, wxID_ANY, L"Таймаут SSH, с"), 0, wxALIGN_CENTER_VERTICAL);
  health_timeout_ = new wxTextCtrl(health_page, wxID_ANY, std::to_wstring(st.health_timeout_sec), wxDefaultPosition,
                                   FromDIP(wxSize(64, -1)));
  trow_h->Add(health_timeout_, 0, wxLEFT, 8);
  auto* metrics_label = new wxStaticText(health_page, wxID_ANY, L"Что собирать и показывать");
  metrics_label->SetName(L"section");
  health_cpu_ = new wxCheckBox(health_page, wxID_ANY, L"CPU");
  health_cpu_->SetValue(st.health_show_cpu);
  health_ram_ = new wxCheckBox(health_page, wxID_ANY, L"RAM");
  health_ram_->SetValue(st.health_show_ram);
  health_disk_ = new wxCheckBox(health_page, wxID_ANY, L"Диск");
  health_disk_->SetValue(st.health_show_disk);
  health_load_ = new wxCheckBox(health_page, wxID_ANY, L"Нагрузка (load average)");
  health_load_->SetValue(st.health_show_load);
  health_docker_disks_ = new wxCheckBox(health_page, wxID_ANY, L"Тома Docker overlay / snap");
  health_docker_disks_->SetValue(st.health_show_docker_disks);
  health_docker_disks_->SetToolTip(
      L"Слои контейнеров на том же диске, что и /. Снятая галочка прячет их с графика. "
      L"После включения нажмите «Обновить» в окне Состояние.");
  auto* mrow = new wxBoxSizer(wxHORIZONTAL);
  mrow->Add(health_cpu_, 0, wxRIGHT, 12);
  mrow->Add(health_ram_, 0, wxRIGHT, 12);
  mrow->Add(health_disk_, 0, wxRIGHT, 12);
  mrow->Add(health_load_);
  auto* color_label = new wxStaticText(health_page, wxID_ANY, L"Цвета шкал: жёлтый с … %, красный с … %");
  color_label->SetName(L"section");
  auto* color_hint = new wxStaticText(
      health_page, wxID_ANY, L"Это только окраска диаграмм, не уведомления. Можно подогнать под свои серверы.");
  color_hint->SetName(L"muted");
  color_hint->SetForegroundColour(Theme::muted());
  color_hint->Wrap(FromDIP(520));
  auto thresh_row = [&](const wxString& name, int warn, int crit, wxTextCtrl** warn_ctrl, wxTextCtrl** crit_ctrl) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(health_page, wxID_ANY, name), 0, wxALIGN_CENTER_VERTICAL);
    *warn_ctrl = new wxTextCtrl(health_page, wxID_ANY, std::to_wstring(warn), wxDefaultPosition, FromDIP(wxSize(48, -1)));
    *crit_ctrl = new wxTextCtrl(health_page, wxID_ANY, std::to_wstring(crit), wxDefaultPosition, FromDIP(wxSize(48, -1)));
    row->Add(*warn_ctrl, 0, wxLEFT, 8);
    row->Add(new wxStaticText(health_page, wxID_ANY, L"/"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    row->Add(*crit_ctrl, 0, wxLEFT, 4);
    return row;
  };
  auto* disk_row = thresh_row(L"Диск", st.health_disk_warn, st.health_disk_crit, &health_disk_warn_, &health_disk_crit_);
  auto* ram_row = thresh_row(L"RAM", st.health_ram_warn, st.health_ram_crit, &health_ram_warn_, &health_ram_crit_);
  auto* cpu_row = thresh_row(L"CPU", st.health_cpu_warn, st.health_cpu_crit, &health_cpu_warn_, &health_cpu_crit_);
  hsz->Add(health_auto_, 0, wxALL, 8);
  hsz->Add(health_hint, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
  hsz->Add(irow, 0, wxALL, 8);
  hsz->Add(trow_h, 0, wxALL, 8);
  hsz->Add(metrics_label, 0, wxLEFT | wxRIGHT | wxTOP, 8);
  hsz->Add(mrow, 0, wxALL, 8);
  hsz->Add(health_docker_disks_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
  hsz->Add(color_label, 0, wxLEFT | wxRIGHT | wxTOP, 8);
  hsz->Add(color_hint, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
  hsz->Add(disk_row, 0, wxALL, 8);
  hsz->Add(ram_row, 0, wxALL, 8);
  hsz->Add(cpu_row, 0, wxALL, 8);
  health_page->SetSizer(hsz);
  nb->AddPage(health_page, L"Состояние");
  health_interval_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    const int sel = health_interval_->GetSelection();
    if (sel >= 0 && sel < 8) {
      health_interval_sec_->SetValue(std::to_wstring(kHealthIntervalChoices[sel]));
    }
  });
  auto sync_docker_disks = [this] {
    health_docker_disks_->Enable(health_disk_->GetValue());
  };
  health_disk_->Bind(wxEVT_CHECKBOX, [sync_docker_disks](wxCommandEvent&) { sync_docker_disks(); });
  sync_docker_disks();

  extra_programs_ = st.extra_programs;
  auto* programs = new wxPanel(nb);
  programs->SetName(L"card-page");
  auto* psz = new wxFlexGridSizer(3, 3, 8, 8);
  psz->AddGrowableCol(1);
  psz->Add(new wxStaticText(programs, wxID_ANY, L"PuTTY"), 0, wxALIGN_CENTER_VERTICAL);
  putty_ = new wxTextCtrl(programs, wxID_ANY, wxString::FromUTF8(st.putty_path));
  psz->Add(putty_, 1, wxEXPAND);
  auto* pbrowse = make_button(programs, L"Обзор…", BtnIcon::Folder);
  psz->Add(pbrowse);
  psz->Add(new wxStaticText(programs, wxID_ANY, L"WinSCP"), 0, wxALIGN_CENTER_VERTICAL);
  winscp_ = new wxTextCtrl(programs, wxID_ANY, wxString::FromUTF8(st.winscp_path));
  psz->Add(winscp_, 1, wxEXPAND);
  auto* wbrowse = make_button(programs, L"Обзор…", BtnIcon::Folder);
  psz->Add(wbrowse);
  psz->Add(new wxStaticText(programs, wxID_ANY, L"ssh.exe"), 0, wxALIGN_CENTER_VERTICAL);
  ssh_ = new wxTextCtrl(programs, wxID_ANY, wxString::FromUTF8(st.ssh_path));
  psz->Add(ssh_, 1, wxEXPAND);
  auto* sbrowse = make_button(programs, L"Обзор…", BtnIcon::Folder);
  psz->Add(sbrowse);
  auto* extra_label = new wxStaticText(programs, wxID_ANY, L"Другие программы");
  extra_label->SetName(L"section");
  auto* extra_card = new RoundedCard(programs);
  extra_list_ = new StripedListCtrl(extra_card, wxID_ANY, wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
  extra_list_->AppendColumn(L"Название", wxLIST_FORMAT_LEFT, FromDIP(120));
  extra_list_->AppendColumn(L"Файл", wxLIST_FORMAT_LEFT, FromDIP(180));
  extra_list_->AppendColumn(L"Аргументы", wxLIST_FORMAT_LEFT, FromDIP(180));
  style_list(extra_list_);
  extra_list_->SetMinSize(FromDIP(wxSize(-1, 140)));
  auto* extra_sz = new wxBoxSizer(wxVERTICAL);
  extra_sz->Add(extra_list_, 1, wxEXPAND);
  extra_card->SetSizer(extra_sz);
  auto* extra_btns = new wxBoxSizer(wxHORIZONTAL);
  auto* extra_add = make_button(programs, L"Добавить", BtnIcon::Plus);
  auto* extra_edit = make_button(programs, L"Изменить", BtnIcon::Pencil);
  auto* extra_del = make_button(programs, L"Удалить", BtnIcon::Trash);
  extra_btns->Add(extra_add, 0, wxRIGHT, 8);
  extra_btns->Add(extra_edit, 0, wxRIGHT, 8);
  extra_btns->Add(extra_del);
  auto* extra_hint = new wxStaticText(
      programs, wxID_ANY, L"Кнопки появятся на главном окне. Аргументы подставляются из карточки выбранного VPS.");
  extra_hint->SetName(L"muted");
  extra_hint->SetForegroundColour(Theme::muted());
  extra_hint->Wrap(FromDIP(520));
  auto* proot = new wxBoxSizer(wxVERTICAL);
  proot->Add(psz, 0, wxEXPAND | wxALL, 12);
  proot->Add(extra_label, 0, wxLEFT | wxRIGHT, 12);
  proot->Add(extra_card, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
  proot->Add(extra_btns, 0, wxALL, 12);
  proot->Add(extra_hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  programs->SetSizer(proot);
  nb->AddPage(programs, L"Программы");
  refresh_extra_list();

  auto* data = new wxPanel(nb);
  data->SetName(L"card-page");
  auto* dsz = new wxBoxSizer(wxVERTICAL);
  auto* open_dir = make_button(data, L"Открыть папку конфига", BtnIcon::Folder);
  backup_ = new wxCheckBox(data, wxID_ANY, L"Автоматические резервные копии конфига (раз в сутки)");
  backup_->SetValue(st.backup_enabled);
  auto* backup_note = new wxStaticText(data, wxID_ANY, L"Папка backups рядом с конфигом, хранятся последние 14 копий.");
  backup_note->SetName(L"muted");
  backup_note->SetForegroundColour(Theme::muted());
  backup_note->Wrap(FromDIP(480));
  auto* open_backups = make_button(data, L"Открыть папку копий", BtnIcon::Folder);
  export_secrets_ = new wxCheckBox(data, wxID_ANY, L"Экспорт: включить пароли");
  export_settings_ = new wxCheckBox(data, wxID_ANY, L"Экспорт: включить настройки");
  export_settings_->SetValue(true);
  import_settings_ = new wxCheckBox(data, wxID_ANY, L"Импорт: применять настройки");
  import_settings_->SetValue(true);
  auto* exp = make_button(data, L"Экспорт…", BtnIcon::Export);
  auto* imp = make_button(data, L"Импорт…", BtnIcon::Import);
  dsz->Add(open_dir, 0, wxALL, 8);
  dsz->Add(backup_, 0, wxALL, 8);
  dsz->Add(backup_note, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
  dsz->Add(open_backups, 0, wxALL, 8);
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
  auto* lock_min_row = new wxBoxSizer(wxHORIZONTAL);
  lock_min_row->Add(new wxStaticText(sec, wxID_ANY, L"Минут блокировки"), 0, wxALIGN_CENTER_VERTICAL);
  lockout_minutes_ = new wxTextCtrl(sec, wxID_ANY, std::to_wstring(st.master_password_lockout_minutes));
  lock_min_row->Add(lockout_minutes_, 0, wxLEFT, 8);
  auto* chpw = make_button(sec, L"Сменить мастер-пароль…", BtnIcon::Key);
  ssz->Add(short_pw_, 0, wxALL, 8);
  ssz->Add(arow, 0, wxALL, 8);
  ssz->Add(lock_min_row, 0, wxALL, 8);
  ssz->Add(chpw, 0, wxALL, 8);
  sec->SetSizer(ssz);
  nb->AddPage(sec, L"Безопасность");

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  auto* save = accent_button(this, L"Сохранить", BtnIcon::Save);
  btns->Add(save, 0, wxRIGHT, 8);
  btns->Add(make_button(this, L"Отмена", BtnIcon::Cancel, wxID_CANCEL));
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
  wbrowse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxFileDialog dlg(this, L"WinSCP.exe", L"", L"WinSCP.exe", L"WinSCP|WinSCP.exe|EXE|*.exe",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) winscp_->SetValue(dlg.GetPath());
  });
  sbrowse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxFileDialog dlg(this, L"ssh.exe", L"", L"ssh.exe", L"ssh|ssh.exe|EXE|*.exe", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) ssh_->SetValue(dlg.GetPath());
  });
  extra_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { edit_extra(-1); });
  extra_edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    long i = extra_list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (i >= 0) edit_extra(i);
  });
  extra_del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    long i = extra_list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (i < 0 || i >= static_cast<long>(extra_programs_.size())) return;
    if (wxMessageBox(L"Удалить программу «" + wxString::FromUTF8(extra_programs_[static_cast<std::size_t>(i)].name) +
                         L"»?",
                     L"Удалить", wxYES_NO | wxICON_QUESTION, this) != wxYES) {
      return;
    }
    extra_programs_.erase(extra_programs_.begin() + i);
    refresh_extra_list();
  });
  extra_list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& e) { edit_extra(e.GetIndex()); });
  open_dir->Bind(wxEVT_BUTTON, [](wxCommandEvent&) { open_directory(app_dir()); });
  open_backups->Bind(wxEVT_BUTTON, [](wxCommandEvent&) { open_directory(backups_dir()); });
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
      extra_programs_ = config_.settings.extra_programs;
      refresh_extra_list();
      putty_->SetValue(wxString::FromUTF8(config_.settings.putty_path));
      winscp_->SetValue(wxString::FromUTF8(config_.settings.winscp_path));
      ssh_->SetValue(wxString::FromUTF8(config_.settings.ssh_path));
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
  config_.settings.automation_to_shell_when_connected = automation_to_shell_->GetValue();
  config_.settings.shell_follow_command_cwd = shell_follow_cwd_->GetValue();
  config_.settings.advance_command_after_run = advance_command_->GetValue();
  config_.settings.show_command_folder_column = show_folder_col_->GetValue();
  config_.settings.backup_enabled = backup_->GetValue();
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
  config_.settings.winscp_path = std::string(winscp_->GetValue().utf8_string());
  config_.settings.ssh_path = std::string(ssh_->GetValue().utf8_string());
  config_.settings.extra_programs = extra_programs_;
  config_.settings.allow_short_master_password = short_pw_->GetValue();
  config_.settings.master_password_max_attempts = clamp_int(attempts, 0, 100);
  config_.settings.master_password_lockout_minutes = clamp_int(minutes, 1, 24 * 60);
  config_.settings.health_auto = health_auto_->GetValue();
  int health_interval = config_.settings.health_interval_sec;
  int health_timeout = config_.settings.health_timeout_sec;
  int disk_warn = config_.settings.health_disk_warn;
  int disk_crit = config_.settings.health_disk_crit;
  int ram_warn = config_.settings.health_ram_warn;
  int ram_crit = config_.settings.health_ram_crit;
  int cpu_warn = config_.settings.health_cpu_warn;
  int cpu_crit = config_.settings.health_cpu_crit;
  parse_int(std::string(health_interval_sec_->GetValue().utf8_string()), health_interval);
  parse_int(std::string(health_timeout_->GetValue().utf8_string()), health_timeout);
  parse_int(std::string(health_disk_warn_->GetValue().utf8_string()), disk_warn);
  parse_int(std::string(health_disk_crit_->GetValue().utf8_string()), disk_crit);
  parse_int(std::string(health_ram_warn_->GetValue().utf8_string()), ram_warn);
  parse_int(std::string(health_ram_crit_->GetValue().utf8_string()), ram_crit);
  parse_int(std::string(health_cpu_warn_->GetValue().utf8_string()), cpu_warn);
  parse_int(std::string(health_cpu_crit_->GetValue().utf8_string()), cpu_crit);
  config_.settings.health_interval_sec = clamp_int(health_interval, 300, 30 * 24 * 3600);
  config_.settings.health_timeout_sec = clamp_int(health_timeout, 5, 120);
  config_.settings.health_disk_warn = clamp_int(disk_warn, 1, 100);
  config_.settings.health_disk_crit = clamp_int(std::max(disk_crit, config_.settings.health_disk_warn), 1, 100);
  config_.settings.health_ram_warn = clamp_int(ram_warn, 1, 100);
  config_.settings.health_ram_crit = clamp_int(std::max(ram_crit, config_.settings.health_ram_warn), 1, 100);
  config_.settings.health_cpu_warn = clamp_int(cpu_warn, 1, 100);
  config_.settings.health_cpu_crit = clamp_int(std::max(cpu_crit, config_.settings.health_cpu_warn), 1, 100);
  config_.settings.health_show_cpu = health_cpu_->GetValue();
  config_.settings.health_show_ram = health_ram_->GetValue();
  config_.settings.health_show_disk = health_disk_->GetValue();
  config_.settings.health_show_load = health_load_->GetValue();
  config_.settings.health_show_docker_disks = health_docker_disks_->GetValue();
  if (on_apply_) on_apply_();
  EndModal(wxID_OK);
}

void SettingsDialog::refresh_extra_list() {
  if (!extra_list_) return;
  extra_list_->DeleteAllItems();
  for (std::size_t i = 0; i < extra_programs_.size(); ++i) {
    const auto& p = extra_programs_[i];
    long row = extra_list_->InsertItem(static_cast<long>(i), wxString::FromUTF8(p.name));
    extra_list_->SetItem(row, 1, wxString::FromUTF8(p.path));
    extra_list_->SetItem(row, 2, wxString::FromUTF8(p.args));
  }
}

void SettingsDialog::edit_extra(long index) {
  ExtraProgram program = index >= 0 && index < static_cast<long>(extra_programs_.size())
                             ? extra_programs_[static_cast<std::size_t>(index)]
                             : ExtraProgram::make_new();
  ExtraProgramDialog dlg(this, program, &config_.settings, [this] {
    if (on_apply_) on_apply_();
  });
  if (dlg.ShowModal() != wxID_OK || !dlg.accepted) return;
  if (index >= 0 && index < static_cast<long>(extra_programs_.size())) {
    extra_programs_[static_cast<std::size_t>(index)] = dlg.extra;
  } else {
    extra_programs_.push_back(dlg.extra);
  }
  refresh_extra_list();
}

}  // namespace fatty

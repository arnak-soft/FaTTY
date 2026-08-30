#include "ui/app_frame.hpp"

#include "app/single_instance.hpp"
#include "app/version.hpp"
#include "core/config_io.hpp"
#include "core/backup.hpp"
#include "core/paths.hpp"
#include "core/presets.hpp"
#include "core/util.hpp"
#include "net/sftp_session.hpp"
#include "net/updates.hpp"
#include "putty/putty.hpp"
#include "ui/dialogs.hpp"
#include "ui/files_window.hpp"
#include "ui/help_window.hpp"
#include "ui/journal_window.hpp"
#include "ui/bundle_steps_window.hpp"
#include "ui/layout.hpp"
#include "ui/settings_dialog.hpp"
#include "ui/striped_list.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/app.h>
#include <wx/bookctrl.h>
#include <wx/button.h>
#include <wx/choicdlg.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/numdlg.h>
#include <wx/panel.h>
#include <wx/window.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/wrapsizer.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <thread>

namespace fatty {
namespace {

wxString run_confirm_message(const Command& cmd, const std::string& server_name = {}) {
  wxString msg = L"Выполнить «" + wxString::FromUTF8(cmd.name) + L"»";
  if (!server_name.empty()) {
    msg += L" на " + wxString::FromUTF8(server_name);
  }
  msg += L"?";
  if (!cmd.comment.empty()) {
    msg += L"\n\n" + wxString::FromUTF8(cmd.comment);
  }
  return msg;
}

bool confirm_saved_run(wxWindow* parent, const Command& cmd, const std::string& server_name = {}) {
  if (!cmd.confirm_before_run) return true;
  return wxMessageBox(run_confirm_message(cmd, server_name), L"Запуск", wxYES_NO, parent) == wxYES;
}

std::vector<std::string> moved_ids(std::vector<std::string> ids, int from, int to_before) {
  if (from < 0 || from >= static_cast<int>(ids.size())) return ids;
  to_before = std::clamp(to_before, 0, static_cast<int>(ids.size()));
  if (to_before == from || to_before == from + 1) return ids;
  auto id = ids[static_cast<std::size_t>(from)];
  ids.erase(ids.begin() + from);
  int insert = to_before > from ? to_before - 1 : to_before;
  ids.insert(ids.begin() + insert, std::move(id));
  return ids;
}

void fill_row(StripedListCtrl* list, long row, const std::vector<std::string>& ids,
              const std::map<std::string, wxString>& cells) {
  for (int col = 0; col < static_cast<int>(ids.size()); ++col) {
    auto it = cells.find(ids[static_cast<std::size_t>(col)]);
    list->SetItem(row, col, it == cells.end() ? wxString{} : it->second);
  }
}

}  // namespace

AppFrame::AppFrame(Config config, SessionVault vault)
    : wxFrame(nullptr, wxID_ANY, wxString::FromUTF8(std::string(kAppName) + " " + resolve_version()),
              wxDefaultPosition, wxDefaultSize),
      config_(std::move(config)),
      vault_(std::move(vault)),
      journal_(std::make_shared<Journal>(journal_path(), config_.settings.journal_max_entries)) {
  set_icon(this);
  SetMinSize(FromDIP(wxSize(860, 560)));
  SetSize(FromDIP(wxSize(1100, 720)));
  Centre();
  command_stats_ = journal_->stats_by_command_id();
  // Слушателя может дёрнуть фоновый поток команды, поэтому на главный поток
  // возвращаемся через wxTheApp и проверяем живой-токен.
  journal_->add_listener([this, alive = alive_, journal = journal_] {
    if (!alive->load()) return;
    wxTheApp->CallAfter([this, alive, journal] {
      if (!alive->load()) return;
      command_stats_ = journal->stats_by_command_id();
      refresh_commands();
    });
  });
  build_menu();
  build_ui();
  if (!config_.settings.window_geometry.empty() && geometry_on_screen(config_.settings.window_geometry)) {
    restore_window_geometry(this, config_.settings.window_geometry, true);
  }
  apply_dark(this);
  register_window(GetHWND());
  refresh_servers(config_.settings.last_server_id);
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
    if (busy_ && e.CanVeto() && !closing_for_install_) {
      if (wxMessageBox(L"Команда ещё выполняется. Выйти?", L"Выход", wxYES_NO, this) != wxYES) {
        e.Veto();
        return;
      }
    }
    if (busy_ && session_) session_->cancel();
    bundle_cancel_ = true;
    bundle_waiting_ = false;
    bundle_active_ = false;
    clear_run_queue();
    // Закрытие подтверждено: гасим живой-токен, чтобы фоновые задачи не трогали окно.
    alive_->store(false);
    busy_timer_.Stop();
    // Даём потоку команды доработать (он уже получил cancel): цикл опрашивает
    // флаг каждые 40 мс, поэтому ожидание короткое. Потолок — чтобы зависшая
    // сеть не заблокировала выход насовсем.
    for (int waited = 0; worker_running_->load() && waited < 3000; waited += 20) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    persist();
    e.Skip();
  });
  Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
    if (auto* focus = wxWindow::FindFocus()) {
      if (auto* tlw = wxGetTopLevelParent(focus); tlw && tlw != this) {
        e.Skip();
        return;
      }
    }
    if (e.GetKeyCode() == WXK_F5) {
      request_saved_runs();
      return;
    }
    if (e.GetKeyCode() == WXK_F2) {
      if (auto* c = selected_command()) {
        CommandDialog dlg(this, *c, config_.servers, config_.folders, wxString::FromUTF8("Команда: " + c->name));
        dlg.setup_layout(&config_.settings, "command", true, [this] { persist(); });
        if (dlg.ShowModal() == wxID_OK && dlg.accepted) {
          *config_.command_by_id(c->id) = dlg.result;
          config_.settings.last_folder_by_server[dlg.result.server_id] = dlg.result.folder_id;
          persist();
          refresh_servers(dlg.result.server_id);
        }
      }
      return;
    }
    if (e.GetKeyCode() == WXK_F1) {
      show_help();
      return;
    }
    if (e.ControlDown() && (e.GetKeyCode() == 'J' || e.GetKeyCode() == 'j')) {
      show_journal();
      return;
    }
    if (e.ControlDown() && e.GetKeyCode() == ',') {
      open_settings();
      return;
    }
    e.Skip();
  });
  CallAfter([this] {
    if (config_.settings.window_state == "zoomed") Maximize();
    if (config_.settings.sash_pos > 0) hsplit_->SetSashPosition(config_.settings.sash_pos);
    if (config_.settings.vsash_pos > 0) vsplit_->SetSashPosition(config_.settings.vsash_pos);
    restore_columns();
    restoring_ = false;
  });
  if (config_.settings.check_updates_on_start) {
    using clock = std::chrono::system_clock;
    double now = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
    if (now - config_.settings.last_update_check >= 24 * 3600) {
      check_updates_async(false);
    }
  }
  maybe_run_backup();
}

void AppFrame::build_menu() {
  auto* bar = new wxMenuBar();
  auto* file = new wxMenu();
  file->Append(1001, L"Открыть папку конфига");
  file->Append(1002, L"Журнал команд…\tCtrl+J");
  file->AppendSeparator();
  file->Append(1003, L"Экспорт…");
  file->Append(1004, L"Импорт…");
  file->AppendSeparator();
  file->Append(wxID_EXIT, L"Выход");
  bar->Append(file, L"Файл");
  auto* opt = new wxMenu();
  opt->Append(1010, L"Настройки…\tCtrl+,");
  opt->AppendSeparator();
  opt->Append(1011, L"Сменить мастер-пароль…");
  bar->Append(opt, L"Настройки");
  auto* help = new wxMenu();
  help->Append(1020, L"Содержание\tF1");
  help->Append(1021, L"Частые команды");
  help->AppendSeparator();
  help->Append(1022, L"Проверить обновления…");
  help->AppendSeparator();
  help->Append(wxID_ABOUT, L"О программе");
  bar->Append(help, L"Справка");
  SetMenuBar(bar);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { open_directory(app_dir()); }, 1001);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { show_journal(); }, 1002);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    bool secrets = wxMessageBox(L"Включить пароли VPS в файл?", L"Экспорт", wxYES_NO, this) == wxYES;
    if (secrets && wxMessageBox(L"Файл будет содержать пароли в открытом виде. Продолжить?", L"Экспорт", wxYES_NO, this) !=
                       wxYES)
      return;
    bool settings = wxMessageBox(L"Включить настройки приложения?", L"Экспорт", wxYES_NO, this) == wxYES;
    wxFileDialog dlg(this, L"Экспорт FaTTY", L"", L"fatty-backup.json", L"JSON|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;
    try {
      write_export(std::filesystem::path(dlg.GetPath().utf8_string()), config_, secrets, settings);
      wxMessageBox(L"Сохранено.", L"Экспорт", wxOK);
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), L"Экспорт", wxOK | wxICON_ERROR);
    }
  }, 1003);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    int choice = wxMessageBox(L"Да — добавить\nНет — заменить\nОтмена", L"Импорт", wxYES_NO | wxCANCEL, this);
    if (choice == wxCANCEL) return;
    wxFileDialog dlg(this, L"Импорт", L"", L"", L"JSON|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;
    try {
      auto data = read_export(std::filesystem::path(dlg.GetPath().utf8_string()));
      auto result = import_into_config(config_, data, choice == wxYES ? "merge" : "replace", true);
      persist();
      refresh_servers();
      wxMessageBox(wxString::FromUTF8(format_import_summary(result, choice == wxYES ? "merge" : "replace")), L"Импорт");
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), L"Импорт", wxOK | wxICON_ERROR);
    }
  }, 1004);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { open_settings(); }, 1010);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    ChangeMasterDialog d(this, vault_, config_.settings.allow_short_master_password);
    d.setup_layout(&config_.settings, "change_master", false, [this] { persist(); });
    if (d.ShowModal() == wxID_OK && d.ok) {
      persist();
      wxMessageBox(L"Мастер-пароль обновлён.", L"Мастер-пароль");
    }
  }, 1011);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { show_help(); }, 1020);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { show_help("Команды"); }, 1021);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { check_updates_interactive(); }, 1022);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    wxMessageBox(wxString::FromUTF8(std::string(kAppName) + " " + resolve_version() +
                                    "\n\nЗапуск команд на VPS по SSH.\n\nСправка: F1"),
                 L"О программе");
  }, wxID_ABOUT);
}

void AppFrame::build_ui() {
  const int pad = FromDIP(10);
  const int gap = FromDIP(8);
  auto add_btn = [gap](wxSizer* sz, wxWindow* btn) {
    sz->Add(btn, 0, wxRIGHT | wxBOTTOM, gap);
  };
  auto* panel = new wxPanel(this);
  vsplit_ = new wxSplitterWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
  auto* top = new wxPanel(vsplit_);
  hsplit_ = new wxSplitterWindow(top, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
  auto* left = new wxPanel(hsplit_);
  auto* right = new wxPanel(hsplit_);
  server_search_ = new wxTextCtrl(left, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
#if wxCHECK_VERSION(3, 1, 0)
  server_search_->SetHint(L"Поиск VPS…");
#endif
  auto* servers_card = new RoundedCard(left);
  servers_ = new StripedListCtrl(servers_card, wxID_ANY,
                                 wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
  setup_server_columns();
  auto* servers_sz = new wxBoxSizer(wxVERTICAL);
  servers_sz->Add(servers_, 1, wxEXPAND);
  servers_card->SetSizer(servers_sz);
  auto* sbtns = new wxWrapSizer(wxHORIZONTAL);
  auto* sadd = make_button(left, L"Добавить", BtnIcon::Plus);
  auto* sedit = make_button(left, L"Изменить", BtnIcon::Pencil);
  auto* sdup = make_button(left, L"Дублировать", BtnIcon::Copy);
  auto* sdel = make_button(left, L"Удалить", BtnIcon::Trash);
  add_btn(sbtns, sadd);
  add_btn(sbtns, sedit);
  add_btn(sbtns, sdup);
  add_btn(sbtns, sdel);
  auto* sact = new wxWrapSizer(wxHORIZONTAL);
  auto* files = make_button(left, L"Файлы", BtnIcon::Folder);
  auto* cons = make_button(left, L"Открыть консоль", BtnIcon::Terminal);
  auto* putty = make_button(left, L"PuTTY", BtnIcon::Putty);
  auto* winscp = make_button(left, L"WinSCP", BtnIcon::WinSCP);
  auto* test = make_button(left, L"Проверить связь", BtnIcon::Network);
  add_btn(sact, files);
  add_btn(sact, cons);
  add_btn(sact, putty);
  add_btn(sact, winscp);
  add_btn(sact, test);
  extra_tools_parent_ = left;
  extra_tools_sizer_ = new wxWrapSizer(wxHORIZONTAL);
  auto* ls = new wxBoxSizer(wxVERTICAL);
  ls->Add(section_label(left, L"VPS-серверы"), 0, wxBOTTOM, gap);
  ls->Add(server_search_, 0, wxEXPAND | wxBOTTOM, gap);
  ls->Add(servers_card, 1, wxEXPAND);
  ls->Add(sbtns, 0, wxTOP, pad);
  ls->Add(sact, 0);
  ls->Add(extra_tools_sizer_, 0);
  left->SetSizer(ls);
  // Кнопки VPS и внешние программы гасятся на время SSH; список команд
  // остаётся доступен — запуск ставит в очередь.
  busy_disable_ = {sedit, sdup, sdel, cons, putty, winscp, test, files};
  rebuild_extra_tools();

  auto* right_nb = new RoundedNotebook(right);

  auto* groups_page = new wxPanel(right_nb);
  groups_page->SetName(L"card-page");
  groups_page->SetBackgroundColour(Theme::elevated());
  groups_page->SetForegroundColour(Theme::text());
  folders_nb_ = new RoundedNotebook(groups_page);
  auto* first_page = new wxPanel(folders_nb_);
  first_page->SetName(L"card-page");
  first_page->SetBackgroundColour(Theme::elevated());
  first_page->SetForegroundColour(Theme::text());
  auto* page_sz = new wxBoxSizer(wxVERTICAL);
  commands_ = new StripedListCtrl(first_page, wxID_ANY, wxLC_REPORT | wxBORDER_NONE);
  setup_command_columns();
  page_sz->Add(commands_, 1, wxEXPAND);
  first_page->SetSizer(page_sz);
  folders_nb_->AddPage(first_page, L"Общее");
  folder_tab_ids_.push_back("");
  auto* corder = new wxWrapSizer(wxHORIZONTAL);
  auto* up = make_button(groups_page, L"Вверх", BtnIcon::ArrowUp);
  auto* down = make_button(groups_page, L"Вниз", BtnIcon::ArrowDown);
  auto* byname = make_button(groups_page, L"По имени", BtnIcon::Sort);
  auto* fadd = make_button(groups_page, L"Группа+", BtnIcon::FolderPlus);
  auto* frename = make_button(groups_page, L"Переименовать", BtnIcon::Pencil);
  auto* fdel = make_button(groups_page, L"Удалить группу", BtnIcon::Trash);
  add_btn(corder, up);
  add_btn(corder, down);
  corder->Add(byname, 0, wxRIGHT | wxBOTTOM, FromDIP(16));
  add_btn(corder, fadd);
  add_btn(corder, frename);
  add_btn(corder, fdel);
  auto* cbtns = new wxBoxSizer(wxHORIZONTAL);
  auto* cadd = make_button(groups_page, L"Добавить", BtnIcon::Plus);
  auto* cedit = make_button(groups_page, L"Изменить  (F2)", BtnIcon::Pencil);
  auto* cdup = make_button(groups_page, L"Дублировать", BtnIcon::Copy);
  auto* cdel = make_button(groups_page, L"Удалить", BtnIcon::Trash);
  auto* cmove = make_button(groups_page, L"Переместить в группу", BtnIcon::FolderMove);
  auto* presets = make_button(groups_page, L"Пресеты…", BtnIcon::List);
  stop_btn_ = make_button(groups_page, L"Стоп", BtnIcon::Stop);
  run_btn_ = accent_button(groups_page, L"Запустить  (F5)", BtnIcon::Play);
  stop_btn_->Enable(false);
  cbtns->Add(cadd, 0, wxRIGHT, gap);
  cbtns->Add(cedit, 0, wxRIGHT, gap);
  cbtns->Add(cdup, 0, wxRIGHT, gap);
  cbtns->Add(cdel, 0, wxRIGHT, gap);
  cbtns->Add(cmove, 0, wxRIGHT, gap);
  cbtns->Add(presets, 0, wxRIGHT, pad);
  cbtns->AddStretchSpacer();
  cbtns->Add(run_btn_, 0, wxRIGHT, gap);
  cbtns->Add(stop_btn_);
  auto* gs = new wxBoxSizer(wxVERTICAL);
  gs->Add(folders_nb_, 1, wxEXPAND);
  gs->Add(corder, 0, wxTOP, pad);
  gs->Add(cbtns, 0);
  groups_page->SetSizer(gs);

  auto* bundles_page = new wxPanel(right_nb);
  bundles_page->SetName(L"card-page");
  bundles_page->SetBackgroundColour(Theme::elevated());
  bundles_page->SetForegroundColour(Theme::text());
  auto* bundles_card = new RoundedCard(bundles_page);
  bundles_ = new StripedListCtrl(bundles_card, wxID_ANY, wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
  bundles_->AppendColumn(L"Название", wxLIST_FORMAT_LEFT, FromDIP(160));
  bundles_->AppendColumn(L"Команд", wxLIST_FORMAT_LEFT, FromDIP(70));
  bundles_->AppendColumn(L"Пауза", wxLIST_FORMAT_LEFT, FromDIP(70));
  auto* bundles_sz = new wxBoxSizer(wxVERTICAL);
  bundles_sz->Add(bundles_, 1, wxEXPAND);
  bundles_card->SetSizer(bundles_sz);
  auto* bbtns = new wxWrapSizer(wxHORIZONTAL);
  auto* badd = make_button(bundles_page, L"Добавить", BtnIcon::Plus);
  auto* bedit = make_button(bundles_page, L"Изменить", BtnIcon::Pencil);
  auto* bsteps = make_button(bundles_page, L"По шагам", BtnIcon::List);
  auto* bdel = make_button(bundles_page, L"Удалить", BtnIcon::Trash);
  auto* brun = make_button(bundles_page, L"Запустить связку", BtnIcon::Play);
  bundles_stop_btn_ = make_button(bundles_page, L"Стоп", BtnIcon::Stop);
  bundles_stop_btn_->Enable(false);
  add_btn(bbtns, badd);
  add_btn(bbtns, bedit);
  add_btn(bbtns, bsteps);
  add_btn(bbtns, bdel);
  add_btn(bbtns, brun);
  add_btn(bbtns, bundles_stop_btn_);
  auto* bs = new wxBoxSizer(wxVERTICAL);
  bs->Add(bundles_card, 1, wxEXPAND);
  bs->Add(bbtns, 0, wxTOP, gap);
  bundles_page->SetSizer(bs);

  right_nb->AddPage(groups_page, L"Группы");
  right_nb->AddPage(bundles_page, L"Связки");
  right_nb->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this, right_nb, bundles_page](wxBookCtrlEvent& e) {
    const int sel = e.GetSelection();
    if (sel >= 0 && right_nb->GetPage(static_cast<std::size_t>(sel)) == bundles_page) {
      refresh_bundles();
    }
    e.Skip();
  });

  auto* qrow = new wxBoxSizer(wxHORIZONTAL);
  qrow->Add(new wxStaticText(right, wxID_ANY, L"Разовая команда:"), 0, wxALIGN_CENTER_VERTICAL);
  quick_ = new wxTextCtrl(right, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
  auto* qrun = make_button(right, L"Выполнить", BtnIcon::Play);
  quick_->SetMinSize(wxSize(-1, qrun->GetBestSize().GetHeight()));
  qrow->Add(quick_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, pad);
  qrow->Add(qrun, 0, wxALIGN_CENTER_VERTICAL);
  auto* rs = new wxBoxSizer(wxVERTICAL);
  rs->Add(right_nb, 1, wxEXPAND);
  rs->Add(qrow, 0, wxEXPAND | wxTOP, pad);
  right->SetSizer(rs);
  hsplit_->SplitVertically(left, right, FromDIP(400));
  auto* ts = new wxBoxSizer(wxVERTICAL);
  ts->Add(hsplit_, 1, wxEXPAND);
  top->SetSizer(ts);

  auto* outp = new wxPanel(vsplit_);
  cwd_label_ = new wxStaticText(outp, wxID_ANY, L"Папка: ~");
  cwd_label_->SetName(L"muted");
  cwd_reset_ = make_button(outp, L"Сбросить в ~", BtnIcon::Home);
  cwd_reset_->Enable(false);
  auto* clear = make_button(outp, L"Очистить", BtnIcon::Clear);
  auto* jbtn = make_button(outp, L"Журнал", BtnIcon::List);
  auto* out_card = new RoundedCard(outp);
  output_ = new wxTextCtrl(out_card, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxTE_WORDWRAP | wxBORDER_NONE);
  style_text(output_, true);
  auto* out_card_sz = new wxBoxSizer(wxVERTICAL);
  out_card_sz->Add(output_, 1, wxEXPAND);
  out_card->SetSizer(out_card_sz);
  bind_copy_on_select(output_, [this](const std::string& t) {
    status_->SetLabel(wxString::Format(L"Скопировано в буфер (%d симв.)", (int)t.size()));
  });
  auto* ot = new wxBoxSizer(wxHORIZONTAL);
  ot->Add(section_label(outp, L"Вывод"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, pad);
  ot->Add(cwd_label_, 1, wxALIGN_CENTER_VERTICAL);
  ot->Add(cwd_reset_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, pad);
  ot->AddStretchSpacer();
  ot->Add(jbtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
  ot->Add(clear, 0, wxALIGN_CENTER_VERTICAL);
  auto* os = new wxBoxSizer(wxVERTICAL);
  os->Add(ot, 0, wxEXPAND);
  os->Add(out_card, 1, wxEXPAND | wxTOP, gap);
  outp->SetSizer(os);
  vsplit_->SplitHorizontally(top, outp, FromDIP(420));

  auto* status_bar = new wxPanel(panel);
  status_bar->SetName(L"chrome");
  status_ = new wxStaticText(status_bar, wxID_ANY, L"Готово");
  status_->SetName(L"muted");
  status_->SetForegroundColour(Theme::muted());
  busy_gauge_ = new wxGauge(status_bar, wxID_ANY, 100, wxDefaultPosition, FromDIP(wxSize(120, 12)),
                            wxGA_HORIZONTAL | wxGA_SMOOTH);
  busy_gauge_->Hide();
  auto* sbs = new wxBoxSizer(wxHORIZONTAL);
  sbs->Add(status_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, pad);
  sbs->Add(busy_gauge_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, pad);
  status_bar->SetSizer(sbs);
  status_bar->SetMinSize(wxSize(-1, FromDIP(26)));
  busy_timer_.SetOwner(this);
  Bind(wxEVT_TIMER, [this](wxTimerEvent&) { update_busy_indicator(); });

  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(vsplit_, 1, wxEXPAND | wxALL, pad);
  root->Add(status_bar, 0, wxEXPAND);
  panel->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(panel, 1, wxEXPAND);
  SetSizer(outer);

  server_search_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
    server_filter_ = trim(std::string(server_search_->GetValue().utf8_string()));
    refresh_servers();
  });
  servers_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) {
    rebuild_folder_tabs();
    refresh_commands();
  });
  servers_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this, sedit](wxListEvent&) {
    wxCommandEvent ev(wxEVT_BUTTON);
    sedit->GetEventHandler()->ProcessEvent(ev);
  });
  commands_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { request_saved_runs(); });
  commands_->Bind(wxEVT_LIST_COL_CLICK, [this](wxListEvent& e) {
    auto* s = selected_server();
    if (!s) return;
    const auto ids = command_column_ids();
    const int col = e.GetColumn();
    if (col < 0 || col >= static_cast<int>(ids.size())) return;
    const auto& by = ids[static_cast<std::size_t>(col)];
    if (by == "avg") {
      auto group = config_.commands_for(s->id, current_folder_id());
      auto avg_of = [this](const std::string& id) -> double {
        auto it = command_stats_.find(id);
        if (it == command_stats_.end() || it->second.run_count <= 0) return -1;
        return it->second.average_sec;
      };
      std::sort(group.begin(), group.end(), [&](const Command& a, const Command& b) {
        const double da = avg_of(a.id);
        const double db = avg_of(b.id);
        const bool a_missing = da < 0;
        const bool b_missing = db < 0;
        if (a_missing != b_missing) return !a_missing;
        if (da != db) return da < db;
        auto na = to_lower(trim(a.name));
        auto nb = to_lower(trim(b.name));
        if (na != nb) return na < nb;
        return a.id < b.id;
      });
      config_.set_commands_for(s->id, current_folder_id(), group);
    } else if (by == "name" || by == "command" || by == "comment" || by == "folder") {
      config_.sort_commands_for(s->id, current_folder_id(), by);
    } else {
      return;
    }
    persist();
    refresh_commands();
  });
  servers_->Bind(wxEVT_LIST_COL_END_DRAG, [this](wxListEvent& e) {
    if (e.GetInt() >= 0) {
      config_.settings.column_order["servers"] =
          moved_ids(server_column_ids(), e.GetColumn(), e.GetInt());
    }
    if (!restoring_) persist();
  });
  commands_->Bind(wxEVT_LIST_COL_END_DRAG, [this](wxListEvent& e) {
    if (e.GetInt() >= 0) {
      config_.settings.column_order["commands"] =
          moved_ids(command_column_ids(), e.GetColumn(), e.GetInt());
    }
    if (!restoring_) persist();
  });

  sadd->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    ServerDialog dlg(this, Server::make_new(), L"Новый VPS", true);
    dlg.setup_layout(&config_.settings, "server", false, [this] { persist(); });
    if (dlg.ShowModal() == wxID_OK && dlg.accepted) {
      config_.servers.push_back(dlg.result);
      persist();
      refresh_servers(dlg.result.id);
    }
  });
  sedit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) return;
    ServerDialog dlg(this, *s, wxString::FromUTF8("VPS: " + s->name), false);
    dlg.setup_layout(&config_.settings, "server", false, [this] { persist(); });
    if (dlg.ShowModal() == wxID_OK && dlg.accepted) {
      *s = dlg.result;
      persist();
      refresh_servers(s->id);
    }
  });
  sdup->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) return;
    std::vector<std::string> names;
    for (auto& x : config_.servers) names.push_back(x.name);
    auto clone = s->duplicate(copy_name(s->name, names));
    auto cmds = config_.commands_for(s->id);
    std::map<std::string, std::string> folder_map;
    for (const auto& f : config_.folders_for(s->id)) {
      auto nf = Folder::make_new(clone.id, f.name);
      folder_map[f.id] = nf.id;
      config_.folders.push_back(nf);
    }
    config_.servers.push_back(clone);
    std::map<std::string, std::string> cmd_map;
    for (auto& c : cmds) {
      auto d = c.duplicate("", clone.id);
      if (!c.folder_id.empty() && folder_map.count(c.folder_id)) {
        d.folder_id = folder_map[c.folder_id];
      } else {
        d.folder_id.clear();
      }
      config_.commands.push_back(d);
      cmd_map[c.id] = d.id;
    }
    for (const auto& b : config_.bundles_for(s->id)) {
      auto nb = b;
      nb.id = new_uuid();
      nb.server_id = clone.id;
      std::vector<std::string> ids;
      for (const auto& cid : b.command_ids) {
        auto it = cmd_map.find(cid);
        if (it != cmd_map.end()) ids.push_back(it->second);
      }
      if (ids.empty()) continue;
      nb.command_ids = std::move(ids);
      config_.bundles.push_back(std::move(nb));
    }
    persist();
    refresh_servers(clone.id);
  });
  sdel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) return;
    if (wxMessageBox(L"Удалить «" + wxString::FromUTF8(s->name) + L"»?", L"Удалить VPS", wxYES_NO, this) != wxYES) return;
    auto id = s->id;
    config_.servers.erase(std::remove_if(config_.servers.begin(), config_.servers.end(),
                                         [&](const Server& x) { return x.id == id; }),
                          config_.servers.end());
    config_.commands.erase(std::remove_if(config_.commands.begin(), config_.commands.end(),
                                          [&](const Command& x) { return x.server_id == id; }),
                           config_.commands.end());
    config_.folders.erase(std::remove_if(config_.folders.begin(), config_.folders.end(),
                                         [&](const Folder& x) { return x.server_id == id; }),
                          config_.folders.end());
    config_.drop_server_bundles(id);
    persist();
    refresh_servers();
  });
  files->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) {
      wxMessageBox(L"Сначала выберите VPS.", L"Файлы");
      return;
    }
    auto it = files_windows_.find(s->id);
    if (it != files_windows_.end() && it->second) {
      it->second->Show();
      it->second->Raise();
      return;
    }
    auto start = guess_start_path(config_.commands_for(s->id));
    auto* win = new FilesWindow(this, *s, start);
    files_windows_[s->id] = win;
    win->Bind(wxEVT_CLOSE_WINDOW, [this, id = s->id](wxCloseEvent& e) {
      files_windows_.erase(id);
      e.Skip();
    });
    win->Show();
  });
  cons->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) return;
    try {
      open_system_console(*s, config_.settings.ssh_path);
      status_->SetLabel(wxString::FromUTF8("Консоль открыта → " + s->name));
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), L"Консоль", wxOK | wxICON_ERROR, this);
    }
  });
  putty->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) return;
    try {
      open_putty_console(*s, config_.settings.putty_path);
      status_->SetLabel(wxString::FromUTF8("PuTTY открыт → " + s->name));
    } catch (const PuttyNotFoundError&) {
      int c = wxMessageBox(L"PuTTY не найден.\nДа — скачать\nНет — указать putty.exe", L"PuTTY", wxYES_NO | wxCANCEL, this);
      if (c == wxYES) open_url(kPuttyDownloadUrl);
      else if (c == wxNO) {
        wxFileDialog dlg(this, L"putty.exe", L"", L"putty.exe", L"PuTTY|putty.exe", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK) {
          config_.settings.putty_path = std::string(dlg.GetPath().utf8_string());
          persist();
          open_putty_console(*s, config_.settings.putty_path);
        }
      }
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), L"PuTTY", wxOK | wxICON_ERROR, this);
    }
  });
  winscp->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) return;
    try {
      open_winscp(*s, config_.settings.winscp_path);
      status_->SetLabel(wxString::FromUTF8("WinSCP открыт → " + s->name));
    } catch (const WinSCPNotFoundError&) {
      int c = wxMessageBox(L"WinSCP не найден.\nДа — скачать\nНет — указать WinSCP.exe", L"WinSCP",
                           wxYES_NO | wxCANCEL, this);
      if (c == wxYES) open_url(kWinscpDownloadUrl);
      else if (c == wxNO) {
        wxFileDialog dlg(this, L"WinSCP.exe", L"", L"WinSCP.exe", L"WinSCP|WinSCP.exe", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK) {
          config_.settings.winscp_path = std::string(dlg.GetPath().utf8_string());
          persist();
          open_winscp(*s, config_.settings.winscp_path);
        }
      }
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), L"WinSCP", wxOK | wxICON_ERROR, this);
    }
  });
  test->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (s) run_command(*s, "echo OK && hostname && whoami && pwd", 30, true, "Проверка " + s->name, "", "test");
  });
  up->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* c = selected_command();
    if (c && config_.move_command(c->id, -1)) {
      persist();
      refresh_commands();
    }
  });
  down->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* c = selected_command();
    if (c && config_.move_command(c->id, 1)) {
      persist();
      refresh_commands();
    }
  });
  byname->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) return;
    config_.sort_commands_for(s->id, current_folder_id(), "name");
    persist();
    refresh_commands();
  });
  folders_nb_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
    if (updating_folders_) return;
    attach_commands_page(e.GetSelection());
    if (auto* s = selected_server()) {
      config_.settings.last_folder_by_server[s->id] = current_folder_id();
    }
    refresh_commands();
  });
  fadd->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) {
      wxMessageBox(L"Сначала выберите VPS.", L"Группа");
      return;
    }
    auto name = wxGetTextFromUser(L"Имя группы", L"Новая группа", L"", this);
    auto trimmed = trim(std::string(name.utf8_string()));
    if (trimmed.empty()) return;
    for (const auto& f : config_.folders_for(s->id)) {
      if (to_lower(f.name) == to_lower(trimmed)) {
        wxMessageBox(L"Такая группа уже есть.", L"Группа", wxOK | wxICON_WARNING, this);
        return;
      }
    }
    auto folder = Folder::make_new(s->id, trimmed);
    config_.settings.last_folder_by_server[s->id] = folder.id;
    config_.folders.push_back(folder);
    persist();
    rebuild_folder_tabs();
    refresh_commands();
  });
  frename->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto id = current_folder_id();
    auto* f = config_.folder_by_id(id);
    if (!f) {
      wxMessageBox(L"Вкладку «Общее» переименовать нельзя — создайте группу.", L"Группа");
      return;
    }
    auto name = wxGetTextFromUser(L"Новое имя", L"Группа", wxString::FromUTF8(f->name), this);
    auto trimmed = trim(std::string(name.utf8_string()));
    if (trimmed.empty()) return;
    f->name = trimmed;
    persist();
    rebuild_folder_tabs();
    refresh_commands();
  });
  fdel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto id = current_folder_id();
    if (id.empty()) {
      wxMessageBox(L"Вкладку «Общее» удалить нельзя.", L"Группа");
      return;
    }
    auto* f = config_.folder_by_id(id);
    if (!f) return;
    if (wxMessageBox(L"Удалить группу «" + wxString::FromUTF8(f->name) +
                         L"»? Команды останутся во вкладке «Общее».",
                     L"Группа", wxYES_NO, this) != wxYES)
      return;
    auto* s = selected_server();
    if (s) config_.settings.last_folder_by_server[s->id].clear();
    config_.remove_folder(id);
    persist();
    rebuild_folder_tabs();
    refresh_commands();
  });
  cadd->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) {
      wxMessageBox(L"Сначала выберите или добавьте VPS.", L"Команда");
      return;
    }
    auto cmd = Command::make_new(s->id);
    cmd.timeout_sec = config_.settings.default_command_timeout;
    cmd.folder_id = current_folder_id();
    CommandDialog dlg(this, cmd, config_.servers, config_.folders, L"Новая команда");
    dlg.setup_layout(&config_.settings, "command", true, [this] { persist(); });
    if (dlg.ShowModal() == wxID_OK && dlg.accepted) {
      config_.commands.push_back(dlg.result);
      config_.settings.last_folder_by_server[dlg.result.server_id] = dlg.result.folder_id;
      persist();
      refresh_servers(dlg.result.server_id);
    }
  });
  cedit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* c = selected_command();
    if (!c) return;
    CommandDialog dlg(this, *c, config_.servers, config_.folders, wxString::FromUTF8("Команда: " + c->name));
    dlg.setup_layout(&config_.settings, "command", true, [this] { persist(); });
    if (dlg.ShowModal() == wxID_OK && dlg.accepted) {
      *c = dlg.result;
      config_.settings.last_folder_by_server[dlg.result.server_id] = dlg.result.folder_id;
      persist();
      refresh_servers(dlg.result.server_id);
    }
  });
  cdup->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* c = selected_command();
    if (!c) return;
    std::vector<std::string> names;
    for (auto& x : config_.commands_for(c->server_id)) names.push_back(x.name);
    auto clone = c->duplicate(copy_name(c->name, names));
    config_.commands.push_back(clone);
    persist();
    refresh_commands();
  });
  cdel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto sel = selected_commands();
    if (sel.empty()) return;
    wxString msg;
    if (sel.size() == 1) {
      msg = L"Удалить «" + wxString::FromUTF8(sel[0]->name) + L"»?";
    } else {
      msg = wxString::Format(L"Удалить выбранные команды (%d)?", static_cast<int>(sel.size()));
    }
    if (wxMessageBox(msg, L"Удалить команду", wxYES_NO, this) != wxYES) return;
    std::vector<std::string> ids;
    ids.reserve(sel.size());
    for (auto* c : sel) ids.push_back(c->id);
    for (const auto& id : ids) config_.drop_command_from_bundles(id);
    config_.commands.erase(std::remove_if(config_.commands.begin(), config_.commands.end(),
                                          [&](const Command& x) {
                                            return std::find(ids.begin(), ids.end(), x.id) != ids.end();
                                          }),
                           config_.commands.end());
    persist();
    refresh_commands();
  });
  cmove->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto sel = selected_commands();
    if (sel.empty()) {
      wxMessageBox(L"Выберите одну или несколько команд.", L"Переместить в группу");
      return;
    }
    auto* s = selected_server();
    if (!s) return;
    wxArrayString labels;
    std::vector<std::string> ids;
    labels.Add(L"Общее");
    ids.push_back("");
    for (const auto& f : config_.folders_for(s->id)) {
      labels.Add(wxString::FromUTF8(f.name));
      ids.push_back(f.id);
    }
    const int n = wxGetSingleChoiceIndex(L"Куда переместить выбранные команды?", L"Переместить в группу", labels, this);
    if (n < 0 || n >= static_cast<int>(ids.size())) return;
    const std::string dest = ids[static_cast<std::size_t>(n)];
    int moved = 0;
    for (auto* c : sel) {
      if (c->server_id != s->id) continue;
      if (c->folder_id == dest) continue;
      c->folder_id = dest;
      ++moved;
    }
    if (moved == 0) {
      wxMessageBox(L"Выбранные команды уже в этой группе.", L"Переместить в группу");
      return;
    }
    config_.settings.last_folder_by_server[s->id] = dest;
    persist();
    rebuild_folder_tabs();
    refresh_commands();
    status_->SetLabel(wxString::Format(L"Перемещено команд: %d", moved));
  });
  presets->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) return;
    PresetDialog dlg(this, *s);
    dlg.setup_layout(&config_.settings, "preset", false, [this] { persist(); });
    if (dlg.ShowModal() != wxID_OK || !dlg.accepted) return;
    int added = 0;
    auto existing = config_.commands_for(s->id);
    std::vector<std::string> names;
    for (auto& c : existing) names.push_back(c.name);
    for (auto& p : dlg.result) {
      if (std::find(names.begin(), names.end(), p.name) != names.end()) continue;
      auto cmd = Command::make_new(s->id);
      cmd.name = p.name;
      cmd.comment = p.comment;
      cmd.command = p.command;
      cmd.timeout_sec = p.timeout_sec;
      cmd.login_shell = p.login_shell;
      cmd.folder_id = current_folder_id();
      cmd.working_dir = p.working_dir;
      cmd.cd_before_run = true;
      config_.commands.push_back(cmd);
      names.push_back(p.name);
      ++added;
    }
    persist();
    refresh_commands();
    status_->SetLabel(wxString::Format(L"Добавлено команд: %d", added));
  });
  run_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s || selected_commands().empty()) {
      wxMessageBox(L"Выберите VPS и команду.", L"Запуск");
      return;
    }
    request_saved_runs();
  });
  auto on_stop = [this](wxCommandEvent&) {
    clear_run_queue();
    if (bundle_active_) {
      bundle_cancel_ = true;
      bundle_waiting_ = false;
      if (session_) {
        session_->cancel();
      } else {
        finish_bundle("прервано");
      }
      return;
    }
    if (session_) session_->cancel();
  };
  stop_btn_->Bind(wxEVT_BUTTON, on_stop);
  bundles_stop_btn_->Bind(wxEVT_BUTTON, on_stop);
  qrun->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    auto cmd = trim(std::string(quick_->GetValue().utf8_string()));
    if (!s || cmd.empty()) return;
    if (config_.settings.confirm_before_run &&
        wxMessageBox(L"Выполнить разовую команду?", L"Запуск", wxYES_NO, this) != wxYES)
      return;
    run_command(*s, cmd, config_.settings.default_command_timeout, true, "разовая команда", "", "quick");
  });
  quick_->Bind(wxEVT_TEXT_ENTER, [this, qrun](wxCommandEvent&) {
    wxCommandEvent ev(wxEVT_BUTTON);
    qrun->GetEventHandler()->ProcessEvent(ev);
  });
  cwd_reset_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) return;
    remote_cwd_.erase(s->id);
    update_cwd_label();
    status_->SetLabel(L"Рабочая папка сброшена в домашнюю");
  });
  clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { output_->Clear(); });
  jbtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show_journal(); });

  auto edit_bundle = [this](bool is_new) {
    auto* s = selected_server();
    if (!s) {
      wxMessageBox(L"Сначала выберите или добавьте VPS.", L"Связка");
      return;
    }
    if (config_.commands_for(s->id).empty()) {
      wxMessageBox(L"Сначала добавьте хотя бы одну команду.", L"Связка");
      return;
    }
    Bundle src;
    wxString title = L"Новая связка";
    if (is_new) {
      src = Bundle::make_new(s->id);
    } else {
      auto* b = selected_bundle();
      if (!b) {
        wxMessageBox(L"Выберите связку.", L"Связка");
        return;
      }
      src = *b;
      title = wxString::FromUTF8("Связка: " + b->name);
    }
    BundleDialog dlg(this, src, config_, title);
    dlg.setup_layout(&config_.settings, "bundle", true, [this] { persist(); });
    if (dlg.ShowModal() != wxID_OK || !dlg.accepted) return;
    if (is_new) {
      config_.bundles.push_back(dlg.result);
    } else if (auto* b = config_.bundle_by_id(dlg.result.id)) {
      *b = dlg.result;
    }
    persist();
    refresh_bundles();
  };
  badd->Bind(wxEVT_BUTTON, [edit_bundle](wxCommandEvent&) { edit_bundle(true); });
  bedit->Bind(wxEVT_BUTTON, [edit_bundle](wxCommandEvent&) { edit_bundle(false); });
  bsteps->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_bundle_steps(); });
  bdel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* b = selected_bundle();
    if (!b) return;
    if (wxMessageBox(L"Удалить связку «" + wxString::FromUTF8(b->name) + L"»?", L"Связка", wxYES_NO, this) !=
        wxYES)
      return;
    auto id = b->id;
    config_.bundles.erase(std::remove_if(config_.bundles.begin(), config_.bundles.end(),
                                         [&](const Bundle& x) { return x.id == id; }),
                          config_.bundles.end());
    persist();
    refresh_bundles();
  });
  brun->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { start_bundle(); });
  bundles_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { open_bundle_steps(); });
}

void AppFrame::show_journal() {
  if (!journal_window_) {
    journal_window_ = new JournalWindow(this, journal_, [this](const JournalEntry& e) {
      auto* s = config_.server_by_id(e.server_id);
      if (!s) {
        wxMessageBox(L"VPS из этой записи больше нет в списке.", L"Журнал", wxOK | wxICON_ERROR, this);
        return;
      }
      if (auto* c = config_.command_by_id(e.command_id)) {
        if (!confirm_saved_run(this, *c, s->name)) return;
        run_command(*s, e.command, e.timeout_sec, e.login_shell, e.title.empty() ? "журнал" : e.title, e.command_id,
                    e.kind, {}, c->working_dir, c->cd_before_run);
        return;
      }
      if (config_.settings.confirm_before_run) {
        if (wxMessageBox(L"Повторить команду из журнала?", L"Запуск", wxYES_NO, this) != wxYES) return;
      }
      run_command(*s, e.command, e.timeout_sec, e.login_shell, e.title.empty() ? "журнал" : e.title, e.command_id,
                  e.kind);
    });
    journal_window_->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& ev) {
      journal_window_ = nullptr;
      ev.Skip();
    });
  }
  journal_window_->Show();
  journal_window_->Raise();
}

void AppFrame::show_help(const std::string& tab) {
  if (!help_window_) {
    help_window_ = new HelpWindow(this, [this](const std::string& cmd) { quick_->SetValue(wxString::FromUTF8(cmd)); });
    help_window_->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& ev) {
      help_window_ = nullptr;
      ev.Skip();
    });
  }
  help_window_->Show();
  if (!tab.empty()) help_window_->show_tab(tab);
  help_window_->Raise();
}

void AppFrame::check_updates_interactive() { check_updates_async(true); }

void AppFrame::check_updates_async(bool interactive) {
  if (checking_updates_) {
    if (interactive) status_->SetLabel(L"Проверка обновлений уже идёт…");
    return;
  }
  checking_updates_ = true;
  if (interactive) status_->SetLabel(L"Проверка обновлений…");
  auto alive = alive_;
  std::thread([this, alive, interactive] {
    UpdateCheckResult r;
    std::string err;
    try {
      r = check_for_updates(resolve_version());
    } catch (const std::exception& exc) {
      err = exc.what();
    }
    // На главный поток идём через wxTheApp: если окно уже закрыто, alive == false
    // и мы не трогаем this.
    wxTheApp->CallAfter([this, alive, interactive, r, err] {
      if (!alive->load()) return;
      checking_updates_ = false;
      if (!err.empty()) {
        if (interactive) {
          auto text = wxString::FromUTF8(err) + L"\n\nОткрыть страницу релизов в браузере?";
          if (wxMessageBox(text, L"Обновления", wxYES_NO | wxICON_ERROR, this) == wxYES) {
            open_url(std::string("https://github.com/") + kGithubOwner + "/" + kGithubRepo + "/releases");
          }
        } else {
          status_->SetLabel(L"Готово");
        }
        return;
      }
      bool persist_needed = false;
      if (r.status == "update") {
        std::string latest = r.latest.value_or("");
        const bool already_skipped =
            !interactive && !latest.empty() && latest == config_.settings.skipped_update_version;
        if (already_skipped) {
          status_->SetLabel(L"Готово");
        } else {
          status_->SetLabel(wxString::FromUTF8("Доступна версия " + (latest.empty() ? "?" : latest)));
          UpdateAvailableDialog dlg(this, r.current, latest);
          int ans = dlg.ShowModal();
          if (ans == wxID_YES) {
            status_->SetLabel(L"Скачивание установщика…");
            checking_updates_ = true;
            auto dest = std::filesystem::temp_directory_path() /
                        ("FaTTY-" + (latest.empty() ? std::string("update") : latest) + "-Setup.exe");
            auto preferred = r.download_url;
            std::thread([this, alive, latest, preferred, dest] {
              std::string err;
              try {
                download_installer(latest, preferred, dest);
              } catch (const std::exception& exc) {
                err = exc.what();
              }
              wxTheApp->CallAfter([this, alive, dest, err] {
                if (!alive->load()) return;
                checking_updates_ = false;
                if (!err.empty()) {
                  auto text = wxString::FromUTF8(err) + L"\n\nОткрыть страницу релизов в браузере?";
                  if (wxMessageBox(text, L"Обновления", wxYES_NO | wxICON_ERROR, this) == wxYES) {
                    open_url(std::string("https://github.com/") + kGithubOwner + "/" + kGithubRepo + "/releases");
                  }
                  status_->SetLabel(L"Готово");
                  return;
                }
                status_->SetLabel(L"Запуск установщика…");
                open_path(dest);
              });
            }).detach();
          } else if (dlg.dont_remind() && !latest.empty()) {
            if (config_.settings.skipped_update_version != latest) {
              config_.settings.skipped_update_version = latest;
              persist_needed = true;
            }
          } else if (config_.settings.skipped_update_version == latest) {
            config_.settings.skipped_update_version.clear();
            persist_needed = true;
          }
        }
      } else if (interactive && r.status == "current") {
        wxMessageBox(L"У вас актуальная версия.", L"Обновления", wxOK | wxICON_INFORMATION, this);
      } else if (interactive) {
        wxMessageBox(L"На GitHub пока нет опубликованных релизов.", L"Обновления", wxOK | wxICON_INFORMATION, this);
      } else {
        status_->SetLabel(L"Готово");
      }
      if (!interactive) {
        config_.settings.last_update_check =
            std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        persist_needed = true;
      }
      if (persist_needed) persist();
    });
  }).detach();
}

void AppFrame::open_settings() {
  SettingsDialog dlg(this, config_, vault_,
                     [this] {
                       journal_->max_entries = config_.settings.journal_max_entries;
                       apply_ui_theme();
                       rebuild_extra_tools();
                       persist();
                     },
                     [this] {
                       ChangeMasterDialog d(this, vault_, config_.settings.allow_short_master_password);
                       d.setup_layout(&config_.settings, "change_master", false, [this] { persist(); });
                       if (d.ShowModal() == wxID_OK && d.ok) persist();
                     },
                     [this] { check_updates_interactive(); }, [this] { refresh_servers(); });
  dlg.setup_layout(&config_.settings, "settings", true, [this] { persist(); });
  dlg.ShowModal();
}

bool AppFrame::files_busy() const {
  for (const auto& pair : files_windows_) {
    if (pair.second && pair.second->is_busy()) return true;
  }
  return false;
}

void AppFrame::request_close_for_install() {
  closing_for_install_ = true;
  if (session_) session_->cancel();
  wxWindowList top = wxTopLevelWindows;
  for (wxWindow* w : top) {
    if (auto* dlg = wxDynamicCast(w, wxDialog); dlg && dlg->IsModal()) {
      dlg->EndModal(wxID_CANCEL);
    }
  }
  Close(true);
}

void AppFrame::persist() {
  config_.settings.window_geometry = window_geometry(this);
  config_.settings.window_state = IsMaximized() ? "zoomed" : "normal";
  if (hsplit_) config_.settings.sash_pos = hsplit_->GetSashPosition();
  if (vsplit_) config_.settings.vsash_pos = vsplit_->GetSashPosition();
  store_list_columns(servers_, config_.settings, "servers", server_column_ids());
  store_list_columns(commands_, config_.settings, "commands", command_column_ids());
  if (auto* s = selected_server()) {
    config_.settings.last_server_id = s->id;
    config_.settings.last_folder_by_server[s->id] = current_folder_id();
  }
  if (auto* c = selected_command()) config_.settings.last_command_id = c->id;
  try {
    save_config(config_, vault_);
    maybe_run_backup();
  } catch (...) {
  }
}

void AppFrame::rebuild_extra_tools() {
  if (!extra_tools_sizer_ || !extra_tools_parent_) return;
  const int gap = extra_tools_parent_->FromDIP(8);
  for (auto* w : extra_tool_btns_) {
    extra_tools_sizer_->Detach(w);
    w->Destroy();
  }
  extra_tool_btns_.clear();
  for (const auto& prog : config_.settings.extra_programs) {
    auto* btn = make_button(extra_tools_parent_, wxString::FromUTF8(prog.name), BtnIcon::App);
    extra_tools_sizer_->Add(btn, 0, wxRIGHT | wxBOTTOM, gap);
    extra_tool_btns_.push_back(btn);
    btn->Enable(!busy_);
    const std::string id = prog.id;
    btn->Bind(wxEVT_BUTTON, [this, id](wxCommandEvent&) {
      auto* s = selected_server();
      if (!s) return;
      ExtraProgram* prog = nullptr;
      for (auto& p : config_.settings.extra_programs) {
        if (p.id == id) {
          prog = &p;
          break;
        }
      }
      if (!prog) return;
      try {
        open_extra_program(*prog, *s);
        status_->SetLabel(wxString::FromUTF8(prog->name + " открыт → " + s->name));
      } catch (const ExternalProgramNotFoundError&) {
        wxFileDialog dlg(this, wxString::FromUTF8(prog->name), L"", L"", L"EXE|*.exe|Все|*.*",
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK) {
          prog->path = std::string(dlg.GetPath().utf8_string());
          persist();
          try {
            open_extra_program(*prog, *s);
          } catch (const std::exception& exc) {
            wxMessageBox(wxString::FromUTF8(exc.what()), wxString::FromUTF8(prog->name), wxOK | wxICON_ERROR, this);
          }
        }
      } catch (const std::exception& exc) {
        wxMessageBox(wxString::FromUTF8(exc.what()), wxString::FromUTF8(prog->name), wxOK | wxICON_ERROR, this);
      }
    });
  }
  extra_tools_parent_->Layout();
  if (auto* parent = extra_tools_parent_->GetParent()) parent->Layout();
}

void AppFrame::maybe_run_backup() {
  try {
    auto result = maybe_backup_config(config_.settings);
    if (result.made) {
      save_config(config_, vault_);
    }
  } catch (...) {
  }
}

void AppFrame::refresh_servers(const std::string& keep_id) {
  std::string keep = keep_id;
  if (keep.empty()) {
    if (auto* s = selected_server()) keep = s->id;
    else keep = config_.settings.last_server_id;
  }
  servers_->DeleteAllItems();
  const std::string q = to_lower(server_filter_);
  long sel = -1;
  long row = 0;
  for (std::size_t i = 0; i < config_.servers.size(); ++i) {
    const auto& s = config_.servers[i];
    if (!q.empty()) {
      auto hay = to_lower(s.name + " " + s.username + " " + s.host);
      if (hay.find(q) == std::string::npos) continue;
    }
    // Данные строки — индекс в config_.servers; selected_server() читает именно его,
    // поэтому фильтрация не ломает соответствие строки и сервера.
    servers_->InsertItem(row, L"");
    fill_row(servers_, row, server_column_ids(),
             {{"name", wxString::FromUTF8(s.name)},
              {"host", wxString::FromUTF8(s.username + "@" + s.host + ":" + std::to_string(s.port))}});
    servers_->SetItemPtrData(row, static_cast<wxUIntPtr>(i));
    style_list_row(servers_, row, Theme::text());
    if (s.id == keep) sel = row;
    ++row;
  }
  if (sel < 0 && servers_->GetItemCount() > 0) sel = 0;
  if (sel >= 0) servers_->SetItemState(sel, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
  rebuild_folder_tabs();
  refresh_commands();
}

void AppFrame::restore_columns() {
  setup_server_columns();
  setup_command_columns();
  refresh_servers(config_.settings.last_server_id);
}

void AppFrame::apply_ui_theme() {
  set_theme(config_.settings.theme);
  apply_theme(this);
  style_list(servers_);
  style_list(commands_);
  if (bundles_) style_list(bundles_);
  if (journal_window_) apply_theme(journal_window_);
  if (help_window_) apply_theme(help_window_);
  if (bundle_steps_window_) apply_theme(bundle_steps_window_);
  for (auto& [id, win] : files_windows_) {
    if (win) apply_theme(win);
  }
  setup_server_columns();
  setup_command_columns();
  refresh_servers();
}

std::string AppFrame::current_folder_id() const {
  if (!folders_nb_) return {};
  int sel = folders_nb_->GetSelection();
  if (sel < 0 || sel >= static_cast<int>(folder_tab_ids_.size())) return {};
  return folder_tab_ids_[static_cast<std::size_t>(sel)];
}

void AppFrame::attach_commands_page(int index) {
  if (!folders_nb_ || !commands_ || index < 0 || index >= static_cast<int>(folders_nb_->GetPageCount())) return;
  auto* page = folders_nb_->GetPage(static_cast<std::size_t>(index));
  if (!page) return;
  commands_->Reparent(page);
  style_list(commands_);
  if (!page->GetSizer()) {
    page->SetSizer(new wxBoxSizer(wxVERTICAL));
  }
  page->GetSizer()->Clear(false);
  page->GetSizer()->Add(commands_, 1, wxEXPAND);
  page->Layout();
  folders_nb_->Layout();
}

void AppFrame::rebuild_folder_tabs() {
  if (!folders_nb_ || !commands_) return;
  updating_folders_ = true;
  std::string keep;
  if (auto* s = selected_server()) {
    auto it = config_.settings.last_folder_by_server.find(s->id);
    if (it != config_.settings.last_folder_by_server.end()) keep = it->second;
  }
  commands_->Reparent(folders_nb_);
  while (folders_nb_->GetPageCount() > 0) {
    folders_nb_->DeletePage(0);
  }
  folder_tab_ids_.clear();
  auto add_page = [&](const wxString& title, const std::string& id) {
    auto* page = new wxPanel(folders_nb_);
    page->SetName(L"card-page");
    page->SetBackgroundColour(Theme::elevated());
    page->SetForegroundColour(Theme::text());
    page->SetSizer(new wxBoxSizer(wxVERTICAL));
    folders_nb_->AddPage(page, title);
    folder_tab_ids_.push_back(id);
  };
  add_page(L"Общее", "");
  if (auto* s = selected_server()) {
    for (const auto& f : config_.folders_for(s->id)) {
      add_page(wxString::FromUTF8(f.name), f.id);
    }
  }
  int sel = 0;
  for (std::size_t i = 0; i < folder_tab_ids_.size(); ++i) {
    if (folder_tab_ids_[i] == keep) sel = static_cast<int>(i);
  }
  attach_commands_page(sel);
  folders_nb_->SetSelection(sel);
  updating_folders_ = false;
}

void AppFrame::refresh_commands() {
  commands_->DeleteAllItems();
  auto* s = selected_server();
  if (!s) {
    update_cwd_label();
    refresh_bundles();
    return;
  }
  auto cmds = config_.commands_for(s->id, current_folder_id());
  long sel = -1;
  for (std::size_t i = 0; i < cmds.size(); ++i) {
    const auto& c = cmds[i];
    auto preview = c.command;
    if (preview.size() > 90) preview = preview.substr(0, 87) + "…";
    auto comment = c.comment;
    if (comment.size() > 60) comment = comment.substr(0, 57) + "…";
    long row = commands_->InsertItem(static_cast<long>(i), L"");
    wxString last = L"—";
    wxString avg = L"—";
    wxColour colour = Theme::text();
    auto it = command_stats_.find(c.id);
    if (it != command_stats_.end()) {
      last = wxString::FromUTF8(it->second.latest.last_run_label());
      colour = Theme::run_status(it->second.latest.status);
      if (it->second.run_count > 0) {
        avg = wxString::FromUTF8(format_duration(it->second.average_sec));
      }
    }
    fill_row(commands_, row, command_column_ids(),
             {{"name", wxString::FromUTF8(c.name)},
              {"comment", wxString::FromUTF8(comment)},
              {"folder", wxString::FromUTF8(c.cd_before_run && !c.working_dir.empty() ? c.working_dir : "—")},
              {"command", wxString::FromUTF8(preview)},
              {"last", last},
              {"avg", avg}});
    style_list_row(commands_, row, colour);
    if (c.id == config_.settings.last_command_id) sel = row;
  }
  if (sel >= 0) {
    commands_->SetItemState(sel, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                            wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
  }
  update_cwd_label();
  refresh_bundles();
}

std::vector<std::string> AppFrame::command_column_ids() const {
  std::vector<std::string> available{"name", "command", "comment", "last", "avg"};
  if (config_.settings.show_command_folder_column) {
    available = {"folder", "name", "command", "comment", "last", "avg"};
  }
  auto it = config_.settings.column_order.find("commands");
  if (it == config_.settings.column_order.end()) return available;
  const auto& saved = it->second;
  static const std::vector<std::string> old_default_with_folder{"name", "comment", "folder", "command", "last"};
  static const std::vector<std::string> old_default_no_folder{"name", "comment", "command", "last"};
  if (saved == old_default_with_folder || saved == old_default_no_folder) return available;
  return prefer_order(available, saved);
}

std::vector<std::string> AppFrame::server_column_ids() const {
  std::vector<std::string> available{"name", "host"};
  auto it = config_.settings.column_order.find("servers");
  if (it == config_.settings.column_order.end()) return available;
  return prefer_order(available, it->second);
}

void AppFrame::setup_server_columns() {
  if (!servers_) return;
  servers_->DeleteAllItems();
  while (servers_->GetColumnCount() > 0) {
    servers_->DeleteColumn(0);
  }
  const auto ids = server_column_ids();
  for (const auto& id : ids) {
    if (id == "host") {
      servers_->AppendColumn(L"Адрес", wxLIST_FORMAT_LEFT, FromDIP(220));
    } else {
      servers_->AppendColumn(L"Имя", wxLIST_FORMAT_LEFT, FromDIP(160));
    }
  }
  auto it = config_.settings.column_widths.find("servers");
  if (it != config_.settings.column_widths.end()) {
    apply_list_columns(servers_, it->second, ids);
  }
}

void AppFrame::setup_command_columns() {
  if (!commands_) return;
  commands_->DeleteAllItems();
  while (commands_->GetColumnCount() > 0) {
    commands_->DeleteColumn(0);
  }
  const auto ids = command_column_ids();
  for (const auto& id : ids) {
    if (id == "comment") {
      commands_->AppendColumn(L"Комментарий", wxLIST_FORMAT_LEFT, FromDIP(180));
    } else if (id == "folder") {
      commands_->AppendColumn(L"Папка", wxLIST_FORMAT_LEFT, FromDIP(180));
    } else if (id == "command") {
      commands_->AppendColumn(L"Команда", wxLIST_FORMAT_LEFT, FromDIP(320));
    } else if (id == "last") {
      commands_->AppendColumn(L"Последний раз", wxLIST_FORMAT_LEFT, FromDIP(140));
    } else if (id == "avg") {
      commands_->AppendColumn(L"Ср. время", wxLIST_FORMAT_LEFT, FromDIP(100));
    } else {
      commands_->AppendColumn(L"Название", wxLIST_FORMAT_LEFT, FromDIP(160));
    }
  }
  auto it = config_.settings.column_widths.find("commands");
  if (it != config_.settings.column_widths.end()) {
    apply_list_columns(commands_, it->second, ids);
  }
}

Server* AppFrame::selected_server() {
  long row = servers_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (row < 0) return nullptr;
  auto idx = static_cast<std::size_t>(servers_->GetItemData(row));
  if (idx >= config_.servers.size()) return nullptr;
  return &config_.servers[idx];
}

std::vector<Command*> AppFrame::selected_commands() {
  std::vector<Command*> out;
  auto* s = selected_server();
  if (!s || !commands_) return out;
  auto cmds = config_.commands_for(s->id, current_folder_id());
  long i = -1;
  while ((i = commands_->GetNextItem(i, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != wxNOT_FOUND) {
    if (i < 0 || i >= static_cast<long>(cmds.size())) continue;
    if (auto* c = config_.command_by_id(cmds[static_cast<std::size_t>(i)].id)) {
      out.push_back(c);
    }
  }
  return out;
}

Command* AppFrame::selected_command() {
  auto sel = selected_commands();
  return sel.empty() ? nullptr : sel.front();
}

Bundle* AppFrame::selected_bundle() {
  if (!bundles_) return nullptr;
  auto* s = selected_server();
  if (!s) return nullptr;
  long row = bundles_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (row < 0) return nullptr;
  auto list = config_.bundles_for(s->id);
  if (row >= static_cast<long>(list.size())) return nullptr;
  return config_.bundle_by_id(list[static_cast<std::size_t>(row)].id);
}

void AppFrame::refresh_bundles() {
  if (!bundles_) return;
  bundles_->DeleteAllItems();
  auto* s = selected_server();
  if (!s) return;
  auto list = config_.bundles_for(s->id);
  for (std::size_t i = 0; i < list.size(); ++i) {
    const auto& b = list[i];
    int n = 0;
    for (const auto& cid : b.command_ids) {
      if (config_.command_by_id(cid)) ++n;
    }
    long row = bundles_->InsertItem(static_cast<long>(i), wxString::FromUTF8(b.name));
    bundles_->SetItem(row, 1, wxString::FromUTF8(std::to_string(n)));
    bundles_->SetItem(row, 2, wxString::FromUTF8(std::to_string(b.interval_sec) + " с"));
    style_list_row(bundles_, row, Theme::text());
  }
}

void AppFrame::append_output(const std::string& text, const wxColour* colour) {
  output_->SetDefaultStyle(wxTextAttr(colour ? *colour : Theme::text()));
  output_->AppendText(wxString::FromUTF8(text));
}

void AppFrame::set_busy(bool busy) {
  busy_ = busy;
  stop_btn_->Enable(busy);
  if (bundles_stop_btn_) bundles_stop_btn_->Enable(busy);
  for (wxWindow* w : busy_disable_) {
    if (w) w->Enable(!busy);
  }
  for (wxWindow* w : extra_tool_btns_) {
    if (w) w->Enable(!busy);
  }
  if (busy) {
    run_start_ = std::chrono::steady_clock::now();
    busy_gauge_->Show();
    busy_gauge_->Pulse();
    busy_timer_.Start(250);
  } else {
    busy_timer_.Stop();
    busy_gauge_->Hide();
    if (auto* sizer = busy_gauge_->GetContainingSizer()) sizer->Layout();
  }
}

void AppFrame::update_busy_indicator() {
  if (!busy_) return;
  if (bundle_waiting_) {
    if (bundle_cancel_) {
      bundle_waiting_ = false;
      finish_bundle("прервано");
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= bundle_wait_until_) {
      bundle_waiting_ = false;
      run_bundle_step();
      return;
    }
    auto left_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(bundle_wait_until_ - now).count();
    const int left_sec = static_cast<int>((left_ms + 999) / 1000);
    busy_label_ = "Связка «" + bundle_name_ + "»: пауза " + std::to_string(std::max(1, left_sec)) + " с до " +
                  std::to_string(bundle_index_ + 1) + "/" + std::to_string(bundle_cmds_.size());
  }
  busy_gauge_->Pulse();
  auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - run_start_).count();
  status_->SetLabel(wxString::FromUTF8(busy_label_ + "  •  " + std::to_string(secs) + " с" + queue_suffix()));
}

void AppFrame::update_cwd_label() {
  auto* s = selected_server();
  if (!s) {
    cwd_label_->SetLabel(L"");
    cwd_reset_->Enable(false);
    return;
  }
  auto it = remote_cwd_.find(s->id);
  if (it != remote_cwd_.end() && !it->second.empty()) {
    cwd_label_->SetLabel(wxString::FromUTF8("Папка: " + it->second));
    cwd_reset_->Enable(true);
  } else {
    cwd_label_->SetLabel(L"Папка: ~");
    cwd_reset_->Enable(false);
  }
}

void AppFrame::request_saved_runs() {
  auto* s = selected_server();
  auto cmds = selected_commands();
  if (!s || cmds.empty()) return;
  for (auto* c : cmds) {
    if (!c) continue;
    if (!confirm_saved_run(this, *c, s->name)) continue;
    run_command(*s, c->command, c->timeout_sec, c->login_shell, c->name, c->id, "command", {}, c->working_dir,
                c->cd_before_run);
  }
}

std::string AppFrame::queue_suffix() const {
  if (run_queue_.empty()) return {};
  return "  •  очередь: " + std::to_string(run_queue_.size());
}

void AppFrame::clear_run_queue() {
  run_queue_.clear();
}

void AppFrame::pump_run_queue() {
  if (bundle_active_) return;
  if (run_queue_.empty()) {
    set_busy(false);
    return;
  }
  auto job = std::move(run_queue_.front());
  run_queue_.pop_front();
  start_ssh_run(std::move(job.server), std::move(job.command), job.timeout, job.login_shell, std::move(job.title),
                std::move(job.command_id), std::move(job.kind), {}, std::move(job.working_dir), job.cd_before_run);
}

void AppFrame::run_command(const Server& server, const std::string& command, int timeout, bool login_shell,
                           const std::string& title, const std::string& command_id, const std::string& kind,
                           std::function<void(int code, std::string status)> on_done, std::string working_dir,
                           bool cd_before_run) {
  const bool chained = static_cast<bool>(on_done);
  if (busy_ && !chained) {
    run_queue_.push_back({server, command, timeout, login_shell, title, command_id, kind, std::move(working_dir),
                          cd_before_run});
    const wxColour meta = Theme::meta();
    append_output("В очередь: " + title + "  •  " + server.name + " (" + std::to_string(run_queue_.size()) + ")\n",
                  &meta);
    status_->SetLabel(wxString::FromUTF8(busy_label_ + queue_suffix()));
    return;
  }
  start_ssh_run(server, command, timeout, login_shell, title, command_id, kind, std::move(on_done),
                std::move(working_dir), cd_before_run);
}

void AppFrame::start_ssh_run(Server server, std::string command, int timeout, bool login_shell, std::string title,
                             std::string command_id, std::string kind,
                             std::function<void(int code, std::string status)> on_done, std::string working_dir,
                             bool cd_before_run) {
  const bool chained = static_cast<bool>(on_done);
  if (config_.settings.clear_output_before_run && !chained && !busy_) output_->Clear();
  busy_label_ = "Выполняется: " + title + " → " + server.name;
  if (!chained) set_busy(true);
  std::string cwd = remote_cwd_[server.id];
  if (cd_before_run) {
    auto dir = trim(working_dir);
    if (!dir.empty()) cwd = dir;
  }
  status_->SetLabel(wxString::FromUTF8(busy_label_ + queue_suffix()));
  const wxColour meta = Theme::meta();
  append_output("\n" + std::string(60, '-') + "\n", &meta);
  append_output(title + "  •  " + server.name + "\n", &meta);
  session_ = std::make_shared<SSHSession>();
  auto started = now_iso();
  auto t0 = std::chrono::steady_clock::now();
  Server srv = std::move(server);
  // Поток держит собственные копии shared_ptr на сессию и журнал, а к окну
  // обращается только через живой-токен: закрытие FaTTY во время выполнения
  // больше не оставляет поток работать по разрушенному AppFrame.
  auto alive = alive_;
  auto session = session_;
  auto journal = journal_;
  auto running = worker_running_;
  running->store(true);
  std::thread([this, alive, session, journal, running, srv, command = std::move(command), timeout, login_shell,
               title = std::move(title), command_id = std::move(command_id), kind = std::move(kind), cwd, started, t0,
               on_done = std::move(on_done)] {
    struct RunningGuard {
      std::shared_ptr<std::atomic<bool>> flag;
      ~RunningGuard() { flag->store(false); }
    } guard{running};
    // Токен проверяется ДО обращения к wxTheApp: после закрытия окна поток не
    // трогает wx вообще, даже если приложение уже сворачивается.
    auto post = [alive](std::function<void()> fn) {
      if (!alive->load()) return;
      wxTheApp->CallAfter([alive, fn = std::move(fn)] {
        if (!alive->load()) return;
        fn();
      });
    };
    int code = 1;
    std::string status = "error";
    std::string error;
    std::string new_cwd = cwd;
    std::string captured;
    captured.reserve(64 * 1024);
    bool truncated = false;
    try {
      auto result = session->run(srv, command, timeout, login_shell,
                                 [this, post, &captured, &truncated](const std::string& chunk) {
                                   captured.append(chunk);
                                   if (captured.size() > kJournalOutputMax) {
                                     truncated = true;
                                     captured.erase(0, captured.size() - kJournalOutputMax);
                                   }
                                   post([this, chunk] { append_output(chunk); });
                                 },
                                 cwd);
      code = result.exit_code;
      status = status_from_exit(code);
      if (!result.cwd.empty()) new_cwd = result.cwd;
      auto col = code == 0 ? Theme::ok() : Theme::err();
      post([this, code, col] { append_output("\n← код выхода " + std::to_string(code) + "\n", &col); });
    } catch (const std::exception& exc) {
      error = exc.what();
      const wxColour err = Theme::err();
      post([this, error, err] { append_output("\n" + error + "\n", &err); });
    }
    auto duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    // Пишем в журнал прямо здесь: запись переживает закрытие окна, поэтому
    // прерванная выходом команда всё равно попадает в историю.
    JournalEntry e;
    e.started_at = started;
    e.finished_at = now_iso();
    e.duration_sec = duration;
    e.server_id = srv.id;
    e.server_name = srv.name;
    e.host = srv.host;
    e.port = srv.port;
    e.username = srv.username;
    e.command_id = command_id;
    e.title = title;
    e.command = command;
    e.cwd = new_cwd;
    e.login_shell = login_shell;
    e.timeout_sec = timeout;
    if (error.empty()) e.exit_code = code;
    e.status = status;
    e.kind = kind;
    e.error = error;
    e.output = truncated ? ("…\n" + captured) : captured;
    journal->append(e);
    post([this, srv, title, code, status, error, new_cwd, on_done] {
      if (!new_cwd.empty()) remote_cwd_[srv.id] = new_cwd;
      update_cwd_label();
      session_.reset();
      if (on_done) {
        on_done(code, status);
        return;
      }
      if (!run_queue_.empty()) {
        status_->SetLabel(wxString::FromUTF8(
            "Готово  •  код " +
            (status == "error" && code == 1 && !error.empty() ? std::string("—") : std::to_string(code)) + "  •  " +
            title + queue_suffix()));
        pump_run_queue();
        return;
      }
      set_busy(false);
      status_->SetLabel(wxString::FromUTF8(
          "Готово  •  код " + (status == "error" && code == 1 && !error.empty() ? std::string("—") : std::to_string(code)) +
          "  •  " + title));
    });
  }).detach();
}

void AppFrame::open_bundle_steps() {
  auto* s = selected_server();
  auto* b = selected_bundle();
  if (!s || !b) {
    wxMessageBox(L"Выберите VPS и связку.", L"Связка");
    return;
  }
  int n = 0;
  for (const auto& cid : b->command_ids) {
    if (config_.command_by_id(cid)) ++n;
  }
  if (n == 0) {
    wxMessageBox(L"В связке нет доступных команд (их удалили?).", L"Связка");
    return;
  }
  if (!bundle_steps_window_) {
    bundle_steps_window_ = new BundleStepsWindow(this, config_, [this](const Command& cmd) {
      auto* live = config_.command_by_id(cmd.id);
      const Command* c = live ? live : &cmd;
      auto* srv = config_.server_by_id(c->server_id);
      if (!srv) {
        wxMessageBox(L"VPS этой команды больше нет в списке.", L"Связка", wxOK | wxICON_ERROR,
                     bundle_steps_window_ ? static_cast<wxWindow*>(bundle_steps_window_) : this);
        return;
      }
      wxWindow* parent = bundle_steps_window_ ? static_cast<wxWindow*>(bundle_steps_window_) : this;
      if (!confirm_saved_run(parent, *c, srv->name)) return;
      run_command(*srv, c->command, c->timeout_sec, c->login_shell, c->name, c->id, "command", {}, c->working_dir,
                  c->cd_before_run);
    });
    bundle_steps_window_->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& ev) {
      bundle_steps_window_ = nullptr;
      ev.Skip();
    });
  }
  bundle_steps_window_->show_bundle(b->id);
}

void AppFrame::start_bundle() {
  auto* s = selected_server();
  auto* b = selected_bundle();
  if (!s || !b) {
    wxMessageBox(L"Выберите VPS и связку.", L"Связка");
    return;
  }
  std::vector<Command> cmds;
  for (const auto& cid : b->command_ids) {
    if (auto* c = config_.command_by_id(cid)) cmds.push_back(*c);
  }
  if (cmds.empty()) {
    wxMessageBox(L"В связке нет доступных команд (их удалили?).", L"Связка");
    return;
  }
  long pause = wxGetNumberFromUser(
      L"Пауза между командами, секунд.\n0 — запускать следующую сразу после предыдущей.\nСтоп прервёт очередь.",
      L"Интервал", L"Запуск связки", b->interval_sec, 0, 3600, this);
  if (pause < 0) return;
  if (pause != b->interval_sec) {
    b->interval_sec = static_cast<int>(pause);
    persist();
    refresh_bundles();
  }
  auto msg = wxString::Format(L"Запустить связку «%s»?\n%d команд, пауза %d с.", wxString::FromUTF8(b->name),
                              static_cast<int>(cmds.size()), static_cast<int>(pause));
  if (wxMessageBox(msg, L"Связка", wxYES_NO, this) != wxYES) return;
  if (busy_) {
    for (const auto& c : cmds) {
      run_command(*s, c.command, c.timeout_sec, c.login_shell, c.name, c.id, "command", {}, c.working_dir,
                  c.cd_before_run);
    }
    return;
  }
  if (config_.settings.clear_output_before_run) output_->Clear();
  bundle_active_ = true;
  bundle_cancel_ = false;
  bundle_index_ = 0;
  bundle_interval_sec_ = static_cast<int>(pause);
  bundle_name_ = b->name;
  bundle_server_ = *s;
  bundle_cmds_ = std::move(cmds);
  busy_label_ = "Связка «" + bundle_name_ + "»";
  set_busy(true);
  const wxColour meta = Theme::meta();
  append_output("\n" + std::string(60, '=') + "\n", &meta);
  append_output("Связка «" + bundle_name_ + "»  •  " + std::to_string(bundle_cmds_.size()) + " команд, пауза " +
                    std::to_string(bundle_interval_sec_) + " с\n",
                &meta);
  run_bundle_step();
}

void AppFrame::run_bundle_step() {
  if (!bundle_active_) return;
  if (bundle_cancel_ || bundle_index_ >= static_cast<int>(bundle_cmds_.size())) {
    finish_bundle(bundle_cancel_ ? "прервано" : "готово");
    return;
  }
  const auto& c = bundle_cmds_[static_cast<std::size_t>(bundle_index_)];
  busy_label_ = "Связка «" + bundle_name_ + "» (" + std::to_string(bundle_index_ + 1) + "/" +
                std::to_string(bundle_cmds_.size()) + "): " + c.name;
  status_->SetLabel(wxString::FromUTF8(busy_label_));
  run_command(bundle_server_, c.command, c.timeout_sec, c.login_shell, c.name, c.id, "command",
              [this](int code, std::string status) {
                if (!bundle_active_) return;
                if (bundle_cancel_ || status == "cancelled") {
                  finish_bundle("прервано");
                  return;
                }
                if (status != "ok") {
                  const wxColour err = Theme::err();
                  append_output("\nСвязка остановлена: команда завершилась с ошибкой (код " + std::to_string(code) +
                                    ").\n",
                                &err);
                  finish_bundle("ошибка");
                  return;
                }
                ++bundle_index_;
                if (bundle_index_ >= static_cast<int>(bundle_cmds_.size())) {
                  finish_bundle("готово");
                  return;
                }
                schedule_bundle_wait();
              },
              c.working_dir, c.cd_before_run);
}

void AppFrame::schedule_bundle_wait() {
  if (!bundle_active_) return;
  if (bundle_cancel_) {
    finish_bundle("прервано");
    return;
  }
  if (bundle_interval_sec_ <= 0) {
    run_bundle_step();
    return;
  }
  bundle_waiting_ = true;
  bundle_wait_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(bundle_interval_sec_);
  busy_label_ = "Связка «" + bundle_name_ + "»: пауза " + std::to_string(bundle_interval_sec_) + " с до " +
                std::to_string(bundle_index_ + 1) + "/" + std::to_string(bundle_cmds_.size());
  status_->SetLabel(wxString::FromUTF8(busy_label_ + queue_suffix()));
}

void AppFrame::finish_bundle(const std::string& reason) {
  bundle_waiting_ = false;
  bundle_active_ = false;
  bundle_cancel_ = false;
  bundle_cmds_.clear();
  const wxColour meta = Theme::meta();
  append_output("Связка «" + bundle_name_ + "»: " + reason + "\n", &meta);
  if (!run_queue_.empty() && reason != "прервано") {
    status_->SetLabel(wxString::FromUTF8("Связка «" + bundle_name_ + "»: " + reason + queue_suffix()));
    pump_run_queue();
    return;
  }
  set_busy(false);
  status_->SetLabel(wxString::FromUTF8("Связка «" + bundle_name_ + "»: " + reason));
}

}  // namespace fatty

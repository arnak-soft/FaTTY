#include "ui/app_frame.hpp"

#include "app/single_instance.hpp"
#include "app/version.hpp"
#include "core/config_io.hpp"
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
#include "ui/layout.hpp"
#include "ui/settings_dialog.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/bookctrl.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <algorithm>
#include <chrono>
#include <thread>

namespace fatty {

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
  last_runs_ = journal_->latest_by_command_id();
  // Слушателя может дёрнуть фоновый поток команды, поэтому на главный поток
  // возвращаемся через wxTheApp и проверяем живой-токен.
  journal_->add_listener([this, alive = alive_, journal = journal_] {
    if (!alive->load()) return;
    wxTheApp->CallAfter([this, alive, journal] {
      if (!alive->load()) return;
      last_runs_ = journal->latest_by_command_id();
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
    if (busy_) {
      if (wxMessageBox("Команда ещё выполняется. Выйти?", "Выход", wxYES_NO, this) != wxYES) {
        e.Veto();
        return;
      }
      if (session_) session_->cancel();
    }
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
      if (auto* c = selected_command()) {
        auto* s = selected_server();
        if (s) {
          if (config_.settings.confirm_before_run &&
              wxMessageBox("Выполнить «" + wxString::FromUTF8(c->name) + "»?", "Запуск", wxYES_NO, this) != wxYES)
            return;
          run_command(*s, c->command, c->timeout_sec, c->login_shell, c->name, c->id, "command");
        }
      }
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
}

void AppFrame::build_menu() {
  auto* bar = new wxMenuBar();
  auto* file = new wxMenu();
  file->Append(1001, "Открыть папку конфига");
  file->Append(1002, "Журнал команд…\tCtrl+J");
  file->AppendSeparator();
  file->Append(1003, "Экспорт…");
  file->Append(1004, "Импорт…");
  file->AppendSeparator();
  file->Append(wxID_EXIT, "Выход");
  bar->Append(file, "Файл");
  auto* opt = new wxMenu();
  opt->Append(1010, "Настройки…\tCtrl+,");
  opt->AppendSeparator();
  opt->Append(1011, "Сменить мастер-пароль…");
  bar->Append(opt, "Настройки");
  auto* help = new wxMenu();
  help->Append(1020, "Содержание\tF1");
  help->Append(1021, "Частые команды");
  help->AppendSeparator();
  help->Append(1022, "Проверить обновления…");
  help->AppendSeparator();
  help->Append(wxID_ABOUT, "О программе");
  bar->Append(help, "Справка");
  SetMenuBar(bar);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { open_directory(app_dir()); }, 1001);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { show_journal(); }, 1002);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    bool secrets = wxMessageBox("Включить пароли VPS в файл?", "Экспорт", wxYES_NO, this) == wxYES;
    if (secrets && wxMessageBox("Файл будет содержать пароли в открытом виде. Продолжить?", "Экспорт", wxYES_NO, this) !=
                       wxYES)
      return;
    bool settings = wxMessageBox("Включить настройки приложения?", "Экспорт", wxYES_NO, this) == wxYES;
    wxFileDialog dlg(this, "Экспорт FaTTY", "", "fatty-backup.json", "JSON|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;
    try {
      write_export(std::filesystem::path(dlg.GetPath().utf8_string()), config_, secrets, settings);
      wxMessageBox("Сохранено.", "Экспорт", wxOK);
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), "Экспорт", wxOK | wxICON_ERROR);
    }
  }, 1003);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    int choice = wxMessageBox("Да — добавить\nНет — заменить\nОтмена", "Импорт", wxYES_NO | wxCANCEL, this);
    if (choice == wxCANCEL) return;
    wxFileDialog dlg(this, "Импорт", "", "", "JSON|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;
    try {
      auto data = read_export(std::filesystem::path(dlg.GetPath().utf8_string()));
      auto result = import_into_config(config_, data, choice == wxYES ? "merge" : "replace", true);
      persist();
      refresh_servers();
      wxMessageBox(wxString::FromUTF8(format_import_summary(result, choice == wxYES ? "merge" : "replace")), "Импорт");
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), "Импорт", wxOK | wxICON_ERROR);
    }
  }, 1004);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { open_settings(); }, 1010);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    ChangeMasterDialog d(this, vault_, config_.settings.allow_short_master_password);
    d.setup_layout(&config_.settings, "change_master", false, [this] { persist(); });
    if (d.ShowModal() == wxID_OK && d.ok) {
      persist();
      wxMessageBox("Мастер-пароль обновлён.", "Мастер-пароль");
    }
  }, 1011);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { show_help(); }, 1020);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { show_help("Команды"); }, 1021);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { check_updates_interactive(); }, 1022);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    wxMessageBox(wxString::FromUTF8(std::string(kAppName) + " " + resolve_version() +
                                    "\n\nЗапуск команд на VPS по SSH.\n\nСправка: F1"),
                 "О программе");
  }, wxID_ABOUT);
}

void AppFrame::build_ui() {
  const int pad = FromDIP(10);
  const int gap = FromDIP(6);
  auto* panel = new wxPanel(this);
  vsplit_ = new wxSplitterWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
  auto* top = new wxPanel(vsplit_);
  hsplit_ = new wxSplitterWindow(top, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
  auto* left = new wxPanel(hsplit_);
  auto* right = new wxPanel(hsplit_);
  server_search_ = new wxTextCtrl(left, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
#if wxCHECK_VERSION(3, 1, 0)
  server_search_->SetHint("Поиск VPS…");
#endif
  servers_ = new wxListCtrl(left, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  servers_->AppendColumn("Имя", wxLIST_FORMAT_LEFT, FromDIP(160));
  servers_->AppendColumn("Адрес", wxLIST_FORMAT_LEFT, FromDIP(220));
  auto* sbtns = new wxBoxSizer(wxHORIZONTAL);
  auto* sadd = new wxButton(left, wxID_ANY, "Добавить");
  auto* sedit = new wxButton(left, wxID_ANY, "Изменить");
  auto* sdup = new wxButton(left, wxID_ANY, "Дублировать");
  auto* sdel = new wxButton(left, wxID_ANY, "Удалить");
  sbtns->Add(sadd, 0, wxRIGHT, gap);
  sbtns->Add(sedit, 0, wxRIGHT, gap);
  sbtns->Add(sdup, 0, wxRIGHT, gap);
  sbtns->Add(sdel);
  auto* sact = new wxBoxSizer(wxHORIZONTAL);
  auto* files = new wxButton(left, wxID_ANY, "Файлы");
  auto* cons = new wxButton(left, wxID_ANY, "Открыть консоль");
  auto* putty = new wxButton(left, wxID_ANY, "PuTTY");
  auto* test = new wxButton(left, wxID_ANY, "Проверить связь");
  sact->Add(files, 0, wxRIGHT, gap);
  sact->Add(cons, 0, wxRIGHT, gap);
  sact->Add(putty, 0, wxRIGHT, gap);
  sact->Add(test);
  auto* ls = new wxBoxSizer(wxVERTICAL);
  ls->Add(section_label(left, "VPS-серверы"), 0, wxBOTTOM, gap);
  ls->Add(server_search_, 0, wxEXPAND | wxBOTTOM, gap);
  ls->Add(servers_, 1, wxEXPAND);
  ls->Add(sbtns, 0, wxTOP, pad);
  ls->Add(sact, 0, wxTOP, gap);
  left->SetSizer(ls);
  // Кнопки редактирования VPS/команд гасятся на время выполнения команды.
  busy_disable_ = {sedit, sdup, sdel, cons, putty, test, files};

  folders_nb_ = new wxNotebook(right, wxID_ANY);
  auto* first_page = new wxPanel(folders_nb_);
  auto* page_sz = new wxBoxSizer(wxVERTICAL);
  commands_ = new wxListCtrl(first_page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  commands_->AppendColumn("Название", wxLIST_FORMAT_LEFT, FromDIP(180));
  commands_->AppendColumn("Команда", wxLIST_FORMAT_LEFT, FromDIP(320));
  commands_->AppendColumn("Последний раз", wxLIST_FORMAT_LEFT, FromDIP(140));
  page_sz->Add(commands_, 1, wxEXPAND);
  first_page->SetSizer(page_sz);
  folders_nb_->AddPage(first_page, "Общее");
  folder_tab_ids_.push_back("");
  auto* corder = new wxBoxSizer(wxHORIZONTAL);
  auto* up = new wxButton(right, wxID_ANY, "Вверх");
  auto* down = new wxButton(right, wxID_ANY, "Вниз");
  auto* byname = new wxButton(right, wxID_ANY, "По имени");
  auto* fadd = new wxButton(right, wxID_ANY, "Папка+");
  auto* frename = new wxButton(right, wxID_ANY, "Переименовать");
  auto* fdel = new wxButton(right, wxID_ANY, "Удалить папку");
  corder->Add(up, 0, wxRIGHT, gap);
  corder->Add(down, 0, wxRIGHT, gap);
  corder->Add(byname, 0, wxRIGHT, FromDIP(16));
  corder->Add(fadd, 0, wxRIGHT, gap);
  corder->Add(frename, 0, wxRIGHT, gap);
  corder->Add(fdel);
  auto* cbtns = new wxBoxSizer(wxHORIZONTAL);
  auto* cadd = new wxButton(right, wxID_ANY, "Добавить");
  auto* cedit = new wxButton(right, wxID_ANY, "Изменить  (F2)");
  auto* cdup = new wxButton(right, wxID_ANY, "Дублировать");
  auto* cdel = new wxButton(right, wxID_ANY, "Удалить");
  auto* presets = new wxButton(right, wxID_ANY, "Пресеты…");
  stop_btn_ = new wxButton(right, wxID_ANY, "Стоп");
  run_btn_ = accent_button(right, "Запустить  (F5)");
  stop_btn_->Enable(false);
  cbtns->Add(cadd, 0, wxRIGHT, gap);
  cbtns->Add(cedit, 0, wxRIGHT, gap);
  cbtns->Add(cdup, 0, wxRIGHT, gap);
  cbtns->Add(cdel, 0, wxRIGHT, gap);
  cbtns->Add(presets, 0, wxRIGHT, pad);
  cbtns->AddStretchSpacer();
  cbtns->Add(run_btn_, 0, wxRIGHT, gap);
  cbtns->Add(stop_btn_);
  auto* qrow = new wxBoxSizer(wxHORIZONTAL);
  qrow->Add(new wxStaticText(right, wxID_ANY, "Разовая команда:"), 0, wxALIGN_CENTER_VERTICAL);
  quick_ = new wxTextCtrl(right, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
  auto* qrun = new wxButton(right, wxID_ANY, "Выполнить");
  qrow->Add(quick_, 1, wxEXPAND | wxLEFT | wxRIGHT, pad);
  qrow->Add(qrun);
  auto* rs = new wxBoxSizer(wxVERTICAL);
  rs->Add(section_label(right, "Команды"), 0, wxBOTTOM, gap);
  rs->Add(folders_nb_, 1, wxEXPAND);
  rs->Add(corder, 0, wxTOP, pad);
  rs->Add(cbtns, 0, wxTOP, gap);
  rs->Add(qrow, 0, wxEXPAND | wxTOP, pad);
  right->SetSizer(rs);
  for (wxButton* b : {cadd, cedit, cdup, cdel, presets, up, down, byname, fadd, frename, fdel, qrun}) {
    busy_disable_.push_back(b);
  }
  busy_disable_.push_back(quick_);
  hsplit_->SplitVertically(left, right, FromDIP(360));
  auto* ts = new wxBoxSizer(wxVERTICAL);
  ts->Add(hsplit_, 1, wxEXPAND);
  top->SetSizer(ts);

  auto* outp = new wxPanel(vsplit_);
  cwd_label_ = new wxStaticText(outp, wxID_ANY, "Папка: ~");
  cwd_label_->SetName("muted");
  cwd_reset_ = new wxButton(outp, wxID_ANY, "Сбросить в ~");
  cwd_reset_->Enable(false);
  auto* clear = new wxButton(outp, wxID_ANY, "Очистить");
  auto* jbtn = new wxButton(outp, wxID_ANY, "Журнал");
  output_ = new wxTextCtrl(outp, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxTE_WORDWRAP);
  style_text(output_, true);
  bind_copy_on_select(output_, [this](const std::string& t) {
    status_->SetLabel(wxString::Format("Скопировано в буфер (%d симв.)", (int)t.size()));
  });
  auto* ot = new wxBoxSizer(wxHORIZONTAL);
  ot->Add(section_label(outp, "Вывод"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, pad);
  ot->Add(cwd_label_, 1, wxALIGN_CENTER_VERTICAL);
  ot->Add(cwd_reset_, 0, wxLEFT, pad);
  ot->AddStretchSpacer();
  ot->Add(jbtn, 0, wxRIGHT, gap);
  ot->Add(clear);
  auto* os = new wxBoxSizer(wxVERTICAL);
  os->Add(ot, 0, wxEXPAND);
  os->Add(output_, 1, wxEXPAND | wxTOP, gap);
  outp->SetSizer(os);
  vsplit_->SplitHorizontally(top, outp, FromDIP(420));

  auto* status_bar = new wxPanel(panel);
  status_bar->SetName("chrome");
  status_ = new wxStaticText(status_bar, wxID_ANY, "Готово");
  status_->SetName("muted");
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
  commands_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) {
    auto* c = selected_command();
    auto* s = selected_server();
    if (c && s) run_command(*s, c->command, c->timeout_sec, c->login_shell, c->name, c->id, "command");
  });
  commands_->Bind(wxEVT_LIST_COL_CLICK, [this](wxListEvent& e) {
    auto* s = selected_server();
    if (!s || e.GetColumn() > 1) return;
    config_.sort_commands_for(s->id, current_folder_id(), e.GetColumn() == 1 ? "command" : "name");
    persist();
    refresh_commands();
  });

  sadd->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    ServerDialog dlg(this, Server::make_new(), "Новый VPS", true);
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
    for (auto& c : cmds) {
      auto d = c.duplicate("", clone.id);
      if (!c.folder_id.empty() && folder_map.count(c.folder_id)) {
        d.folder_id = folder_map[c.folder_id];
      } else {
        d.folder_id.clear();
      }
      config_.commands.push_back(d);
    }
    persist();
    refresh_servers(clone.id);
  });
  sdel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) return;
    if (wxMessageBox("Удалить «" + wxString::FromUTF8(s->name) + "»?", "Удалить VPS", wxYES_NO, this) != wxYES) return;
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
    persist();
    refresh_servers();
  });
  files->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) {
      wxMessageBox("Сначала выберите VPS.", "Файлы");
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
      wxMessageBox(wxString::FromUTF8(exc.what()), "Консоль", wxOK | wxICON_ERROR, this);
    }
  });
  putty->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) return;
    try {
      open_putty_console(*s, config_.settings.putty_path);
      status_->SetLabel(wxString::FromUTF8("PuTTY открыт → " + s->name));
    } catch (const PuttyNotFoundError&) {
      int c = wxMessageBox("PuTTY не найден.\nДа — скачать\nНет — указать putty.exe", "PuTTY", wxYES_NO | wxCANCEL, this);
      if (c == wxYES) open_url(kPuttyDownloadUrl);
      else if (c == wxNO) {
        wxFileDialog dlg(this, "putty.exe", "", "putty.exe", "PuTTY|putty.exe", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK) {
          config_.settings.putty_path = std::string(dlg.GetPath().utf8_string());
          persist();
          open_putty_console(*s, config_.settings.putty_path);
        }
      }
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), "PuTTY", wxOK | wxICON_ERROR, this);
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
      wxMessageBox("Сначала выберите VPS.", "Папка");
      return;
    }
    auto name = wxGetTextFromUser("Имя папки (проект)", "Новая папка", "", this);
    auto trimmed = trim(std::string(name.utf8_string()));
    if (trimmed.empty()) return;
    for (const auto& f : config_.folders_for(s->id)) {
      if (to_lower(f.name) == to_lower(trimmed)) {
        wxMessageBox("Такая папка уже есть.", "Папка", wxOK | wxICON_WARNING, this);
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
      wxMessageBox("Вкладку «Общее» переименовать нельзя — создайте папку.", "Папка");
      return;
    }
    auto name = wxGetTextFromUser("Новое имя", "Папка", wxString::FromUTF8(f->name), this);
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
      wxMessageBox("Вкладку «Общее» удалить нельзя.", "Папка");
      return;
    }
    auto* f = config_.folder_by_id(id);
    if (!f) return;
    if (wxMessageBox("Удалить папку «" + wxString::FromUTF8(f->name) +
                         "»? Команды останутся во вкладке «Общее».",
                     "Папка", wxYES_NO, this) != wxYES)
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
      wxMessageBox("Сначала выберите или добавьте VPS.", "Команда");
      return;
    }
    auto cmd = Command::make_new(s->id);
    cmd.timeout_sec = config_.settings.default_command_timeout;
    cmd.folder_id = current_folder_id();
    CommandDialog dlg(this, cmd, config_.servers, config_.folders, "Новая команда");
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
    auto* c = selected_command();
    if (!c) return;
    if (wxMessageBox("Удалить «" + wxString::FromUTF8(c->name) + "»?", "Удалить команду", wxYES_NO, this) != wxYES)
      return;
    auto id = c->id;
    config_.commands.erase(std::remove_if(config_.commands.begin(), config_.commands.end(),
                                          [&](const Command& x) { return x.id == id; }),
                           config_.commands.end());
    persist();
    refresh_commands();
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
      cmd.command = p.command;
      cmd.timeout_sec = p.timeout_sec;
      cmd.login_shell = p.login_shell;
      cmd.folder_id = current_folder_id();
      config_.commands.push_back(cmd);
      names.push_back(p.name);
      ++added;
    }
    persist();
    refresh_commands();
    status_->SetLabel(wxString::Format("Добавлено команд: %d", added));
  });
  run_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* c = selected_command();
    auto* s = selected_server();
    if (!c || !s) {
      wxMessageBox("Выберите VPS и команду.", "Запуск");
      return;
    }
    if (config_.settings.confirm_before_run &&
        wxMessageBox("Выполнить «" + wxString::FromUTF8(c->name) + "» на " + wxString::FromUTF8(s->name) + "?", "Запуск",
                     wxYES_NO, this) != wxYES)
      return;
    run_command(*s, c->command, c->timeout_sec, c->login_shell, c->name, c->id, "command");
  });
  stop_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (session_) session_->cancel();
  });
  qrun->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    auto cmd = trim(std::string(quick_->GetValue().utf8_string()));
    if (!s || cmd.empty()) return;
    if (config_.settings.confirm_before_run &&
        wxMessageBox("Выполнить разовую команду?", "Запуск", wxYES_NO, this) != wxYES)
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
    status_->SetLabel("Рабочая папка сброшена в домашнюю");
  });
  clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { output_->Clear(); });
  jbtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show_journal(); });
}

void AppFrame::show_journal() {
  if (!journal_window_) {
    journal_window_ = new JournalWindow(this, journal_, [this](const JournalEntry& e) {
      if (busy_) {
        wxMessageBox("Дождитесь окончания текущей команды или нажмите Стоп.", "Занято", wxOK, this);
        return;
      }
      auto* s = config_.server_by_id(e.server_id);
      if (!s) {
        wxMessageBox("VPS из этой записи больше нет в списке.", "Журнал", wxOK | wxICON_ERROR, this);
        return;
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
    if (interactive) status_->SetLabel("Проверка обновлений уже идёт…");
    return;
  }
  checking_updates_ = true;
  if (interactive) status_->SetLabel("Проверка обновлений…");
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
        if (interactive) wxMessageBox(wxString::FromUTF8(err), "Обновления", wxOK | wxICON_ERROR, this);
        else status_->SetLabel("Готово");
        return;
      }
      if (r.status == "update") {
        status_->SetLabel(wxString::FromUTF8("Доступна версия " + (r.latest ? *r.latest : "?")));
        std::string url = r.page_url.value_or(r.download_url.value_or(""));
        if (wxMessageBox("Доступна новая версия. Открыть страницу загрузки?", "Обновления", wxYES_NO, this) == wxYES &&
            !url.empty()) {
          open_url(url);
        }
      } else if (interactive && r.status == "current") {
        wxMessageBox("У вас актуальная версия.", "Обновления", wxOK | wxICON_INFORMATION, this);
      } else if (interactive) {
        wxMessageBox("На GitHub пока нет опубликованных релизов.", "Обновления", wxOK | wxICON_INFORMATION, this);
      } else {
        status_->SetLabel("Готово");
      }
      if (!interactive) {
        config_.settings.last_update_check =
            std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        persist();
      }
    });
  }).detach();
}

void AppFrame::open_settings() {
  SettingsDialog dlg(this, config_, vault_,
                     [this] {
                       journal_->max_entries = config_.settings.journal_max_entries;
                       persist();
                       apply_ui_theme();
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

void AppFrame::persist() {
  config_.settings.window_geometry = window_geometry(this);
  config_.settings.window_state = IsMaximized() ? "zoomed" : "normal";
  if (hsplit_) config_.settings.sash_pos = hsplit_->GetSashPosition();
  if (vsplit_) config_.settings.vsash_pos = vsplit_->GetSashPosition();
  store_list_columns(servers_, config_.settings, "servers", {"name", "host"});
  store_list_columns(commands_, config_.settings, "commands", {"name", "command", "last"});
  if (auto* s = selected_server()) {
    config_.settings.last_server_id = s->id;
    config_.settings.last_folder_by_server[s->id] = current_folder_id();
  }
  if (auto* c = selected_command()) config_.settings.last_command_id = c->id;
  try {
    save_config(config_, vault_);
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
    servers_->InsertItem(row, wxString::FromUTF8(s.name));
    servers_->SetItem(row, 1, wxString::FromUTF8(s.username + "@" + s.host + ":" + std::to_string(s.port)));
    servers_->SetItemPtrData(row, static_cast<wxUIntPtr>(i));
    if (s.id == keep) sel = row;
    ++row;
  }
  if (sel < 0 && servers_->GetItemCount() > 0) sel = 0;
  if (sel >= 0) servers_->SetItemState(sel, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
  rebuild_folder_tabs();
  refresh_commands();
}

void AppFrame::restore_columns() {
  auto it_s = config_.settings.column_widths.find("servers");
  if (it_s != config_.settings.column_widths.end())
    apply_list_columns(servers_, it_s->second, {"name", "host"});
  auto it_c = config_.settings.column_widths.find("commands");
  if (it_c != config_.settings.column_widths.end())
    apply_list_columns(commands_, it_c->second, {"name", "command", "last"});
}

void AppFrame::apply_ui_theme() {
  set_theme(config_.settings.theme);
  apply_theme(this);
  if (journal_window_) apply_theme(journal_window_);
  if (help_window_) apply_theme(help_window_);
  for (auto& [id, win] : files_windows_) {
    if (win) apply_theme(win);
  }
  refresh_commands();
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
    page->SetSizer(new wxBoxSizer(wxVERTICAL));
    folders_nb_->AddPage(page, title);
    folder_tab_ids_.push_back(id);
  };
  add_page("Общее", "");
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
    return;
  }
  auto cmds = config_.commands_for(s->id, current_folder_id());
  long sel = -1;
  for (std::size_t i = 0; i < cmds.size(); ++i) {
    const auto& c = cmds[i];
    auto preview = c.command;
    if (preview.size() > 90) preview = preview.substr(0, 87) + "…";
    long row = commands_->InsertItem(static_cast<long>(i), wxString::FromUTF8(c.name));
    commands_->SetItem(row, 1, wxString::FromUTF8(preview));
    auto it = last_runs_.find(c.id);
    if (it == last_runs_.end()) {
      commands_->SetItem(row, 2, "—");
    } else {
      commands_->SetItem(row, 2, wxString::FromUTF8(it->second.last_run_label()));
      wxColour col = Theme::text();
      if (it->second.status == "ok") col = Theme::ok();
      else if (it->second.status == "timeout") col = Theme::warn();
      else if (it->second.status == "cancelled") col = Theme::cancel();
      else col = Theme::err();
      commands_->SetItemTextColour(row, col);
    }
    if (c.id == config_.settings.last_command_id) sel = row;
  }
  if (sel >= 0) commands_->SetItemState(sel, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
  update_cwd_label();
}

Server* AppFrame::selected_server() {
  long row = servers_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (row < 0) return nullptr;
  auto idx = static_cast<std::size_t>(servers_->GetItemData(row));
  if (idx >= config_.servers.size()) return nullptr;
  return &config_.servers[idx];
}

Command* AppFrame::selected_command() {
  auto* s = selected_server();
  if (!s) return nullptr;
  long i = commands_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  auto cmds = config_.commands_for(s->id, current_folder_id());
  if (i < 0 || i >= static_cast<long>(cmds.size())) return nullptr;
  return config_.command_by_id(cmds[static_cast<std::size_t>(i)].id);
}

void AppFrame::append_output(const std::string& text, const wxColour* colour) {
  output_->SetDefaultStyle(wxTextAttr(colour ? *colour : Theme::text()));
  output_->AppendText(wxString::FromUTF8(text));
}

void AppFrame::set_busy(bool busy) {
  busy_ = busy;
  run_btn_->Enable(!busy);
  stop_btn_->Enable(busy);
  for (wxWindow* w : busy_disable_) {
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
  busy_gauge_->Pulse();
  auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - run_start_).count();
  status_->SetLabel(wxString::FromUTF8(busy_label_ + "  •  " + std::to_string(secs) + " с"));
}

void AppFrame::update_cwd_label() {
  auto* s = selected_server();
  if (!s) {
    cwd_label_->SetLabel("");
    cwd_reset_->Enable(false);
    return;
  }
  auto it = remote_cwd_.find(s->id);
  if (it != remote_cwd_.end() && !it->second.empty()) {
    cwd_label_->SetLabel(wxString::FromUTF8("Папка: " + it->second));
    cwd_reset_->Enable(true);
  } else {
    cwd_label_->SetLabel("Папка: ~");
    cwd_reset_->Enable(false);
  }
}

void AppFrame::run_command(const Server& server, const std::string& command, int timeout, bool login_shell,
                           const std::string& title, const std::string& command_id, const std::string& kind) {
  if (busy_) {
    wxMessageBox("Дождитесь окончания текущей команды или нажмите Стоп.", "Занято", wxOK, this);
    return;
  }
  if (config_.settings.clear_output_before_run) output_->Clear();
  busy_label_ = "Выполняется: " + title + " → " + server.name;
  set_busy(true);
  auto cwd = remote_cwd_[server.id];
  status_->SetLabel(wxString::FromUTF8(busy_label_));
  const wxColour meta = Theme::meta();
  append_output("\n" + std::string(60, '-') + "\n", &meta);
  append_output(title + "  •  " + server.name + "\n", &meta);
  session_ = std::make_shared<SSHSession>();
  auto started = now_iso();
  auto t0 = std::chrono::steady_clock::now();
  Server srv = server;
  // Поток держит собственные копии shared_ptr на сессию и журнал, а к окну
  // обращается только через живой-токен: закрытие FaTTY во время выполнения
  // больше не оставляет поток работать по разрушенному AppFrame.
  auto alive = alive_;
  auto session = session_;
  auto journal = journal_;
  auto running = worker_running_;
  running->store(true);
  std::thread([this, alive, session, journal, running, srv, command, timeout, login_shell, title, command_id, kind, cwd,
               started, t0] {
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
    try {
      auto result = session->run(srv, command, timeout, login_shell,
                                 [this, post](const std::string& chunk) {
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
    journal->append(e);
    post([this, srv, title, code, status, error, new_cwd] {
      if (!new_cwd.empty()) remote_cwd_[srv.id] = new_cwd;
      update_cwd_label();
      set_busy(false);
      session_.reset();
      status_->SetLabel(wxString::FromUTF8(
          "Готово  •  код " + (status == "error" && code == 1 && !error.empty() ? std::string("—") : std::to_string(code)) +
          "  •  " + title));
    });
  }).detach();
}

}  // namespace fatty

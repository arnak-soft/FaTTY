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
              wxDefaultPosition, wxSize(1100, 720)),
      config_(std::move(config)),
      vault_(std::move(vault)),
      journal_(journal_path(), config_.settings.journal_max_entries) {
  set_icon(this);
  SetMinSize(wxSize(860, 560));
  last_runs_ = journal_.latest_by_command_id();
  journal_.add_listener([this] {
    CallAfter([this] {
      last_runs_ = journal_.latest_by_command_id();
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
      wxCommandEvent ev;
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
      if (!help_window_) {
        help_window_ = new HelpWindow(this, [this](const std::string& cmd) { quick_->SetValue(wxString::FromUTF8(cmd)); });
        help_window_->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& ev) {
          help_window_ = nullptr;
          ev.Skip();
        });
      }
      help_window_->Show();
      help_window_->Raise();
      return;
    }
    if (e.ControlDown() && (e.GetKeyCode() == 'J' || e.GetKeyCode() == 'j')) {
      if (!journal_window_) {
        journal_window_ = new JournalWindow(this, journal_, [this](const JournalEntry& e) {
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
      return;
    }
    if (e.ControlDown() && e.GetKeyCode() == ',') {
      SettingsDialog dlg(this, config_, vault_, [this] { persist(); apply_ui_theme(); },
                         [this] {
                           ChangeMasterDialog d(this, vault_, config_.settings.allow_short_master_password);
                           if (d.ShowModal() == wxID_OK && d.ok) persist();
                         },
                         [this] { /* manual updates from settings */ }, [this] { refresh_servers(); });
      dlg.setup_layout(&config_.settings, "settings", true, [this] { persist(); });
      dlg.ShowModal();
      return;
    }
    e.Skip();
  });
  CallAfter([this] {
    if (config_.settings.window_state == "zoomed") Maximize();
    if (config_.settings.sash_pos > 0) hsplit_->SetSashPosition(config_.settings.sash_pos);
    if (config_.settings.vsash_pos > 0) vsplit_->SetSashPosition(config_.settings.vsash_pos);
    restoring_ = false;
  });
  if (config_.settings.check_updates_on_start) {
    using clock = std::chrono::system_clock;
    double now = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
    if (now - config_.settings.last_update_check >= 24 * 3600) {
      std::thread([this] {
        try {
          auto r = check_for_updates(resolve_version());
          CallAfter([this, r] {
            config_.settings.last_update_check =
                std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
            persist();
            if (r.status == "update") {
              status_->SetLabel(wxString::FromUTF8("Доступна версия " + (r.latest ? *r.latest : "?")));
              if (wxMessageBox("Доступна новая версия. Открыть страницу загрузки?", "Обновления", wxYES_NO, this) ==
                      wxYES &&
                  r.download_url) {
                open_url(*r.download_url);
              }
            }
          });
        } catch (...) {
        }
      }).detach();
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
  Bind(wxEVT_MENU, [this](wxCommandEvent& e) {
    wxKeyEvent k;
    k.m_controlDown = true;
    k.m_keyCode = 'J';
    ProcessWindowEvent(k);
  }, 1002);
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
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    SettingsDialog dlg(this, config_, vault_, [this] { persist(); apply_ui_theme(); },
                       [this] {
                         ChangeMasterDialog d(this, vault_, config_.settings.allow_short_master_password);
                         if (d.ShowModal() == wxID_OK && d.ok) persist();
                       },
                       [this] {}, [this] { refresh_servers(); });
    dlg.ShowModal();
  }, 1010);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    ChangeMasterDialog d(this, vault_, config_.settings.allow_short_master_password);
    d.setup_layout(&config_.settings, "change_master", false, [this] { persist(); });
    if (d.ShowModal() == wxID_OK && d.ok) {
      persist();
      wxMessageBox("Мастер-пароль обновлён.", "Мастер-пароль");
    }
  }, 1011);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    wxKeyEvent k;
    k.m_keyCode = WXK_F1;
    ProcessWindowEvent(k);
  }, 1020);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    if (!help_window_) {
      help_window_ = new HelpWindow(this, [this](const std::string& cmd) { quick_->SetValue(wxString::FromUTF8(cmd)); });
    }
    help_window_->Show();
    help_window_->show_tab("Команды");
  }, 1021);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    try {
      auto r = check_for_updates(resolve_version());
      if (r.status == "update") {
        if (wxMessageBox("Доступна новая версия. Открыть?", "Обновления", wxYES_NO, this) == wxYES && r.page_url)
          open_url(*r.page_url);
      } else if (r.status == "current") {
        wxMessageBox("У вас актуальная версия.", "Обновления");
      } else {
        wxMessageBox("На GitHub пока нет опубликованных релизов.", "Обновления");
      }
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), "Обновления", wxOK | wxICON_ERROR);
    }
  }, 1022);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    wxMessageBox(wxString::FromUTF8(std::string(kAppName) + " " + resolve_version() +
                                    "\n\nЗапуск команд на VPS по SSH.\n\nСправка: F1"),
                 "О программе");
  }, wxID_ABOUT);
}

void AppFrame::build_ui() {
  auto* panel = new wxPanel(this);
  status_ = new wxStaticText(panel, wxID_ANY, "Готово");
  status_->SetForegroundColour(Theme::muted());
  vsplit_ = new wxSplitterWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
  auto* top = new wxPanel(vsplit_);
  hsplit_ = new wxSplitterWindow(top, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
  auto* left = new wxPanel(hsplit_);
  auto* right = new wxPanel(hsplit_);
  servers_ = new wxListCtrl(left, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  servers_->AppendColumn("Имя", wxLIST_FORMAT_LEFT, 160);
  servers_->AppendColumn("Адрес", wxLIST_FORMAT_LEFT, 220);
  auto* sbtns = new wxBoxSizer(wxHORIZONTAL);
  auto* sadd = new wxButton(left, wxID_ANY, "Добавить");
  auto* sedit = new wxButton(left, wxID_ANY, "Изменить");
  auto* sdup = new wxButton(left, wxID_ANY, "Дублировать");
  auto* sdel = new wxButton(left, wxID_ANY, "Удалить");
  sbtns->Add(sadd, 0, wxRIGHT, 4);
  sbtns->Add(sedit, 0, wxRIGHT, 4);
  sbtns->Add(sdup, 0, wxRIGHT, 4);
  sbtns->Add(sdel);
  auto* sact = new wxBoxSizer(wxHORIZONTAL);
  auto* files = new wxButton(left, wxID_ANY, "Файлы");
  auto* cons = new wxButton(left, wxID_ANY, "Открыть консоль");
  auto* putty = new wxButton(left, wxID_ANY, "PuTTY");
  auto* test = new wxButton(left, wxID_ANY, "Проверить связь");
  sact->Add(files, 0, wxRIGHT, 4);
  sact->Add(cons, 0, wxRIGHT, 4);
  sact->Add(putty, 0, wxRIGHT, 4);
  sact->Add(test);
  auto* ls = new wxBoxSizer(wxVERTICAL);
  ls->Add(new wxStaticText(left, wxID_ANY, "VPS-серверы"), 0, wxBOTTOM, 4);
  ls->Add(servers_, 1, wxEXPAND);
  ls->Add(sbtns, 0, wxTOP, 8);
  ls->Add(sact, 0, wxTOP, 4);
  left->SetSizer(ls);

  folders_nb_ = new wxNotebook(right, wxID_ANY);
  auto* first_page = new wxPanel(folders_nb_);
  auto* page_sz = new wxBoxSizer(wxVERTICAL);
  commands_ = new wxListCtrl(first_page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  commands_->AppendColumn("Название", wxLIST_FORMAT_LEFT, 180);
  commands_->AppendColumn("Команда", wxLIST_FORMAT_LEFT, 320);
  commands_->AppendColumn("Последний раз", wxLIST_FORMAT_LEFT, 140);
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
  corder->Add(up, 0, wxRIGHT, 4);
  corder->Add(down, 0, wxRIGHT, 4);
  corder->Add(byname, 0, wxRIGHT, 12);
  corder->Add(fadd, 0, wxRIGHT, 4);
  corder->Add(frename, 0, wxRIGHT, 4);
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
  cbtns->Add(cadd, 0, wxRIGHT, 4);
  cbtns->Add(cedit, 0, wxRIGHT, 4);
  cbtns->Add(cdup, 0, wxRIGHT, 4);
  cbtns->Add(cdel, 0, wxRIGHT, 4);
  cbtns->Add(presets, 0, wxRIGHT, 8);
  cbtns->AddStretchSpacer();
  cbtns->Add(run_btn_, 0, wxRIGHT, 4);
  cbtns->Add(stop_btn_);
  auto* qrow = new wxBoxSizer(wxHORIZONTAL);
  qrow->Add(new wxStaticText(right, wxID_ANY, "Разовая команда:"), 0, wxALIGN_CENTER_VERTICAL);
  quick_ = new wxTextCtrl(right, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
  auto* qrun = new wxButton(right, wxID_ANY, "Выполнить");
  qrow->Add(quick_, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);
  qrow->Add(qrun);
  auto* rs = new wxBoxSizer(wxVERTICAL);
  rs->Add(new wxStaticText(right, wxID_ANY, "Команды выбранного VPS"), 0, wxBOTTOM, 4);
  rs->Add(folders_nb_, 1, wxEXPAND);
  rs->Add(corder, 0, wxTOP, 8);
  rs->Add(cbtns, 0, wxTOP, 4);
  rs->Add(qrow, 0, wxEXPAND | wxTOP, 8);
  right->SetSizer(rs);
  hsplit_->SplitVertically(left, right, 360);
  auto* ts = new wxBoxSizer(wxVERTICAL);
  ts->Add(hsplit_, 1, wxEXPAND);
  top->SetSizer(ts);

  auto* outp = new wxPanel(vsplit_);
  cwd_label_ = new wxStaticText(outp, wxID_ANY, "Папка: ~");
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
  ot->Add(cwd_label_, 1, wxALIGN_CENTER_VERTICAL);
  ot->Add(cwd_reset_, 0, wxLEFT, 8);
  ot->AddStretchSpacer();
  ot->Add(jbtn, 0, wxRIGHT, 4);
  ot->Add(clear);
  auto* os = new wxBoxSizer(wxVERTICAL);
  os->Add(new wxStaticText(outp, wxID_ANY, "Вывод"), 0);
  os->Add(ot, 0, wxEXPAND | wxTOP, 4);
  os->Add(output_, 1, wxEXPAND | wxTOP, 4);
  outp->SetSizer(os);
  vsplit_->SplitHorizontally(top, outp, 420);

  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(vsplit_, 1, wxEXPAND | wxALL, 8);
  root->Add(status_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
  panel->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(panel, 1, wxEXPAND);
  SetSizer(outer);

  servers_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) {
    rebuild_folder_tabs();
    refresh_commands();
  });
  servers_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { sedit->GetEventHandler()->ProcessEvent(wxCommandEvent(wxEVT_BUTTON)); });
  commands_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) {
    auto* c = selected_command();
    auto* s = selected_server();
    if (c && s) run_command(*s, c->command, c->timeout_sec, c->login_shell, c->name, c->id, "command");
  });
  commands_->Bind(wxEVT_LIST_COL_CLICK, [this](wxListEvent& e) {
    auto* s = selected_server();
    if (!s) return;
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
    wxCommandEvent ev;
    qrun->GetEventHandler()->ProcessEvent(wxCommandEvent(wxEVT_BUTTON));
  });
  cwd_reset_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto* s = selected_server();
    if (!s) return;
    remote_cwd_.erase(s->id);
    update_cwd_label();
    status_->SetLabel("Рабочая папка сброшена в домашнюю");
  });
  clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { output_->Clear(); });
  jbtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxCommandEvent e(wxEVT_MENU, 1002);
    ProcessWindowEvent(e);
  });
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
  long sel = -1;
  for (std::size_t i = 0; i < config_.servers.size(); ++i) {
    const auto& s = config_.servers[i];
    long row = servers_->InsertItem(static_cast<long>(i), wxString::FromUTF8(s.name));
    servers_->SetItem(row, 1, wxString::FromUTF8(s.username + "@" + s.host + ":" + std::to_string(s.port)));
    servers_->SetItemPtrData(row, static_cast<wxUIntPtr>(i));
    if (s.id == keep) sel = row;
  }
  if (sel < 0 && !config_.servers.empty()) sel = 0;
  if (sel >= 0) servers_->SetItemState(sel, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
  rebuild_folder_tabs();
  refresh_commands();
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
  long i = servers_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i < 0 || i >= static_cast<long>(config_.servers.size())) return nullptr;
  return &config_.servers[static_cast<std::size_t>(i)];
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
  set_busy(true);
  auto cwd = remote_cwd_[server.id];
  status_->SetLabel(wxString::FromUTF8("Выполняется: " + title + " → " + server.name));
  append_output("\n" + std::string(60, '-') + "\n", &Theme::meta());
  append_output(title + "  •  " + server.name + "\n", &Theme::meta());
  session_ = std::make_unique<SSHSession>();
  auto started = now_iso();
  auto t0 = std::chrono::steady_clock::now();
  Server srv = server;
  std::thread([this, srv, command, timeout, login_shell, title, command_id, kind, cwd, started, t0] {
    int code = 1;
    std::string status = "error";
    std::string error;
    std::string new_cwd = cwd;
    try {
      auto result = session_->run(srv, command, timeout, login_shell,
                                  [this](const std::string& chunk) {
                                    CallAfter([this, chunk] { append_output(chunk); });
                                  },
                                  cwd);
      code = result.exit_code;
      status = status_from_exit(code);
      if (!result.cwd.empty()) new_cwd = result.cwd;
      auto col = code == 0 ? Theme::ok() : Theme::err();
      CallAfter([this, code, col] { append_output("\n← код выхода " + std::to_string(code) + "\n", &col); });
    } catch (const std::exception& exc) {
      error = exc.what();
      CallAfter([this, error] { append_output("\n" + error + "\n", &Theme::err()); });
    }
    auto duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CallAfter([this, srv, command, title, command_id, kind, login_shell, timeout, started, duration, code, status, error,
               new_cwd] {
      if (!new_cwd.empty()) remote_cwd_[srv.id] = new_cwd;
      update_cwd_label();
      set_busy(false);
      session_.reset();
      status_->SetLabel(wxString::FromUTF8("Готово  •  код " + (status == "error" && code == 1 && !error.empty() ? std::string("—") : std::to_string(code)) +
                                           "  •  " + title));
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
      journal_.append(e);
    });
  }).detach();
}

}  // namespace fatty

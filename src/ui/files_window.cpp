#include "ui/files_window.hpp"

#include "core/remote_text.hpp"
#include "core/util.hpp"
#include "ui/chrome.hpp"
#include "ui/dialogs.hpp"
#include "ui/file_editor_dialog.hpp"
#include "ui/layout.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/textctrl.h>
#include <wx/wrapsizer.h>
#include <wx/imaglist.h>
#include <wx/artprov.h>
#include <wx/app.h>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace fatty {

FilesWindow::FilesWindow(wxWindow* parent, const Server& server, std::string start_path, AppSettings* settings,
                         std::function<void()> persist)
    : wxPanel(parent),
      server_(server),
      start_path_(std::move(start_path)) {
  (void)settings;
  (void)persist;
  SetName(L"card-page");
  path_ = new wxTextCtrl(this, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
  auto* up = make_button(this, L"Вверх", BtnIcon::ArrowUp);
  auto* mkdir = make_button(this, L"Новая папка", BtnIcon::FolderPlus);
  auto* upload_btn = make_button(this, L"Загрузить", BtnIcon::Upload);
  auto* download_btn = make_button(this, L"Скачать", BtnIcon::Download);
  auto* del = make_button(this, L"Удалить", BtnIcon::Trash);
  auto* stop = make_button(this, L"Стоп", BtnIcon::Stop);
  stop_btn_ = stop;
  busy_disable_ = {up, mkdir, upload_btn, download_btn, del, path_};
  auto* list_card = new RoundedCard(this);
  list_ = new wxListCtrl(list_card, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
  list_->AppendColumn(L"Имя", wxLIST_FORMAT_LEFT, FromDIP(280));
  list_->AppendColumn(L"Тип", wxLIST_FORMAT_LEFT, FromDIP(120));
  list_->AppendColumn(L"Размер", wxLIST_FORMAT_RIGHT, FromDIP(90));
  list_->AppendColumn(L"Изменён", wxLIST_FORMAT_LEFT, FromDIP(140));
  auto* list_sz = new wxBoxSizer(wxVERTICAL);
  list_sz->Add(list_, 1, wxEXPAND);
  list_card->SetSizer(list_sz);
  gauge_ = new wxGauge(this, wxID_ANY, 100);
  status_ = new wxStaticText(this, wxID_ANY, L"Подключение…");
  status_->SetName(L"muted");
  auto* top = new wxBoxSizer(wxHORIZONTAL);
  top->Add(path_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  path_->SetMinSize(wxSize(-1, up->GetBestSize().GetHeight()));
  top->Add(up, 0, wxALIGN_CENTER_VERTICAL);
  auto* acts = new wxWrapSizer(wxHORIZONTAL);
  acts->Add(mkdir, 0, wxRIGHT | wxBOTTOM, 8);
  acts->Add(upload_btn, 0, wxRIGHT | wxBOTTOM, 8);
  acts->Add(download_btn, 0, wxRIGHT | wxBOTTOM, 8);
  acts->Add(del, 0, wxRIGHT | wxBOTTOM, 8);
  acts->Add(stop, 0, wxBOTTOM, 8);
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(top, 0, wxEXPAND | wxALL, 8);
  root->Add(acts, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
  root->Add(list_card, 1, wxEXPAND | wxALL, 8);
  root->Add(gauge_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
  root->Add(status_, 0, wxEXPAND | wxALL, 8);
  SetSizer(root);
  apply_dark(this);
  up->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { go_up(); });
  path_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
    if (busy_) return;
    try {
      session_->enter(std::string(path_->GetValue().utf8_string()));
      refresh();
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), L"Файлы", wxOK | wxICON_ERROR, this);
    }
  });
  list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& e) {
    if (busy_) return;
    long i = e.GetIndex();
    if (i == 0) {
      go_up();
      return;
    }
    auto& ent = entries_[static_cast<std::size_t>(i - 1)];
    if (ent.is_dir) {
      try {
        session_->enter(ent.path);
        refresh();
      } catch (const std::exception& exc) {
        wxMessageBox(wxString::FromUTF8(exc.what()), L"Файлы", wxOK | wxICON_ERROR, this);
      }
    } else {
      // Двойной клик / Enter — только скачать; правка только из ПКМ.
      download_selected();
    }
  });
  list_->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, [this](wxListEvent& e) {
    if (busy_) return;
    show_list_context_menu(e.GetIndex());
  });
  mkdir->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxTextEntryDialog dlg(this, L"Имя папки", L"Новая папка");
    if (dlg.ShowModal() != wxID_OK) return;
    try {
      session_->mkdir(std::string(dlg.GetValue().utf8_string()));
      refresh();
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), L"Файлы", wxOK | wxICON_ERROR, this);
    }
  });
  upload_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { upload(); });
  download_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { download_selected(); });
  del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (i <= 0) return;
    auto& ent = entries_[static_cast<std::size_t>(i - 1)];
    if (!ask_confirm(this, L"Файлы", L"Удалить «" + wxString::FromUTF8(ent.name) + L"»?", L"Удалить", false,
                     BtnIcon::Trash))
      return;
    try {
      session_->remove(ent);
      refresh();
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), L"Файлы", wxOK | wxICON_ERROR, this);
    }
  });
  stop->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { session_->cancel_transfer(); });
  CallAfter([this] { connect(); });
}

FilesWindow::~FilesWindow() {
  alive_->store(false);
  session_->cancel_transfer();
  session_->close();
}

void FilesWindow::set_busy(bool busy) {
  busy_ = busy;
  for (wxWindow* w : busy_disable_) {
    if (w) w->Enable(!busy);
  }
  if (stop_btn_) stop_btn_->Enable(busy);
}

void FilesWindow::connect() {
  set_busy(true);
  status_->SetLabel(L"Подключение…");
  auto alive = alive_;
  auto session = session_;
  auto server = server_;
  auto start = start_path_;
  std::thread([this, alive, session, server, start] {
    std::string err;
    try {
      session->connect(server, start);
    } catch (const std::exception& exc) {
      err = exc.what();
    }
    wxTheApp->CallAfter([this, alive, err] {
      if (!alive->load()) return;
      set_busy(false);
      if (err.empty()) {
        refresh();
        status_->SetLabel(L"Готово");
      } else {
        status_->SetLabel(wxString::FromUTF8(err));
        wxMessageBox(wxString::FromUTF8(err), L"Файлы", wxOK | wxICON_ERROR, this);
      }
    });
  }).detach();
}

void FilesWindow::refresh() {
  entries_ = session_->listdir();
  path_->SetValue(wxString::FromUTF8(session_->remote_cwd));
  list_->DeleteAllItems();
  long row = list_->InsertItem(0, L"..");
  list_->SetItem(row, 1, L"папка");
  for (const auto& e : entries_) {
    row = list_->InsertItem(list_->GetItemCount(), wxString::FromUTF8(e.name));
    list_->SetItem(row, 1, wxString::FromUTF8(e.kind_label()));
    list_->SetItem(row, 2, wxString::FromUTF8(e.is_dir ? "—" : format_size(e.size)));
    list_->SetItem(row, 3, wxString::FromUTF8(format_mtime(e.mtime)));
  }
}

void FilesWindow::go_up() {
  if (busy_) return;
  try {
    session_->go_up();
    refresh();
  } catch (const std::exception& exc) {
    wxMessageBox(wxString::FromUTF8(exc.what()), L"Файлы", wxOK | wxICON_ERROR, this);
  }
}

RemoteEntry* FilesWindow::selected_file_entry() {
  long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i <= 0) return nullptr;
  auto& ent = entries_[static_cast<std::size_t>(i - 1)];
  if (ent.is_dir) return nullptr;
  return &ent;
}

void FilesWindow::show_list_context_menu(long row) {
  if (row < 0 || row >= list_->GetItemCount()) return;
  list_->SetItemState(row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                      wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
  if (row == 0) return;
  auto& ent = entries_[static_cast<std::size_t>(row - 1)];
  wxMenu menu;
  if (!ent.is_dir) {
    menu.Append(1, L"Открыть…");
    menu.Append(2, L"Скачать");
    menu.AppendSeparator();
  }
  menu.Append(3, L"Удалить");
  menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { open_selected_for_edit(); }, 1);
  menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { download_selected(); }, 2);
  menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    auto* file = selected_file_entry();
    RemoteEntry* ent_ptr = file;
    if (!ent_ptr) {
      long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
      if (i <= 0) return;
      ent_ptr = &entries_[static_cast<std::size_t>(i - 1)];
    }
    if (!ask_confirm(this, L"Файлы", L"Удалить «" + wxString::FromUTF8(ent_ptr->name) + L"»?", L"Удалить", false,
                     BtnIcon::Trash))
      return;
    try {
      session_->remove(*ent_ptr);
      refresh();
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), L"Файлы", wxOK | wxICON_ERROR, this);
    }
  }, 3);
  show_themed_menu(this, &menu);
}

void FilesWindow::open_selected_for_edit() {
  if (busy_) return;
  auto* ent = selected_file_entry();
  if (!ent) return;
  if (ent->is_link) {
    wxMessageBox(L"Ссылки пока не редактируются — скачайте цель или откройте реальный файл.", L"Файлы",
                 wxOK | wxICON_INFORMATION, this);
    return;
  }
  if (ent->size > static_cast<long long>(kRemoteTextMaxBytes)) {
    wxMessageBox(wxString::FromUTF8("Файл больше " + std::to_string(kRemoteTextMaxBytes / 1024) +
                                    " КБ. Встроенный редактор его не открывает — используйте «Скачать»."),
                 L"Файлы", wxOK | wxICON_INFORMATION, this);
    return;
  }
  set_busy(true);
  status_->SetLabel(L"Чтение…");
  auto alive = alive_;
  auto session = session_;
  const auto name = ent->name;
  const auto size = ent->size;
  const auto mtime = ent->mtime;
  std::thread([this, alive, session, name, size, mtime] {
    std::string err;
    SFTPSession::FileBytes bytes;
    try {
      bytes = session->read_file(name, size, [this, alive](long long sent, long long total) {
        wxTheApp->CallAfter([this, alive, sent, total] {
          if (!alive->load()) return;
          gauge_->SetRange(static_cast<int>(total > 0 ? total : 1));
          gauge_->SetValue(static_cast<int>(sent > total ? total : sent));
        });
      });
    } catch (const std::exception& exc) {
      err = exc.what();
    }
    wxTheApp->CallAfter([this, alive, err, bytes = std::move(bytes), name, mtime]() mutable {
      if (!alive->load()) return;
      set_busy(false);
      if (!err.empty()) {
        wxMessageBox(wxString::FromUTF8(err), L"Файлы", wxOK | wxICON_ERROR, this);
        return;
      }
      const auto decoded = decode_remote_text(bytes.data);
      if (!decoded.ok) {
        wxMessageBox(wxString::FromUTF8(decoded.error), L"Файлы", wxOK | wxICON_WARNING, this);
        return;
      }
      FileEditorDialog dlg(this, wxString::FromUTF8("Файл: " + name), decoded.view);
      if (dlg.ShowModal() != wxID_OK || !dlg.saved) return;
      const long long expected = bytes.mtime > 0 ? bytes.mtime : mtime;
      save_edited_file(name, std::move(dlg.result_bytes), expected, false);
    });
  }).detach();
}

void FilesWindow::save_edited_file(const std::string& name, std::string payload, long long expected_mtime,
                                   bool force) {
  if (busy_) return;
  set_busy(true);
  status_->SetLabel(L"Сохранение…");
  auto alive = alive_;
  auto session = session_;
  const long long expect = force ? -1 : expected_mtime;
  std::thread([this, alive, session, name, payload = std::move(payload), expect, expected_mtime]() mutable {
    std::string write_err;
    try {
      session->write_file(name, payload, expect, [this, alive](long long sent, long long total) {
        wxTheApp->CallAfter([this, alive, sent, total] {
          if (!alive->load()) return;
          gauge_->SetRange(static_cast<int>(total > 0 ? total : 1));
          gauge_->SetValue(static_cast<int>(sent > total ? total : sent));
        });
      });
    } catch (const std::exception& exc) {
      write_err = exc.what();
    }
    wxTheApp->CallAfter([this, alive, write_err, name, payload = std::move(payload), expected_mtime]() mutable {
      if (!alive->load()) return;
      set_busy(false);
      if (write_err.empty()) {
        status_->SetLabel(L"Сохранено");
        try {
          refresh();
        } catch (...) {
        }
        return;
      }
      if (write_err.find("изменился") != std::string::npos) {
        if (ask_confirm(this, L"Файлы",
                        L"Файл на сервере изменился с момента открытия.\nПерезаписать своей версией?",
                        L"Перезаписать", false)) {
          save_edited_file(name, std::move(payload), expected_mtime, true);
        }
        return;
      }
      wxMessageBox(wxString::FromUTF8(write_err), L"Файлы", wxOK | wxICON_ERROR, this);
    });
  }).detach();
}

void FilesWindow::download_selected() {
  if (busy_) return;
  auto* ent_ptr = selected_file_entry();
  if (!ent_ptr) return;
  auto& ent = *ent_ptr;
  wxFileDialog dlg(this, L"Сохранить как", L"", wxString::FromUTF8(ent.name), L"All|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  if (dlg.ShowModal() != wxID_OK) return;
  auto dest = std::filesystem::path(dlg.GetPath().utf8_string());
  set_busy(true);
  status_->SetLabel(L"Скачивание…");
  auto alive = alive_;
  auto session = session_;
  std::thread([this, alive, session, name = ent.name, size = ent.size, dest] {
    std::string err;
    try {
      session->download(name, dest, size, [this, alive](long long sent, long long total) {
        wxTheApp->CallAfter([this, alive, sent, total] {
          if (!alive->load()) return;
          gauge_->SetRange(static_cast<int>(total > 0 ? total : 1));
          gauge_->SetValue(static_cast<int>(sent > total ? total : sent));
        });
      });
    } catch (const std::exception& exc) {
      err = exc.what();
    }
    wxTheApp->CallAfter([this, alive, err] {
      if (!alive->load()) return;
      set_busy(false);
      if (err.empty()) status_->SetLabel(L"Скачано");
      else wxMessageBox(wxString::FromUTF8(err), L"Файлы", wxOK | wxICON_ERROR, this);
    });
  }).detach();
}

void FilesWindow::upload() {
  if (busy_) return;
  wxFileDialog dlg(this, L"Загрузить", L"", L"", L"All|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
  if (dlg.ShowModal() != wxID_OK) return;
  auto local = std::filesystem::path(dlg.GetPath().utf8_string());
  auto remote = local.filename().string();
  set_busy(true);
  status_->SetLabel(L"Загрузка…");
  auto alive = alive_;
  auto session = session_;
  std::thread([this, alive, session, local, remote] {
    std::string err;
    try {
      session->upload(local, remote, [this, alive](long long sent, long long total) {
        wxTheApp->CallAfter([this, alive, sent, total] {
          if (!alive->load()) return;
          gauge_->SetRange(static_cast<int>(total > 0 ? total : 1));
          gauge_->SetValue(static_cast<int>(sent > total ? total : sent));
        });
      });
    } catch (const std::exception& exc) {
      err = exc.what();
    }
    wxTheApp->CallAfter([this, alive, err] {
      if (!alive->load()) return;
      set_busy(false);
      if (err.empty()) {
        status_->SetLabel(L"Загружено");
        refresh();
      } else {
        wxMessageBox(wxString::FromUTF8(err), L"Файлы", wxOK | wxICON_ERROR, this);
      }
    });
  }).detach();
}

}  // namespace fatty

#include "ui/files_window.hpp"

#include "core/util.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/dirdlg.h>
#include <wx/filedlg.h>
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

FilesWindow::FilesWindow(wxWindow* parent, const Server& server, std::string start_path)
    : wxFrame(parent, wxID_ANY, wxString::FromUTF8("Файлы — " + (server.name.empty() ? server.host : server.name)),
              wxDefaultPosition, wxDefaultSize),
      server_(server),
      start_path_(std::move(start_path)) {
  set_icon(this);
  SetSize(FromDIP(wxSize(780, 520)));
  auto* panel = new wxPanel(this);
  path_ = new wxTextCtrl(panel, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
  auto* up = make_button(panel, L"Вверх", BtnIcon::ArrowUp);
  auto* mkdir = make_button(panel, L"Новая папка", BtnIcon::FolderPlus);
  auto* upload_btn = make_button(panel, L"Загрузить", BtnIcon::Upload);
  auto* download_btn = make_button(panel, L"Скачать", BtnIcon::Download);
  auto* del = make_button(panel, L"Удалить", BtnIcon::Trash);
  auto* stop = make_button(panel, L"Стоп", BtnIcon::Stop);
  stop_btn_ = stop;
  busy_disable_ = {up, mkdir, upload_btn, download_btn, del, path_};
  auto* list_card = new RoundedCard(panel);
  list_ = new wxListCtrl(list_card, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
  list_->AppendColumn(L"Имя", wxLIST_FORMAT_LEFT, FromDIP(280));
  list_->AppendColumn(L"Тип", wxLIST_FORMAT_LEFT, FromDIP(120));
  list_->AppendColumn(L"Размер", wxLIST_FORMAT_RIGHT, FromDIP(90));
  list_->AppendColumn(L"Изменён", wxLIST_FORMAT_LEFT, FromDIP(140));
  auto* list_sz = new wxBoxSizer(wxVERTICAL);
  list_sz->Add(list_, 1, wxEXPAND);
  list_card->SetSizer(list_sz);
  gauge_ = new wxGauge(panel, wxID_ANY, 100);
  status_ = new wxStaticText(panel, wxID_ANY, L"Подключение…");
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
  panel->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(panel, 1, wxEXPAND);
  SetSizer(outer);
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
      download_selected();
    }
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
    if (wxMessageBox(L"Удалить «" + wxString::FromUTF8(ent.name) + L"»?", L"Файлы", wxYES_NO, this) != wxYES) return;
    try {
      session_->remove(ent);
      refresh();
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), L"Файлы", wxOK | wxICON_ERROR, this);
    }
  });
  stop->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { session_->cancel_transfer(); });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
    alive_->store(false);
    session_->cancel_transfer();
    session_->close();
    e.Skip();
  });
  bind_escape_close(this);
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

void FilesWindow::download_selected() {
  if (busy_) return;
  long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i <= 0) return;
  auto& ent = entries_[static_cast<std::size_t>(i - 1)];
  if (ent.is_dir) return;
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

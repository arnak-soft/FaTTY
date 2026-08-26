#include "ui/files_window.hpp"

#include "core/util.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/button.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/textctrl.h>
#include <wx/imaglist.h>
#include <wx/artprov.h>
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
              wxDefaultPosition, wxSize(780, 520)),
      server_(server),
      start_path_(std::move(start_path)) {
  set_icon(this);
  auto* panel = new wxPanel(this);
  path_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
  auto* up = new wxButton(panel, wxID_ANY, "Вверх");
  auto* mkdir = new wxButton(panel, wxID_ANY, "Новая папка");
  auto* upload_btn = new wxButton(panel, wxID_ANY, "Загрузить");
  auto* download_btn = new wxButton(panel, wxID_ANY, "Скачать");
  auto* del = new wxButton(panel, wxID_ANY, "Удалить");
  auto* stop = new wxButton(panel, wxID_ANY, "Стоп");
  list_ = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  list_->AppendColumn("Имя", wxLIST_FORMAT_LEFT, 280);
  list_->AppendColumn("Тип", wxLIST_FORMAT_LEFT, 120);
  list_->AppendColumn("Размер", wxLIST_FORMAT_RIGHT, 90);
  list_->AppendColumn("Изменён", wxLIST_FORMAT_LEFT, 140);
  gauge_ = new wxGauge(panel, wxID_ANY, 100);
  status_ = new wxStaticText(panel, wxID_ANY, "Подключение…");
  auto* top = new wxBoxSizer(wxHORIZONTAL);
  top->Add(path_, 1, wxEXPAND | wxRIGHT, 8);
  top->Add(up);
  auto* acts = new wxBoxSizer(wxHORIZONTAL);
  acts->Add(mkdir, 0, wxRIGHT, 4);
  acts->Add(upload_btn, 0, wxRIGHT, 4);
  acts->Add(download_btn, 0, wxRIGHT, 4);
  acts->Add(del, 0, wxRIGHT, 4);
  acts->Add(stop);
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(top, 0, wxEXPAND | wxALL, 8);
  root->Add(acts, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
  root->Add(list_, 1, wxEXPAND | wxALL, 8);
  root->Add(gauge_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
  root->Add(status_, 0, wxEXPAND | wxALL, 8);
  panel->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(panel, 1, wxEXPAND);
  SetSizer(outer);
  apply_dark(this);
  up->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { go_up(); });
  path_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
    try {
      session_.enter(std::string(path_->GetValue().utf8_string()));
      refresh();
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), "Файлы", wxOK | wxICON_ERROR, this);
    }
  });
  list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& e) {
    long i = e.GetIndex();
    if (i == 0) {
      go_up();
      return;
    }
    auto& ent = entries_[static_cast<std::size_t>(i - 1)];
    if (ent.is_dir) {
      try {
        session_.enter(ent.path);
        refresh();
      } catch (const std::exception& exc) {
        wxMessageBox(wxString::FromUTF8(exc.what()), "Файлы", wxOK | wxICON_ERROR, this);
      }
    } else {
      download_selected();
    }
  });
  mkdir->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxTextEntryDialog dlg(this, "Имя папки", "Новая папка");
    if (dlg.ShowModal() != wxID_OK) return;
    try {
      session_.mkdir(std::string(dlg.GetValue().utf8_string()));
      refresh();
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), "Файлы", wxOK | wxICON_ERROR, this);
    }
  });
  upload_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { upload(); });
  download_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { download_selected(); });
  del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (i <= 0) return;
    auto& ent = entries_[static_cast<std::size_t>(i - 1)];
    if (wxMessageBox("Удалить «" + wxString::FromUTF8(ent.name) + "»?", "Файлы", wxYES_NO, this) != wxYES) return;
    try {
      session_.remove(ent);
      refresh();
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(exc.what()), "Файлы", wxOK | wxICON_ERROR, this);
    }
  });
  stop->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { session_.cancel_transfer(); });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
    session_.close();
    e.Skip();
  });
  CallAfter([this] { connect(); });
}

FilesWindow::~FilesWindow() {
  session_.close();
}

void FilesWindow::connect() {
  try {
    session_.connect(server_, start_path_);
    refresh();
    status_->SetLabel("Готово");
  } catch (const std::exception& exc) {
    status_->SetLabel(wxString::FromUTF8(exc.what()));
    wxMessageBox(wxString::FromUTF8(exc.what()), "Файлы", wxOK | wxICON_ERROR, this);
  }
}

void FilesWindow::refresh() {
  entries_ = session_.listdir();
  path_->SetValue(wxString::FromUTF8(session_.remote_cwd));
  list_->DeleteAllItems();
  long row = list_->InsertItem(0, "..");
  list_->SetItem(row, 1, "папка");
  for (const auto& e : entries_) {
    row = list_->InsertItem(list_->GetItemCount(), wxString::FromUTF8(e.name));
    list_->SetItem(row, 1, wxString::FromUTF8(e.kind_label()));
    list_->SetItem(row, 2, wxString::FromUTF8(e.is_dir ? "—" : format_size(e.size)));
    list_->SetItem(row, 3, wxString::FromUTF8(format_mtime(e.mtime)));
  }
}

void FilesWindow::go_up() {
  try {
    session_.go_up();
    refresh();
  } catch (const std::exception& exc) {
    wxMessageBox(wxString::FromUTF8(exc.what()), "Файлы", wxOK | wxICON_ERROR, this);
  }
}

void FilesWindow::download_selected() {
  long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i <= 0) return;
  auto& ent = entries_[static_cast<std::size_t>(i - 1)];
  if (ent.is_dir) return;
  wxFileDialog dlg(this, "Сохранить как", "", wxString::FromUTF8(ent.name), "All|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  if (dlg.ShowModal() != wxID_OK) return;
  auto dest = std::filesystem::path(dlg.GetPath().utf8_string());
  std::thread([this, name = ent.name, size = ent.size, dest] {
    try {
      session_.download(name, dest, size, [this](long long sent, long long total) {
        CallAfter([this, sent, total] {
          gauge_->SetRange(static_cast<int>(total > 0 ? total : 1));
          gauge_->SetValue(static_cast<int>(sent > total ? total : sent));
        });
      });
      CallAfter([this] { status_->SetLabel("Скачано"); });
    } catch (const std::exception& exc) {
      CallAfter([this, msg = std::string(exc.what())] {
        wxMessageBox(wxString::FromUTF8(msg), "Файлы", wxOK | wxICON_ERROR, this);
      });
    }
  }).detach();
}

void FilesWindow::upload() {
  wxFileDialog dlg(this, "Загрузить", "", "", "All|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
  if (dlg.ShowModal() != wxID_OK) return;
  auto local = std::filesystem::path(dlg.GetPath().utf8_string());
  auto remote = local.filename().string();
  std::thread([this, local, remote] {
    try {
      session_.upload(local, remote, [this](long long sent, long long total) {
        CallAfter([this, sent, total] {
          gauge_->SetRange(static_cast<int>(total > 0 ? total : 1));
          gauge_->SetValue(static_cast<int>(sent));
        });
      });
      CallAfter([this] {
        status_->SetLabel("Загружено");
        refresh();
      });
    } catch (const std::exception& exc) {
      CallAfter([this, msg = std::string(exc.what())] {
        wxMessageBox(wxString::FromUTF8(msg), "Файлы", wxOK | wxICON_ERROR, this);
      });
    }
  }).detach();
}

}  // namespace fatty

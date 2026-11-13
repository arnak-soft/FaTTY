#include "ui/backup_journal_window.hpp"

#include "core/journal.hpp"
#include "core/util.hpp"
#include "ui/chrome.hpp"
#include "ui/dialogs.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/app.h>
#include <wx/clipbrd.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <fstream>

namespace fatty {

BackupJournalWindow::BackupJournalWindow(wxWindow* parent, std::shared_ptr<BackupJournal> journal,
                                         AppSettings* settings, std::function<void()> persist)
    : wxFrame(parent, wxID_ANY, L"Журнал резервных копий", wxDefaultPosition, wxDefaultSize),
      journal_(std::move(journal)) {
  set_icon(this);
  const bool had_geometry = settings && settings->dialog_geometry.count("backup_journal");
  SetSize(FromDIP(wxSize(960, 580)));
  setup_frame_geometry(this, settings, "backup_journal", true, std::move(persist));
  if (!had_geometry) CentreOnParent();

  auto* panel = new wxPanel(this);
  filter_ = new wxTextCtrl(panel, wxID_ANY);
  auto* copy = make_button(panel, L"Копировать", BtnIcon::Copy);
  auto* save = make_button(panel, L"Сохранить…", BtnIcon::Save);
  auto* del = make_button(panel, L"Удалить", BtnIcon::Trash);
  auto* clear = make_button(panel, L"Очистить", BtnIcon::Clear);

  auto* list_card = new RoundedCard(panel);
  list_ = new wxListCtrl(list_card, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                         wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
  list_->AppendColumn(L"Время", wxLIST_FORMAT_LEFT, FromDIP(150));
  list_->AppendColumn(L"Задание", wxLIST_FORMAT_LEFT, FromDIP(200));
  list_->AppendColumn(L"VPS", wxLIST_FORMAT_LEFT, FromDIP(120));
  list_->AppendColumn(L"Результат", wxLIST_FORMAT_LEFT, FromDIP(120));
  list_->AppendColumn(L"Длит.", wxLIST_FORMAT_LEFT, FromDIP(80));
  auto* list_sz = new wxBoxSizer(wxVERTICAL);
  list_sz->Add(list_, 1, wxEXPAND);
  list_card->SetSizer(list_sz);

  auto* detail_card = new RoundedCard(panel);
  detail_ = new wxTextCtrl(detail_card, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
  style_text(detail_, true);
  auto* detail_sz = new wxBoxSizer(wxVERTICAL);
  detail_sz->Add(detail_, 1, wxEXPAND);
  detail_card->SetSizer(detail_sz);
  bind_copy_on_select(detail_);

  auto* top = new wxBoxSizer(wxHORIZONTAL);
  top->Add(new wxStaticText(panel, wxID_ANY, L"Фильтр"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  top->Add(filter_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  filter_->SetMinSize(wxSize(-1, copy->GetBestSize().GetHeight()));
  top->Add(copy, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  top->Add(save, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  top->Add(del, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  top->Add(clear, 0, wxALIGN_CENTER_VERTICAL);

  auto* split = new wxBoxSizer(wxVERTICAL);
  split->Add(list_card, 1, wxEXPAND);
  split->Add(detail_card, 1, wxEXPAND | wxTOP, 8);
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(top, 0, wxEXPAND | wxALL, 8);
  root->Add(split, 1, wxEXPAND | wxALL, 8);
  panel->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(panel, 1, wxEXPAND);
  SetSizer(outer);
  apply_dark(this);

  reload();
  filter_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { reload(); });
  list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& e) {
    long i = e.GetIndex();
    if (i >= 0 && i < static_cast<long>(entries_.size())) {
      detail_->SetValue(wxString::FromUTF8(entries_[static_cast<std::size_t>(i)].as_text()));
    }
  });
  copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (i < 0) return;
    auto text = entries_[static_cast<std::size_t>(i)].as_text();
    if (wxTheClipboard->Open()) {
      wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(text)));
      wxTheClipboard->Close();
    }
  });
  save->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxFileDialog dlg(this, L"Сохранить журнал резервов", L"", L"backup_journal.txt", L"Text|*.txt",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;
    std::ofstream out(std::filesystem::path(dlg.GetPath().utf8_string()), std::ios::binary);
    out << journal_->export_text(&entries_);
  });
  del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { delete_selected(); });
  clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (!ask_confirm(this, L"Журнал резервов", L"Очистить весь журнал резервных копий?", L"Очистить", false,
                     BtnIcon::Trash))
      return;
    journal_->clear();
    detail_->Clear();
    reload();
  });
  list_->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, [this](wxListEvent& e) {
    list_->SetItemState(e.GetIndex(), wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                        wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    wxMenu menu;
    menu.Append(1, L"Удалить запись");
    menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { delete_selected(); }, 1);
    show_themed_menu(this, &menu);
  });
  list_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
    if (e.GetKeyCode() == WXK_DELETE) {
      delete_selected();
      return;
    }
    e.Skip();
  });

  if (journal_) {
    listener_id_ = journal_->add_listener([this, alive = alive_] {
      wxTheApp->CallAfter([this, alive] {
        if (!alive->load()) return;
        reload();
      });
    });
  }
}

BackupJournalWindow::~BackupJournalWindow() {
  *alive_ = false;
  if (journal_ && listener_id_) journal_->remove_listener(listener_id_);
}

void BackupJournalWindow::delete_selected() {
  long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i < 0 || i >= static_cast<long>(entries_.size())) return;
  if (!ask_confirm(this, L"Удалить", L"Удалить выбранную запись журнала?", L"Удалить", false, BtnIcon::Trash))
    return;
  journal_->remove(entries_[static_cast<std::size_t>(i)].id);
  detail_->Clear();
  reload();
}

void BackupJournalWindow::reload() {
  if (!journal_ || !list_) return;
  const auto filter = to_lower(trim(std::string(filter_->GetValue().utf8_string())));
  entries_.clear();
  for (auto& e : journal_->load()) {
    if (!filter.empty()) {
      const auto hay = to_lower(e.job_name + " " + e.server_name + " " + e.status_display() + " " +
                                e.kind_label + " " + e.dest_label + " " + e.error + " " + e.archive_name);
      if (hay.find(filter) == std::string::npos) continue;
    }
    entries_.push_back(std::move(e));
  }
  list_->DeleteAllItems();
  for (long row = 0; row < static_cast<long>(entries_.size()); ++row) {
    const auto& e = entries_[static_cast<std::size_t>(row)];
    list_->InsertItem(row, wxString::FromUTF8(e.started_display()));
    list_->SetItem(row, 1, wxString::FromUTF8(e.job_name.empty() ? "—" : e.job_name));
    list_->SetItem(row, 2, wxString::FromUTF8(e.server_name.empty() ? "—" : e.server_name));
    list_->SetItem(row, 3, wxString::FromUTF8(e.status_display()));
    list_->SetItem(row, 4, wxString::FromUTF8(format_duration(e.duration_sec)));
  }
}

}  // namespace fatty

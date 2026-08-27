#include "ui/journal_window.hpp"

#include "core/util.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/app.h>
#include <wx/clipbrd.h>
#include <wx/button.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <algorithm>
#include <fstream>

namespace fatty {

JournalWindow::JournalWindow(wxWindow* parent, std::shared_ptr<Journal> journal,
                             std::function<void(const JournalEntry&)> on_rerun)
    : wxFrame(parent, wxID_ANY, "Журнал команд", wxDefaultPosition, wxDefaultSize),
      journal_(std::move(journal)),
      on_rerun_(std::move(on_rerun)) {
  set_icon(this);
  SetSize(FromDIP(wxSize(960, 580)));
  auto* panel = new wxPanel(this);
  filter_ = new wxTextCtrl(panel, wxID_ANY);
  auto* rerun = new wxButton(panel, wxID_ANY, "Повтор");
  auto* copy = new wxButton(panel, wxID_ANY, "Копировать");
  auto* save = new wxButton(panel, wxID_ANY, "Сохранить…");
  auto* del = new wxButton(panel, wxID_ANY, "Удалить");
  auto* clear = new wxButton(panel, wxID_ANY, "Очистить");
  list_ = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  list_->AppendColumn("Время", wxLIST_FORMAT_LEFT, FromDIP(150));
  list_->AppendColumn("VPS", wxLIST_FORMAT_LEFT, FromDIP(140));
  list_->AppendColumn("Команда", wxLIST_FORMAT_LEFT, FromDIP(280));
  list_->AppendColumn("Результат", wxLIST_FORMAT_LEFT, FromDIP(100));
  list_->AppendColumn("Длит.", wxLIST_FORMAT_LEFT, FromDIP(90));
  detail_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
  style_text(detail_, true);
  bind_copy_on_select(detail_);
  auto* top = new wxBoxSizer(wxHORIZONTAL);
  top->Add(new wxStaticText(panel, wxID_ANY, "Фильтр"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  top->Add(filter_, 1, wxEXPAND | wxRIGHT, 8);
  top->Add(rerun, 0, wxRIGHT, 4);
  top->Add(copy, 0, wxRIGHT, 4);
  top->Add(save, 0, wxRIGHT, 4);
  top->Add(del, 0, wxRIGHT, 4);
  top->Add(clear);
  auto* split = new wxBoxSizer(wxVERTICAL);
  split->Add(list_, 1, wxEXPAND);
  split->Add(detail_, 1, wxEXPAND | wxTOP, 8);
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
  rerun->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (i >= 0 && on_rerun_) on_rerun_(entries_[static_cast<std::size_t>(i)]);
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
    wxFileDialog dlg(this, "Сохранить журнал", "", "journal.txt", "Text|*.txt", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;
    std::ofstream out(std::filesystem::path(dlg.GetPath().utf8_string()), std::ios::binary);
    out << journal_->export_text(&entries_);
  });
  del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { delete_selected(); });
  clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (wxMessageBox("Очистить журнал?", "Журнал", wxYES_NO, this) != wxYES) return;
    journal_->clear();
    detail_->Clear();
    reload();
  });
  Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
    if (e.GetKeyCode() == WXK_DELETE && wxWindow::FindFocus() != filter_) {
      long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
      if (i >= 0) {
        delete_selected();
        return;
      }
    }
    e.Skip();
  });
  // Обновляемся сами, когда во время открытого окна завершилась команда.
  // Слушателя зовёт в том числе поток команды, поэтому — через wxTheApp и токен.
  listener_id_ = journal_->add_listener([this, alive = alive_] {
    if (!alive->load()) return;
    wxTheApp->CallAfter([this, alive] {
      if (!alive->load()) return;
      reload_preserving_selection();
    });
  });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
    alive_->store(false);
    journal_->remove_listener(listener_id_);
    e.Skip();
  });
  bind_escape_close(this);
}

JournalWindow::~JournalWindow() {
  // Родитель может уничтожить окно без wxEVT_CLOSE_WINDOW — снимаем подписку и здесь.
  alive_->store(false);
  journal_->remove_listener(listener_id_);
}

void JournalWindow::reload_preserving_selection() {
  long sel = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  std::string keep_id;
  if (sel >= 0 && sel < static_cast<long>(entries_.size())) keep_id = entries_[static_cast<std::size_t>(sel)].id;
  reload();
  if (keep_id.empty()) return;
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].id == keep_id) {
      list_->SetItemState(static_cast<long>(i), wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                          wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
      break;
    }
  }
}

void JournalWindow::delete_selected() {
  long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i < 0 || i >= static_cast<long>(entries_.size())) return;
  const auto& e = entries_[static_cast<std::size_t>(i)];
  auto preview = e.command_preview(48);
  auto msg = preview.empty() ? wxString("Удалить эту запись из журнала?")
                             : wxString::FromUTF8("Удалить запись «" + preview + "»?");
  if (wxMessageBox(msg, "Журнал", wxYES_NO | wxNO_DEFAULT, this) != wxYES) return;
  journal_->remove(e.id);
  detail_->Clear();
  reload();
  if (list_->GetItemCount() == 0) return;
  long next = std::min(i, static_cast<long>(list_->GetItemCount()) - 1);
  list_->SetItemState(next, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                      wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
  if (next >= 0 && next < static_cast<long>(entries_.size())) {
    detail_->SetValue(wxString::FromUTF8(entries_[static_cast<std::size_t>(next)].as_text()));
  }
}

void JournalWindow::reload() {
  auto all = journal_->load();
  auto q = to_lower(trim(std::string(filter_->GetValue().utf8_string())));
  entries_.clear();
  for (const auto& e : all) {
    if (!q.empty()) {
      auto hay = to_lower(e.server_name + " " + e.command + " " + e.title);
      if (hay.find(q) == std::string::npos) continue;
    }
    entries_.push_back(e);
  }
  list_->DeleteAllItems();
  for (const auto& e : entries_) {
    long row = list_->InsertItem(list_->GetItemCount(), wxString::FromUTF8(e.started_display()));
    list_->SetItem(row, 1, wxString::FromUTF8(e.server_name));
    list_->SetItem(row, 2, wxString::FromUTF8(e.command_preview(80)));
    list_->SetItem(row, 3, wxString::FromUTF8(e.status_display()));
    list_->SetItem(row, 4, wxString::FromUTF8(e.duration_display()));
    wxColour c = Theme::text();
    if (e.status == "ok") c = Theme::ok();
    else if (e.status == "timeout") c = Theme::warn();
    else if (e.status == "cancelled") c = Theme::cancel();
    else c = Theme::err();
    list_->SetItemTextColour(row, c);
  }
}

}  // namespace fatty

#include "ui/file_editor_dialog.hpp"

#include "ui/chrome.hpp"
#include "ui/dialogs.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace fatty {

FileEditorDialog::FileEditorDialog(wxWindow* parent, const wxString& title, RemoteTextView view)
    : PositionedDialog(parent, title, wxSize(720, 560)), view_(std::move(view)) {
  SetName(L"card-page");
  auto* body = new wxPanel(this);
  body->SetName(L"card-page");
  text_ = new wxTextCtrl(body, wxID_ANY, wxString::FromUTF8(view_.display_utf8), wxDefaultPosition, wxDefaultSize,
                         wxTE_MULTILINE | wxTE_RICH2 | wxTE_PROCESS_TAB | wxHSCROLL);
  style_text(text_, false);
  text_->SetFont(Theme::mono());
  text_->SetMinSize(FromDIP(wxSize(560, 360)));
  meta_ = new wxStaticText(body, wxID_ANY, L"");
  meta_->SetName(L"muted");
  meta_->SetForegroundColour(Theme::muted());

  auto* save = accent_button(body, L"Сохранить", BtnIcon::Save);
  auto* cancel = make_button(body, L"Закрыть", BtnIcon::Cancel);
  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->Add(meta_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
  btns->Add(save, 0, wxRIGHT, FromDIP(8));
  btns->Add(cancel, 0);

  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(text_, 1, wxEXPAND | wxALL, FromDIP(10));
  root->Add(btns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
  body->SetSizer(root);

  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(body, 1, wxEXPAND);
  SetSizer(outer);

  text_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
    dirty_ = true;
    update_meta();
  });
  save->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_save(); });
  cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (!confirm_discard()) return;
    EndModal(wxID_CANCEL);
  });
  Bind(wxEVT_CLOSE_WINDOW, &FileEditorDialog::on_close, this);
  Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
    if (e.ControlDown() && (e.GetKeyCode() == 'S' || e.GetKeyCode() == 's')) {
      on_save();
      return;
    }
    e.Skip();
  });

  update_meta();
  apply_theme(this);
  CentreOnParent();
}

void FileEditorDialog::update_meta() {
  wxString line = L"UTF-8 · ";
  line += wxString::FromUTF8(text_eol_label(view_.eol));
  if (dirty_) line += L" · не сохранено";
  else if (saved) line += L" · сохранено";
  meta_->SetLabel(line);
}

bool FileEditorDialog::confirm_discard() {
  if (!dirty_) return true;
  return ask_confirm(this, L"Редактор", L"Закрыть без сохранения?", L"Закрыть", false);
}

void FileEditorDialog::on_save() {
  const std::string normalized = normalize_editor_newlines(std::string(text_->GetValue().utf8_string()));
  const auto check = decode_remote_text(normalized);
  if (!check.ok) {
    wxMessageBox(wxString::FromUTF8(check.error.empty() ? "Текст нельзя сохранить как UTF-8." : check.error),
                 L"Редактор", wxOK | wxICON_WARNING, this);
    return;
  }
  result_bytes = encode_remote_text(normalized, view_.eol);
  const auto roundtrip = decode_remote_text(result_bytes);
  if (!roundtrip.ok || roundtrip.view.eol != view_.eol) {
    wxMessageBox(L"Не удалось подготовить файл с исходными переводами строк. Сохранение отменено.", L"Редактор",
                 wxOK | wxICON_WARNING, this);
    return;
  }
  view_.display_utf8 = normalized;
  dirty_ = false;
  saved = true;
  update_meta();
  EndModal(wxID_OK);
}

void FileEditorDialog::on_close(wxCloseEvent& e) {
  if (!confirm_discard()) {
    e.Veto();
    return;
  }
  if (IsModal()) EndModal(saved ? wxID_OK : wxID_CANCEL);
  else e.Skip();
}

}  // namespace fatty

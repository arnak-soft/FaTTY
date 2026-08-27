#include "ui/widgets.hpp"

#include "core/paths.hpp"
#include "ui/theme.hpp"

#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/dialog.h>
#include <wx/icon.h>
#include <wx/stattext.h>
#include <wx/toplevel.h>

namespace fatty {

void bind_copy_on_select(wxTextCtrl* ctrl, std::function<void(const std::string&)> on_copied) {
  auto copy_sel = [ctrl, on_copied](wxMouseEvent& event) {
    event.Skip();
    long from = 0, to = 0;
    ctrl->GetSelection(&from, &to);
    if (from == to) return;
    auto text = ctrl->GetStringSelection();
    if (text.empty()) return;
    if (wxTheClipboard->Open()) {
      wxTheClipboard->SetData(new wxTextDataObject(text));
      wxTheClipboard->Close();
    }
    if (on_copied) on_copied(std::string(text.utf8_string()));
  };
  ctrl->Bind(wxEVT_LEFT_UP, copy_sel);
  ctrl->Bind(wxEVT_CHAR_HOOK, [ctrl, on_copied](wxKeyEvent& event) {
    if (event.ControlDown() && event.GetKeyCode() == 'C') {
      auto text = ctrl->GetStringSelection();
      if (!text.empty() && wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(text));
        wxTheClipboard->Close();
        if (on_copied) on_copied(std::string(text.utf8_string()));
      }
      return;
    }
    event.Skip();
  });
}

void bind_escape_close(wxWindow* window) {
  window->Bind(wxEVT_CHAR_HOOK, [window](wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_ESCAPE) {
      if (auto* dlg = dynamic_cast<wxDialog*>(window); dlg && dlg->IsModal()) {
        dlg->EndModal(wxID_CANCEL);
      } else {
        window->Close();
      }
      return;
    }
    event.Skip();
  });
}

void set_icon(wxWindow* window) {
  auto ico = resource_root() / "assets" / "app.ico";
  if (!std::filesystem::exists(ico)) ico = resource_root() / "app.ico";
  if (std::filesystem::exists(ico)) {
    wxIcon icon;
    const auto u8 = ico.u8string();
    icon.LoadFile(wxString::FromUTF8(reinterpret_cast<const char*>(u8.c_str()), u8.size()), wxBITMAP_TYPE_ICO);
    if (icon.IsOk()) {
      if (auto* top = dynamic_cast<wxTopLevelWindow*>(window)) top->SetIcon(icon);
    }
  }
}

wxButton* accent_button(wxWindow* parent, const wxString& label, wxWindowID id) {
  auto* btn = new wxButton(parent, id, label);
  btn->SetName("accent");
  btn->SetBackgroundColour(Theme::accent());
  btn->SetForegroundColour(*wxWHITE);
  return btn;
}

wxStaticText* section_label(wxWindow* parent, const wxString& text) {
  auto* label = new wxStaticText(parent, wxID_ANY, text.Upper());
  label->SetName("section");
  label->SetFont(Theme::ui_section());
  label->SetForegroundColour(Theme::muted());
  return label;
}

}  // namespace fatty

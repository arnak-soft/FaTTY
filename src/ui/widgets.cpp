#include "ui/widgets.hpp"

#include "core/paths.hpp"
#include "ui/theme.hpp"

#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/icon.h>

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

void set_icon(wxWindow* window) {
  auto ico = resource_root() / "assets" / "app.ico";
  if (!std::filesystem::exists(ico)) ico = resource_root() / "app.ico";
  if (std::filesystem::exists(ico)) {
    wxIcon icon;
    icon.LoadFile(wxString::FromUTF8(ico.u8string()), wxBITMAP_TYPE_ICO);
    if (icon.IsOk()) window->SetIcon(icon);
  }
}

wxButton* accent_button(wxWindow* parent, const wxString& label, wxWindowID id) {
  auto* btn = new wxButton(parent, id, label);
  btn->SetName("accent");
  btn->SetBackgroundColour(Theme::accent());
  btn->SetForegroundColour(*wxWHITE);
  return btn;
}

}  // namespace fatty

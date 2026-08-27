#pragma once

#include <wx/textctrl.h>
#include <wx/window.h>
#include <functional>
#include <string>

namespace fatty {

void bind_copy_on_select(wxTextCtrl* ctrl, std::function<void(const std::string&)> on_copied = {});
void bind_escape_close(wxWindow* window);
void set_icon(wxWindow* window);
wxButton* accent_button(wxWindow* parent, const wxString& label, wxWindowID id = wxID_ANY);

}  // namespace fatty

#pragma once

#include "ui/theme.hpp"
#include "ui/chrome.hpp"

#include <wx/textctrl.h>
#include <wx/window.h>
#include <functional>
#include <string>

class wxStaticText;

namespace fatty {

void bind_copy_on_select(wxTextCtrl* ctrl, std::function<void(const std::string&)> on_copied = {});
void bind_escape_close(wxWindow* window);
void set_icon(wxWindow* window);
wxStaticText* section_label(wxWindow* parent, const wxString& text);

}  // namespace fatty

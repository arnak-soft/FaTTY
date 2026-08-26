#pragma once

#include <wx/frame.h>
#include <wx/notebook.h>
#include <functional>
#include <string>

namespace fatty {

class HelpWindow : public wxFrame {
 public:
  HelpWindow(wxWindow* parent, std::function<void(const std::string&)> on_insert_quick);
  void show_tab(const std::string& name);

 private:
  wxNotebook* nb_{};
};

}  // namespace fatty

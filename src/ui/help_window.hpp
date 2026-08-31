#pragma once

#include "core/store.hpp"
#include "ui/chrome.hpp"

#include <wx/frame.h>
#include <functional>
#include <string>

namespace fatty {

class HelpWindow : public wxFrame {
 public:
  HelpWindow(wxWindow* parent, std::function<void(const std::string&)> on_insert_quick, AppSettings* settings = nullptr,
             std::function<void()> persist = {});
  void show_tab(const std::string& name);

 private:
  RoundedNotebook* nb_{};
};

}  // namespace fatty

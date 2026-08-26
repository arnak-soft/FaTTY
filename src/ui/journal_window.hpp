#pragma once

#include "core/journal.hpp"
#include "ui/layout.hpp"

#include <wx/frame.h>
#include <wx/listctrl.h>
#include <functional>

namespace fatty {

class JournalWindow : public wxFrame {
 public:
  JournalWindow(wxWindow* parent, Journal& journal, std::function<void(const JournalEntry&)> on_rerun);

 private:
  void reload();
  Journal& journal_;
  std::function<void(const JournalEntry&)> on_rerun_;
  std::vector<JournalEntry> entries_;
  wxListCtrl* list_{};
  wxTextCtrl* detail_{};
  wxTextCtrl* filter_{};
};

}  // namespace fatty

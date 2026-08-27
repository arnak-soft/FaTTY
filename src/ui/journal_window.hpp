#pragma once

#include "core/journal.hpp"
#include "ui/layout.hpp"

#include <wx/frame.h>
#include <wx/listctrl.h>
#include <atomic>
#include <functional>
#include <memory>

namespace fatty {

class JournalWindow : public wxFrame {
 public:
  JournalWindow(wxWindow* parent, std::shared_ptr<Journal> journal,
                std::function<void(const JournalEntry&)> on_rerun);
  ~JournalWindow() override;

 private:
  void reload();
  void reload_preserving_selection();
  void delete_selected();
  std::shared_ptr<Journal> journal_;
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
  std::function<void(const JournalEntry&)> on_rerun_;
  std::vector<JournalEntry> entries_;
  wxListCtrl* list_{};
  wxTextCtrl* detail_{};
  wxTextCtrl* filter_{};
  int listener_id_ = 0;
};

}  // namespace fatty

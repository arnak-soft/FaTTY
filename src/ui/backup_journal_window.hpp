#pragma once

#include "core/backup_journal.hpp"
#include "ui/layout.hpp"

#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/textctrl.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fatty {

class BackupJournalWindow : public wxFrame {
 public:
  BackupJournalWindow(wxWindow* parent, std::shared_ptr<BackupJournal> journal,
                      AppSettings* settings = nullptr, std::function<void()> persist = {});
  ~BackupJournalWindow() override;

 private:
  void reload();
  void delete_selected();

  std::shared_ptr<BackupJournal> journal_;
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
  std::vector<BackupJournalEntry> entries_;
  wxListCtrl* list_{};
  wxTextCtrl* detail_{};
  wxTextCtrl* filter_{};
  int listener_id_ = 0;
};

}  // namespace fatty

#pragma once

#include "core/store.hpp"
#include "ui/backup_monitor.hpp"
#include "ui/chrome.hpp"
#include "ui/layout.hpp"

#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace fatty {

class BackupWindow : public wxFrame {
 public:
  BackupWindow(wxWindow* parent, Config* config, BackupMonitor* monitor, AppSettings* settings = nullptr,
               std::function<void()> persist = {});
  ~BackupWindow() override;
  void reload();
  void append_log(const std::string& line);

 private:
  void rebuild_list();
  void add_job();
  void edit_selected();
  void delete_selected();
  void run_selected();
  void install_cron_selected();
  void uninstall_cron_selected();
  BackupJob* selected_job();

  Config* config_ = nullptr;
  BackupMonitor* monitor_ = nullptr;
  AppSettings* settings_ = nullptr;
  std::function<void()> persist_;
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
  wxListCtrl* list_{};
  wxTextCtrl* log_{};
  wxStaticText* hint_{};
  RoundButton* run_btn_{};
  RoundButton* cron_btn_{};
  RoundButton* uncron_btn_{};
};

bool edit_backup_job_dialog(wxWindow* parent, BackupJob& job, Config& config, AppSettings* settings,
                            std::function<void()> persist);

}  // namespace fatty

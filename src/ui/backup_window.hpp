#pragma once

#include "core/store.hpp"
#include "ui/backup_monitor.hpp"
#include "ui/chrome.hpp"

#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fatty {

/// Панель заданий резервного копирования VPS (встраивается в главное окно).
class BackupWindow : public wxPanel {
 public:
  BackupWindow(wxWindow* parent, Config* config, BackupMonitor* monitor, AppSettings* settings = nullptr,
               std::function<void()> persist = {}, std::string preferred_server_id = {});
  ~BackupWindow() override;
  void reload();
  void append_log(const std::string& line);
  void set_preferred_server(const std::string& server_id);
  const std::string& preferred_server() const { return preferred_server_id_; }

 private:
  void rebuild_list();
  void add_job();
  void edit_selected();
  void delete_selected();
  void duplicate_selected();
  void run_selected();
  void install_cron_selected();
  void uninstall_cron_selected();
  BackupJob* selected_job();
  std::string default_source_server_id() const;

  Config* config_ = nullptr;
  BackupMonitor* monitor_ = nullptr;
  AppSettings* settings_ = nullptr;
  std::function<void()> persist_;
  std::string preferred_server_id_;
  std::vector<std::string> row_job_ids_;
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
  wxListCtrl* list_{};
  wxTextCtrl* log_{};
  wxStaticText* hint_{};
  ThemedCheckBox* show_all_cb_{};
  RoundButton* run_btn_{};
  RoundButton* cron_btn_{};
  RoundButton* uncron_btn_{};
};

bool edit_backup_job_dialog(wxWindow* parent, BackupJob& job, Config& config, AppSettings* settings,
                            std::function<void()> persist);

}  // namespace fatty

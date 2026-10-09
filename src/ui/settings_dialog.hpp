#pragma once

#include "core/store.hpp"
#include "core/vault.hpp"
#include "ui/chrome.hpp"
#include "ui/layout.hpp"
#include "ui/striped_list.hpp"

#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <functional>
#include <vector>

namespace fatty {

class SettingsDialog : public PositionedDialog {
 public:
  SettingsDialog(wxWindow* parent, Config& config, SessionVault& vault, std::function<void()> on_apply,
                 std::function<void()> on_change_master, std::function<void()> on_check_updates,
                 std::function<void()> on_import_done);

 private:
  void on_save(wxCommandEvent&);
  void refresh_extra_list();
  void edit_extra(long index);
  Config& config_;
  SessionVault& vault_;
  std::function<void()> on_apply_;
  std::function<void()> on_change_master_;
  std::function<void()> on_check_updates_;
  std::function<void()> on_import_done_;
  ThemedCheckBox* confirm_{};
  ThemedCheckBox* updates_{};
  ThemedCheckBox* clear_output_{};
  ThemedCheckBox* automation_to_shell_{};
  ThemedCheckBox* shell_follow_cwd_{};
  ThemedCheckBox* advance_command_{};
  ThemedCheckBox* show_folder_col_{};
  ThemedCheckBox* backup_{};
  ThemedChoice* theme_{};
  ThemedChoice* terminal_font_{};
  wxTextCtrl* timeout_{};
  wxTextCtrl* journal_{};
  wxTextCtrl* putty_{};
  wxTextCtrl* winscp_{};
  wxTextCtrl* ssh_{};
  StripedListCtrl* extra_list_{};
  std::vector<ExtraProgram> extra_programs_;
  ThemedCheckBox* export_secrets_{};
  ThemedCheckBox* export_settings_{};
  ThemedCheckBox* import_settings_{};
  ThemedCheckBox* short_pw_{};
  wxTextCtrl* lockout_attempts_{};
  wxTextCtrl* lockout_minutes_{};
  ThemedCheckBox* health_auto_{};
  ThemedChoice* health_interval_{};
  wxTextCtrl* health_interval_sec_{};
  wxTextCtrl* health_timeout_{};
  wxTextCtrl* health_disk_warn_{};
  wxTextCtrl* health_disk_crit_{};
  wxTextCtrl* health_ram_warn_{};
  wxTextCtrl* health_ram_crit_{};
  wxTextCtrl* health_swap_warn_{};
  wxTextCtrl* health_swap_crit_{};
  wxTextCtrl* health_cpu_warn_{};
  wxTextCtrl* health_cpu_crit_{};
  wxTextCtrl* health_load_warn_{};
  wxTextCtrl* health_load_crit_{};
  ThemedCheckBox* health_cpu_{};
  ThemedCheckBox* health_ram_{};
  ThemedCheckBox* health_swap_{};
  ThemedCheckBox* health_disk_{};
  ThemedCheckBox* health_load_{};
  ThemedCheckBox* health_docker_disks_{};
};

}  // namespace fatty

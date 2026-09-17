#pragma once

#include "core/store.hpp"
#include "core/vault.hpp"
#include "ui/layout.hpp"
#include "ui/striped_list.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
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
  wxCheckBox* confirm_{};
  wxCheckBox* updates_{};
  wxCheckBox* clear_output_{};
  wxCheckBox* advance_command_{};
  wxCheckBox* show_folder_col_{};
  wxCheckBox* backup_{};
  wxChoice* theme_{};
  wxTextCtrl* timeout_{};
  wxTextCtrl* journal_{};
  wxTextCtrl* putty_{};
  wxTextCtrl* winscp_{};
  wxTextCtrl* ssh_{};
  StripedListCtrl* extra_list_{};
  std::vector<ExtraProgram> extra_programs_;
  wxCheckBox* export_secrets_{};
  wxCheckBox* export_settings_{};
  wxCheckBox* import_settings_{};
  wxCheckBox* short_pw_{};
  wxTextCtrl* lockout_attempts_{};
  wxTextCtrl* lockout_minutes_{};
  wxCheckBox* health_auto_{};
  wxChoice* health_interval_{};
  wxTextCtrl* health_interval_sec_{};
  wxTextCtrl* health_timeout_{};
  wxTextCtrl* health_disk_warn_{};
  wxTextCtrl* health_disk_crit_{};
  wxTextCtrl* health_ram_warn_{};
  wxTextCtrl* health_ram_crit_{};
  wxTextCtrl* health_cpu_warn_{};
  wxTextCtrl* health_cpu_crit_{};
  wxCheckBox* health_cpu_{};
  wxCheckBox* health_ram_{};
  wxCheckBox* health_disk_{};
  wxCheckBox* health_load_{};
};

}  // namespace fatty

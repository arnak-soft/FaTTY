#pragma once

#include "core/journal.hpp"
#include "core/store.hpp"
#include "ui/striped_list.hpp"

#include <wx/frame.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace fatty {

class BundleStepsWindow : public wxFrame {
 public:
  BundleStepsWindow(wxWindow* parent, Config& config, const std::map<std::string, CommandRunStats>* command_stats,
                    std::function<void(const Command&)> on_run,
                    std::function<void(const std::string& bundle_id)> on_run_bundle, AppSettings* settings = nullptr,
                    std::function<void()> persist = {});
  void show_bundle(const std::string& bundle_id);
  void refresh();
  void await_step(const std::string& command_id, const std::string& name);
  void notify_bundle_finished(const std::string& bundle_id, const std::string& name, const std::string& reason);
  /// Перекрасить окно в цвет VPS этой связки (не зависит от выбранного VPS в главном окне).
  void apply_server_look();

 private:
  void reload(const std::string& keep_command_id = {});
  void show_detail();
  void clear_detail();
  void run_selected();
  void run_bundle();
  std::string group_label(const Command& cmd) const;
  void maybe_announce_awaited_step();
  void set_notice(const wxString& text, const char* tone = "meta");
  void clear_notice();
  void on_notice_timer(wxTimerEvent&);

  Config& config_;
  const std::map<std::string, CommandRunStats>* command_stats_{};
  std::function<void(const Command&)> on_run_;
  std::function<void(const std::string& bundle_id)> on_run_bundle_;
  std::string bundle_id_;
  std::string awaiting_id_;
  std::string awaiting_name_;
  std::string awaiting_since_;
  std::vector<Command> steps_;
  wxPanel* accent_bar_{};
  StripedListCtrl* list_{};
  wxStaticText* hint_{};
  wxStaticText* detail_title_{};
  wxStaticText* detail_group_{};
  wxStaticText* detail_folder_{};
  wxStaticText* detail_last_{};
  wxStaticText* detail_avg_{};
  wxStaticText* detail_comment_{};
  wxTextCtrl* detail_command_{};
  wxStaticText* notice_{};
  wxTimer notice_timer_;
};

}  // namespace fatty

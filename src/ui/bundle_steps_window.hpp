#pragma once

#include "core/store.hpp"
#include "ui/striped_list.hpp"

#include <wx/frame.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <functional>
#include <string>
#include <vector>

namespace fatty {

class BundleStepsWindow : public wxFrame {
 public:
  BundleStepsWindow(wxWindow* parent, Config& config, std::function<void(const Command&)> on_run,
                    std::function<void(const std::string& bundle_id)> on_run_bundle, AppSettings* settings = nullptr,
                    std::function<void()> persist = {});
  void show_bundle(const std::string& bundle_id);

 private:
  void reload();
  void show_detail();
  void run_selected();
  void run_bundle();
  std::string group_label(const Command& cmd) const;

  Config& config_;
  std::function<void(const Command&)> on_run_;
  std::function<void(const std::string& bundle_id)> on_run_bundle_;
  std::string bundle_id_;
  std::vector<Command> steps_;
  StripedListCtrl* list_{};
  wxTextCtrl* detail_{};
  wxStaticText* hint_{};
};

}  // namespace fatty

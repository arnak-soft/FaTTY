#pragma once

#include "core/health.hpp"
#include "core/store.hpp"
#include "ui/health_monitor.hpp"
#include "ui/layout.hpp"

#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/stattext.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fatty {

class HealthWindow : public wxFrame {
 public:
  HealthWindow(wxWindow* parent, HealthMonitor* monitor, Config* config,
               std::function<void(const std::string&)> on_refresh, std::function<void()> on_refresh_all,
               AppSettings* settings = nullptr, std::function<void()> persist = {});
  ~HealthWindow() override;

  void reload();
  void select_server(const std::string& id);

 private:
  void rebuild_cards();
  void show_detail();
  std::string selected_id() const;

  HealthMonitor* monitor_ = nullptr;
  Config* config_ = nullptr;
  AppSettings* settings_ = nullptr;
  std::function<void(const std::string&)> on_refresh_;
  std::function<void()> on_refresh_all_;
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
  std::string selected_id_;
  wxScrolledWindow* list_{};
  wxBoxSizer* list_sz_{};
  wxPanel* detail_{};
  wxStaticText* status_{};
  wxStaticText* auto_label_{};
};

}  // namespace fatty

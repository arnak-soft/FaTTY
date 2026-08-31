#pragma once

#include "core/store.hpp"
#include "ui/striped_list.hpp"

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace fatty {

inline constexpr const char* kDefaultGeometry = "1100x720";

bool parse_geometry(const std::string& geom, int& w, int& h, int& x, int& y, bool& has_pos);
bool geometry_on_screen(const std::string& geom, int min_w = 400, int min_h = 300);
std::string window_geometry(wxWindow* window);
void restore_window_geometry(wxWindow* window, const std::string& geom, bool remember_size);
void restore_dialog_geometry(wxWindow* window, AppSettings& settings, const std::string& key, bool remember_size);
void store_dialog_geometry(wxWindow* window, AppSettings& settings, const std::string& key, bool remember_size);
void setup_frame_geometry(wxWindow* window, AppSettings* settings, const std::string& key, bool remember_size = true,
                          std::function<void()> persist = {});
void apply_list_columns(wxListCtrl* list, const std::map<std::string, int>& widths, const std::vector<std::string>& ids);
void store_list_columns(wxListCtrl* list, AppSettings& settings, const std::string& key,
                        const std::vector<std::string>& ids);
void apply_list_columns(StripedListCtrl* list, const std::map<std::string, int>& widths,
                        const std::vector<std::string>& ids);
void store_list_columns(StripedListCtrl* list, AppSettings& settings, const std::string& key,
                        const std::vector<std::string>& ids);

class PositionedDialog : public wxDialog {
 public:
  PositionedDialog(wxWindow* parent, const wxString& title, const wxSize& size = wxDefaultSize);
  void setup_layout(AppSettings* settings, const std::string& key, bool remember_size = false,
                    std::function<void()> persist = {});

 protected:
  void on_close_layout(wxCloseEvent& event);

  AppSettings* settings_ = nullptr;
  std::string key_;
  bool remember_size_ = false;
  std::function<void()> persist_;
};

}  // namespace fatty

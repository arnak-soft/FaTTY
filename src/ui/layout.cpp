#include "ui/layout.hpp"

#include "core/util.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <regex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fatty {

bool parse_geometry(const std::string& geom, int& w, int& h, int& x, int& y, bool& has_pos) {
  static const std::regex re(R"(^(\d+)x(\d+)(?:([+-]\d+)([+-]\d+))?$)");
  std::smatch m;
  auto text = trim(geom);
  if (!std::regex_match(text, m, re)) return false;
  w = std::stoi(m[1]);
  h = std::stoi(m[2]);
  has_pos = m[3].matched;
  if (has_pos) {
    x = std::stoi(m[3]);
    y = std::stoi(m[4]);
  } else {
    x = y = 0;
  }
  return true;
}

bool geometry_on_screen(const std::string& geom, int min_w, int min_h) {
  int w, h, x, y;
  bool has_pos = false;
  if (!parse_geometry(geom, w, h, x, y, has_pos)) return false;
  if (w < min_w || h < min_h) return false;
  if (!has_pos) return true;
#ifdef _WIN32
  RECT rect{x, y, x + w, y + h};
  return MonitorFromRect(&rect, MONITOR_DEFAULTTONULL) != nullptr;
#else
  return true;
#endif
}

std::string window_geometry(wxWindow* window) {
  auto size = window->GetSize();
  auto pos = window->GetPosition();
  auto sign = [](int v) { return (v >= 0 ? "+" : "") + std::to_string(v); };
  return std::to_string(size.GetWidth()) + "x" + std::to_string(size.GetHeight()) + sign(pos.x) + sign(pos.y);
}

void restore_window_geometry(wxWindow* window, const std::string& geom, bool remember_size) {
  int w, h, x, y;
  bool has_pos = false;
  if (!parse_geometry(geom, w, h, x, y, has_pos)) return;
  const wxSize min_size = window->GetMinSize();
  const int min_w = std::max(200, min_size.GetWidth() > 0 ? min_size.GetWidth() : 200);
  const int min_h = std::max(150, min_size.GetHeight() > 0 ? min_size.GetHeight() : 150);
  if (remember_size && geometry_on_screen(geom, 80, 50)) {
    w = std::max(w, min_w);
    h = std::max(h, min_h);
    if (has_pos) {
      window->SetSize(x, y, w, h);
    } else {
      window->SetSize(w, h);
    }
    return;
  }
  if (has_pos && geometry_on_screen(std::to_string(std::max(window->GetSize().x, 80)) + "x" +
                                        std::to_string(std::max(window->GetSize().y, 50)) +
                                        (x >= 0 ? "+" : "") + std::to_string(x) + (y >= 0 ? "+" : "") +
                                        std::to_string(y),
                                    80, 50)) {
    window->SetPosition({x, y});
  }
}

void restore_dialog_geometry(wxWindow* window, AppSettings& settings, const std::string& key, bool remember_size) {
  auto it = settings.dialog_geometry.find(key);
  if (it == settings.dialog_geometry.end()) return;
  restore_window_geometry(window, it->second, remember_size);
}

void store_dialog_geometry(wxWindow* window, AppSettings& settings, const std::string& key, bool remember_size) {
  settings.dialog_geometry[key] = window_geometry(window);
  (void)remember_size;
}

void apply_list_columns(wxListCtrl* list, const std::map<std::string, int>& widths, const std::vector<std::string>& ids) {
  for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
    auto it = widths.find(ids[static_cast<std::size_t>(i)]);
    if (it != widths.end() && it->second > 0) {
      list->SetColumnWidth(i, it->second);
    }
  }
}

void store_list_columns(wxListCtrl* list, AppSettings& settings, const std::string& key,
                        const std::vector<std::string>& ids) {
  auto& cols = settings.column_widths[key];
  for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
    int w = list->GetColumnWidth(i);
    if (w > 0) cols[ids[static_cast<std::size_t>(i)]] = w;
  }
}

PositionedDialog::PositionedDialog(wxWindow* parent, const wxString& title, const wxSize& size)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  SetSize(FromDIP(size));
  if (parent) CentreOnParent();
  bind_escape_close(this);
}

void PositionedDialog::setup_layout(AppSettings* settings, const std::string& key, bool remember_size,
                                    std::function<void()> persist) {
  settings_ = settings;
  key_ = key;
  remember_size_ = remember_size;
  persist_ = std::move(persist);
  if (settings_) restore_dialog_geometry(this, *settings_, key_, remember_size_);
  Bind(wxEVT_CLOSE_WINDOW, &PositionedDialog::on_close_layout, this);
}

void PositionedDialog::on_close_layout(wxCloseEvent& event) {
  if (settings_ && !key_.empty()) {
    store_dialog_geometry(this, *settings_, key_, remember_size_);
    if (persist_) persist_();
  }
  event.Skip();
}

}  // namespace fatty

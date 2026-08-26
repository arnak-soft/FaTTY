#include "ui/theme.hpp"

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

namespace fatty {

wxColour Theme::bg() { return {0x1e, 0x1e, 0x1e}; }
wxColour Theme::chrome() { return {0x25, 0x25, 0x26}; }
wxColour Theme::elevated() { return {0x2d, 0x2d, 0x2d}; }
wxColour Theme::btn() { return {0x3c, 0x3c, 0x3c}; }
wxColour Theme::text() { return {0xd4, 0xd4, 0xd4}; }
wxColour Theme::text_bright() { return {0xf3, 0xf3, 0xf3}; }
wxColour Theme::muted() { return {0x9d, 0x9d, 0x9d}; }
wxColour Theme::accent() { return {0x0e, 0x63, 0x9c}; }
wxColour Theme::select() { return {0x26, 0x4f, 0x78}; }
wxColour Theme::meta() { return {0x9c, 0xdc, 0xfe}; }
wxColour Theme::ok() { return {0x6a, 0x99, 0x55}; }
wxColour Theme::err() { return {0xf1, 0x4c, 0x4c}; }
wxColour Theme::warn() { return {0xdc, 0xdc, 0xaa}; }
wxColour Theme::cancel() { return {0xc5, 0x86, 0xc0}; }
wxColour Theme::terminal() { return {0x1e, 0x1e, 0x1e}; }
wxFont Theme::ui() { return wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"); }
wxFont Theme::ui_small() { return wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"); }
wxFont Theme::ui_title() { return wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"); }
wxFont Theme::mono() { return wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Consolas"); }

void apply_dark_titlebar(wxWindow* window) {
#ifdef _WIN32
  HWND hwnd = static_cast<HWND>(window->GetHWND());
  if (!hwnd) return;
  BOOL value = TRUE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
#endif
}

void apply_dark(wxWindow* window) {
  if (!window) return;
  window->SetBackgroundColour(Theme::bg());
  window->SetForegroundColour(Theme::text());
  window->SetFont(Theme::ui());
  apply_dark_titlebar(window);
  for (auto* child : window->GetChildren()) {
    apply_dark(child);
  }
}

void style_text(wxTextCtrl* ctrl, bool terminal) {
  ctrl->SetBackgroundColour(terminal ? Theme::terminal() : Theme::elevated());
  ctrl->SetForegroundColour(Theme::text());
  ctrl->SetFont(terminal ? Theme::mono() : Theme::ui());
}

}  // namespace fatty

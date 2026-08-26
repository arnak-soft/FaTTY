#include "ui/theme.hpp"

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
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
namespace {

bool g_dark = true;

struct Palette {
  wxColour bg;
  wxColour chrome;
  wxColour elevated;
  wxColour btn;
  wxColour text;
  wxColour text_bright;
  wxColour muted;
  wxColour accent;
  wxColour select;
  wxColour meta;
  wxColour ok;
  wxColour err;
  wxColour warn;
  wxColour cancel;
  wxColour terminal;
};

const Palette kDark{
    {0x1e, 0x1e, 0x1e}, {0x25, 0x25, 0x26}, {0x2d, 0x2d, 0x2d}, {0x3c, 0x3c, 0x3c},
    {0xd4, 0xd4, 0xd4}, {0xf3, 0xf3, 0xf3}, {0x9d, 0x9d, 0x9d}, {0x0e, 0x63, 0x9c},
    {0x26, 0x4f, 0x78}, {0x9c, 0xdc, 0xfe}, {0x6a, 0x99, 0x55}, {0xf1, 0x4c, 0x4c},
    {0xdc, 0xdc, 0xaa}, {0xc5, 0x86, 0xc0}, {0x1e, 0x1e, 0x1e},
};

const Palette kLight{
    {0xf3, 0xf3, 0xf3}, {0xe8, 0xe8, 0xe8}, {0xff, 0xff, 0xff}, {0xe1, 0xe1, 0xe1},
    {0x33, 0x33, 0x33}, {0x1a, 0x1a, 0x1a}, {0x6e, 0x6e, 0x6e}, {0x00, 0x7a, 0xcc},
    {0xad, 0xd6, 0xff}, {0x04, 0x51, 0xa5}, {0x38, 0x8a, 0x34}, {0xcd, 0x31, 0x31},
    {0x9a, 0x76, 0x00}, {0xaf, 0x00, 0xaf}, {0xff, 0xff, 0xff},
};

const Palette& pal() { return g_dark ? kDark : kLight; }

}  // namespace

void set_theme(const std::string& name) { g_dark = name != "light"; }
bool theme_is_dark() { return g_dark; }
std::string theme_name() { return g_dark ? "dark" : "light"; }

wxColour Theme::bg() { return pal().bg; }
wxColour Theme::chrome() { return pal().chrome; }
wxColour Theme::elevated() { return pal().elevated; }
wxColour Theme::btn() { return pal().btn; }
wxColour Theme::text() { return pal().text; }
wxColour Theme::text_bright() { return pal().text_bright; }
wxColour Theme::muted() { return pal().muted; }
wxColour Theme::accent() { return pal().accent; }
wxColour Theme::select() { return pal().select; }
wxColour Theme::meta() { return pal().meta; }
wxColour Theme::ok() { return pal().ok; }
wxColour Theme::err() { return pal().err; }
wxColour Theme::warn() { return pal().warn; }
wxColour Theme::cancel() { return pal().cancel; }
wxColour Theme::terminal() { return pal().terminal; }
wxFont Theme::ui() { return wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"); }
wxFont Theme::ui_small() { return wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"); }
wxFont Theme::ui_title() { return wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"); }
wxFont Theme::mono() { return wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Consolas"); }

void apply_dark_titlebar(wxWindow* window) {
#ifdef _WIN32
  HWND hwnd = static_cast<HWND>(window->GetHWND());
  if (!hwnd) return;
  BOOL value = theme_is_dark() ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
#endif
}

void apply_theme(wxWindow* window) {
  if (!window) return;
  window->SetFont(Theme::ui());
  if (window->GetName() == "accent") {
    window->SetBackgroundColour(Theme::accent());
    window->SetForegroundColour(*wxWHITE);
  } else if (auto* tc = dynamic_cast<wxTextCtrl*>(window)) {
    style_text(tc, window->GetName() == "terminal");
  } else if (dynamic_cast<wxListCtrl*>(window) || dynamic_cast<wxNotebook*>(window)) {
    window->SetBackgroundColour(Theme::elevated());
    window->SetForegroundColour(Theme::text());
  } else {
    window->SetBackgroundColour(Theme::bg());
    window->SetForegroundColour(Theme::text());
  }
  apply_dark_titlebar(window);
  for (auto* child : window->GetChildren()) {
    apply_theme(child);
  }
  window->Refresh();
}

void apply_dark(wxWindow* window) { apply_theme(window); }

void style_text(wxTextCtrl* ctrl, bool terminal) {
  if (terminal) ctrl->SetName("terminal");
  ctrl->SetBackgroundColour(terminal ? Theme::terminal() : Theme::elevated());
  ctrl->SetForegroundColour(Theme::text());
  ctrl->SetFont(terminal ? Theme::mono() : Theme::ui());
}

}  // namespace fatty

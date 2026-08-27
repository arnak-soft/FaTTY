#include "ui/theme.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/fontenum.h>
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

// Тёмная палитра в духе GitHub Dark: холодный графит с синим акцентом.
const Palette kDark{
    {0x16, 0x1b, 0x22}, {0x1c, 0x22, 0x2b}, {0x1d, 0x23, 0x2c}, {0x2a, 0x31, 0x3c},
    {0xd9, 0xde, 0xe5}, {0xf2, 0xf5, 0xf8}, {0x8b, 0x94, 0x9e}, {0x2f, 0x81, 0xf7},
    {0x1f, 0x3a, 0x5f}, {0x79, 0xc0, 0xff}, {0x3f, 0xb9, 0x50}, {0xf8, 0x51, 0x49},
    {0xd2, 0x99, 0x22}, {0xbc, 0x8c, 0xff}, {0x0d, 0x11, 0x17},
};

const Palette kLight{
    {0xf6, 0xf8, 0xfa}, {0xea, 0xee, 0xf2}, {0xff, 0xff, 0xff}, {0xe6, 0xea, 0xef},
    {0x24, 0x29, 0x2f}, {0x14, 0x18, 0x1c}, {0x65, 0x6d, 0x76}, {0x09, 0x69, 0xda},
    {0xcf, 0xe4, 0xfc}, {0x05, 0x50, 0xae}, {0x1a, 0x7f, 0x37}, {0xcf, 0x22, 0x2e},
    {0x9a, 0x67, 0x00}, {0x82, 0x50, 0xdf}, {0xff, 0xff, 0xff},
};

const Palette& pal() { return g_dark ? kDark : kLight; }

const wxString& mono_face() {
  static const wxString face = [] {
    return wxFontEnumerator::IsValidFacename(L"Cascadia Mono") ? wxString(L"Cascadia Mono") : wxString(L"Consolas");
  }();
  return face;
}

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
wxFont Theme::ui() { return wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, L"Segoe UI"); }
wxFont Theme::ui_small() { return wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, L"Segoe UI"); }
wxFont Theme::ui_section() { return wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_SEMIBOLD, false, L"Segoe UI"); }
wxFont Theme::ui_title() { return wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, L"Segoe UI"); }
wxFont Theme::mono() { return wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, mono_face()); }

void apply_dark_titlebar(wxWindow* window) {
#ifdef _WIN32
  HWND hwnd = static_cast<HWND>(window->GetHWND());
  if (!hwnd) return;
  BOOL value = theme_is_dark() ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
#endif
}

// Роли задаются через SetName: "accent", "title", "section", "muted", "meta",
// "chrome" (панель-полоса), "terminal" (моно-вывод), "mono" (не трогать шрифт).
void apply_theme(wxWindow* window) {
  if (!window) return;
  const wxString name = window->GetName();
  if (name != L"title" && name != L"mono" && name != L"section") window->SetFont(Theme::ui());
  wxWindow* parent = window->GetParent();
  const wxColour parent_bg = parent ? parent->GetBackgroundColour() : Theme::bg();
  if (name == L"accent") {
    window->SetBackgroundColour(Theme::accent());
    window->SetForegroundColour(*wxWHITE);
  } else if (name == L"title") {
    window->SetFont(Theme::ui_title());
    window->SetBackgroundColour(parent_bg);
    window->SetForegroundColour(Theme::text_bright());
  } else if (name == L"section") {
    window->SetFont(Theme::ui_section());
    window->SetBackgroundColour(parent_bg);
    window->SetForegroundColour(Theme::muted());
  } else if (name == L"muted") {
    window->SetBackgroundColour(parent_bg);
    window->SetForegroundColour(Theme::muted());
  } else if (name == L"error") {
    window->SetBackgroundColour(parent_bg);
    window->SetForegroundColour(Theme::err());
  } else if (name == L"meta") {
    window->SetBackgroundColour(parent_bg);
    window->SetForegroundColour(Theme::meta());
  } else if (auto* tc = dynamic_cast<wxTextCtrl*>(window)) {
    style_text(tc, name == L"terminal");
  } else if (dynamic_cast<wxListCtrl*>(window) || dynamic_cast<wxNotebook*>(window)) {
    window->SetBackgroundColour(Theme::elevated());
    window->SetForegroundColour(Theme::text());
  } else if (dynamic_cast<wxButton*>(window)) {
    window->SetBackgroundColour(Theme::btn());
    window->SetForegroundColour(Theme::text_bright());
  } else if (name == L"chrome") {
    window->SetBackgroundColour(Theme::chrome());
    window->SetForegroundColour(Theme::text());
  } else if (dynamic_cast<wxStaticText*>(window) || dynamic_cast<wxCheckBox*>(window)) {
    window->SetBackgroundColour(parent_bg);
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
  if (terminal) ctrl->SetName(L"terminal");
  ctrl->SetBackgroundColour(terminal ? Theme::terminal() : Theme::elevated());
  ctrl->SetForegroundColour(Theme::text());
  ctrl->SetFont(terminal ? Theme::mono() : Theme::ui());
}

}  // namespace fatty

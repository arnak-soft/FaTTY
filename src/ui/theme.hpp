#pragma once

#include <wx/colour.h>
#include <wx/font.h>
#include <wx/window.h>
#include <string>

class wxTextCtrl;

namespace fatty {

class StripedListCtrl;

struct Theme {
  static wxColour bg();
  static wxColour chrome();
  static wxColour elevated();
  static wxColour btn();
  static wxColour text();
  static wxColour text_bright();
  static wxColour muted();
  static wxColour accent();
  static wxColour select();
  static wxColour meta();
  static wxColour ok();
  static wxColour err();
  static wxColour warn();
  static wxColour cancel();
  static wxColour terminal();
  static wxColour border();
  static wxColour hover();
  static wxColour blend(const wxColour& a, const wxColour& b, float t);
  static wxColour stripe(int row);
  static wxColour sorted_header();
  static wxColour sorted_column(const wxColour& row_bg);
  static wxColour run_status(const std::string& status);
  static wxFont ui();
  static wxFont ui_small();
  static wxFont ui_section();
  static wxFont ui_heading();
  static wxFont ui_title();
  static wxFont mono();
};

void set_theme(const std::string& name);
bool theme_is_dark();
std::string theme_name();
void set_mono_point_size(int pt);
int mono_point_size();
/// Тонирование поверхностей (фон окна) цветом выбранного VPS. `wxNullColour` — сброс.
void set_surface_accent(const wxColour& accent);
wxColour surface_accent();
void apply_theme(wxWindow* window);
void apply_dark(wxWindow* window);
void apply_dark_titlebar(wxWindow* window);
// Полоса прокрутки и wxGauge. На Windows 10 тёмная тема приложения их не красит.
void polish_native_chrome(wxWindow* window);
void style_list(StripedListCtrl* list);
void style_list_row(StripedListCtrl* list, long row, const wxColour& text);
void style_text(wxTextCtrl* ctrl, bool terminal = false);

/// `#RGB` / `#RRGGBB` → цвет; пустая или битая строка — false.
bool parse_hex_colour(std::string_view hex, wxColour& out);
std::string colour_to_hex(const wxColour& colour);
/// Цвет VPS для текущей темы (тёмная / светлая).
bool server_ui_colour(const std::string& colour_dark, const std::string& colour_light, wxColour& out);
/// Шрифт списков для VPS: face/pt из карточки или шрифт темы.
wxFont server_list_font(const std::string& face, int pt);

}  // namespace fatty

#pragma once

#include <wx/colour.h>
#include <wx/font.h>
#include <wx/window.h>

namespace fatty {

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
  static wxFont ui();
  static wxFont ui_small();
  static wxFont ui_title();
  static wxFont mono();
};

void apply_dark(wxWindow* window);
void apply_dark_titlebar(wxWindow* window);
void style_text(wxTextCtrl* ctrl, bool terminal = false);

}  // namespace fatty

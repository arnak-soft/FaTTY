#include "ui/splash.hpp"

#include "app/version.hpp"
#include "core/paths.hpp"
#include "core/util.hpp"
#include "ui/theme.hpp"

#include <wx/frame.h>
#include <wx/gauge.h>
#include <wx/image.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/sizer.h>

namespace fatty {
namespace {
wxFrame* g_splash = nullptr;
}

void show_splash() {
  hide_splash();
  auto* f = new wxFrame(nullptr, wxID_ANY, "FaTTY — загрузка", wxDefaultPosition, wxSize(420, 268),
                        wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_NONE);
  f->SetBackgroundColour(Theme::bg());
  auto* body = new wxPanel(f);
  body->SetBackgroundColour(Theme::bg());
  auto* title = new wxStaticText(body, wxID_ANY, kAppName);
  title->SetForegroundColour(Theme::text_bright());
  title->SetFont(Theme::ui_title());
  auto* ver = new wxStaticText(body, wxID_ANY, wxString::FromUTF8(resolve_version()));
  ver->SetForegroundColour(Theme::meta());
  auto* st = new wxStaticText(body, wxID_ANY, "Загрузка…");
  st->SetForegroundColour(Theme::text());
  auto* bar = new wxGauge(body, wxID_ANY, 100, wxDefaultPosition, wxSize(260, 16), wxGA_HORIZONTAL | wxGA_SMOOTH);
  bar->Pulse();
  auto* s = new wxBoxSizer(wxVERTICAL);
  s->AddStretchSpacer();
  s->Add(title, 0, wxALIGN_CENTER);
  s->Add(ver, 0, wxALIGN_CENTER | wxTOP, 4);
  s->Add(st, 0, wxALIGN_CENTER | wxTOP, 10);
  s->Add(bar, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, 16);
  s->AddStretchSpacer();
  body->SetSizer(s);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(body, 1, wxEXPAND);
  f->SetSizer(outer);
  f->Centre();
  f->Show();
  f->Update();
  g_splash = f;
}

void hide_splash() {
  if (g_splash) {
    g_splash->Destroy();
    g_splash = nullptr;
  }
}

}  // namespace fatty

#include "ui/splash.hpp"

#include "app/version.hpp"
#include "core/paths.hpp"
#include "core/util.hpp"
#include "ui/theme.hpp"
#include "ui/chrome.hpp"

#include <wx/frame.h>
#include <wx/gauge.h>
#include <wx/image.h>
#include <wx/panel.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/sizer.h>

namespace fatty {
namespace {
wxFrame* g_splash = nullptr;
}

void show_splash() {
  hide_splash();
  auto* f = new wxFrame(nullptr, wxID_ANY, L"FaTTY — загрузка", wxDefaultPosition, wxDefaultSize,
                        wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_NONE);
  f->SetSize(f->FromDIP(wxSize(420, 268)));
  f->SetBackgroundColour(Theme::bg());
  auto* body = new wxPanel(f);
  body->SetBackgroundColour(Theme::bg());
  auto* title = new wxStaticText(body, wxID_ANY, wxString::FromUTF8(kAppName));
  title->SetForegroundColour(Theme::text_bright());
  title->SetFont(Theme::ui_title());
  auto* ver = new wxStaticText(body, wxID_ANY, wxString::FromUTF8(resolve_version()));
  ver->SetForegroundColour(Theme::meta());
  auto* st = new wxStaticText(body, wxID_ANY, L"Загрузка…");
  st->SetForegroundColour(Theme::text());
  auto* bar = new wxGauge(body, wxID_ANY, 100, wxDefaultPosition, f->FromDIP(wxSize(260, 16)),
                          wxGA_HORIZONTAL | wxGA_SMOOTH);
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
  const int splash_r = f->FromDIP(8);
  auto round_splash = [f, splash_r] {
    apply_rounded_region(f, splash_r);
  };
  f->Bind(wxEVT_SIZE, [round_splash](wxSizeEvent& e) {
    round_splash();
    e.Skip();
  });
  f->Show();
  f->Update();
  round_splash();
  g_splash = f;
}

void hide_splash() {
  if (g_splash) {
    g_splash->Destroy();
    g_splash = nullptr;
  }
}

}  // namespace fatty

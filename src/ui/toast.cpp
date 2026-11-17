#include "ui/toast.hpp"

#include "ui/chrome.hpp"
#include "ui/theme.hpp"

#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>

#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fatty {
namespace {

class CompletionToastFrame : public wxFrame {
 public:
  CompletionToastFrame(wxWindow* main_frame, const wxString& title, const wxString& detail, const wxColour& accent)
      : wxFrame(nullptr, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize,
                wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_NONE | wxFRAME_TOOL_WINDOW),
        main_frame_(main_frame) {
    SetBackgroundColour(Theme::elevated());
    SetName(L"completion-toast");

    auto* root = new wxPanel(this);
    root->SetBackgroundColour(Theme::elevated());
    root->SetBackgroundStyle(wxBG_STYLE_PAINT);
    root->Bind(wxEVT_PAINT, [root, accent](wxPaintEvent&) {
      wxPaintDC dc(root);
      const wxSize sz = root->GetClientSize();
      dc.SetBrush(wxBrush(Theme::elevated()));
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawRectangle(0, 0, sz.x, sz.y);
      const int bar = root->FromDIP(4);
      dc.SetBrush(wxBrush(accent));
      dc.DrawRectangle(0, 0, bar, sz.y);
    });

    auto* title_lbl = new wxStaticText(root, wxID_ANY, title);
    title_lbl->SetForegroundColour(Theme::text_bright());
    title_lbl->SetFont(Theme::ui_section());
    auto* detail_lbl = new wxStaticText(root, wxID_ANY, detail);
    detail_lbl->SetForegroundColour(Theme::muted());
    detail_lbl->SetFont(Theme::ui_small());

    auto* col = new wxBoxSizer(wxVERTICAL);
    col->Add(title_lbl, 0, wxEXPAND);
    col->Add(detail_lbl, 0, wxEXPAND | wxTOP, FromDIP(2));

    auto* pad = new wxBoxSizer(wxHORIZONTAL);
    pad->AddSpacer(FromDIP(14));
    pad->Add(col, 1, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(10));
    pad->AddSpacer(FromDIP(14));
    root->SetSizer(pad);

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(root, 1, wxEXPAND);
    SetSizer(outer);

    const int max_w = FromDIP(360);
    title_lbl->Wrap(max_w - FromDIP(28));
    detail_lbl->Wrap(max_w - FromDIP(28));
    Layout();
    Fit();
    const wxSize want = GetBestSize();
    SetSize(wxSize(std::min(max_w, std::max(FromDIP(240), want.x)), want.y));

    place_near(main_frame);
    apply_rounded_region(this, FromDIP(8));
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
      apply_rounded_region(this, FromDIP(8));
      e.Skip();
    });

    auto raise_main = [this](wxMouseEvent&) {
      if (auto* tlw = dynamic_cast<wxTopLevelWindow*>(main_frame_)) {
        if (tlw->IsIconized()) tlw->Iconize(false);
        tlw->Raise();
        tlw->SetFocus();
      }
      Destroy();
    };
    Bind(wxEVT_LEFT_UP, raise_main);
    root->Bind(wxEVT_LEFT_UP, raise_main);
    title_lbl->Bind(wxEVT_LEFT_UP, raise_main);
    detail_lbl->Bind(wxEVT_LEFT_UP, raise_main);

    hide_timer_.SetOwner(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Destroy(); });
    hide_timer_.StartOnce(3800);

#ifdef _WIN32
    if (HWND hwnd = static_cast<HWND>(GetHWND())) {
      LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
      SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    }
#endif
  }

 private:
  void place_near(wxWindow* main_frame) {
    wxRect area;
    int disp = wxDisplay::GetFromWindow(main_frame ? main_frame : this);
    if (disp == wxNOT_FOUND) disp = 0;
    area = wxDisplay(disp).GetClientArea();
    const wxSize sz = GetSize();
    const int margin = FromDIP(16);
    SetPosition(wxPoint(area.GetRight() - sz.x - margin, area.GetBottom() - sz.y - margin));
  }

  wxWindow* main_frame_ = nullptr;
  wxTimer hide_timer_;
};

CompletionToastFrame* g_toast = nullptr;

}  // namespace

bool app_is_foreground() {
#ifdef _WIN32
  HWND fg = GetForegroundWindow();
  if (!fg) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(fg, &pid);
  return pid == GetCurrentProcessId();
#else
  wxWindow* focus = wxWindow::FindFocus();
  return focus != nullptr;
#endif
}

void show_completion_toast(wxWindow* main_frame, const std::string& title, const std::string& detail,
                           const std::string& status) {
  if (!main_frame) return;
  if (g_toast) {
    g_toast->Destroy();
    g_toast = nullptr;
  }
  const wxColour accent = Theme::run_status(status);
  auto* toast = new CompletionToastFrame(main_frame, wxString::FromUTF8(title), wxString::FromUTF8(detail), accent);
  g_toast = toast;
  toast->Bind(wxEVT_DESTROY, [](wxWindowDestroyEvent& e) {
    if (e.GetWindow() == g_toast) g_toast = nullptr;
    e.Skip();
  });
  toast->ShowWithoutActivating();
  if (auto* tlw = dynamic_cast<wxTopLevelWindow*>(main_frame)) {
    tlw->RequestUserAttention(wxUSER_ATTENTION_INFO);
  }
}

}  // namespace fatty

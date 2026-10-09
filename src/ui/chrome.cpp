#include "ui/chrome.hpp"

#include "ui/theme.hpp"

#include <wx/bookctrl.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/display.h>
#include <wx/event.h>
#include <wx/eventfilter.h>
#include <wx/evtloop.h>
#include <wx/graphics.h>
#include <wx/menu.h>
#include <wx/notebook.h>
#include <wx/popupwin.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/toplevel.h>
#include <wx/utils.h>
#include <wx/window.h>
#include <algorithm>
#include <cmath>
#include <cstddef>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef DrawText
#undef DrawText
#endif
#endif

namespace fatty {
namespace {

constexpr int kButtonRadiusDip = 4;
constexpr int kTabRadiusDip = 8;
constexpr int kTabHeightDip = 32;
constexpr int kTabTopGapDip = 6;

wxGraphicsPath chrome_tab_path(wxGraphicsContext* gfx, const wxRect& body, double radius) {
  const double x = body.x;
  const double y = body.y;
  const double w = body.width;
  const double h = body.height;
  const double r = std::min(radius, std::min(w, h) / 2.0);
  auto path = gfx->CreatePath();
  // Силуэт Chrome: выпуклый верх и вогнутые нижние «ушки».
  path.MoveToPoint(x - r, y + h);
  path.AddArcToPoint(x, y + h, x, y, r);
  path.AddArcToPoint(x, y, x + w, y, r);
  path.AddArcToPoint(x + w, y, x + w, y + h, r);
  path.AddArcToPoint(x + w, y + h, x + w + r, y + h, r);
  path.CloseSubpath();
  return path;
}

void fill_round(wxAutoBufferedPaintDC& dc, const wxRect& r, int radius, const wxColour& fill,
                const wxColour& border, const wxColour& bg) {
  wxGCDC gc(dc);
  gc.SetBackground(wxBrush(bg));
  gc.Clear();
  if (wxGraphicsContext* gfx = gc.GetGraphicsContext()) {
    gfx->SetAntialiasMode(wxANTIALIAS_DEFAULT);
  }
  gc.SetPen(wxPen(border, 1));
  gc.SetBrush(wxBrush(fill));
  gc.DrawRoundedRectangle(r.x + 1.0, r.y + 1.0, r.width - 2.0, r.height - 2.0, radius);
}

wxColour shift(const wxColour& c, int d) {
  auto ch = [d](unsigned char v) {
    return static_cast<unsigned char>(std::clamp(static_cast<int>(v) + d, 0, 255));
  };
  return {ch(c.Red()), ch(c.Green()), ch(c.Blue())};
}

void draw_btn_icon(wxGraphicsContext* gfx, BtnIcon icon, double x, double y, double size, const wxColour& color) {
  if (!gfx || icon == BtnIcon::None || size < 4.0) return;
  const double u = size / 16.0;
  auto X = [x, u](double v) { return x + v * u; };
  auto Y = [y, u](double v) { return y + v * u; };
  wxGraphicsPenInfo info(color, std::max(1.15, size * 0.12));
  info.Cap(wxCAP_ROUND).Join(wxJOIN_ROUND);
  gfx->SetPen(gfx->CreatePen(info));
  gfx->SetBrush(gfx->CreateBrush(*wxTRANSPARENT_BRUSH));
  auto line = [&](double x1, double y1, double x2, double y2) {
    gfx->StrokeLine(X(x1), Y(y1), X(x2), Y(y2));
  };
  auto stroke_path = [&](const wxGraphicsPath& p) { gfx->StrokePath(p); };
  auto fill_path = [&](const wxGraphicsPath& p) {
    gfx->SetPen(wxNullGraphicsPen);
    gfx->SetBrush(gfx->CreateBrush(wxBrush(color)));
    gfx->FillPath(p);
    gfx->SetPen(gfx->CreatePen(info));
    gfx->SetBrush(gfx->CreateBrush(*wxTRANSPARENT_BRUSH));
  };
  auto rrect = [&](double x1, double y1, double w, double h, double r) {
    auto p = gfx->CreatePath();
    p.AddRoundedRectangle(X(x1), Y(y1), w * u, h * u, r * u);
    stroke_path(p);
  };

  switch (icon) {
    case BtnIcon::Plus:
      line(8, 3, 8, 13);
      line(3, 8, 13, 8);
      break;
    case BtnIcon::Pencil: {
      auto p = gfx->CreatePath();
      p.MoveToPoint(X(3.2), Y(12.8));
      p.AddLineToPoint(X(11.2), Y(4.8));
      p.AddLineToPoint(X(13.2), Y(6.8));
      p.AddLineToPoint(X(5.2), Y(14.8));
      p.CloseSubpath();
      stroke_path(p);
      line(10.2, 3.8, 12.2, 5.8);
      break;
    }
    case BtnIcon::Copy:
      rrect(5.2, 2.4, 8.4, 8.4, 1.4);
      rrect(2.4, 5.4, 8.4, 8.4, 1.4);
      break;
    case BtnIcon::Trash:
      line(3, 5, 13, 5);
      line(6.2, 3.2, 9.8, 3.2);
      line(6.2, 3.2, 6.2, 5);
      line(9.8, 3.2, 9.8, 5);
      {
        auto p = gfx->CreatePath();
        p.MoveToPoint(X(4.6), Y(5));
        p.AddLineToPoint(X(5.4), Y(13.6));
        p.AddLineToPoint(X(10.6), Y(13.6));
        p.AddLineToPoint(X(11.4), Y(5));
        stroke_path(p);
      }
      line(7.2, 7.2, 7.4, 11.6);
      line(8.8, 7.2, 8.6, 11.6);
      break;
    case BtnIcon::Folder:
    case BtnIcon::FolderPlus:
    case BtnIcon::FolderMove: {
      auto p = gfx->CreatePath();
      p.MoveToPoint(X(2.2), Y(13.4));
      p.AddLineToPoint(X(2.2), Y(4.8));
      p.AddLineToPoint(X(6.2), Y(4.8));
      p.AddLineToPoint(X(7.6), Y(3.4));
      p.AddLineToPoint(X(13.8), Y(3.4));
      p.AddLineToPoint(X(13.8), Y(13.4));
      p.CloseSubpath();
      stroke_path(p);
      if (icon == BtnIcon::FolderPlus) {
        line(8, 7.2, 8, 11.6);
        line(5.8, 9.4, 10.2, 9.4);
      } else if (icon == BtnIcon::FolderMove) {
        line(5.2, 9.2, 11.2, 9.2);
        line(8.6, 6.8, 11.4, 9.2);
        line(8.6, 11.6, 11.4, 9.2);
      }
      break;
    }
    case BtnIcon::Terminal:
    case BtnIcon::Putty:
    case BtnIcon::App:
      rrect(2.2, 3.2, 11.6, 9.8, 1.6);
      if (icon == BtnIcon::Putty) {
        line(2.2, 6.2, 13.8, 6.2);
        line(4.2, 4.6, 5.6, 4.6);
      } else if (icon == BtnIcon::App) {
        line(2.2, 6.0, 13.8, 6.0);
        line(4.2, 4.6, 5.2, 4.6);
        line(6.0, 4.6, 7.0, 4.6);
      } else {
        line(4.4, 6.2, 6.4, 8.0);
        line(6.4, 8.0, 4.4, 9.8);
        line(7.8, 10.2, 11.2, 10.2);
      }
      break;
    case BtnIcon::WinSCP:
      rrect(2.2, 3.4, 5.4, 9.4, 1.2);
      rrect(8.4, 3.4, 5.4, 9.4, 1.2);
      line(7.6, 8.0, 8.4, 8.0);
      break;
    case BtnIcon::Network:
      rrect(2.4, 5.6, 4.6, 4.6, 1.4);
      rrect(9.0, 5.6, 4.6, 4.6, 1.4);
      line(7.0, 8.0, 9.0, 8.0);
      break;
    case BtnIcon::Pulse:
      line(2.0, 8.0, 5.0, 8.0);
      line(5.0, 8.0, 6.6, 3.2);
      line(6.6, 3.2, 8.4, 13.0);
      line(8.4, 13.0, 10.2, 8.0);
      line(10.2, 8.0, 14.0, 8.0);
      break;
    case BtnIcon::ArrowUp:
      line(8, 12.8, 8, 3.6);
      line(4.4, 7.4, 8, 3.4);
      line(11.6, 7.4, 8, 3.4);
      break;
    case BtnIcon::ArrowDown:
      line(8, 3.2, 8, 12.4);
      line(4.4, 8.6, 8, 12.6);
      line(11.6, 8.6, 8, 12.6);
      break;
    case BtnIcon::Sort:
      line(8, 2.8, 4.2, 7.0);
      line(8, 2.8, 11.8, 7.0);
      line(8, 13.2, 4.2, 9.0);
      line(8, 13.2, 11.8, 9.0);
      break;
    case BtnIcon::List:
      line(5.8, 4.6, 13.0, 4.6);
      line(5.8, 8.0, 13.0, 8.0);
      line(5.8, 11.4, 13.0, 11.4);
      {
        auto d = gfx->CreatePath();
        d.AddCircle(X(3.2), Y(4.6), 0.9 * u);
        d.AddCircle(X(3.2), Y(8.0), 0.9 * u);
        d.AddCircle(X(3.2), Y(11.4), 0.9 * u);
        fill_path(d);
      }
      break;
    case BtnIcon::Play: {
      auto p = gfx->CreatePath();
      p.MoveToPoint(X(5.0), Y(3.4));
      p.AddLineToPoint(X(13.0), Y(8.0));
      p.AddLineToPoint(X(5.0), Y(12.6));
      p.CloseSubpath();
      fill_path(p);
      break;
    }
    case BtnIcon::Stop: {
      auto p = gfx->CreatePath();
      p.AddRoundedRectangle(X(4.2), Y(4.2), 7.6 * u, 7.6 * u, 1.4 * u);
      fill_path(p);
      break;
    }
    case BtnIcon::Home: {
      auto p = gfx->CreatePath();
      p.MoveToPoint(X(2.4), Y(8.2));
      p.AddLineToPoint(X(8.0), Y(3.0));
      p.AddLineToPoint(X(13.6), Y(8.2));
      stroke_path(p);
      rrect(4.6, 8.0, 6.8, 5.4, 0.6);
      line(7.2, 13.4, 7.2, 10.2);
      line(8.8, 13.4, 8.8, 10.2);
      line(7.2, 10.2, 8.8, 10.2);
      break;
    }
    case BtnIcon::Clear: {
      auto p = gfx->CreatePath();
      p.AddCircle(X(8), Y(8), 5.6 * u);
      stroke_path(p);
      line(5.6, 5.6, 10.4, 10.4);
      line(10.4, 5.6, 5.6, 10.4);
      break;
    }
    case BtnIcon::Upload:
      line(3.2, 9.4, 3.2, 13.2);
      line(3.2, 13.2, 12.8, 13.2);
      line(12.8, 13.2, 12.8, 9.4);
      line(8, 11.0, 8, 3.2);
      line(4.8, 6.4, 8, 3.2);
      line(11.2, 6.4, 8, 3.2);
      break;
    case BtnIcon::Download:
      line(3.2, 9.4, 3.2, 13.2);
      line(3.2, 13.2, 12.8, 13.2);
      line(12.8, 13.2, 12.8, 9.4);
      line(8, 3.2, 8, 10.0);
      line(4.8, 6.8, 8, 10.2);
      line(11.2, 6.8, 8, 10.2);
      break;
    case BtnIcon::Save:
      rrect(3.0, 2.8, 10.0, 10.6, 1.2);
      rrect(5.2, 2.8, 5.6, 4.0, 0.4);
      line(5.4, 11.4, 10.6, 11.4);
      break;
    case BtnIcon::Cancel:
      line(4.2, 4.2, 11.8, 11.8);
      line(11.8, 4.2, 4.2, 11.8);
      break;
    case BtnIcon::Refresh:
    case BtnIcon::Repeat: {
      auto p = gfx->CreatePath();
      p.AddArc(X(8), Y(8), 5.2 * u, 0.55, 5.4, true);
      stroke_path(p);
      line(11.6, 3.6, 13.4, 6.4);
      line(11.6, 3.6, 8.8, 4.6);
      break;
    }
    case BtnIcon::Export:
      rrect(2.6, 5.4, 8.0, 8.0, 1.2);
      line(9.0, 7.0, 13.4, 2.8);
      line(10.6, 2.8, 13.4, 2.8);
      line(13.4, 2.8, 13.4, 5.6);
      break;
    case BtnIcon::Import:
      rrect(2.6, 5.4, 8.0, 8.0, 1.2);
      line(13.4, 2.8, 9.0, 7.0);
      line(9.0, 4.2, 9.0, 7.0);
      line(9.0, 7.0, 11.8, 7.0);
      break;
    case BtnIcon::Key: {
      auto p = gfx->CreatePath();
      p.AddCircle(X(5.4), Y(8.0), 3.0 * u);
      stroke_path(p);
      line(8.2, 8.0, 14.0, 8.0);
      line(12.2, 8.0, 12.2, 10.4);
      line(13.8, 8.0, 13.8, 9.6);
      break;
    }
    case BtnIcon::Check:
      line(3.4, 8.4, 6.6, 11.6);
      line(6.6, 11.6, 12.8, 4.6);
      break;
    case BtnIcon::Insert:
      line(3.0, 8.0, 10.2, 8.0);
      line(7.2, 5.2, 10.4, 8.0);
      line(7.2, 10.8, 10.4, 8.0);
      line(12.4, 4.2, 12.4, 11.8);
      break;
    case BtnIcon::None:
      break;
  }
}

}  // namespace

void apply_rounded_region(wxWindow* window, int radius_px) {
#ifdef _WIN32
  if (!window) return;
  HWND hwnd = static_cast<HWND>(window->GetHWND());
  if (!hwnd) return;
  const wxSize sz = window->GetSize();
  if (sz.x <= 1 || sz.y <= 1) return;
  const int r = std::max(2, radius_px);
  HRGN rgn = CreateRoundRectRgn(0, 0, sz.x + 1, sz.y + 1, r * 2, r * 2);
  SetWindowRgn(hwnd, rgn, TRUE);
#else
  (void)window;
  (void)radius_px;
#endif
}

RoundedCard::RoundedCard(wxWindow* parent, int radius_dip)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL),
      radius_dip_(radius_dip) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  Bind(wxEVT_PAINT, &RoundedCard::on_paint, this);
  Bind(wxEVT_SIZE, &RoundedCard::on_size, this);
}

void RoundedCard::on_paint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  const wxColour bg = GetParent() ? GetParent()->GetBackgroundColour() : Theme::bg();
  const int r = FromDIP(radius_dip_);
  fill_round(dc, GetClientRect(), r, Theme::elevated(), Theme::border(), bg);
}

void RoundedCard::on_size(wxSizeEvent& e) {
  apply_rounded_region(this, FromDIP(radius_dip_));
  Refresh();
  e.Skip();
}

RoundButton::RoundButton(wxWindow* parent, wxWindowID id, const wxString& label, BtnIcon icon)
    : wxControl(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL), icon_(icon) {
  SetLabel(label);
  SetFont(Theme::ui());
  SetCanFocus(true);
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetCursor(wxCURSOR_HAND);
  Bind(wxEVT_PAINT, &RoundButton::on_paint, this);
  Bind(wxEVT_SIZE, &RoundButton::on_size, this);
  Bind(wxEVT_ENTER_WINDOW, &RoundButton::on_mouse, this);
  Bind(wxEVT_LEAVE_WINDOW, &RoundButton::on_mouse, this);
  Bind(wxEVT_LEFT_DOWN, &RoundButton::on_mouse, this);
  Bind(wxEVT_LEFT_DCLICK, &RoundButton::on_mouse, this);
  Bind(wxEVT_LEFT_UP, &RoundButton::on_mouse, this);
  Bind(wxEVT_MOTION, &RoundButton::on_mouse, this);
  Bind(wxEVT_MOUSE_CAPTURE_LOST, &RoundButton::on_capture_lost, this);
  Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
    if (IsEnabled() && (e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_SPACE)) {
      fire_async();
      return;
    }
    e.Skip();
  });
  Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) {
    Refresh();
    e.Skip();
  });
  Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
    Refresh();
    e.Skip();
  });
}

void RoundButton::on_size(wxSizeEvent& e) {
  Refresh();
  e.Skip();
}

void RoundButton::DoEnable(bool enable) {
  wxControl::DoEnable(enable);
  if (!enable) {
    if (HasCapture()) ReleaseMouse();
    hovered_ = false;
    pressed_ = false;
    Refresh();
    return;
  }
  sync_hover();
}

void RoundButton::SetLabel(const wxString& label) {
  wxControl::SetLabel(label);
  InvalidateBestSize();
  Refresh();
}

void RoundButton::SetIcon(BtnIcon icon) {
  icon_ = icon;
  InvalidateBestSize();
  Refresh();
}

void RoundButton::SetDefault() {
  default_ = true;
  auto* top = wxGetTopLevelParent(this);
  if (!top) return;
  top->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
    if (!IsEnabled() || !IsShown()) {
      e.Skip();
      return;
    }
    if (e.GetKeyCode() != WXK_RETURN && e.GetKeyCode() != WXK_NUMPAD_ENTER) {
      e.Skip();
      return;
    }
    auto* focus = wxWindow::FindFocus();
    if (auto* tc = dynamic_cast<wxTextCtrl*>(focus)) {
      const long style = tc->GetWindowStyle();
      if ((style & wxTE_MULTILINE) && !(style & wxTE_PROCESS_ENTER)) {
        e.Skip();
        return;
      }
    }
    fire_async();
  });
}

wxSize RoundButton::DoGetBestSize() const {
  const wxSize text = GetTextExtent(GetLabel());
  const int hpad = FromDIP(14);
  const int vpad = FromDIP(8);
  const int icon = (icon_ != BtnIcon::None) ? FromDIP(14) : 0;
  const int igap = icon ? FromDIP(6) : 0;
  return {hpad + icon + igap + text.x + hpad, std::max(FromDIP(32), text.y + vpad * 2)};
}

void RoundButton::fire() {
  wxCommandEvent ev(wxEVT_BUTTON, GetId());
  ev.SetEventObject(this);
  ProcessWindowEvent(ev);
}

void RoundButton::fire_async() {
  CallAfter([this] {
    if (IsBeingDeleted() || !IsEnabled()) return;
    pressed_ = false;
    fire();
    if (IsBeingDeleted()) return;
    // Диалог/PuTTY отключают окно: кнопка успевает перерисоваться серой
    // (IsEnabled() == false). После возврата всегда сбрасываем вид.
    sync_hover();
  });
}

void RoundButton::sync_hover() {
  const wxPoint pt = ScreenToClient(wxGetMousePosition());
  const bool inside = IsEnabled() && IsShown() && GetClientRect().Contains(pt);
  hovered_ = inside;
  if (!HasCapture()) pressed_ = false;
  Refresh();
}

void RoundButton::on_capture_lost(wxMouseCaptureLostEvent&) {
  pressed_ = false;
  sync_hover();
}

void RoundButton::on_mouse(wxMouseEvent& e) {
  const bool inside = GetClientRect().Contains(e.GetPosition());
  const wxEventType t = e.GetEventType();
  if (t == wxEVT_LEFT_DOWN || t == wxEVT_LEFT_DCLICK) {
    if (!IsEnabled()) return;
    pressed_ = true;
    hovered_ = true;
    if (!HasCapture()) CaptureMouse();
    Refresh();
    return;
  }
  if (t == wxEVT_LEFT_UP) {
    const bool click = pressed_ && inside && IsEnabled();
    pressed_ = false;
    if (HasCapture()) ReleaseMouse();
    hovered_ = inside;
    Refresh();
    Update();
    if (click) fire_async();
    return;
  }
  if (t == wxEVT_LEAVE_WINDOW) {
    hovered_ = false;
    if (!HasCapture()) pressed_ = false;
    Refresh();
    return;
  }
  if (hovered_ != inside) {
    hovered_ = inside;
    Refresh();
  }
}

void RoundButton::on_paint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  const wxColour parent_bg = GetParent() ? GetParent()->GetBackgroundColour() : Theme::bg();
  SetBackgroundColour(parent_bg);
  const bool accent = GetName() == L"accent";
  wxColour fill = accent ? Theme::accent() : Theme::btn();
  wxColour fg = accent ? *wxWHITE : Theme::text_bright();
  if (!IsEnabled()) {
    fill = Theme::chrome();
    fg = Theme::muted();
  } else if (pressed_ && hovered_) {
    fill = shift(fill, theme_is_dark() ? -18 : -22);
  } else if (hovered_) {
    fill = accent ? shift(fill, 16) : Theme::hover();
  }
  wxGCDC gc(dc);
  gc.SetBackground(wxBrush(parent_bg));
  gc.Clear();
  if (wxGraphicsContext* gfx = gc.GetGraphicsContext()) {
    gfx->SetAntialiasMode(wxANTIALIAS_DEFAULT);
  }
  const wxSize sz = GetClientSize();
  const double radius = FromDIP(kButtonRadiusDip);
  // Без обводки и без SetWindowRgn: иначе GDI+ сглаживает к чёрному, а регион
  // обрезает пиксели ступенькой — на синем это особенно заметно.
  gc.SetPen(*wxTRANSPARENT_PEN);
  gc.SetBrush(wxBrush(fill));
  gc.DrawRoundedRectangle(0.5, 0.5, sz.x - 1.0, sz.y - 1.0, radius);
  if (IsEnabled() && HasFocus()) {
    const wxColour ring = accent ? *wxWHITE : Theme::accent();
    gc.SetBrush(*wxTRANSPARENT_BRUSH);
    gc.SetPen(wxPen(ring, std::max(1, FromDIP(2))));
    const double inset = FromDIP(2);
    gc.DrawRoundedRectangle(inset, inset, std::max(1.0, sz.x - inset * 2), std::max(1.0, sz.y - inset * 2),
                            std::max(1.0, radius - 1.0));
  }
  gc.SetFont(GetFont().IsOk() ? GetFont() : Theme::ui());
  gc.SetTextForeground(fg);
  const wxString label = GetLabel();
  const wxSize text = gc.GetTextExtent(label);
  const int icon_sz = (icon_ != BtnIcon::None) ? FromDIP(14) : 0;
  const int igap = icon_sz ? FromDIP(6) : 0;
  const int total = icon_sz + igap + text.x;
  int tx = (sz.x - total) / 2;
  if (icon_sz) {
    if (wxGraphicsContext* igfx = gc.GetGraphicsContext()) {
      draw_btn_icon(igfx, icon_, tx, (sz.y - icon_sz) / 2.0, icon_sz, fg);
    }
    tx += icon_sz + igap;
  }
  gc.DrawText(label, tx, (sz.y - text.y) / 2);
}

RoundButton* make_button(wxWindow* parent, const wxString& label, wxWindowID id) {
  return new RoundButton(parent, id, label);
}

RoundButton* make_button(wxWindow* parent, const wxString& label, BtnIcon icon, wxWindowID id) {
  return new RoundButton(parent, id, label, icon);
}

RoundButton* accent_button(wxWindow* parent, const wxString& label, wxWindowID id) {
  return accent_button(parent, label, BtnIcon::None, id);
}

RoundButton* accent_button(wxWindow* parent, const wxString& label, BtnIcon icon, wxWindowID id) {
  auto* btn = make_button(parent, label, icon, id);
  btn->SetName(L"accent");
  return btn;
}

TabStrip::TabStrip(RoundedNotebook* owner)
    : wxPanel(owner, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), owner_(owner) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetBackgroundColour(Theme::bg());
  SetMinSize(wxSize(-1, FromDIP(kTabTopGapDip + kTabHeightDip)));
  timer_.SetOwner(this);
  Bind(wxEVT_PAINT, &TabStrip::on_paint, this);
  Bind(wxEVT_SIZE, &TabStrip::on_size, this);
  Bind(wxEVT_LEFT_DOWN, &TabStrip::on_mouse, this);
  Bind(wxEVT_RIGHT_UP, &TabStrip::on_mouse, this);
  Bind(wxEVT_MOTION, &TabStrip::on_mouse, this);
  Bind(wxEVT_LEAVE_WINDOW, &TabStrip::on_leave, this);
  Bind(wxEVT_TIMER, &TabStrip::on_timer, this);
}

void TabStrip::notify_pages_changed() {
  ensure_hover_size();
  InvalidateBestSize();
  rebuild_layout(GetClientSize().GetWidth());
  if (auto* parent = GetParent()) parent->Layout();
  Refresh();
}

wxSize TabStrip::DoGetBestSize() const {
  const int w = GetClientSize().GetWidth();
  return {wxDefaultCoord, layout_height(w > 0 ? w : FromDIP(400))};
}

void TabStrip::ensure_hover_size() {
  const std::size_t n = owner_->GetPageCount();
  hover_.resize(n, 0.f);
}

void TabStrip::rebuild_layout(int width) {
  rects_.clear();
  const std::size_t n = owner_->GetPageCount();
  if (!n || width <= 0) return;
  SetFont(Theme::ui());
  const int ear = FromDIP(kTabRadiusDip);
  const int overlap = ear;
  const int hpad = FromDIP(14);
  const int th = FromDIP(kTabHeightDip);
  const int top = FromDIP(kTabTopGapDip);
  int x = ear;
  int y = top;
  for (std::size_t i = 0; i < n; ++i) {
    const wxSize text = GetTextExtent(owner_->GetPageText(i));
    const int tw = std::clamp(text.x + hpad * 2, FromDIP(72), FromDIP(240));
    if (x + tw + ear > width && x > ear) {
      x = ear;
      y += th;
    }
    rects_.push_back(wxRect(x, y, tw, th));
    x += tw - overlap;
  }
}

int TabStrip::layout_height(int width) const {
  const int top = FromDIP(kTabTopGapDip);
  const int th = FromDIP(kTabHeightDip);
  if (owner_->GetPageCount() == 0) return top + th;
  TabStrip* self = const_cast<TabStrip*>(this);
  self->rebuild_layout(width > 0 ? width : FromDIP(400));
  if (rects_.empty()) return top + th;
  int bottom = 0;
  for (const auto& r : rects_) bottom = std::max(bottom, r.GetBottom());
  return bottom;
}

int TabStrip::hit_test(const wxPoint& pos) const {
  const int ear = FromDIP(kTabRadiusDip);
  const int sel = owner_->GetSelection();
  auto hits = [ear, pos](const wxRect& r) {
    wxRect hit = r;
    hit.x -= ear;
    hit.width += ear * 2;
    return hit.Contains(pos);
  };
  if (sel >= 0 && sel < static_cast<int>(rects_.size()) && hits(rects_[static_cast<std::size_t>(sel)])) {
    return sel;
  }
  for (int i = static_cast<int>(rects_.size()) - 1; i >= 0; --i) {
    if (i == sel) continue;
    if (hits(rects_[static_cast<std::size_t>(i)])) return i;
  }
  return -1;
}

void TabStrip::on_paint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  const wxColour strip_bg = Theme::bg();
  wxGCDC gc(dc);
  gc.SetBackground(wxBrush(strip_bg));
  gc.Clear();
  wxGraphicsContext* gfx = gc.GetGraphicsContext();
  if (gfx) gfx->SetAntialiasMode(wxANTIALIAS_DEFAULT);
  ensure_hover_size();
  if (rects_.size() != owner_->GetPageCount()) rebuild_layout(GetClientSize().GetWidth());
  gc.SetFont(Theme::ui());
  const int sel = owner_->GetSelection();
  const double radius = FromDIP(kTabRadiusDip);
  const int hpad = FromDIP(14);
  const int strip_h = GetClientSize().GetHeight();

  for (std::size_t i = 0; i < rects_.size(); ++i) {
    if (static_cast<int>(i) == sel) continue;
    const float hov = i < hover_.size() ? hover_[i] : 0.f;
    const std::size_t next = i + 1;
    const bool next_sel = static_cast<int>(next) == sel;
    const float next_hov = next < hover_.size() ? hover_[next] : 0.f;
    if (hov < 0.2f && !next_sel && next_hov < 0.2f && next < rects_.size()) {
      const wxRect r = rects_[i];
      const int sx = r.GetRight() - FromDIP(kTabRadiusDip) / 2;
      const int sy = r.y + r.height / 4;
      const int sh = r.height / 2;
      gc.SetPen(wxPen(Theme::blend(strip_bg, Theme::border(), 0.75f), 1));
      gc.DrawLine(sx, sy, sx, sy + sh);
    }
  }

  for (std::size_t i = 0; i < rects_.size(); ++i) {
    if (static_cast<int>(i) == sel) continue;
    const float hov = i < hover_.size() ? hover_[i] : 0.f;
    const wxRect r = rects_[i];
    const int inset = FromDIP(5);
    wxColour fill = Theme::blend(strip_bg, Theme::chrome(), 0.55f + 0.45f * hov);
    gc.SetPen(*wxTRANSPARENT_PEN);
    gc.SetBrush(wxBrush(fill));
    gc.DrawRoundedRectangle(r.x + inset + 0.5, r.y + inset + 0.5, r.width - inset * 2 - 1.0,
                             r.height - inset * 2 - 1.0, FromDIP(6));
  }

  if (sel >= 0 && sel < static_cast<int>(rects_.size()) && gfx) {
    wxRect r = rects_[static_cast<std::size_t>(sel)];
    if (r.GetBottom() >= strip_h - FromDIP(2)) {
      r.height = strip_h - r.y;
    }
    const float hov = static_cast<std::size_t>(sel) < hover_.size() ? hover_[static_cast<std::size_t>(sel)] : 0.f;
    wxColour fill = Theme::blend(Theme::elevated(), Theme::text_bright(), theme_is_dark() ? 0.10f : 0.0f);
    fill = Theme::blend(fill, Theme::hover(), hov * 0.12f);
    gfx->SetPen(wxNullPen);
    gfx->SetBrush(wxBrush(fill));
    gfx->FillPath(chrome_tab_path(gfx, r, radius));
    const int bar_h = std::max(2, FromDIP(3));
    gc.SetPen(*wxTRANSPARENT_PEN);
    gc.SetBrush(wxBrush(Theme::accent()));
    gc.DrawRectangle(static_cast<int>(r.x + radius * 0.35), r.y + 1,
                     static_cast<int>(r.width - radius * 0.7), bar_h);
  }

  for (std::size_t i = 0; i < rects_.size(); ++i) {
    const wxRect r = rects_[i];
    const float hov = i < hover_.size() ? hover_[i] : 0.f;
    const bool selected = static_cast<int>(i) == sel;
    wxFont font = Theme::ui();
    if (selected) font.SetWeight(wxFONTWEIGHT_SEMIBOLD);
    gc.SetFont(font);
    gc.SetTextForeground(selected ? Theme::text_bright()
                                  : Theme::blend(Theme::muted(), Theme::text(), 0.15f + 0.35f * hov));
    const wxString title = owner_->GetPageText(i);
    const wxSize text = gc.GetTextExtent(title);
    const int tx = r.x + hpad;
    const int ty = r.y + (r.height - text.y) / 2;
    gc.DrawText(title, tx, ty);
  }
}

void TabStrip::on_size(wxSizeEvent& e) {
  rebuild_layout(GetClientSize().GetWidth());
  Refresh();
  e.Skip();
}

void TabStrip::emit_tab_right_click(int index) {
  wxCommandEvent ev(wxEVT_TAB_RIGHT_CLICK, owner_->GetId());
  ev.SetEventObject(owner_);
  ev.SetInt(index);
  owner_->ProcessWindowEvent(ev);
}

void TabStrip::on_mouse(wxMouseEvent& e) {
  const int hit = hit_test(e.GetPosition());
  if (hit != hover_index_) {
    hover_index_ = hit;
    if (!timer_.IsRunning()) timer_.Start(16);
    SetCursor(hit >= 0 ? wxCURSOR_HAND : wxCURSOR_ARROW);
  }
  if (e.RightUp() && hit >= 0) {
    owner_->SetSelection(hit);
    emit_tab_right_click(hit);
    return;
  }
  if (e.LeftDown() && hit >= 0) {
    owner_->SetSelection(hit);
  }
  e.Skip();
}

void TabStrip::on_leave(wxMouseEvent&) {
  hover_index_ = -1;
  if (!timer_.IsRunning()) timer_.Start(16);
  SetCursor(wxCURSOR_ARROW);
}

void TabStrip::on_timer(wxTimerEvent&) {
  ensure_hover_size();
  bool dirty = false;
  for (std::size_t i = 0; i < hover_.size(); ++i) {
    const float target = (static_cast<int>(i) == hover_index_) ? 1.f : 0.f;
    const float next = hover_[i] + (target - hover_[i]) * 0.28f;
    if (std::abs(next - hover_[i]) > 0.008f) {
      hover_[i] = next;
      dirty = true;
    } else if (hover_[i] != target) {
      hover_[i] = target;
      dirty = true;
    }
  }
  if (dirty) Refresh();
  else timer_.Stop();
}

RoundedNotebook::RoundedNotebook(wxWindow* parent, wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL) {
  SetBackgroundColour(Theme::bg());
  strip_ = new TabStrip(this);
  body_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL);
  body_->SetName(L"card-page");
  body_->SetBackgroundColour(Theme::elevated());
  auto* inner = new wxBoxSizer(wxVERTICAL);
  body_->SetSizer(inner);
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(strip_, 0, wxEXPAND);
  root->Add(body_, 1, wxEXPAND);
  SetSizer(root);
}

bool RoundedNotebook::AddPage(wxWindow* page, const wxString& text, bool select) {
  if (!page) return false;
  page->Reparent(body_);
  body_->GetSizer()->Add(page, 1, wxEXPAND);
  pages_.push_back({page, text});
  if (selection_ < 0 || select) {
    const int old = selection_;
    if (old >= 0 && static_cast<std::size_t>(old) < pages_.size() - 1) {
      pages_[static_cast<std::size_t>(old)].window->Hide();
    }
    selection_ = static_cast<int>(pages_.size() - 1);
    page->Show();
    if (old != selection_) emit_changed(old, selection_);
  } else {
    page->Hide();
  }
  relayout_body();
  strip_->notify_pages_changed();
  return true;
}

bool RoundedNotebook::DeletePage(std::size_t n) {
  if (n >= pages_.size()) return false;
  wxWindow* win = pages_[n].window;
  body_->GetSizer()->Detach(win);
  pages_.erase(pages_.begin() + static_cast<std::ptrdiff_t>(n));
  if (win) win->Destroy();
  if (pages_.empty()) {
    selection_ = -1;
  } else if (selection_ >= static_cast<int>(pages_.size())) {
    selection_ = static_cast<int>(pages_.size() - 1);
  }
  if (selection_ >= 0) pages_[static_cast<std::size_t>(selection_)].window->Show();
  relayout_body();
  strip_->notify_pages_changed();
  return true;
}

wxWindow* RoundedNotebook::GetPage(std::size_t n) const {
  if (n >= pages_.size()) return nullptr;
  return pages_[n].window;
}

wxString RoundedNotebook::GetPageText(std::size_t n) const {
  if (n >= pages_.size()) return {};
  return pages_[n].title;
}

int RoundedNotebook::SetSelection(int n) {
  if (n < 0 || n >= static_cast<int>(pages_.size())) return selection_;
  const int old = selection_;
  if (old == n) return old;
  if (old >= 0 && static_cast<std::size_t>(old) < pages_.size()) {
    pages_[static_cast<std::size_t>(old)].window->Hide();
  }
  selection_ = n;
  pages_[static_cast<std::size_t>(n)].window->Show();
  relayout_body();
  strip_->Refresh();
  emit_changed(old, selection_);
  return old;
}

void RoundedNotebook::emit_changed(int old_sel, int new_sel) {
  wxBookCtrlEvent ev(wxEVT_NOTEBOOK_PAGE_CHANGED, GetId(), new_sel, old_sel);
  ev.SetEventObject(this);
  ProcessWindowEvent(ev);
}

void RoundedNotebook::relayout_body() {
  if (body_) body_->Layout();
  Layout();
}

wxDEFINE_EVENT(wxEVT_TAB_RIGHT_CLICK, wxCommandEvent);

namespace {

struct PopupRow {
  wxString label;
  wxString hint;
  int id = -1;
  bool enabled = true;
  bool separator = false;
};

class ThemedPopup : public wxPopupTransientWindow {
 public:
  ThemedPopup(wxWindow* parent, std::vector<PopupRow> rows, int preselect)
      : wxPopupTransientWindow(parent, wxBORDER_NONE), rows_(std::move(rows)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    hover_ = first_enabled(preselect);
    Bind(wxEVT_PAINT, &ThemedPopup::on_paint, this);
    Bind(wxEVT_MOTION, &ThemedPopup::on_mouse, this);
    Bind(wxEVT_LEFT_UP, &ThemedPopup::on_mouse, this);
    Bind(wxEVT_CHAR_HOOK, &ThemedPopup::on_key, this);
  }

  bool finished() const { return finished_; }
  int result() const { return result_; }
  void nudge(int delta) { move_hover(delta); }
  void accept_current() { accept(hover_); }
  void cancel() {
    if (finished_) return;
    result_ = -1;
    finished_ = true;
    if (IsShown()) Dismiss();
  }

  wxSize preferred_size(wxWindow* measure, int min_width) const {
    wxClientDC dc(measure);
    dc.SetFont(Theme::ui());
    int text_w = 0;
    int hint_w = 0;
    int height = FromDIP(8);
    for (const auto& row : rows_) {
      if (row.separator) {
        height += FromDIP(9);
        continue;
      }
      const wxSize label = dc.GetTextExtent(row.label);
      text_w = std::max(text_w, label.GetWidth());
      if (!row.hint.empty()) hint_w = std::max(hint_w, dc.GetTextExtent(row.hint).GetWidth());
      height += FromDIP(30);
    }
    height += FromDIP(8);
    const int gap = hint_w ? FromDIP(24) : 0;
    const int width = std::max(min_width, FromDIP(16) + text_w + gap + hint_w + FromDIP(16));
    return {width, std::max(height, FromDIP(36))};
  }

 private:
  int first_enabled(int prefer) const {
    if (prefer >= 0 && prefer < static_cast<int>(rows_.size()) && rows_[static_cast<std::size_t>(prefer)].enabled &&
        !rows_[static_cast<std::size_t>(prefer)].separator) {
      return prefer;
    }
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
      if (rows_[static_cast<std::size_t>(i)].enabled && !rows_[static_cast<std::size_t>(i)].separator) return i;
    }
    return -1;
  }

  int row_at(int y) const {
    int acc = FromDIP(4);
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
      const int h = rows_[static_cast<std::size_t>(i)].separator ? FromDIP(9) : FromDIP(30);
      if (y >= acc && y < acc + h) return i;
      acc += h;
    }
    return -1;
  }

  void move_hover(int delta) {
    if (rows_.empty()) return;
    int i = hover_;
    for (int n = 0; n < static_cast<int>(rows_.size()); ++n) {
      i += delta;
      if (i < 0) i = static_cast<int>(rows_.size()) - 1;
      if (i >= static_cast<int>(rows_.size())) i = 0;
      const auto& row = rows_[static_cast<std::size_t>(i)];
      if (row.enabled && !row.separator) {
        hover_ = i;
        Refresh();
        return;
      }
    }
  }

  void accept(int index) {
    if (index < 0 || index >= static_cast<int>(rows_.size())) return;
    const auto& row = rows_[static_cast<std::size_t>(index)];
    if (!row.enabled || row.separator) return;
    result_ = row.id;
    finished_ = true;
    Dismiss();
  }

  void on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    wxGCDC gc(dc);
    const wxSize sz = GetClientSize();
    gc.SetBackground(wxBrush(Theme::elevated()));
    gc.Clear();
    gc.SetPen(wxPen(Theme::border()));
    gc.SetBrush(*wxTRANSPARENT_BRUSH);
    gc.DrawRectangle(0, 0, sz.x, sz.y);
    gc.SetFont(Theme::ui());
    int y = FromDIP(4);
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
      const auto& row = rows_[static_cast<std::size_t>(i)];
      if (row.separator) {
        const int mid = y + FromDIP(4);
        gc.SetPen(wxPen(Theme::border()));
        gc.DrawLine(FromDIP(8), mid, sz.x - FromDIP(8), mid);
        y += FromDIP(9);
        continue;
      }
      const int h = FromDIP(30);
      const bool hot = i == hover_ && row.enabled;
      if (hot) {
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(wxBrush(Theme::select()));
        gc.DrawRectangle(FromDIP(4), y, sz.x - FromDIP(8), h);
      }
      gc.SetTextForeground(row.enabled ? (hot ? Theme::text_bright() : Theme::text()) : Theme::muted());
      const wxSize ext = gc.GetTextExtent(row.label);
      gc.DrawText(row.label, FromDIP(12), y + (h - ext.y) / 2);
      if (!row.hint.empty()) {
        gc.SetTextForeground(Theme::muted());
        const wxSize hint = gc.GetTextExtent(row.hint);
        gc.DrawText(row.hint, sz.x - FromDIP(12) - hint.x, y + (h - hint.y) / 2);
      }
      y += h;
    }
  }

  void on_mouse(wxMouseEvent& e) {
    const int index = row_at(e.GetPosition().y);
    if (e.Moving() || e.Dragging()) {
      if (index != hover_) {
        hover_ = index;
        Refresh();
      }
      return;
    }
    if (e.LeftUp()) accept(index);
  }

  void on_key(wxKeyEvent& e) {
    const int key = e.GetKeyCode();
    if (key == WXK_ESCAPE) {
      finished_ = true;
      result_ = -1;
      Dismiss();
      return;
    }
    if (key == WXK_UP || key == WXK_NUMPAD_UP) {
      move_hover(-1);
      return;
    }
    if (key == WXK_DOWN || key == WXK_NUMPAD_DOWN) {
      move_hover(1);
      return;
    }
    if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER || key == WXK_SPACE) {
      accept(hover_);
      return;
    }
    e.Skip();
  }

  void OnDismiss() override {
    finished_ = true;
    wxPopupTransientWindow::OnDismiss();
  }

  std::vector<PopupRow> rows_;
  int hover_ = -1;
  int result_ = -1;
  bool finished_ = false;
};

struct MenuSwitch {
  int current = -1;
  int count = 0;
  int next = -1;
  bool busy = false;
  ThemedPopup* pop = nullptr;
};

MenuSwitch* g_menu_switch = nullptr;

void menu_bar_switch_to(int index) {
  if (!g_menu_switch) return;
  if (index < 0 || index >= g_menu_switch->count || index == g_menu_switch->current) return;
  g_menu_switch->next = index;
  if (g_menu_switch->busy || !g_menu_switch->pop || g_menu_switch->pop->finished()) return;
  g_menu_switch->busy = true;
  g_menu_switch->pop->cancel();
  g_menu_switch->busy = false;
}

void menu_bar_switch_delta(int delta) {
  if (!g_menu_switch || g_menu_switch->count <= 1) return;
  int next = g_menu_switch->current + delta;
  if (next < 0) next = g_menu_switch->count - 1;
  if (next >= g_menu_switch->count) next = 0;
  menu_bar_switch_to(next);
}

void menu_bar_cancel() {
  if (!g_menu_switch || g_menu_switch->busy) return;
  g_menu_switch->next = -1;
  if (!g_menu_switch->pop || g_menu_switch->pop->finished()) return;
  g_menu_switch->busy = true;
  g_menu_switch->pop->cancel();
  g_menu_switch->busy = false;
}

bool menu_switch_armed() {
  return g_menu_switch && g_menu_switch->pop && !g_menu_switch->pop->finished();
}

bool pointer_over_menu_popup() {
  if (!menu_switch_armed()) return false;
  return g_menu_switch->pop->GetScreenRect().Contains(wxGetMousePosition());
}

class MenuKeyFilter : public wxEventFilter {
 public:
  int FilterEvent(wxEvent& event) override {
    if (event.GetEventType() != wxEVT_CHAR_HOOK || !menu_switch_armed()) return Event_Skip;
    auto* key = static_cast<wxKeyEvent*>(&event);
    if (key->ControlDown() || key->AltDown()) return Event_Skip;
    switch (key->GetKeyCode()) {
      case WXK_ESCAPE:
        menu_bar_cancel();
        return Event_Processed;
      case WXK_LEFT:
      case WXK_NUMPAD_LEFT:
        menu_bar_switch_delta(-1);
        return Event_Processed;
      case WXK_RIGHT:
      case WXK_NUMPAD_RIGHT:
        menu_bar_switch_delta(1);
        return Event_Processed;
      case WXK_UP:
      case WXK_NUMPAD_UP:
        g_menu_switch->pop->nudge(-1);
        return Event_Processed;
      case WXK_DOWN:
      case WXK_NUMPAD_DOWN:
        g_menu_switch->pop->nudge(1);
        return Event_Processed;
      case WXK_RETURN:
      case WXK_NUMPAD_ENTER:
      case WXK_SPACE:
        g_menu_switch->pop->accept_current();
        return Event_Processed;
      default:
        return Event_Skip;
    }
  }
};

class MenuFilterGuard {
 public:
  MenuFilterGuard(wxEventFilter* filter, MenuSwitch* track) {
    if (!track) return;
    active_ = true;
    prev_ = g_menu_switch;
    g_menu_switch = track;
    filter_ = filter;
    wxEvtHandler::AddFilter(filter_);
  }
  ~MenuFilterGuard() {
    if (!active_) return;
    wxEvtHandler::RemoveFilter(filter_);
    g_menu_switch = prev_;
  }
  MenuFilterGuard(const MenuFilterGuard&) = delete;
  MenuFilterGuard& operator=(const MenuFilterGuard&) = delete;

 private:
  wxEventFilter* filter_ = nullptr;
  MenuSwitch* prev_ = nullptr;
  bool active_ = false;
};

wxPoint clamp_popup(const wxPoint& screen, const wxSize& size, int anchor_h) {
  const int display = wxDisplay::GetFromPoint(screen);
  wxRect area = display == wxNOT_FOUND ? wxRect(0, 0, 1280, 800) : wxDisplay(display).GetClientArea();
  wxPoint pos = screen;
  if (pos.x + size.x > area.GetRight()) pos.x = area.GetRight() - size.x;
  if (pos.x < area.x) pos.x = area.x;
  if (pos.y + size.y > area.GetBottom()) pos.y = std::max(area.y, screen.y - anchor_h - size.y);
  if (pos.y < area.y) pos.y = area.y;
  return pos;
}

int run_popup(wxWindow* parent, std::vector<PopupRow> rows, const wxPoint& screen, int min_width, int anchor_h,
              int preselect, MenuSwitch* track = nullptr) {
  if (!parent || rows.empty()) return -1;
  wxWindow* owner = wxGetTopLevelParent(parent);
  if (!owner) owner = parent;
  auto* pop = new ThemedPopup(owner, std::move(rows), preselect);
  const wxSize size = pop->preferred_size(owner, min_width);
  pop->SetSize(size);
  pop->SetPosition(clamp_popup(screen, size, anchor_h));
  if (track) {
    track->pop = pop;
    track->next = -1;
  }
  int id = -1;
  {
    MenuKeyFilter filter;
    MenuFilterGuard guard(track ? &filter : nullptr, track);
    pop->Popup();
    pop->SetFocus();
    pop->Update();
    wxEventLoop loop;
    wxEventLoopActivator activator(&loop);
    while (!pop->finished()) {
      if (!loop.Dispatch()) break;
    }
    id = pop->result();
    if (track) track->pop = nullptr;
  }
  if (!pop->IsBeingDeleted()) pop->Destroy();
  return id;
}

std::vector<PopupRow> menu_rows(wxMenu* menu) {
  std::vector<PopupRow> rows;
  if (!menu) return rows;
  rows.reserve(menu->GetMenuItemCount());
  for (size_t i = 0; i < menu->GetMenuItemCount(); ++i) {
    wxMenuItem* item = menu->FindItemByPosition(i);
    if (!item) continue;
    PopupRow row;
    if (item->IsSeparator()) {
      row.separator = true;
      rows.push_back(std::move(row));
      continue;
    }
    row.label = item->GetItemLabelText();
    row.id = item->GetId();
    row.enabled = item->IsEnabled();
    if (auto* accel = item->GetAccel()) row.hint = accel->ToString();
    if (row.hint.empty()) {
      const wxString full = item->GetItemLabel();
      const int tab = full.Find(wxUniChar('\t'));
      if (tab != wxNOT_FOUND) row.hint = full.Mid(tab + 1);
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

void dispatch_menu(wxWindow* parent, wxMenu* menu, int id) {
  if (!parent || !menu || id < 0) return;
  wxCommandEvent ev(wxEVT_MENU, id);
  ev.SetEventObject(menu);
  if (!menu->ProcessEvent(ev)) {
    ev.SetEventObject(parent);
    parent->ProcessWindowEvent(ev);
  }
}

void draw_check(wxGraphicsContext* gfx, double x, double y, double size, const wxColour& colour) {
  if (!gfx) return;
  gfx->SetPen(wxPen(colour, std::max(1.5, size / 8.0)));
  wxGraphicsPath path = gfx->CreatePath();
  path.MoveToPoint(x + size * 0.22, y + size * 0.52);
  path.AddLineToPoint(x + size * 0.42, y + size * 0.72);
  path.AddLineToPoint(x + size * 0.78, y + size * 0.30);
  gfx->StrokePath(path);
}

void draw_chevron(wxGraphicsContext* gfx, double x, double y, double size, const wxColour& colour) {
  if (!gfx) return;
  gfx->SetPen(wxPen(colour, std::max(1.5, size / 8.0)));
  wxGraphicsPath path = gfx->CreatePath();
  path.MoveToPoint(x, y);
  path.AddLineToPoint(x + size * 0.5, y + size * 0.55);
  path.AddLineToPoint(x + size, y);
  gfx->StrokePath(path);
}

}  // namespace

ThemedCheckBox::ThemedCheckBox(wxWindow* parent, wxWindowID id, const wxString& label)
    : wxControl(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL) {
  SetLabel(label);
  SetFont(Theme::ui());
  SetCanFocus(true);
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  Bind(wxEVT_PAINT, &ThemedCheckBox::on_paint, this);
  Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) { toggle(); });
  Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) {
    Refresh();
    e.Skip();
  });
  Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
    Refresh();
    e.Skip();
  });
  Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
    if (IsEnabled() && e.GetKeyCode() == WXK_SPACE) {
      toggle();
      return;
    }
    e.Skip();
  });
}

void ThemedCheckBox::SetLabel(const wxString& label) {
  wxControl::SetLabel(label);
  InvalidateBestSize();
  Refresh();
}

void ThemedCheckBox::SetValue(bool on) {
  if (value_ == on) return;
  value_ = on;
  Refresh();
}

void ThemedCheckBox::DoEnable(bool enable) {
  wxControl::DoEnable(enable);
  Refresh();
}

wxSize ThemedCheckBox::DoGetBestSize() const {
  const wxSize text = GetTextExtent(GetLabel());
  const int box = FromDIP(16);
  const int pad = FromDIP(3);
  return {pad + box + FromDIP(8) + text.x + pad, std::max(FromDIP(24), text.y + FromDIP(8))};
}

void ThemedCheckBox::toggle() {
  if (!IsEnabled()) return;
  value_ = !value_;
  Refresh();
  wxCommandEvent ev(wxEVT_CHECKBOX, GetId());
  ev.SetEventObject(this);
  ev.SetInt(value_ ? 1 : 0);
  ProcessWindowEvent(ev);
}

void ThemedCheckBox::on_paint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  wxGCDC gc(dc);
  const wxColour parent_bg = GetParent() ? GetParent()->GetBackgroundColour() : Theme::bg();
  gc.SetBackground(wxBrush(parent_bg));
  gc.Clear();
  const int box = FromDIP(16);
  const int pad = FromDIP(3);
  const int x = pad;
  const int y = (GetClientSize().y - box) / 2;
  const bool on = value_;
  wxColour fill = on ? Theme::accent() : Theme::btn();
  wxColour border = on ? Theme::accent() : Theme::border();
  if (!IsEnabled()) {
    fill = Theme::chrome();
    border = Theme::border();
  }
  if (wxGraphicsContext* gfx = gc.GetGraphicsContext()) gfx->SetAntialiasMode(wxANTIALIAS_DEFAULT);
  gc.SetPen(wxPen(border));
  gc.SetBrush(wxBrush(fill));
  gc.DrawRoundedRectangle(x, y, box, box, FromDIP(4));
  if (on) {
    if (wxGraphicsContext* gfx = gc.GetGraphicsContext()) draw_check(gfx, x, y, box, *wxWHITE);
  }
  if (IsEnabled() && HasFocus()) {
    gc.SetBrush(*wxTRANSPARENT_BRUSH);
    gc.SetPen(wxPen(Theme::accent(), std::max(1, FromDIP(2))));
    gc.DrawRoundedRectangle(x - pad, y - pad, box + pad * 2, box + pad * 2, FromDIP(6));
  }
  gc.SetFont(GetFont().IsOk() ? GetFont() : Theme::ui());
  gc.SetTextForeground(IsEnabled() ? Theme::text() : Theme::muted());
  const wxString label = GetLabel();
  const wxSize text = gc.GetTextExtent(label);
  gc.DrawText(label, box + FromDIP(8), (GetClientSize().y - text.y) / 2);
}

ThemedChoice::ThemedChoice(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size,
                           const wxArrayString& choices)
    : wxControl(parent, id, pos, size, wxBORDER_NONE | wxTAB_TRAVERSAL) {
  init(wxString(), choices);
}

ThemedChoice::ThemedChoice(wxWindow* parent, wxWindowID id, const wxString& value, const wxPoint& pos,
                           const wxSize& size, const wxArrayString& choices)
    : wxControl(parent, id, pos, size, wxBORDER_NONE | wxTAB_TRAVERSAL) {
  init(value, choices);
}

void ThemedChoice::init(const wxString& value, const wxArrayString& choices) {
  SetFont(Theme::ui());
  SetCanFocus(true);
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetCursor(wxCURSOR_HAND);
  for (unsigned i = 0; i < choices.size(); ++i) items_.push_back(choices[i]);
  if (!value.empty()) {
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
      if (items_[static_cast<std::size_t>(i)] == value) {
        selection_ = i;
        break;
      }
    }
  }
  Bind(wxEVT_PAINT, &ThemedChoice::on_paint, this);
  Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) { open_popup(); });
  Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) {
    Refresh();
    e.Skip();
  });
  Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
    Refresh();
    e.Skip();
  });
  Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
    if (!IsEnabled()) {
      e.Skip();
      return;
    }
    const int key = e.GetKeyCode();
    if (key == WXK_SPACE || key == WXK_F4 || ((key == WXK_DOWN || key == WXK_NUMPAD_DOWN) && e.AltDown())) {
      open_popup();
      return;
    }
    if (key == WXK_DOWN || key == WXK_NUMPAD_DOWN) {
      move_selection(1);
      return;
    }
    if (key == WXK_UP || key == WXK_NUMPAD_UP) {
      move_selection(-1);
      return;
    }
    e.Skip();
  });
}

void ThemedChoice::Clear() {
  items_.clear();
  selection_ = -1;
  InvalidateBestSize();
  Refresh();
}

int ThemedChoice::Append(const wxString& item) {
  items_.push_back(item);
  InvalidateBestSize();
  Refresh();
  return static_cast<int>(items_.size()) - 1;
}

void ThemedChoice::SetSelection(int n) {
  if (n < 0 || n >= static_cast<int>(items_.size())) n = -1;
  selection_ = n;
  Refresh();
}

wxString ThemedChoice::GetValue() const {
  if (selection_ < 0 || selection_ >= static_cast<int>(items_.size())) return {};
  return items_[static_cast<std::size_t>(selection_)];
}

void ThemedChoice::DoEnable(bool enable) {
  wxControl::DoEnable(enable);
  Refresh();
}

wxSize ThemedChoice::DoGetBestSize() const {
  wxClientDC dc(const_cast<ThemedChoice*>(this));
  dc.SetFont(GetFont().IsOk() ? GetFont() : Theme::ui());
  int text_w = FromDIP(80);
  for (const auto& item : items_) text_w = std::max(text_w, dc.GetTextExtent(item).GetWidth());
  const wxSize one = dc.GetTextExtent(L"Ag");
  return {text_w + FromDIP(36), std::max(FromDIP(32), one.y + FromDIP(12))};
}

void ThemedChoice::choose(int index, bool notify) {
  if (index < 0 || index >= static_cast<int>(items_.size())) return;
  const bool changed = index != selection_;
  selection_ = index;
  Refresh();
  if (!notify || !changed) return;
  wxCommandEvent choice(wxEVT_CHOICE, GetId());
  choice.SetEventObject(this);
  choice.SetInt(selection_);
  choice.SetString(GetValue());
  ProcessWindowEvent(choice);
  wxCommandEvent combo(wxEVT_COMBOBOX, GetId());
  combo.SetEventObject(this);
  combo.SetInt(selection_);
  combo.SetString(GetValue());
  ProcessWindowEvent(combo);
}

void ThemedChoice::move_selection(int delta) {
  if (items_.empty()) return;
  int next = selection_ < 0 ? (delta > 0 ? 0 : static_cast<int>(items_.size()) - 1) : selection_ + delta;
  next = std::clamp(next, 0, static_cast<int>(items_.size()) - 1);
  choose(next, true);
}

void ThemedChoice::open_popup() {
  if (!IsEnabled() || items_.empty() || open_) return;
  std::vector<PopupRow> rows;
  rows.reserve(items_.size());
  for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
    PopupRow row;
    row.label = items_[static_cast<std::size_t>(i)];
    row.id = i;
    rows.push_back(std::move(row));
  }
  open_ = true;
  Refresh();
  const wxPoint screen = ClientToScreen(wxPoint(0, GetClientSize().y));
  const int picked = run_popup(this, std::move(rows), screen, GetClientSize().x, GetClientSize().y, selection_);
  open_ = false;
  if (picked >= 0) choose(picked, true);
  Refresh();
  if (!IsBeingDeleted()) SetFocus();
}

void ThemedChoice::on_paint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  wxGCDC gc(dc);
  const wxColour parent_bg = GetParent() ? GetParent()->GetBackgroundColour() : Theme::bg();
  gc.SetBackground(wxBrush(parent_bg));
  gc.Clear();
  if (wxGraphicsContext* gfx = gc.GetGraphicsContext()) gfx->SetAntialiasMode(wxANTIALIAS_DEFAULT);
  const wxSize sz = GetClientSize();
  const double radius = FromDIP(4);
  wxColour fill = Theme::btn();
  wxColour fg = Theme::text();
  if (!IsEnabled()) fg = Theme::muted();
  gc.SetPen(wxPen(Theme::border()));
  gc.SetBrush(wxBrush(fill));
  gc.DrawRoundedRectangle(0.5, 0.5, std::max(1.0, sz.x - 1.0), std::max(1.0, sz.y - 1.0), radius);
  if (IsEnabled() && (HasFocus() || open_)) {
    gc.SetBrush(*wxTRANSPARENT_BRUSH);
    gc.SetPen(wxPen(Theme::accent(), std::max(1, FromDIP(2))));
    const double inset = FromDIP(2);
    gc.DrawRoundedRectangle(inset, inset, std::max(1.0, sz.x - inset * 2), std::max(1.0, sz.y - inset * 2), radius);
  }
  gc.SetFont(GetFont().IsOk() ? GetFont() : Theme::ui());
  gc.SetTextForeground(fg);
  const wxString label = GetValue();
  const wxSize text = gc.GetTextExtent(label);
  const int chevron = FromDIP(10);
  const int text_x = FromDIP(10);
  const int text_right = sz.x - FromDIP(22);
  if (text_right > text_x) {
    gc.SetClippingRegion(text_x, 0, text_right - text_x, sz.y);
    gc.DrawText(label, text_x, (sz.y - text.y) / 2);
    gc.DestroyClippingRegion();
  }
  if (wxGraphicsContext* gfx = gc.GetGraphicsContext()) {
    draw_chevron(gfx, sz.x - FromDIP(16), (sz.y - chevron * 0.55) / 2.0, chevron,
                 IsEnabled() ? Theme::muted() : Theme::border());
  }
}

ThemedMenuBar::ThemedMenuBar(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
  SetName(L"chrome");
  SetFont(Theme::ui());
  SetCanFocus(false);
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetMinSize(wxSize(-1, FromDIP(32)));
  SetCursor(wxCURSOR_HAND);
  Bind(wxEVT_PAINT, &ThemedMenuBar::on_paint, this);
  Bind(wxEVT_LEFT_DOWN, &ThemedMenuBar::on_mouse, this);
  Bind(wxEVT_LEFT_UP, &ThemedMenuBar::on_mouse, this);
  Bind(wxEVT_MOTION, &ThemedMenuBar::on_mouse, this);
  Bind(wxEVT_LEAVE_WINDOW, &ThemedMenuBar::on_mouse, this);
  track_timer_.Bind(wxEVT_TIMER, &ThemedMenuBar::on_track_timer, this);
}

ThemedMenuBar::~ThemedMenuBar() {
  track_timer_.Stop();
  for (auto& entry : entries_) delete entry.menu;
}

void ThemedMenuBar::AddMenu(const wxString& title, wxMenu* menu) {
  entries_.push_back(Entry{title, menu, {}});
  InvalidateBestSize();
  Refresh();
}

void ThemedMenuBar::on_paint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  wxGCDC gc(dc);
  const wxSize sz = GetClientSize();
  gc.SetBackground(wxBrush(Theme::chrome()));
  gc.Clear();
  gc.SetFont(GetFont().IsOk() ? GetFont() : Theme::ui());
  int x = FromDIP(8);
  const int pad_x = FromDIP(12);
  const int h = sz.y;
  for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
    auto& entry = entries_[static_cast<std::size_t>(index)];
    const wxSize text = gc.GetTextExtent(entry.title);
    const int w = text.x + pad_x * 2;
    entry.rect = wxRect(x, 0, w, h);
    const int lit = hover_ >= 0 ? hover_ : open_index_;
    if (index == lit) {
      gc.SetPen(*wxTRANSPARENT_PEN);
      gc.SetBrush(wxBrush(Theme::hover()));
      gc.DrawRectangle(entry.rect);
    }
    gc.SetTextForeground(Theme::text());
    gc.DrawText(entry.title, x + pad_x, (h - text.y) / 2);
    x += w;
  }
  gc.SetPen(wxPen(Theme::border()));
  gc.DrawLine(0, h - 1, sz.x, h - 1);
}

int ThemedMenuBar::index_at(const wxPoint& client) const {
  for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
    if (entries_[static_cast<std::size_t>(i)].rect.Contains(client)) return i;
  }
  return -1;
}

void ThemedMenuBar::on_mouse(wxMouseEvent& e) {
  if (e.Leaving()) {
    if (hover_ != -1) {
      hover_ = -1;
      Refresh();
    }
    return;
  }
  const int found = index_at(e.GetPosition());
  if (e.LeftDown()) {
    if (open_index_ >= 0 && found >= 0) {
      // Отпускание этой кнопки не должно сразу открыть меню заново.
      swallow_up_ = true;
      if (found == open_index_) menu_bar_cancel();
      else menu_bar_switch_to(found);
      return;
    }
    swallow_up_ = false;
  }
  if (e.LeftUp()) {
    const bool swallow = swallow_up_;
    swallow_up_ = false;
    if (!swallow && found >= 0 && open_index_ < 0) open_at(found);
    return;
  }
  if ((e.Moving() || e.Dragging()) && open_index_ >= 0 && found >= 0 && found != open_index_ &&
      menu_switch_armed()) {
    hover_ = found;
    Refresh();
    menu_bar_switch_to(found);
    return;
  }
  if ((e.Moving() || e.Dragging()) && found != hover_) {
    hover_ = found;
    Refresh();
  }
}

void ThemedMenuBar::on_track_timer(wxTimerEvent&) {
  if (!menu_switch_armed() || pointer_over_menu_popup()) return;
  const int found = index_at(ScreenToClient(wxGetMousePosition()));
  if (found < 0 || found == open_index_) return;
  hover_ = found;
  Refresh();
  menu_bar_switch_to(found);
}

void ThemedMenuBar::open_at(int index) {
  if (open_index_ >= 0 || index < 0 || index >= static_cast<int>(entries_.size())) return;
  wxWindow* parent = wxGetTopLevelParent(this);
  if (!parent) return;
  int chosen = -1;
  wxMenu* chosen_menu = nullptr;
  track_timer_.Start(32);
  while (index >= 0 && index < static_cast<int>(entries_.size())) {
    wxMenu* menu = entries_[static_cast<std::size_t>(index)].menu;
    if (!menu) break;
    open_index_ = index;
    hover_ = index;
    Refresh();
    Update();
    const wxPoint screen = ClientToScreen(wxPoint(entries_[static_cast<std::size_t>(index)].rect.x, GetClientSize().y));
    MenuSwitch track;
    track.current = index;
    track.count = static_cast<int>(entries_.size());
    const int id =
        run_popup(parent, menu_rows(menu), screen, parent->FromDIP(180), GetClientSize().y, -1, &track);
    if (track.next >= 0 && track.next != index && track.next < track.count) {
      index = track.next;
      continue;
    }
    chosen = id;
    chosen_menu = menu;
    break;
  }
  track_timer_.Stop();
  open_index_ = -1;
  hover_ = index_at(ScreenToClient(wxGetMousePosition()));
  Refresh();
  if (chosen_menu && chosen >= 0) dispatch_menu(parent, chosen_menu, chosen);
}

void show_themed_menu(wxWindow* parent, wxMenu* menu, const wxPoint& screen_pos) {
  if (!parent || !menu) return;
  const wxPoint screen = screen_pos == wxDefaultPosition ? wxGetMousePosition() : screen_pos;
  const int id = run_popup(parent, menu_rows(menu), screen, parent->FromDIP(180), 0, -1);
  if (id >= 0) dispatch_menu(parent, menu, id);
}

}  // namespace fatty

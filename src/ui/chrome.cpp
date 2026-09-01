#include "ui/chrome.hpp"

#include "ui/theme.hpp"

#include <wx/bookctrl.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/toplevel.h>
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
  Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) {
    hovered_ = true;
    Refresh();
  });
  Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
    hovered_ = false;
    pressed_ = false;
    Refresh();
  });
  Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
    if (!IsEnabled()) return;
    pressed_ = true;
    CaptureMouse();
    Refresh();
  });
  Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
    if (HasCapture()) ReleaseMouse();
    const bool inside = GetClientRect().Contains(e.GetPosition());
    const bool was = pressed_;
    pressed_ = false;
    Refresh();
    if (was && inside && IsEnabled()) fire();
  });
  Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
    if (IsEnabled() && (e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_SPACE)) {
      fire();
      return;
    }
    e.Skip();
  });
}

void RoundButton::on_size(wxSizeEvent& e) {
  Refresh();
  e.Skip();
}

bool RoundButton::Enable(bool enable) {
  const bool changed = wxControl::Enable(enable);
  Refresh();
  return changed;
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
    fire();
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
  } else if (pressed_) {
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

}  // namespace fatty

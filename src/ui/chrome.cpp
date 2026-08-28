#include "ui/chrome.hpp"

#include "ui/theme.hpp"

#include <wx/bookctrl.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
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
#endif

namespace fatty {
namespace {

void fill_round(wxDC& dc, const wxRect& r, int radius, const wxColour& fill, const wxColour& border) {
  wxGCDC gc(dc);
  gc.SetPen(wxPen(border, 1));
  gc.SetBrush(wxBrush(fill));
  gc.DrawRoundedRectangle(r.x + 0.5, r.y + 0.5, r.width - 1, r.height - 1, radius);
}

wxColour shift(const wxColour& c, int d) {
  auto ch = [d](unsigned char v) {
    return static_cast<unsigned char>(std::clamp(static_cast<int>(v) + d, 0, 255));
  };
  return {ch(c.Red()), ch(c.Green()), ch(c.Blue())};
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
  dc.SetBackground(wxBrush(GetParent() ? GetParent()->GetBackgroundColour() : Theme::bg()));
  dc.Clear();
  const int r = FromDIP(radius_dip_);
  fill_round(dc, GetClientRect(), r, Theme::elevated(), Theme::border());
}

void RoundedCard::on_size(wxSizeEvent& e) {
  apply_rounded_region(this, FromDIP(radius_dip_));
  Refresh();
  e.Skip();
}

RoundButton::RoundButton(wxWindow* parent, wxWindowID id, const wxString& label)
    : wxControl(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL) {
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
  apply_rounded_region(this, FromDIP(8));
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
  return {text.x + FromDIP(28), std::max(FromDIP(28), text.y + FromDIP(10))};
}

void RoundButton::fire() {
  wxCommandEvent ev(wxEVT_BUTTON, GetId());
  ev.SetEventObject(this);
  ProcessWindowEvent(ev);
}

void RoundButton::on_paint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  const wxColour parent_bg = GetParent() ? GetParent()->GetBackgroundColour() : Theme::bg();
  dc.SetBackground(wxBrush(parent_bg));
  dc.Clear();
  const bool accent = GetName() == L"accent";
  wxColour fill = accent ? Theme::accent() : Theme::btn();
  wxColour fg = accent ? *wxWHITE : Theme::text_bright();
  wxColour border = accent ? shift(Theme::accent(), theme_is_dark() ? 18 : -18) : Theme::border();
  if (!IsEnabled()) {
    fill = Theme::chrome();
    fg = Theme::muted();
    border = Theme::border();
  } else if (pressed_) {
    fill = shift(fill, theme_is_dark() ? -18 : -22);
  } else if (hovered_) {
    fill = accent ? shift(fill, 16) : Theme::hover();
  } else if (default_ && !accent) {
    border = Theme::accent();
  }
  const int r = FromDIP(8);
  wxGCDC gc(dc);
  gc.SetPen(wxPen(border, 1));
  gc.SetBrush(wxBrush(fill));
  const wxRect rect = GetClientRect();
  gc.DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, r);
  gc.SetFont(GetFont().IsOk() ? GetFont() : Theme::ui());
  gc.SetTextForeground(fg);
  const wxSize sz = GetClientSize();
  const wxSize text = gc.GetTextExtent(GetLabel());
  gc.DrawText(GetLabel(), (sz.x - text.x) / 2, (sz.y - text.y) / 2);
}

RoundButton* make_button(wxWindow* parent, const wxString& label, wxWindowID id) {
  return new RoundButton(parent, id, label);
}

RoundButton* accent_button(wxWindow* parent, const wxString& label, wxWindowID id) {
  auto* btn = make_button(parent, label, id);
  btn->SetName(L"accent");
  return btn;
}

TabStrip::TabStrip(RoundedNotebook* owner)
    : wxPanel(owner, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), owner_(owner) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetMinSize(wxSize(-1, FromDIP(36)));
  timer_.SetOwner(this);
  Bind(wxEVT_PAINT, &TabStrip::on_paint, this);
  Bind(wxEVT_SIZE, &TabStrip::on_size, this);
  Bind(wxEVT_LEFT_DOWN, &TabStrip::on_mouse, this);
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
  const int pad = FromDIP(4);
  const int gap = FromDIP(6);
  const int hpad = FromDIP(12);
  const int th = FromDIP(28);
  int x = pad;
  int y = pad;
  for (std::size_t i = 0; i < n; ++i) {
    const wxSize text = GetTextExtent(owner_->GetPageText(i));
    const int tw = std::max(FromDIP(36), text.x + hpad * 2);
    if (x + tw > width - pad && x > pad) {
      x = pad;
      y += th + FromDIP(4);
    }
    rects_.push_back(wxRect(x, y, tw, th));
    x += tw + gap;
  }
}

int TabStrip::layout_height(int width) const {
  const int pad = FromDIP(4);
  const int th = FromDIP(28);
  if (owner_->GetPageCount() == 0) return th + pad * 2;
  TabStrip* self = const_cast<TabStrip*>(this);
  self->rebuild_layout(width > 0 ? width : FromDIP(400));
  if (rects_.empty()) return th + pad * 2;
  int bottom = 0;
  for (const auto& r : rects_) bottom = std::max(bottom, r.GetBottom());
  return bottom + pad + 2;
}

int TabStrip::hit_test(const wxPoint& pos) const {
  for (int i = 0; i < static_cast<int>(rects_.size()); ++i) {
    if (rects_[i].Contains(pos)) return i;
  }
  return -1;
}

void TabStrip::on_paint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  dc.SetBackground(wxBrush(GetParent() ? GetParent()->GetBackgroundColour() : Theme::bg()));
  dc.Clear();
  ensure_hover_size();
  if (rects_.size() != owner_->GetPageCount()) rebuild_layout(GetClientSize().GetWidth());
  wxGCDC gc(dc);
  gc.SetFont(Theme::ui());
  const int sel = owner_->GetSelection();
  for (std::size_t i = 0; i < rects_.size(); ++i) {
    const wxRect r = rects_[i];
    const float hov = i < hover_.size() ? hover_[i] : 0.f;
    const bool selected = static_cast<int>(i) == sel;
    wxColour fill = Theme::bg();
    if (selected) {
      fill = Theme::blend(Theme::select(), Theme::hover(), hov * 0.25f);
    } else {
      fill = Theme::blend(Theme::bg(), Theme::hover(), hov);
    }
    const wxColour border = selected ? Theme::accent() : Theme::blend(Theme::bg(), Theme::border(), 0.45f + 0.55f * hov);
    const int radius = r.height / 2;
    gc.SetPen(wxPen(border, selected ? 1 : 1));
    gc.SetBrush(wxBrush(fill));
    gc.DrawRoundedRectangle(r.x + 0.5, r.y + 0.5, r.width - 1, r.height - 1, radius);
    if (selected) {
      gc.SetPen(*wxTRANSPARENT_PEN);
      gc.SetBrush(wxBrush(Theme::accent()));
      const int bar_h = FromDIP(3);
      gc.DrawRoundedRectangle(r.x + FromDIP(10), r.GetBottom() - bar_h - 1, r.width - FromDIP(20), bar_h, bar_h / 2.0);
    }
    gc.SetTextForeground(selected ? Theme::text_bright() : Theme::blend(Theme::muted(), Theme::text(), 0.35f + 0.65f * hov));
    const wxString title = owner_->GetPageText(i);
    const wxSize text = gc.GetTextExtent(title);
    gc.DrawText(title, r.x + (r.width - text.x) / 2, r.y + (r.height - text.y) / 2);
  }
}

void TabStrip::on_size(wxSizeEvent& e) {
  rebuild_layout(GetClientSize().GetWidth());
  Refresh();
  e.Skip();
}

void TabStrip::on_mouse(wxMouseEvent& e) {
  const int hit = hit_test(e.GetPosition());
  if (hit != hover_index_) {
    hover_index_ = hit;
    if (!timer_.IsRunning()) timer_.Start(16);
    SetCursor(hit >= 0 ? wxCURSOR_HAND : wxCURSOR_ARROW);
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
  strip_ = new TabStrip(this);
  body_ = new RoundedCard(this, 10);
  auto* inner = new wxBoxSizer(wxVERTICAL);
  body_->SetSizer(inner);
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(strip_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
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

}  // namespace fatty

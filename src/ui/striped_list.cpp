#include "ui/striped_list.hpp"

#include "ui/theme.hpp"

#include <wx/dcbuffer.h>
#include <wx/sizer.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace fatty {
namespace {

wxColour row_bg(int row, bool selected) {
  if (selected) return Theme::select();
  return Theme::stripe(row);
}

}  // namespace

StripedListCtrl::Header::Header(StripedListCtrl* owner)
    : wxPanel(owner, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), owner_(owner) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetName(L"card-page");
  Bind(wxEVT_PAINT, &Header::on_paint, this);
  Bind(wxEVT_LEFT_DOWN, &Header::on_mouse, this);
  Bind(wxEVT_LEFT_UP, &Header::on_mouse, this);
  Bind(wxEVT_MOTION, &Header::on_mouse, this);
  Bind(wxEVT_LEAVE_WINDOW, &Header::on_mouse, this);
  Bind(wxEVT_MOUSE_CAPTURE_LOST, &Header::on_capture_lost, this);
}

wxSize StripedListCtrl::Header::DoGetBestSize() const { return {FromDIP(100), owner_->header_height()}; }

void StripedListCtrl::Header::on_paint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  dc.SetBackground(wxBrush(Theme::elevated()));
  dc.Clear();
  dc.SetFont(Theme::ui_small());
  dc.SetTextForeground(Theme::muted());
  const int h = GetClientSize().y;
  int x = 0;
  for (std::size_t i = 0; i < owner_->columns_.size(); ++i) {
    const auto& col = owner_->columns_[i];
    wxRect cell(x + FromDIP(6), 0, std::max(0, col.width - FromDIP(10)), h);
    dc.SetClippingRegion(cell);
    dc.DrawLabel(col.title, cell, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    dc.DestroyClippingRegion();
    x += col.width;
    dc.SetPen(wxPen(Theme::border()));
    dc.DrawLine(x - 1, FromDIP(4), x - 1, h - FromDIP(4));
  }
  if (moving_ && drop_before_ >= 0) {
    int mark = 0;
    const int n = static_cast<int>(owner_->columns_.size());
    for (int i = 0; i < drop_before_ && i < n; ++i) {
      mark += owner_->columns_[static_cast<std::size_t>(i)].width;
    }
    dc.SetPen(wxPen(Theme::accent(), FromDIP(2)));
    dc.DrawLine(mark, FromDIP(2), mark, h - FromDIP(2));
  }
  dc.SetPen(wxPen(Theme::border()));
  dc.DrawLine(0, h - 1, GetClientSize().x, h - 1);
}

int StripedListCtrl::Header::hit_split(int x) const {
  int pos = 0;
  const int grip = FromDIP(5);
  for (int i = 0; i < static_cast<int>(owner_->columns_.size()); ++i) {
    pos += owner_->columns_[static_cast<std::size_t>(i)].width;
    if (std::abs(x - pos) <= grip) return i;
  }
  return -1;
}

int StripedListCtrl::Header::hit_column(int x) const {
  int pos = 0;
  for (int i = 0; i < static_cast<int>(owner_->columns_.size()); ++i) {
    pos += owner_->columns_[static_cast<std::size_t>(i)].width;
    if (x < pos) return i;
  }
  return -1;
}

int StripedListCtrl::Header::drop_before_at(int x) const {
  int pos = 0;
  const int n = static_cast<int>(owner_->columns_.size());
  for (int i = 0; i < n; ++i) {
    const int w = owner_->columns_[static_cast<std::size_t>(i)].width;
    if (x < pos + w / 2) return i;
    pos += w;
  }
  return n;
}

void StripedListCtrl::Header::reset_drag() {
  if (HasCapture()) ReleaseMouse();
  drag_col_ = -1;
  press_col_ = -1;
  moving_ = false;
  drop_before_ = -1;
  SetCursor(wxCURSOR_ARROW);
  Refresh();
}

void StripedListCtrl::Header::on_capture_lost(wxMouseCaptureLostEvent&) { reset_drag(); }

void StripedListCtrl::Header::on_mouse(wxMouseEvent& e) {
  const int x = e.GetX();
  if (e.LeftDown()) {
    const int split = hit_split(x);
    if (split >= 0) {
      drag_col_ = split;
      drag_start_x_ = x;
      drag_start_w_ = owner_->columns_[static_cast<std::size_t>(split)].width;
      CaptureMouse();
      SetCursor(wxCURSOR_SIZEWE);
      return;
    }
    press_col_ = hit_column(x);
    press_x_ = x;
    moving_ = false;
    drop_before_ = -1;
    if (press_col_ >= 0) CaptureMouse();
    return;
  }
  if (e.LeftUp()) {
    if (HasCapture()) ReleaseMouse();
    if (drag_col_ >= 0) {
      owner_->emit_col_end_drag(drag_col_, -1);
      drag_col_ = -1;
      SetCursor(wxCURSOR_ARROW);
      return;
    }
    if (press_col_ >= 0) {
      if (moving_) {
        const int dest = drop_before_at(x);
        const int from = press_col_;
        if (dest != from && dest != from + 1) {
          owner_->MoveColumn(from, dest);
          owner_->emit_col_end_drag(from, dest);
        }
      } else {
        owner_->emit_col_click(press_col_);
      }
      press_col_ = -1;
      moving_ = false;
      drop_before_ = -1;
      SetCursor(wxCURSOR_ARROW);
      Refresh();
      return;
    }
    SetCursor(wxCURSOR_ARROW);
    return;
  }
  if (e.Dragging() && e.LeftIsDown()) {
    if (drag_col_ >= 0) {
      const int w = std::max(FromDIP(40), drag_start_w_ + (x - drag_start_x_));
      owner_->columns_[static_cast<std::size_t>(drag_col_)].width = w;
      Refresh();
      owner_->refresh_body();
      return;
    }
    if (press_col_ >= 0) {
      if (!moving_ && std::abs(x - press_x_) >= FromDIP(8)) {
        moving_ = true;
        SetCursor(wxCURSOR_HAND);
      }
      if (moving_) {
        drop_before_ = drop_before_at(x);
        Refresh();
      }
    }
    return;
  }
  if (e.Leaving() && drag_col_ < 0 && press_col_ < 0) {
    SetCursor(wxCURSOR_ARROW);
    return;
  }
  if (!e.LeftIsDown() && drag_col_ < 0 && press_col_ < 0) {
    SetCursor(hit_split(x) >= 0 ? wxCURSOR_SIZEWE : wxCURSOR_ARROW);
  }
}

StripedListCtrl::Body::Body(StripedListCtrl* owner)
    : wxVScrolledWindow(owner, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL),
      owner_(owner) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetName(L"card-page");
  SetRowCount(0);
  Bind(wxEVT_PAINT, &Body::on_paint, this);
  Bind(wxEVT_LEFT_DOWN, &Body::on_mouse, this);
  Bind(wxEVT_LEFT_DCLICK, &Body::on_dclick, this);
  Bind(wxEVT_KEY_DOWN, &Body::on_key, this);
  Bind(wxEVT_SIZE, &Body::on_size, this);
}

wxCoord StripedListCtrl::Body::OnGetRowHeight(size_t) const { return owner_->row_height(); }

int StripedListCtrl::Body::hit_row(int y) const {
  if (owner_->rows_.empty()) return -1;
  const int top = static_cast<int>(GetVisibleBegin());
  int acc = 0;
  for (int i = top; i < static_cast<int>(owner_->rows_.size()); ++i) {
    const int h = owner_->row_height();
    if (y >= acc && y < acc + h) return i;
    acc += h;
    if (acc > GetClientSize().y) break;
  }
  return -1;
}

void StripedListCtrl::Body::on_paint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  dc.SetBackground(wxBrush(Theme::elevated()));
  dc.Clear();
  if (owner_->rows_.empty()) return;
  dc.SetFont(Theme::ui());
  const int h = owner_->row_height();
  const int pad = FromDIP(6);
  const int first = static_cast<int>(GetVisibleBegin());
  const int last = static_cast<int>(GetVisibleEnd());
  for (int row = first; row < last && row < static_cast<int>(owner_->rows_.size()); ++row) {
    const auto& item = owner_->rows_[static_cast<std::size_t>(row)];
    const int y = (row - first) * h;
    wxRect row_rc(0, y, GetClientSize().x, h);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(row_bg(row, item.selected)));
    dc.DrawRectangle(row_rc);
    dc.SetTextForeground(item.text.IsOk() ? item.text : Theme::text());
    int x = 0;
    for (int col = 0; col < static_cast<int>(owner_->columns_.size()); ++col) {
      const int w = owner_->columns_[static_cast<std::size_t>(col)].width;
      wxRect cell(x + pad, y, std::max(0, w - pad * 2), h);
      wxString text;
      if (col < static_cast<int>(item.cells.size())) text = item.cells[static_cast<std::size_t>(col)];
      dc.SetClippingRegion(cell);
      dc.DrawLabel(text, cell, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
      dc.DestroyClippingRegion();
      x += w;
    }
  }
}

void StripedListCtrl::Body::on_mouse(wxMouseEvent& e) {
  SetFocus();
  const int row = hit_row(e.GetY());
  if (row < 0) return;
  if (owner_->single_sel_ || !(e.ControlDown() || e.ShiftDown())) {
    owner_->select_only(row, true);
  } else if (e.ShiftDown()) {
    owner_->select_range(owner_->anchor_ < 0 ? row : owner_->anchor_, row);
  } else {
    owner_->toggle_select(row);
  }
}

void StripedListCtrl::Body::on_dclick(wxMouseEvent& e) {
  const int row = hit_row(e.GetY());
  if (row >= 0) owner_->emit_activated(row);
}

void StripedListCtrl::Body::on_key(wxKeyEvent& e) {
  const int key = e.GetKeyCode();
  long cur = owner_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (key == WXK_UP || key == WXK_DOWN) {
    long next = cur;
    if (key == WXK_UP) next = std::max(0L, cur < 0 ? 0L : cur - 1);
    else next = std::min(static_cast<long>(owner_->rows_.size()) - 1, cur < 0 ? 0L : cur + 1);
    if (next >= 0 && !owner_->rows_.empty()) {
      owner_->select_only(next, true);
      ScrollToRow(static_cast<size_t>(next));
    }
    return;
  }
  if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER) {
    if (cur >= 0) owner_->emit_activated(cur);
    return;
  }
  e.Skip();
}

void StripedListCtrl::Body::on_size(wxSizeEvent& e) {
  Refresh();
  e.Skip();
}

StripedListCtrl::StripedListCtrl(wxWindow* parent, wxWindowID id, long style)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL),
      single_sel_((style & wxLC_SINGLE_SEL) != 0) {
  SetName(L"card-page");
  SetBackgroundColour(Theme::elevated());
  header_ = new Header(this);
  body_ = new Body(this);
  auto* sz = new wxBoxSizer(wxVERTICAL);
  sz->Add(header_, 0, wxEXPAND);
  sz->Add(body_, 1, wxEXPAND);
  SetSizer(sz);
}

void StripedListCtrl::restyle() {
  SetBackgroundColour(Theme::elevated());
  if (header_) header_->SetBackgroundColour(Theme::elevated());
  if (body_) body_->SetBackgroundColour(Theme::elevated());
  Refresh();
}

void StripedListCtrl::set_row_text(long row, const wxColour& text) {
  if (row < 0 || row >= GetItemCount()) return;
  rows_[static_cast<std::size_t>(row)].text = text.IsOk() ? text : Theme::text();
  refresh_body();
}

void StripedListCtrl::ensure_cells(Row& row) const {
  if (row.cells.size() < columns_.size()) row.cells.resize(columns_.size());
}

long StripedListCtrl::InsertItem(long index, const wxString& label) {
  if (index < 0) index = 0;
  if (index > GetItemCount()) index = GetItemCount();
  Row row;
  row.text = Theme::text();
  ensure_cells(row);
  if (!row.cells.empty()) row.cells[0] = label;
  rows_.insert(rows_.begin() + index, std::move(row));
  body_->SetRowCount(rows_.size());
  refresh_body();
  return index;
}

void StripedListCtrl::SetItem(long row, int col, const wxString& text) {
  if (row < 0 || row >= GetItemCount() || col < 0) return;
  ensure_cells(rows_[static_cast<std::size_t>(row)]);
  if (col >= static_cast<int>(rows_[static_cast<std::size_t>(row)].cells.size())) {
    rows_[static_cast<std::size_t>(row)].cells.resize(static_cast<std::size_t>(col) + 1);
  }
  rows_[static_cast<std::size_t>(row)].cells[static_cast<std::size_t>(col)] = text;
  refresh_body();
}

void StripedListCtrl::SetItemPtrData(long row, wxUIntPtr data) {
  if (row < 0 || row >= GetItemCount()) return;
  rows_[static_cast<std::size_t>(row)].data = data;
}

wxUIntPtr StripedListCtrl::GetItemData(long row) const {
  if (row < 0 || row >= GetItemCount()) return 0;
  return rows_[static_cast<std::size_t>(row)].data;
}

void StripedListCtrl::DeleteAllItems() {
  rows_.clear();
  anchor_ = -1;
  if (body_) body_->SetRowCount(0);
  refresh_body();
}

void StripedListCtrl::AppendColumn(const wxString& title, int, int width) {
  columns_.push_back({title, width > 0 ? width : FromDIP(80)});
  for (auto& row : rows_) ensure_cells(row);
  if (header_) header_->Refresh();
  refresh_body();
}

bool StripedListCtrl::DeleteColumn(int col) {
  if (col < 0 || col >= GetColumnCount()) return false;
  columns_.erase(columns_.begin() + col);
  for (auto& row : rows_) {
    if (col < static_cast<int>(row.cells.size())) {
      row.cells.erase(row.cells.begin() + col);
    }
  }
  if (header_) header_->Refresh();
  refresh_body();
  return true;
}

void StripedListCtrl::SetColumnWidth(int col, int width) {
  if (col < 0 || col >= GetColumnCount() || width <= 0) return;
  columns_[static_cast<std::size_t>(col)].width = width;
  if (header_) header_->Refresh();
  refresh_body();
}

int StripedListCtrl::GetColumnWidth(int col) const {
  if (col < 0 || col >= GetColumnCount()) return 0;
  return columns_[static_cast<std::size_t>(col)].width;
}

void StripedListCtrl::SetItemState(long row, long state, long mask) {
  if (row < 0 || row >= GetItemCount()) return;
  if (mask & wxLIST_STATE_SELECTED) {
    if (state & wxLIST_STATE_SELECTED) {
      if (single_sel_) select_only(row, false);
      else rows_[static_cast<std::size_t>(row)].selected = true;
    } else {
      rows_[static_cast<std::size_t>(row)].selected = false;
    }
    anchor_ = row;
    refresh_body();
  }
}

long StripedListCtrl::GetNextItem(long start, int, int state) const {
  const long from = start < 0 ? 0 : start + 1;
  for (long i = from; i < GetItemCount(); ++i) {
    if ((state & wxLIST_STATE_SELECTED) && rows_[static_cast<std::size_t>(i)].selected) return i;
    if (!(state & wxLIST_STATE_SELECTED)) return i;
  }
  return wxNOT_FOUND;
}

void StripedListCtrl::emit_selected(long row) {
  wxListEvent e(wxEVT_LIST_ITEM_SELECTED, GetId());
  e.SetEventObject(this);
  e.SetIndex(row);
  ProcessWindowEvent(e);
}

void StripedListCtrl::emit_activated(long row) {
  wxListEvent e(wxEVT_LIST_ITEM_ACTIVATED, GetId());
  e.SetEventObject(this);
  e.SetIndex(row);
  ProcessWindowEvent(e);
}

void StripedListCtrl::emit_col_click(int col) {
  wxListEvent e(wxEVT_LIST_COL_CLICK, GetId());
  e.SetEventObject(this);
  e.SetColumn(col);
  ProcessWindowEvent(e);
}

void StripedListCtrl::emit_col_end_drag(int from, int to_before) {
  wxListEvent e(wxEVT_LIST_COL_END_DRAG, GetId());
  e.SetEventObject(this);
  e.SetColumn(from);
  e.SetInt(to_before);
  ProcessWindowEvent(e);
}

void StripedListCtrl::MoveColumn(int from, int to_before) {
  const int n = GetColumnCount();
  if (from < 0 || from >= n) return;
  to_before = std::clamp(to_before, 0, n);
  if (to_before == from || to_before == from + 1) return;
  Column col = std::move(columns_[static_cast<std::size_t>(from)]);
  columns_.erase(columns_.begin() + from);
  int insert = to_before > from ? to_before - 1 : to_before;
  columns_.insert(columns_.begin() + insert, std::move(col));
  for (auto& row : rows_) {
    ensure_cells(row);
    if (from >= static_cast<int>(row.cells.size())) continue;
    wxString cell = std::move(row.cells[static_cast<std::size_t>(from)]);
    row.cells.erase(row.cells.begin() + from);
    if (insert > static_cast<int>(row.cells.size())) {
      row.cells.resize(static_cast<std::size_t>(insert));
    }
    row.cells.insert(row.cells.begin() + insert, std::move(cell));
    ensure_cells(row);
  }
  refresh_body();
}

void StripedListCtrl::select_only(long row, bool notify) {
  for (auto& item : rows_) item.selected = false;
  if (row >= 0 && row < GetItemCount()) {
    rows_[static_cast<std::size_t>(row)].selected = true;
    anchor_ = row;
  }
  refresh_body();
  if (notify && row >= 0) emit_selected(row);
}

void StripedListCtrl::toggle_select(long row) {
  if (row < 0 || row >= GetItemCount()) return;
  auto& item = rows_[static_cast<std::size_t>(row)];
  item.selected = !item.selected;
  anchor_ = row;
  refresh_body();
  if (item.selected) emit_selected(row);
}

void StripedListCtrl::select_range(long from, long to) {
  if (from > to) std::swap(from, to);
  from = std::max(0L, from);
  to = std::min(static_cast<long>(rows_.size()) - 1, to);
  for (auto& item : rows_) item.selected = false;
  for (long i = from; i <= to; ++i) rows_[static_cast<std::size_t>(i)].selected = true;
  refresh_body();
  if (to >= from) emit_selected(to);
}

int StripedListCtrl::row_height() const { return FromDIP(22); }
int StripedListCtrl::header_height() const { return FromDIP(24); }

void StripedListCtrl::refresh_body() {
  if (header_) header_->Refresh();
  if (body_) body_->Refresh();
}

}  // namespace fatty

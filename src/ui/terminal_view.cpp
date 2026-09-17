#include "ui/terminal_view.hpp"

#include "ui/theme.hpp"

#include <wx/clipbrd.h>
#include <wx/dcbuffer.h>

#include <algorithm>
#include <cstdlib>

namespace fatty {
namespace {

int decode_utf8(std::string& hold, unsigned char b, char32_t& out) {
  hold.push_back(static_cast<char>(b));
  const unsigned char c0 = static_cast<unsigned char>(hold[0]);
  int need = 1;
  if (c0 < 0x80)
    need = 1;
  else if ((c0 & 0xE0) == 0xC0)
    need = 2;
  else if ((c0 & 0xF0) == 0xE0)
    need = 3;
  else if ((c0 & 0xF8) == 0xF0)
    need = 4;
  else {
    hold.clear();
    out = U'\uFFFD';
    return 1;
  }
  if (static_cast<int>(hold.size()) < need) return 0;
  char32_t cp = 0;
  if (need == 1) {
    cp = c0;
  } else if (need == 2) {
    cp = (c0 & 0x1F) << 6 | (static_cast<unsigned char>(hold[1]) & 0x3F);
  } else if (need == 3) {
    cp = (c0 & 0x0F) << 12 | (static_cast<unsigned char>(hold[1]) & 0x3F) << 6 |
         (static_cast<unsigned char>(hold[2]) & 0x3F);
  } else {
    cp = (c0 & 0x07) << 18 | (static_cast<unsigned char>(hold[1]) & 0x3F) << 12 |
         (static_cast<unsigned char>(hold[2]) & 0x3F) << 6 | (static_cast<unsigned char>(hold[3]) & 0x3F);
  }
  hold.clear();
  out = cp;
  return 1;
}

std::vector<int> split_params(const std::string& s) {
  std::vector<int> out;
  if (s.empty()) {
    out.push_back(0);
    return out;
  }
  std::size_t i = 0;
  while (i <= s.size()) {
    std::size_t j = s.find(';', i);
    if (j == std::string::npos) j = s.size();
    if (j == i)
      out.push_back(0);
    else
      out.push_back(std::atoi(s.substr(i, j - i).c_str()));
    if (j == s.size()) break;
    i = j + 1;
  }
  return out;
}

wxColour ansi16(int idx, bool bold) {
  static const wxColour base[16] = {
      {0, 0, 0},       {205, 49, 49},  {13, 188, 121}, {229, 229, 16}, {36, 114, 200},  {188, 63, 188},
      {17, 168, 205},  {204, 204, 204}, {102, 102, 102}, {241, 76, 76}, {35, 209, 139}, {245, 245, 67},
      {59, 142, 234},  {214, 112, 214}, {41, 184, 219}, {229, 229, 229},
  };
  int i = idx & 15;
  if (bold && i < 8) i += 8;
  return base[i];
}

wxColour colour256(int idx) {
  if (idx < 16) return ansi16(idx, false);
  if (idx < 232) {
    idx -= 16;
    const int r = idx / 36;
    const int g = (idx / 6) % 6;
    const int b = idx % 6;
    auto v = [](int n) { return n == 0 ? 0 : 55 + n * 40; };
    return wxColour(v(r), v(g), v(b));
  }
  const int gray = 8 + (idx - 232) * 10;
  return wxColour(gray, gray, gray);
}

}  // namespace

TerminalView::TerminalView(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxBORDER_NONE) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetName(L"terminal");
  SetBackgroundColour(Theme::terminal());
  SetForegroundColour(Theme::text());
  SetFont(Theme::mono());
  SetCanFocus(true);
  Bind(wxEVT_PAINT, &TerminalView::on_paint, this);
  Bind(wxEVT_SIZE, &TerminalView::on_size, this);
  Bind(wxEVT_CHAR, &TerminalView::on_char, this);
  Bind(wxEVT_KEY_DOWN, &TerminalView::on_key_down, this);
  Bind(wxEVT_LEFT_DOWN, &TerminalView::on_mouse_down, this);
  blink_timer_.SetOwner(this);
  Bind(wxEVT_TIMER, &TerminalView::on_blink, this);
  blink_timer_.Start(530);
  ensure_grid();
}

wxSize TerminalView::DoGetBestSize() const { return FromDIP(wxSize(640, 240)); }

void TerminalView::reset() {
  state_ = ParseState::Ground;
  seq_.clear();
  utf8_hold_.clear();
  cur_fg_ = 7;
  cur_bg_ = 0;
  cur_attrs_ = 0;
  alt_active_ = false;
  scr_ = &primary_;
  scrollback_.clear();
  primary_ = Screen{};
  alt_ = Screen{};
  ensure_grid();
  Refresh();
}

void TerminalView::clear_screen() {
  erase_in_display(2);
  cup(1, 1);
  Refresh();
}

void TerminalView::feed(const std::string& bytes) {
  for (unsigned char b : bytes) parse_byte(b);
  Refresh();
}

void TerminalView::ensure_grid() {
  const int prev_c = grid_cols_ > 0 ? grid_cols_ : cols_;
  const int prev_r = grid_rows_ > 0 ? grid_rows_ : rows_;
  auto resize_scr = [&](Screen& s) {
    std::vector<Cell> next(static_cast<std::size_t>(rows_ * cols_));
    if (!s.cells.empty() && prev_c > 0 && prev_r > 0 &&
        static_cast<int>(s.cells.size()) == prev_c * prev_r) {
      for (int y = 0; y < std::min(rows_, prev_r); ++y) {
        for (int x = 0; x < std::min(cols_, prev_c); ++x) {
          next[static_cast<std::size_t>(y * cols_ + x)] = s.cells[static_cast<std::size_t>(y * prev_c + x)];
        }
      }
    }
    s.cells = std::move(next);
    s.cx = std::clamp(s.cx, 0, std::max(0, cols_ - 1));
    s.cy = std::clamp(s.cy, 0, std::max(0, rows_ - 1));
  };
  resize_scr(primary_);
  resize_scr(alt_);
  grid_cols_ = cols_;
  grid_rows_ = rows_;
}

TerminalView::Cell& TerminalView::at(Screen& scr, int x, int y) {
  x = std::clamp(x, 0, cols_ - 1);
  y = std::clamp(y, 0, rows_ - 1);
  return scr.cells[static_cast<std::size_t>(y * cols_ + x)];
}

void TerminalView::put_char(char32_t ch) {
  if (scr_->cx >= cols_) {
    scr_->cx = 0;
    newline();
  }
  auto& cell = at(*scr_, scr_->cx, scr_->cy);
  cell.ch = ch;
  cell.fg = cur_fg_;
  cell.bg = cur_bg_;
  cell.attrs = cur_attrs_;
  ++scr_->cx;
}

void TerminalView::newline() {
  ++scr_->cy;
  if (scr_->cy >= rows_) {
    scr_->cy = rows_ - 1;
    scroll_up(1);
  }
}

void TerminalView::scroll_up(int n) {
  if (n <= 0) return;
  if (!alt_active_) {
    for (int i = 0; i < n; ++i) {
      std::vector<Cell> row(static_cast<std::size_t>(cols_));
      for (int x = 0; x < cols_; ++x) row[static_cast<std::size_t>(x)] = at(*scr_, x, 0);
      scrollback_.push_back(std::move(row));
      while (static_cast<int>(scrollback_.size()) > kMaxScrollback) scrollback_.pop_front();
    }
  }
  for (int y = 0; y < rows_ - n; ++y) {
    for (int x = 0; x < cols_; ++x) at(*scr_, x, y) = at(*scr_, x, y + n);
  }
  for (int y = std::max(0, rows_ - n); y < rows_; ++y) {
    for (int x = 0; x < cols_; ++x) at(*scr_, x, y) = Cell{};
  }
}

void TerminalView::erase_in_display(int mode) {
  if (mode == 2 || mode == 3) {
    for (auto& c : scr_->cells) c = Cell{};
    if (mode == 3) scrollback_.clear();
    return;
  }
  if (mode == 0) {
    erase_in_line(0);
    for (int y = scr_->cy + 1; y < rows_; ++y)
      for (int x = 0; x < cols_; ++x) at(*scr_, x, y) = Cell{};
  } else if (mode == 1) {
    for (int y = 0; y < scr_->cy; ++y)
      for (int x = 0; x < cols_; ++x) at(*scr_, x, y) = Cell{};
    erase_in_line(1);
  }
}

void TerminalView::erase_in_line(int mode) {
  if (mode == 0) {
    for (int x = scr_->cx; x < cols_; ++x) at(*scr_, x, scr_->cy) = Cell{};
  } else if (mode == 1) {
    for (int x = 0; x <= scr_->cx; ++x) at(*scr_, x, scr_->cy) = Cell{};
  } else if (mode == 2) {
    for (int x = 0; x < cols_; ++x) at(*scr_, x, scr_->cy) = Cell{};
  }
}

void TerminalView::cup(int row, int col) {
  scr_->cy = std::clamp(row - 1, 0, rows_ - 1);
  scr_->cx = std::clamp(col - 1, 0, cols_ - 1);
}

void TerminalView::apply_sgr(const std::vector<int>& params) {
  for (std::size_t i = 0; i < params.size(); ++i) {
    const int p = params[i];
    if (p == 0) {
      cur_fg_ = 7;
      cur_bg_ = 0;
      cur_attrs_ = 0;
    } else if (p == 1) {
      cur_attrs_ |= 1;
    } else if (p == 22) {
      cur_attrs_ &= ~1;
    } else if (p == 39) {
      cur_fg_ = 7;
    } else if (p == 49) {
      cur_bg_ = 0;
    } else if (p >= 30 && p <= 37) {
      cur_fg_ = static_cast<uint8_t>(p - 30);
    } else if (p >= 90 && p <= 97) {
      cur_fg_ = static_cast<uint8_t>(p - 90 + 8);
    } else if (p >= 40 && p <= 47) {
      cur_bg_ = static_cast<uint8_t>(p - 40);
    } else if (p >= 100 && p <= 107) {
      cur_bg_ = static_cast<uint8_t>(p - 100 + 8);
    } else if (p == 38 || p == 48) {
      const bool is_fg = p == 38;
      if (i + 2 < params.size() && params[i + 1] == 5) {
        const int idx = std::clamp(params[i + 2], 0, 255);
        if (is_fg)
          cur_fg_ = static_cast<uint8_t>(idx);
        else
          cur_bg_ = static_cast<uint8_t>(idx);
        i += 2;
      } else if (i + 4 < params.size() && params[i + 1] == 2) {
        // Truecolor → nearest 256 cube (rough)
        const int r = params[i + 2], g = params[i + 3], b = params[i + 4];
        auto q = [](int v) { return v < 48 ? 0 : (v < 115 ? 1 : (v - 35) / 40); };
        const int idx = 16 + 36 * q(r) + 6 * q(g) + q(b);
        if (is_fg)
          cur_fg_ = static_cast<uint8_t>(idx);
        else
          cur_bg_ = static_cast<uint8_t>(idx);
        i += 4;
      }
    }
  }
}

void TerminalView::switch_alt(bool enable) {
  if (enable == alt_active_) return;
  alt_active_ = enable;
  scr_ = enable ? &alt_ : &primary_;
  if (enable) {
    for (auto& c : alt_.cells) c = Cell{};
    alt_.cx = alt_.cy = 0;
  }
}

void TerminalView::parse_byte(unsigned char b) {
  switch (state_) {
    case ParseState::Ground:
      if (b == 0x1B) {
        state_ = ParseState::Esc;
        seq_.clear();
        return;
      }
      if (b == '\n') {
        newline();
        return;
      }
      if (b == '\r') {
        scr_->cx = 0;
        return;
      }
      if (b == '\b') {
        if (scr_->cx > 0) --scr_->cx;
        return;
      }
      if (b == '\t') {
        scr_->cx = std::min(cols_ - 1, (scr_->cx / 8 + 1) * 8);
        return;
      }
      if (b == 0x07) return;  // BEL
      if (b < 0x20) return;
      {
        char32_t ch = 0;
        if (!decode_utf8(utf8_hold_, b, ch)) return;
        put_char(ch);
      }
      return;
    case ParseState::Esc:
      if (b == '[') {
        state_ = ParseState::Csi;
        seq_.clear();
        csi_priv_ = false;
        return;
      }
      if (b == ']') {
        state_ = ParseState::Osc;
        seq_.clear();
        return;
      }
      if (b == '7' || b == '8' || b == 'c') {
        // DECSC/DECRC/RIS — minimal
        if (b == 'c') reset();
        state_ = ParseState::Ground;
        return;
      }
      if (b == '(' || b == ')') {
        // charset designate — eat next
        seq_ = "x";
        return;
      }
      if (!seq_.empty() && seq_ == "x") {
        state_ = ParseState::Ground;
        seq_.clear();
        return;
      }
      state_ = ParseState::Ground;
      return;
    case ParseState::Csi:
      if (seq_.empty() && (b == '?' || b == '>')) {
        csi_priv_ = true;
        return;
      }
      if (b >= 0x40 && b <= 0x7E) {
        handle_csi(static_cast<char>(b), seq_, csi_priv_);
        state_ = ParseState::Ground;
        seq_.clear();
        return;
      }
      seq_.push_back(static_cast<char>(b));
      if (seq_.size() > 64) {
        state_ = ParseState::Ground;
        seq_.clear();
      }
      return;
    case ParseState::Osc:
      if (b == 0x07) {
        handle_osc(seq_);
        state_ = ParseState::Ground;
        seq_.clear();
        return;
      }
      if (b == 0x1B) {
        state_ = ParseState::OscEsc;
        return;
      }
      seq_.push_back(static_cast<char>(b));
      if (seq_.size() > 512) {
        state_ = ParseState::Ground;
        seq_.clear();
      }
      return;
    case ParseState::OscEsc:
      if (b == '\\') {
        handle_osc(seq_);
        state_ = ParseState::Ground;
        seq_.clear();
      } else {
        state_ = ParseState::Ground;
        seq_.clear();
      }
      return;
  }
}

void TerminalView::handle_osc(const std::string&) {
  // titles etc. — ignore
}

void TerminalView::handle_csi(char final_byte, const std::string& params, bool priv) {
  auto ps = split_params(params);
  const int p0 = ps.empty() ? 0 : ps[0];
  const int p1 = ps.size() > 1 ? ps[1] : 0;

  if (priv) {
    if (final_byte == 'h' || final_byte == 'l') {
      const bool set = final_byte == 'h';
      for (int p : ps) {
        if (p == 25) scr_->cursor_visible = set;
        if (p == 1049 || p == 47 || p == 1047) switch_alt(set);
      }
    }
    return;
  }

  switch (final_byte) {
    case 'A':
      scr_->cy = std::max(0, scr_->cy - std::max(1, p0 == 0 ? 1 : p0));
      break;
    case 'B':
      scr_->cy = std::min(rows_ - 1, scr_->cy + std::max(1, p0 == 0 ? 1 : p0));
      break;
    case 'C':
      scr_->cx = std::min(cols_ - 1, scr_->cx + std::max(1, p0 == 0 ? 1 : p0));
      break;
    case 'D':
      scr_->cx = std::max(0, scr_->cx - std::max(1, p0 == 0 ? 1 : p0));
      break;
    case 'H':
    case 'f':
      cup(p0 == 0 ? 1 : p0, p1 == 0 ? 1 : p1);
      break;
    case 'J':
      erase_in_display(p0);
      break;
    case 'K':
      erase_in_line(p0);
      break;
    case 'G':
      scr_->cx = std::clamp((p0 == 0 ? 1 : p0) - 1, 0, cols_ - 1);
      break;
    case 'd':
      scr_->cy = std::clamp((p0 == 0 ? 1 : p0) - 1, 0, rows_ - 1);
      break;
    case 'm':
      apply_sgr(ps);
      break;
    case 'n':
      if (p0 == 6 && write_cb_) {
        // CPR
        write_cb_("\x1b[" + std::to_string(scr_->cy + 1) + ";" + std::to_string(scr_->cx + 1) + "R");
      }
      break;
    case 'S':
      scroll_up(std::max(1, p0 == 0 ? 1 : p0));
      break;
    case '@': {  // insert blanks
      const int n = std::max(1, p0 == 0 ? 1 : p0);
      for (int x = cols_ - 1; x >= scr_->cx + n; --x) at(*scr_, x, scr_->cy) = at(*scr_, x - n, scr_->cy);
      for (int x = scr_->cx; x < std::min(cols_, scr_->cx + n); ++x) at(*scr_, x, scr_->cy) = Cell{};
      break;
    }
    case 'P': {  // delete chars
      const int n = std::max(1, p0 == 0 ? 1 : p0);
      for (int x = scr_->cx; x < cols_ - n; ++x) at(*scr_, x, scr_->cy) = at(*scr_, x + n, scr_->cy);
      for (int x = std::max(scr_->cx, cols_ - n); x < cols_; ++x) at(*scr_, x, scr_->cy) = Cell{};
      break;
    }
    default:
      break;
  }
}

wxColour TerminalView::fg_colour(uint8_t idx, bool bold) const {
  if (idx < 16) return ansi16(idx, bold);
  return colour256(idx);
}

wxColour TerminalView::bg_colour(uint8_t idx) const {
  if (idx < 16) return ansi16(idx, false);
  return colour256(idx);
}

void TerminalView::recompute_size() {
  wxClientDC dc(this);
  dc.SetFont(Theme::mono());
  const wxSize ext = dc.GetTextExtent(L"W");
  cell_w_ = std::max(1, ext.GetWidth());
  cell_h_ = std::max(1, ext.GetHeight());
  const wxSize sz = GetClientSize();
  const int nc = std::max(2, sz.GetWidth() / cell_w_);
  const int nr = std::max(2, sz.GetHeight() / cell_h_);
  if (nc != cols_ || nr != rows_) {
    cols_ = nc;
    rows_ = nr;
    ensure_grid();
  }
}

void TerminalView::emit_resize_if_needed() {
  if (cols_ == last_sent_cols_ && rows_ == last_sent_rows_) return;
  last_sent_cols_ = cols_;
  last_sent_rows_ = rows_;
  if (resize_cb_) resize_cb_(cols_, rows_);
}

void TerminalView::on_size(wxSizeEvent& e) {
  recompute_size();
  emit_resize_if_needed();
  Refresh();
  e.Skip();
}

void TerminalView::on_paint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  dc.SetFont(Theme::mono());
  dc.SetBackground(wxBrush(Theme::terminal()));
  dc.Clear();
  if (scr_->cells.empty()) return;

  for (int y = 0; y < rows_; ++y) {
    for (int x = 0; x < cols_; ++x) {
      const Cell& c = at(*scr_, x, y);
      const wxColour bg = c.bg == 0 ? Theme::terminal() : bg_colour(c.bg);
      const wxColour fg = fg_colour(c.fg, (c.attrs & 1) != 0);
      const int px = x * cell_w_;
      const int py = y * cell_h_;
      if (c.bg != 0 || c.ch != U' ') {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(bg));
        dc.DrawRectangle(px, py, cell_w_, cell_h_);
      }
      if (c.ch != U' ' && c.ch != 0) {
        dc.SetTextForeground(fg);
        wchar_t wbuf[3] = {};
        if (c.ch <= 0xFFFF) {
          wbuf[0] = static_cast<wchar_t>(c.ch);
        } else {
          const char32_t u = c.ch - 0x10000;
          wbuf[0] = static_cast<wchar_t>(0xD800 + (u >> 10));
          wbuf[1] = static_cast<wchar_t>(0xDC00 + (u & 0x3FF));
        }
        dc.DrawText(wxString(wbuf), px, py);
      }
    }
  }

  if (scr_->cursor_visible && focused_blink_ && HasFocus()) {
    const int px = scr_->cx * cell_w_;
    const int py = scr_->cy * cell_h_;
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(Theme::text()));
    dc.DrawRectangle(px, py + cell_h_ - 2, cell_w_, 2);
  }
}

void TerminalView::on_blink(wxTimerEvent&) {
  focused_blink_ = !focused_blink_;
  if (HasFocus()) RefreshRect(wxRect(scr_->cx * cell_w_, scr_->cy * cell_h_, cell_w_, cell_h_));
}

void TerminalView::on_mouse_down(wxMouseEvent& e) {
  SetFocus();
  e.Skip();
}

std::string TerminalView::key_to_seq(int key, int modifiers) const {
  const bool ctrl = (modifiers & wxMOD_CONTROL) != 0;
  const bool alt = (modifiers & wxMOD_ALT) != 0;
  auto wrap_alt = [alt](std::string s) {
    if (alt) return std::string("\x1b") + s;
    return s;
  };

  switch (key) {
    case WXK_UP:
      return wrap_alt("\x1b[A");
    case WXK_DOWN:
      return wrap_alt("\x1b[B");
    case WXK_RIGHT:
      return wrap_alt("\x1b[C");
    case WXK_LEFT:
      return wrap_alt("\x1b[D");
    case WXK_HOME:
      return wrap_alt("\x1b[H");
    case WXK_END:
      return wrap_alt("\x1b[F");
    case WXK_PAGEUP:
      return wrap_alt("\x1b[5~");
    case WXK_PAGEDOWN:
      return wrap_alt("\x1b[6~");
    case WXK_INSERT:
      return wrap_alt("\x1b[2~");
    case WXK_DELETE:
      return wrap_alt("\x1b[3~");
    case WXK_F1:
      return "\x1bOP";
    case WXK_F2:
      return "\x1bOQ";
    case WXK_F3:
      return "\x1bOR";
    case WXK_F4:
      return "\x1bOS";
    case WXK_F5:
      return "\x1b[15~";
    case WXK_F6:
      return "\x1b[17~";
    case WXK_F7:
      return "\x1b[18~";
    case WXK_F8:
      return "\x1b[19~";
    case WXK_F9:
      return "\x1b[20~";
    case WXK_F10:
      return "\x1b[21~";
    case WXK_F11:
      return "\x1b[23~";
    case WXK_F12:
      return "\x1b[24~";
    case WXK_BACK:
      return "\x7f";
    case WXK_TAB:
      return ctrl ? "\x1b[Z" : "\t";
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
      return "\r";
    case WXK_ESCAPE:
      return "\x1b";
    default:
      break;
  }
  if (ctrl && key >= 1 && key < 128) {
    // handled in CHAR often
  }
  return {};
}

void TerminalView::on_key_down(wxKeyEvent& e) {
  const int key = e.GetKeyCode();
  if (key == 'C' && e.ControlDown() && !e.AltDown() && !e.ShiftDown()) {
    // Ctrl+C → ETX to shell (not copy). Copy via select not implemented yet.
  }
  if (key == 'V' && e.ControlDown()) {
    if (wxTheClipboard->Open()) {
      if (wxTheClipboard->IsSupported(wxDF_UNICODETEXT)) {
        wxTextDataObject data;
        wxTheClipboard->GetData(data);
        const wxString t = data.GetText();
        if (write_cb_ && !t.empty()) write_cb_(std::string(t.utf8_string()));
      }
      wxTheClipboard->Close();
    }
    return;
  }
  const std::string seq = key_to_seq(key, e.GetModifiers());
  if (!seq.empty()) {
    if (write_cb_) write_cb_(seq);
    return;
  }
  e.Skip();
}

void TerminalView::on_char(wxKeyEvent& e) {
  const int uc = e.GetUnicodeKey();
  if (uc == WXK_NONE) {
    e.Skip();
    return;
  }
  // Special keys already handled in KEY_DOWN
  if (uc < 32 && uc != 8 && uc != 9 && uc != 13 && uc != 27) {
    if (write_cb_) write_cb_(std::string(1, static_cast<char>(uc)));
    return;
  }
  if (uc == 8 || uc == 9 || uc == 13 || uc == 27) {
    // prefer key_down sequences
    e.Skip();
    return;
  }
  if (write_cb_) {
    wxString s;
    s.Append(static_cast<wchar_t>(uc));
    write_cb_(std::string(s.utf8_string()));
  }
}

}  // namespace fatty

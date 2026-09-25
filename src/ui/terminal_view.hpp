#pragma once

#include <wx/panel.h>
#include <wx/timer.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace fatty {

// Простой xterm-подобный эмулятор: сетка ячеек, scrollback, CSI/SGR,
// альтернативный экран (htop/nano), ввод с клавиатуры → write_cb.
class TerminalView : public wxPanel {
 public:
  using WriteCb = std::function<void(const std::string&)>;

  explicit TerminalView(wxWindow* parent);

  void set_write_callback(WriteCb cb) { write_cb_ = std::move(cb); }
  void feed(const std::string& bytes, bool from_pty = false);
  // Следующий вывод PTY: спрятать эхо команды (может быть пустым) и перевод строки,
  // чтобы приглашение встало на текущую строку.
  void expect_prompt_after(const std::string& echoed_line);
  void clear_screen();
  void reset();
  bool alt_screen() const { return alt_active_; }

  int cols() const { return cols_; }
  int rows() const { return rows_; }

  using ResizeCb = std::function<void(int cols, int rows)>;
  void set_resize_callback(ResizeCb cb) { resize_cb_ = std::move(cb); }
  using CopyCb = std::function<void(const std::string&)>;
  void set_copy_callback(CopyCb cb) { copy_cb_ = std::move(cb); }

 protected:
  wxSize DoGetBestSize() const override;

 private:
  struct Cell {
    char32_t ch = U' ';
    uint8_t fg = 7;
    uint8_t bg = 0;
    uint8_t attrs = 0;  // bit0 = bold
  };

  struct Screen {
    std::vector<Cell> cells;  // rows * cols
    int cx = 0;
    int cy = 0;
    bool cursor_visible = true;
  };

  void on_paint(wxPaintEvent&);
  void on_size(wxSizeEvent&);
  void on_char(wxKeyEvent&);
  void on_key_down(wxKeyEvent&);
  void on_mouse_down(wxMouseEvent&);
  void on_mouse_move(wxMouseEvent&);
  void on_mouse_up(wxMouseEvent&);
  void on_mouse_wheel(wxMouseEvent&);
  void on_scroll(wxScrollWinEvent&);
  void on_blink(wxTimerEvent&);

  void ensure_grid();
  Cell& at(Screen& scr, int x, int y);
  void put_char(char32_t ch);
  void newline();
  void scroll_up(int n = 1);
  void scroll_view(int lines_up);
  void snap_to_bottom();
  void update_scrollbar();
  int max_view_offset() const;
  const Cell* view_cell(int x, int y) const;
  int cursor_view_row() const;
  struct Pos {
    int line = 0;
    int col = 0;
  };
  int history_line_at(int view_y) const;
  const Cell* history_cell(int line, int col) const;
  Pos pos_at(int px, int py) const;
  bool cell_selected(int line, int col) const;
  void copy_selection();
  void erase_in_display(int mode);
  void erase_in_line(int mode);
  void cup(int row, int col);
  void apply_sgr(const std::vector<int>& params);
  void parse_byte(unsigned char b);
  std::string take_swallowed(const std::string& in);
  void handle_csi(char final_byte, const std::string& params, bool priv);
  void handle_osc(const std::string& body);
  void switch_alt(bool enable);
  void recompute_size();
  void emit_resize_if_needed();
  wxColour fg_colour(uint8_t idx, bool bold) const;
  wxColour bg_colour(uint8_t idx) const;
  std::string key_to_seq(int key, int modifiers) const;

  WriteCb write_cb_;
  ResizeCb resize_cb_;
  CopyCb copy_cb_;
  Pos sel_a_{};
  Pos sel_b_{};
  bool sel_on_ = false;
  bool sel_drag_ = false;

  Screen primary_;
  Screen alt_;
  Screen* scr_ = &primary_;
  bool alt_active_ = false;
  std::deque<std::vector<Cell>> scrollback_;
  // Строк вверх от живого экрана. 0 — низ, новый вывод виден сразу.
  int view_offset_ = 0;
  Cell blank_{};
  static constexpr int kMaxScrollback = 5000;

  int cols_ = 80;
  int rows_ = 24;
  int grid_cols_ = 0;
  int grid_rows_ = 0;
  int cell_w_ = 8;
  int cell_h_ = 16;
  int last_sent_cols_ = 0;
  int last_sent_rows_ = 0;

  enum class ParseState { Ground, Esc, Csi, Osc, OscEsc };
  ParseState state_ = ParseState::Ground;
  std::string seq_;
  bool csi_priv_ = false;
  std::string utf8_hold_;

  uint8_t cur_fg_ = 7;
  uint8_t cur_bg_ = 0;
  uint8_t cur_attrs_ = 0;

  bool focused_blink_ = true;
  wxTimer blink_timer_;
  bool swallow_on_ = false;
  bool swallow_nl_ = false;
  std::string swallow_echo_;
  std::size_t swallow_i_ = 0;
};

}  // namespace fatty

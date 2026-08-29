#pragma once

#include <wx/colour.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/vscroll.h>
#include <vector>

namespace fatty {

// Список с полосами и цветом строк. Нативный wxListCtrl на Windows в тёмной
// теме игнорирует SetItemTextColour / фон — поэтому рисуем сами.
class StripedListCtrl : public wxPanel {
 public:
  StripedListCtrl(wxWindow* parent, wxWindowID id, long style);

  void restyle();
  void set_row_text(long row, const wxColour& text);

  long InsertItem(long index, const wxString& label);
  void SetItem(long row, int col, const wxString& text);
  void SetItemPtrData(long row, wxUIntPtr data);
  wxUIntPtr GetItemData(long row) const;
  void DeleteAllItems();
  int GetItemCount() const { return static_cast<int>(rows_.size()); }

  void AppendColumn(const wxString& title, int format, int width);
  bool DeleteColumn(int col);
  int GetColumnCount() const { return static_cast<int>(columns_.size()); }
  void SetColumnWidth(int col, int width);
  int GetColumnWidth(int col) const;

  void SetItemState(long row, long state, long mask);
  long GetNextItem(long start, int geometry, int state) const;

 private:
  class Body;
  friend class Body;

  struct Column {
    wxString title;
    int width = 80;
  };
  struct Row {
    std::vector<wxString> cells;
    wxColour text;
    wxUIntPtr data = 0;
    bool selected = false;
  };

  class Header : public wxPanel {
   public:
    explicit Header(StripedListCtrl* owner);
    wxSize DoGetBestSize() const override;

   private:
    void on_paint(wxPaintEvent&);
    void on_mouse(wxMouseEvent&);
    int hit_split(int x) const;

    StripedListCtrl* owner_;
    int drag_col_ = -1;
    int drag_start_x_ = 0;
    int drag_start_w_ = 0;
  };

  class Body : public wxVScrolledWindow {
   public:
    explicit Body(StripedListCtrl* owner);
    wxCoord OnGetRowHeight(size_t row) const override;

   private:
    void on_paint(wxPaintEvent&);
    void on_mouse(wxMouseEvent&);
    void on_dclick(wxMouseEvent&);
    void on_key(wxKeyEvent&);
    void on_size(wxSizeEvent&);
    int hit_row(int y) const;

    StripedListCtrl* owner_;
  };

  void emit_selected(long row);
  void emit_activated(long row);
  void emit_col_click(int col);
  void select_only(long row, bool notify);
  void toggle_select(long row);
  void select_range(long from, long to);
  void ensure_cells(Row& row) const;
  int row_height() const;
  int header_height() const;
  void refresh_body();

  std::vector<Column> columns_;
  std::vector<Row> rows_;
  bool single_sel_ = true;
  long anchor_ = -1;
  Header* header_{};
  Body* body_{};
};

}  // namespace fatty

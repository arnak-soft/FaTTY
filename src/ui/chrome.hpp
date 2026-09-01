#pragma once

#include <wx/control.h>
#include <wx/panel.h>
#include <wx/string.h>
#include <wx/timer.h>
#include <vector>

class wxBookCtrlEvent;
class wxCommandEvent;

namespace fatty {

wxDECLARE_EVENT(wxEVT_TAB_RIGHT_CLICK, wxCommandEvent);

class TabStrip;

void apply_rounded_region(wxWindow* window, int radius_px);

class RoundedCard : public wxPanel {
 public:
  explicit RoundedCard(wxWindow* parent, int radius_dip = 5);

 private:
  void on_paint(wxPaintEvent&);
  void on_size(wxSizeEvent&);
  int radius_dip_;
};

enum class BtnIcon {
  None = 0,
  Plus,
  Pencil,
  Copy,
  Trash,
  Folder,
  FolderPlus,
  FolderMove,
  Terminal,
  Putty,
  WinSCP,
  App,
  Network,
  ArrowUp,
  ArrowDown,
  Sort,
  List,
  Play,
  Stop,
  Home,
  Clear,
  Upload,
  Download,
  Save,
  Cancel,
  Refresh,
  Repeat,
  Export,
  Import,
  Key,
  Check,
  Insert,
};

class RoundButton : public wxControl {
 public:
  RoundButton(wxWindow* parent, wxWindowID id, const wxString& label, BtnIcon icon = BtnIcon::None);
  bool Enable(bool enable = true) override;
  void SetLabel(const wxString& label) override;
  void SetIcon(BtnIcon icon);
  void SetDefault();
  bool AcceptsFocus() const override { return IsShown() && IsEnabled(); }

 protected:
  wxSize DoGetBestSize() const override;

 private:
  void on_paint(wxPaintEvent&);
  void on_size(wxSizeEvent&);
  void fire();
  BtnIcon icon_ = BtnIcon::None;
  bool hovered_ = false;
  bool pressed_ = false;
  bool default_ = false;
};

RoundButton* make_button(wxWindow* parent, const wxString& label, wxWindowID id = wxID_ANY);
RoundButton* make_button(wxWindow* parent, const wxString& label, BtnIcon icon, wxWindowID id = wxID_ANY);
RoundButton* accent_button(wxWindow* parent, const wxString& label, wxWindowID id = wxID_ANY);
RoundButton* accent_button(wxWindow* parent, const wxString& label, BtnIcon icon, wxWindowID id = wxID_ANY);

class RoundedNotebook : public wxPanel {
 public:
  explicit RoundedNotebook(wxWindow* parent, wxWindowID id = wxID_ANY);

  bool AddPage(wxWindow* page, const wxString& text, bool select = false);
  bool DeletePage(std::size_t n);
  std::size_t GetPageCount() const { return pages_.size(); }
  wxWindow* GetPage(std::size_t n) const;
  wxString GetPageText(std::size_t n) const;
  int GetSelection() const { return selection_; }
  int SetSelection(int n);

 private:
  friend class TabStrip;
  struct Page {
    wxWindow* window = nullptr;
    wxString title;
  };

  void emit_changed(int old_sel, int new_sel);
  void relayout_body();

  TabStrip* strip_{};
  wxPanel* body_{};
  std::vector<Page> pages_;
  int selection_ = -1;
};

class TabStrip : public wxPanel {
 public:
  explicit TabStrip(RoundedNotebook* owner);
  void notify_pages_changed();

 protected:
  wxSize DoGetBestSize() const override;

 private:
  struct TabRect {
    wxRect rect;
  };
  void rebuild_layout(int width);
  int layout_height(int width) const;
  int hit_test(const wxPoint& pos) const;
  void on_paint(wxPaintEvent&);
  void on_size(wxSizeEvent&);
  void on_mouse(wxMouseEvent&);
  void on_leave(wxMouseEvent&);
  void on_timer(wxTimerEvent&);
  void ensure_hover_size();
  void emit_tab_right_click(int index);

  RoundedNotebook* owner_;
  std::vector<wxRect> rects_;
  std::vector<float> hover_;
  int hover_index_ = -1;
  wxTimer timer_;
};

}  // namespace fatty

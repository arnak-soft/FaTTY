#pragma once

#include <wx/arrstr.h>
#include <wx/control.h>
#include <wx/panel.h>
#include <wx/string.h>
#include <wx/timer.h>
#include <vector>

class wxMenu;

class wxBookCtrlEvent;
class wxCommandEvent;
class wxMouseCaptureLostEvent;
class wxMouseEvent;

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
  Pulse,
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
  void SetLabel(const wxString& label) override;
  void SetIcon(BtnIcon icon);
  void SetDefault();
  bool AcceptsFocus() const override { return IsShown() && IsEnabled(); }

 protected:
  wxSize DoGetBestSize() const override;
  void DoEnable(bool enable) override;

 private:
  void on_paint(wxPaintEvent&);
  void on_size(wxSizeEvent&);
  void on_mouse(wxMouseEvent&);
  void on_capture_lost(wxMouseCaptureLostEvent&);
  void fire();
  void fire_async();
  void sync_hover();
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

// Галочка в цветах темы. Пробел переключает, клик — по всей строке.
class ThemedCheckBox : public wxControl {
 public:
  ThemedCheckBox(wxWindow* parent, wxWindowID id, const wxString& label);
  void SetLabel(const wxString& label) override;
  void SetValue(bool on);
  bool GetValue() const { return value_; }
  bool AcceptsFocus() const override { return IsShown() && IsEnabled(); }

 protected:
  wxSize DoGetBestSize() const override;
  void DoEnable(bool enable) override;

 private:
  void toggle();
  void on_paint(wxPaintEvent&);
  bool value_ = false;
};

// Выпадающий список в цветах темы. Заменяет светлые wxChoice и wxComboBox.
class ThemedChoice : public wxControl {
 public:
  ThemedChoice(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, const wxArrayString& choices);
  ThemedChoice(wxWindow* parent, wxWindowID id, const wxString& value, const wxPoint& pos, const wxSize& size,
               const wxArrayString& choices);

  void Clear();
  int Append(const wxString& item);
  void SetSelection(int n);
  int GetSelection() const { return selection_; }
  wxString GetValue() const;
  int GetCount() const { return static_cast<int>(items_.size()); }
  bool AcceptsFocus() const override { return IsShown() && IsEnabled(); }

 protected:
  wxSize DoGetBestSize() const override;
  void DoEnable(bool enable) override;

 private:
  void init(const wxString& value, const wxArrayString& choices);
  void open_popup();
  void choose(int index, bool notify);
  void move_selection(int delta);
  void on_paint(wxPaintEvent&);

  std::vector<wxString> items_;
  int selection_ = -1;
  bool open_ = false;
};

// Полоса «Файл / Настройки / Справка» вместо системного меню.
class ThemedMenuBar : public wxPanel {
 public:
  explicit ThemedMenuBar(wxWindow* parent);
  ~ThemedMenuBar() override;
  void AddMenu(const wxString& title, wxMenu* menu);

 private:
  void on_paint(wxPaintEvent&);
  void on_mouse(wxMouseEvent&);
  void on_track_timer(wxTimerEvent&);
  void open_at(int index);
  int index_at(const wxPoint& client) const;

  struct Entry {
    wxString title;
    wxMenu* menu = nullptr;
    wxRect rect;
  };
  std::vector<Entry> entries_;
  wxTimer track_timer_;
  int hover_ = -1;
  int open_index_ = -1;
  bool swallow_up_ = false;
};

// Модальное меню в цветах темы. Событие wxEVT_MENU уходит в menu, затем в parent.
void show_themed_menu(wxWindow* parent, wxMenu* menu, const wxPoint& screen_pos = wxDefaultPosition);

}  // namespace fatty

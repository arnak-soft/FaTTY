#include "ui/health_window.hpp"

#include "core/util.hpp"
#include "ui/chrome.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/app.h>
#include <wx/graphics.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace fatty {
namespace {

wxColour health_colour(HealthLevel level) {
  switch (level) {
    case HealthLevel::Ok:
      return Theme::ok();
    case HealthLevel::Warn:
      return Theme::warn();
    case HealthLevel::Crit:
    case HealthLevel::Offline:
      return Theme::err();
    default:
      return Theme::muted();
  }
}

HealthLevel bar_level(double pct, int warn, int crit) { return level_from_pct(pct, warn, crit); }

void draw_h_bar(wxGraphicsContext* gfx, const wxRect& r, double pct, const wxColour& fill, const wxColour& track) {
  if (!gfx) return;
  gfx->SetAntialiasMode(wxANTIALIAS_DEFAULT);
  const double rad = std::min(6.0, r.height / 2.0);
  gfx->SetPen(wxNullPen);
  gfx->SetBrush(wxBrush(track));
  gfx->DrawRoundedRectangle(r.x, r.y, r.width, r.height, rad);
  if (pct < 0) return;
  const double w = std::max(rad * 2, r.width * std::clamp(pct, 0.0, 100.0) / 100.0);
  gfx->SetBrush(wxBrush(fill));
  gfx->DrawRoundedRectangle(r.x, r.y, w, r.height, rad);
}

class MetricBar : public wxPanel {
 public:
  MetricBar(wxWindow* parent, const wxString& title, int title_w_dip = 44)
      : wxPanel(parent, wxID_ANY), title_(title), title_w_dip_(title_w_dip) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &MetricBar::on_paint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
      Refresh();
      e.Skip();
    });
  }

  void set(double pct, const wxString& caption, HealthLevel level) {
    pct_ = pct;
    caption_ = caption;
    level_ = level;
    Refresh();
  }

 protected:
  wxSize DoGetBestSize() const override { return FromDIP(wxSize(220, 22)); }

 private:
  void on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(Theme::elevated()));
    dc.Clear();
    wxGCDC gc(dc);
    auto* gfx = gc.GetGraphicsContext();
    const wxSize sz = GetClientSize();
    const int title_w = FromDIP(title_w_dip_);
    gc.SetFont(Theme::ui_small());
    gc.SetTextForeground(Theme::muted());
    gc.DrawText(title_, FromDIP(2), (sz.y - gc.GetCharHeight()) / 2);
    gc.SetFont(Theme::ui_small());
    gc.SetTextForeground(Theme::text());
    const wxSize cap = gc.GetTextExtent(caption_);
    const int cap_x = sz.x - cap.x - FromDIP(4);
    gc.DrawText(caption_, cap_x, (sz.y - cap.y) / 2);
    const int bar_x = title_w;
    const int bar_w = std::max(FromDIP(40), cap_x - bar_x - FromDIP(8));
    const int bar_h = FromDIP(10);
    const int bar_y = (sz.y - bar_h) / 2;
    if (gfx) {
      draw_h_bar(gfx, wxRect(bar_x, bar_y, bar_w, bar_h), pct_, health_colour(level_), Theme::btn());
    }
  }

  wxString title_;
  int title_w_dip_ = 44;
  double pct_ = -1;
  wxString caption_ = L"—";
  HealthLevel level_ = HealthLevel::Unknown;
};

class DiskChart : public wxPanel {
 public:
  explicit DiskChart(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &DiskChart::on_paint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
      Refresh();
      e.Skip();
    });
  }

  struct Col {
    wxString name;
    double pct = -1;
    HealthLevel level = HealthLevel::Unknown;
    wxString caption;
  };

  void set_cols(std::vector<Col> cols) {
    cols_ = std::move(cols);
    Refresh();
  }

 protected:
  wxSize DoGetBestSize() const override { return FromDIP(wxSize(280, 160)); }

 private:
  void on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(Theme::elevated()));
    dc.Clear();
    wxGCDC gc(dc);
    auto* gfx = gc.GetGraphicsContext();
    const wxSize sz = GetClientSize();
    gc.SetFont(Theme::ui_small());
    gc.SetTextForeground(Theme::muted());
    gc.DrawText(L"Диски", FromDIP(4), FromDIP(2));
    if (cols_.empty()) {
      gc.SetFont(Theme::ui());
    gc.SetTextForeground(Theme::muted());
      gc.DrawText(L"нет данных", FromDIP(8), sz.y / 2 - FromDIP(8));
      return;
    }
    const int top = FromDIP(22);
    const int bottom = sz.y - FromDIP(28);
    const int h = std::max(FromDIP(40), bottom - top);
    const int n = static_cast<int>(cols_.size());
    const int gap = FromDIP(8);
    const int col_w = std::max(FromDIP(18), (sz.x - FromDIP(16) - gap * (n + 1)) / n);
    for (int i = 0; i < n; ++i) {
      const auto& c = cols_[static_cast<std::size_t>(i)];
      const int x = FromDIP(8) + i * (col_w + gap);
      if (gfx) {
        gfx->SetPen(wxNullPen);
        gfx->SetBrush(wxBrush(Theme::btn()));
        gfx->DrawRoundedRectangle(x, top, col_w, h, 4);
        if (c.pct >= 0) {
          const double fh = h * std::clamp(c.pct, 0.0, 100.0) / 100.0;
          gfx->SetBrush(wxBrush(health_colour(c.level)));
          gfx->DrawRoundedRectangle(x, top + (h - fh), col_w, fh, 4);
        }
      }
      gc.SetFont(Theme::ui_small());
    gc.SetTextForeground(Theme::text());
      auto name = c.name;
      if (name.size() > 10) name = name.Left(9) + L"…";
      const wxSize ns = gc.GetTextExtent(name);
      gc.DrawText(name, x + (col_w - ns.x) / 2, bottom + FromDIP(2));
      gc.SetTextForeground(health_colour(c.level));
      const wxSize ps = gc.GetTextExtent(c.caption);
      gc.DrawText(c.caption, x + (col_w - ps.x) / 2, top - FromDIP(2) + (c.pct < 0 ? h / 2 : 0));
    }
  }

  std::vector<Col> cols_;
};

class LoadChart : public wxPanel {
 public:
  explicit LoadChart(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &LoadChart::on_paint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
      Refresh();
      e.Skip();
    });
  }

  void set_load(int nproc, double l1, double l5, double l15) {
    nproc_ = nproc;
    l1_ = l1;
    l5_ = l5;
    l15_ = l15;
    Refresh();
  }

 protected:
  wxSize DoGetBestSize() const override { return FromDIP(wxSize(220, 120)); }

 private:
  void bar(wxGraphicsContext* gfx, wxGCDC& gc, int x, int w, int top, int h, double value, const wxString& label) {
    const double scale = nproc_ > 0 ? static_cast<double>(nproc_) : 1.0;
    const double pct = value < 0 ? -1 : 100.0 * value / scale;
    HealthLevel lvl = HealthLevel::Unknown;
    if (pct >= 0) {
      if (pct >= 150) lvl = HealthLevel::Crit;
      else if (pct >= 100) lvl = HealthLevel::Warn;
      else lvl = HealthLevel::Ok;
    }
    if (gfx) {
      gfx->SetPen(wxNullPen);
      gfx->SetBrush(wxBrush(Theme::btn()));
      gfx->DrawRoundedRectangle(x, top, w, h, 4);
      if (pct >= 0) {
        const double fh = h * std::clamp(pct, 0.0, 150.0) / 150.0;
        gfx->SetBrush(wxBrush(health_colour(lvl)));
        gfx->DrawRoundedRectangle(x, top + (h - fh), w, fh, 4);
      }
    }
    gc.SetFont(Theme::ui_small());
    gc.SetTextForeground(Theme::muted());
    const wxSize ls = gc.GetTextExtent(label);
    gc.DrawText(label, x + (w - ls.x) / 2, top + h + FromDIP(2));
    wxString num = L"—";
    if (value >= 0) {
      num = wxString::Format(L"%.2f", value);
    }
    gc.SetFont(Theme::ui_small());
    gc.SetTextForeground(Theme::text());
    const wxSize ns = gc.GetTextExtent(num);
    gc.DrawText(num, x + (w - ns.x) / 2, top - FromDIP(16));
  }

  void on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(Theme::elevated()));
    dc.Clear();
    wxGCDC gc(dc);
    auto* gfx = gc.GetGraphicsContext();
    const wxSize sz = GetClientSize();
    gc.SetFont(Theme::ui_small());
    gc.SetTextForeground(Theme::muted());
    wxString head = L"Нагрузка";
    if (nproc_ > 0) head += wxString::Format(L"  •  %d CPU", nproc_);
    gc.DrawText(head, FromDIP(4), FromDIP(2));
    const int top = FromDIP(28);
    const int h = std::max(FromDIP(36), sz.y - FromDIP(52));
    const int w = FromDIP(36);
    const int gap = (sz.x - FromDIP(16) - 3 * w) / 4;
    bar(gfx, gc, FromDIP(8) + gap, w, top, h, l1_, L"1 мин");
    bar(gfx, gc, FromDIP(8) + 2 * gap + w, w, top, h, l5_, L"5 мин");
    bar(gfx, gc, FromDIP(8) + 3 * gap + 2 * w, w, top, h, l15_, L"15 мин");
  }

  int nproc_ = 0;
  double l1_ = -1;
  double l5_ = -1;
  double l15_ = -1;
};

class HostCard : public wxPanel {
 public:
  HostCard(wxWindow* parent, HealthWindow* owner) : wxPanel(parent), owner_(owner) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &HostCard::on_paint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
      Refresh();
      e.Skip();
    });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
      if (owner_) owner_->select_server(id_);
    });
  }

  void set(const Server& server, const HealthSnapshot& snap, bool selected, const AppSettings& st) {
    id_ = server.id;
    name_ = wxString::FromUTF8(server.name.empty() ? server.host : server.name);
    host_ = wxString::FromUTF8(server.username + "@" + server.host);
    snap_ = snap;
    selected_ = selected;
    show_cpu_ = st.health_show_cpu;
    show_ram_ = st.health_show_ram;
    show_disk_ = st.health_show_disk;
    disk_warn_ = st.health_disk_warn;
    disk_crit_ = st.health_disk_crit;
    ram_warn_ = st.health_ram_warn;
    ram_crit_ = st.health_ram_crit;
    cpu_warn_ = st.health_cpu_warn;
    cpu_crit_ = st.health_cpu_crit;
    enabled_ = server.health_enabled;
    SetToolTip(host_);
    Refresh();
  }

  const std::string& id() const { return id_; }

 protected:
  wxSize DoGetBestSize() const override { return FromDIP(wxSize(260, 118)); }

 private:
  void mini_bar(wxGraphicsContext* gfx, wxGCDC& gc, int x, int y, int w, const wxString& label, double pct,
                HealthLevel level) {
    gc.SetFont(Theme::ui_small());
    gc.SetTextForeground(Theme::muted());
    gc.DrawText(label, x, y - FromDIP(2));
    if (gfx) draw_h_bar(gfx, wxRect(x + FromDIP(36), y + FromDIP(2), w - FromDIP(36), FromDIP(9)), pct,
                        health_colour(level), Theme::btn());
  }

  void on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    const wxColour bg = selected_ ? Theme::select() : Theme::elevated();
    dc.SetBackground(wxBrush(bg));
    dc.Clear();
    wxGCDC gc(dc);
    auto* gfx = gc.GetGraphicsContext();
    const wxSize sz = GetClientSize();
    const auto accent = health_colour(snap_.checking ? HealthLevel::Checking : snap_.level);
    if (gfx) {
      gfx->SetPen(wxNullPen);
      gfx->SetBrush(wxBrush(accent));
      gfx->DrawRoundedRectangle(0, FromDIP(6), FromDIP(4), sz.y - FromDIP(12), 2);
      gfx->SetPen(wxPen(selected_ ? Theme::accent() : Theme::border(), 1));
      gfx->SetBrush(wxNullBrush);
      gfx->DrawRoundedRectangle(0.5, 0.5, sz.x - 1.0, sz.y - 1.0, FromDIP(5));
    }
    gc.SetFont(Theme::ui());
    gc.SetTextForeground(Theme::text_bright());
    gc.DrawText(name_, FromDIP(14), FromDIP(8));
    gc.SetFont(Theme::ui_small());
    gc.SetTextForeground(health_colour(snap_.level));
    gc.DrawText(wxString::FromUTF8(health_level_label(snap_.level)), FromDIP(14), FromDIP(28));
    gc.SetFont(Theme::ui_small());
    gc.SetTextForeground(Theme::muted());
    gc.DrawText(wxString::FromUTF8(format_health_when(snap_.checked_at)), FromDIP(14), sz.y - FromDIP(20));
    if (!enabled_) {
      gc.DrawText(L"выключен", sz.x - FromDIP(80), FromDIP(10));
    }
    int y = FromDIP(48);
    const int x = FromDIP(14);
    const int w = sz.x - FromDIP(24);
    if (show_cpu_) {
      mini_bar(gfx, gc, x, y, w, L"CPU", snap_.cpu_pct, bar_level(snap_.cpu_pct, cpu_warn_, cpu_crit_));
      y += FromDIP(16);
    }
    if (show_ram_) {
      mini_bar(gfx, gc, x, y, w, L"RAM", snap_.mem_pct, bar_level(snap_.mem_pct, ram_warn_, ram_crit_));
      y += FromDIP(16);
    }
    if (show_disk_) {
      double dp = -1;
      if (auto* d = health_root_or_worst(snap_)) dp = d->pct();
      mini_bar(gfx, gc, x, y, w, L"Диск", dp, bar_level(dp, disk_warn_, disk_crit_));
    }
  }

  HealthWindow* owner_ = nullptr;
  std::string id_;
  wxString name_;
  wxString host_;
  HealthSnapshot snap_;
  bool selected_ = false;
  bool enabled_ = true;
  bool show_cpu_ = true;
  bool show_ram_ = true;
  bool show_disk_ = true;
  int disk_warn_ = 80;
  int disk_crit_ = 90;
  int ram_warn_ = 80;
  int ram_crit_ = 90;
  int cpu_warn_ = 80;
  int cpu_crit_ = 95;
};

}  // namespace

HealthWindow::HealthWindow(wxWindow* parent, HealthMonitor* monitor, Config* config,
                           std::function<void(const std::string&)> on_refresh, std::function<void()> on_refresh_all,
                           AppSettings* settings, std::function<void()> persist)
    : wxFrame(parent, wxID_ANY, L"Состояние VPS", wxDefaultPosition, wxDefaultSize),
      monitor_(monitor),
      config_(config),
      settings_(settings),
      on_refresh_(std::move(on_refresh)),
      on_refresh_all_(std::move(on_refresh_all)) {
  set_icon(this);
  const bool had_geometry = settings && settings->dialog_geometry.count("health");
  SetSize(FromDIP(wxSize(980, 620)));
  setup_frame_geometry(this, settings, "health", true, std::move(persist));
  if (!had_geometry) CentreOnParent();

  auto* panel = new wxPanel(this);
  auto* refresh_sel = make_button(panel, L"Обновить выбранный", BtnIcon::Refresh);
  auto* refresh_all = make_button(panel, L"Обновить все", BtnIcon::Repeat);
  auto_label_ = new wxStaticText(panel, wxID_ANY, L"");
  auto_label_->SetName(L"muted");
  auto_label_->SetForegroundColour(Theme::muted());

  auto* split = new wxSplitterWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
  list_ = new wxScrolledWindow(split, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxBORDER_NONE);
  list_->SetScrollRate(0, 16);
  list_sz_ = new wxBoxSizer(wxVERTICAL);
  list_->SetSizer(list_sz_);
  auto* detail_card = new RoundedCard(split);
  detail_ = new wxPanel(detail_card);
  detail_->SetName(L"card-page");
  auto* dsz = new wxBoxSizer(wxVERTICAL);
  dsz->Add(detail_, 1, wxEXPAND);
  detail_card->SetSizer(dsz);
  split->SplitVertically(list_, detail_card, FromDIP(300));
  split->SetMinimumPaneSize(FromDIP(220));

  status_ = new wxStaticText(panel, wxID_ANY, L"");
  status_->SetName(L"muted");
  status_->SetForegroundColour(Theme::muted());

  auto* top = new wxBoxSizer(wxHORIZONTAL);
  top->Add(refresh_sel, 0, wxRIGHT, 8);
  top->Add(refresh_all, 0, wxRIGHT, 12);
  top->Add(auto_label_, 1, wxALIGN_CENTER_VERTICAL);

  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(top, 0, wxEXPAND | wxALL, 8);
  root->Add(split, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
  root->Add(status_, 0, wxEXPAND | wxALL, 8);
  panel->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(panel, 1, wxEXPAND);
  SetSizer(outer);
  apply_dark(this);

  refresh_sel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto id = selected_id();
    if (id.empty() || !on_refresh_) return;
    on_refresh_(id);
  });
  refresh_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (on_refresh_all_) on_refresh_all_();
  });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
    alive_->store(false);
    e.Skip();
  });
  bind_escape_close(this);
  reload();
}

HealthWindow::~HealthWindow() { alive_->store(false); }

void HealthWindow::reload() {
  if (!config_ || !monitor_) return;
  if (selected_id_.empty() && !config_->servers.empty()) selected_id_ = config_->servers.front().id;
  rebuild_cards();
  show_detail();
  wxString auto_text;
  if (settings_ && settings_->health_auto) {
    auto_text = wxString::FromUTF8("Автопроверка: каждые " + format_interval_label(settings_->health_interval_sec));
  } else {
    auto_text = L"Автопроверка выключена — обновляйте вручную (Настройки → Состояние)";
  }
  auto_label_->SetLabel(auto_text);
  const auto checking = monitor_->checking_id();
  if (!checking.empty()) {
    auto* s = config_->server_by_id(checking);
    status_->SetLabel(L"Проверяю «" + wxString::FromUTF8(s ? s->name : checking) + L"»…");
  } else {
    status_->SetLabel(L"Последние замеры хранятся локально. Цвет шкалы — заполнение, не тревога.");
  }
}

void HealthWindow::select_server(const std::string& id) {
  selected_id_ = id;
  auto alive = alive_;
  wxTheApp->CallAfter([this, alive] {
    if (!alive->load()) return;
    rebuild_cards();
    show_detail();
  });
}

std::string HealthWindow::selected_id() const { return selected_id_; }

void HealthWindow::rebuild_cards() {
  if (!list_ || !list_sz_ || !config_) return;
  list_sz_->Clear(true);
  const auto st = settings_ ? *settings_ : AppSettings{};
  for (const auto& server : config_->servers) {
    auto* card = new HostCard(list_, this);
    auto snap = monitor_->snapshot(server.id);
    snap.server_id = server.id;
    snap.server_name = server.name;
    if (monitor_->checking_id() == server.id) {
      snap.checking = true;
      snap.level = HealthLevel::Checking;
    }
    card->set(server, snap, server.id == selected_id_, st);
    list_sz_->Add(card, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
  }
  list_->FitInside();
  list_->Layout();
}

void HealthWindow::show_detail() {
  if (!detail_ || !config_) return;
  detail_->DestroyChildren();
  auto* root = new wxBoxSizer(wxVERTICAL);
  auto* s = config_->server_by_id(selected_id_);
  if (!s) {
    auto* empty = new wxStaticText(detail_, wxID_ANY, L"Выберите VPS слева.");
    empty->SetForegroundColour(Theme::muted());
    root->Add(empty, 0, wxALL, 12);
    detail_->SetSizer(root);
    detail_->Layout();
    return;
  }
  auto snap = monitor_->snapshot(s->id);
  if (monitor_->checking_id() == s->id) {
    snap.checking = true;
    snap.level = HealthLevel::Checking;
  }
  const auto st = settings_ ? *settings_ : AppSettings{};

  auto* title = new wxStaticText(detail_, wxID_ANY, wxString::FromUTF8(s->name));
  title->SetFont(Theme::ui_title());
  title->SetForegroundColour(Theme::text_bright());
  auto* sub = new wxStaticText(
      detail_, wxID_ANY,
      wxString::FromUTF8(s->username + "@" + s->host + ":" + std::to_string(s->port) + "  •  " +
                         health_level_label(snap.level) + "  •  " + format_health_when(snap.checked_at)));
  sub->SetForegroundColour(health_colour(snap.level));

  if (st.health_show_cpu) {
    auto* cpu = new MetricBar(detail_, L"CPU", 52);
    wxString cap = wxString::FromUTF8(format_pct(snap.cpu_pct));
    cpu->set(snap.cpu_pct, cap, bar_level(snap.cpu_pct, st.health_cpu_warn, st.health_cpu_crit));
    root->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    root->Add(sub, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
    root->Add(cpu, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
  } else {
    root->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    root->Add(sub, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  }
  if (st.health_show_ram) {
    auto* ram = new MetricBar(detail_, L"RAM", 52);
    wxString cap = L"—";
    if (snap.mem_pct >= 0) {
      cap = wxString::FromUTF8(format_pct(snap.mem_pct) + "  " + format_kib(snap.mem_total_kb - snap.mem_avail_kb) +
                               " / " + format_kib(snap.mem_total_kb));
    }
    ram->set(snap.mem_pct, cap, bar_level(snap.mem_pct, st.health_ram_warn, st.health_ram_crit));
    root->Add(ram, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
  }
  if (st.health_show_disk) {
    auto* disks = new DiskChart(detail_);
    std::vector<DiskChart::Col> cols;
    for (const auto& d : snap.disks) {
      DiskChart::Col c;
      c.name = wxString::FromUTF8(d.mount);
      c.pct = d.pct();
      c.level = bar_level(c.pct, st.health_disk_warn, st.health_disk_crit);
      c.caption = wxString::FromUTF8(format_pct(c.pct));
      cols.push_back(std::move(c));
    }
    disks->set_cols(std::move(cols));
    root->Add(disks, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
  }
  if (st.health_show_load) {
    auto* load = new LoadChart(detail_);
    load->set_load(snap.nproc, snap.load1, snap.load5, snap.load15);
    root->Add(load, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
  }
  auto* meta = new wxStaticText(detail_, wxID_ANY,
                                wxString::FromUTF8("Аптайм: " + format_uptime_sec(snap.uptime_sec)));
  meta->SetForegroundColour(Theme::muted());
  root->Add(meta, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  if (!snap.error.empty() && snap.level != HealthLevel::Ok) {
    auto* err = new wxStaticText(detail_, wxID_ANY, wxString::FromUTF8(snap.error));
    err->SetForegroundColour(Theme::err());
    err->Wrap(FromDIP(420));
    root->Add(err, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  }
  if (!s->health_enabled) {
    auto* off = new wxStaticText(detail_, wxID_ANY, L"Этот VPS исключён из проверки (карточка сервера).");
    off->SetForegroundColour(Theme::muted());
    root->Add(off, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  }
  detail_->SetSizer(root);
  apply_dark(detail_);
  detail_->Layout();
  if (auto* p = detail_->GetParent()) p->Layout();
}

}  // namespace fatty

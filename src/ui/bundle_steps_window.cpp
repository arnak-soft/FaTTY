#include "ui/bundle_steps_window.hpp"

#include "core/store.hpp"
#include "ui/layout.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace fatty {
namespace {

wxString folder_cell(const Config& config, const Command& c) {
  return wxString::FromUTF8(command_display_folder(config, c));
}

wxStaticText* muted_label(wxWindow* parent, const wxString& text) {
  auto* t = new wxStaticText(parent, wxID_ANY, text);
  t->SetName(L"muted");
  t->SetForegroundColour(Theme::muted());
  return t;
}

}  // namespace

BundleStepsWindow::BundleStepsWindow(wxWindow* parent, Config& config,
                                     const std::map<std::string, CommandRunStats>* command_stats,
                                     std::function<void(const Command&)> on_run,
                                     std::function<void(const std::string& bundle_id)> on_run_bundle,
                                     AppSettings* settings, std::function<void()> persist)
    : wxFrame(parent, wxID_ANY, L"Связка — по шагам", wxDefaultPosition, wxDefaultSize),
      config_(config),
      command_stats_(command_stats),
      on_run_(std::move(on_run)),
      on_run_bundle_(std::move(on_run_bundle)) {
  set_icon(this);
  const bool had_geometry = settings && settings->dialog_geometry.count("bundle_steps");
  SetSize(FromDIP(wxSize(920, 560)));
  setup_frame_geometry(this, settings, "bundle_steps", true, std::move(persist));
  if (!had_geometry) CentreOnParent();
  auto* panel = new wxPanel(this);
  accent_bar_ = new wxPanel(panel, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 4)));
  accent_bar_->SetBackgroundColour(Theme::accent());
  hint_ = new wxStaticText(
      panel, wxID_ANY,
      L"Запускайте выбранный шаг, когда будете готовы. Это шпаргалка сценария: шаги можно пропускать, повторять и менять порядок кликом.");
  hint_->SetName(L"muted");
  hint_->SetForegroundColour(Theme::muted());
  hint_->Wrap(FromDIP(740));
  auto* run_bundle_btn = accent_button(panel, L"Запустить связку", BtnIcon::Play);
  auto* run = accent_button(panel, L"Запустить шаг  (F5)", BtnIcon::Play);
  auto* list_card = new RoundedCard(panel);
  list_ = new StripedListCtrl(list_card, wxID_ANY, wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
  list_->AppendColumn(L"#", wxLIST_FORMAT_LEFT, FromDIP(36));
  list_->AppendColumn(L"Название", wxLIST_FORMAT_LEFT, FromDIP(130));
  list_->AppendColumn(L"Группа", wxLIST_FORMAT_LEFT, FromDIP(90));
  list_->AppendColumn(L"Каталог", wxLIST_FORMAT_LEFT, FromDIP(120));
  list_->AppendColumn(L"Команда", wxLIST_FORMAT_LEFT, FromDIP(180));
  list_->AppendColumn(L"Комментарий", wxLIST_FORMAT_LEFT, FromDIP(120));
  list_->AppendColumn(L"Последний раз", wxLIST_FORMAT_LEFT, FromDIP(130));
  list_->AppendColumn(L"Ср. время", wxLIST_FORMAT_LEFT, FromDIP(90));
  auto* list_sz = new wxBoxSizer(wxVERTICAL);
  list_sz->Add(list_, 1, wxEXPAND);
  list_card->SetSizer(list_sz);

  auto* info_card = new RoundedCard(panel);
  detail_title_ = new wxStaticText(info_card, wxID_ANY, L"");
  detail_title_->SetName(L"heading");
  detail_title_->SetFont(Theme::ui_heading());
  detail_title_->SetForegroundColour(Theme::text_bright());

  auto* meta = new wxFlexGridSizer(2, FromDIP(8), FromDIP(16));
  meta->AddGrowableCol(1, 1);
  meta->Add(muted_label(info_card, L"Группа"), 0, wxALIGN_CENTER_VERTICAL);
  detail_group_ = new wxStaticText(info_card, wxID_ANY, L"—");
  meta->Add(detail_group_, 1, wxALIGN_CENTER_VERTICAL);
  meta->Add(muted_label(info_card, L"Каталог"), 0, wxALIGN_CENTER_VERTICAL);
  detail_folder_ = new wxStaticText(info_card, wxID_ANY, L"—", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
  meta->Add(detail_folder_, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND);
  meta->Add(muted_label(info_card, L"Последний раз"), 0, wxALIGN_CENTER_VERTICAL);
  detail_last_ = new wxStaticText(info_card, wxID_ANY, L"—");
  meta->Add(detail_last_, 1, wxALIGN_CENTER_VERTICAL);
  meta->Add(muted_label(info_card, L"Ср. время"), 0, wxALIGN_CENTER_VERTICAL);
  detail_avg_ = new wxStaticText(info_card, wxID_ANY, L"—");
  meta->Add(detail_avg_, 1, wxALIGN_CENTER_VERTICAL);

  detail_comment_ = new wxStaticText(info_card, wxID_ANY, L"");
  detail_comment_->SetName(L"muted");
  detail_comment_->SetForegroundColour(Theme::muted());
  detail_comment_->Hide();

  auto* cmd_label = muted_label(info_card, L"Команда");
  detail_command_ = new wxTextCtrl(info_card, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize,
                                   wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP | wxBORDER_NONE);
  style_text(detail_command_, false);
  detail_command_->SetFont(Theme::mono());
  detail_command_->SetMinSize(FromDIP(wxSize(-1, 56)));
  bind_copy_on_select(detail_command_);

  notice_ = new wxStaticText(info_card, wxID_ANY, L"");
  notice_->SetName(L"meta");
  notice_->SetForegroundColour(Theme::meta());
  notice_->Hide();

  auto* info_sz = new wxBoxSizer(wxVERTICAL);
  info_sz->Add(detail_title_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
  info_sz->Add(meta, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
  info_sz->Add(detail_comment_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
  info_sz->Add(cmd_label, 0, wxBOTTOM, FromDIP(4));
  info_sz->Add(detail_command_, 1, wxEXPAND);
  info_sz->Add(notice_, 0, wxEXPAND | wxTOP, FromDIP(8));
  info_card->SetSizer(info_sz);
  info_card->SetMinSize(FromDIP(wxSize(-1, 180)));

  auto* top = new wxBoxSizer(wxHORIZONTAL);
  top->Add(hint_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  top->Add(run_bundle_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  top->Add(run, 0, wxALIGN_CENTER_VERTICAL);
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(accent_bar_, 0, wxEXPAND);
  root->Add(top, 0, wxEXPAND | wxALL, 8);
  root->Add(list_card, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
  root->Add(info_card, 0, wxEXPAND | wxALL, 8);
  panel->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(panel, 1, wxEXPAND);
  SetSizer(outer);
  apply_dark(this);
  style_list(list_);
  bind_escape_close(this);

  notice_timer_.SetOwner(this);
  Bind(wxEVT_TIMER, &BundleStepsWindow::on_notice_timer, this, notice_timer_.GetId());

  run_bundle_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { run_bundle(); });
  run->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { run_selected(); });
  list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { show_detail(); });
  list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { run_selected(); });
  Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
    if (e.GetKeyCode() == WXK_F5) {
      run_selected();
      return;
    }
    e.Skip();
  });
}

void BundleStepsWindow::show_bundle(const std::string& bundle_id) {
  bundle_id_ = bundle_id;
  clear_notice();
  reload();
  apply_server_look();
  Show();
  Raise();
}

void BundleStepsWindow::apply_server_look() {
  auto* b = config_.bundle_by_id(bundle_id_);
  auto* s = b ? config_.server_by_id(b->server_id) : nullptr;
  wxColour accent = Theme::accent();
  if (s) server_ui_colour(s->ui_colour_dark, s->ui_colour_light, accent);
  if (accent_bar_) {
    accent_bar_->SetBackgroundColour(accent);
    accent_bar_->Refresh();
  }
  if (detail_title_) {
    detail_title_->SetName(L"");
    detail_title_->SetFont(Theme::ui_heading());
    detail_title_->SetForegroundColour(accent.IsOk() ? accent : Theme::text_bright());
  }
  const wxFont list_font = s ? server_list_font(s->ui_font, s->ui_font_pt) : Theme::ui();
  if (list_) {
    list_->SetFont(list_font);
    style_list(list_);
  }
}

void BundleStepsWindow::refresh() {
  if (!IsShown()) return;
  std::string keep;
  long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i >= 0 && i < static_cast<long>(steps_.size())) keep = steps_[static_cast<std::size_t>(i)].id;
  reload(keep);
  maybe_announce_awaited_step();
}

void BundleStepsWindow::await_step(const std::string& command_id, const std::string& name) {
  awaiting_id_ = command_id;
  awaiting_name_ = name;
  awaiting_since_ = now_iso();
  clear_notice();
}

void BundleStepsWindow::reload(const std::string& keep_command_id) {
  steps_.clear();
  list_->DeleteAllItems();
  clear_detail();
  auto* b = config_.bundle_by_id(bundle_id_);
  if (!b) {
    SetTitle(L"Связка — по шагам");
    apply_server_look();
    return;
  }
  std::string title = "Связка: " + b->name;
  if (auto* s = config_.server_by_id(b->server_id)) {
    title += " · " + (s->name.empty() ? s->host : s->name);
  }
  title += " — по шагам";
  SetTitle(wxString::FromUTF8(title));
  for (const auto& cid : b->command_ids) {
    if (auto* c = config_.command_by_id(cid)) steps_.push_back(*c);
  }
  long sel = -1;
  for (std::size_t i = 0; i < steps_.size(); ++i) {
    const auto& c = steps_[i];
    auto preview = c.command;
    if (preview.size() > 80) preview = preview.substr(0, 77) + "…";
    auto comment = c.comment;
    if (comment.size() > 50) comment = comment.substr(0, 47) + "…";
    wxString last = L"—";
    wxString avg = L"—";
    wxColour colour = Theme::text();
    if (command_stats_) {
      auto it = command_stats_->find(c.id);
      if (it != command_stats_->end()) {
        last = wxString::FromUTF8(it->second.latest.last_run_label());
        colour = Theme::run_status(it->second.latest.status);
        if (it->second.run_count > 0) {
          avg = wxString::FromUTF8(format_duration(it->second.average_sec));
        }
      }
    }
    long row = list_->InsertItem(static_cast<long>(i), wxString::FromUTF8(std::to_string(i + 1)));
    list_->SetItem(row, 1, wxString::FromUTF8(c.name));
    list_->SetItem(row, 2, wxString::FromUTF8(group_label(c)));
    list_->SetItem(row, 3, folder_cell(config_, c));
    list_->SetItem(row, 4, wxString::FromUTF8(preview));
    list_->SetItem(row, 5, wxString::FromUTF8(comment));
    list_->SetItem(row, 6, last);
    list_->SetItem(row, 7, avg);
    style_list_row(list_, row, colour);
    if (!keep_command_id.empty() && c.id == keep_command_id) sel = row;
  }
  if (sel < 0 && !steps_.empty()) sel = 0;
  if (sel >= 0) {
    list_->SetItemState(sel, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                        wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    show_detail();
  }
  apply_server_look();
}

void BundleStepsWindow::clear_detail() {
  if (detail_title_) detail_title_->SetLabel(L"");
  if (detail_group_) detail_group_->SetLabel(L"—");
  if (detail_folder_) detail_folder_->SetLabel(L"—");
  if (detail_last_) {
    detail_last_->SetLabel(L"—");
    detail_last_->SetForegroundColour(Theme::text());
  }
  if (detail_avg_) detail_avg_->SetLabel(L"—");
  if (detail_comment_) {
    detail_comment_->SetLabel(L"");
    detail_comment_->Hide();
  }
  if (detail_command_) detail_command_->Clear();
}

void BundleStepsWindow::show_detail() {
  long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i < 0 || i >= static_cast<long>(steps_.size())) {
    clear_detail();
    return;
  }
  const auto& c = steps_[static_cast<std::size_t>(i)];
  detail_title_->SetLabel(
      wxString::FromUTF8("Шаг " + std::to_string(i + 1) + " / " + std::to_string(steps_.size()) + "  •  " + c.name));
  detail_group_->SetLabel(wxString::FromUTF8(group_label(c)));
  detail_folder_->SetLabel(folder_cell(config_, c));

  wxString last = L"—";
  wxString avg = L"—";
  wxColour last_colour = Theme::text();
  if (command_stats_) {
    auto it = command_stats_->find(c.id);
    if (it != command_stats_->end()) {
      last = wxString::FromUTF8(it->second.latest.last_run_label());
      last_colour = Theme::run_status(it->second.latest.status);
      if (it->second.run_count > 0) {
        avg = wxString::FromUTF8(format_duration(it->second.average_sec));
      }
    }
  }
  detail_last_->SetLabel(last);
  detail_last_->SetForegroundColour(last_colour);
  detail_avg_->SetLabel(avg);

  if (c.comment.empty()) {
    detail_comment_->SetLabel(L"");
    detail_comment_->Hide();
  } else {
    detail_comment_->SetLabel(wxString::FromUTF8(c.comment));
    detail_comment_->Show();
  }
  detail_command_->SetValue(wxString::FromUTF8(c.command));
  if (auto* sizer = detail_comment_->GetContainingSizer()) sizer->Layout();
  Layout();
}

void BundleStepsWindow::run_bundle() {
  if (bundle_id_.empty() || !on_run_bundle_) return;
  clear_notice();
  on_run_bundle_(bundle_id_);
}

void BundleStepsWindow::run_selected() {
  long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i < 0 || i >= static_cast<long>(steps_.size())) return;
  if (!on_run_) return;
  const auto& snap = steps_[static_cast<std::size_t>(i)];
  if (auto* live = config_.command_by_id(snap.id)) {
    on_run_(*live);
  } else {
    on_run_(snap);
  }
}

std::string BundleStepsWindow::group_label(const Command& cmd) const {
  if (cmd.group_id.empty()) return "Общее";
  if (auto* g = config_.group_by_id(cmd.group_id)) return g->name;
  return "Общее";
}

void BundleStepsWindow::maybe_announce_awaited_step() {
  if (awaiting_id_.empty() || !command_stats_) return;
  auto it = command_stats_->find(awaiting_id_);
  if (it == command_stats_->end()) return;
  if (it->second.latest.started_at < awaiting_since_) return;
  const auto& latest = it->second.latest;
  std::string result = latest.status_display();
  result += " · " + latest.duration_display();
  const char* tone = (latest.status == "ok") ? "meta" : "error";
  const auto name = awaiting_name_.empty() ? latest.title : awaiting_name_;
  awaiting_id_.clear();
  awaiting_name_.clear();
  awaiting_since_.clear();
  set_notice(wxString::FromUTF8("Шаг «" + name + "»: " + result), tone);
}

void BundleStepsWindow::notify_bundle_finished(const std::string& bundle_id, const std::string& name,
                                               const std::string& reason) {
  if (!IsShown() || bundle_id != bundle_id_) return;
  awaiting_id_.clear();
  awaiting_name_.clear();
  awaiting_since_.clear();
  const bool ok = reason == "готово";
  const char* tone = ok ? "meta" : "error";
  wxString text;
  if (ok) {
    text = wxString::FromUTF8("Связка «" + name + "» завершена");
  } else {
    text = wxString::FromUTF8("Связка «" + name + "»: " + reason);
  }
  set_notice(text, tone);
  refresh();
}

void BundleStepsWindow::set_notice(const wxString& text, const char* tone) {
  if (!notice_) return;
  notice_->SetLabel(text);
  notice_->SetName(wxString::FromUTF8(tone));
  if (std::string(tone) == "error") {
    notice_->SetForegroundColour(Theme::err());
  } else if (std::string(tone) == "muted") {
    notice_->SetForegroundColour(Theme::muted());
  } else {
    notice_->SetForegroundColour(Theme::meta());
  }
  notice_->Show();
  if (auto* sizer = notice_->GetContainingSizer()) sizer->Layout();
  Layout();
  notice_timer_.StartOnce(6000);
}

void BundleStepsWindow::clear_notice() {
  notice_timer_.Stop();
  if (!notice_ || !notice_->IsShown()) return;
  notice_->SetLabel(L"");
  notice_->Hide();
  if (auto* sizer = notice_->GetContainingSizer()) sizer->Layout();
  Layout();
}

void BundleStepsWindow::on_notice_timer(wxTimerEvent&) { clear_notice(); }

}  // namespace fatty

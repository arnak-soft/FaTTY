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

}  // namespace

BundleStepsWindow::BundleStepsWindow(wxWindow* parent, Config& config, std::function<void(const Command&)> on_run,
                                     std::function<void(const std::string& bundle_id)> on_run_bundle,
                                     AppSettings* settings, std::function<void()> persist)
    : wxFrame(parent, wxID_ANY, L"Связка — по шагам", wxDefaultPosition, wxDefaultSize),
      config_(config),
      on_run_(std::move(on_run)),
      on_run_bundle_(std::move(on_run_bundle)) {
  set_icon(this);
  const bool had_geometry = settings && settings->dialog_geometry.count("bundle_steps");
  SetSize(FromDIP(wxSize(780, 520)));
  setup_frame_geometry(this, settings, "bundle_steps", true, std::move(persist));
  if (!had_geometry) CentreOnParent();
  auto* panel = new wxPanel(this);
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
  list_->AppendColumn(L"Название", wxLIST_FORMAT_LEFT, FromDIP(140));
  list_->AppendColumn(L"Группа", wxLIST_FORMAT_LEFT, FromDIP(100));
  list_->AppendColumn(L"Папка", wxLIST_FORMAT_LEFT, FromDIP(140));
  list_->AppendColumn(L"Команда", wxLIST_FORMAT_LEFT, FromDIP(220));
  list_->AppendColumn(L"Комментарий", wxLIST_FORMAT_LEFT, FromDIP(160));
  auto* list_sz = new wxBoxSizer(wxVERTICAL);
  list_sz->Add(list_, 1, wxEXPAND);
  list_card->SetSizer(list_sz);
  auto* detail_card = new RoundedCard(panel);
  detail_ = new wxTextCtrl(detail_card, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP | wxBORDER_NONE);
  style_text(detail_, true);
  auto* detail_sz = new wxBoxSizer(wxVERTICAL);
  detail_sz->Add(detail_, 1, wxEXPAND);
  detail_card->SetSizer(detail_sz);
  bind_copy_on_select(detail_);
  auto* top = new wxBoxSizer(wxHORIZONTAL);
  top->Add(hint_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  top->Add(run_bundle_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  top->Add(run, 0, wxALIGN_CENTER_VERTICAL);
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(top, 0, wxEXPAND | wxALL, 8);
  root->Add(list_card, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
  root->Add(detail_card, 0, wxEXPAND | wxALL, 8);
  detail_card->SetMinSize(FromDIP(wxSize(-1, 120)));
  panel->SetSizer(root);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(panel, 1, wxEXPAND);
  SetSizer(outer);
  apply_dark(this);
  style_list(list_);
  bind_escape_close(this);

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
  reload();
  Show();
  Raise();
}

void BundleStepsWindow::reload() {
  steps_.clear();
  list_->DeleteAllItems();
  detail_->Clear();
  auto* b = config_.bundle_by_id(bundle_id_);
  if (!b) {
    SetTitle(L"Связка — по шагам");
    return;
  }
  SetTitle(wxString::FromUTF8("Связка: " + b->name + " — по шагам"));
  for (const auto& cid : b->command_ids) {
    if (auto* c = config_.command_by_id(cid)) steps_.push_back(*c);
  }
  for (std::size_t i = 0; i < steps_.size(); ++i) {
    const auto& c = steps_[i];
    auto preview = c.command;
    if (preview.size() > 80) preview = preview.substr(0, 77) + "…";
    auto comment = c.comment;
    if (comment.size() > 50) comment = comment.substr(0, 47) + "…";
    long row = list_->InsertItem(static_cast<long>(i), wxString::FromUTF8(std::to_string(i + 1)));
    list_->SetItem(row, 1, wxString::FromUTF8(c.name));
    list_->SetItem(row, 2, wxString::FromUTF8(group_label(c)));
    list_->SetItem(row, 3, folder_cell(config_, c));
    list_->SetItem(row, 4, wxString::FromUTF8(preview));
    list_->SetItem(row, 5, wxString::FromUTF8(comment));
    style_list_row(list_, row, Theme::text());
  }
  if (!steps_.empty()) {
    list_->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                        wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    show_detail();
  }
}

void BundleStepsWindow::show_detail() {
  long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i < 0 || i >= static_cast<long>(steps_.size())) {
    detail_->Clear();
    return;
  }
  const auto& c = steps_[static_cast<std::size_t>(i)];
  std::string text = "Шаг " + std::to_string(i + 1) + " / " + std::to_string(steps_.size()) + "  •  " + c.name;
  text += "\nГруппа: " + group_label(c);
  const auto folder = command_display_folder(config_, c);
  if (folder != "—") {
    text += "\nПапка: " + folder;
  }
  if (!c.comment.empty()) text += "\n\n" + c.comment;
  text += "\n\n" + c.command;
  detail_->SetValue(wxString::FromUTF8(text));
}

void BundleStepsWindow::run_bundle() {
  if (bundle_id_.empty() || !on_run_bundle_) return;
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

}  // namespace fatty

#include "ui/help_window.hpp"

#include "core/paths.hpp"
#include "core/presets.hpp"
#include "core/util.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace fatty {
namespace {

wxTextCtrl* prose(wxWindow* parent, const wxString& text) {
  auto* t = new wxTextCtrl(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP | wxBORDER_NONE);
  style_text(t);
  return t;
}

}  // namespace

HelpWindow::HelpWindow(wxWindow* parent, std::function<void(const std::string&)> on_insert_quick)
    : wxFrame(parent, wxID_ANY, wxString::FromUTF8(std::string("Справка — ") + kAppName), wxDefaultPosition,
              wxSize(840, 600)) {
  set_icon(this);
  nb_ = new wxNotebook(this, wxID_ANY);
  auto start = wxString::FromUTF8(
      "Что это\n\nFaTTY хранит список VPS и команды к ним, затем запускает выбранную команду по SSH.\n\n"
      "Как начать\n\n1. Добавьте VPS: имя, хост, порт, логин, пароль и/или ключ.\n"
      "2. Команды сами не создаются — «Пресеты…» или «Добавить».\n"
      "3. F5 / двойной клик / Enter — запуск. «Стоп» обрывает сессию.\n\n"
      "Окно\n\nСлева серверы, справа команды по папкам-вкладкам, внизу лог. Колонка «Последний раз» — из журнала.\n"
      "«Сбросить в ~» возвращает рабочую папку в домашнюю.\n\n"
      "Файлы, консоль, PuTTY\n\n«Файлы» — SFTP. «Открыть консоль» — ssh.exe. «PuTTY» подставляет пароль или ключ.\n"
      "Интерактивное (htop, nano) — в консоли, не через F5.\n\n"
      "Журнал: Ctrl+J. Настройки: Ctrl+,.");
  auto keys = wxString::FromUTF8(
      "F5              запустить выбранную команду\n"
      "F2              изменить команду\n"
      "Enter           запустить (фокус в списке команд)\n"
      "Ctrl+↑ / Ctrl+↓ порядок команд\n"
      "Ctrl+J          журнал запусков\n"
      "Delete          удалить запись в журнале\n"
      "Ctrl+,          настройки\n"
      "F1              эта справка\n\n"
      "Выделение мышью  сразу копирует текст (как в PuTTY)\n"
      "Ctrl+C           копировать выделение в выводе\n"
      "Escape           закрыть диалог\n\n"
      "Файлы: Enter / двойной клик — папка или скачать\n"
      "Backspace / Alt+↑ — на уровень вверх");
  auto tips = wxString::FromUTF8(
      "• Login-shell лучше не выключать — иначе может не быть PATH из .bashrc.\n"
      "• FaTTY помнит рабочую папку. Случайный cd уедет в следующий Deploy — «Сбросить в ~».\n"
      "• Не запускайте через F5 интерактивное: top, less, vim, pm2 logs без --nostream.\n"
      "• Для sudo нужен NOPASSWD, иначе команда зависнет на Password:.\n"
      "• Мастер-пароль нельзя восстановить.\n"
      "• Журнал не хранит пароли.\n\n"
      "Конфиг: %APPDATA%\\FaTTY\\config.json");

  auto* p0 = new wxPanel(nb_);
  auto* s0 = new wxBoxSizer(wxVERTICAL);
  s0->Add(prose(p0, start), 1, wxEXPAND | wxALL, 8);
  p0->SetSizer(s0);
  nb_->AddPage(p0, "Как пользоваться");

  auto* p1 = new wxPanel(nb_);
  auto* s1 = new wxBoxSizer(wxVERTICAL);
  auto* k = prose(p1, keys);
  k->SetFont(Theme::mono());
  s1->Add(k, 1, wxEXPAND | wxALL, 8);
  p1->SetSizer(s1);
  nb_->AddPage(p1, "Клавиши");

  auto* p2 = new wxPanel(nb_);
  auto* list = new wxListCtrl(p2, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  list->AppendColumn("Группа", wxLIST_FORMAT_LEFT, 100);
  list->AppendColumn("Название", wxLIST_FORMAT_LEFT, 140);
  list->AppendColumn("Команда", wxLIST_FORMAT_LEFT, 420);
  struct Item {
    std::string group, name, command, tip;
  };
  std::vector<Item> cmds = {
      {"Деплой", "Deploy", std::string("cd ") + kDefaultAppDir + " && git pull origin main && pm2 restart app", ""},
      {"Деплой", "Git pull", std::string("cd ") + kDefaultAppDir + " && git pull origin main", ""},
      {"PM2", "Restart", "pm2 restart app", ""},
      {"PM2", "Status", "pm2 status", ""},
      {"PM2", "Logs", "pm2 logs app --lines 120 --nostream", ""},
      {"Nginx", "Reload", "nginx -t && (systemctl reload nginx || service nginx reload)", ""},
      {"Сервер", "Состояние", "hostname; date; uptime; echo; df -hT; echo; free -h", ""},
      {"Сеть", "Кто слушает", "ss -tlnp", ""},
  };
  for (const auto& c : cmds) {
    long row = list->InsertItem(list->GetItemCount(), wxString::FromUTF8(c.group));
    list->SetItem(row, 1, wxString::FromUTF8(c.name));
    list->SetItem(row, 2, wxString::FromUTF8(c.command));
  }
  auto* copy = new wxButton(p2, wxID_ANY, "Копировать");
  auto* insert = new wxButton(p2, wxID_ANY, "В разовую");
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  row->Add(copy, 0, wxRIGHT, 8);
  row->Add(insert);
  auto* s2 = new wxBoxSizer(wxVERTICAL);
  s2->Add(list, 1, wxEXPAND | wxALL, 8);
  s2->Add(row, 0, wxALL, 8);
  p2->SetSizer(s2);
  nb_->AddPage(p2, "Команды");
  copy->Bind(wxEVT_BUTTON, [list, cmds](wxCommandEvent&) {
    long i = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (i < 0) return;
    if (wxTheClipboard->Open()) {
      wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(cmds[static_cast<std::size_t>(i)].command)));
      wxTheClipboard->Close();
    }
  });
  insert->Bind(wxEVT_BUTTON, [list, cmds, on_insert_quick](wxCommandEvent&) {
    long i = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (i >= 0 && on_insert_quick) on_insert_quick(cmds[static_cast<std::size_t>(i)].command);
  });

  auto* p3 = new wxPanel(nb_);
  auto* s3 = new wxBoxSizer(wxVERTICAL);
  s3->Add(prose(p3, tips), 1, wxEXPAND | wxALL, 8);
  p3->SetSizer(s3);
  nb_->AddPage(p3, "Советы");

  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(nb_, 1, wxEXPAND);
  SetSizer(outer);
  apply_dark(this);
  bind_escape_close(this);
}

void HelpWindow::show_tab(const std::string& name) {
  for (size_t i = 0; i < nb_->GetPageCount(); ++i) {
    if (nb_->GetPageText(i) == wxString::FromUTF8(name)) {
      nb_->SetSelection(i);
      break;
    }
  }
}

}  // namespace fatty

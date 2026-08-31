#include "ui/help_window.hpp"

#include "core/paths.hpp"
#include "core/presets.hpp"
#include "core/util.hpp"
#include "ui/layout.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/clipbrd.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <vector>

namespace fatty {
namespace {

wxTextCtrl* prose(wxWindow* parent, const wxString& text) {
  auto* t = new wxTextCtrl(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP | wxBORDER_NONE);
  style_text(t);
  return t;
}

}  // namespace

HelpWindow::HelpWindow(wxWindow* parent, std::function<void(const std::string&)> on_insert_quick,
                       AppSettings* settings, std::function<void()> persist)
    : wxFrame(parent, wxID_ANY, wxString(L"Справка — ") + wxString::FromUTF8(kAppName), wxDefaultPosition,
              wxDefaultSize) {
  set_icon(this);
  const bool had_geometry = settings && settings->dialog_geometry.count("help");
  SetSize(FromDIP(wxSize(840, 600)));
  setup_frame_geometry(this, settings, "help", true, std::move(persist));
  if (!had_geometry) CentreOnParent();
  auto* panel = new wxPanel(this);
  nb_ = new RoundedNotebook(panel);
  auto start = wxString(
      L"Что это\n\nFaTTY хранит список VPS и команды к ним, затем запускает выбранную команду по SSH.\n\n"
      L"Как начать\n\n1. Добавьте VPS: имя, хост, порт, логин, пароль и/или ключ.\n"
      L"2. Команды сами не создаются — «Пресеты…» или «Добавить». Справа вкладки «Группы» и «Связки». Внутри Групп — вкладки групп («Группа+»); без группы — «Общее».\n"
      L"3. F5 / двойной клик / Enter — запуск. Пока идёт команда, те же действия ставят выбранные в очередь; «Стоп» обрывает текущую и сбрасывает очередь.\n\n"
      L"Окно\n\nСлева серверы, справа вкладки «Группы» и «Связки» (как в Chrome), внизу лог. Колонка «Последний раз» — из журнала; «Ср. время» — среднее по всем запускам этой команды.\n"
      L"Цвет названия: зелёный — успех, жёлтый — таймаут/прервано, красный — ошибка.\n"
      L"Колонка «Папка» — каталог, в который консоль перейдёт перед командой. Скрывается в настройках. Комментарий команды — колонка в списке и текст при подтверждении запуска. Если снять галочку «Переходить в папку перед выполнением», команда идёт в текущем каталоге.\n"
      L"Порядок столбцов по умолчанию: Папка, Название, Команда, Комментарий, Последний раз, Ср. время. Заголовки перетаскиваются; клик по Папка / Название / Команда / Комментарий / Ср. время сортирует.\n"
      L"Перед запуском — предупреждение. В правке команды можно снять галочку «Предупреждать перед запуском».\n"
      L"Несколько команд: Ctrl/Shift+клик, затем «Переместить в группу» или F5 — все выбранные в очередь.\n"
      L"«Сбросить в ~» возвращает рабочую папку в домашнюю (для команд без своего каталога).\n\n"
      L"Связка (bundle) — последовательность команд выбранного VPS, в том числе из разных групп. Вкладка «Связки» рядом с «Группами». Создайте связку и перенесите команды вправо: слева они исчезают. Один и тот же шаг дважды — «Копировать» справа.\n"
      L"«По шагам» (или двойной клик по связке) открывает шпаргалку: запускайте каждый шаг вручную, когда готовы; шаги можно пропускать и повторять. «Запустить связку» гоняет все шаги подряд с паузой. «Стоп» прерывает и текущую команду, и очередь. Ошибка на шаге останавливает автозапуск.\n\n"
      L"Файлы, консоль, PuTTY, WinSCP\n\n«Файлы» — SFTP внутри FaTTY. «Открыть консоль» — ssh.exe. "
      L"«PuTTY» и «WinSCP» подставляют пароль или ключ из карточки VPS.\n"
      L"В Настройки → Программы можно указать пути и добавить свои кнопки (аргументы с {host}, {user}, {sftp_url} и т.п.).\n"
      L"Интерактивное (htop, nano) — в консоли или PuTTY, не через F5.\n\n"
      L"Журнал: Ctrl+J. Настройки: Ctrl+,.");
  auto keys = wxString(
      L"F5              запустить выбранную команду (если занято — в очередь)\n"
      L"F2              изменить команду\n"
      L"Enter           запустить (фокус в списке команд)\n"
      L"Ctrl+клик       добавить команду к выделению\n"
      L"Shift+клик      выделить диапазон команд\n"
      L"Ctrl+↑ / Ctrl+↓ порядок команд\n"
      L"Ctrl+J          журнал запусков\n"
      L"Delete          удалить запись в журнале\n"
      L"Ctrl+,          настройки\n"
      L"F1              эта справка\n\n"
      L"Выделение мышью  сразу копирует текст (как в PuTTY)\n"
      L"Ctrl+C           копировать выделение в выводе\n"
      L"Escape           закрыть диалог\n\n"
      L"Файлы: Enter / двойной клик — папка или скачать\n"
      L"Backspace / Alt+↑ — на уровень вверх");
  auto tips = wxString(
      L"• Login-shell лучше не выключать — иначе может не быть PATH из .bashrc.\n"
      L"• У команды можно задать папку и галочку «переходить перед выполнением» — тогда cd всегда в этот каталог, а не в последний случайный.\n"
      L"• Если галочку снять, команда идёт в текущей папке сессии. «Сбросить в ~» возвращает её в домашнюю.\n"
      L"• Не запускайте через F5 интерактивное: top, less, vim, pm2 logs без --nostream.\n"
      L"• Для sudo нужен NOPASSWD, иначе команда зависнет на Password:.\n"
      L"• Мастер-пароль нельзя восстановить.\n"
      L"• Журнал не хранит пароли, но сохраняет вывод команды.\n"
      L"• Конфиг копируется раз в сутки в backups (Настройки → Данные; можно выключить).\n\n"
      L"Конфиг: %APPDATA%\\FaTTY\\config.json");

  auto* p0 = new wxPanel(nb_);
  p0->SetName(L"card-page");
  auto* s0 = new wxBoxSizer(wxVERTICAL);
  s0->Add(prose(p0, start), 1, wxEXPAND | wxALL, 8);
  p0->SetSizer(s0);
  nb_->AddPage(p0, L"Как пользоваться");

  auto* p1 = new wxPanel(nb_);
  p1->SetName(L"card-page");
  auto* s1 = new wxBoxSizer(wxVERTICAL);
  auto* k = prose(p1, keys);
  k->SetFont(Theme::mono());
  s1->Add(k, 1, wxEXPAND | wxALL, 8);
  p1->SetSizer(s1);
  nb_->AddPage(p1, L"Клавиши");

  auto* p2 = new wxPanel(nb_);
  p2->SetName(L"card-page");
  auto* list = new wxListCtrl(p2, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
  list->AppendColumn(L"Группа", wxLIST_FORMAT_LEFT, FromDIP(90));
  list->AppendColumn(L"Название", wxLIST_FORMAT_LEFT, FromDIP(120));
  list->AppendColumn(L"Комментарий", wxLIST_FORMAT_LEFT, FromDIP(200));
  list->AppendColumn(L"Команда", wxLIST_FORMAT_LEFT, FromDIP(360));
  struct Item {
    std::string group, name, command, tip;
  };
  std::vector<Item> cmds = {
      {"Деплой", "Deploy", std::string("cd ") + kDefaultAppDir + " && git pull origin main && pm2 restart app",
       "git pull и перезапуск pm2"},
      {"Деплой", "Git pull", std::string("cd ") + kDefaultAppDir + " && git pull origin main",
       "подтянуть ветку без перезапуска"},
      {"PM2", "Restart", "pm2 restart app", "перезапустить процесс"},
      {"PM2", "Status", "pm2 status", "список процессов"},
      {"PM2", "Logs", "pm2 logs app --lines 120 --nostream", "последние 120 строк, без follow"},
      {"Nginx", "Reload", "nginx -t && (systemctl reload nginx || service nginx reload)",
       "проверка конфига и reload"},
      {"Сервер", "Состояние", "hostname; date; uptime; echo; df -hT; echo; free -h", "диск, память, uptime"},
      {"Сеть", "Кто слушает", "ss -tlnp", "порты и процессы"},
  };
  for (const auto& c : cmds) {
    long row = list->InsertItem(list->GetItemCount(), wxString::FromUTF8(c.group));
    list->SetItem(row, 1, wxString::FromUTF8(c.name));
    list->SetItem(row, 2, wxString::FromUTF8(c.tip));
    list->SetItem(row, 3, wxString::FromUTF8(c.command));
  }
  auto* copy = make_button(p2, L"Копировать", BtnIcon::Copy);
  auto* insert = make_button(p2, L"В разовую", BtnIcon::Insert);
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  row->Add(copy, 0, wxRIGHT, 8);
  row->Add(insert);
  auto* s2 = new wxBoxSizer(wxVERTICAL);
  s2->Add(list, 1, wxEXPAND | wxALL, 8);
  s2->Add(row, 0, wxALL, 8);
  p2->SetSizer(s2);
  nb_->AddPage(p2, L"Команды");
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
  p3->SetName(L"card-page");
  auto* s3 = new wxBoxSizer(wxVERTICAL);
  s3->Add(prose(p3, tips), 1, wxEXPAND | wxALL, 8);
  p3->SetSizer(s3);
  nb_->AddPage(p3, L"Советы");

  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(nb_, 1, wxEXPAND | wxALL, 8);
  panel->SetSizer(outer);
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(panel, 1, wxEXPAND);
  SetSizer(root);
  apply_dark(this);
  bind_escape_close(this);
}

void HelpWindow::show_tab(const std::string& name) {
  for (size_t i = 0; i < nb_->GetPageCount(); ++i) {
    if (nb_->GetPageText(i) == wxString::FromUTF8(name)) {
      nb_->SetSelection(static_cast<int>(i));
      break;
    }
  }
}

}  // namespace fatty

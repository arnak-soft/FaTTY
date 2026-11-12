#include "ui/backup_window.hpp"

#include "core/util.hpp"
#include "core/vps_backup.hpp"
#include "core/vps_backup_engine.hpp"
#include "ui/dialogs.hpp"
#include "ui/theme.hpp"

#include <wx/dirdlg.h>
#include <wx/msgdlg.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>

#include <algorithm>
#include <sstream>

namespace fatty {
namespace {

wxStaticText* muted_label(wxWindow* parent, const wxString& text, int wrap_dip = 520) {
  auto* t = new wxStaticText(parent, wxID_ANY, text);
  t->SetName(L"muted");
  t->SetForegroundColour(Theme::muted());
  t->Wrap(parent->FromDIP(wrap_dip));
  return t;
}

class BackupJobDialog : public PositionedDialog {
 public:
  BackupJobDialog(wxWindow* parent, BackupJob job, Config& config, AppSettings* settings,
                  std::function<void()> persist)
      : PositionedDialog(parent, L"Задание резервного копирования", wxSize(780, 700)),
        job_(std::move(job)),
        config_(config) {
    SetName(L"dialog");
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* nb = new RoundedNotebook(this);

    // --- Источник ---
    auto* src = new wxScrolledWindow(nb, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    src->SetName(L"card-page");
    src->SetScrollRate(0, 16);
    auto* ssz = new wxBoxSizer(wxVERTICAL);
    ssz->Add(muted_label(src, L"Что копировать. Для папок указывайте абсолютные пути на VPS, например /var/www."),
             0, wxALL, 8);
    auto* nrow = new wxBoxSizer(wxHORIZONTAL);
    nrow->Add(new wxStaticText(src, wxID_ANY, L"Название"), 0, wxALIGN_CENTER_VERTICAL);
    name_ = new wxTextCtrl(src, wxID_ANY, wxString::FromUTF8(job_.name));
    nrow->Add(name_, 1, wxLEFT, 8);
    ssz->Add(nrow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* vrow = new wxBoxSizer(wxHORIZONTAL);
    vrow->Add(new wxStaticText(src, wxID_ANY, L"VPS"), 0, wxALIGN_CENTER_VERTICAL);
    {
      wxArrayString names;
      int sel = 0;
      for (const auto& s : config_.servers) {
        server_ids_.push_back(s.id);
        names.Add(wxString::FromUTF8(s.name.empty() ? s.host : s.name));
        if (s.id == job_.source_server_id) sel = static_cast<int>(server_ids_.size() - 1);
      }
      server_ = new ThemedChoice(src, wxID_ANY, wxDefaultPosition, wxDefaultSize, names);
      if (!names.empty()) server_->SetSelection(sel);
    }
    vrow->Add(server_, 1, wxLEFT, 8);
    ssz->Add(vrow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    {
      wxArrayString kinds;
      kinds.Add(L"Папки (всё содержимое)");
      kinds.Add(L"База данных на хосте");
      kinds.Add(L"База данных в Docker");
      kind_ = new ThemedChoice(src, wxID_ANY, wxDefaultPosition, wxDefaultSize, kinds);
      kind_->SetSelection(static_cast<int>(job_.kind));
    }
    ssz->Add(new wxStaticText(src, wxID_ANY, L"Тип"), 0, wxLEFT | wxRIGHT, 8);
    ssz->Add(kind_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    paths_ = new wxTextCtrl(src, wxID_ANY, wxString::FromUTF8(join_lines(job_.paths)), wxDefaultPosition,
                            FromDIP(wxSize(-1, 90)), wxTE_MULTILINE);
    ssz->Add(new wxStaticText(src, wxID_ANY, L"Папки (по одной на строку)"), 0, wxLEFT | wxRIGHT, 8);
    ssz->Add(paths_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    ssz->Add(muted_label(src, L"Пример: /var/www и /etc/nginx. Не указывайте / — это опасно и запрещено."), 0,
             wxLEFT | wxRIGHT | wxBOTTOM, 8);

    excludes_ = new wxTextCtrl(src, wxID_ANY, wxString::FromUTF8(join_lines(job_.exclude_globs)),
                               wxDefaultPosition, FromDIP(wxSize(-1, 60)), wxTE_MULTILINE);
    ssz->Add(new wxStaticText(src, wxID_ANY, L"Исключения (glob, по одному)"), 0, wxLEFT | wxRIGHT, 8);
    ssz->Add(excludes_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    {
      wxArrayString engines;
      engines.Add(L"PostgreSQL");
      engines.Add(L"MySQL");
      engines.Add(L"MariaDB");
      db_engine_ = new ThemedChoice(src, wxID_ANY, wxDefaultPosition, wxDefaultSize, engines);
      db_engine_->SetSelection(static_cast<int>(job_.db_engine));
    }
    ssz->Add(new wxStaticText(src, wxID_ANY, L"СУБД"), 0, wxLEFT | wxRIGHT, 8);
    ssz->Add(db_engine_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    db_name_ = new wxTextCtrl(src, wxID_ANY, wxString::FromUTF8(job_.db_name));
    db_user_ = new wxTextCtrl(src, wxID_ANY, wxString::FromUTF8(job_.db_user));
    db_password_ = new wxTextCtrl(src, wxID_ANY, wxString::FromUTF8(job_.db_password), wxDefaultPosition,
                                  wxDefaultSize, wxTE_PASSWORD);
    db_port_ = new wxTextCtrl(src, wxID_ANY, job_.db_port ? std::to_wstring(job_.db_port) : L"");
    docker_ = new wxTextCtrl(src, wxID_ANY, wxString::FromUTF8(job_.docker_container));
    auto add_labeled = [&](const wchar_t* label, wxWindow* w) {
      ssz->Add(new wxStaticText(src, wxID_ANY, label), 0, wxLEFT | wxRIGHT, 8);
      ssz->Add(w, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    };
    add_labeled(L"База (пусто = все)", db_name_);
    add_labeled(L"Пользователь БД", db_user_);
    add_labeled(L"Пароль БД", db_password_);
    add_labeled(L"Порт БД (пусто = по умолчанию)", db_port_);
    add_labeled(L"Docker-контейнер", docker_);
    ssz->Add(muted_label(src, L"Пароль БД хранится в том же vault, что и пароли VPS. Для peer-auth Postgres "
                              L"оставьте пароль пустым."),
             0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    src->SetSizer(ssz);
    nb->AddPage(src, L"Источник");

    // --- Назначение ---
    auto* dst = new wxScrolledWindow(nb, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    dst->SetName(L"card-page");
    dst->SetScrollRate(0, 16);
    auto* dsz = new wxBoxSizer(wxVERTICAL);
    dsz->Add(muted_label(dst, L"Куда класть готовые архивы. Путь назначения обязателен и должен быть абсолютным."),
             0, wxALL, 8);
    dest_same_ = new wxRadioButton(dst, wxID_ANY, L"На этом же VPS", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    dest_other_ = new wxRadioButton(dst, wxID_ANY, L"На другую VPS из списка FaTTY");
    dest_local_ = new wxRadioButton(dst, wxID_ANY, L"На этот компьютер (где запущен FaTTY)");
    dest_same_->SetValue(job_.dest_kind == BackupDestKind::SameVps);
    dest_other_->SetValue(job_.dest_kind == BackupDestKind::OtherVps);
    dest_local_->SetValue(job_.dest_kind == BackupDestKind::LocalPc);
    dsz->Add(dest_same_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    dsz->Add(muted_label(dst, L"Быстро и без трафика через ПК. Удобно, если на сервере есть отдельный диск."), 0,
             wxLEFT | wxRIGHT | wxBOTTOM, 8);
    dsz->Add(dest_other_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    dsz->Add(muted_label(dst, L"В режиме «пока FaTTY открыт» копия идёт через ваш ПК. Для больших объёмов лучше "
                              L"потом поставить cron и прямой rsync между серверами."),
             0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    {
      wxArrayString names;
      int sel = 0;
      for (const auto& s : config_.servers) {
        dest_server_ids_.push_back(s.id);
        names.Add(wxString::FromUTF8(s.name.empty() ? s.host : s.name));
        if (s.id == job_.dest_server_id) sel = static_cast<int>(dest_server_ids_.size() - 1);
      }
      dest_server_ = new ThemedChoice(dst, wxID_ANY, wxDefaultPosition, wxDefaultSize, names);
      if (!names.empty()) dest_server_->SetSelection(sel);
    }
    dsz->Add(dest_server_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    dsz->Add(dest_local_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    dsz->Add(muted_label(dst, L"Архив скачивается по SFTP в выбранную папку Windows. Cron для этого режима "
                              L"недоступен — ПК может быть выключен."),
             0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* prow = new wxBoxSizer(wxHORIZONTAL);
    dest_path_ = new wxTextCtrl(dst, wxID_ANY, wxString::FromUTF8(job_.dest_path));
    auto* browse = make_button(dst, L"Обзор…", BtnIcon::Folder);
    browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      wxDirDialog dlg(this, L"Папка для копий на этом компьютере", dest_path_->GetValue());
      if (dlg.ShowModal() == wxID_OK) dest_path_->SetValue(dlg.GetPath());
    });
    prow->Add(dest_path_, 1, wxALIGN_CENTER_VERTICAL);
    prow->Add(browse, 0, wxLEFT, 8);
    dsz->Add(new wxStaticText(dst, wxID_ANY, L"Папка назначения"), 0, wxLEFT | wxRIGHT, 8);
    dsz->Add(prow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    dst->SetSizer(dsz);
    nb->AddPage(dst, L"Назначение");

    // --- Расписание ---
    auto* sch = new wxScrolledWindow(nb, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    sch->SetName(L"card-page");
    sch->SetScrollRate(0, 16);
    auto* zsz = new wxBoxSizer(wxVERTICAL);
    enabled_ = new ThemedCheckBox(sch, wxID_ANY, L"Включить автоматический запуск");
    enabled_->SetValue(job_.enabled);
    zsz->Add(enabled_, 0, wxALL, 8);
    zsz->Add(muted_label(sch, L"Клиентский режим работает только пока открыт FaTTY (как автоопрос «Состояние»). "
                              L"Минимальный интервал — 1 час."),
             0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    {
      wxArrayString modes;
      modes.Add(L"Пока FaTTY открыт (клиент)");
      modes.Add(L"На сервере (cron)");
      schedule_ = new ThemedChoice(sch, wxID_ANY, wxDefaultPosition, wxDefaultSize, modes);
      schedule_->SetSelection(job_.schedule_mode == BackupScheduleMode::Cron ? 1 : 0);
    }
    zsz->Add(schedule_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    {
      wxArrayString intervals;
      intervals.Add(L"1 час");
      intervals.Add(L"6 часов");
      intervals.Add(L"12 часов");
      intervals.Add(L"1 сутки");
      intervals.Add(L"1 неделя");
      intervals.Add(L"свой (секунды)…");
      interval_ = new ThemedChoice(sch, wxID_ANY, wxDefaultPosition, wxDefaultSize, intervals);
      const int presets[] = {3600, 6 * 3600, 12 * 3600, 86400, 7 * 86400};
      int sel = 5;
      for (int i = 0; i < 5; ++i) {
        if (job_.interval_sec == presets[i]) {
          sel = i;
          break;
        }
      }
      interval_->SetSelection(sel);
    }
    interval_sec_ = new wxTextCtrl(sch, wxID_ANY, std::to_wstring(job_.interval_sec));
    zsz->Add(new wxStaticText(sch, wxID_ANY, L"Интервал"), 0, wxLEFT | wxRIGHT, 8);
    zsz->Add(interval_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    zsz->Add(interval_sec_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    sch->SetSizer(zsz);
    nb->AddPage(sch, L"Расписание");

    // --- Лимиты ---
    auto* lim = new wxScrolledWindow(nb, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    lim->SetName(L"card-page");
    lim->SetScrollRate(0, 16);
    auto* lsz = new wxBoxSizer(wxVERTICAL);
    lsz->Add(muted_label(lim, L"Лимит защищает диск от переполнения. При достижении порога выполняются выбранные "
                              L"действия. Самую новую копию программа не удаляет."),
             0, wxALL, 8);
    max_gb_ = new wxTextCtrl(lim, wxID_ANY, wxString::Format(L"%.1f", job_.max_total_bytes / (1024.0 * 1024.0 * 1024.0)));
    overflow_pct_ = new wxTextCtrl(lim, wxID_ANY, std::to_wstring(job_.overflow_at_pct));
    auto add_num = [&](const wchar_t* label, wxTextCtrl* field) {
      auto* row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(new wxStaticText(lim, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
      row->Add(field, 0, wxLEFT, 8);
      lsz->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    };
    add_num(L"Лимит всех копий, ГБ", max_gb_);
    add_num(L"Порог действий, %", overflow_pct_);

    act_warn_ = new ThemedCheckBox(lim, wxID_ANY, L"Предупреждение");
    act_oldest_ = new ThemedCheckBox(lim, wxID_ANY, L"Удалять самые старые");
    act_bytes_ = new ThemedCheckBox(lim, wxID_ANY, L"Удалить заданный объём старых");
    act_stride_ = new ThemedCheckBox(lim, wxID_ANY, L"Удаление через шаг (N удалить, M оставить)");
    auto has = [&](BackupOverflowAction a) {
      return std::find(job_.overflow_actions.begin(), job_.overflow_actions.end(), a) != job_.overflow_actions.end();
    };
    act_warn_->SetValue(has(BackupOverflowAction::Warn));
    act_oldest_->SetValue(has(BackupOverflowAction::DeleteOldest));
    act_bytes_->SetValue(has(BackupOverflowAction::DeleteBytes));
    act_stride_->SetValue(has(BackupOverflowAction::DeleteStride));
    lsz->Add(act_warn_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    lsz->Add(act_oldest_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    lsz->Add(act_bytes_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    delete_gb_ = new wxTextCtrl(lim, wxID_ANY,
                                wxString::Format(L"%.1f", job_.delete_bytes_amount / (1024.0 * 1024.0 * 1024.0)));
    add_num(L"Объём удаления, ГБ", delete_gb_);
    lsz->Add(act_stride_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    stride_del_ = new wxTextCtrl(lim, wxID_ANY, std::to_wstring(job_.stride_delete));
    stride_keep_ = new wxTextCtrl(lim, wxID_ANY, std::to_wstring(job_.stride_keep));
    add_num(L"Удалить подряд", stride_del_);
    add_num(L"Оставить подряд", stride_keep_);
    preview_ = muted_label(lim, wxString::FromUTF8(describe_overflow_preview(job_)));
    lsz->Add(preview_, 0, wxALL, 8);
    min_free_gb_ =
        new wxTextCtrl(lim, wxID_ANY, wxString::Format(L"%.1f", job_.min_dest_free_bytes / (1024.0 * 1024.0 * 1024.0)));
    max_one_gb_ = new wxTextCtrl(
        lim, wxID_ANY,
        job_.max_single_archive_bytes > 0
            ? wxString::Format(L"%.1f", job_.max_single_archive_bytes / (1024.0 * 1024.0 * 1024.0))
            : wxString(L"0"));
    add_num(L"Мин. свободное место dest, ГБ", min_free_gb_);
    add_num(L"Макс. размер одной копии, ГБ (0 = без лимита)", max_one_gb_);
    auto refresh_preview = [this](wxCommandEvent&) { update_preview(); };
    for (auto* w : {max_gb_, overflow_pct_, stride_del_, stride_keep_, delete_gb_}) {
      w->Bind(wxEVT_TEXT, refresh_preview);
    }
    act_warn_->Bind(wxEVT_CHECKBOX, refresh_preview);
    act_oldest_->Bind(wxEVT_CHECKBOX, refresh_preview);
    act_bytes_->Bind(wxEVT_CHECKBOX, refresh_preview);
    act_stride_->Bind(wxEVT_CHECKBOX, refresh_preview);
    lim->SetSizer(lsz);
    nb->AddPage(lim, L"Лимиты");

    error_ = new wxStaticText(this, wxID_ANY, L"");
    error_->SetForegroundColour(Theme::err());
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* cancel = make_button(this, L"Отмена", BtnIcon::Cancel);
    auto* ok = make_button(this, L"Сохранить", BtnIcon::Save);
    ok->SetDefault();
    buttons->AddStretchSpacer();
    buttons->Add(cancel, 0, wxRIGHT, 8);
    buttons->Add(ok, 0);
    cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
    ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_ok(); });

    root->Add(nb, 1, wxEXPAND | wxALL, 8);
    root->Add(error_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    SetSizer(root);
    SetMinSize(FromDIP(wxSize(720, 640)));
    // Если сохранённой геометрии ещё нет — стартуем с нормального размера, не с крошечного default.
    if (!settings || settings->dialog_geometry.find("backup_job") == settings->dialog_geometry.end()) {
      SetSize(FromDIP(wxSize(780, 700)));
      if (GetParent()) CentreOnParent();
    }
    setup_layout(settings, "backup_job", true, std::move(persist));
    sync_kind_ui();
    kind_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { sync_kind_ui(); });
  }

  BackupJob result;
  bool accepted = false;

 private:
  static std::string join_lines(const std::vector<std::string>& lines) {
    std::ostringstream ss;
    for (std::size_t i = 0; i < lines.size(); ++i) {
      if (i) ss << '\n';
      ss << lines[i];
    }
    return ss.str();
  }
  static std::vector<std::string> split_lines(const wxString& text) {
    std::vector<std::string> out;
    std::istringstream in(text.utf8_string());
    std::string line;
    while (std::getline(in, line)) {
      auto t = trim(line);
      if (!t.empty()) out.push_back(std::move(t));
    }
    return out;
  }
  static std::int64_t gb_to_bytes(const wxString& text) {
    double gb = 0;
    text.ToDouble(&gb);
    if (gb < 0) gb = 0;
    return static_cast<std::int64_t>(gb * 1024.0 * 1024.0 * 1024.0);
  }

  void sync_kind_ui() {
    const int k = kind_->GetSelection();
    const bool files = k <= 0;
    const bool docker = k == 2;
    paths_->Enable(files);
    excludes_->Enable(files);
    db_engine_->Enable(!files);
    db_name_->Enable(!files);
    db_user_->Enable(!files);
    db_password_->Enable(!files);
    db_port_->Enable(!files);
    docker_->Enable(docker);
  }

  void update_preview() {
    BackupJob tmp = collect_unchecked();
    preview_->SetLabel(wxString::FromUTF8(describe_overflow_preview(tmp)));
    preview_->Wrap(FromDIP(520));
    Layout();
  }

  BackupJob collect_unchecked() {
    BackupJob j = job_;
    j.name = name_->GetValue().utf8_string();
    int si = server_->GetSelection();
    if (si >= 0 && si < static_cast<int>(server_ids_.size())) j.source_server_id = server_ids_[si];
    j.kind = static_cast<BackupKind>(std::max(0, kind_->GetSelection()));
    j.paths = split_lines(paths_->GetValue());
    j.exclude_globs = split_lines(excludes_->GetValue());
    j.db_engine = static_cast<BackupDbEngine>(std::max(0, db_engine_->GetSelection()));
    j.db_name = trim(db_name_->GetValue().utf8_string());
    j.db_user = trim(db_user_->GetValue().utf8_string());
    j.db_password = db_password_->GetValue().utf8_string();
    long port = 0;
    db_port_->GetValue().ToLong(&port);
    j.db_port = static_cast<int>(port);
    j.docker_container = trim(docker_->GetValue().utf8_string());
    if (dest_other_->GetValue())
      j.dest_kind = BackupDestKind::OtherVps;
    else if (dest_local_->GetValue())
      j.dest_kind = BackupDestKind::LocalPc;
    else
      j.dest_kind = BackupDestKind::SameVps;
    j.dest_path = trim(dest_path_->GetValue().utf8_string());
    int di = dest_server_->GetSelection();
    if (di >= 0 && di < static_cast<int>(dest_server_ids_.size())) j.dest_server_id = dest_server_ids_[di];
    j.enabled = enabled_->GetValue();
    j.schedule_mode = schedule_->GetSelection() == 1 ? BackupScheduleMode::Cron : BackupScheduleMode::Client;
    const int presets[] = {3600, 6 * 3600, 12 * 3600, 86400, 7 * 86400};
    int isel = interval_->GetSelection();
    if (isel >= 0 && isel < 5)
      j.interval_sec = presets[isel];
    else {
      long v = j.interval_sec;
      interval_sec_->GetValue().ToLong(&v);
      j.interval_sec = static_cast<int>(v);
    }
    j.max_total_bytes = gb_to_bytes(max_gb_->GetValue());
    long pct = j.overflow_at_pct;
    overflow_pct_->GetValue().ToLong(&pct);
    j.overflow_at_pct = static_cast<int>(pct);
    j.overflow_actions.clear();
    if (act_warn_->GetValue()) j.overflow_actions.push_back(BackupOverflowAction::Warn);
    if (act_oldest_->GetValue()) j.overflow_actions.push_back(BackupOverflowAction::DeleteOldest);
    if (act_bytes_->GetValue()) j.overflow_actions.push_back(BackupOverflowAction::DeleteBytes);
    if (act_stride_->GetValue()) j.overflow_actions.push_back(BackupOverflowAction::DeleteStride);
    long sd = j.stride_delete, sk = j.stride_keep;
    stride_del_->GetValue().ToLong(&sd);
    stride_keep_->GetValue().ToLong(&sk);
    j.stride_delete = static_cast<int>(sd);
    j.stride_keep = static_cast<int>(sk);
    j.delete_bytes_amount = gb_to_bytes(delete_gb_->GetValue());
    j.min_dest_free_bytes = gb_to_bytes(min_free_gb_->GetValue());
    j.max_single_archive_bytes = gb_to_bytes(max_one_gb_->GetValue());
    clamp_backup_job(j);
    return j;
  }

  void on_ok() {
    auto j = collect_unchecked();
    auto v = validate_backup_job(j, config_);
    if (!v.ok) {
      error_->SetLabel(wxString::FromUTF8(v.errors.front()));
      error_->Wrap(FromDIP(560));
      Layout();
      return;
    }
    if (!v.warnings.empty()) {
      wxString msg = L"Проверьте настройки:\n";
      for (const auto& w : v.warnings) msg += L"• " + wxString::FromUTF8(w) + L"\n";
      msg += L"\nСохранить всё равно?";
      if (!ask_confirm(this, L"Предупреждение", msg, L"Сохранить", false)) return;
    }
    result = std::move(j);
    accepted = true;
    EndModal(wxID_OK);
  }

  BackupJob job_;
  Config& config_;
  std::vector<std::string> server_ids_;
  std::vector<std::string> dest_server_ids_;
  wxTextCtrl* name_{};
  ThemedChoice* server_{};
  ThemedChoice* kind_{};
  wxTextCtrl* paths_{};
  wxTextCtrl* excludes_{};
  ThemedChoice* db_engine_{};
  wxTextCtrl* db_name_{};
  wxTextCtrl* db_user_{};
  wxTextCtrl* db_password_{};
  wxTextCtrl* db_port_{};
  wxTextCtrl* docker_{};
  wxRadioButton* dest_same_{};
  wxRadioButton* dest_other_{};
  wxRadioButton* dest_local_{};
  ThemedChoice* dest_server_{};
  wxTextCtrl* dest_path_{};
  ThemedCheckBox* enabled_{};
  ThemedChoice* schedule_{};
  ThemedChoice* interval_{};
  wxTextCtrl* interval_sec_{};
  wxTextCtrl* max_gb_{};
  wxTextCtrl* overflow_pct_{};
  ThemedCheckBox* act_warn_{};
  ThemedCheckBox* act_oldest_{};
  ThemedCheckBox* act_bytes_{};
  ThemedCheckBox* act_stride_{};
  wxTextCtrl* delete_gb_{};
  wxTextCtrl* stride_del_{};
  wxTextCtrl* stride_keep_{};
  wxTextCtrl* min_free_gb_{};
  wxTextCtrl* max_one_gb_{};
  wxStaticText* preview_{};
  wxStaticText* error_{};
};

}  // namespace

bool edit_backup_job_dialog(wxWindow* parent, BackupJob& job, Config& config, AppSettings* settings,
                            std::function<void()> persist) {
  BackupJobDialog dlg(parent, job, config, settings, std::move(persist));
  dlg.ShowModal();
  if (!dlg.accepted) return false;
  job = std::move(dlg.result);
  return true;
}

BackupWindow::BackupWindow(wxWindow* parent, Config* config, BackupMonitor* monitor, AppSettings* settings,
                           std::function<void()> persist, std::string preferred_server_id)
    : wxPanel(parent),
      config_(config),
      monitor_(monitor),
      settings_(settings),
      persist_(std::move(persist)),
      preferred_server_id_(std::move(preferred_server_id)) {
  SetName(L"card-page");
  auto* root = new wxBoxSizer(wxVERTICAL);
  hint_ = muted_label(
      this,
      L"Это копии данных на VPS, не локальные копии config.json (Настройки → Данные). "
      L"Клиентский автозапуск работает только пока открыт FaTTY. Можно запускать вручную в любой момент.");
  root->Add(hint_, 0, wxALL, 8);

  show_all_cb_ = new ThemedCheckBox(this, wxID_ANY, L"Показать все VPS");
  show_all_cb_->SetValue(settings_ && settings_->backup_show_all_servers);
  root->Add(show_all_cb_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

  list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                         wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxBORDER_NONE);
  list_->AppendColumn(L"Название", wxLIST_FORMAT_LEFT, FromDIP(160));
  list_->AppendColumn(L"VPS", wxLIST_FORMAT_LEFT, FromDIP(120));
  list_->AppendColumn(L"Тип", wxLIST_FORMAT_LEFT, FromDIP(100));
  list_->AppendColumn(L"Куда", wxLIST_FORMAT_LEFT, FromDIP(120));
  list_->AppendColumn(L"Статус", wxLIST_FORMAT_LEFT, FromDIP(90));
  list_->AppendColumn(L"Авто", wxLIST_FORMAT_LEFT, FromDIP(50));
  root->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

  auto* buttons = new wxBoxSizer(wxHORIZONTAL);
  auto* add = make_button(this, L"Добавить", BtnIcon::Plus);
  auto* edit = make_button(this, L"Изменить", BtnIcon::Pencil);
  auto* dup = make_button(this, L"Дублировать", BtnIcon::Copy);
  auto* del = make_button(this, L"Удалить", BtnIcon::Trash);
  run_btn_ = make_button(this, L"Запустить", BtnIcon::Play);
  cron_btn_ = make_button(this, L"Поставить cron", BtnIcon::Save);
  uncron_btn_ = make_button(this, L"Снять cron", BtnIcon::Trash);
  buttons->Add(add, 0, wxRIGHT, 6);
  buttons->Add(edit, 0, wxRIGHT, 6);
  buttons->Add(dup, 0, wxRIGHT, 6);
  buttons->Add(del, 0, wxRIGHT, 6);
  buttons->Add(run_btn_, 0, wxRIGHT, 6);
  buttons->Add(cron_btn_, 0, wxRIGHT, 6);
  buttons->Add(uncron_btn_, 0);
  root->Add(buttons, 0, wxALL, 8);

  log_ = new wxTextCtrl(this, wxID_ANY, L"", wxDefaultPosition, FromDIP(wxSize(-1, 140)),
                        wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
  root->Add(new wxStaticText(this, wxID_ANY, L"Журнал запуска"), 0, wxLEFT | wxRIGHT, 8);
  root->Add(log_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

  SetSizer(root);
  apply_dark(this);

  add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { add_job(); });
  edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { edit_selected(); });
  dup->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { duplicate_selected(); });
  del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { delete_selected(); });
  run_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { run_selected(); });
  cron_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { install_cron_selected(); });
  uncron_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { uninstall_cron_selected(); });
  show_all_cb_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
    if (settings_) {
      settings_->backup_show_all_servers = show_all_cb_->GetValue();
      if (persist_) persist_();
    }
    rebuild_list();
  });
  list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { edit_selected(); });
  rebuild_list();
}

BackupWindow::~BackupWindow() { *alive_ = false; }

void BackupWindow::reload() { rebuild_list(); }

void BackupWindow::set_preferred_server(const std::string& server_id) {
  preferred_server_id_ = server_id;
  rebuild_list();
}

void BackupWindow::append_log(const std::string& line) {
  if (!log_) return;
  log_->AppendText(wxString::FromUTF8(line));
  if (line.empty() || line.back() != '\n') log_->AppendText(L"\n");
}

std::string BackupWindow::default_source_server_id() const {
  if (!config_ || config_->servers.empty()) return {};
  if (!preferred_server_id_.empty() && config_->server_by_id(preferred_server_id_)) {
    return preferred_server_id_;
  }
  return config_->servers.front().id;
}

void BackupWindow::rebuild_list() {
  list_->DeleteAllItems();
  row_job_ids_.clear();
  if (!config_) return;
  const bool show_all = settings_ && settings_->backup_show_all_servers;
  long row = 0;
  for (const auto& j : config_->backup_jobs) {
    if (!show_all && !preferred_server_id_.empty() && j.source_server_id != preferred_server_id_) {
      continue;
    }
    const Server* s = config_->server_by_id(j.source_server_id);
    list_->InsertItem(row, wxString::FromUTF8(j.name));
    list_->SetItem(row, 1, wxString::FromUTF8(s ? (s->name.empty() ? s->host : s->name) : "?"));
    list_->SetItem(row, 2, wxString::FromUTF8(backup_kind_label(j.kind)));
    list_->SetItem(row, 3, wxString::FromUTF8(backup_dest_label(j.dest_kind)));
    wxString st = wxString::FromUTF8(j.last_status.empty() ? "—" : j.last_status);
    if (!j.last_error.empty()) st += L" / " + wxString::FromUTF8(clip(j.last_error, 40));
    list_->SetItem(row, 4, st);
    list_->SetItem(row, 5, j.enabled ? L"да" : L"нет");
    row_job_ids_.push_back(j.id);
    ++row;
  }
}

BackupJob* BackupWindow::selected_job() {
  long i = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (i < 0 || !config_ || i >= static_cast<long>(row_job_ids_.size())) return nullptr;
  return config_->backup_job_by_id(row_job_ids_[static_cast<std::size_t>(i)]);
}

void BackupWindow::add_job() {
  if (!config_ || config_->servers.empty()) {
    wxMessageBox(L"Сначала добавьте VPS.", L"Резервные копии");
    return;
  }
  BackupJob job = BackupJob::make_new(default_source_server_id());
  if (!edit_backup_job_dialog(this, job, *config_, settings_, persist_)) return;
  if (!ask_confirm(this, L"Создать задание", L"Создать задание «" + wxString::FromUTF8(job.name) + L"»?", L"Создать",
                   false))
    return;
  config_->backup_jobs.push_back(std::move(job));
  if (persist_) persist_();
  rebuild_list();
}

void BackupWindow::duplicate_selected() {
  auto* job = selected_job();
  if (!job) return;
  std::vector<std::string> names;
  for (const auto& j : config_->backup_jobs) names.push_back(j.name);
  auto clone = job->duplicate(copy_name(job->name, names));
  config_->backup_jobs.push_back(std::move(clone));
  if (persist_) persist_();
  rebuild_list();
}

void BackupWindow::edit_selected() {
  auto* job = selected_job();
  if (!job) return;
  BackupJob copy = *job;
  if (!edit_backup_job_dialog(this, copy, *config_, settings_, persist_)) return;
  *job = std::move(copy);
  if (persist_) persist_();
  rebuild_list();
}

void BackupWindow::delete_selected() {
  auto* job = selected_job();
  if (!job) return;
  if (!ask_confirm(this, L"Удалить задание", L"Удалить «" + wxString::FromUTF8(job->name) + L"»? Файлы копий на "
                                                           L"диске не трогаем.",
                   L"Удалить", false, BtnIcon::Trash))
    return;
  const auto id = job->id;
  config_->backup_jobs.erase(std::remove_if(config_->backup_jobs.begin(), config_->backup_jobs.end(),
                                            [&](const BackupJob& j) { return j.id == id; }),
                             config_->backup_jobs.end());
  if (persist_) persist_();
  rebuild_list();
}

void BackupWindow::run_selected() {
  auto* job = selected_job();
  if (!job || !monitor_) return;
  if (!ask_confirm(this, L"Запуск", L"Запустить резервное копирование «" + wxString::FromUTF8(job->name) + L"» сейчас?",
                   L"Запустить", true, BtnIcon::Play))
    return;
  log_->AppendText(L"\n— Запуск " + wxString::FromUTF8(job->name) + L" —\n");
  monitor_->run_now(job->id);
}

void BackupWindow::install_cron_selected() {
  auto* job = selected_job();
  if (!job) return;
  if (job->dest_kind == BackupDestKind::LocalPc) {
    wxMessageBox(L"Для копий на этот компьютер cron недоступен.", L"Cron");
    return;
  }
  if (!ask_confirm(this, L"Cron", L"Установить скрипт и запись crontab на исходном VPS?", L"Установить", false))
    return;
  BackupEngine engine;
  auto result = engine.install_cron(*job, *config_, [this](const std::string& line) {
    log_->AppendText(wxString::FromUTF8(line));
    if (line.empty() || line.back() != '\n') log_->AppendText(L"\n");
  });
  if (persist_) persist_();
  rebuild_list();
  if (!result.ok) wxMessageBox(wxString::FromUTF8(result.error), L"Cron");
}

void BackupWindow::uninstall_cron_selected() {
  auto* job = selected_job();
  if (!job) return;
  if (!ask_confirm(this, L"Снять cron", L"Удалить crontab-запись и скрипт этого задания с VPS?", L"Снять", false,
                   BtnIcon::Trash))
    return;
  BackupEngine engine;
  auto result = engine.uninstall_cron(*job, *config_, [this](const std::string& line) {
    log_->AppendText(wxString::FromUTF8(line));
    if (line.empty() || line.back() != '\n') log_->AppendText(L"\n");
  });
  if (persist_) persist_();
  rebuild_list();
  if (!result.ok) wxMessageBox(wxString::FromUTF8(result.error), L"Cron");
}

}  // namespace fatty

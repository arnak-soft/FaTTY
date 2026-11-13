#include "core/backup_journal.hpp"

#include "core/journal.hpp"
#include "core/paths.hpp"
#include "core/util.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fatty {
using json = nlohmann::json;

namespace {

constexpr int kTrimSlack = 100;
constexpr int kErrorMax = 4096;

std::string clip_utf8(std::string text, std::size_t limit) {
  if (text.size() <= limit) return text;
  std::size_t n = limit;
  while (n > 0 && (static_cast<unsigned char>(text[n - 1]) & 0xC0) == 0x80) --n;
  if (n > 0 && (static_cast<unsigned char>(text[n - 1]) & 0x80)) --n;
  text.resize(n);
  text += "\n…";
  return text;
}

std::string status_ru(const std::string& status) {
  if (status == "ok") return "OK";
  if (status == "warned") return "с предупреждением";
  if (status == "skipped") return "пропущено";
  if (status == "error") return "ошибка";
  return status.empty() ? "—" : status;
}

json entry_to_json(const BackupJournalEntry& e) {
  return {
      {"id", e.id},
      {"started_at", e.started_at},
      {"finished_at", e.finished_at},
      {"duration_sec", e.duration_sec},
      {"job_id", e.job_id},
      {"job_name", e.job_name},
      {"server_id", e.server_id},
      {"server_name", e.server_name},
      {"kind_label", e.kind_label},
      {"dest_label", e.dest_label},
      {"status", e.status},
      {"error", e.error},
      {"archive_name", e.archive_name},
      {"archive_bytes", e.archive_bytes},
      {"manual", e.manual},
      {"log", e.log},
  };
}

BackupJournalEntry parse_entry(const json& raw) {
  BackupJournalEntry e;
  e.id = raw.value("id", "");
  if (e.id.empty()) e.id = new_uuid();
  e.started_at = raw.value("started_at", "");
  e.finished_at = raw.value("finished_at", "");
  e.duration_sec = std::max(0.0, raw.value("duration_sec", 0.0));
  e.job_id = raw.value("job_id", "");
  e.job_name = raw.value("job_name", "");
  e.server_id = raw.value("server_id", "");
  e.server_name = raw.value("server_name", "");
  e.kind_label = raw.value("kind_label", "");
  e.dest_label = raw.value("dest_label", "");
  e.status = raw.value("status", "error");
  e.error = raw.value("error", "");
  e.archive_name = raw.value("archive_name", "");
  e.archive_bytes = raw.value("archive_bytes", static_cast<std::int64_t>(0));
  e.manual = raw.value("manual", false);
  e.log = raw.value("log", "");
  return e;
}

}  // namespace

std::string BackupJournalEntry::started_display() const {
  auto t = started_at;
  auto p = t.find('T');
  if (p != std::string::npos) t[p] = ' ';
  if (t.size() > 19) t.resize(19);
  return t.empty() ? "—" : t;
}

std::string BackupJournalEntry::status_display() const { return status_ru(status); }

std::string BackupJournalEntry::as_text() const {
  std::ostringstream ss;
  ss << "Время: " << started_display() << "\n";
  if (!finished_at.empty()) {
    auto fin = finished_at;
    auto p = fin.find('T');
    if (p != std::string::npos) fin[p] = ' ';
    if (fin.size() > 19) fin.resize(19);
    ss << "Завершено: " << fin << "\n";
  }
  if (duration_sec > 0) ss << "Длительность: " << format_duration(duration_sec) << "\n";
  ss << "Задание: " << (job_name.empty() ? "—" : job_name) << "\n";
  ss << "VPS: " << (server_name.empty() ? "—" : server_name) << "\n";
  ss << "Тип: " << (kind_label.empty() ? "—" : kind_label) << "\n";
  ss << "Куда: " << (dest_label.empty() ? "—" : dest_label) << "\n";
  ss << "Запуск: " << (manual ? "вручную" : "по расписанию") << "\n";
  ss << "Результат: " << status_display() << "\n";
  if (!error.empty()) ss << "Ошибка: " << error << "\n";
  if (!archive_name.empty()) {
    ss << "Архив: " << archive_name;
    if (archive_bytes > 0) ss << " (" << archive_bytes << " байт)";
    ss << "\n";
  }
  ss << "\nЖурнал:\n" << (log.empty() ? "—" : log);
  return ss.str();
}

BackupJournal::BackupJournal(std::filesystem::path path, int max_entries_in)
    : max_entries(std::max(100, std::min(20000, max_entries_in))),
      path_(path.empty() ? backup_journal_path() : std::move(path)) {}

int BackupJournal::add_listener(std::function<void()> cb) {
  std::lock_guard lock(listeners_mutex_);
  int id = next_listener_id_++;
  listeners_.emplace_back(id, std::move(cb));
  return id;
}

void BackupJournal::remove_listener(int id) {
  std::lock_guard lock(listeners_mutex_);
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                  [id](const auto& l) { return l.first == id; }),
                   listeners_.end());
}

void BackupJournal::remove_listeners() {
  std::lock_guard lock(listeners_mutex_);
  listeners_.clear();
}

void BackupJournal::notify_listeners() {
  std::vector<std::function<void()>> callbacks;
  {
    std::lock_guard lock(listeners_mutex_);
    for (const auto& [id, cb] : listeners_) {
      (void)id;
      callbacks.push_back(cb);
    }
  }
  for (auto& cb : callbacks) {
    try {
      cb();
    } catch (...) {
    }
  }
}

void BackupJournal::append(BackupJournalEntry entry) {
  entry.error = clip(entry.error, kErrorMax);
  entry.log = clip_utf8(std::move(entry.log), kBackupJournalLogMax);
  if (entry.id.empty()) entry.id = new_uuid();
  if (entry.started_at.empty()) entry.started_at = now_iso();
  if (entry.finished_at.empty()) entry.finished_at = now_iso();
  const std::string line = entry_to_json(entry).dump(-1, ' ', false, json::error_handler_t::replace);
  {
    std::lock_guard lock(mutex_);
    std::filesystem::create_directories(path_.parent_path());
    std::ofstream out(path_, std::ios::binary | std::ios::app);
    out << line << "\n";
    try {
      auto text = read_text_file(path_);
      std::vector<std::string> lines;
      std::istringstream ss(text);
      std::string row;
      while (std::getline(ss, row)) {
        if (!trim(row).empty()) lines.push_back(row);
      }
      if (static_cast<int>(lines.size()) > max_entries + kTrimSlack) {
        std::ostringstream kept;
        const int start = static_cast<int>(lines.size()) - max_entries;
        for (int i = start; i < static_cast<int>(lines.size()); ++i) {
          kept << lines[static_cast<std::size_t>(i)] << "\n";
        }
        atomic_write_text(path_, kept.str());
      }
    } catch (...) {
    }
  }
  notify_listeners();
}

std::vector<BackupJournalEntry> BackupJournal::load(int limit) const {
  std::lock_guard lock(mutex_);
  std::vector<BackupJournalEntry> entries;
  if (!std::filesystem::exists(path_)) return entries;
  try {
    auto text = read_text_file(path_);
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
      line = trim(line);
      if (line.empty()) continue;
      try {
        auto parsed = json::parse(line);
        if (parsed.is_object()) entries.push_back(parse_entry(parsed));
      } catch (...) {
      }
    }
  } catch (...) {
    return {};
  }
  if (limit > 0 && static_cast<int>(entries.size()) > limit) {
    entries.erase(entries.begin(), entries.end() - limit);
  }
  std::reverse(entries.begin(), entries.end());
  return entries;
}

bool BackupJournal::remove(const std::string& id) {
  const auto want = trim(id);
  if (want.empty()) return false;
  bool removed = false;
  {
    std::lock_guard lock(mutex_);
    if (!std::filesystem::exists(path_)) return false;
    try {
      auto text = read_text_file(path_);
      std::ostringstream kept;
      std::istringstream ss(text);
      std::string line;
      while (std::getline(ss, line)) {
        const auto row = trim(line);
        if (row.empty()) continue;
        try {
          auto parsed = json::parse(row);
          if (parsed.is_object() && parsed.value("id", "") == want) {
            removed = true;
            continue;
          }
        } catch (...) {
        }
        kept << row << "\n";
      }
      if (removed) atomic_write_text(path_, kept.str());
    } catch (...) {
      return false;
    }
  }
  if (removed) notify_listeners();
  return removed;
}

void BackupJournal::clear() {
  {
    std::lock_guard lock(mutex_);
    try {
      atomic_write_text(path_, "");
    } catch (...) {
    }
  }
  notify_listeners();
}

std::string BackupJournal::export_text(const std::vector<BackupJournalEntry>* entries) const {
  const auto list = entries ? *entries : load();
  std::ostringstream ss;
  for (const auto& e : list) {
    ss << e.as_text() << "\n\n--------------------\n\n";
  }
  return ss.str();
}

}  // namespace fatty

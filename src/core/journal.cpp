#include "core/journal.hpp"

#include "core/paths.hpp"
#include "core/util.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fatty {
using json = nlohmann::json;

namespace {

constexpr int kTrimSlack = 200;
constexpr int kCommandMax = 16384;
constexpr int kErrorMax = 4096;

std::tm local_tm(std::chrono::system_clock::time_point tp) {
  std::time_t t = std::chrono::system_clock::to_time_t(tp);
  std::tm out{};
#ifdef _WIN32
  localtime_s(&out, &t);
#else
  localtime_r(&t, &out);
#endif
  return out;
}

std::optional<std::chrono::system_clock::time_point> parse_iso(const std::string& raw) {
  if (raw.empty()) {
    return std::nullopt;
  }
  std::tm tm{};
  std::istringstream ss(raw);
  ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  if (ss.fail()) {
    return std::nullopt;
  }
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

json entry_to_json(const JournalEntry& e) {
  json j = {
      {"id", e.id},
      {"started_at", e.started_at},
      {"finished_at", e.finished_at},
      {"duration_sec", e.duration_sec},
      {"server_id", e.server_id},
      {"server_name", e.server_name},
      {"host", e.host},
      {"port", e.port},
      {"username", e.username},
      {"command_id", e.command_id},
      {"title", e.title},
      {"command", e.command},
      {"cwd", e.cwd},
      {"login_shell", e.login_shell},
      {"timeout_sec", e.timeout_sec},
      {"status", e.status},
      {"kind", e.kind},
      {"error", e.error},
  };
  if (e.exit_code) {
    j["exit_code"] = *e.exit_code;
  } else {
    j["exit_code"] = nullptr;
  }
  return j;
}

JournalEntry parse_entry(const json& raw) {
  JournalEntry e;
  e.id = raw.value("id", "");
  if (e.id.empty()) {
    e.id = new_uuid();
  }
  e.started_at = raw.value("started_at", "");
  e.finished_at = raw.value("finished_at", "");
  e.duration_sec = std::max(0.0, raw.value("duration_sec", 0.0));
  e.server_id = raw.value("server_id", "");
  e.server_name = raw.value("server_name", "");
  e.host = raw.value("host", "");
  e.port = raw.value("port", 22);
  e.username = raw.value("username", "");
  e.command_id = raw.value("command_id", "");
  e.title = raw.value("title", "");
  e.command = raw.value("command", "");
  e.cwd = raw.value("cwd", "");
  e.login_shell = raw.value("login_shell", true);
  e.timeout_sec = raw.value("timeout_sec", 180);
  if (raw.contains("exit_code") && !raw["exit_code"].is_null()) {
    try {
      e.exit_code = raw["exit_code"].get<int>();
    } catch (...) {
    }
  }
  e.status = raw.value("status", "error");
  e.kind = raw.value("kind", "command");
  e.error = raw.value("error", "");
  return e;
}

}  // namespace

std::string status_label(const std::string& status) {
  if (status == "ok") return "OK";
  if (status == "failed") return "ошибка";
  if (status == "timeout") return "таймаут";
  if (status == "cancelled") return "прервано";
  if (status == "error") return "сбой";
  return status;
}

std::string kind_label(const std::string& kind) {
  if (kind == "command") return "сохранённая";
  if (kind == "quick") return "разовая";
  if (kind == "test") return "проверка связи";
  return kind;
}

std::string now_iso() {
  auto now = std::chrono::system_clock::now();
  auto tm = local_tm(now);
  std::ostringstream ss;
  ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
  return ss.str();
}

std::string format_duration(double seconds) {
  if (seconds < 0) seconds = 0;
  if (seconds < 60) {
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(1);
    ss << seconds << " с";
    return ss.str();
  }
  int total = static_cast<int>(std::round(seconds));
  int minutes = total / 60;
  int sec = total % 60;
  if (minutes < 60) {
    return std::to_string(minutes) + " мин " + std::to_string(sec) + " с";
  }
  int hours = minutes / 60;
  minutes %= 60;
  return std::to_string(hours) + " ч " + std::to_string(minutes) + " мин";
}

std::string status_from_exit(int code) {
  if (code == 0) return "ok";
  if (code == 124) return "timeout";
  if (code == 130) return "cancelled";
  return "failed";
}

std::string JournalEntry::target() const {
  return username + "@" + host + ":" + std::to_string(port);
}

std::string JournalEntry::started_display() const {
  auto tp = parse_iso(started_at);
  if (!tp) {
    auto t = started_at;
    auto p = t.find('T');
    if (p != std::string::npos) t[p] = ' ';
    return t.empty() ? "—" : t.substr(0, 19);
  }
  auto tm = local_tm(*tp);
  std::ostringstream ss;
  ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return ss.str();
}

std::string JournalEntry::duration_display() const {
  return format_duration(duration_sec);
}

std::string JournalEntry::status_display() const {
  if (exit_code && (status == "ok" || status == "failed")) {
    return std::to_string(*exit_code);
  }
  return status_label(status);
}

std::string JournalEntry::command_preview(int limit) const {
  std::string text;
  for (char ch : command) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      if (!text.empty() && text.back() != ' ') text.push_back(' ');
    } else {
      text.push_back(ch);
    }
  }
  if (static_cast<int>(text.size()) > limit) {
    return text.substr(0, static_cast<std::size_t>(limit - 1)) + "…";
  }
  return text;
}

std::string JournalEntry::compact_when() const {
  auto tp = parse_iso(started_at);
  if (!tp) return "";
  auto tm = local_tm(*tp);
  auto now = local_tm(std::chrono::system_clock::now());
  std::ostringstream ss;
  if (tm.tm_year == now.tm_year && tm.tm_yday == now.tm_yday) {
    ss << std::put_time(&tm, "%H:%M");
  } else if (tm.tm_year == now.tm_year) {
    ss << std::put_time(&tm, "%d.%m %H:%M");
  } else {
    ss << std::put_time(&tm, "%d.%m.%y");
  }
  return ss.str();
}

std::string JournalEntry::last_run_label() const {
  std::string result;
  if (status == "ok") {
    result = "OK";
  } else if (exit_code && status == "failed") {
    result = std::to_string(*exit_code);
  } else {
    result = status_label(status);
  }
  auto when = compact_when();
  if (!when.empty()) {
    return result + " · " + when;
  }
  return result;
}

std::string JournalEntry::as_text() const {
  std::ostringstream ss;
  ss << "Время: " << started_display() << "\n";
  ss << "Длительность: " << duration_display() << "\n";
  ss << "VPS: " << server_name << "  (" << target() << ")\n";
  ss << "Тип: " << kind_label(kind) << "\n";
  ss << "Название: " << (title.empty() ? "—" : title) << "\n";
  if (!cwd.empty()) {
    ss << "Каталог: " << cwd << "\n";
  }
  std::string result = status_display();
  if (exit_code && status != "ok" && status != "failed") {
    result = status_label(status) + " (код " + std::to_string(*exit_code) + ")";
  }
  ss << "Результат: " << result << "\n";
  if (!error.empty()) {
    ss << "Ошибка: " << error << "\n";
  }
  ss << "Команда:\n" << (command.empty() ? "—" : command);
  return ss.str();
}

Journal::Journal(std::filesystem::path path, int max_entries_in)
    : max_entries(std::max(100, std::min(50000, max_entries_in))),
      path_(path.empty() ? journal_path() : std::move(path)) {}

int Journal::add_listener(std::function<void()> cb) {
  int id = next_listener_id_++;
  listeners_.emplace_back(id, std::move(cb));
  return id;
}

void Journal::remove_listener(int id) {
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                  [id](const auto& l) { return l.first == id; }),
                   listeners_.end());
}

void Journal::remove_listeners() {
  listeners_.clear();
}

void Journal::append(JournalEntry entry) {
  entry.command = clip(entry.command, kCommandMax);
  entry.error = clip(entry.error, kErrorMax);
  if (entry.id.empty()) {
    entry.id = new_uuid();
  }
  std::string line = entry_to_json(entry).dump();
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
        int start = static_cast<int>(lines.size()) - max_entries;
        for (int i = start; i < static_cast<int>(lines.size()); ++i) {
          kept << lines[static_cast<std::size_t>(i)] << "\n";
        }
        atomic_write_text(path_, kept.str());
      }
    } catch (...) {
    }
  }
  for (auto& [id, cb] : listeners_) {
    (void)id;
    try {
      cb();
    } catch (...) {
    }
  }
}

std::vector<JournalEntry> Journal::load(int limit) const {
  std::lock_guard lock(mutex_);
  std::vector<JournalEntry> entries;
  if (!std::filesystem::exists(path_)) {
    return entries;
  }
  try {
    auto text = read_text_file(path_);
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
      line = trim(line);
      if (line.empty()) continue;
      try {
        auto parsed = json::parse(line);
        if (parsed.is_object()) {
          entries.push_back(parse_entry(parsed));
        }
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

std::map<std::string, JournalEntry> Journal::latest_by_command_id() const {
  std::map<std::string, JournalEntry> latest;
  for (const auto& item : load()) {
    auto cid = trim(item.command_id);
    if (!cid.empty() && !latest.count(cid)) {
      latest.emplace(cid, item);
    }
  }
  return latest;
}

bool Journal::remove(const std::string& id) {
  auto target = trim(id);
  if (target.empty()) return false;
  bool removed = false;
  {
    std::lock_guard lock(mutex_);
    if (!std::filesystem::exists(path_)) return false;
    std::vector<std::string> kept;
    try {
      auto text = read_text_file(path_);
      std::istringstream ss(text);
      std::string line;
      while (std::getline(ss, line)) {
        auto row = trim(line);
        if (row.empty()) continue;
        bool drop = false;
        try {
          auto parsed = json::parse(row);
          if (parsed.is_object() && parsed.value("id", "") == target) {
            drop = true;
          }
        } catch (...) {
        }
        if (drop) {
          removed = true;
          continue;
        }
        kept.push_back(std::move(row));
      }
    } catch (...) {
      return false;
    }
    if (!removed) return false;
    std::ostringstream out;
    for (const auto& row : kept) {
      out << row << "\n";
    }
    atomic_write_text(path_, out.str());
  }
  for (auto& [id, cb] : listeners_) {
    (void)id;
    try {
      cb();
    } catch (...) {
    }
  }
  return true;
}

void Journal::clear() {
  {
    std::lock_guard lock(mutex_);
    atomic_write_text(path_, "");
  }
  for (auto& [id, cb] : listeners_) {
    (void)id;
    try {
      cb();
    } catch (...) {
    }
  }
}

std::string Journal::export_text(const std::vector<JournalEntry>* entries) const {
  auto data = entries ? *entries : load();
    std::ostringstream ss;
    const char* sep = "\n\n────────────────────────────────────────────────\n\n";
    for (std::size_t i = 0; i < data.size(); ++i) {
      if (i) ss << sep;
      ss << data[i].as_text();
    }
  return ss.str();
}

}  // namespace fatty

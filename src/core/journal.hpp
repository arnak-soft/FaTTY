#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <filesystem>

namespace fatty {

struct JournalEntry {
  std::string id;
  std::string started_at;
  std::string finished_at;
  double duration_sec = 0;
  std::string server_id;
  std::string server_name;
  std::string host;
  int port = 22;
  std::string username;
  std::string command_id;
  std::string title;
  std::string command;
  std::string cwd;
  bool login_shell = true;
  int timeout_sec = 180;
  std::optional<int> exit_code;
  std::string status = "error";
  std::string kind = "command";
  std::string error;

  std::string target() const;
  std::string started_display() const;
  std::string duration_display() const;
  std::string status_display() const;
  std::string command_preview(int limit = 80) const;
  std::string compact_when() const;
  std::string last_run_label() const;
  std::string as_text() const;
};

std::string now_iso();
std::string format_duration(double seconds);
std::string status_from_exit(int code);
std::string status_label(const std::string& status);
std::string kind_label(const std::string& kind);

class Journal {
 public:
  explicit Journal(std::filesystem::path path = {}, int max_entries = 5000);

  void add_listener(std::function<void()> cb);
  void remove_listeners();
  void append(JournalEntry entry);
  std::vector<JournalEntry> load(int limit = 5000) const;
  std::map<std::string, JournalEntry> latest_by_command_id() const;
  void clear();
  std::string export_text(const std::vector<JournalEntry>* entries = nullptr) const;

  int max_entries = 5000;

 private:
  std::filesystem::path path_;
  mutable std::mutex mutex_;
  std::vector<std::function<void()>> listeners_;
};

}  // namespace fatty

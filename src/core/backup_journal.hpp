#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace fatty {

struct BackupJournalEntry {
  std::string id;
  std::string started_at;
  std::string finished_at;
  double duration_sec = 0;
  std::string job_id;
  std::string job_name;
  std::string server_id;
  std::string server_name;
  std::string kind_label;
  std::string dest_label;
  std::string status = "error";
  std::string error;
  std::string archive_name;
  std::int64_t archive_bytes = 0;
  bool manual = false;
  std::string log;

  std::string started_display() const;
  std::string status_display() const;
  std::string as_text() const;
};

inline constexpr std::size_t kBackupJournalLogMax = 512 * 1024;

class BackupJournal {
 public:
  explicit BackupJournal(std::filesystem::path path = {}, int max_entries = 2000);

  int add_listener(std::function<void()> cb);
  void remove_listener(int id);
  void remove_listeners();
  void append(BackupJournalEntry entry);
  std::vector<BackupJournalEntry> load(int limit = 2000) const;
  bool remove(const std::string& id);
  void clear();
  std::string export_text(const std::vector<BackupJournalEntry>* entries = nullptr) const;

  int max_entries = 2000;

 private:
  void notify_listeners();

  std::filesystem::path path_;
  mutable std::mutex mutex_;
  std::mutex listeners_mutex_;
  std::vector<std::pair<int, std::function<void()>>> listeners_;
  int next_listener_id_ = 1;
};

}  // namespace fatty

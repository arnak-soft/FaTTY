#include "tests/test.hpp"
#include "core/backup_journal.hpp"
#include "core/util.hpp"

#include <filesystem>

using namespace fatty;

namespace fatty::test {

void test_backup_journal() {
  expect(format_unix_local(0).empty(), "zero unix empty");
  expect(format_unix_local(1731490000.0).size() == 19, "format_unix length");

  const auto dir = std::filesystem::temp_directory_path() / ("fatty-bj-" + new_uuid());
  std::filesystem::create_directories(dir);
  const auto path = dir / "backup_journal.jsonl";

  BackupJournal journal(path, 100);
  BackupJournalEntry e;
  e.job_name = "Резерв: test";
  e.server_name = "Argentum";
  e.kind_label = "Папки";
  e.dest_label = "Этот компьютер";
  e.status = "ok";
  e.manual = true;
  e.log = "line1\nline2\n";
  e.started_at = "2026-11-13T12:21:47";
  e.finished_at = "2026-11-13T12:22:01";
  e.duration_sec = 14;
  journal.append(e);

  auto loaded = journal.load();
  expect(loaded.size() == 1, "one entry");
  expect(loaded[0].job_name == "Резерв: test", "job name kept");
  expect(loaded[0].started_display() == "2026-11-13 12:21:47", "started display");
  expect(loaded[0].status_display() == "OK", "status display");
  expect(loaded[0].as_text().find("line1") != std::string::npos, "log in as_text");

  const auto id = loaded[0].id;
  expect(journal.remove(id), "remove entry");
  expect(journal.load().empty(), "empty after remove");

  journal.append(e);
  journal.append(e);
  expect(journal.load().size() == 2, "two entries");
  journal.clear();
  expect(journal.load().empty(), "cleared");

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

}  // namespace fatty::test

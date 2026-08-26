#include "tests/test.hpp"
#include "core/journal.hpp"
#include "core/util.hpp"

#include <filesystem>

using namespace fatty;

namespace fatty::test {

void test_journal() {
  auto dir = std::filesystem::temp_directory_path() / ("fatty-test-" + new_uuid());
  std::filesystem::create_directories(dir);
  Journal journal(dir / "journal.jsonl", 100);
  JournalEntry e;
  e.server_name = "s";
  e.host = "h";
  e.port = 22;
  e.username = "u";
  e.command = "echo";
  e.command_id = "cid";
  e.status = "ok";
  e.exit_code = 0;
  e.started_at = now_iso();
  journal.append(e);
  auto items = journal.load();
  expect(items.size() == 1, "journal size");
  expect(status_from_exit(0) == "ok", "status ok");
  expect(status_from_exit(124) == "timeout", "status timeout");
  expect(status_from_exit(130) == "cancelled", "status cancel");
  auto latest = journal.latest_by_command_id();
  expect(latest.count("cid") == 1, "latest by id");

  JournalEntry other;
  other.id = "keep-me";
  other.server_name = "s";
  other.host = "h";
  other.command = "ls";
  other.status = "ok";
  journal.append(other);
  items = journal.load();
  expect(items.size() == 2, "journal size after second");
  std::string first_id = items.back().id;
  expect(journal.remove(first_id), "remove first entry");
  items = journal.load();
  expect(items.size() == 1, "journal size after remove");
  expect(items[0].id == "keep-me", "kept remaining entry");
  expect(!journal.remove("missing"), "remove missing id");
  items = journal.load();
  expect(items.size() == 1, "size unchanged after missing");

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

}  // namespace fatty::test

#include "tests/test.hpp"
#include "core/journal.hpp"
#include "core/util.hpp"

#include <filesystem>

using namespace fatty;
using namespace fatty::test;

void fatty::test::test_journal() {
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
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

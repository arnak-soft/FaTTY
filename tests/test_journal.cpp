#include "tests/test.hpp"
#include "core/journal.hpp"
#include "core/util.hpp"

#include <atomic>
#include <filesystem>
#include <thread>
#include <vector>

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

  // Слушатели: окно журнала подписывается и обязано отписаться при закрытии,
  // иначе колбэк уйдёт по разрушенному окну.
  int hits_a = 0;
  int hits_b = 0;
  int id_a = journal.add_listener([&] { ++hits_a; });
  journal.add_listener([&] { ++hits_b; });
  journal.append(other);
  expect(hits_a == 1 && hits_b == 1, "both listeners notified");
  journal.remove_listener(id_a);
  journal.append(other);
  expect(hits_a == 1, "removed listener is silent");
  expect(hits_b == 2, "remaining listener still notified");
  journal.remove_listener(id_a);
  journal.append(other);
  expect(hits_b == 3, "double remove is harmless");

  // append() зовётся из потока команды параллельно с подпиской из GUI-потока.
  {
    Journal concurrent(dir / "concurrent.jsonl", 1000);
    std::atomic<int> notified{0};
    std::thread writer([&] {
      JournalEntry w;
      w.server_name = "s";
      w.command = "echo";
      w.status = "ok";
      for (int i = 0; i < 50; ++i) concurrent.append(w);
    });
    std::vector<int> ids;
    for (int i = 0; i < 50; ++i) {
      ids.push_back(concurrent.add_listener([&] { ++notified; }));
    }
    for (int id : ids) concurrent.remove_listener(id);
    writer.join();
    expect(concurrent.load().size() == 50, "all concurrent appends stored");
  }

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

}  // namespace fatty::test

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
  e.output = "hello\nworld";
  e.started_at = now_iso();
  journal.append(e);
  auto items = journal.load();
  expect(items.size() == 1, "journal size");
  expect(items[0].output == "hello\nworld", "journal stores output");
  expect(items[0].as_text().find("hello") != std::string::npos, "as_text includes output");
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

  {
    Journal timed(dir / "avg.jsonl", 100);
    JournalEntry timed_a;
    timed_a.command_id = "avg-cmd";
    timed_a.command = "sleep";
    timed_a.status = "ok";
    timed_a.duration_sec = 2.0;
    timed.append(timed_a);
    JournalEntry timed_b = timed_a;
    timed_b.duration_sec = 6.0;
    timed.append(timed_b);
    JournalEntry timed_other;
    timed_other.command_id = "other";
    timed_other.command = "ls";
    timed_other.status = "ok";
    timed_other.duration_sec = 10.0;
    timed.append(timed_other);
    JournalEntry no_id;
    no_id.command = "quick";
    no_id.status = "ok";
    no_id.duration_sec = 99.0;
    timed.append(no_id);
    auto stats = timed.stats_by_command_id();
    expect(stats.count("avg-cmd") == 1, "avg command present");
    expect(stats["avg-cmd"].run_count == 2, "avg run count");
    expect(stats["avg-cmd"].average_sec > 3.9 && stats["avg-cmd"].average_sec < 4.1, "avg duration");
    expect(stats["other"].average_sec > 9.9 && stats["other"].average_sec < 10.1, "other avg isolated");
    expect(stats.count("") == 0, "empty command_id skipped");
    expect(format_duration(4.0) == "4.0 с", "format duration seconds");

    std::vector<JournalEntry> in_memory;
    JournalEntry newest;
    newest.command_id = "x";
    newest.duration_sec = 1;
    newest.status = "ok";
    JournalEntry older = newest;
    older.duration_sec = 5;
    older.status = "failed";
    in_memory.push_back(newest);
    in_memory.push_back(older);
    auto mem = command_run_stats(in_memory);
    expect(mem["x"].run_count == 2, "in-memory run count");
    expect(mem["x"].average_sec > 2.9 && mem["x"].average_sec < 3.1, "in-memory average");
    expect(mem["x"].latest.status == "ok", "newest-first is latest");
  }

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

}  // namespace fatty::test

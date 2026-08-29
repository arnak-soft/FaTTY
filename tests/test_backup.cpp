#include "tests/test.hpp"
#include "core/backup.hpp"
#include "core/util.hpp"

#include <filesystem>
#include <fstream>

using namespace fatty;

namespace fatty::test {

void test_backup() {
  auto dir = std::filesystem::temp_directory_path() / ("fatty-backup-" + new_uuid());
  auto src = dir / "config.json";
  auto dest = dir / "backups";
  std::filesystem::create_directories(dir);
  {
    std::ofstream f(src);
    f << "{\"ok\":true}\n";
  }

  AppSettings st;
  st.backup_enabled = false;
  auto r = maybe_backup_config(src, dest, st, 1'000'000, {0, 5});
  expect(!r.made, "disabled skips");
  expect(!std::filesystem::exists(dest) || std::filesystem::is_empty(dest), "disabled writes nothing");

  st.backup_enabled = true;
  r = maybe_backup_config(src, dest, st, 1'000'000, {3600, 5});
  expect(r.made, "first backup");
  expect(std::filesystem::exists(r.path), "backup file exists");
  expect(st.last_backup == 1'000'000, "last_backup stamped");

  r = maybe_backup_config(src, dest, st, 1'000'000 + 10, {3600, 5});
  expect(!r.made, "too soon skipped");

  r = maybe_backup_config(src, dest, st, 1'000'000 + 3600, {3600, 5});
  expect(r.made, "interval elapsed");

  st.last_backup = 0;
  for (int i = 0; i < 8; ++i) {
    r = maybe_backup_config(src, dest, st, 2'000'000 + i, {0, 5});
    expect(r.made, "interval 0 always copies");
  }
  int count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dest)) {
    if (entry.path().extension() == ".json") ++count;
  }
  expect(count == 5, "prune keeps 5");

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

}  // namespace fatty

#include "tests/test.hpp"
#include "core/quote.hpp"

using namespace fatty;

namespace fatty::test {

void test_quote() {
  expect(shlex_quote("abc") == "abc", "safe quote");
  expect(shlex_quote("") == "''", "empty quote");
  expect(shlex_quote("a b").find('\'') == 0, "space quoted");
  auto [remote, mark] = wrap_remote_command("echo hi", "/tmp", true);
  expect(remote.rfind("bash -lc ", 0) == 0, "login shell");
  auto [sh_remote, sh_mark] = wrap_remote_command("echo hi", "", false, "sh");
  expect(sh_remote.rfind("sh -c ", 0) == 0, "sh shell");
  expect(mark.rfind("FATTYCWD_", 0) == 0, "mark prefix");
  expect(remote.find("cd /tmp") != std::string::npos, "cwd in wrap");
  auto [home, mark_home] = wrap_remote_command("echo hi", "~/proj", false);
  expect(mark_home.rfind("FATTYCWD_", 0) == 0, "mark for tilde wrap");
  expect(home.find("cd ~/proj") != std::string::npos, "tilde cwd unquoted prefix");

  std::string out;
  CwdOutputFilter filt(mark, [&](const std::string& t) { out += t; });
  filt.feed("hello\n" + mark + "/home/app\npartial");
  filt.finish();
  expect(out.find("hello") != std::string::npos, "stdout kept");
  expect(filt.cwd() == "/home/app", "cwd captured");
  expect(out.find(mark) == std::string::npos, "mark hidden");
}

}  // namespace fatty::test

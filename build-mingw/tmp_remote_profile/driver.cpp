#include "tests/test.hpp"
#include <cstdio>
#include <exception>
namespace fatty::test { void test_remote_profile(); }
int main() {
  try { fatty::test::test_remote_profile(); }
  catch (const std::exception& e) { std::fprintf(stderr, "exception: %s\n", e.what()); return 1; }
  std::printf("passed=%d failed=%d\n", fatty::test::g_passed, fatty::test::g_failed);
  return fatty::test::g_failed ? 1 : 0;
}

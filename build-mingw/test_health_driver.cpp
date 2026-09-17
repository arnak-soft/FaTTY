#include "tests/test.hpp"

#include <cstdio>
#include <exception>

namespace fatty::test {
void test_health();
}

int main() {
  try {
    fatty::test::test_health();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "EXC: %s\n", e.what());
    return 1;
  }
  std::fprintf(stderr, "passed=%d failed=%d\n", fatty::test::g_passed, fatty::test::g_failed);
  return fatty::test::g_failed ? 1 : 0;
}

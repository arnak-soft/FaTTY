#include "tests/test.hpp"

#include <cstdio>
#include <exception>

namespace fatty::test {
void test_vault();
void test_store();
void test_quote();
void test_journal();
void test_backup();
void test_placeholders();
void test_updates();
}  // namespace fatty::test

int main() {
  using namespace fatty::test;
  try {
    test_vault();
    test_store();
    test_quote();
    test_journal();
    test_backup();
    test_placeholders();
    test_updates();
  } catch (const std::exception& exc) {
    std::fprintf(stderr, "exception: %s\n", exc.what());
    return 1;
  }
  std::printf("passed=%d failed=%d\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}

#pragma once

#include <cstdio>
#include <stdexcept>
#include <string>

namespace fatty::test {

inline int g_failed = 0;
inline int g_passed = 0;

inline void check(bool cond, const std::string& msg) {
  if (!cond) {
    ++g_failed;
    throw std::runtime_error(msg);
  }
  ++g_passed;
}

inline void expect(bool cond, const std::string& msg) {
  if (!cond) {
    ++g_failed;
    std::fprintf(stderr, "FAIL: %s\n", msg.c_str());
  } else {
    ++g_passed;
  }
}

}  // namespace fatty::test

#include "tests/test.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>
#endif

namespace fatty::test {

void test_smoke() {
#ifndef _WIN32
  return;
#else
  wchar_t self[MAX_PATH]{};
  if (!GetModuleFileNameW(nullptr, self, MAX_PATH)) {
    expect(false, "smoke: GetModuleFileNameW");
    return;
  }
  auto fatty = std::filesystem::path(self).replace_filename(L"FaTTY.exe");
  if (!std::filesystem::exists(fatty)) {
    expect(false, "smoke: FaTTY.exe next to fatty_tests");
    return;
  }

  std::wstring cmd = L"\"";
  cmd += fatty.wstring();
  cmd += L"\" --smoke-test";
  std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
  mutable_cmd.push_back(L'\0');

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, fatty.parent_path().wstring().c_str(),
                      &si, &pi)) {
    expect(false, "smoke: CreateProcessW");
    return;
  }

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  expect(code != STILL_ACTIVE && code == 0, "smoke: FaTTY --smoke-test exit code");
#endif
}

}  // namespace fatty::test

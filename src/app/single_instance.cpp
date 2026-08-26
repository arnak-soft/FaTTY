#include "app/single_instance.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <vector>

namespace fatty {
#ifdef _WIN32
namespace {
HANDLE g_mutex = nullptr;
constexpr wchar_t kMutex[] = L"Local\\FaTTY.SingleInstance";
constexpr wchar_t kProp[] = L"FaTTY";

BOOL CALLBACK enum_cb(HWND hwnd, LPARAM lparam) {
  auto* found = reinterpret_cast<std::vector<HWND>*>(lparam);
  if (GetPropW(hwnd, kProp)) found->push_back(hwnd);
  return TRUE;
}
}  // namespace

bool try_become_primary() {
  g_mutex = CreateMutexW(nullptr, FALSE, kMutex);
  if (!g_mutex) return true;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(g_mutex);
    g_mutex = nullptr;
    return false;
  }
  return true;
}

void register_window(void* hwnd) {
  if (hwnd) SetPropW(static_cast<HWND>(hwnd), kProp, HANDLE(1));
}

bool activate_existing() {
  std::vector<HWND> found;
  for (int i = 0; i < 20; ++i) {
    found.clear();
    EnumWindows(enum_cb, reinterpret_cast<LPARAM>(&found));
    if (!found.empty()) break;
    Sleep(50);
  }
  if (found.empty()) return false;
  HWND hwnd = found[0];
  HWND popup = GetLastActivePopup(hwnd);
  if (popup && IsWindowVisible(popup)) hwnd = popup;
  if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
  else ShowWindow(hwnd, SW_SHOW);
  SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
  HWND fg = GetForegroundWindow();
  DWORD cur = GetCurrentThreadId();
  DWORD fg_pid = 0, target_pid = 0;
  DWORD fg_th = fg ? GetWindowThreadProcessId(fg, &fg_pid) : 0;
  DWORD target_th = GetWindowThreadProcessId(hwnd, &target_pid);
  if (fg_th && fg_th != cur) AttachThreadInput(cur, fg_th, TRUE);
  if (target_th && target_th != cur) AttachThreadInput(cur, target_th, TRUE);
  BringWindowToTop(hwnd);
  SetForegroundWindow(hwnd);
  if (fg_th && fg_th != cur) AttachThreadInput(cur, fg_th, FALSE);
  if (target_th && target_th != cur) AttachThreadInput(cur, target_th, FALSE);
  return true;
}
#else
bool try_become_primary() { return true; }
void register_window(void*) {}
bool activate_existing() { return false; }
#endif
}  // namespace fatty

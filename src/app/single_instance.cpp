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
HANDLE g_close_event = nullptr;
HANDLE g_busy_event = nullptr;
HANDLE g_modal_event = nullptr;
constexpr wchar_t kMutex[] = L"Local\\FaTTY.SingleInstance";
constexpr wchar_t kCloseEvent[] = L"Local\\FaTTY.CloseForInstall";
constexpr wchar_t kBusyEvent[] = L"Local\\FaTTY.BusyWork";
constexpr wchar_t kModalEvent[] = L"Local\\FaTTY.OpenDialog";
constexpr wchar_t kProp[] = L"FaTTY";

BOOL CALLBACK enum_cb(HWND hwnd, LPARAM lparam) {
  auto* found = reinterpret_cast<std::vector<HWND>*>(lparam);
  if (GetPropW(hwnd, kProp)) found->push_back(hwnd);
  return TRUE;
}

// NULL DACL: elevated Setup.exe (high IL) должен SetEvent/Wait на событиях,
// которые создал FaTTY (medium IL). UIPI окна не пускает, события — да.
SECURITY_ATTRIBUTES everyone_sa() {
  static SECURITY_DESCRIPTOR sd;
  static SECURITY_ATTRIBUTES sa{};
  InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
  SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = &sd;
  sa.bInheritHandle = FALSE;
  return sa;
}

HANDLE create_event(const wchar_t* name) {
  auto sa = everyone_sa();
  return CreateEventW(&sa, TRUE, FALSE, name);
}

void set_signaled(HANDLE ev, bool on) {
  if (!ev) return;
  if (on) SetEvent(ev);
  else ResetEvent(ev);
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

void init_install_close_ipc() {
  if (!g_close_event) g_close_event = create_event(kCloseEvent);
  if (!g_busy_event) g_busy_event = create_event(kBusyEvent);
  if (!g_modal_event) g_modal_event = create_event(kModalEvent);
}

void shutdown_install_close_ipc() {
  auto close = [](HANDLE& h) {
    if (h) {
      CloseHandle(h);
      h = nullptr;
    }
  };
  close(g_close_event);
  close(g_busy_event);
  close(g_modal_event);
}

bool take_install_close_request() {
  if (!g_close_event) return false;
  if (WaitForSingleObject(g_close_event, 0) != WAIT_OBJECT_0) return false;
  ResetEvent(g_close_event);
  return true;
}

void publish_install_state(bool busy_work, bool open_dialog) {
  set_signaled(g_busy_event, busy_work);
  set_signaled(g_modal_event, open_dialog);
}
#else
bool try_become_primary() { return true; }
void register_window(void*) {}
bool activate_existing() { return false; }
void init_install_close_ipc() {}
void shutdown_install_close_ipc() {}
bool take_install_close_request() { return false; }
void publish_install_state(bool, bool) {}
#endif
}  // namespace fatty

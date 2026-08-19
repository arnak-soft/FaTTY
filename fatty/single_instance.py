"""One running copy of the app: mutex + activate the existing window."""

from __future__ import annotations

import ctypes
import sys
import time
from ctypes import wintypes

MUTEX_NAME = "Local\\FaTTY.SingleInstance"
PROP_NAME = "FaTTY"
_ERROR_ALREADY_EXISTS = 183
_SW_RESTORE = 9
_SW_SHOW = 5
_HWND_TOP = 0
_SWP_NOSIZE = 0x0001
_SWP_NOMOVE = 0x0002
_SWP_SHOWWINDOW = 0x0040

_mutex_handle: int | None = None

_WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_int, wintypes.HWND, wintypes.LPARAM)


def _user32():
    lib = ctypes.WinDLL("user32", use_last_error=True)
    lib.SetPropW.argtypes = [wintypes.HWND, wintypes.LPCWSTR, wintypes.HANDLE]
    lib.SetPropW.restype = wintypes.BOOL
    lib.GetPropW.argtypes = [wintypes.HWND, wintypes.LPCWSTR]
    lib.GetPropW.restype = wintypes.HANDLE
    lib.EnumWindows.argtypes = [_WNDENUMPROC, wintypes.LPARAM]
    lib.EnumWindows.restype = wintypes.BOOL
    lib.GetLastActivePopup.argtypes = [wintypes.HWND]
    lib.GetLastActivePopup.restype = wintypes.HWND
    lib.IsWindowVisible.argtypes = [wintypes.HWND]
    lib.IsWindowVisible.restype = wintypes.BOOL
    lib.IsIconic.argtypes = [wintypes.HWND]
    lib.IsIconic.restype = wintypes.BOOL
    lib.IsWindow.argtypes = [wintypes.HWND]
    lib.IsWindow.restype = wintypes.BOOL
    lib.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    lib.ShowWindow.restype = wintypes.BOOL
    lib.SetForegroundWindow.argtypes = [wintypes.HWND]
    lib.SetForegroundWindow.restype = wintypes.BOOL
    lib.BringWindowToTop.argtypes = [wintypes.HWND]
    lib.BringWindowToTop.restype = wintypes.BOOL
    lib.GetForegroundWindow.argtypes = []
    lib.GetForegroundWindow.restype = wintypes.HWND
    lib.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    lib.GetWindowThreadProcessId.restype = wintypes.DWORD
    lib.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
    lib.AttachThreadInput.restype = wintypes.BOOL
    lib.GetParent.argtypes = [wintypes.HWND]
    lib.GetParent.restype = wintypes.HWND
    lib.SetWindowPos.argtypes = [
        wintypes.HWND,
        wintypes.HWND,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        wintypes.UINT,
    ]
    lib.SetWindowPos.restype = wintypes.BOOL
    return lib


def _kernel32():
    lib = ctypes.WinDLL("kernel32", use_last_error=True)
    lib.CreateMutexW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.LPCWSTR]
    lib.CreateMutexW.restype = wintypes.HANDLE
    lib.CloseHandle.argtypes = [wintypes.HANDLE]
    lib.CloseHandle.restype = wintypes.BOOL
    lib.GetCurrentThreadId.argtypes = []
    lib.GetCurrentThreadId.restype = wintypes.DWORD
    return lib


def try_become_primary() -> bool:
    """True if this process should continue; False if another copy already runs."""
    global _mutex_handle
    if _mutex_handle is not None:
        return True
    if sys.platform != "win32":
        return True
    kernel32 = _kernel32()
    handle = kernel32.CreateMutexW(None, False, MUTEX_NAME)
    if not handle:
        return True
    if ctypes.get_last_error() == _ERROR_ALREADY_EXISTS:
        kernel32.CloseHandle(handle)
        return False
    _mutex_handle = int(handle)
    return True


def hwnd_of(widget) -> int:
    widget.update_idletasks()
    inner = wintypes.HWND(int(widget.winfo_id()))
    parent = _user32().GetParent(inner)
    return int(parent or inner)


def register_window(widget) -> None:
    if sys.platform != "win32":
        return
    try:
        hwnd = hwnd_of(widget)
        _user32().SetPropW(wintypes.HWND(hwnd), PROP_NAME, wintypes.HANDLE(1))
    except Exception:
        pass


def _tagged_windows() -> list[int]:
    found: list[int] = []
    user32 = _user32()

    def _cb(hwnd, _lparam):
        if user32.GetPropW(hwnd, PROP_NAME):
            found.append(int(hwnd))
        return 1

    callback = _WNDENUMPROC(_cb)
    user32.EnumWindows(callback, 0)
    return found


def _pick_window(candidates: list[int]) -> int | None:
    if not candidates:
        return None
    user32 = _user32()
    visible = [hwnd for hwnd in candidates if user32.IsWindowVisible(wintypes.HWND(hwnd))]
    if not visible:
        return None
    hwnd = visible[0]
    popup = int(user32.GetLastActivePopup(wintypes.HWND(hwnd)) or 0)
    if popup and user32.IsWindowVisible(wintypes.HWND(popup)):
        return popup
    return hwnd


def _activate_hwnd(hwnd: int) -> None:
    user32 = _user32()
    kernel32 = _kernel32()
    handle = wintypes.HWND(hwnd)
    if not user32.IsWindow(handle):
        return
    if user32.IsIconic(handle):
        user32.ShowWindow(handle, _SW_RESTORE)
    else:
        user32.ShowWindow(handle, _SW_SHOW)
    user32.SetWindowPos(
        handle,
        wintypes.HWND(_HWND_TOP),
        0,
        0,
        0,
        0,
        _SWP_NOMOVE | _SWP_NOSIZE | _SWP_SHOWWINDOW,
    )
    foreground = user32.GetForegroundWindow()
    current_thread = kernel32.GetCurrentThreadId()
    fg_pid = wintypes.DWORD(0)
    target_pid = wintypes.DWORD(0)
    fg_thread = user32.GetWindowThreadProcessId(foreground, ctypes.byref(fg_pid)) if foreground else 0
    target_thread = user32.GetWindowThreadProcessId(handle, ctypes.byref(target_pid))
    if fg_thread and fg_thread != current_thread:
        user32.AttachThreadInput(current_thread, fg_thread, True)
    if target_thread and target_thread != current_thread:
        user32.AttachThreadInput(current_thread, target_thread, True)
    user32.BringWindowToTop(handle)
    user32.SetForegroundWindow(handle)
    if fg_thread and fg_thread != current_thread:
        user32.AttachThreadInput(current_thread, fg_thread, False)
    if target_thread and target_thread != current_thread:
        user32.AttachThreadInput(current_thread, target_thread, False)


def activate_existing() -> bool:
    if sys.platform != "win32":
        return False
    hwnd: int | None = None
    for _ in range(20):
        hwnd = _pick_window(_tagged_windows())
        if hwnd:
            break
        time.sleep(0.05)
    if not hwnd:
        return False
    _activate_hwnd(hwnd)
    return True

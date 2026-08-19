"""Small Windows shell icons for the file list (folder / type by extension)."""

from __future__ import annotations

import ctypes
import sys
import tkinter as tk
from ctypes import wintypes
from pathlib import Path

from fatty.sftp import RemoteEntry

_SHGFI_ICON = 0x00000100
_SHGFI_SMALLICON = 0x00000001
_SHGFI_USEFILEATTRIBUTES = 0x00000010
_FILE_ATTRIBUTE_DIRECTORY = 0x00000010
_FILE_ATTRIBUTE_NORMAL = 0x00000080
_DI_NORMAL = 0x0003
_DIB_RGB_COLORS = 0
_BI_RGB = 0
_SM_CXSMICON = 49
_APIS_READY = False


def _configure_winapi() -> None:
    global _APIS_READY
    if _APIS_READY or sys.platform != "win32":
        return
    shell32 = ctypes.windll.shell32
    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32
    shell32.SHGetFileInfoW.argtypes = [
        wintypes.LPCWSTR,
        wintypes.DWORD,
        ctypes.POINTER(_SHFILEINFOW),
        wintypes.UINT,
        wintypes.UINT,
    ]
    shell32.SHGetFileInfoW.restype = ctypes.c_size_t
    user32.DrawIconEx.argtypes = [
        wintypes.HDC,
        ctypes.c_int,
        ctypes.c_int,
        wintypes.HICON,
        ctypes.c_int,
        ctypes.c_int,
        wintypes.UINT,
        wintypes.HANDLE,
        wintypes.UINT,
    ]
    user32.DrawIconEx.restype = wintypes.BOOL
    user32.DestroyIcon.argtypes = [wintypes.HICON]
    user32.DestroyIcon.restype = wintypes.BOOL
    user32.GetDC.argtypes = [wintypes.HWND]
    user32.GetDC.restype = wintypes.HDC
    user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
    user32.ReleaseDC.restype = ctypes.c_int
    gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
    gdi32.CreateCompatibleDC.restype = wintypes.HDC
    gdi32.CreateDIBSection.argtypes = [
        wintypes.HDC,
        ctypes.c_void_p,
        wintypes.UINT,
        ctypes.POINTER(ctypes.c_void_p),
        wintypes.HANDLE,
        wintypes.DWORD,
    ]
    gdi32.CreateDIBSection.restype = wintypes.HBITMAP
    gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
    gdi32.SelectObject.restype = wintypes.HGDIOBJ
    gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
    gdi32.DeleteObject.restype = wintypes.BOOL
    gdi32.DeleteDC.argtypes = [wintypes.HDC]
    gdi32.DeleteDC.restype = wintypes.BOOL
    _APIS_READY = True


class _SHFILEINFOW(ctypes.Structure):
    _fields_ = [
        ("hIcon", wintypes.HICON),
        ("iIcon", ctypes.c_int),
        ("dwAttributes", wintypes.DWORD),
        ("szDisplayName", wintypes.WCHAR * 260),
        ("szTypeName", wintypes.WCHAR * 80),
    ]


class _BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


class _BITMAPINFO(ctypes.Structure):
    _fields_ = [
        ("bmiHeader", _BITMAPINFOHEADER),
        ("bmiColors", wintypes.DWORD * 3),
    ]


def _small_icon_size() -> int:
    if sys.platform != "win32":
        return 16
    try:
        size = int(ctypes.windll.user32.GetSystemMetrics(_SM_CXSMICON))
    except Exception:
        return 16
    return max(16, min(size, 48))


def _blend_on_white(r: int, g: int, b: int, a: int) -> tuple[int, int, int]:
    if a <= 0:
        return (255, 255, 255)
    if a >= 255:
        return (r, g, b)
    t = a / 255.0
    return (
        int(r * t + 255 * (1.0 - t)),
        int(g * t + 255 * (1.0 - t)),
        int(b * t + 255 * (1.0 - t)),
    )


def _hicon_pixels(hicon: int, size: int) -> list[tuple[int, int, int]] | None:
    _configure_winapi()
    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32
    bmi = _BITMAPINFO()
    bmi.bmiHeader.biSize = ctypes.sizeof(_BITMAPINFOHEADER)
    bmi.bmiHeader.biWidth = size
    bmi.bmiHeader.biHeight = -size
    bmi.bmiHeader.biPlanes = 1
    bmi.bmiHeader.biBitCount = 32
    bmi.bmiHeader.biCompression = _BI_RGB

    hdc = user32.GetDC(0)
    if not hdc:
        return None
    bits = ctypes.c_void_p()
    hbmp = gdi32.CreateDIBSection(hdc, ctypes.byref(bmi), _DIB_RGB_COLORS, ctypes.byref(bits), None, 0)
    if not hbmp or not bits.value:
        user32.ReleaseDC(0, hdc)
        return None
    memdc = gdi32.CreateCompatibleDC(hdc)
    old = gdi32.SelectObject(memdc, hbmp)
    try:
        ctypes.memset(bits, 0, size * size * 4)
        if not user32.DrawIconEx(memdc, 0, 0, hicon, size, size, 0, None, _DI_NORMAL):
            return None
        raw = ctypes.string_at(bits, size * size * 4)
    finally:
        gdi32.SelectObject(memdc, old)
        gdi32.DeleteDC(memdc)
        gdi32.DeleteObject(hbmp)
        user32.ReleaseDC(0, hdc)

    pixels: list[tuple[int, int, int]] = []
    for i in range(0, len(raw), 4):
        blue, green, red, alpha = raw[i], raw[i + 1], raw[i + 2], raw[i + 3]
        pixels.append(_blend_on_white(red, green, blue, alpha))
    return pixels


def _shell_pixels(name: str, directory: bool, size: int) -> list[tuple[int, int, int]] | None:
    if sys.platform != "win32":
        return None
    _configure_winapi()
    shell32 = ctypes.windll.shell32
    user32 = ctypes.windll.user32
    info = _SHFILEINFOW()
    attrs = _FILE_ATTRIBUTE_DIRECTORY if directory else _FILE_ATTRIBUTE_NORMAL
    flags = _SHGFI_ICON | _SHGFI_SMALLICON | _SHGFI_USEFILEATTRIBUTES
    try:
        result = shell32.SHGetFileInfoW(name, attrs, ctypes.byref(info), ctypes.sizeof(info), flags)
    except Exception:
        return None
    if not result or not info.hIcon:
        return None
    try:
        return _hicon_pixels(int(info.hIcon), size)
    except Exception:
        return None
    finally:
        try:
            user32.DestroyIcon(info.hIcon)
        except Exception:
            pass


def _fallback_pixels(directory: bool, size: int) -> list[tuple[int, int, int]]:
    pixels = [(255, 255, 255)] * (size * size)
    pad = max(1, size // 8)

    def set_px(x: int, y: int, color: tuple[int, int, int]) -> None:
        if 0 <= x < size and 0 <= y < size:
            pixels[y * size + x] = color

    if directory:
        tab_h = max(2, size // 5)
        body_top = pad + tab_h - 1
        for y in range(pad, pad + tab_h):
            for x in range(pad, pad + size // 2):
                set_px(x, y, (242, 201, 76))
        for y in range(body_top, size - pad):
            for x in range(pad, size - pad):
                set_px(x, y, (255, 214, 90) if y == body_top else (232, 184, 46))
        return pixels

    for y in range(pad, size - pad):
        for x in range(pad, size - pad):
            set_px(x, y, (248, 248, 248))
    fold = max(3, size // 3)
    for y in range(pad, pad + fold):
        for x in range(size - pad - fold + (y - pad), size - pad):
            set_px(x, y, (210, 210, 210))
    ink = (90, 90, 90)
    for y in (pad, size - pad - 1):
        for x in range(pad, size - pad):
            set_px(x, y, ink)
    for x in (pad, size - pad - 1):
        for y in range(pad, size - pad):
            set_px(x, y, ink)
    return pixels


def _photo_from_pixels(master: tk.Misc, pixels: list[tuple[int, int, int]], size: int) -> tk.PhotoImage:
    image = tk.PhotoImage(master=master, width=size, height=size)
    for y in range(size):
        row = pixels[y * size : (y + 1) * size]
        colors = " ".join(f"#{r:02x}{g:02x}{b:02x}" for r, g, b in row)
        image.put("{" + colors + "}", to=(0, y))
    return image


class ShellIcons:
    def __init__(self, master: tk.Misc) -> None:
        self._master = master
        self.size = _small_icon_size()
        self._cache: dict[str, tk.PhotoImage] = {}

    def parent(self) -> tk.PhotoImage:
        return self.folder()

    def folder(self) -> tk.PhotoImage:
        return self._cached("dir:", True, "folder")

    def for_entry(self, entry: RemoteEntry) -> tk.PhotoImage:
        if entry.is_dir:
            return self.folder()
        suffix = Path(entry.name).suffix.lower()
        if not suffix:
            return self._cached("file:", False, "file")
        return self._cached(f"ext:{suffix}", False, f"file{suffix}")

    def _cached(self, key: str, directory: bool, query_name: str) -> tk.PhotoImage:
        image = self._cache.get(key)
        if image is not None:
            return image
        pixels = _shell_pixels(query_name, directory, self.size)
        if pixels is None or len(pixels) != self.size * self.size:
            pixels = _fallback_pixels(directory, self.size)
        image = _photo_from_pixels(self._master, pixels, self.size)
        self._cache[key] = image
        return image

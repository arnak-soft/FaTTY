"""Window geometry and widget layout persistence."""
from __future__ import annotations

import ctypes
import re
import sys
import tkinter as tk
from tkinter import ttk

from fatty.store import AppSettings

GEOM_RE = re.compile(r"^(\d+)x(\d+)(?:([+-]\d+)([+-]\d+))?$")
DEFAULT_GEOMETRY = "1100x720"


class _RECT(ctypes.Structure):
    _fields_ = [
        ("left", ctypes.c_long),
        ("top", ctypes.c_long),
        ("right", ctypes.c_long),
        ("bottom", ctypes.c_long),
    ]


def geometry_on_screen(geom: str, min_w: int = 400, min_h: int = 300) -> bool:
    match = GEOM_RE.match((geom or "").strip())
    if not match:
        return False
    width, height = int(match.group(1)), int(match.group(2))
    if width < min_w or height < min_h:
        return False
    if match.group(3) is None:
        return True
    x, y = int(match.group(3)), int(match.group(4))
    if sys.platform == "win32":
        try:
            rect = _RECT(x, y, x + width, y + height)
            handle = ctypes.windll.user32.MonitorFromRect(ctypes.byref(rect), 0)
            return bool(handle)
        except Exception:
            pass
    return True


def restore_dialog_geometry(
    window: tk.Toplevel,
    settings: AppSettings,
    key: str,
    remember_size: bool,
) -> None:
    saved = (settings.dialog_geometry.get(key) or "").strip()
    match = GEOM_RE.match(saved)
    if not match:
        return
    x_s, y_s = match.group(3), match.group(4)
    if remember_size and geometry_on_screen(saved, min_w=80, min_h=50):
        width, height = int(match.group(1)), int(match.group(2))
        try:
            min_w = int(window.wm_minsize()[0] or 0)
            min_h = int(window.wm_minsize()[1] or 0)
        except (tk.TclError, TypeError, ValueError):
            min_w, min_h = 0, 0
        if min_w > 0:
            width = max(width, min_w)
        if min_h > 0:
            height = max(height, min_h)
        if x_s and y_s:
            window.geometry(f"{width}x{height}{x_s}{y_s}")
        else:
            window.geometry(f"{width}x{height}")
        return
    if not x_s or not y_s:
        return
    width = max(window.winfo_reqwidth(), window.winfo_width(), 80)
    height = max(window.winfo_reqheight(), window.winfo_height(), 50)
    if geometry_on_screen(f"{width}x{height}{x_s}{y_s}", min_w=80, min_h=50):
        window.geometry(f"{x_s}{y_s}")


def parent_layout(parent: tk.Misc) -> tuple[AppSettings | None, object]:
    data = getattr(parent, "config_data", None)
    persist = getattr(parent, "persist", None)
    settings = getattr(data, "settings", None) if data is not None else None
    if not isinstance(settings, AppSettings):
        return None, None
    return settings, persist if callable(persist) else None


def tree_column_ids(tree: ttk.Treeview) -> list[str]:
    names = ["#0"]
    cols = tree.cget("columns")
    if not cols:
        return names
    if isinstance(cols, str):
        cols = (cols,)
    names.extend(str(col) for col in cols)
    return names


def tree_column_widths(tree: ttk.Treeview) -> dict[str, int]:
    out: dict[str, int] = {}
    for name in tree_column_ids(tree):
        try:
            width = int(tree.column(name, "width"))
        except (tk.TclError, TypeError, ValueError):
            continue
        if width > 0:
            out[name] = width
    return out


def apply_tree_columns(tree: ttk.Treeview, widths: dict[str, int] | None) -> None:
    if not widths:
        return
    valid = set(tree_column_ids(tree))
    for name, raw in widths.items():
        if name not in valid:
            continue
        try:
            width = int(raw)
        except (TypeError, ValueError):
            continue
        if width < 40:
            continue
        try:
            tree.column(name, width=width)
        except tk.TclError:
            pass


def store_tree_columns(settings: AppSettings | None, key: str, tree: ttk.Treeview) -> None:
    if settings is None:
        return
    widths = tree_column_widths(tree)
    if widths:
        settings.column_widths[key] = widths


class PositionedToplevel(tk.Toplevel):
    """Toplevel, который помнит позицию (и размер, если remember_size)."""

    def __init__(self, master=None, **kw):
        super().__init__(master, **kw)
        try:
            from fatty.theme import apply_window

            apply_window(self)
        except Exception:
            pass

    def _setup_layout(
        self,
        settings: AppSettings | None,
        key: str,
        *,
        remember_size: bool = False,
        persist=None,
    ) -> None:
        self._layout_settings = settings
        self._layout_key = key
        self._layout_size = remember_size
        self._layout_persist = persist
        self._layout_saved = settings is None
        if settings is None:
            return
        self.update_idletasks()
        restore_dialog_geometry(self, settings, key, remember_size)
        self.bind("<Configure>", self._on_layout_configure, add="+")

    def _on_layout_configure(self, event) -> None:
        if event.widget is not self:
            return
        self._capture_layout()

    def _capture_layout(self) -> None:
        settings = getattr(self, "_layout_settings", None)
        key = getattr(self, "_layout_key", None)
        if settings is None or not key:
            return
        try:
            geom = self.geometry()
        except tk.TclError:
            return
        if GEOM_RE.match(geom):
            settings.dialog_geometry[key] = geom

    def destroy(self) -> None:
        if not getattr(self, "_layout_saved", True):
            self._layout_saved = True
            self._capture_layout()
            persist = getattr(self, "_layout_persist", None)
            if persist is not None:
                try:
                    persist()
                except Exception:
                    pass
        super().destroy()

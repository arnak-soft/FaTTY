"""Splash while the app is starting: PyInstaller boot for the exe, Tk for source runs."""

from __future__ import annotations

import ctypes
import sys
import tkinter as tk
from pathlib import Path
from tkinter import ttk

from fatty import APP_NAME, __version__


def _resource_root() -> Path:
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent))
    return Path(__file__).resolve().parent.parent


def _enable_dpi() -> None:
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except Exception:
            pass


def _set_app_id() -> None:
    if sys.platform != "win32":
        return
    try:
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(APP_NAME)
    except Exception:
        pass


def _close_pyi_splash() -> None:
    try:
        import pyi_splash

        pyi_splash.close()
    except Exception:
        pass


def _update_pyi_splash(text: str) -> None:
    try:
        import pyi_splash

        pyi_splash.update_text(text)
    except Exception:
        pass


def _has_pyi_splash() -> bool:
    try:
        import pyi_splash
    except Exception:
        return False
    try:
        return bool(pyi_splash.is_alive())
    except Exception:
        return True


class _TkSplash:
    def __init__(self) -> None:
        self.root = tk.Tk()
        self.root.title(f"{APP_NAME} — загрузка")
        self.root.resizable(False, False)
        self.root.overrideredirect(True)
        try:
            self.root.attributes("-topmost", True)
        except tk.TclError:
            pass

        width, height = 420, 268
        self.root.update_idletasks()
        x = max((self.root.winfo_screenwidth() - width) // 2, 0)
        y = max((self.root.winfo_screenheight() - height) // 3, 0)
        self.root.geometry(f"{width}x{height}+{x}+{y}")
        try:
            ttk.Style(self.root).theme_use("vista")
        except tk.TclError:
            pass

        outer = tk.Frame(self.root, background="#3a3a3a")
        outer.pack(fill="both", expand=True)
        body = tk.Frame(outer, background="#1e1e1e")
        body.pack(fill="both", expand=True, padx=1, pady=1)

        self._photo = self._load_icon()
        if self._photo is not None:
            tk.Label(body, image=self._photo, background="#1e1e1e").pack(pady=(28, 8))
        tk.Label(
            body,
            text=APP_NAME,
            background="#1e1e1e",
            foreground="#ffffff",
            font=("Segoe UI", 18, "bold"),
        ).pack()
        tk.Label(
            body,
            text=__version__,
            background="#1e1e1e",
            foreground="#9cdcfe",
            font=("Segoe UI", 9),
        ).pack(pady=(2, 10))
        self.status = tk.Label(
            body,
            text="Загрузка…",
            background="#1e1e1e",
            foreground="#d4d4d4",
            font=("Segoe UI", 10),
        )
        self.status.pack()
        bar = ttk.Progressbar(body, mode="indeterminate", length=260)
        bar.pack(pady=(14, 24))
        bar.start(14)
        self._bar = bar
        self.root.update()

    def _load_icon(self) -> tk.PhotoImage | None:
        png = _resource_root() / "assets" / "app.png"
        if not png.is_file():
            return None
        try:
            image = tk.PhotoImage(file=str(png))
        except tk.TclError:
            return None
        factor = max(1, image.width() // 96)
        if factor > 1:
            image = image.subsample(factor, factor)
        return image

    def destroy(self) -> None:
        try:
            self._bar.stop()
        except Exception:
            pass
        try:
            self.root.destroy()
        except tk.TclError:
            pass


def run() -> None:
    _enable_dpi()
    _set_app_id()

    from fatty.single_instance import activate_existing, try_become_primary

    if not try_become_primary():
        _close_pyi_splash()
        activate_existing()
        return

    tk_splash: _TkSplash | None = None
    try:
        if _has_pyi_splash():
            _update_pyi_splash("Загрузка…")
        else:
            tk_splash = _TkSplash()
        from fatty.ui import main as app_main
    except Exception:
        _close_pyi_splash()
        if tk_splash is not None:
            tk_splash.destroy()
        raise
    _close_pyi_splash()
    if tk_splash is not None:
        tk_splash.destroy()
    app_main()

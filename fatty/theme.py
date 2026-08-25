"""Dark UI theme shared by every FaTTY window."""

from __future__ import annotations

import ctypes
import sys
import tkinter as tk
from tkinter import ttk


class C:
    bg = "#1e1e1e"
    chrome = "#252526"
    elevated = "#2d2d2d"
    btn = "#3c3c3c"
    btn_hover = "#4a4a4a"
    btn_press = "#333333"
    border = "#3c3c3c"
    sash = "#3c3c3c"
    text = "#d4d4d4"
    text_bright = "#f3f3f3"
    muted = "#9d9d9d"
    heading = "#e8e8e8"
    accent = "#0e639c"
    accent_hover = "#1177bb"
    accent_press = "#0b4f7c"
    on_accent = "#ffffff"
    select = "#264f78"
    meta = "#9cdcfe"
    ok = "#6a9955"
    err = "#f14c4c"
    warn = "#dcdcaa"
    cancel = "#c586c0"
    terminal = "#1e1e1e"
    insert = "#ffffff"


FONT_UI = ("Segoe UI", 10)
FONT_UI_SMALL = ("Segoe UI", 9)
FONT_UI_HEAD = ("Segoe UI", 12, "bold")
FONT_UI_TITLE = ("Segoe UI", 18, "bold")
FONT_UI_BOLD = ("Segoe UI", 10, "bold")
FONT_MONO = ("Consolas", 10)
FONT_SECTION = ("Segoe UI", 9, "bold")


def apply_theme(root: tk.Misc) -> ttk.Style:
    style = ttk.Style(root)
    try:
        style.theme_use("clam")
    except tk.TclError:
        pass

    style.configure(
        ".",
        background=C.bg,
        foreground=C.text,
        font=FONT_UI,
        troughcolor=C.chrome,
        bordercolor=C.border,
        darkcolor=C.border,
        lightcolor=C.border,
        focuscolor=C.accent,
        fieldbackground=C.elevated,
    )
    style.configure("TFrame", background=C.bg)
    style.configure("TLabel", background=C.bg, foreground=C.text, font=FONT_UI)
    style.configure("Muted.TLabel", background=C.bg, foreground=C.muted, font=FONT_UI_SMALL)
    style.configure("Error.TLabel", background=C.bg, foreground=C.err, font=FONT_UI)
    style.configure("Meta.TLabel", background=C.bg, foreground=C.meta, font=FONT_UI)
    style.configure(
        "Status.TLabel",
        background=C.chrome,
        foreground=C.muted,
        font=FONT_UI_SMALL,
        padding=(12, 6),
    )
    style.configure("Status.TFrame", background=C.chrome)

    style.configure(
        "TLabelframe",
        background=C.bg,
        foreground=C.text,
        bordercolor=C.border,
        relief="groove",
        padding=8,
    )
    style.configure(
        "TLabelframe.Label",
        background=C.bg,
        foreground=C.meta,
        font=FONT_SECTION,
    )

    style.configure(
        "TButton",
        background=C.btn,
        foreground=C.text_bright,
        bordercolor=C.border,
        focusthickness=1,
        focuscolor=C.accent,
        padding=(10, 5),
        font=FONT_UI,
    )
    style.map(
        "TButton",
        background=[("pressed", C.btn_press), ("active", C.btn_hover), ("disabled", C.elevated)],
        foreground=[("disabled", C.muted)],
        bordercolor=[("focus", C.accent), ("disabled", C.border)],
    )
    _accent_button(style, "Accent.TButton")
    _accent_button(style, "Run.TButton")

    style.configure(
        "TEntry",
        fieldbackground=C.elevated,
        foreground=C.text,
        insertcolor=C.insert,
        bordercolor=C.border,
        padding=5,
    )
    style.map(
        "TEntry",
        fieldbackground=[("disabled", C.chrome), ("readonly", C.chrome)],
        foreground=[("disabled", C.muted)],
        bordercolor=[("focus", C.accent)],
        lightcolor=[("focus", C.accent)],
        darkcolor=[("focus", C.accent)],
    )

    style.configure(
        "TCombobox",
        fieldbackground=C.elevated,
        background=C.btn,
        foreground=C.text,
        arrowcolor=C.text,
        bordercolor=C.border,
        padding=4,
    )
    style.map(
        "TCombobox",
        fieldbackground=[("readonly", C.elevated), ("disabled", C.chrome)],
        foreground=[("disabled", C.muted)],
        bordercolor=[("focus", C.accent)],
        arrowcolor=[("disabled", C.muted)],
    )
    root.option_add("*TCombobox*Listbox.background", C.elevated)
    root.option_add("*TCombobox*Listbox.foreground", C.text)
    root.option_add("*TCombobox*Listbox.selectBackground", C.select)
    root.option_add("*TCombobox*Listbox.selectForeground", C.text_bright)
    root.option_add("*TCombobox*Listbox.font", FONT_UI)

    style.configure(
        "TCheckbutton",
        background=C.bg,
        foreground=C.text,
        indicatorcolor=C.elevated,
        indicatormargin=4,
        font=FONT_UI,
    )
    style.map(
        "TCheckbutton",
        background=[("active", C.bg)],
        foreground=[("disabled", C.muted)],
        indicatorcolor=[("selected", C.accent), ("pressed", C.accent_hover)],
    )

    _tree(style, "Treeview")
    _tree(style, "Files.Treeview")

    style.configure(
        "TScrollbar",
        background=C.btn,
        troughcolor=C.chrome,
        bordercolor=C.chrome,
        arrowcolor=C.muted,
        relief="flat",
    )
    style.map(
        "TScrollbar",
        background=[("active", C.btn_hover), ("disabled", C.chrome)],
        arrowcolor=[("disabled", C.border)],
    )

    style.configure(
        "TProgressbar",
        background=C.accent,
        troughcolor=C.elevated,
        bordercolor=C.border,
        lightcolor=C.accent,
        darkcolor=C.accent,
    )

    style.configure("TNotebook", background=C.bg, borderwidth=0, tabmargins=(4, 4, 4, 0))
    style.configure(
        "TNotebook.Tab",
        background=C.chrome,
        foreground=C.muted,
        padding=(14, 7),
        font=FONT_UI,
        bordercolor=C.border,
        lightcolor=C.chrome,
        darkcolor=C.chrome,
    )
    style.map(
        "TNotebook.Tab",
        background=[("selected", C.elevated), ("active", C.btn)],
        foreground=[("selected", C.text_bright), ("active", C.text)],
        lightcolor=[("selected", C.elevated)],
    )

    style.configure("TPanedwindow", background=C.bg)
    style.configure("Sash", sashthickness=6, sashrelief="flat")

    style.configure("TSeparator", background=C.border)

    try:
        root.configure(background=C.bg)
    except tk.TclError:
        pass
    apply_window(root)
    return style


def _accent_button(style: ttk.Style, name: str) -> None:
    style.configure(
        name,
        background=C.accent,
        foreground=C.on_accent,
        bordercolor=C.accent,
        focusthickness=1,
        focuscolor=C.on_accent,
        padding=(12, 6),
        font=FONT_UI_BOLD,
    )
    style.map(
        name,
        background=[("pressed", C.accent_press), ("active", C.accent_hover), ("disabled", C.btn)],
        foreground=[("disabled", C.muted)],
        bordercolor=[("disabled", C.border)],
    )


def _tree(style: ttk.Style, name: str) -> None:
    style.configure(
        name,
        background=C.terminal,
        fieldbackground=C.terminal,
        foreground=C.text,
        bordercolor=C.border,
        rowheight=26,
        font=FONT_UI,
    )
    style.map(
        name,
        background=[("selected", C.select)],
        foreground=[("selected", C.text_bright)],
    )
    heading = f"{name}.Heading"
    style.configure(
        heading,
        background=C.chrome,
        foreground=C.muted,
        bordercolor=C.border,
        relief="flat",
        font=FONT_SECTION,
        padding=(8, 6),
    )
    style.map(
        heading,
        background=[("active", C.elevated)],
        foreground=[("active", C.text_bright)],
    )


def apply_window(window: tk.Misc) -> None:
    try:
        window.configure(background=C.bg)
    except tk.TclError:
        pass
    try:
        window.after(10, lambda: apply_dark_titlebar(window))
    except Exception:
        apply_dark_titlebar(window)


def apply_dark_titlebar(window: tk.Misc) -> None:
    if sys.platform != "win32":
        return
    try:
        window.update_idletasks()
        inner = int(window.winfo_id())
        hwnd = int(ctypes.windll.user32.GetParent(inner) or inner)
        value = ctypes.c_int(1)
        dwm = ctypes.windll.dwmapi.DwmSetWindowAttribute
        for attr in (20, 19):
            dwm(hwnd, attr, ctypes.byref(value), ctypes.sizeof(value))
    except Exception:
        pass


def style_menu(menu: tk.Menu) -> None:
    try:
        menu.configure(
            background=C.chrome,
            foreground=C.text,
            activebackground=C.select,
            activeforeground=C.text_bright,
            disabledforeground=C.muted,
            borderwidth=0,
            relief="flat",
            font=FONT_UI,
        )
    except tk.TclError:
        pass


def style_code(widget: tk.Text) -> None:
    widget.configure(
        background=C.terminal,
        foreground=C.text,
        insertbackground=C.insert,
        selectbackground=C.select,
        selectforeground=C.text_bright,
        font=FONT_MONO,
        relief="flat",
        borderwidth=0,
        highlightthickness=1,
        highlightbackground=C.border,
        highlightcolor=C.accent,
        padx=8,
        pady=6,
    )


def style_output(widget: tk.Text) -> None:
    style_code(widget)
    widget.tag_configure("meta", foreground=C.meta)
    widget.tag_configure("ok", foreground=C.ok)
    widget.tag_configure("err", foreground=C.err)


def style_prose(widget: tk.Text) -> None:
    widget.configure(
        background=C.bg,
        foreground=C.text,
        insertbackground=C.insert,
        selectbackground=C.select,
        selectforeground=C.text_bright,
        font=FONT_UI,
        relief="flat",
        borderwidth=0,
        highlightthickness=0,
        padx=12,
        pady=10,
        cursor="arrow",
    )
    widget.tag_configure("h", font=FONT_UI_HEAD, foreground=C.heading, spacing1=14, spacing3=4)
    widget.tag_configure("p", font=FONT_UI, foreground=C.text, spacing3=8)
    widget.tag_configure(
        "pre",
        font=FONT_MONO,
        foreground=C.meta,
        background=C.elevated,
        spacing3=10,
        lmargin1=10,
        lmargin2=10,
    )


def apply_result_tags(tree: ttk.Treeview) -> None:
    tree.tag_configure("ok", foreground=C.ok)
    tree.tag_configure("failed", foreground=C.err)
    tree.tag_configure("timeout", foreground=C.warn)
    tree.tag_configure("cancelled", foreground=C.cancel)
    tree.tag_configure("error", foreground=C.err)

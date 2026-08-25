from __future__ import annotations

import ctypes
import os
import sys
import threading
import time
import traceback
import uuid
import webbrowser
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from fatty import APP_NAME, __version__
from fatty.presets import (
    DEFAULT_APP_DIR,
    DEFAULT_BRANCH,
    DEFAULT_PM2,
    Preset,
    all_presets,
)
from fatty.single_instance import activate_existing, register_window, try_become_primary
from fatty.ssh_runner import (
    PUTTY_DOWNLOAD_URL,
    PuttyLaunchError,
    PuttyNotFoundError,
    SSHError,
    SSHSession,
    open_putty_console,
    open_system_console,
)
from fatty.sftp import guess_start_path
from fatty.settings_ui import SettingsDialog
from fatty.files_ui import FilesWindow
from fatty.journal import Journal, JournalEntry, now_iso, status_from_exit
from fatty.journal_ui import JournalWindow
from fatty.layout import (
    DEFAULT_GEOMETRY as _DEFAULT_GEOMETRY,
    GEOM_RE as _GEOM_RE,
    PositionedToplevel,
    apply_tree_columns,
    geometry_on_screen as _geometry_on_screen,
    parent_layout as _parent_layout,
    store_tree_columns,
)
from fatty.store import APP_DIR, Command, Config, Server, load, save, unlock_secrets
from fatty.updates import UpdateCheckResult, UpdateError, check_for_updates
from fatty.vault import MIN_PASSWORD_LEN, MIN_PASSWORD_LEN_RELAXED, SessionVault, VaultError, VaultLocked


def _resource_root() -> Path:
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent))
    return Path(__file__).resolve().parent.parent


def _apply_app_icon(window: tk.Tk) -> None:
    root = _resource_root()
    ico = root / "assets" / "app.ico"
    png = root / "assets" / "app.png"
    if ico.is_file():
        try:
            window.iconbitmap(str(ico))
        except tk.TclError:
            pass
        try:
            window.iconbitmap(default=str(ico))
        except tk.TclError:
            pass
    if png.is_file():
        try:
            window._app_icon_photo = tk.PhotoImage(file=str(png))  # type: ignore[attr-defined]
            window.iconphoto(True, window._app_icon_photo)  # type: ignore[attr-defined]
        except tk.TclError:
            pass


def _attach_presets(config: Config, server_id: str, presets: list[Preset]) -> int:
    existing = {c.name for c in config.commands_for(server_id)}
    added = 0
    for preset in presets:
        if preset.name in existing:
            continue
        config.commands.append(
            Command(
                id=Command.new(server_id).id,
                name=preset.name,
                server_id=server_id,
                command=preset.command,
                timeout_sec=preset.timeout_sec,
                login_shell=preset.login_shell,
            )
        )
        existing.add(preset.name)
        added += 1
    return added


def _copy_name(base: str, taken: set[str]) -> str:
    stem = f"{base} (копия)"
    if stem not in taken:
        return stem
    n = 2
    while True:
        candidate = f"{base} (копия {n})"
        if candidate not in taken:
            return candidate
        n += 1


def _enable_dpi() -> None:
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except Exception:
            pass


def _parent_is_withdrawn(parent: tk.Misc) -> bool:
    try:
        return str(parent.state()) == "withdrawn"
    except tk.TclError:
        return False


def _copy_widget_selection(widget: tk.Text) -> str:
    try:
        text = widget.get("sel.first", "sel.last")
    except tk.TclError:
        return ""
    if not text:
        return ""
    widget.clipboard_clear()
    widget.clipboard_append(text)
    try:
        widget.update_idletasks()
    except tk.TclError:
        pass
    return text


def _bind_copy_on_select(widget: tk.Text, on_copied=None) -> None:
    """PuTTY-style: releasing the mouse after a selection copies to clipboard."""

    def copy_sel(_event=None):
        text = _copy_widget_selection(widget)
        if text and on_copied is not None:
            on_copied(text)
        return "break" if text else None

    widget.bind("<ButtonRelease-1>", copy_sel, add="+")
    widget.bind("<Control-c>", copy_sel)
    widget.bind("<Control-C>", copy_sel)


def _present_toplevel(window: tk.Toplevel) -> None:
    """Make a Toplevel visible and modal even if its parent was withdrawn."""
    window.deiconify()
    window.lift()
    window.update_idletasks()
    try:
        window.wait_visibility()
    except tk.TclError:
        pass
    try:
        window.grab_set()
    except tk.TclError:
        pass
    try:
        window.focus_force()
    except tk.TclError:
        pass


class ServerDialog(PositionedToplevel):
    def __init__(self, parent: tk.Tk, server: Server, title: str, is_new: bool = False) -> None:
        super().__init__(parent)
        self.title(title)
        self.resizable(False, False)
        self.transient(parent)
        self.result: Server | None = None
        self._server = server
        self._is_new = is_new

        pad = {"padx": 10, "pady": 4}
        body = ttk.Frame(self, padding=12)
        body.grid(sticky="nsew")

        self.name_var = tk.StringVar(value=server.name)
        self.host_var = tk.StringVar(value=server.host)
        self.port_var = tk.StringVar(value=str(server.port or 22))
        self.user_var = tk.StringVar(value=server.username)
        self.password_var = tk.StringVar(value="")
        self.key_var = tk.StringVar(value=server.key_path)
        self.show_pw = tk.BooleanVar(value=False)
        self.clear_pw_var = tk.BooleanVar(value=False)
        self._stored_password = "" if is_new else (server.password or "")

        rows = [
            ("Имя", self.name_var),
            ("Хост / IP", self.host_var),
            ("Порт", self.port_var),
            ("Логин", self.user_var),
        ]
        for i, (label, var) in enumerate(rows):
            ttk.Label(body, text=label).grid(row=i, column=0, sticky="w", **pad)
            ttk.Entry(body, textvariable=var, width=42).grid(row=i, column=1, columnspan=2, sticky="ew", **pad)

        ttk.Label(body, text="Пароль").grid(row=4, column=0, sticky="w", **pad)
        self.pw_entry = ttk.Entry(body, textvariable=self.password_var, width=32, show="•")
        self.pw_entry.grid(row=4, column=1, sticky="ew", **pad)
        ttk.Checkbutton(
            body,
            text="показать",
            variable=self.show_pw,
            command=self._toggle_pw,
        ).grid(row=4, column=2, sticky="w", **pad)

        if is_new:
            pw_note = "После сохранения пароль нельзя просмотреть — только заменить новым."
        elif self._stored_password:
            pw_note = "Пароль сохранён, просмотр недоступен. Оставьте поле пустым, чтобы не менять."
        else:
            pw_note = "Пароль не задан. После сохранения его нельзя будет просмотреть."
        ttk.Label(body, text=pw_note, foreground="#555", wraplength=420).grid(
            row=5, column=1, columnspan=2, sticky="w", padx=10, pady=(0, 2)
        )
        if self._stored_password:
            ttk.Checkbutton(
                body,
                text="Удалить сохранённый пароль",
                variable=self.clear_pw_var,
                command=self._toggle_clear_pw,
            ).grid(row=6, column=1, columnspan=2, sticky="w", padx=10, pady=(0, 2))

        ttk.Label(body, text="SSH-ключ").grid(row=7, column=0, sticky="w", **pad)
        ttk.Entry(body, textvariable=self.key_var, width=32).grid(row=7, column=1, sticky="ew", **pad)
        ttk.Button(body, text="Обзор…", command=self._browse_key).grid(row=7, column=2, sticky="ew", **pad)

        hint = ttk.Label(
            body,
            text="Пароль хранится в зашифрованном виде (Windows DPAPI).\n"
            "Ключ можно не указывать, если вход только по паролю.",
            foreground="#555",
        )
        hint.grid(row=8, column=0, columnspan=3, sticky="w", padx=10, pady=(8, 4))

        btns = ttk.Frame(body)
        btns.grid(row=9, column=0, columnspan=3, sticky="e", pady=(10, 0))
        ttk.Button(btns, text="Отмена", command=self.destroy).pack(side="right", padx=4)
        ttk.Button(btns, text="Сохранить", command=self._ok).pack(side="right", padx=4)

        self.bind("<Return>", lambda _e: self._ok())
        self.bind("<Escape>", lambda _e: self.destroy())
        self.grab_set()
        self.wait_visibility()
        settings, persist = _parent_layout(parent)
        self._setup_layout(settings, "server", persist=persist)
        self.after(50, lambda: body.grid_slaves(row=0, column=1)[0].focus_set())

    def _toggle_pw(self) -> None:
        self.pw_entry.configure(show="" if self.show_pw.get() else "•")

    def _toggle_clear_pw(self) -> None:
        clearing = self.clear_pw_var.get()
        state = "disabled" if clearing else "normal"
        self.pw_entry.configure(state=state)
        if clearing:
            self.password_var.set("")
            self.show_pw.set(False)
            self.pw_entry.configure(show="•")

    def _resolve_password(self) -> str:
        if self.clear_pw_var.get():
            return ""
        typed = self.password_var.get()
        if typed:
            return typed
        return self._stored_password

    def _browse_key(self) -> None:
        path = filedialog.askopenfilename(
            parent=self,
            title="SSH private key",
            initialdir=os.path.expanduser("~/.ssh"),
        )
        if path:
            self.key_var.set(path)

    def _ok(self) -> None:
        name = self.name_var.get().strip()
        host = self.host_var.get().strip()
        user = self.user_var.get().strip()
        if not name or not host or not user:
            messagebox.showwarning("Проверка", "Заполните имя, хост и логин.", parent=self)
            return
        try:
            port = int(self.port_var.get().strip() or "22")
            if not (1 <= port <= 65535):
                raise ValueError
        except ValueError:
            messagebox.showwarning("Проверка", "Порт должен быть числом 1–65535.", parent=self)
            return
        password = self._resolve_password()
        key_path = self.key_var.get().strip()
        if not password and not key_path:
            if not messagebox.askyesno(
                "Без пароля и ключа",
                "Пароль и ключ пустые. Подключаться через ssh-agent / ключи по умолчанию?",
                parent=self,
            ):
                return
        self.result = Server(
            id=self._server.id,
            name=name,
            host=host,
            port=port,
            username=user,
            password=password,
            key_path=key_path,
        )
        self.destroy()


class PresetDialog(PositionedToplevel):
    def __init__(self, parent: tk.Tk, server: Server) -> None:
        super().__init__(parent)
        self.title(f"Пресеты — {server.name}")
        self.resizable(False, False)
        self.transient(parent)
        self.result: list[Preset] | None = None
        self._server = server
        self._vars: list[tuple[tk.BooleanVar, Preset]] = []

        body = ttk.Frame(self, padding=12)
        body.pack(fill="both", expand=True)

        self.app_dir_var = tk.StringVar(value=DEFAULT_APP_DIR)
        self.branch_var = tk.StringVar(value=DEFAULT_BRANCH)
        self.pm2_var = tk.StringVar(value=DEFAULT_PM2)

        form = ttk.Frame(body)
        form.pack(fill="x")
        ttk.Label(form, text="Каталог приложения").grid(row=0, column=0, sticky="w", padx=4, pady=3)
        ttk.Entry(form, textvariable=self.app_dir_var, width=44).grid(row=0, column=1, sticky="ew", padx=4, pady=3)
        ttk.Label(form, text="Ветка git").grid(row=1, column=0, sticky="w", padx=4, pady=3)
        ttk.Entry(form, textvariable=self.branch_var, width=20).grid(row=1, column=1, sticky="w", padx=4, pady=3)
        ttk.Label(form, text="Процесс pm2").grid(row=2, column=0, sticky="w", padx=4, pady=3)
        ttk.Entry(form, textvariable=self.pm2_var, width=20).grid(row=2, column=1, sticky="w", padx=4, pady=3)
        form.columnconfigure(1, weight=1)

        ttk.Button(body, text="Обновить список", command=self._rebuild).pack(anchor="w", pady=(6, 4))
        self.list_frame = ttk.Labelframe(body, text="Что добавить", padding=8)
        self.list_frame.pack(fill="both", expand=True)
        self._rebuild()

        hint = ttk.Label(
            body,
            text="Уже существующие команды с тем же названием не дублируются.",
            foreground="#555",
        )
        hint.pack(anchor="w", pady=(6, 0))

        btns = ttk.Frame(body)
        btns.pack(fill="x", pady=(10, 0))
        ttk.Button(btns, text="Отмена", command=self.destroy).pack(side="right", padx=4)
        ttk.Button(btns, text="Добавить выбранные", command=self._ok).pack(side="right", padx=4)

        self.bind("<Escape>", lambda _e: self.destroy())
        self.grab_set()
        settings, persist = _parent_layout(parent)
        self._setup_layout(settings, "preset", persist=persist)

    def _rebuild(self) -> None:
        for child in self.list_frame.winfo_children():
            child.destroy()
        self._vars.clear()
        presets = all_presets(
            self.app_dir_var.get().strip() or DEFAULT_APP_DIR,
            self.branch_var.get().strip() or DEFAULT_BRANCH,
            self.pm2_var.get().strip() or DEFAULT_PM2,
            include_server=True,
        )
        for preset in presets:
            var = tk.BooleanVar(value=True)
            preview = preset.command if len(preset.command) <= 80 else preset.command[:77] + "…"
            ttk.Checkbutton(self.list_frame, text=f"{preset.name}  —  {preview}", variable=var).pack(
                anchor="w", pady=1
            )
            self._vars.append((var, preset))

    def _ok(self) -> None:
        selected = [preset for var, preset in self._vars if var.get()]
        if not selected:
            messagebox.showwarning("Пресеты", "Ничего не выбрано.", parent=self)
            return
        self.result = selected
        self.destroy()


class CommandDialog(PositionedToplevel):
    def __init__(self, parent: tk.Tk, command: Command, servers: list[Server], title: str) -> None:
        super().__init__(parent)
        self.title(title)
        self.geometry("640x420")
        self.minsize(520, 360)
        self.transient(parent)
        self.result: Command | None = None
        self._command = command
        self._servers = servers

        body = ttk.Frame(self, padding=12)
        body.pack(fill="both", expand=True)

        self.name_var = tk.StringVar(value=command.name)
        self.timeout_var = tk.StringVar(value=str(command.timeout_sec))
        self.login_var = tk.BooleanVar(value=command.login_shell)
        server_names = {s.id: s.name for s in servers}
        self.server_var = tk.StringVar(value=server_names.get(command.server_id, ""))
        self._id_by_name = {s.name: s.id for s in servers}

        form = ttk.Frame(body)
        form.pack(fill="x")
        ttk.Label(form, text="Название").grid(row=0, column=0, sticky="w", padx=4, pady=4)
        ttk.Entry(form, textvariable=self.name_var, width=48).grid(row=0, column=1, sticky="ew", padx=4, pady=4)
        ttk.Label(form, text="VPS").grid(row=1, column=0, sticky="w", padx=4, pady=4)
        ttk.Combobox(
            form,
            textvariable=self.server_var,
            values=[s.name for s in servers],
            state="readonly",
            width=45,
        ).grid(row=1, column=1, sticky="ew", padx=4, pady=4)
        ttk.Label(form, text="Таймаут, с").grid(row=2, column=0, sticky="w", padx=4, pady=4)
        ttk.Entry(form, textvariable=self.timeout_var, width=12).grid(row=2, column=1, sticky="w", padx=4, pady=4)
        ttk.Checkbutton(
            form,
            text="Login-shell (bash -lc) — подхватывает PATH из .bashrc",
            variable=self.login_var,
        ).grid(row=3, column=1, sticky="w", padx=4, pady=4)
        ttk.Label(form, text="Пресет").grid(row=4, column=0, sticky="w", padx=4, pady=4)
        self._presets = all_presets(include_server=True)
        self.preset_var = tk.StringVar()
        preset_box = ttk.Combobox(
            form,
            textvariable=self.preset_var,
            values=[p.name for p in self._presets],
            state="readonly",
            width=45,
        )
        preset_box.grid(row=4, column=1, sticky="ew", padx=4, pady=4)
        preset_box.bind("<<ComboboxSelected>>", self._apply_preset)
        form.columnconfigure(1, weight=1)

        ttk.Label(body, text="Команда").pack(anchor="w", padx=4, pady=(8, 2))
        self.text = tk.Text(body, height=10, wrap="word", font=("Consolas", 10))
        self.text.pack(fill="both", expand=True, padx=4)
        self.text.insert("1.0", command.command)

        btns = ttk.Frame(body)
        btns.pack(fill="x", pady=(10, 0))
        ttk.Button(btns, text="Отмена", command=self.destroy).pack(side="right", padx=4)
        ttk.Button(btns, text="Сохранить", command=self._ok).pack(side="right", padx=4)

        self.bind("<Escape>", lambda _e: self.destroy())
        self.grab_set()
        settings, persist = _parent_layout(parent)
        self._setup_layout(settings, "command", remember_size=True, persist=persist)
        self.after(50, lambda: self.text.focus_set())

    def _apply_preset(self, _event=None) -> None:
        chosen = self.preset_var.get()
        preset = next((p for p in self._presets if p.name == chosen), None)
        if not preset:
            return
        self.name_var.set(preset.name)
        self.timeout_var.set(str(preset.timeout_sec))
        self.login_var.set(preset.login_shell)
        self.text.delete("1.0", "end")
        self.text.insert("1.0", preset.command)

    def _ok(self) -> None:
        name = self.name_var.get().strip()
        server_name = self.server_var.get().strip()
        cmd = self.text.get("1.0", "end").strip()
        if not name or not cmd or not server_name:
            messagebox.showwarning("Проверка", "Заполните название, VPS и команду.", parent=self)
            return
        try:
            timeout = int(self.timeout_var.get().strip() or "180")
            if timeout < 1:
                raise ValueError
        except ValueError:
            messagebox.showwarning("Проверка", "Таймаут должен быть положительным числом.", parent=self)
            return
        self.result = Command(
            id=self._command.id,
            name=name,
            server_id=self._id_by_name[server_name],
            command=cmd,
            timeout_sec=timeout,
            login_shell=self.login_var.get(),
        )
        self.destroy()


class MasterPasswordDialog(PositionedToplevel):
    def __init__(self, parent: tk.Tk, config: Config, vault: SessionVault) -> None:
        super().__init__(parent)
        self.ok = False
        self._config = config
        self._vault = vault
        self._setup = config.vault is None
        self.title("Мастер-пароль")
        self.resizable(False, False)
        if not _parent_is_withdrawn(parent):
            self.transient(parent)
        self.protocol("WM_DELETE_WINDOW", self._cancel)

        body = ttk.Frame(self, padding=16)
        body.pack(fill="both", expand=True)

        if self._setup:
            intro = (
                "Задайте мастер-пароль. Им шифруются пароли VPS.\n"
                "Без него конфиг нельзя расшифровать — восстановить фразу нельзя."
            )
        else:
            intro = "Введите мастер-пароль, чтобы открыть сохранённые пароли VPS."
        ttk.Label(body, text=intro, wraplength=420, justify="left").pack(anchor="w")

        form = ttk.Frame(body)
        form.pack(fill="x", pady=(12, 0))
        self.pw_var = tk.StringVar()
        self.pw2_var = tk.StringVar()
        ttk.Label(form, text="Мастер-пароль").grid(row=0, column=0, sticky="w", pady=4)
        self.pw_entry = ttk.Entry(form, textvariable=self.pw_var, width=36, show="•")
        self.pw_entry.grid(row=0, column=1, sticky="ew", padx=(8, 0), pady=4)
        if self._setup:
            ttk.Label(form, text="Ещё раз").grid(row=1, column=0, sticky="w", pady=4)
            ttk.Entry(form, textvariable=self.pw2_var, width=36, show="•").grid(
                row=1, column=1, sticky="ew", padx=(8, 0), pady=4
            )
            ttk.Label(
                form,
                text=f"Не короче {MIN_PASSWORD_LEN} символов.",
                foreground="#555",
            ).grid(row=2, column=1, sticky="w", padx=(8, 0), pady=(0, 4))
        form.columnconfigure(1, weight=1)

        self.error = ttk.Label(body, text="", foreground="#b00020")
        self.error.pack(anchor="w", pady=(8, 0))

        btns = ttk.Frame(body)
        btns.pack(fill="x", pady=(12, 0))
        ttk.Button(btns, text="Выход", command=self._cancel).pack(side="right", padx=4)
        ttk.Button(btns, text="Продолжить", command=self._submit).pack(side="right", padx=4)

        self.bind("<Return>", lambda _e: self._submit())
        self.bind("<Escape>", lambda _e: self._cancel())
        _present_toplevel(self)
        self._setup_layout(config.settings, "master")
        register_window(self)
        self.after(50, self.pw_entry.focus_set)

    def _cancel(self) -> None:
        self.ok = False
        self._clear_fields()
        self.destroy()

    def _clear_fields(self) -> None:
        self.pw_var.set("")
        self.pw2_var.set("")

    def _submit(self) -> None:
        password = self.pw_var.get()
        self.error.configure(text="")
        try:
            if self._setup:
                if password != self.pw2_var.get():
                    self.error.configure(text="Пароли не совпадают.")
                    return
                meta = self._vault.create(password)
                self._config.vault = meta
            else:
                assert self._config.vault is not None
                if not self._vault.unlock(password, self._config.vault):
                    self.error.configure(text="Неверный мастер-пароль.")
                    self.pw_var.set("")
                    return
                unlock_secrets(self._config, self._vault)
        except VaultError as exc:
            self.error.configure(text=str(exc))
            return
        except Exception:
            self.error.configure(text="Не удалось открыть хранилище. Проверьте пароль.")
            return
        self.ok = True
        self._clear_fields()
        self.destroy()


class ChangeMasterDialog(PositionedToplevel):
    def __init__(
        self,
        parent: tk.Tk,
        vault: SessionVault,
        *,
        allow_short_master_password: bool = False,
    ) -> None:
        super().__init__(parent)
        self.ok = False
        self._vault = vault
        self._allow_short = allow_short_master_password
        self.title("Сменить мастер-пароль")
        self.resizable(False, False)
        self.transient(parent)

        body = ttk.Frame(self, padding=16)
        body.pack(fill="both", expand=True)
        ttk.Label(
            body,
            text="Текущие пароли VPS будут перешифрованы новым мастер-паролем.",
            wraplength=400,
        ).pack(anchor="w")

        form = ttk.Frame(body)
        form.pack(fill="x", pady=(12, 0))
        self.old_var = tk.StringVar()
        self.new_var = tk.StringVar()
        self.new2_var = tk.StringVar()
        rows = [
            ("Текущий", self.old_var),
            ("Новый", self.new_var),
            ("Новый ещё раз", self.new2_var),
        ]
        for i, (label, var) in enumerate(rows):
            ttk.Label(form, text=label).grid(row=i, column=0, sticky="w", pady=4)
            ttk.Entry(form, textvariable=var, width=32, show="•").grid(
                row=i, column=1, sticky="ew", padx=(8, 0), pady=4
            )
        if allow_short_master_password:
            hint = (
                f"Можно от {MIN_PASSWORD_LEN_RELAXED} символов "
                f"(короче {MIN_PASSWORD_LEN} — с подтверждением)."
            )
        else:
            hint = f"Не короче {MIN_PASSWORD_LEN} символов."
        ttk.Label(form, text=hint, foreground="#555", wraplength=320).grid(
            row=len(rows), column=0, columnspan=2, sticky="w", pady=(4, 0)
        )
        form.columnconfigure(1, weight=1)

        self.error = ttk.Label(body, text="", foreground="#b00020")
        self.error.pack(anchor="w", pady=(8, 0))

        btns = ttk.Frame(body)
        btns.pack(fill="x", pady=(12, 0))
        ttk.Button(btns, text="Отмена", command=self.destroy).pack(side="right", padx=4)
        ttk.Button(btns, text="Сменить", command=self._submit).pack(side="right", padx=4)
        self.bind("<Return>", lambda _e: self._submit())
        self.bind("<Escape>", lambda _e: self.destroy())
        self.grab_set()
        settings, persist = _parent_layout(parent)
        self._setup_layout(settings, "change_master", persist=persist)

    def _submit(self) -> None:
        old_pw = self.old_var.get()
        new_pw = self.new_var.get()
        self.error.configure(text="")
        if new_pw != self.new2_var.get():
            self.error.configure(text="Новые пароли не совпадают.")
            return
        min_len = MIN_PASSWORD_LEN_RELAXED if self._allow_short else MIN_PASSWORD_LEN
        if len(new_pw) < min_len:
            self.error.configure(text=f"Новый пароль не короче {min_len} символов.")
            return
        if self._allow_short and len(new_pw) < MIN_PASSWORD_LEN:
            if not messagebox.askyesno(
                "Мастер-пароль",
                f"Пароль короче {MIN_PASSWORD_LEN} символов — его проще подобрать.\n\n"
                "Вы уверены, что хотите использовать такой пароль?",
                parent=self,
            ):
                return
        meta = self._vault.meta
        if meta is None or not self._vault.unlock(old_pw, meta):
            self.error.configure(text="Неверный текущий мастер-пароль.")
            self.old_var.set("")
            return
        try:
            self._vault.create(new_pw, min_len=min_len)
        except VaultError as exc:
            self.error.configure(text=str(exc))
            return
        self.ok = True
        self.old_var.set("")
        self.new_var.set("")
        self.new2_var.set("")
        self.destroy()


class App(tk.Tk):
    def __init__(self, config: Config, vault: SessionVault) -> None:
        super().__init__()
        self.title(f"{APP_NAME} {__version__}")
        _apply_app_icon(self)
        register_window(self)
        self.minsize(860, 560)

        self.config_data: Config = config
        self.vault = vault
        self._session: SSHSession | None = None
        self._files_windows: dict[str, FilesWindow] = {}
        self._journal_window: JournalWindow | None = None
        self.journal = Journal(max_entries=config.settings.journal_max_entries)
        self._remote_cwd: dict[str, str] = {}
        self._busy = False
        self._restoring_layout = True
        self._last_wm_state = self.config_data.settings.window_state or "normal"

        self._build_style()
        self._build_menu()
        self._build_ui()
        self._restore_window_layout()
        self._refresh_servers(self.config_data.settings.last_server_id)
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.bind("<F5>", lambda _e: self._run_selected())
        self.bind("<Control-j>", lambda _e: self._open_journal())
        self.bind("<Control-J>", lambda _e: self._open_journal())
        self.bind("<Control-comma>", lambda _e: self._open_settings())
        self.bind("<Configure>", self._on_window_configure)
        self.after(120, self._finish_layout_restore)
        self.after(2000, self._maybe_auto_check_updates)

    def _build_style(self) -> None:
        style = ttk.Style(self)
        try:
            style.theme_use("vista")
        except tk.TclError:
            pass
        style.configure("TLabelframe.Label", font=("Segoe UI", 10, "bold"))
        style.configure("Run.TButton", font=("Segoe UI", 10, "bold"))

    def _build_menu(self) -> None:
        menubar = tk.Menu(self)
        file_menu = tk.Menu(menubar, tearoff=0)
        file_menu.add_command(label="Открыть папку конфига", command=self._open_config_dir)
        file_menu.add_command(label="Журнал команд…", command=self._open_journal, accelerator="Ctrl+J")
        file_menu.add_separator()
        file_menu.add_command(label="Экспорт…", command=self._export_config)
        file_menu.add_command(label="Импорт…", command=self._import_config)
        file_menu.add_separator()
        file_menu.add_command(label="Выход", command=self._on_close)
        menubar.add_cascade(label="Файл", menu=file_menu)

        opt = tk.Menu(menubar, tearoff=0)
        opt.add_command(label="Настройки…", command=self._open_settings, accelerator="Ctrl+,")
        opt.add_separator()
        opt.add_command(label="Сменить мастер-пароль…", command=self._change_master_password)
        menubar.add_cascade(label="Настройки", menu=opt)

        help_menu = tk.Menu(menubar, tearoff=0)
        help_menu.add_command(label="Проверить обновления…", command=self._check_updates_manual)
        help_menu.add_separator()
        help_menu.add_command(label="О программе", command=self._about)
        menubar.add_cascade(label="Справка", menu=help_menu)
        self.config(menu=menubar)

    def _build_ui(self) -> None:
        root = ttk.Frame(self, padding=8)
        root.pack(fill="both", expand=True)

        vpaned = ttk.Panedwindow(root, orient="vertical")
        vpaned.pack(fill="both", expand=True)
        self.vpaned = vpaned

        top = ttk.Frame(vpaned)
        paned = ttk.Panedwindow(top, orient="horizontal")
        paned.pack(fill="both", expand=True)
        self.paned = paned

        left = ttk.Labelframe(paned, text="VPS-серверы", padding=8)
        right = ttk.Labelframe(paned, text="Команды выбранного VPS", padding=8)
        paned.add(left, weight=1)
        paned.add(right, weight=2)

        saved_cols = self.config_data.settings.column_widths
        self.server_tree = ttk.Treeview(left, columns=("host",), show="tree headings", selectmode="browse", height=12)
        self.server_tree.heading("#0", text="Имя")
        self.server_tree.heading("host", text="Адрес")
        self.server_tree.column("#0", width=160, minwidth=80, stretch=False)
        self.server_tree.column("host", width=220, minwidth=80, stretch=True)
        apply_tree_columns(self.server_tree, saved_cols.get("servers"))
        self.server_tree.pack(fill="both", expand=True)
        self.server_tree.bind("<<TreeviewSelect>>", lambda _e: self._on_server_select())
        self.server_tree.bind("<Double-1>", lambda _e: self._edit_server())
        self.server_tree.bind("<ButtonRelease-1>", self._on_layout_drag, add="+")

        sbtns = ttk.Frame(left)
        sbtns.pack(fill="x", pady=(8, 0))
        ttk.Button(sbtns, text="Добавить", command=self._add_server).pack(side="left", padx=(0, 4))
        ttk.Button(sbtns, text="Изменить", command=self._edit_server).pack(side="left", padx=4)
        ttk.Button(sbtns, text="Копия", command=self._duplicate_server).pack(side="left", padx=4)
        ttk.Button(sbtns, text="Удалить", command=self._delete_server).pack(side="left", padx=4)

        sact = ttk.Frame(left)
        sact.pack(fill="x", pady=(4, 0))
        ttk.Button(sact, text="Файлы", command=self._open_files).pack(side="left")
        ttk.Button(sact, text="Открыть консоль", command=self._open_console).pack(side="left", padx=4)
        ttk.Button(sact, text="PuTTY", command=self._open_putty).pack(side="left", padx=4)
        ttk.Button(sact, text="Проверить связь", command=self._test_connection).pack(side="left", padx=4)

        self.cmd_tree = ttk.Treeview(
            right,
            columns=("command",),
            show="tree headings",
            selectmode="browse",
            height=12,
        )
        self.cmd_tree.heading("#0", text="Название", command=self._sort_commands_by_name)
        self.cmd_tree.heading("command", text="Команда", command=self._sort_commands_by_command)
        self.cmd_tree.column("#0", width=180, minwidth=80, stretch=False)
        self.cmd_tree.column("command", width=420, minwidth=80, stretch=True)
        apply_tree_columns(self.cmd_tree, saved_cols.get("commands"))
        self.cmd_tree.pack(fill="both", expand=True)
        self.cmd_tree.bind("<<TreeviewSelect>>", lambda _e: self._remember_selection())
        self.cmd_tree.bind("<Double-1>", lambda _e: self._edit_command())
        self.cmd_tree.bind("<Control-Up>", lambda _e: self._move_command(-1))
        self.cmd_tree.bind("<Control-Down>", lambda _e: self._move_command(1))
        self.cmd_tree.bind("<ButtonRelease-1>", self._on_layout_drag, add="+")

        corder = ttk.Frame(right)
        corder.pack(fill="x", pady=(8, 0))
        ttk.Button(corder, text="Вверх", command=lambda: self._move_command(-1)).pack(side="left")
        ttk.Button(corder, text="Вниз", command=lambda: self._move_command(1)).pack(side="left", padx=4)
        ttk.Button(corder, text="По имени", command=self._sort_commands_by_name).pack(side="left", padx=4)

        cbtns = ttk.Frame(right)
        cbtns.pack(fill="x", pady=(4, 0))
        ttk.Button(cbtns, text="Добавить", command=self._add_command).pack(side="left", padx=(0, 4))
        ttk.Button(cbtns, text="Изменить", command=self._edit_command).pack(side="left", padx=4)
        ttk.Button(cbtns, text="Копия", command=self._duplicate_command).pack(side="left", padx=4)
        ttk.Button(cbtns, text="Удалить", command=self._delete_command).pack(side="left", padx=4)
        ttk.Button(cbtns, text="Пресеты…", command=self._add_presets).pack(side="left", padx=4)
        self.stop_btn = ttk.Button(cbtns, text="Стоп", command=self._stop, state="disabled")
        self.stop_btn.pack(side="right", padx=(8, 0))
        self.run_btn = ttk.Button(cbtns, text="Запустить  (F5)", style="Run.TButton", command=self._run_selected)
        self.run_btn.pack(side="right")

        quick = ttk.Frame(right)
        quick.pack(fill="x", pady=(8, 0))
        ttk.Label(quick, text="Разовая команда:").pack(side="left")
        self.quick_var = tk.StringVar()
        qentry = ttk.Entry(quick, textvariable=self.quick_var)
        qentry.pack(side="left", fill="x", expand=True, padx=6)
        qentry.bind("<Return>", lambda _e: self._run_quick())
        ttk.Button(quick, text="Выполнить", command=self._run_quick).pack(side="left")

        out_frame = ttk.Labelframe(vpaned, text="Вывод", padding=6)
        out_btns = ttk.Frame(out_frame)
        out_btns.pack(fill="x")
        self.cwd_var = tk.StringVar(value="Папка: ~")
        ttk.Label(out_btns, textvariable=self.cwd_var).pack(side="left")
        ttk.Button(out_btns, text="Очистить", command=self._clear_output).pack(side="right")
        ttk.Button(out_btns, text="Журнал", command=self._open_journal).pack(side="right", padx=(0, 4))
        self.output = tk.Text(
            out_frame,
            height=14,
            wrap="word",
            background="#1e1e1e",
            foreground="#d4d4d4",
            insertbackground="#fff",
            font=("Consolas", 10),
            state="disabled",
        )
        scroll = ttk.Scrollbar(out_frame, command=self.output.yview)
        self.output.configure(yscrollcommand=scroll.set)
        self.output.pack(side="left", fill="both", expand=True, pady=(4, 0))
        scroll.pack(side="right", fill="y", pady=(4, 0))
        self.output.tag_configure("meta", foreground="#9cdcfe")
        self.output.tag_configure("ok", foreground="#6a9955")
        self.output.tag_configure("err", foreground="#f14c4c")
        _bind_copy_on_select(self.output, self._on_output_copied)

        vpaned.add(top, weight=3)
        vpaned.add(out_frame, weight=2)
        paned.bind("<ButtonRelease-1>", self._on_layout_drag, add="+")
        vpaned.bind("<ButtonRelease-1>", self._on_layout_drag, add="+")

        self.status = ttk.Label(self, text="Готово", anchor="w", padding=(10, 4))
        self.status.pack(fill="x", side="bottom")

    def _selected_server(self) -> Server | None:
        sel = self.server_tree.selection()
        if not sel:
            return None
        return self.config_data.server_by_id(sel[0])

    def _selected_command(self) -> Command | None:
        sel = self.cmd_tree.selection()
        if not sel:
            return None
        return self.config_data.command_by_id(sel[0])

    def _on_server_select(self) -> None:
        self._remember_selection()
        self._refresh_commands()
        self._update_cwd_label()

    def _remember_selection(self) -> None:
        server = self._selected_server()
        if server:
            self.config_data.settings.last_server_id = server.id
        cmd = self._selected_command()
        if cmd:
            self.config_data.settings.last_command_id = cmd.id

    def persist(self) -> None:
        self._sync_ui_settings()
        save(self.config_data, self.vault)

    def _sync_ui_settings(self) -> None:
        settings = self.config_data.settings
        try:
            state = self.state()
        except tk.TclError:
            state = "normal"
        if state == "iconic":
            state = getattr(self, "_last_wm_state", settings.window_state or "normal")
        if state == "zoomed":
            settings.window_state = "zoomed"
        elif state == "normal":
            settings.window_state = "normal"
            try:
                geom = self.geometry()
            except tk.TclError:
                geom = ""
            if geom and _GEOM_RE.match(geom):
                settings.window_geometry = geom
        if getattr(self, "paned", None) is not None:
            try:
                settings.sash_pos = max(0, int(self.paned.sashpos(0)))
            except tk.TclError:
                pass
        if getattr(self, "vpaned", None) is not None:
            try:
                settings.vsash_pos = max(0, int(self.vpaned.sashpos(0)))
            except tk.TclError:
                pass
        if getattr(self, "server_tree", None) is not None:
            store_tree_columns(settings, "servers", self.server_tree)
        if getattr(self, "cmd_tree", None) is not None:
            store_tree_columns(settings, "commands", self.cmd_tree)
        self._remember_selection()

    def _restore_window_layout(self) -> None:
        settings = self.config_data.settings
        geom = (settings.window_geometry or "").strip()
        if geom and _geometry_on_screen(geom):
            try:
                self.geometry(geom)
            except tk.TclError:
                self.geometry(_DEFAULT_GEOMETRY)
        else:
            self.geometry(_DEFAULT_GEOMETRY)
        self.update_idletasks()

    def _finish_layout_restore(self) -> None:
        settings = self.config_data.settings
        if settings.window_state == "zoomed":
            try:
                self.state("zoomed")
            except tk.TclError:
                pass
        self.update_idletasks()
        self._restore_sashes()
        self.update_idletasks()
        self._restore_columns()
        self._restoring_layout = False
        self.after(80, self._retry_layout_restore)

    def _retry_layout_restore(self) -> None:
        if not self.winfo_exists():
            return
        self._restoring_layout = True
        try:
            self._restore_sashes()
            self.update_idletasks()
            self._restore_columns()
        finally:
            self._restoring_layout = False

    def _restore_sashes(self) -> None:
        self._restore_sash(self.paned, self.config_data.settings.sash_pos, min_pos=180, min_other=220, vertical=False)
        self._restore_sash(self.vpaned, self.config_data.settings.vsash_pos, min_pos=160, min_other=120, vertical=True)

    def _restore_sash(
        self,
        paned: ttk.Panedwindow | None,
        pos: int,
        *,
        min_pos: int,
        min_other: int,
        vertical: bool,
    ) -> None:
        if pos <= 0 or paned is None:
            return
        try:
            total = int(paned.winfo_height() if vertical else paned.winfo_width())
            if total > min_pos + min_other:
                pos = min(max(min_pos, pos), total - min_other)
            paned.sashpos(0, pos)
        except tk.TclError:
            pass

    def _restore_columns(self) -> None:
        widths = self.config_data.settings.column_widths
        apply_tree_columns(self.server_tree, widths.get("servers"))
        apply_tree_columns(self.cmd_tree, widths.get("commands"))

    def _on_layout_drag(self, _event=None) -> None:
        if getattr(self, "_restoring_layout", False):
            return
        self._sync_ui_settings()

    def _on_window_configure(self, event) -> None:
        if event.widget is not self or getattr(self, "_restoring_layout", False):
            return
        try:
            state = self.state()
        except tk.TclError:
            return
        if state in {"normal", "zoomed"}:
            self._last_wm_state = state
            self.config_data.settings.window_state = state
        if state != "normal":
            return
        if self.winfo_width() < 400 or self.winfo_height() < 300:
            return
        geom = self.geometry()
        if _GEOM_RE.match(geom):
            self.config_data.settings.window_geometry = geom

    def _refresh_servers(self, keep_id: str | None = None) -> None:
        keep_id = keep_id or (self.server_tree.selection()[0] if self.server_tree.selection() else None)
        if not keep_id:
            keep_id = self.config_data.settings.last_server_id
        self.server_tree.delete(*self.server_tree.get_children())
        for server in self.config_data.servers:
            self.server_tree.insert(
                "",
                "end",
                iid=server.id,
                text=server.name,
                values=(f"{server.username}@{server.host}:{server.port}",),
            )
        if keep_id and self.server_tree.exists(keep_id):
            self.server_tree.selection_set(keep_id)
            self.server_tree.see(keep_id)
        elif self.config_data.servers:
            first = self.config_data.servers[0].id
            self.server_tree.selection_set(first)
        self._refresh_commands()

    def _refresh_commands(self) -> None:
        keep = self.cmd_tree.selection()[0] if self.cmd_tree.selection() else None
        self.cmd_tree.delete(*self.cmd_tree.get_children())
        server = self._selected_server()
        if not server:
            return
        for cmd in self.config_data.commands_for(server.id):
            preview = " ".join(cmd.command.split())
            if len(preview) > 90:
                preview = preview[:87] + "…"
            self.cmd_tree.insert("", "end", iid=cmd.id, text=cmd.name, values=(preview,))
        if keep and self.cmd_tree.exists(keep):
            self.cmd_tree.selection_set(keep)
            self.cmd_tree.see(keep)
        else:
            last_cmd = self.config_data.settings.last_command_id
            if last_cmd and self.cmd_tree.exists(last_cmd):
                self.cmd_tree.selection_set(last_cmd)
                self.cmd_tree.see(last_cmd)
        self._remember_selection()
        self._update_cwd_label()

    def _add_server(self) -> None:
        dlg = ServerDialog(self, Server.new(), "Новый VPS", is_new=True)
        self.wait_window(dlg)
        if dlg.result:
            self.config_data.servers.append(dlg.result)
            self.persist()
            self._refresh_servers(dlg.result.id)

    def _edit_server(self) -> None:
        server = self._selected_server()
        if not server:
            return
        dlg = ServerDialog(self, server, f"VPS: {server.name}")
        self.wait_window(dlg)
        if dlg.result:
            idx = next(i for i, s in enumerate(self.config_data.servers) if s.id == server.id)
            self.config_data.servers[idx] = dlg.result
            self.persist()
            self._refresh_servers(dlg.result.id)

    def _duplicate_server(self) -> None:
        server = self._selected_server()
        if not server:
            return
        taken = {s.name for s in self.config_data.servers}
        clone = server.duplicate(_copy_name(server.name, taken))
        idx = next(i for i, item in enumerate(self.config_data.servers) if item.id == server.id)
        self.config_data.servers.insert(idx + 1, clone)
        for cmd in self.config_data.commands_for(server.id):
            self.config_data.commands.append(cmd.duplicate(server_id=clone.id))
        self.persist()
        self._refresh_servers(clone.id)

    def _delete_server(self) -> None:
        server = self._selected_server()
        if not server:
            return
        cmds = self.config_data.commands_for(server.id)
        extra = f" Вместе с ним будут удалены команды: {len(cmds)}." if cmds else ""
        if not messagebox.askyesno("Удалить VPS", f"Удалить «{server.name}»?{extra}", parent=self):
            return
        self.config_data.servers = [s for s in self.config_data.servers if s.id != server.id]
        self.config_data.commands = [c for c in self.config_data.commands if c.server_id != server.id]
        self.persist()
        self._refresh_servers()

    def _move_command(self, delta: int) -> str:
        cmd = self._selected_command()
        if not cmd:
            return "break"
        if not self.config_data.move_command(cmd.id, delta):
            return "break"
        self.persist()
        self._refresh_commands()
        if self.cmd_tree.exists(cmd.id):
            self.cmd_tree.selection_set(cmd.id)
            self.cmd_tree.see(cmd.id)
            self.cmd_tree.focus(cmd.id)
        return "break"

    def _sort_commands_by_name(self) -> None:
        self._sort_commands("name")

    def _sort_commands_by_command(self) -> None:
        self._sort_commands("command")

    def _sort_commands(self, by: str) -> None:
        server = self._selected_server()
        if not server:
            return
        keep = self.cmd_tree.selection()[0] if self.cmd_tree.selection() else None
        self.config_data.sort_commands_for(server.id, by=by)
        self.persist()
        self._refresh_commands()
        if keep and self.cmd_tree.exists(keep):
            self.cmd_tree.selection_set(keep)
            self.cmd_tree.see(keep)
        label = "имени" if by == "name" else "команде"
        self.status.configure(text=f"Команды отсортированы по {label}")

    def _add_command(self) -> None:
        server = self._selected_server()
        if not server:
            messagebox.showinfo("Команда", "Сначала выберите или добавьте VPS.", parent=self)
            return
        if not self.config_data.servers:
            return
        cmd = Command.new(server.id)
        cmd.timeout_sec = self.config_data.settings.default_command_timeout
        dlg = CommandDialog(self, cmd, self.config_data.servers, "Новая команда")
        self.wait_window(dlg)
        if dlg.result:
            self.config_data.commands.append(dlg.result)
            self.persist()
            self._refresh_servers(dlg.result.server_id)
            if self.cmd_tree.exists(dlg.result.id):
                self.cmd_tree.selection_set(dlg.result.id)

    def _edit_command(self) -> None:
        cmd = self._selected_command()
        if not cmd:
            return
        dlg = CommandDialog(self, cmd, self.config_data.servers, f"Команда: {cmd.name}")
        self.wait_window(dlg)
        if dlg.result:
            idx = next(i for i, c in enumerate(self.config_data.commands) if c.id == cmd.id)
            self.config_data.commands[idx] = dlg.result
            self.persist()
            self._refresh_servers(dlg.result.server_id)
            if self.cmd_tree.exists(dlg.result.id):
                self.cmd_tree.selection_set(dlg.result.id)

    def _duplicate_command(self) -> None:
        cmd = self._selected_command()
        if not cmd:
            return
        taken = {c.name for c in self.config_data.commands_for(cmd.server_id)}
        clone = cmd.duplicate(name=_copy_name(cmd.name, taken))
        idx = next(i for i, item in enumerate(self.config_data.commands) if item.id == cmd.id)
        self.config_data.commands.insert(idx + 1, clone)
        self.persist()
        self._refresh_commands()
        if self.cmd_tree.exists(clone.id):
            self.cmd_tree.selection_set(clone.id)
            self.cmd_tree.see(clone.id)

    def _delete_command(self) -> None:
        cmd = self._selected_command()
        if not cmd:
            return
        if not messagebox.askyesno("Удалить команду", f"Удалить «{cmd.name}»?", parent=self):
            return
        self.config_data.commands = [c for c in self.config_data.commands if c.id != cmd.id]
        self.persist()
        self._refresh_commands()

    def _add_presets(self) -> None:
        server = self._selected_server()
        if not server:
            messagebox.showinfo("Пресеты", "Сначала выберите VPS.", parent=self)
            return
        dlg = PresetDialog(self, server)
        self.wait_window(dlg)
        if not dlg.result:
            return
        added = _attach_presets(self.config_data, server.id, dlg.result)
        self.persist()
        self._refresh_commands()
        if added:
            self.status.configure(text=f"Добавлено команд: {added}")
        else:
            messagebox.showinfo(
                "Пресеты",
                "Все выбранные команды уже есть у этого VPS.",
                parent=self,
            )

    def _apply_runtime_settings(self) -> None:
        self.journal.max_entries = self.config_data.settings.journal_max_entries

    def _after_config_import(self) -> None:
        self._apply_runtime_settings()
        keep_id = self.config_data.settings.last_server_id
        self._refresh_servers(keep_id)

    def _open_settings(self) -> None:
        dlg = SettingsDialog(
            self,
            self.config_data,
            self.vault,
            on_apply=self._save_settings,
            on_change_master=self._change_master_password,
            on_check_updates=self._check_updates_manual,
            on_import_done=self._after_config_import,
        )
        self.wait_window(dlg)
        self._apply_runtime_settings()

    def _save_settings(self) -> None:
        self.persist()

    def _export_config(self) -> None:
        from fatty.config_io import write_export

        include_secrets = messagebox.askyesno(
            "Экспорт",
            "Включить пароли VPS в файл?\n\n"
            "Нет — только хосты, команды и (опционально) настройки.",
            parent=self,
        )
        if include_secrets and not messagebox.askyesno(
            "Экспорт",
            "Файл будет содержать пароли в открытом виде.\nПродолжить?",
            parent=self,
        ):
            return
        include_settings = messagebox.askyesno(
            "Экспорт",
            "Включить настройки приложения?",
            parent=self,
        )
        stamp = datetime.now().strftime("%Y-%m-%d")
        path = filedialog.asksaveasfilename(
            parent=self,
            title="Экспорт FaTTY",
            defaultextension=".json",
            initialfile=f"fatty-backup-{stamp}.json",
            filetypes=[("FaTTY backup", "*.json"), ("JSON", "*.json"), ("Все файлы", "*.*")],
        )
        if not path:
            return
        try:
            write_export(
                Path(path),
                self.config_data,
                include_secrets=include_secrets,
                include_settings=include_settings,
            )
        except OSError as exc:
            messagebox.showerror("Экспорт", f"Не удалось сохранить файл:\n{exc}", parent=self)
            return
        messagebox.showinfo("Экспорт", f"Сохранено:\n{path}", parent=self)

    def _import_config(self) -> None:
        from fatty.config_io import ConfigIOError, format_import_summary, import_into_config, read_export

        choice = messagebox.askyesnocancel(
            "Импорт",
            "Да — добавить к текущим VPS и командам\n"
            "Нет — заменить все VPS и команды\n"
            "Отмена",
            parent=self,
        )
        if choice is None:
            return
        mode = "merge" if choice else "replace"
        path = filedialog.askopenfilename(
            parent=self,
            title="Импорт FaTTY",
            filetypes=[("FaTTY backup", "*.json"), ("JSON", "*.json"), ("Все файлы", "*.*")],
        )
        if not path:
            return
        try:
            data = read_export(Path(path))
        except ConfigIOError as exc:
            messagebox.showerror("Импорт", str(exc), parent=self)
            return
        import_settings = messagebox.askyesno(
            "Импорт",
            "Импортировать настройки приложения из файла?",
            parent=self,
        )
        if mode == "replace" and not messagebox.askyesno(
            "Импорт",
            "Текущие VPS и команды будут удалены.\nПродолжить?",
            parent=self,
        ):
            return
        try:
            result = import_into_config(
                self.config_data,
                data,
                mode=mode,
                import_settings=import_settings,
            )
            self.persist()
        except (ConfigIOError, VaultLocked) as exc:
            messagebox.showerror("Импорт", str(exc), parent=self)
            return
        self._after_config_import()
        messagebox.showinfo(
            "Импорт",
            format_import_summary(result, mode=mode),
            parent=self,
        )

    def _maybe_auto_check_updates(self) -> None:
        settings = self.config_data.settings
        if not settings.check_updates_on_start:
            return
        # не чаще раза в сутки
        if time.time() - float(settings.last_update_check or 0) < 24 * 3600:
            return
        self._check_updates(silent=True)

    def _check_updates_manual(self) -> None:
        self._check_updates(silent=False)

    def _check_updates(self, *, silent: bool) -> None:
        if getattr(self, "_update_check_busy", False):
            if not silent:
                self.status.configure(text="Проверка обновлений уже идёт…")
            return
        self._update_check_busy = True
        if not silent:
            self.status.configure(text="Проверка обновлений…")

        def worker() -> None:
            try:
                result = check_for_updates(__version__)
                error: str | None = None
            except UpdateError as exc:
                result = None
                error = str(exc)
            except Exception as exc:  # noqa: BLE001 — показать в UI
                result = None
                error = f"Ошибка проверки: {exc}"
            self.after(0, self._on_update_check_done, result, error, silent)

        threading.Thread(target=worker, daemon=True).start()

    def _on_update_check_done(
        self,
        result: UpdateCheckResult | None,
        error: str | None,
        silent: bool,
    ) -> None:
        self._update_check_busy = False
        if error:
            if not silent:
                self.status.configure(text="Не удалось проверить обновления")
                messagebox.showerror("Обновления", error, parent=self)
            return

        assert result is not None
        self.config_data.settings.last_update_check = time.time()
        try:
            self.persist()
        except Exception:
            pass

        if result.status == "update":
            latest = result.latest or "?"
            self.status.configure(text=f"Доступна версия {latest}")
            url = result.download_url or result.page_url
            msg = (
                f"Доступна новая версия {latest} (у вас {result.current}).\n\n"
                "Открыть страницу загрузки?"
            )
            if messagebox.askyesno("Обновления", msg, parent=self) and url:
                webbrowser.open(url)
            return

        if silent:
            return

        if result.status == "current":
            latest = result.latest or result.current
            self.status.configure(text="Обновлений нет")
            messagebox.showinfo(
                "Обновления",
                f"У вас актуальная версия ({result.current}).\n"
                f"Последний релиз на GitHub: {latest}.",
                parent=self,
            )
            return

        self.status.configure(text="Релизы на GitHub ещё не опубликованы")
        msg = (
            "На GitHub пока нет опубликованных релизов или тегов.\n\n"
            "Открыть страницу релизов?"
        )
        if messagebox.askyesno("Обновления", msg, parent=self) and result.page_url:
            webbrowser.open(result.page_url)

    def _change_master_password(self) -> None:
        dlg = ChangeMasterDialog(
            self,
            self.vault,
            allow_short_master_password=self.config_data.settings.allow_short_master_password,
        )
        self.wait_window(dlg)
        if not dlg.ok:
            return
        try:
            self.persist()
        except VaultLocked as exc:
            messagebox.showerror("Мастер-пароль", str(exc), parent=self)
            return
        messagebox.showinfo("Мастер-пароль", "Мастер-пароль обновлён.", parent=self)

    def _open_config_dir(self) -> None:
        APP_DIR.mkdir(parents=True, exist_ok=True)
        os.startfile(APP_DIR)  # type: ignore[attr-defined]

    def _about(self) -> None:
        messagebox.showinfo(
            f"О {APP_NAME}",
            f"{APP_NAME} {__version__}\n\n"
            "Проверка обновлений: Справка → Проверить обновления…",
            parent=self,
        )

    def _on_output_copied(self, text: str) -> None:
        n = len(text)
        self.status.configure(text=f"Скопировано в буфер ({n} симв.)")

    def _clear_output(self) -> None:
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.configure(state="disabled")

    def _remember_cwd(self, server_id: str, cwd: str) -> None:
        path = (cwd or "").strip()
        if path:
            self._remote_cwd[server_id] = path
        self._update_cwd_label()

    def _update_cwd_label(self) -> None:
        if getattr(self, "cwd_var", None) is None:
            return
        server = self._selected_server()
        if not server:
            self.cwd_var.set("")
            return
        path = self._remote_cwd.get(server.id, "")
        self.cwd_var.set(f"Папка: {path}" if path else "Папка: ~")

    def _append(self, text: str, tag: str | None = None) -> None:
        self.output.configure(state="normal")
        self.output.insert("end", text, (tag,) if tag else ())
        self.output.see("end")
        self.output.configure(state="disabled")

    def _set_busy(self, busy: bool) -> None:
        self._busy = busy
        state = "disabled" if busy else "normal"
        self.run_btn.configure(state=state)
        self.stop_btn.configure(state="normal" if busy else "disabled")

    def _stop(self) -> None:
        if self._session:
            self._session.cancel()

    def _test_connection(self) -> None:
        server = self._selected_server()
        if not server:
            return
        self._run(
            server,
            "echo OK && hostname && whoami && pwd",
            30,
            True,
            title=f"Проверка {server.name}",
            kind="test",
        )

    def _open_console(self) -> None:
        server = self._selected_server()
        if not server:
            messagebox.showinfo("Консоль", "Сначала выберите VPS.", parent=self)
            return
        try:
            ssh_path = (self.config_data.settings.ssh_path or "").strip() or None
            open_system_console(server, ssh_path=ssh_path)
        except SSHError as exc:
            messagebox.showerror("Консоль", str(exc), parent=self)
            return
        extra = ""
        if not server.key_path:
            extra = "  •  пароль, если спросит, введите в окне SSH"
        self.status.configure(text=f"Консоль открыта → {server.name}{extra}")

    def _prompt_putty_setup(self) -> bool:
        """Предложить скачать PuTTY или указать putty.exe. True — путь сохранён, можно повторить."""
        msg = (
            "PuTTY не найден на этом компьютере.\n\n"
            "Да — открыть страницу загрузки\n"
            "Нет — указать путь к putty.exe\n"
            "Отмена — закрыть"
        )
        choice = messagebox.askyesnocancel("PuTTY", msg, parent=self)
        if choice is None:
            return False
        if choice:
            webbrowser.open(PUTTY_DOWNLOAD_URL)
            return False
        initialdir = None
        saved = (self.config_data.settings.putty_path or "").strip()
        if saved:
            initialdir = str(Path(saved).expanduser().parent)
        path = filedialog.askopenfilename(
            parent=self,
            title="Укажите putty.exe",
            initialdir=initialdir,
            filetypes=[("PuTTY", "putty.exe"), ("Исполняемые", "*.exe"), ("Все файлы", "*.*")],
        )
        if not path:
            return False
        candidate = Path(path)
        if not candidate.is_file():
            messagebox.showerror("PuTTY", f"Файл не найден: {candidate}", parent=self)
            return False
        self.config_data.settings.putty_path = str(candidate)
        try:
            self.persist()
        except Exception:
            pass
        return True

    def _open_putty(self) -> None:
        server = self._selected_server()
        if not server:
            messagebox.showinfo("PuTTY", "Сначала выберите VPS.", parent=self)
            return
        while True:
            putty_path = (self.config_data.settings.putty_path or "").strip() or None
            try:
                open_putty_console(server, putty_path=putty_path)
                break
            except PuttyNotFoundError:
                if not self._prompt_putty_setup():
                    return
            except PuttyLaunchError as exc:
                messagebox.showerror("PuTTY", str(exc), parent=self)
                if not self._prompt_putty_setup():
                    return
            except SSHError as exc:
                messagebox.showerror("PuTTY", str(exc), parent=self)
                return
        via = "ключ" if (server.key_path or "").strip() else "пароль"
        self.status.configure(text=f"PuTTY открыт → {server.name}  •  {via}")

    def _open_files(self) -> None:
        server = self._selected_server()
        if not server:
            messagebox.showinfo("Файлы", "Сначала выберите VPS.", parent=self)
            return
        existing = self._files_windows.get(server.id)
        if existing is not None:
            try:
                if existing.winfo_exists():
                    existing.deiconify()
                    existing.lift()
                    existing.focus_force()
                    return
            except tk.TclError:
                pass
        start = guess_start_path(self.config_data.commands_for(server.id))
        win = FilesWindow(self, server, start_path=start)
        self._files_windows[server.id] = win

        def _clear(event) -> None:
            if event.widget is win:
                self._files_windows.pop(server.id, None)

        win.bind("<Destroy>", _clear)

    def _run_selected(self) -> None:
        cmd = self._selected_command()
        server = self._selected_server()
        if not cmd or not server:
            messagebox.showinfo("Запуск", "Выберите VPS и команду.", parent=self)
            return
        if server.id != cmd.server_id:
            server = self.config_data.server_by_id(cmd.server_id)
        if not server:
            messagebox.showerror("Запуск", "VPS для этой команды не найден.", parent=self)
            return
        if self.config_data.settings.confirm_before_run:
            if not messagebox.askyesno(
                "Запуск",
                f"Выполнить «{cmd.name}» на {server.name}\n({server.username}@{server.host})?",
                parent=self,
            ):
                return
        self._run(
            server,
            cmd.command,
            cmd.timeout_sec,
            cmd.login_shell,
            title=cmd.name,
            command_id=cmd.id,
            kind="command",
        )

    def _run_quick(self) -> None:
        server = self._selected_server()
        command = self.quick_var.get().strip()
        if not server:
            messagebox.showinfo("Запуск", "Выберите VPS.", parent=self)
            return
        if not command:
            return
        if self.config_data.settings.confirm_before_run:
            if not messagebox.askyesno(
                "Запуск",
                f"Выполнить разовую команду на {server.name}?",
                parent=self,
            ):
                return
        timeout = self.config_data.settings.default_command_timeout
        self._run(server, command, timeout, True, title="разовая команда", kind="quick")

    def _open_journal(self) -> None:
        existing = self._journal_window
        if existing is not None:
            try:
                if existing.winfo_exists():
                    existing.deiconify()
                    existing.lift()
                    existing.focus_force()
                    return
            except tk.TclError:
                pass
        win = JournalWindow(self, self.journal, on_rerun=self._rerun_journal_entry)
        self._journal_window = win

        def _clear(event) -> None:
            if event.widget is win:
                self._journal_window = None

        win.bind("<Destroy>", _clear)

    def _rerun_journal_entry(self, entry: JournalEntry) -> None:
        server = self.config_data.server_by_id(entry.server_id)
        if server is None:
            for item in self.config_data.servers:
                if (
                    item.host == entry.host
                    and int(item.port or 22) == int(entry.port or 22)
                    and item.username == entry.username
                ):
                    server = item
                    break
        if server is None:
            messagebox.showerror(
                "Журнал",
                "VPS из этой записи больше нет в списке.",
                parent=self,
            )
            return
        timeout = entry.timeout_sec or 180
        self._run(
            server,
            entry.command,
            timeout,
            entry.login_shell,
            title=entry.title or "журнал",
            command_id=entry.command_id,
            kind=entry.kind or "command",
        )

    def _log_run(
        self,
        *,
        server: Server,
        command: str,
        title: str,
        cwd: str,
        timeout: int,
        login_shell: bool,
        command_id: str,
        kind: str,
        started_at: str,
        duration_sec: float,
        exit_code: int | None,
        status: str,
        error: str,
    ) -> None:
        try:
            self.journal.append(
                JournalEntry(
                    id=str(uuid.uuid4()),
                    started_at=started_at,
                    finished_at=now_iso(),
                    duration_sec=duration_sec,
                    server_id=server.id,
                    server_name=server.name,
                    host=server.host,
                    port=server.port or 22,
                    username=server.username,
                    command_id=command_id,
                    title=title,
                    command=command,
                    cwd=cwd,
                    login_shell=login_shell,
                    timeout_sec=timeout,
                    exit_code=exit_code,
                    status=status,
                    kind=kind,
                    error=error,
                )
            )
        except OSError:
            pass

    def _run(
        self,
        server: Server,
        command: str,
        timeout: int,
        login_shell: bool,
        title: str,
        *,
        command_id: str = "",
        kind: str = "command",
    ) -> None:
        if self._busy:
            messagebox.showinfo("Занято", "Дождитесь окончания текущей команды или нажмите Стоп.", parent=self)
            return
        if self.config_data.settings.clear_output_before_run:
            self._clear_output()
        self._set_busy(True)
        cwd = self._remote_cwd.get(server.id, "")
        self.status.configure(text=f"Выполняется: {title} → {server.name}")
        self._append(f"\n{'─' * 60}\n", "meta")
        self._append(f"{title}  •  {server.name}\n", "meta")
        session = SSHSession()
        self._session = session
        started_at = now_iso()
        t0 = time.monotonic()

        def worker() -> None:
            code: int | None = 1
            status = "error"
            error_text = ""
            try:
                result = session.run(
                    server,
                    command,
                    timeout,
                    login_shell,
                    lambda chunk: self.after(0, self._append, chunk),
                    cwd=cwd,
                )
                code = result.exit_code
                status = status_from_exit(code)
                if result.cwd:
                    self.after(0, self._remember_cwd, server.id, result.cwd)
            except SSHError as exc:
                code = None
                status = "error"
                error_text = str(exc)
                self.after(0, self._append, f"\n{exc}\n", "err")
            except Exception as exc:
                code = None
                status = "error"
                error_text = str(exc)
                self.after(0, self._append, f"\nОшибка: {exc}\n", "err")
            else:
                tag = "ok" if code == 0 else "err"
                self.after(0, self._append, f"\n← код выхода {code}\n", tag)

            duration = time.monotonic() - t0

            def done() -> None:
                self._set_busy(False)
                self._session = None
                folder = self._remote_cwd.get(server.id, "")
                extra = f"  •  {folder}" if folder else ""
                shown = "—" if code is None else str(code)
                self.status.configure(text=f"Готово  •  код {shown}  •  {title}{extra}")
                self._log_run(
                    server=server,
                    command=command,
                    title=title,
                    cwd=folder or cwd,
                    timeout=timeout,
                    login_shell=login_shell,
                    command_id=command_id,
                    kind=kind,
                    started_at=started_at,
                    duration_sec=duration,
                    exit_code=code,
                    status=status,
                    error=error_text,
                )

            self.after(0, done)

        threading.Thread(target=worker, daemon=True).start()

    def _on_close(self) -> None:
        if self._busy:
            if not messagebox.askyesno("Выход", "Команда ещё выполняется. Выйти?", parent=self):
                return
            self._stop()
        try:
            self.persist()
        except Exception:
            pass
        self.destroy()


def main() -> None:
    if sys.platform != "win32":
        print(f"{APP_NAME} рассчитан на Windows.")
    _enable_dpi()
    if sys.platform == "win32":
        try:
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("FaTTY")
        except Exception:
            pass
    if not try_become_primary():
        if activate_existing():
            return

    def _excepthook(exc_type, exc, tb) -> None:
        APP_DIR.mkdir(parents=True, exist_ok=True)
        log = APP_DIR / "error.log"
        log.write_text("".join(traceback.format_exception(exc_type, exc, tb)), encoding="utf-8")
        try:
            messagebox.showerror(APP_NAME, f"Необработанная ошибка.\nПодробности: {log}")
        except Exception:
            pass

    sys.excepthook = _excepthook
    bootstrap = tk.Tk()
    bootstrap.withdraw()
    _apply_app_icon(bootstrap)
    config = load()
    vault = SessionVault()
    dlg = MasterPasswordDialog(bootstrap, config, vault)
    bootstrap.wait_window(dlg)
    if not dlg.ok or not vault.unlocked:
        bootstrap.destroy()
        return
    try:
        save(config, vault)
    except Exception as exc:
        messagebox.showerror(APP_NAME, f"Не удалось сохранить хранилище: {exc}")
        bootstrap.destroy()
        return
    bootstrap.destroy()
    app = App(config, vault)
    app.mainloop()


if __name__ == "__main__":
    main()

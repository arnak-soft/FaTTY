from __future__ import annotations

import os
import webbrowser
from collections.abc import Callable
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from fatty import APP_NAME, __version__
from fatty.config_io import (
    ConfigIOError,
    format_import_summary,
    import_into_config,
    read_export,
    write_export,
)
from fatty.layout import PositionedToplevel, parent_layout
from fatty.ssh_runner import PUTTY_DOWNLOAD_URL, find_putty_executable, find_ssh_executable
from fatty.store import APP_DIR, CONFIG_PATH, Config
from fatty.vault import MIN_PASSWORD_LEN, MIN_PASSWORD_LEN_RELAXED, SessionVault, VaultLocked


class SettingsDialog(PositionedToplevel):
    def __init__(
        self,
        parent: tk.Tk,
        config: Config,
        vault: SessionVault,
        *,
        on_apply: Callable[[], None],
        on_change_master: Callable[[], None],
        on_check_updates: Callable[[], None],
        on_import_done: Callable[[], None],
    ) -> None:
        super().__init__(parent)
        self._config = config
        self._vault = vault
        self._on_apply = on_apply
        self._on_change_master = on_change_master
        self._on_check_updates = on_check_updates
        self._on_import_done = on_import_done
        self.title("Настройки")
        self.minsize(520, 460)
        self.transient(parent)

        settings = config.settings
        self.confirm_var = tk.BooleanVar(value=settings.confirm_before_run)
        self.updates_var = tk.BooleanVar(value=settings.check_updates_on_start)
        self.clear_output_var = tk.BooleanVar(value=settings.clear_output_before_run)
        self.timeout_var = tk.StringVar(value=str(settings.default_command_timeout))
        self.journal_var = tk.StringVar(value=str(settings.journal_max_entries))
        self.putty_var = tk.StringVar(value=settings.putty_path)
        self.ssh_var = tk.StringVar(value=settings.ssh_path)
        self.export_secrets_var = tk.BooleanVar(value=False)
        self.export_settings_var = tk.BooleanVar(value=True)
        self.import_settings_var = tk.BooleanVar(value=True)
        self.short_pw_var = tk.BooleanVar(value=settings.allow_short_master_password)
        self.lockout_attempts_var = tk.StringVar(value=str(settings.master_password_max_attempts))
        self.lockout_minutes_var = tk.StringVar(value=str(settings.master_password_lockout_minutes))
        self._advanced_visible = False

        body = ttk.Frame(self, padding=12)
        body.pack(fill="both", expand=True)
        notebook = ttk.Notebook(body)
        notebook.pack(fill="both", expand=True)

        notebook.add(self._build_general_tab(notebook), text="Общие")
        notebook.add(self._build_programs_tab(notebook), text="Программы")
        notebook.add(self._build_data_tab(notebook), text="Данные")
        notebook.add(self._build_security_tab(notebook), text="Безопасность")

        btns = ttk.Frame(body)
        btns.pack(fill="x", pady=(12, 0))
        ttk.Button(btns, text="Отмена", command=self.destroy).pack(side="right", padx=4)
        ttk.Button(btns, text="Сохранить", command=self._save).pack(side="right", padx=4)

        self.bind("<Escape>", lambda _e: self.destroy())
        self.grab_set()
        layout_settings, persist = parent_layout(parent)
        self._setup_layout(layout_settings, "settings", remember_size=True, persist=persist)

    def _section(self, parent: tk.Misc, title: str) -> ttk.Frame:
        frame = ttk.Labelframe(parent, text=title, padding=10)
        frame.pack(fill="x", pady=(0, 10))
        return frame

    def _path_row(
        self,
        parent: ttk.Frame,
        row: int,
        label: str,
        variable: tk.StringVar,
        *,
        title: str,
        filetypes: list[tuple[str, str]],
    ) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", pady=4)
        entry = ttk.Entry(parent, textvariable=variable)
        entry.grid(row=row, column=1, sticky="ew", padx=(8, 4), pady=4)

        def browse() -> None:
            initialdir = None
            saved = variable.get().strip()
            if saved:
                initialdir = str(Path(saved).expanduser().parent)
            path = filedialog.askopenfilename(
                parent=self,
                title=title,
                initialdir=initialdir,
                filetypes=filetypes,
            )
            if path:
                variable.set(path)

        def clear() -> None:
            variable.set("")

        actions = ttk.Frame(parent)
        actions.grid(row=row, column=2, sticky="e", pady=4)
        ttk.Button(actions, text="Обзор…", command=browse, width=9).pack(side="left", padx=(0, 4))
        ttk.Button(actions, text="Сброс", command=clear, width=7).pack(side="left")
        parent.columnconfigure(1, weight=1)

    def _build_general_tab(self, notebook: ttk.Notebook) -> ttk.Frame:
        tab = ttk.Frame(notebook, padding=8)
        run = self._section(tab, "Запуск команд")
        ttk.Checkbutton(
            run,
            text="Спрашивать подтверждение перед запуском",
            variable=self.confirm_var,
        ).pack(anchor="w", pady=2)
        ttk.Checkbutton(
            run,
            text="Очищать панель вывода перед новым запуском",
            variable=self.clear_output_var,
        ).pack(anchor="w", pady=2)
        timeout_row = ttk.Frame(run)
        timeout_row.pack(fill="x", pady=(6, 0))
        ttk.Label(timeout_row, text="Таймаут новых команд, с").pack(side="left")
        ttk.Entry(timeout_row, textvariable=self.timeout_var, width=8).pack(side="left", padx=(8, 0))

        updates = self._section(tab, "Обновления")
        ttk.Checkbutton(
            updates,
            text="Проверять обновления при запуске (не чаще раза в сутки)",
            variable=self.updates_var,
        ).pack(anchor="w", pady=2)
        ttk.Button(updates, text="Проверить сейчас…", command=self._on_check_updates).pack(
            anchor="w", pady=(6, 0)
        )

        journal = self._section(tab, "Журнал")
        journal_row = ttk.Frame(journal)
        journal_row.pack(fill="x")
        ttk.Label(journal_row, text="Хранить записей, не более").pack(side="left")
        ttk.Entry(journal_row, textvariable=self.journal_var, width=8).pack(side="left", padx=(8, 0))
        ttk.Label(
            journal,
            text="Старые записи удаляются автоматически при превышении лимита.",
            foreground="#555",
            wraplength=460,
        ).pack(anchor="w", pady=(6, 0))
        return tab

    def _build_programs_tab(self, notebook: ttk.Notebook) -> ttk.Frame:
        tab = ttk.Frame(notebook, padding=8)
        putty = self._section(tab, "PuTTY")
        self._path_row(
            putty,
            0,
            "putty.exe",
            self.putty_var,
            title="Укажите putty.exe",
            filetypes=[
                ("PuTTY", "putty.exe"),
                ("Исполняемые", "*.exe"),
                ("Все файлы", "*.*"),
            ],
        )
        putty_btns = ttk.Frame(putty)
        putty_btns.grid(row=1, column=0, columnspan=3, sticky="w", pady=(6, 0))
        ttk.Button(putty_btns, text="Скачать PuTTY…", command=self._open_putty_download).pack(
            side="left", padx=(0, 6)
        )
        ttk.Button(putty_btns, text="Проверить путь", command=self._test_putty).pack(side="left")
        ttk.Label(
            putty,
            text="Если путь пустой, FaTTY ищет PuTTY в PATH и стандартных папках установки.",
            foreground="#555",
            wraplength=460,
        ).grid(row=2, column=0, columnspan=3, sticky="w", pady=(8, 0))

        ssh = self._section(tab, "OpenSSH")
        self._path_row(
            ssh,
            0,
            "ssh.exe",
            self.ssh_var,
            title="Укажите ssh.exe",
            filetypes=[
                ("OpenSSH", "ssh.exe"),
                ("Исполняемые", "*.exe"),
                ("Все файлы", "*.*"),
            ],
        )
        ttk.Button(ssh, text="Проверить путь", command=self._test_ssh).grid(
            row=1, column=0, columnspan=3, sticky="w", pady=(6, 0)
        )
        ttk.Label(
            ssh,
            text="Используется кнопкой «Открыть консоль». Пустой путь — поиск в PATH и "
            "C:\\Windows\\System32\\OpenSSH\\.",
            foreground="#555",
            wraplength=460,
        ).grid(row=2, column=0, columnspan=3, sticky="w", pady=(8, 0))
        return tab

    def _build_data_tab(self, notebook: ttk.Notebook) -> ttk.Frame:
        tab = ttk.Frame(notebook, padding=8)
        paths = self._section(tab, "Файлы данных")
        ttk.Label(paths, text=f"Конфиг: {CONFIG_PATH}", wraplength=460).pack(anchor="w", pady=2)
        ttk.Label(paths, text=f"Папка: {APP_DIR}", wraplength=460).pack(anchor="w", pady=2)
        ttk.Button(paths, text="Открыть папку конфига", command=self._open_config_dir).pack(
            anchor="w", pady=(6, 0)
        )

        export = self._section(tab, "Экспорт")
        ttk.Checkbutton(
            export,
            text="Включить пароли VPS (файл будет содержать секреты в открытом виде!)",
            variable=self.export_secrets_var,
        ).pack(anchor="w", pady=2)
        ttk.Checkbutton(
            export,
            text="Включить настройки приложения",
            variable=self.export_settings_var,
        ).pack(anchor="w", pady=2)
        ttk.Button(export, text="Экспорт в файл…", command=self._export).pack(anchor="w", pady=(6, 0))

        imp = self._section(tab, "Импорт")
        ttk.Label(
            imp,
            text="Импорт VPS и команд из файла FaTTY. Режим «Добавить» пропускает дубликаты "
            "(тот же VPS по имени и хосту, та же команда по названию).",
            wraplength=460,
            foreground="#555",
        ).pack(anchor="w", pady=(0, 6))
        ttk.Checkbutton(
            imp,
            text="Импортировать настройки приложения из файла",
            variable=self.import_settings_var,
        ).pack(anchor="w", pady=2)
        imp_btns = ttk.Frame(imp)
        imp_btns.pack(fill="x", pady=(6, 0))
        ttk.Button(imp_btns, text="Добавить к текущим…", command=lambda: self._import("merge")).pack(
            side="left", padx=(0, 6)
        )
        ttk.Button(imp_btns, text="Заменить всё…", command=lambda: self._import("replace")).pack(side="left")
        return tab

    def _build_security_tab(self, notebook: ttk.Notebook) -> ttk.Frame:
        tab = ttk.Frame(notebook, padding=8)
        vault = self._section(tab, "Мастер-пароль")
        ttk.Label(
            vault,
            text="Пароли VPS шифруются мастер-паролем. Без него конфиг нельзя расшифровать.",
            wraplength=460,
        ).pack(anchor="w", pady=(0, 6))
        ttk.Button(vault, text="Сменить мастер-пароль…", command=self._change_master).pack(anchor="w")

        advanced_toggle = ttk.Frame(vault)
        advanced_toggle.pack(fill="x", pady=(10, 0))
        self._advanced_toggle_btn = ttk.Button(
            advanced_toggle,
            text="Дополнительные параметры безопасности…",
            command=self._toggle_advanced_security,
        )
        self._advanced_toggle_btn.pack(anchor="w")

        self._advanced_frame = ttk.Frame(vault)
        ttk.Checkbutton(
            self._advanced_frame,
            text=f"Разрешить короткий мастер-пароль (от {MIN_PASSWORD_LEN_RELAXED} символов)",
            variable=self.short_pw_var,
            command=self._on_short_pw_toggle,
        ).pack(anchor="w", pady=(0, 6))
        ttk.Label(
            self._advanced_frame,
            text=f"При первой настройке по-прежнему требуется не меньше {MIN_PASSWORD_LEN} символов.",
            foreground="#555",
            wraplength=440,
        ).pack(anchor="w", pady=(0, 8))

        lockout_row = ttk.Frame(self._advanced_frame)
        lockout_row.pack(fill="x", pady=2)
        ttk.Label(lockout_row, text="Блокировка после попыток").pack(side="left")
        ttk.Entry(lockout_row, textvariable=self.lockout_attempts_var, width=5).pack(side="left", padx=(8, 4))
        ttk.Label(lockout_row, text="на").pack(side="left")
        ttk.Entry(lockout_row, textvariable=self.lockout_minutes_var, width=5).pack(side="left", padx=(8, 4))
        ttk.Label(lockout_row, text="мин (0 попыток — отключить)").pack(side="left")

        about = self._section(tab, "О программе")
        ttk.Label(about, text=f"{APP_NAME} {__version__}").pack(anchor="w", pady=2)
        ttk.Label(
            about,
            text="SSH-клиент и менеджер команд для нескольких VPS на Windows.",
            wraplength=460,
            foreground="#555",
        ).pack(anchor="w")
        return tab

    def _open_putty_download(self) -> None:
        webbrowser.open(PUTTY_DOWNLOAD_URL)

    def _test_putty(self) -> None:
        path = self.putty_var.get().strip() or None
        found = find_putty_executable(path)
        if found:
            messagebox.showinfo("PuTTY", f"Найден:\n{found}", parent=self)
        else:
            messagebox.showwarning(
                "PuTTY",
                "PuTTY не найден.\nУкажите путь или установите программу.",
                parent=self,
            )

    def _test_ssh(self) -> None:
        path = self.ssh_var.get().strip() or None
        found = find_ssh_executable(path)
        if found:
            messagebox.showinfo("OpenSSH", f"Найден:\n{found}", parent=self)
        else:
            messagebox.showwarning(
                "OpenSSH",
                "ssh.exe не найден.\nУкажите путь или установите компонент OpenSSH Client.",
                parent=self,
            )

    def _open_config_dir(self) -> None:
        APP_DIR.mkdir(parents=True, exist_ok=True)
        os.startfile(APP_DIR)  # type: ignore[attr-defined]

    def _toggle_advanced_security(self) -> None:
        self._advanced_visible = not self._advanced_visible
        if self._advanced_visible:
            self._advanced_frame.pack(fill="x", pady=(8, 0))
            self._advanced_toggle_btn.configure(text="Скрыть дополнительные параметры")
        else:
            self._advanced_frame.pack_forget()
            self._advanced_toggle_btn.configure(text="Дополнительные параметры безопасности…")

    def _change_master(self) -> None:
        self._on_change_master()

    def _on_short_pw_toggle(self) -> None:
        if not self.short_pw_var.get():
            return
        if self._config.settings.allow_short_master_password:
            return
        if not messagebox.askyesno(
            "Безопасность",
            f"Короткий мастер-пароль (от {MIN_PASSWORD_LEN_RELAXED} символов) проще подобрать.\n"
            f"Рекомендуется не короче {MIN_PASSWORD_LEN} символов.\n\n"
            "Вы уверены, что хотите разрешить короткий пароль?",
            parent=self,
        ):
            self.short_pw_var.set(False)

    def _validate(self) -> bool:
        try:
            timeout = int(self.timeout_var.get().strip() or "180")
            if not (1 <= timeout <= 86_400):
                raise ValueError
        except ValueError:
            messagebox.showwarning(
                "Настройки",
                "Таймаут новых команд должен быть числом от 1 до 86400.",
                parent=self,
            )
            return False
        try:
            journal = int(self.journal_var.get().strip() or "5000")
            if not (100 <= journal <= 50_000):
                raise ValueError
        except ValueError:
            messagebox.showwarning(
                "Настройки",
                "Лимит журнала должен быть числом от 100 до 50000.",
                parent=self,
            )
            return False
        for label, raw in (("PuTTY", self.putty_var.get()), ("OpenSSH", self.ssh_var.get())):
            path = raw.strip()
            if path and not Path(path).expanduser().is_file():
                messagebox.showwarning(
                    "Настройки",
                    f"Файл {label} не найден:\n{path}",
                    parent=self,
                )
                return False
        try:
            attempts = int(self.lockout_attempts_var.get().strip() or "5")
            if not (0 <= attempts <= 100):
                raise ValueError
        except ValueError:
            messagebox.showwarning(
                "Настройки",
                "Число попыток блокировки должно быть от 0 до 100 (0 — отключить).",
                parent=self,
            )
            return False
        try:
            lockout_min = int(self.lockout_minutes_var.get().strip() or "20")
            if not (1 <= lockout_min <= 24 * 60):
                raise ValueError
        except ValueError:
            messagebox.showwarning(
                "Настройки",
                "Длительность блокировки должна быть от 1 до 1440 минут.",
                parent=self,
            )
            return False
        return True

    def _apply_to_config(self) -> None:
        settings = self._config.settings
        settings.confirm_before_run = bool(self.confirm_var.get())
        settings.check_updates_on_start = bool(self.updates_var.get())
        settings.clear_output_before_run = bool(self.clear_output_var.get())
        settings.default_command_timeout = int(self.timeout_var.get().strip())
        settings.journal_max_entries = int(self.journal_var.get().strip())
        settings.putty_path = self.putty_var.get().strip()
        settings.ssh_path = self.ssh_var.get().strip()
        settings.allow_short_master_password = bool(self.short_pw_var.get())
        settings.master_password_max_attempts = int(self.lockout_attempts_var.get().strip())
        settings.master_password_lockout_minutes = int(self.lockout_minutes_var.get().strip())

    def _save(self) -> None:
        if not self._validate():
            return
        self._apply_to_config()
        try:
            self._on_apply()
        except VaultLocked as exc:
            messagebox.showerror("Настройки", str(exc), parent=self)
            return
        self.destroy()

    def _export(self) -> None:
        if not self._validate():
            return
        include_secrets = bool(self.export_secrets_var.get())
        if include_secrets:
            if not messagebox.askyesno(
                "Экспорт",
                "Файл будет содержать пароли VPS в открытом виде.\n"
                "Сохраняйте его только в безопасном месте.\n\nПродолжить?",
                parent=self,
            ):
                return
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
                self._config,
                include_secrets=include_secrets,
                include_settings=bool(self.export_settings_var.get()),
            )
        except OSError as exc:
            messagebox.showerror("Экспорт", f"Не удалось сохранить файл:\n{exc}", parent=self)
            return
        messagebox.showinfo("Экспорт", f"Сохранено:\n{path}", parent=self)

    def _import(self, mode: str) -> None:
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

        servers_count = len(data.get("servers") or [])
        commands_count = len(data.get("commands") or [])
        if servers_count == 0 and commands_count == 0:
            messagebox.showinfo("Импорт", "В файле нет VPS и команд.", parent=self)
            return

        if mode == "replace":
            prompt = (
                f"Файл: {Path(path).name}\n"
                f"VPS: {servers_count}, команд: {commands_count}\n\n"
                "Текущие VPS и команды будут удалены и заменены содержимым файла.\n"
                "Продолжить?"
            )
        else:
            prompt = (
                f"Файл: {Path(path).name}\n"
                f"VPS: {servers_count}, команд: {commands_count}\n\n"
                "Новые записи будут добавлены; дубликаты пропущены.\n"
                "Продолжить?"
            )
        if not messagebox.askyesno("Импорт", prompt, parent=self):
            return

        try:
            result = import_into_config(
                self._config,
                data,
                mode=mode,
                import_settings=bool(self.import_settings_var.get()),
            )
            self._on_apply()
        except (ConfigIOError, VaultLocked) as exc:
            messagebox.showerror("Импорт", str(exc), parent=self)
            return

        if result.settings_applied:
            self._load_from_config()
        self._on_import_done()
        messagebox.showinfo(
            "Импорт",
            format_import_summary(result, mode=mode),
            parent=self,
        )

    def _load_from_config(self) -> None:
        settings = self._config.settings
        self.confirm_var.set(settings.confirm_before_run)
        self.updates_var.set(settings.check_updates_on_start)
        self.clear_output_var.set(settings.clear_output_before_run)
        self.timeout_var.set(str(settings.default_command_timeout))
        self.journal_var.set(str(settings.journal_max_entries))
        self.putty_var.set(settings.putty_path)
        self.ssh_var.set(settings.ssh_path)
        self.short_pw_var.set(settings.allow_short_master_password)
        self.lockout_attempts_var.set(str(settings.master_password_max_attempts))
        self.lockout_minutes_var.set(str(settings.master_password_lockout_minutes))

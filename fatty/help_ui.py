from __future__ import annotations

import sys
import tkinter as tk
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from tkinter import ttk

from fatty import APP_NAME
from fatty.layout import PositionedToplevel, apply_tree_columns, parent_layout, store_tree_columns
from fatty.presets import DEFAULT_APP_DIR, DEFAULT_BRANCH, DEFAULT_PM2
from fatty.store import APP_DIR, CONFIG_PATH


def _resource_root() -> Path:
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent))
    return Path(__file__).resolve().parent.parent


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
    def copy_sel(_event=None):
        text = _copy_widget_selection(widget)
        if text and on_copied is not None:
            on_copied(text)
        return "break" if text else None

    widget.bind("<ButtonRelease-1>", copy_sel, add="+")
    widget.bind("<Control-c>", copy_sel)
    widget.bind("<Control-C>", copy_sel)


def _apply_app_icon(window: tk.Toplevel) -> None:
    root = _resource_root()
    ico = root / "assets" / "app.ico"
    png = root / "assets" / "app.png"
    if ico.is_file():
        try:
            window.iconbitmap(str(ico))
        except tk.TclError:
            pass
    if png.is_file():
        try:
            window._app_icon_photo = tk.PhotoImage(file=str(png))  # type: ignore[attr-defined]
            window.iconphoto(True, window._app_icon_photo)  # type: ignore[attr-defined]
        except tk.TclError:
            pass


def _copy_text(window: tk.Misc, text: str) -> None:
    text = (text or "").strip()
    if not text:
        return
    window.clipboard_clear()
    window.clipboard_append(text)
    try:
        window.update_idletasks()
    except tk.TclError:
        pass


@dataclass(frozen=True)
class HelpCommand:
    group: str
    name: str
    command: str
    tip: str


def _help_commands() -> list[HelpCommand]:
    app = DEFAULT_APP_DIR.rstrip("/")
    branch = DEFAULT_BRANCH
    pm2 = DEFAULT_PM2
    return [
        HelpCommand(
            "Деплой",
            "Deploy",
            f"cd {app} && git pull origin {branch} && pm2 restart {pm2}",
            "Типичный выкат: подтянуть ветку и перезапустить процесс. Каталог и имя pm2 подставьте свои — или возьмите пресет.",
        ),
        HelpCommand(
            "Деплой",
            "Git pull",
            f"cd {app} && git pull origin {branch}",
            "Только обновить код, без рестарта.",
        ),
        HelpCommand(
            "Деплой",
            "Git status",
            f"cd {app} && git status -sb && echo && git log -8 --oneline",
            "Грязное ли дерево и какие коммиты на сервере.",
        ),
        HelpCommand(
            "PM2",
            "Restart",
            f"pm2 restart {pm2}",
            "Перезапуск процесса. Имя возьмите из `pm2 list`.",
        ),
        HelpCommand(
            "PM2",
            "Status",
            "pm2 status",
            "Все процессы: online / errored / stopped.",
        ),
        HelpCommand(
            "PM2",
            "Logs",
            f"pm2 logs {pm2} --lines 120 --nostream",
            "Без --nostream команда не завершится: FaTTY не интерактивный терминал.",
        ),
        HelpCommand(
            "Nginx",
            "Проверить конфиг",
            "nginx -t",
            "Всегда перед reload. Если test не проходит — reload не делайте.",
        ),
        HelpCommand(
            "Nginx",
            "Reload",
            "nginx -t && (systemctl reload nginx || service nginx reload)",
            "Мягко перечитать конфиг, без обрыва соединений.",
        ),
        HelpCommand(
            "Nginx",
            "Status",
            "systemctl status nginx --no-pager -l || service nginx status",
            "--no-pager, чтобы команда не зависла в less.",
        ),
        HelpCommand(
            "Сервер",
            "Состояние",
            "hostname; date; uptime; echo; df -hT; echo; free -h",
            "Имя, время, нагрузка, диски и память одним взглядом.",
        ),
        HelpCommand(
            "Сервер",
            "Кто я и где",
            "whoami; pwd; echo; uname -a",
            "Проверка, что зашли тем пользователем и в ту систему.",
        ),
        HelpCommand(
            "Сервер",
            "Процессы",
            "ps aux --sort=-%mem | head -n 20",
            "Кто ест память. head, чтобы вывод был коротким.",
        ),
        HelpCommand(
            "Логи",
            "systemd",
            "journalctl -n 80 --no-pager",
            "Последние строки журнала. Снова --no-pager, иначе зависание.",
        ),
        HelpCommand(
            "Логи",
            "Ошибки nginx",
            "tail -n 80 /var/log/nginx/error.log",
            "Если путь другой — посмотрите sites-enabled.",
        ),
        HelpCommand(
            "Сеть",
            "Кто слушает",
            "ss -tlnp",
            "Открытые TCP-порты. Если ss нет: netstat -tlnp.",
        ),
        HelpCommand(
            "Сеть",
            "Ответ сайта",
            "curl -sI --max-time 8 http://127.0.0.1/",
            "Жив ли nginx/приложение на localhost, без браузера.",
        ),
        HelpCommand(
            "Docker",
            "Контейнеры",
            "docker ps --format 'table {{.Names}}\t{{.Status}}\t{{.Ports}}'",
            "Если стек на Compose, смотрите и `docker compose ps`.",
        ),
        HelpCommand(
            "Docker",
            "Compose",
            "docker compose ps",
            "Из каталога с compose.yaml. Иначе укажите -f.",
        ),
    ]


_START_BLOCKS: list[tuple[str, str]] = [
    (
        "h",
        "Что это",
    ),
    (
        "p",
        f"{APP_NAME} хранит список VPS и команды к ним, затем запускает выбранную "
        "команду по SSH с этого компьютера. Типичный сценарий — git pull и "
        "перезапуск процесса, без ручного набора ssh.",
    ),
    (
        "h",
        "Как начать",
    ),
    (
        "p",
        "1. Добавьте VPS: имя, хост или IP, порт (обычно 22), логин, пароль и/или SSH-ключ.\n"
        "2. Команды сами не создаются. Нажмите «Пресеты…» или «Добавить».\n"
        "3. Выберите команду и запустите: F5, двойной клик или Enter.\n"
        "4. Смотрите вывод внизу. «Стоп» обрывает сессию.",
    ),
    (
        "h",
        "Окно",
    ),
    (
        "p",
        "Слева — серверы, справа — команды выбранного VPS, внизу — лог.\n"
        "Колонка «Последний раз» берётся из журнала: код и время прошлого запуска.\n"
        "Строка «Разовая команда» выполняет текст один раз, не сохраняя.\n"
        "«Папка» — каталог на сервере, в котором пойдёт следующий запуск. "
        "«Сбросить в ~» возвращает в домашнюю.",
    ),
    (
        "h",
        "Файлы, консоль, PuTTY",
    ),
    (
        "p",
        "«Файлы» — просмотр каталога по SFTP, загрузка и скачивание.\n"
        "«Открыть консоль» — системный SSH. «PuTTY» подставляет пароль или ключ из карточки "
        "(OpenSSH-ключ FaTTY сам переводит в .ppk).\n"
        "Интерактивное (htop, nano, less, top) запускайте в консоли или PuTTY, не через F5.",
    ),
    (
        "h",
        "Журнал и настройки",
    ),
    (
        "p",
        "Каждый запуск пишется в журнал (Ctrl+J): можно повторить, скопировать, сохранить в файл.\n"
        "Настройки (Ctrl+,): подтверждение перед запуском, обновления, журнал, пути к PuTTY и ssh, "
        "экспорт и импорт, мастер-пароль.",
    ),
]

_KEYS_BLOCKS: list[tuple[str, str]] = [
    ("h", "Главное окно"),
    (
        "pre",
        "F5              запустить выбранную команду\n"
        "F2              изменить команду\n"
        "Enter           запустить (фокус в списке команд)\n"
        "Ctrl+↑ / Ctrl+↓ порядок команд\n"
        "Ctrl+J          журнал запусков\n"
        "Ctrl+,          настройки\n"
        "F1              эта справка",
    ),
    ("h", "Вывод и диалоги"),
    (
        "pre",
        "Выделение мышью  сразу копирует текст (как в PuTTY)\n"
        "Ctrl+C           копировать выделение в выводе\n"
        "Escape           закрыть диалог\n"
        "Enter            в разовой команде — выполнить",
    ),
    ("h", "Файлы (SFTP)"),
    (
        "pre",
        "Enter / двойной клик  открыть папку или скачать файл\n"
        "Backspace / Alt+↑     на уровень вверх",
    ),
]

_TIPS_BLOCKS: list[tuple[str, str]] = [
    ("h", "Команды и запуск"),
    (
        "p",
        "• Login-shell (bash -lc) лучше не выключать: иначе на сервере может не быть PATH из .bashrc "
        "(не найдутся pm2, node, git).\n"
        "• FaTTY помнит рабочую папку между запусками. Случайный cd уедет в следующий Deploy — "
        "смотрите строку «Папка» и при необходимости жмите «Сбросить в ~». "
        "Пресеты с собственным cd безопаснее.\n"
        "• Одновременно выполняется одна команда. Не успела — «Стоп» или дождитесь конца.\n"
        "• Не запускайте через F5 интерактивное: top, htop, less, vim, nano, `pm2 logs` без --nostream. "
        "Они не завершатся. Для этого — консоль или PuTTY.\n"
        "• Для sudo нужен NOPASSWD или ключ без запроса пароля. Иначе команда зависнет на Password:.\n"
        "• Таймаут в карточке команды — если процесс молчит слишком долго (логи, долгий git).",
    ),
    ("h", "Пресеты"),
    (
        "p",
        f"• Кнопка «Пресеты…» подставляет Deploy, git, pm2 и nginx. Каталог по умолчанию — "
        f"{DEFAULT_APP_DIR}, ветка {DEFAULT_BRANCH}, процесс {DEFAULT_PM2}. Поменяйте поля в диалоге "
        "перед добавлением.\n"
        "• Уже существующие названия не дублируются.",
    ),
    ("h", "Безопасность и данные"),
    (
        "p",
        "• Мастер-пароль нельзя восстановить. Без него сохранённые пароли VPS не открыть.\n"
        "• Пароли в конфиге зашифрованы. Файл бесполезен на другом компьютере и у другого пользователя Windows.\n"
        "• Журнал не хранит пароли — только имя VPS, адрес, текст команды и код выхода.\n"
        "• Экспорт с паролями — только на свой носитель. Для «скелета» серверов экспортируйте без секретов.",
    ),
    ("h", "Мелочи, которые экономят время"),
    (
        "p",
        "• Двойной клик по команде запускает, а не открывает редактор. Правка — F2 или «Изменить».\n"
        "• «Проверить связь» гоняет hostname / whoami / pwd — удобно сразу после добавления VPS.\n"
        "• Выделение в логе копируется само. Не нужно Ctrl+C, если привыкли к PuTTY.\n"
        "• Подтверждение перед каждым F5 можно выключить в настройках, если мешает.\n"
        "• nginx reload только после nginx -t. Иначе легко уронить сайт опечаткой в конфиге.",
    ),
    ("h", "Где лежат файлы"),
    (
        "p",
        f"Конфиг: {CONFIG_PATH}\n"
        f"Журнал: {APP_DIR / 'journal.jsonl'}\n"
        f"Known hosts: {APP_DIR / 'known_hosts'}\n"
        "Папка: меню Файл → Открыть папку конфига.",
    ),
]


InsertQuick = Callable[[str], None]


class HelpWindow(PositionedToplevel):
    def __init__(self, parent: tk.Tk, *, on_insert_quick: InsertQuick | None = None) -> None:
        super().__init__(parent)
        self.title(f"Справка — {APP_NAME}")
        _apply_app_icon(self)
        self.minsize(700, 480)
        self.geometry("840x600")
        self._on_insert_quick = on_insert_quick
        self._commands = _help_commands()
        self._by_iid: dict[str, HelpCommand] = {}

        self._build()
        settings, persist = parent_layout(parent)
        if settings is not None:
            apply_tree_columns(self.tree, settings.column_widths.get("help"))
        self._setup_layout(settings, "help", remember_size=True, persist=persist)
        self.tree.bind("<ButtonRelease-1>", self._on_columns_drag, add="+")
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.bind("<Escape>", lambda _e: self._on_close())
        self.after(40, self._restore_columns)

    def _restore_columns(self) -> None:
        settings = getattr(self, "_layout_settings", None)
        if settings is not None:
            apply_tree_columns(self.tree, settings.column_widths.get("help"))

    def _build(self) -> None:
        root = ttk.Frame(self, padding=8)
        root.pack(fill="both", expand=True)

        notebook = ttk.Notebook(root)
        notebook.pack(fill="both", expand=True)
        self._notebook = notebook

        notebook.add(self._text_tab(notebook, _START_BLOCKS), text="Как пользоваться")
        notebook.add(self._text_tab(notebook, _KEYS_BLOCKS), text="Клавиши")
        notebook.add(self._commands_tab(notebook), text="Команды")
        notebook.add(self._text_tab(notebook, _TIPS_BLOCKS), text="Советы")

        bottom = ttk.Frame(root)
        bottom.pack(fill="x", pady=(8, 0))
        self.status_var = tk.StringVar(value="Выделение в тексте копируется в буфер.")
        ttk.Label(bottom, textvariable=self.status_var).pack(side="left")
        ttk.Button(bottom, text="Закрыть", command=self._on_close).pack(side="right")

    def _text_tab(self, parent: ttk.Notebook, blocks: list[tuple[str, str]]) -> ttk.Frame:
        tab = ttk.Frame(parent, padding=8)
        text = tk.Text(
            tab,
            wrap="word",
            font=("Segoe UI", 10),
            relief="flat",
            padx=8,
            pady=8,
            cursor="arrow",
        )
        scroll = ttk.Scrollbar(tab, command=text.yview)
        text.configure(yscrollcommand=scroll.set)
        text.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")
        text.tag_configure("h", font=("Segoe UI", 11, "bold"), spacing1=12, spacing3=4)
        text.tag_configure("p", font=("Segoe UI", 10), spacing3=8, lmargin1=0, lmargin2=0)
        text.tag_configure("pre", font=("Consolas", 10), spacing3=10, lmargin1=8, lmargin2=8)
        for kind, body in blocks:
            text.insert("end", body.strip() + "\n", (kind,))
        text.configure(state="disabled")
        _bind_copy_on_select(text, self._on_copied)
        return tab

    def _commands_tab(self, parent: ttk.Notebook) -> ttk.Frame:
        tab = ttk.Frame(parent, padding=8)
        hint = ttk.Label(
            tab,
            text="Частые команды для VPS. Двойной клик или «Копировать» — в буфер. "
            "«В разовую» подставляет строку на главном окне.",
            wraplength=760,
        )
        hint.pack(anchor="w", pady=(0, 6))

        table = ttk.Frame(tab)
        table.pack(fill="both", expand=True)
        self.tree = ttk.Treeview(
            table,
            columns=("command",),
            show="tree headings",
            selectmode="browse",
            height=12,
        )
        self.tree.heading("#0", text="Команда")
        self.tree.heading("command", text="Строка")
        self.tree.column("#0", width=200, minwidth=120, stretch=False)
        self.tree.column("command", width=480, minwidth=160, stretch=True)
        scroll = ttk.Scrollbar(table, command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")
        self.tree.bind("<<TreeviewSelect>>", lambda _e: self._show_command())
        self.tree.bind("<Double-1>", lambda _e: self._copy_command())
        self.tree.bind("<Return>", lambda _e: self._copy_command())

        self.details = tk.Text(
            tab,
            height=5,
            wrap="word",
            font=("Consolas", 10),
            state="disabled",
        )
        self.details.pack(fill="x", pady=(8, 0))
        _bind_copy_on_select(self.details, self._on_copied)

        btns = ttk.Frame(tab)
        btns.pack(fill="x", pady=(8, 0))
        ttk.Button(btns, text="Копировать", command=self._copy_command).pack(side="left")
        if self._on_insert_quick is not None:
            ttk.Button(btns, text="В разовую", command=self._insert_quick).pack(side="left", padx=4)

        grouped: dict[str, list[HelpCommand]] = {}
        for item in self._commands:
            grouped.setdefault(item.group, []).append(item)
        for group, items in grouped.items():
            parent_id = f"g:{group}"
            self.tree.insert("", "end", iid=parent_id, text=group, values=("",), open=True)
            for index, item in enumerate(items):
                iid = f"{group}:{index}"
                preview = item.command if len(item.command) <= 90 else item.command[:87] + "…"
                self.tree.insert(parent_id, "end", iid=iid, text=item.name, values=(preview,))
                self._by_iid[iid] = item
        first = next(iter(self._by_iid), None)
        if first:
            self.tree.selection_set(first)
            self.tree.see(first)
        self._show_command()
        return tab

    def _selected_command(self) -> HelpCommand | None:
        sel = self.tree.selection()
        if not sel:
            return None
        return self._by_iid.get(sel[0])

    def _show_command(self) -> None:
        item = self._selected_command()
        self.details.configure(state="normal")
        self.details.delete("1.0", "end")
        if item is None:
            self.details.insert("1.0", "Выберите команду в списке.")
        else:
            self.details.insert("1.0", f"{item.command}\n\n{item.tip}")
        self.details.configure(state="disabled")

    def _on_copied(self, text: str) -> None:
        self.status_var.set(f"Скопировано ({len(text)} симв.)")

    def _copy_command(self) -> str:
        item = self._selected_command()
        if item is None:
            return "break"
        _copy_text(self, item.command)
        self.status_var.set(f"Скопировано: {item.name}")
        return "break"

    def _insert_quick(self) -> None:
        item = self._selected_command()
        if item is None or self._on_insert_quick is None:
            return
        self._on_insert_quick(item.command)
        self.status_var.set(f"В разовой команде: {item.name}")

    def show_tab(self, name: str) -> None:
        for tab_id in self._notebook.tabs():
            if self._notebook.tab(tab_id, "text") == name:
                self._notebook.select(tab_id)
                return

    def _on_columns_drag(self, _event=None) -> None:
        store_tree_columns(getattr(self, "_layout_settings", None), "help", self.tree)

    def _on_close(self) -> None:
        self._on_columns_drag()
        self.destroy()

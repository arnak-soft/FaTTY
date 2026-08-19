from __future__ import annotations

import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, simpledialog, ttk

from fatty import APP_NAME
from fatty.sftp import (
    RemoteEntry,
    SFTPError,
    SFTPSession,
    TransferCancelled,
    format_mtime,
    format_size,
)
from fatty.store import Server


def _resource_root() -> Path:
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent))
    return Path(__file__).resolve().parent.parent


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


class FilesWindow(tk.Toplevel):
    def __init__(self, parent: tk.Tk, server: Server, start_path: str = ".") -> None:
        super().__init__(parent)
        self.title(f"Файлы — {server.name or server.host}")
        _apply_app_icon(self)
        self.minsize(640, 420)
        self.geometry("780x520")

        self._server = server
        self._start_path = start_path or "."
        self._session = SFTPSession()
        self._busy = False
        self._entries: dict[str, RemoteEntry] = {}

        self._build()
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.bind("<Alt-Up>", lambda _e: self._go_up())
        self.after(80, self._connect)

    def _build(self) -> None:
        root = ttk.Frame(self, padding=8)
        root.pack(fill="both", expand=True)

        path_row = ttk.Frame(root)
        path_row.pack(fill="x")
        ttk.Button(path_row, text="Вверх", command=self._go_up).pack(side="left")
        ttk.Button(path_row, text="Обновить", command=self._refresh).pack(side="left", padx=(4, 8))
        ttk.Label(path_row, text="Путь").pack(side="left")
        self.path_var = tk.StringVar(value="подключение…")
        path_entry = ttk.Entry(path_row, textvariable=self.path_var)
        path_entry.pack(side="left", fill="x", expand=True, padx=6)
        path_entry.bind("<Return>", lambda _e: self._go_path())
        ttk.Button(path_row, text="Перейти", command=self._go_path).pack(side="left")

        tree_frame = ttk.Frame(root)
        tree_frame.pack(fill="both", expand=True, pady=(8, 0))
        self.tree = ttk.Treeview(
            tree_frame,
            columns=("size", "mtime", "kind"),
            show="tree headings",
            selectmode="browse",
        )
        self.tree.heading("#0", text="Имя")
        self.tree.heading("size", text="Размер")
        self.tree.heading("mtime", text="Изменён")
        self.tree.heading("kind", text="Тип")
        self.tree.column("#0", width=280)
        self.tree.column("size", width=90, anchor="e")
        self.tree.column("mtime", width=140)
        self.tree.column("kind", width=110)
        scroll = ttk.Scrollbar(tree_frame, command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")
        self.tree.bind("<Double-1>", lambda _e: self._activate())
        self.tree.bind("<Return>", lambda _e: self._activate())
        self.tree.bind("<BackSpace>", lambda _e: self._go_up())

        actions = ttk.Frame(root)
        actions.pack(fill="x", pady=(8, 0))
        self.upload_btn = ttk.Button(actions, text="Загрузить…", command=self._upload)
        self.upload_btn.pack(side="left")
        self.download_btn = ttk.Button(actions, text="Скачать…", command=self._download)
        self.download_btn.pack(side="left", padx=4)
        self.mkdir_btn = ttk.Button(actions, text="Новая папка", command=self._mkdir)
        self.mkdir_btn.pack(side="left", padx=4)
        self.delete_btn = ttk.Button(actions, text="Удалить", command=self._delete)
        self.delete_btn.pack(side="left", padx=4)
        self.stop_btn = ttk.Button(actions, text="Стоп", command=self._stop, state="disabled")
        self.stop_btn.pack(side="right")

        progress_row = ttk.Frame(root)
        progress_row.pack(fill="x", pady=(8, 0))
        self.progress = ttk.Progressbar(progress_row, mode="determinate")
        self.progress.pack(side="left", fill="x", expand=True)
        self.progress_label = ttk.Label(progress_row, text="", width=22, anchor="e")
        self.progress_label.pack(side="right", padx=(8, 0))

        self.status = ttk.Label(self, text="Подключение…", anchor="w", padding=(10, 4))
        self.status.pack(fill="x", side="bottom")
        self._set_controls(False)

    def _set_controls(self, enabled: bool) -> None:
        state = "normal" if enabled else "disabled"
        for widget in (
            self.upload_btn,
            self.download_btn,
            self.mkdir_btn,
            self.delete_btn,
        ):
            widget.configure(state=state)

    def _set_busy(self, busy: bool, transferring: bool = False) -> None:
        self._busy = busy
        self._set_controls(not busy)
        self.stop_btn.configure(state="normal" if transferring else "disabled")
        if not transferring:
            self.progress.configure(value=0, maximum=100)
            self.progress_label.configure(text="")

    def _alive(self) -> bool:
        try:
            return bool(self.winfo_exists())
        except tk.TclError:
            return False

    def _ui(self, fn, *args) -> None:
        if self._alive():
            fn(*args)

    def _connect(self) -> None:
        self._set_busy(True)
        self.status.configure(text=f"Подключение к {self._server.username}@{self._server.host}…")

        def worker() -> None:
            try:
                self._session.connect(self._server, self._start_path)
                entries = self._session.listdir()
            except SFTPError as exc:
                self.after(0, self._ui, self._connect_failed, str(exc))
                return
            except Exception as exc:
                self.after(0, self._ui, self._connect_failed, str(exc))
                return
            self.after(0, self._ui, self._connected, entries)

        threading.Thread(target=worker, daemon=True).start()

    def _connect_failed(self, message: str) -> None:
        messagebox.showerror("Файлы", message, parent=self)
        self._on_close()

    def _connected(self, entries: list[RemoteEntry]) -> None:
        self._fill(entries)
        self._set_busy(False)
        self.status.configure(text=f"Подключено  •  {self._server.name}")

    def _fill(self, entries: list[RemoteEntry]) -> None:
        self._entries = {entry.name: entry for entry in entries}
        self.path_var.set(self._session.remote_cwd or ".")
        for item in self.tree.get_children():
            self.tree.delete(item)
        for entry in entries:
            size = "" if entry.is_dir else format_size(entry.size)
            self.tree.insert(
                "",
                "end",
                iid=entry.name,
                text=entry.name,
                values=(size, format_mtime(entry.mtime), entry.kind_label),
            )

    def _selected(self) -> RemoteEntry | None:
        sel = self.tree.selection()
        if not sel:
            return None
        return self._entries.get(sel[0])

    def _run_list(self, action, ok_status: str) -> None:
        if self._busy:
            return
        self._set_busy(True)
        self.status.configure(text="Чтение каталога…")

        def worker() -> None:
            try:
                action()
                entries = self._session.listdir()
            except SFTPError as exc:
                self.after(0, self._ui, self._op_failed, str(exc))
                return
            except Exception as exc:
                self.after(0, self._ui, self._op_failed, str(exc))
                return
            self.after(0, self._ui, self._listed, entries, ok_status)

        threading.Thread(target=worker, daemon=True).start()

    def _listed(self, entries: list[RemoteEntry], ok_status: str) -> None:
        self._fill(entries)
        self._set_busy(False)
        self.status.configure(text=ok_status)

    def _op_failed(self, message: str) -> None:
        self._set_busy(False)
        self.status.configure(text="Ошибка")
        if self._alive():
            messagebox.showerror("Файлы", message, parent=self)

    def _refresh(self) -> None:
        self._run_list(lambda: None, "Обновлено")

    def _go_up(self) -> None:
        self._run_list(self._session.go_up, "Готово")

    def _go_path(self) -> None:
        path = self.path_var.get().strip()
        if not path:
            return
        self._run_list(lambda: self._session.enter(path), "Готово")

    def _activate(self) -> None:
        entry = self._selected()
        if entry is None:
            return
        if entry.is_dir:
            self._run_list(lambda: self._session.enter(entry.path), "Готово")
            return
        self._download(entry)

    def _mkdir(self) -> None:
        if self._busy:
            return
        name = simpledialog.askstring("Новая папка", "Имя папки:", parent=self)
        if not name:
            return
        self._run_list(lambda: self._session.mkdir(name), f"Создана папка «{name.strip()}»")

    def _delete(self) -> None:
        entry = self._selected()
        if entry is None or self._busy:
            return
        kind = "папку" if entry.is_dir else "файл"
        if not messagebox.askyesno("Удалить", f"Удалить {kind} «{entry.name}»?", parent=self):
            return
        self._run_list(lambda: self._session.remove(entry), f"Удалено «{entry.name}»")

    def _upload(self) -> None:
        if self._busy:
            return
        path = filedialog.askopenfilename(parent=self, title="Загрузить на сервер")
        if not path:
            return
        local = Path(path)
        dest = local.name

        def after_exists(exists: bool) -> None:
            if exists and not messagebox.askyesno(
                "Файл есть",
                f"«{dest}» уже есть на сервере. Заменить?",
                parent=self,
            ):
                return
            self._start_transfer(upload=True, local=local, remote=dest, size=local.stat().st_size)

        self._check_exists(dest, after_exists)

    def _download(self, entry: RemoteEntry | None = None) -> None:
        if self._busy:
            return
        item = entry or self._selected()
        if item is None:
            messagebox.showinfo("Скачать", "Выберите файл.", parent=self)
            return
        if item.is_dir:
            messagebox.showinfo("Скачать", "Скачивание папок пока не поддерживается.", parent=self)
            return
        dest = filedialog.asksaveasfilename(parent=self, title="Сохранить как", initialfile=item.name)
        if not dest:
            return
        local = Path(dest)
        if local.exists() and not messagebox.askyesno(
            "Файл есть",
            f"«{local.name}» уже есть на компьютере. Заменить?",
            parent=self,
        ):
            return
        self._start_transfer(upload=False, local=local, remote=item.name, size=item.size)

    def _check_exists(self, name: str, done) -> None:
        self._set_busy(True)

        def worker() -> None:
            try:
                exists = self._session.exists(name)
            except SFTPError as exc:
                self.after(0, self._ui, self._op_failed, str(exc))
                return
            self.after(0, self._ui, self._exists_checked, exists, done)

        threading.Thread(target=worker, daemon=True).start()

    def _exists_checked(self, exists: bool, done) -> None:
        self._set_busy(False)
        done(exists)

    def _start_transfer(self, *, upload: bool, local: Path, remote: str, size: int) -> None:
        self._set_busy(True, transferring=True)
        self.progress.configure(value=0, maximum=max(size, 1))
        label = f"Загрузка «{local.name}»…" if upload else f"Скачивание «{remote}»…"
        self.status.configure(text=label)
        self._set_progress(0, size)

        def on_progress(sent: int, total: int) -> None:
            self.after(0, self._ui, self._set_progress, sent, total)

        def worker() -> None:
            try:
                if upload:
                    self._session.upload(local, remote, on_progress)
                else:
                    self._session.download(remote, local, size, on_progress)
                entries = self._session.listdir()
            except TransferCancelled:
                self.after(0, self._ui, self._transfer_done, None, "Передача прервана", True)
                return
            except SFTPError as exc:
                self.after(0, self._ui, self._transfer_done, None, str(exc), False)
                return
            except Exception as exc:
                self.after(0, self._ui, self._transfer_done, None, str(exc), False)
                return
            done = f"Загружено «{local.name}»" if upload else f"Скачано «{remote}»"
            self.after(0, self._ui, self._transfer_done, entries, done, True)

        threading.Thread(target=worker, daemon=True).start()

    def _set_progress(self, sent: int, total: int) -> None:
        maximum = max(total, 1)
        self.progress.configure(maximum=maximum, value=min(sent, maximum))
        if total > 0:
            self.progress_label.configure(text=f"{format_size(sent)} / {format_size(total)}")
        else:
            self.progress_label.configure(text=format_size(sent))

    def _transfer_done(self, entries: list[RemoteEntry] | None, message: str, ok: bool) -> None:
        if entries is not None:
            self._fill(entries)
        self._set_busy(False)
        self.status.configure(text=message)
        if not ok and self._alive():
            messagebox.showerror("Файлы", message, parent=self)

    def _stop(self) -> None:
        self._session.cancel_transfer()
        self.status.configure(text="Остановка…")

    def _on_close(self) -> None:
        self._session.cancel_transfer()
        self._session.close()
        self.destroy()

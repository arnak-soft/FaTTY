from __future__ import annotations

import sys
import tkinter as tk
from collections.abc import Callable
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from fatty.journal import Journal, JournalEntry, KIND_LABELS, STATUS_LABELS
from fatty.layout import PositionedToplevel, apply_tree_columns, parent_layout, store_tree_columns
from fatty.theme import apply_result_tags, style_code


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


def _bind_copy_on_select(widget: tk.Text) -> None:
    def copy_sel(_event=None):
        text = _copy_widget_selection(widget)
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


RerunCb = Callable[[JournalEntry], None]


class JournalWindow(PositionedToplevel):
    def __init__(self, parent: tk.Tk, journal: Journal, *, on_rerun: RerunCb | None = None) -> None:
        super().__init__(parent)
        self.title("Журнал команд")
        _apply_app_icon(self)
        self.minsize(780, 460)
        self.geometry("960x580")

        self._journal = journal
        self._on_rerun = on_rerun
        self._entries: list[JournalEntry] = []
        self._by_id: dict[str, JournalEntry] = {}
        self._reload_after: str | None = None

        self._build()
        settings, persist = parent_layout(parent)
        if settings is not None:
            apply_tree_columns(self.tree, settings.column_widths.get("journal"))
        self._setup_layout(settings, "journal", remember_size=True, persist=persist)
        self.tree.bind("<ButtonRelease-1>", self._on_columns_drag, add="+")
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.bind("<Escape>", lambda _e: self._on_close())
        self._journal.add_listener(self._on_journal_changed)
        self.after(40, self._restore_columns)
        self._reload()

    def _restore_columns(self) -> None:
        settings = getattr(self, "_layout_settings", None)
        if settings is not None:
            apply_tree_columns(self.tree, settings.column_widths.get("journal"))

    def _build(self) -> None:
        root = ttk.Frame(self, padding=8)
        root.pack(fill="both", expand=True)

        filters = ttk.Frame(root)
        filters.pack(fill="x")
        ttk.Label(filters, text="VPS:").pack(side="left")
        self.vps_var = tk.StringVar(value="Все")
        self.vps_combo = ttk.Combobox(filters, textvariable=self.vps_var, state="readonly", width=28)
        self.vps_combo.pack(side="left", padx=(4, 12))
        self.vps_combo.bind("<<ComboboxSelected>>", lambda _e: self._apply_filter())
        ttk.Label(filters, text="Поиск:").pack(side="left")
        self.search_var = tk.StringVar()
        search = ttk.Entry(filters, textvariable=self.search_var, width=32)
        search.pack(side="left", padx=(4, 0), fill="x", expand=True)
        self.search_var.trace_add("write", lambda *_a: self._apply_filter())

        paned = ttk.Panedwindow(root, orient="vertical")
        paned.pack(fill="both", expand=True, pady=(8, 0))

        table = ttk.Frame(paned)
        self.tree = ttk.Treeview(
            table,
            columns=("vps", "title", "command", "result", "duration"),
            show="tree headings",
            selectmode="browse",
            height=12,
        )
        self.tree.heading("#0", text="Дата и время")
        self.tree.heading("vps", text="VPS")
        self.tree.heading("title", text="Название")
        self.tree.heading("command", text="Команда")
        self.tree.heading("result", text="Код")
        self.tree.heading("duration", text="Длит.")
        self.tree.column("#0", width=160, minwidth=120, stretch=False)
        self.tree.column("vps", width=140, minwidth=80, stretch=False)
        self.tree.column("title", width=140, minwidth=80, stretch=False)
        self.tree.column("command", width=280, minwidth=80, stretch=True)
        self.tree.column("result", width=80, minwidth=50, stretch=False)
        self.tree.column("duration", width=80, minwidth=50, stretch=False)
        scroll = ttk.Scrollbar(table, command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")
        self.tree.bind("<<TreeviewSelect>>", lambda _e: self._show_details())
        self.tree.bind("<Double-1>", lambda _e: self._rerun())
        paned.add(table, weight=3)

        detail_frame = ttk.Labelframe(paned, text="Подробности", padding=6)
        self.details = tk.Text(
            detail_frame,
            height=10,
            wrap="word",
            state="disabled",
        )
        style_code(self.details)
        dscroll = ttk.Scrollbar(detail_frame, command=self.details.yview)
        self.details.configure(yscrollcommand=dscroll.set)
        self.details.pack(side="left", fill="both", expand=True)
        dscroll.pack(side="right", fill="y")
        _bind_copy_on_select(self.details)
        paned.add(detail_frame, weight=2)

        btns = ttk.Frame(root)
        btns.pack(fill="x", pady=(8, 0))
        ttk.Button(btns, text="Повторить", style="Accent.TButton", command=self._rerun).pack(side="left")
        ttk.Button(btns, text="Копировать", command=self._copy_selected).pack(side="left", padx=4)
        ttk.Button(btns, text="Сохранить как…", command=self._export).pack(side="left", padx=4)
        ttk.Button(btns, text="Очистить журнал", command=self._clear).pack(side="left", padx=4)
        self.count_var = tk.StringVar(value="")
        ttk.Label(btns, textvariable=self.count_var, style="Muted.TLabel").pack(side="left", padx=(12, 0))
        ttk.Button(btns, text="Закрыть", command=self._on_close).pack(side="right")

        apply_result_tags(self.tree)

    def _on_columns_drag(self, _event=None) -> None:
        settings = getattr(self, "_layout_settings", None)
        store_tree_columns(settings, "journal", self.tree)

    def _on_journal_changed(self) -> None:
        try:
            if self._reload_after is not None:
                self.after_cancel(self._reload_after)
            self._reload_after = self.after(80, self._reload)
        except tk.TclError:
            pass

    def _reload(self) -> None:
        self._reload_after = None
        keep = self.tree.selection()[0] if self.tree.selection() else None
        self._entries = self._journal.load()
        self._by_id = {item.id: item for item in self._entries}
        names = sorted({item.server_name for item in self._entries if item.server_name}, key=str.casefold)
        current = self.vps_var.get()
        values = ["Все", *names]
        self.vps_combo.configure(values=values)
        if current not in values:
            self.vps_var.set("Все")
        self._apply_filter(keep_id=keep)

    def _visible(self) -> list[JournalEntry]:
        vps = (self.vps_var.get() or "Все").strip()
        needle = (self.search_var.get() or "").strip().casefold()
        out: list[JournalEntry] = []
        for item in self._entries:
            if vps != "Все" and item.server_name != vps:
                continue
            if needle:
                hay = " ".join(
                    [
                        item.started_display(),
                        item.server_name,
                        item.target(),
                        item.title,
                        item.command,
                        item.status,
                        KIND_LABELS.get(item.kind, item.kind),
                    ]
                ).casefold()
                if needle not in hay:
                    continue
            out.append(item)
        return out

    def _apply_filter(self, keep_id: str | None = None) -> None:
        keep_id = keep_id or (self.tree.selection()[0] if self.tree.selection() else None)
        visible = self._visible()
        self.tree.delete(*self.tree.get_children())
        for item in visible:
            tag = item.status if item.status in STATUS_LABELS else "error"
            self.tree.insert(
                "",
                "end",
                iid=item.id,
                text=item.started_display(),
                values=(
                    item.server_name,
                    item.title,
                    item.command_preview(),
                    item.status_display(),
                    item.duration_display(),
                ),
                tags=(tag,),
            )
        total = len(self._entries)
        shown = len(visible)
        if shown == total:
            self.count_var.set(f"Записей: {total}")
        else:
            self.count_var.set(f"Показано: {shown} из {total}")
        if keep_id and self.tree.exists(keep_id):
            self.tree.selection_set(keep_id)
            self.tree.see(keep_id)
        elif visible:
            first = visible[0].id
            self.tree.selection_set(first)
        self._show_details()

    def _selected(self) -> JournalEntry | None:
        sel = self.tree.selection()
        if not sel:
            return None
        return self._by_id.get(sel[0])

    def _show_details(self) -> None:
        entry = self._selected()
        text = entry.as_text() if entry else "Выберите запись."
        self.details.configure(state="normal")
        self.details.delete("1.0", "end")
        self.details.insert("1.0", text)
        self.details.configure(state="disabled")

    def _rerun(self) -> None:
        entry = self._selected()
        if entry is None:
            return
        if not (entry.command or "").strip():
            messagebox.showinfo("Журнал", "У этой записи нет команды.", parent=self)
            return
        if self._on_rerun is None:
            return
        self._on_rerun(entry)

    def _copy_selected(self) -> None:
        entry = self._selected()
        if entry is None:
            return
        text = entry.as_text()
        self.clipboard_clear()
        self.clipboard_append(text)
        try:
            self.update_idletasks()
        except tk.TclError:
            pass

    def _export(self) -> None:
        visible = self._visible()
        if not visible:
            messagebox.showinfo("Журнал", "Нечего сохранять.", parent=self)
            return
        path = filedialog.asksaveasfilename(
            parent=self,
            title="Сохранить журнал",
            defaultextension=".txt",
            filetypes=[("Текст", "*.txt"), ("Все файлы", "*.*")],
            initialfile="fatty-journal.txt",
        )
        if not path:
            return
        try:
            Path(path).write_text(self._journal.export_text(visible), encoding="utf-8")
        except OSError as exc:
            messagebox.showerror("Журнал", f"Не удалось сохранить файл:\n{exc}", parent=self)

    def _clear(self) -> None:
        if not self._entries:
            return
        if not messagebox.askyesno(
            "Очистить журнал",
            "Удалить все записи журнала? Это нельзя отменить.",
            parent=self,
        ):
            return
        self._journal.clear()

    def _on_close(self) -> None:
        self._journal.remove_listener(self._on_journal_changed)
        self._on_columns_drag()
        self.destroy()

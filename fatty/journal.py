from __future__ import annotations

import json
import os
import tempfile
import threading
import uuid
from collections.abc import Callable
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

from fatty.store import APP_DIR

JOURNAL_PATH = APP_DIR / "journal.jsonl"
MAX_ENTRIES = 5000
_TRIM_SLACK = 200
_COMMAND_MAX = 16_384
_ERROR_MAX = 4_096

Listener = Callable[[], None]


@dataclass
class JournalEntry:
    id: str
    started_at: str
    finished_at: str
    duration_sec: float
    server_id: str
    server_name: str
    host: str
    port: int
    username: str
    command_id: str = ""
    title: str = ""
    command: str = ""
    cwd: str = ""
    login_shell: bool = True
    timeout_sec: int = 180
    exit_code: int | None = None
    status: str = "error"
    kind: str = "command"
    error: str = ""

    def target(self) -> str:
        return f"{self.username}@{self.host}:{self.port}"

    def started_display(self) -> str:
        return _format_when(self.started_at)

    def duration_display(self) -> str:
        return format_duration(self.duration_sec)

    def status_display(self) -> str:
        if self.exit_code is not None and self.status in {"ok", "failed"}:
            return str(self.exit_code)
        return STATUS_LABELS.get(self.status, self.status)

    def command_preview(self, limit: int = 80) -> str:
        text = " ".join((self.command or "").split())
        if len(text) > limit:
            return text[: limit - 1] + "…"
        return text

    def as_text(self) -> str:
        lines = [
            f"Время: {self.started_display()}",
            f"Длительность: {self.duration_display()}",
            f"VPS: {self.server_name}  ({self.target()})",
            f"Тип: {KIND_LABELS.get(self.kind, self.kind)}",
            f"Название: {self.title or '—'}",
        ]
        if self.cwd:
            lines.append(f"Каталог: {self.cwd}")
        result = self.status_display()
        if self.exit_code is not None and self.status not in {"ok", "failed"}:
            result = f"{STATUS_LABELS.get(self.status, self.status)} (код {self.exit_code})"
        lines.append(f"Результат: {result}")
        if self.error:
            lines.append(f"Ошибка: {self.error}")
        lines.append("Команда:")
        lines.append(self.command or "—")
        return "\n".join(lines)


STATUS_LABELS = {
    "ok": "OK",
    "failed": "ошибка",
    "timeout": "таймаут",
    "cancelled": "прервано",
    "error": "сбой",
}

KIND_LABELS = {
    "command": "сохранённая",
    "quick": "разовая",
    "test": "проверка связи",
}


def now_iso() -> str:
    return datetime.now().astimezone().isoformat(timespec="seconds")


def format_duration(seconds: float) -> str:
    if seconds < 0:
        seconds = 0.0
    if seconds < 60:
        return f"{seconds:.1f} с"
    total = int(round(seconds))
    minutes, sec = divmod(total, 60)
    if minutes < 60:
        return f"{minutes} мин {sec} с"
    hours, minutes = divmod(minutes, 60)
    return f"{hours} ч {minutes} мин"


def status_from_exit(code: int) -> str:
    if code == 0:
        return "ok"
    if code == 124:
        return "timeout"
    if code == 130:
        return "cancelled"
    return "failed"


def _format_when(value: str) -> str:
    raw = (value or "").strip()
    if not raw:
        return "—"
    try:
        dt = datetime.fromisoformat(raw)
        return dt.strftime("%Y-%m-%d %H:%M:%S")
    except ValueError:
        return raw.replace("T", " ")[:19]


def _clip(text: str, limit: int) -> str:
    text = text or ""
    if len(text) <= limit:
        return text
    return text[: limit - 1] + "…"


def _parse_entry(raw: dict) -> JournalEntry | None:
    try:
        port = int(raw.get("port", 22) or 22)
    except (TypeError, ValueError):
        port = 22
    try:
        duration = float(raw.get("duration_sec", 0) or 0)
    except (TypeError, ValueError):
        duration = 0.0
    try:
        timeout = int(raw.get("timeout_sec", 180) or 180)
    except (TypeError, ValueError):
        timeout = 180
    exit_raw = raw.get("exit_code", None)
    exit_code: int | None
    if exit_raw is None or exit_raw == "":
        exit_code = None
    else:
        try:
            exit_code = int(exit_raw)
        except (TypeError, ValueError):
            exit_code = None
    entry_id = str(raw.get("id") or "").strip() or str(uuid.uuid4())
    return JournalEntry(
        id=entry_id,
        started_at=str(raw.get("started_at") or ""),
        finished_at=str(raw.get("finished_at") or ""),
        duration_sec=max(0.0, duration),
        server_id=str(raw.get("server_id") or ""),
        server_name=str(raw.get("server_name") or ""),
        host=str(raw.get("host") or ""),
        port=port,
        username=str(raw.get("username") or ""),
        command_id=str(raw.get("command_id") or ""),
        title=str(raw.get("title") or ""),
        command=str(raw.get("command") or ""),
        cwd=str(raw.get("cwd") or ""),
        login_shell=bool(raw.get("login_shell", True)),
        timeout_sec=timeout,
        exit_code=exit_code,
        status=str(raw.get("status") or "error"),
        kind=str(raw.get("kind") or "command"),
        error=str(raw.get("error") or ""),
    )


def _write_lines(path: Path, lines: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix="journal-", suffix=".jsonl", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            if lines:
                handle.write("\n".join(lines))
                handle.write("\n")
        os.replace(tmp_name, path)
    except Exception:
        if os.path.exists(tmp_name):
            os.remove(tmp_name)
        raise


class Journal:
    def __init__(self, path: Path | None = None, *, max_entries: int = MAX_ENTRIES) -> None:
        self.path = path or JOURNAL_PATH
        self.max_entries = max(100, min(50_000, int(max_entries or MAX_ENTRIES)))
        self._lock = threading.Lock()
        self._listeners: list[Listener] = []

    def add_listener(self, callback: Listener) -> None:
        if callback not in self._listeners:
            self._listeners.append(callback)

    def remove_listener(self, callback: Listener) -> None:
        try:
            self._listeners.remove(callback)
        except ValueError:
            pass

    def _notify(self) -> None:
        for callback in list(self._listeners):
            try:
                callback()
            except Exception:
                pass

    def append(self, entry: JournalEntry) -> None:
        entry.command = _clip(entry.command, _COMMAND_MAX)
        entry.error = _clip(entry.error, _ERROR_MAX)
        line = json.dumps(asdict(entry), ensure_ascii=False)
        with self._lock:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            with self.path.open("a", encoding="utf-8") as handle:
                handle.write(line)
                handle.write("\n")
            self._trim_unlocked()
        self._notify()

    def load(self, limit: int = MAX_ENTRIES) -> list[JournalEntry]:
        with self._lock:
            return self._read_unlocked(limit)

    def clear(self) -> None:
        with self._lock:
            _write_lines(self.path, [])
        self._notify()

    def export_text(self, entries: list[JournalEntry] | None = None) -> str:
        if entries is None:
            entries = self.load()
        blocks = [item.as_text() for item in entries]
        return ("\n\n" + ("─" * 48) + "\n\n").join(blocks)

    def _read_unlocked(self, limit: int) -> list[JournalEntry]:
        if not self.path.exists():
            return []
        entries: list[JournalEntry] = []
        try:
            text = self.path.read_text(encoding="utf-8")
        except OSError:
            return []
        for line in text.splitlines():
            raw_line = line.strip()
            if not raw_line:
                continue
            try:
                payload = json.loads(raw_line)
            except json.JSONDecodeError:
                continue
            if not isinstance(payload, dict):
                continue
            parsed = _parse_entry(payload)
            if parsed is not None:
                entries.append(parsed)
        if limit > 0 and len(entries) > limit:
            entries = entries[-limit:]
        entries.reverse()
        return entries

    def _trim_unlocked(self) -> None:
        if not self.path.exists():
            return
        try:
            raw_lines = [line for line in self.path.read_text(encoding="utf-8").splitlines() if line.strip()]
        except OSError:
            return
        if len(raw_lines) <= self.max_entries + _TRIM_SLACK:
            return
        _write_lines(self.path, raw_lines[-self.max_entries :])

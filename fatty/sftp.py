from __future__ import annotations

import re
import stat
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

from fatty.ssh_runner import SSHError, connect_client
from fatty.store import Command, Server

ProgressCb = Callable[[int, int], None]

_CD_RE = re.compile(r"(?:^|[;&|\n])\s*cd\s+(/[^\s;&|]+)")


class SFTPError(SSHError):
    pass


class TransferCancelled(SFTPError):
    def __init__(self) -> None:
        super().__init__("Передача прервана")


@dataclass(frozen=True)
class RemoteEntry:
    name: str
    path: str
    is_dir: bool
    size: int
    mtime: int
    is_link: bool = False

    @property
    def kind_label(self) -> str:
        if self.is_link and self.is_dir:
            return "ссылка → папка"
        if self.is_link:
            return "ссылка"
        return "папка" if self.is_dir else "файл"


def posix_join(cwd: str, name: str) -> str:
    if not name or name == ".":
        return cwd or "."
    if name.startswith("/"):
        return name
    if not cwd or cwd == ".":
        return name
    if cwd == "/":
        return f"/{name}"
    return f"{cwd.rstrip('/')}/{name}"


def posix_parent(path: str) -> str:
    text = (path or "").rstrip("/") or "/"
    if text == "/":
        return "/"
    parent = text.rsplit("/", 1)[0]
    return parent or "/"


def guess_start_path(commands: list[Command]) -> str:
    for command in commands:
        match = _CD_RE.search(command.command or "")
        if match:
            return match.group(1)
    return "."


def format_size(n: int) -> str:
    if n < 0:
        return "—"
    if n < 1024:
        return f"{n} Б"
    for unit, div in (("КБ", 1024), ("МБ", 1024**2), ("ГБ", 1024**3)):
        value = n / div
        if n < div * 1024 or unit == "ГБ":
            if value < 10:
                return f"{value:.1f} {unit}"
            return f"{value:.0f} {unit}"
    return f"{n} Б"


def format_mtime(ts: int) -> str:
    if ts <= 0:
        return "—"
    try:
        return datetime.fromtimestamp(ts).strftime("%d.%m.%Y %H:%M")
    except (OSError, OverflowError, ValueError):
        return "—"


class SFTPSession:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._cancel = threading.Event()
        self._client = None
        self._sftp = None
        self.remote_cwd = ""

    def connect(self, server: Server, start_path: str = ".") -> None:
        self._cancel.clear()
        client = connect_client(server, on_client=self._set_client)
        try:
            sftp = client.open_sftp()
        except Exception as exc:
            self.close()
            raise SFTPError(
                "Не удалось открыть SFTP. На сервере должна быть включена подсистема sftp."
            ) from exc
        self._sftp = sftp
        self._chdir(".")
        wanted = (start_path or "").strip()
        if wanted and wanted not in (".", self.remote_cwd):
            try:
                self._chdir(wanted)
            except SFTPError:
                pass

    def close(self) -> None:
        self._cancel.set()
        sftp = self._sftp
        client = self._client
        self._sftp = None
        self._client = None
        self.remote_cwd = ""
        if sftp is not None:
            try:
                sftp.close()
            except Exception:
                pass
        if client is not None:
            try:
                client.close()
            except Exception:
                pass

    def cancel_transfer(self) -> None:
        self._cancel.set()

    def _set_client(self, client) -> None:
        self._client = client

    def _require(self):
        sftp = self._sftp
        if sftp is None:
            raise SFTPError("SFTP-сессия закрыта")
        return sftp

    def _chdir(self, path: str) -> None:
        sftp = self._require()
        try:
            sftp.chdir(path)
        except Exception as exc:
            raise SFTPError(f"Не удалось открыть каталог «{path}»: {exc}") from exc
        cwd = sftp.getcwd()
        self.remote_cwd = cwd or path

    def listdir(self) -> list[RemoteEntry]:
        with self._lock:
            sftp = self._require()
            cwd = self.remote_cwd or sftp.getcwd() or "."
            try:
                attrs = sftp.listdir_attr(".")
            except Exception as exc:
                raise SFTPError(f"Не удалось прочитать каталог: {exc}") from exc
            entries: list[RemoteEntry] = []
            for item in attrs:
                name = item.filename or ""
                if not name or name in (".", ".."):
                    continue
                mode = int(item.st_mode or 0)
                is_link = stat.S_ISLNK(mode)
                is_dir = stat.S_ISDIR(mode)
                if is_link:
                    try:
                        followed = sftp.stat(name)
                        is_dir = stat.S_ISDIR(int(followed.st_mode or 0))
                    except Exception:
                        pass
                entries.append(
                    RemoteEntry(
                        name=name,
                        path=posix_join(cwd, name),
                        is_dir=is_dir,
                        size=int(item.st_size or 0),
                        mtime=int(item.st_mtime or 0),
                        is_link=is_link,
                    )
                )
            entries.sort(key=lambda entry: (not entry.is_dir, entry.name.lower()))
            return entries

    def enter(self, path: str) -> None:
        with self._lock:
            self._chdir(path)

    def go_up(self) -> None:
        with self._lock:
            cwd = self.remote_cwd or "/"
            if cwd == "/":
                return
            self._chdir(posix_parent(cwd))

    def mkdir(self, name: str) -> None:
        name = name.strip().strip("/\\")
        if not name or "/" in name or "\\" in name:
            raise SFTPError("Укажите имя папки без слэшей.")
        with self._lock:
            sftp = self._require()
            try:
                sftp.mkdir(name)
            except Exception as exc:
                raise SFTPError(f"Не удалось создать папку: {exc}") from exc

    def remove(self, entry: RemoteEntry) -> None:
        with self._lock:
            sftp = self._require()
            try:
                if entry.is_dir and not entry.is_link:
                    sftp.rmdir(entry.name)
                else:
                    sftp.remove(entry.name)
            except Exception as exc:
                raise SFTPError(f"Не удалось удалить «{entry.name}»: {exc}") from exc

    def exists(self, name: str) -> bool:
        with self._lock:
            sftp = self._require()
            try:
                sftp.stat(name)
                return True
            except FileNotFoundError:
                return False
            except OSError:
                return False

    def upload(self, local: Path, remote_name: str, on_progress: ProgressCb | None = None) -> None:
        local = Path(local)
        if not local.is_file():
            raise SFTPError(f"Локальный файл не найден: {local}")
        self._transfer(local=str(local), remote=remote_name, download=False, size=local.stat().st_size, on_progress=on_progress)

    def download(self, remote_name: str, local: Path, size: int, on_progress: ProgressCb | None = None) -> None:
        local = Path(local)
        local.parent.mkdir(parents=True, exist_ok=True)
        self._transfer(local=str(local), remote=remote_name, download=True, size=max(0, size), on_progress=on_progress)

    def _transfer(
        self,
        *,
        local: str,
        remote: str,
        download: bool,
        size: int,
        on_progress: ProgressCb | None,
    ) -> None:
        self._cancel.clear()
        last = 0.0

        def callback(sent: int, total: int) -> None:
            nonlocal last
            if self._cancel.is_set():
                raise TransferCancelled()
            if on_progress is None:
                return
            now = time.monotonic()
            if sent >= total or now - last >= 0.05:
                last = now
                on_progress(sent, total or size)

        with self._lock:
            sftp = self._require()
            try:
                if download:
                    sftp.get(remote, local, callback=callback)
                else:
                    sftp.put(local, remote, callback=callback, confirm=True)
            except TransferCancelled:
                self._cleanup_partial(sftp, local=local, remote=remote, download=download)
                raise
            except Exception as exc:
                if self._cancel.is_set():
                    self._cleanup_partial(sftp, local=local, remote=remote, download=download)
                    raise TransferCancelled() from exc
                raise SFTPError(f"Не удалось передать файл: {exc}") from exc

    def _cleanup_partial(self, sftp, *, local: str, remote: str, download: bool) -> None:
        try:
            if download:
                Path(local).unlink(missing_ok=True)
            else:
                sftp.remove(remote)
        except Exception:
            pass

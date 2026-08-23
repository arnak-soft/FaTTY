from __future__ import annotations

import hashlib
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import paramiko

from fatty import APP_NAME
from fatty.ppk import PPKError, write_openssh_as_ppk
from fatty.store import APP_DIR, KNOWN_HOSTS_PATH, Server

OutputCb = Callable[[str], None]
ClientReadyCb = Callable[[paramiko.SSHClient], None]


class SSHError(Exception):
    pass


@dataclass
class RunResult:
    exit_code: int
    cwd: str = ""


def wrap_remote_command(command: str, cwd: str, login_shell: bool) -> tuple[str, str]:
    """Run user command in one shell: restore cwd, then print pwd for the next run."""
    mark = f"FATTYCWD_{uuid.uuid4().hex}:"
    lines = ["set +e"]
    cwd = (cwd or "").strip()
    if cwd:
        quoted = shlex.quote(cwd)
        warn = shlex.quote(f"■ Нет каталога {cwd} — стартую из домашней")
        lines.append(f"cd {quoted} || printf '%s\\n' {warn}")
    lines.append(command.strip())
    lines.append("_fatty_st=$?")
    lines.append(f"printf '\\n{mark}%s\\n' \"$(pwd 2>/dev/null || true)\"")
    lines.append("exit $_fatty_st")
    script = "\n".join(lines)
    flag = "-lc" if login_shell else "-c"
    return f"bash {flag} {shlex.quote(script)}", mark


class _CwdOutputFilter:
    def __init__(self, mark: str, on_output: OutputCb) -> None:
        self.mark = mark
        self.on_output = on_output
        self.cwd = ""
        self._hold = ""

    def feed(self, text: str) -> None:
        data = self._hold + text
        lines = data.split("\n")
        self._hold = lines.pop()
        for line in lines:
            body = line.rstrip("\r")
            if body.startswith(self.mark):
                self.cwd = body[len(self.mark) :].strip()
            else:
                self.on_output(line + "\n")

    def finish(self) -> None:
        if not self._hold:
            return
        body = self._hold.rstrip("\r")
        if body.startswith(self.mark):
            self.cwd = body[len(self.mark) :].strip()
        else:
            self.on_output(self._hold)
        self._hold = ""


def connect_client(
    server: Server,
    *,
    on_client: ClientReadyCb | None = None,
) -> paramiko.SSHClient:
    """Open SSH with the same auth and known_hosts as command runs. Caller closes it."""
    client = paramiko.SSHClient()
    KNOWN_HOSTS_PATH.parent.mkdir(parents=True, exist_ok=True)
    if KNOWN_HOSTS_PATH.exists():
        client.load_host_keys(str(KNOWN_HOSTS_PATH))
    try:
        client.load_system_host_keys()
    except Exception:
        pass
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    connect_kwargs: dict = {
        "hostname": server.host,
        "port": server.port,
        "username": server.username,
        "timeout": 20,
        "banner_timeout": 20,
        "auth_timeout": 20,
        "allow_agent": True,
        "look_for_keys": not bool(server.key_path),
    }
    if server.password:
        connect_kwargs["password"] = server.password
    if server.key_path:
        key_path = Path(server.key_path).expanduser()
        if not key_path.exists():
            try:
                client.close()
            except Exception:
                pass
            raise SSHError(f"SSH-ключ не найден: {key_path}")
        connect_kwargs["key_filename"] = str(key_path)

    if on_client is not None:
        on_client(client)

    try:
        client.connect(**connect_kwargs)
    except Exception as exc:
        try:
            client.close()
        except Exception:
            pass
        raise SSHError(f"Не удалось подключиться: {exc}") from exc

    try:
        client.save_host_keys(str(KNOWN_HOSTS_PATH))
    except Exception:
        pass

    transport = client.get_transport()
    if transport is None:
        try:
            client.close()
        except Exception:
            pass
        raise SSHError("SSH-транспорт не установлен")
    transport.set_keepalive(15)
    return client


class SSHSession:
    def __init__(self) -> None:
        self._cancel = threading.Event()
        self._channel: paramiko.Channel | None = None
        self._client: paramiko.SSHClient | None = None

    def cancel(self) -> None:
        self._cancel.set()
        channel = self._channel
        if channel is not None:
            try:
                channel.close()
            except Exception:
                pass
        client = self._client
        if client is not None:
            try:
                client.close()
            except Exception:
                pass

    def _set_client(self, client: paramiko.SSHClient) -> None:
        self._client = client

    def run(
        self,
        server: Server,
        command: str,
        timeout_sec: int,
        login_shell: bool,
        on_output: OutputCb,
        cwd: str = "",
    ) -> RunResult:
        self._cancel.clear()
        cwd = (cwd or "").strip()
        where = f"  {cwd}" if cwd else ""
        on_output(f"→ {server.username}@{server.host}:{server.port}{where}\n")
        try:
            client = connect_client(server, on_client=self._set_client)
        except SSHError:
            self._client = None
            raise
        remote, mark = wrap_remote_command(command, cwd, login_shell)
        filt = _CwdOutputFilter(mark, on_output)

        on_output(f"$ {command.strip()}\n\n")
        transport = client.get_transport()
        if transport is None:
            raise SSHError("SSH-транспорт не установлен")

        channel = transport.open_session()
        self._channel = channel
        channel.settimeout(1.0)
        try:
            channel.exec_command(remote)
        except Exception as exc:
            raise SSHError(f"Не удалось запустить команду: {exc}") from exc

        started = time.monotonic()
        stdout_buf = bytearray()
        stderr_buf = bytearray()

        def flush(buf: bytearray, *, stdout: bool) -> None:
            if not buf:
                return
            text = buf.decode("utf-8", errors="replace")
            buf.clear()
            if stdout:
                filt.feed(text)
            else:
                on_output(text)

        def finish_output() -> None:
            if stdout_buf:
                flush(stdout_buf, stdout=True)
            if stderr_buf:
                flush(stderr_buf, stdout=False)
            filt.finish()

        try:
            while True:
                if self._cancel.is_set():
                    finish_output()
                    on_output("\n■ Выполнение прервано\n")
                    return RunResult(130, filt.cwd or cwd)
                if timeout_sec > 0 and (time.monotonic() - started) > timeout_sec:
                    finish_output()
                    on_output(f"\n■ Таймаут {timeout_sec} с\n")
                    return RunResult(124, filt.cwd or cwd)

                got = False
                if channel.recv_ready():
                    chunk = channel.recv(4096)
                    if chunk:
                        stdout_buf.extend(chunk)
                        if b"\n" in chunk or len(stdout_buf) > 2048:
                            flush(stdout_buf, stdout=True)
                        got = True
                if channel.recv_stderr_ready():
                    chunk = channel.recv_stderr(4096)
                    if chunk:
                        stderr_buf.extend(chunk)
                        if b"\n" in chunk or len(stderr_buf) > 2048:
                            flush(stderr_buf, stdout=False)
                        got = True
                if channel.exit_status_ready() and not channel.recv_ready() and not channel.recv_stderr_ready():
                    break
                if not got:
                    time.sleep(0.04)

            finish_output()
            return RunResult(int(channel.recv_exit_status()), filt.cwd or cwd)
        finally:
            try:
                channel.close()
            except Exception:
                pass
            try:
                client.close()
            except Exception:
                pass
            self._channel = None
            self._client = None


def _win_cmd_quote(arg: str) -> str:
    if not arg:
        return '""'
    if not any(ch in arg for ch in ' \t"&<>|^()%'):
        return arg
    return '"' + arg.replace('"', '""') + '"'


def find_ssh_executable() -> Path | None:
    found = shutil.which("ssh")
    if found:
        return Path(found)
    windir = Path(os.environ.get("WINDIR", r"C:\Windows"))
    for relative in (
        Path("System32") / "OpenSSH" / "ssh.exe",
        Path("Sysnative") / "OpenSSH" / "ssh.exe",
    ):
        candidate = windir / relative
        if candidate.is_file():
            return candidate
    return None


def _putty_install_dirs() -> tuple[Path, ...]:
    program_files = os.environ.get("ProgramFiles", r"C:\Program Files")
    program_files_x86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    local_app = os.environ.get("LocalAppData", "")
    dirs = (
        Path(program_files) / "PuTTY",
        Path(program_files_x86) / "PuTTY",
    )
    if local_app:
        dirs = (*dirs, Path(local_app) / "Programs" / "PuTTY")
    return dirs


def find_putty_executable() -> Path | None:
    found = shutil.which("putty")
    if found:
        return Path(found)
    for directory in _putty_install_dirs():
        candidate = directory / "putty.exe"
        if candidate.is_file():
            return candidate
    return None


def _putty_key_cache_path(source: Path) -> Path:
    stat = source.stat()
    digest = hashlib.sha256(
        f"{source.resolve()}:{stat.st_mtime_ns}:{stat.st_size}".encode()
    ).hexdigest()[:16]
    cache_dir = APP_DIR / "putty-keys"
    cache_dir.mkdir(parents=True, exist_ok=True)
    return cache_dir / f"{digest}.ppk"


def _convert_openssh_key_to_ppk(source: Path) -> Path:
    dest = _putty_key_cache_path(source)
    if dest.is_file() and dest.stat().st_mtime >= source.stat().st_mtime:
        try:
            head = dest.read_text(encoding="utf-8", errors="replace")[:40]
        except OSError:
            head = ""
        if head.startswith("PuTTY-User-Key-File-"):
            return dest
    try:
        return write_openssh_as_ppk(source, dest)
    except PPKError as exc:
        raise SSHError(str(exc)) from exc


def _resolve_putty_key_file(key_path: str) -> Path:
    key = Path(key_path).expanduser()
    if not key.is_file():
        raise SSHError(f"SSH-ключ не найден: {key}")
    if key.suffix.lower() == ".ppk":
        return key
    return _convert_openssh_key_to_ppk(key)


def _schedule_unlink(path: Path, delay: float = 3.0) -> None:
    def run() -> None:
        time.sleep(delay)
        try:
            path.unlink(missing_ok=True)
        except OSError:
            pass

    threading.Thread(target=run, daemon=True).start()


def _putty_password_file(password: str) -> Path:
    APP_DIR.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix="putty-pw-", suffix=".tmp", dir=str(APP_DIR))
    path = Path(name)
    try:
        os.write(fd, (password + "\n").encode("utf-8"))
    finally:
        os.close(fd)
    return path


def open_putty_console(server: Server) -> None:
    if sys.platform != "win32":
        raise SSHError("PuTTY доступен только на Windows.")
    putty = find_putty_executable()
    if putty is None:
        raise SSHError(
            "PuTTY не найден.\n"
            "Установите его с https://www.chiark.greenend.org.uk/~sgtatham/putty/ "
            "или добавьте putty.exe в PATH."
        )

    key_path = (server.key_path or "").strip()
    password = (server.password or "").strip()
    pwfile: Path | None = None

    args = [
        str(putty),
        "-ssh",
        server.host,
        "-P",
        str(server.port or 22),
        "-l",
        server.username,
        "-noagent",
    ]

    if key_path:
        try:
            ppk = _resolve_putty_key_file(key_path)
        except SSHError:
            if not password:
                raise
            ppk = None
        if ppk is not None:
            args += ["-i", str(ppk)]
        else:
            pwfile = _putty_password_file(password)
            args += ["-pwfile", str(pwfile)]
    elif password:
        pwfile = _putty_password_file(password)
        args += ["-pwfile", str(pwfile)]
    else:
        raise SSHError("Для PuTTY укажите пароль или SSH-ключ в карточке VPS.")

    try:
        subprocess.Popen(args, close_fds=True, cwd=str(Path.home()))
    except OSError as exc:
        if pwfile is not None:
            try:
                pwfile.unlink(missing_ok=True)
            except OSError:
                pass
        raise SSHError(f"Не удалось запустить PuTTY: {exc}") from exc

    if pwfile is not None:
        _schedule_unlink(pwfile)


def open_system_console(server: Server) -> None:
    if sys.platform != "win32":
        raise SSHError("Интерактивная консоль доступна только на Windows.")
    ssh = find_ssh_executable()
    if ssh is None:
        raise SSHError(
            "Не найден ssh.exe.\n"
            "Установите «OpenSSH Client»: Параметры → Приложения → "
            "Дополнительные компоненты."
        )
    args = [
        str(ssh),
        "-t",
        "-o",
        "ServerAliveInterval=30",
        "-o",
        "ServerAliveCountMax=4",
    ]
    if server.key_path:
        key_path = Path(server.key_path).expanduser()
        if not key_path.exists():
            raise SSHError(f"SSH-ключ не найден: {key_path}")
        args += ["-i", str(key_path), "-o", "IdentitiesOnly=yes"]
    args += ["-p", str(server.port or 22), f"{server.username}@{server.host}"]

    title = f"{APP_NAME} — {server.name or server.host}"
    safe_title = "".join(ch if ch not in "&|<>^" else " " for ch in title)[:80]
    cmdline = "title " + _win_cmd_quote(safe_title) + " & " + " ".join(_win_cmd_quote(part) for part in args)
    try:
        subprocess.Popen(
            ["cmd.exe", "/k", cmdline],
            creationflags=getattr(subprocess, "CREATE_NEW_CONSOLE", 0),
            close_fds=True,
            cwd=str(Path.home()),
        )
    except OSError as exc:
        raise SSHError(f"Не удалось открыть консоль: {exc}") from exc

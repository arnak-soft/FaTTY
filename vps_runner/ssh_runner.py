from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import sys
import threading
import time
from collections.abc import Callable
from pathlib import Path

import paramiko

from vps_runner import APP_NAME
from vps_runner.store import KNOWN_HOSTS_PATH, Server

OutputCb = Callable[[str], None]


class SSHError(Exception):
    pass


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

    def run(
        self,
        server: Server,
        command: str,
        timeout_sec: int,
        login_shell: bool,
        on_output: OutputCb,
    ) -> int:
        self._cancel.clear()
        client = paramiko.SSHClient()
        self._client = client
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
                raise SSHError(f"SSH-ключ не найден: {key_path}")
            connect_kwargs["key_filename"] = str(key_path)

        on_output(f"→ {server.username}@{server.host}:{server.port}\n")
        try:
            client.connect(**connect_kwargs)
        except Exception as exc:
            raise SSHError(f"Не удалось подключиться: {exc}") from exc

        try:
            client.save_host_keys(str(KNOWN_HOSTS_PATH))
        except Exception:
            pass

        remote = command.strip()
        if login_shell:
            remote = f"bash -lc {shlex.quote(command.strip())}"

        on_output(f"$ {command.strip()}\n\n")
        transport = client.get_transport()
        if transport is None:
            raise SSHError("SSH-транспорт не установлен")
        transport.set_keepalive(15)

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

        def flush(buf: bytearray) -> None:
            if not buf:
                return
            text = buf.decode("utf-8", errors="replace")
            buf.clear()
            on_output(text)

        try:
            while True:
                if self._cancel.is_set():
                    on_output("\n■ Выполнение прервано\n")
                    return 130
                if timeout_sec > 0 and (time.monotonic() - started) > timeout_sec:
                    on_output(f"\n■ Таймаут {timeout_sec} с\n")
                    return 124

                got = False
                if channel.recv_ready():
                    chunk = channel.recv(4096)
                    if chunk:
                        stdout_buf.extend(chunk)
                        if b"\n" in chunk or len(stdout_buf) > 2048:
                            flush(stdout_buf)
                        got = True
                if channel.recv_stderr_ready():
                    chunk = channel.recv_stderr(4096)
                    if chunk:
                        stderr_buf.extend(chunk)
                        if b"\n" in chunk or len(stderr_buf) > 2048:
                            flush(stderr_buf)
                        got = True
                if channel.exit_status_ready() and not channel.recv_ready() and not channel.recv_stderr_ready():
                    break
                if not got:
                    time.sleep(0.04)

            if stdout_buf:
                flush(stdout_buf)
            if stderr_buf:
                flush(stderr_buf)
            return int(channel.recv_exit_status())
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

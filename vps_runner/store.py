from __future__ import annotations

import json
import os
import tempfile
import uuid
from dataclasses import asdict, dataclass, field
from pathlib import Path

from vps_runner import APP_NAME
from vps_runner.crypto import unprotect
from vps_runner.vault import SessionVault, VaultLocked, VaultMeta


def _resolve_app_dir() -> Path:
    root = Path(os.environ.get("APPDATA", Path.home()))
    current = root / APP_NAME
    legacy = root / "vps-runner"
    if current.exists() or not legacy.exists():
        return current
    try:
        legacy.rename(current)
        return current
    except OSError:
        return legacy


APP_DIR = _resolve_app_dir()
CONFIG_PATH = APP_DIR / "config.json"
KNOWN_HOSTS_PATH = APP_DIR / "known_hosts"


@dataclass
class Server:
    id: str
    name: str
    host: str
    port: int = 22
    username: str = "root"
    password: str = field(default="", repr=False)
    password_blob: str = field(default="", repr=False)
    key_path: str = ""

    @staticmethod
    def new() -> Server:
        return Server(id=str(uuid.uuid4()), name="", host="")


@dataclass
class Command:
    id: str
    name: str
    server_id: str
    command: str
    timeout_sec: int = 180
    login_shell: bool = True

    @staticmethod
    def new(server_id: str) -> Command:
        return Command(
            id=str(uuid.uuid4()),
            name="",
            server_id=server_id,
            command="",
        )


@dataclass
class AppSettings:
    confirm_before_run: bool = True
    window_geometry: str = ""
    window_state: str = "normal"
    sash_pos: int = 0
    last_server_id: str = ""
    last_command_id: str = ""


@dataclass
class Config:
    servers: list[Server] = field(default_factory=list)
    commands: list[Command] = field(default_factory=list)
    settings: AppSettings = field(default_factory=AppSettings)
    vault: VaultMeta | None = None
    needs_migration: bool = False

    def server_by_id(self, server_id: str) -> Server | None:
        return next((s for s in self.servers if s.id == server_id), None)

    def command_by_id(self, command_id: str) -> Command | None:
        return next((c for c in self.commands if c.id == command_id), None)

    def commands_for(self, server_id: str) -> list[Command]:
        return [c for c in self.commands if c.server_id == server_id]


def _parse_vault(raw: dict) -> VaultMeta | None:
    salt = str(raw.get("salt") or "")
    verifier = str(raw.get("verifier") or "")
    if not salt or not verifier:
        return None
    try:
        iterations = int(raw.get("iterations") or 600_000)
    except (TypeError, ValueError):
        iterations = 600_000
    return VaultMeta(
        salt=salt,
        verifier=verifier,
        iterations=max(100_000, iterations),
        kdf=str(raw.get("kdf") or "pbkdf2-sha256"),
    )


def load() -> Config:
    if not CONFIG_PATH.exists():
        return Config(needs_migration=True)
    data = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    vault_meta = _parse_vault(data.get("vault") or {})
    needs_migration = vault_meta is None
    servers: list[Server] = []
    for raw in data.get("servers", []):
        blob = str(raw.get("password_vault") or "")
        legacy = str(raw.get("password_encrypted") or "")
        password = ""
        if vault_meta is None and legacy:
            try:
                password = unprotect(legacy)
            except OSError:
                password = ""
            needs_migration = True
        if legacy:
            needs_migration = True
        servers.append(
            Server(
                id=raw["id"],
                name=raw.get("name", ""),
                host=raw.get("host", ""),
                port=int(raw.get("port", 22)),
                username=raw.get("username", "root"),
                password=password,
                password_blob=blob,
                key_path=raw.get("key_path", ""),
            )
        )
    commands = [
        Command(
            id=raw["id"],
            name=raw.get("name", ""),
            server_id=raw.get("server_id", ""),
            command=raw.get("command", ""),
            timeout_sec=int(raw.get("timeout_sec", 180)),
            login_shell=bool(raw.get("login_shell", True)),
        )
        for raw in data.get("commands", [])
    ]
    settings_raw = data.get("settings", {})
    window_state = str(settings_raw.get("window_state", "normal") or "normal")
    if window_state not in {"normal", "zoomed"}:
        window_state = "normal"
    try:
        sash_pos = int(settings_raw.get("sash_pos", 0) or 0)
    except (TypeError, ValueError):
        sash_pos = 0
    settings = AppSettings(
        confirm_before_run=bool(settings_raw.get("confirm_before_run", True)),
        window_geometry=str(settings_raw.get("window_geometry", "") or ""),
        window_state=window_state,
        sash_pos=max(0, sash_pos),
        last_server_id=str(settings_raw.get("last_server_id", "") or ""),
        last_command_id=str(settings_raw.get("last_command_id", "") or ""),
    )
    return Config(
        servers=servers,
        commands=commands,
        settings=settings,
        vault=vault_meta,
        needs_migration=needs_migration,
    )


def unlock_secrets(config: Config, vault: SessionVault) -> None:
    if not vault.unlocked:
        raise VaultLocked("Хранилище заблокировано")
    for server in config.servers:
        if server.password_blob:
            server.password = vault.decrypt_secret(server.password_blob)


def save(config: Config, vault: SessionVault) -> None:
    if not vault.unlocked or vault.meta is None:
        raise VaultLocked("Нельзя сохранить конфиг без мастер-пароля")
    APP_DIR.mkdir(parents=True, exist_ok=True)
    payload = {
        "vault": {
            "kdf": vault.meta.kdf,
            "iterations": vault.meta.iterations,
            "salt": vault.meta.salt,
            "verifier": vault.meta.verifier,
        },
        "servers": [
            {
                "id": s.id,
                "name": s.name,
                "host": s.host,
                "port": s.port,
                "username": s.username,
                "password_vault": vault.encrypt_secret(s.password) if s.password else "",
                "key_path": s.key_path,
            }
            for s in config.servers
        ],
        "commands": [
            {
                "id": c.id,
                "name": c.name,
                "server_id": c.server_id,
                "command": c.command,
                "timeout_sec": c.timeout_sec,
                "login_shell": c.login_shell,
            }
            for c in config.commands
        ],
        "settings": asdict(config.settings),
    }
    encoded = json.dumps(payload, ensure_ascii=False, indent=2)
    fd, tmp_name = tempfile.mkstemp(prefix="config-", suffix=".json", dir=APP_DIR)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write(encoded)
            handle.write("\n")
        os.replace(tmp_name, CONFIG_PATH)
    except Exception:
        if os.path.exists(tmp_name):
            os.remove(tmp_name)
        raise
    config.vault = vault.meta
    config.needs_migration = False
    for server in config.servers:
        server.password_blob = vault.encrypt_secret(server.password) if server.password else ""

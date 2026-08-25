"""Export and import of VPS, commands and user settings."""

from __future__ import annotations

import json
import uuid
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

from fatty import APP_NAME, __version__
from fatty.store import AppSettings, Command, Config, Server

EXPORT_FORMAT = 1


class ConfigIOError(Exception):
    pass


@dataclass
class ImportResult:
    servers_added: int = 0
    servers_skipped: int = 0
    servers_replaced: int = 0
    commands_added: int = 0
    commands_skipped: int = 0
    settings_applied: bool = False


def portable_settings(settings: AppSettings) -> dict:
    return {
        "confirm_before_run": settings.confirm_before_run,
        "check_updates_on_start": settings.check_updates_on_start,
        "putty_path": settings.putty_path,
        "ssh_path": settings.ssh_path,
        "default_command_timeout": settings.default_command_timeout,
        "journal_max_entries": settings.journal_max_entries,
        "clear_output_before_run": settings.clear_output_before_run,
        "allow_short_master_password": settings.allow_short_master_password,
        "master_password_max_attempts": settings.master_password_max_attempts,
        "master_password_lockout_minutes": settings.master_password_lockout_minutes,
    }


def apply_portable_settings(settings: AppSettings, raw: dict) -> None:
    if not isinstance(raw, dict):
        return
    settings.confirm_before_run = bool(raw.get("confirm_before_run", settings.confirm_before_run))
    settings.check_updates_on_start = bool(
        raw.get("check_updates_on_start", settings.check_updates_on_start)
    )
    settings.putty_path = str(raw.get("putty_path", settings.putty_path) or "")
    settings.ssh_path = str(raw.get("ssh_path", settings.ssh_path) or "")
    settings.clear_output_before_run = bool(
        raw.get("clear_output_before_run", settings.clear_output_before_run)
    )
    settings.allow_short_master_password = bool(
        raw.get("allow_short_master_password", settings.allow_short_master_password)
    )
    settings.master_password_max_attempts = max(
        0,
        min(
            100,
            _parse_int_setting(
                raw.get("master_password_max_attempts"),
                settings.master_password_max_attempts,
            ),
        ),
    )
    settings.master_password_lockout_minutes = max(
        1,
        min(
            24 * 60,
            _parse_int_setting(
                raw.get("master_password_lockout_minutes"),
                settings.master_password_lockout_minutes,
            ),
        ),
    )
    try:
        timeout = int(raw.get("default_command_timeout", settings.default_command_timeout) or 180)
        if 1 <= timeout <= 86_400:
            settings.default_command_timeout = timeout
    except (TypeError, ValueError):
        pass
    try:
        journal = int(raw.get("journal_max_entries", settings.journal_max_entries) or 5000)
        if 100 <= journal <= 50_000:
            settings.journal_max_entries = journal
    except (TypeError, ValueError):
        pass


def _parse_int_setting(raw: object, default: int) -> int:
    try:
        return int(raw)  # type: ignore[arg-type]
    except (TypeError, ValueError):
        return default


def _server_key(name: str, host: str) -> tuple[str, str]:
    return name.strip().casefold(), host.strip().casefold()


def build_export_payload(
    config: Config,
    *,
    include_secrets: bool,
    include_settings: bool,
) -> dict:
    servers: list[dict] = []
    for server in config.servers:
        item = {
            "name": server.name,
            "host": server.host,
            "port": server.port,
            "username": server.username,
            "key_path": server.key_path,
        }
        if include_secrets:
            item["password"] = server.password
        servers.append(item)

    commands: list[dict] = []
    for cmd in config.commands:
        server = config.server_by_id(cmd.server_id)
        if server is None:
            continue
        commands.append(
            {
                "server_name": server.name,
                "server_host": server.host,
                "name": cmd.name,
                "command": cmd.command,
                "timeout_sec": cmd.timeout_sec,
                "login_shell": cmd.login_shell,
            }
        )

    payload: dict = {
        "fatty_export": EXPORT_FORMAT,
        "app": APP_NAME,
        "version": __version__,
        "exported_at": datetime.now().astimezone().isoformat(timespec="seconds"),
        "include_secrets": include_secrets,
        "servers": servers,
        "commands": commands,
    }
    if include_settings:
        payload["settings"] = portable_settings(config.settings)
    return payload


def write_export(path: Path, config: Config, *, include_secrets: bool, include_settings: bool) -> None:
    payload = build_export_payload(
        config,
        include_secrets=include_secrets,
        include_settings=include_settings,
    )
    encoded = json.dumps(payload, ensure_ascii=False, indent=2)
    path.write_text(encoded + "\n", encoding="utf-8")


def read_export(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise ConfigIOError(f"Не удалось прочитать файл: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ConfigIOError("Файл не является корректным JSON.") from exc
    if not isinstance(data, dict):
        raise ConfigIOError("Неверный формат файла.")
    if data.get("fatty_export") != EXPORT_FORMAT:
        raise ConfigIOError("Неподдерживаемая версия файла экспорта FaTTY.")
    if data.get("app") not in {APP_NAME, None}:
        raise ConfigIOError("Файл создан другим приложением.")
    return data


def _parse_server(raw: object) -> Server | None:
    if not isinstance(raw, dict):
        return None
    name = str(raw.get("name") or "").strip()
    host = str(raw.get("host") or "").strip()
    if not name or not host:
        return None
    try:
        port = int(raw.get("port", 22) or 22)
        if not (1 <= port <= 65535):
            raise ValueError
    except (TypeError, ValueError):
        port = 22
    return Server(
        id=str(uuid.uuid4()),
        name=name,
        host=host,
        port=port,
        username=str(raw.get("username") or "root").strip() or "root",
        password=str(raw.get("password") or ""),
        key_path=str(raw.get("key_path") or ""),
    )


def _parse_command(raw: object) -> tuple[str, str, Command] | None:
    if not isinstance(raw, dict):
        return None
    server_name = str(raw.get("server_name") or "").strip()
    server_host = str(raw.get("server_host") or "").strip()
    name = str(raw.get("name") or "").strip()
    command = str(raw.get("command") or "").strip()
    if not server_name or not server_host or not name or not command:
        return None
    try:
        timeout = int(raw.get("timeout_sec", 180) or 180)
        if timeout < 1:
            raise ValueError
    except (TypeError, ValueError):
        timeout = 180
    return (
        server_name,
        server_host,
        Command(
            id=str(uuid.uuid4()),
            name=name,
            server_id="",
            command=command,
            timeout_sec=timeout,
            login_shell=bool(raw.get("login_shell", True)),
        ),
    )


def import_into_config(
    config: Config,
    data: dict,
    *,
    mode: str,
    import_settings: bool,
) -> ImportResult:
    if mode not in {"merge", "replace"}:
        raise ConfigIOError("Неизвестный режим импорта.")

    result = ImportResult()
    raw_servers = data.get("servers")
    raw_commands = data.get("commands")
    if not isinstance(raw_servers, list):
        raw_servers = []
    if not isinstance(raw_commands, list):
        raw_commands = []

    imported_servers: list[Server] = []
    for raw in raw_servers:
        parsed = _parse_server(raw)
        if parsed is not None:
            imported_servers.append(parsed)

    imported_commands: list[tuple[str, str, Command]] = []
    for raw in raw_commands:
        parsed = _parse_command(raw)
        if parsed is not None:
            imported_commands.append(parsed)

    if mode == "replace":
        result.servers_replaced = len(config.servers)
        config.servers = imported_servers
        config.commands = []
        id_by_key = {_server_key(s.name, s.host): s.id for s in imported_servers}
        for server_name, server_host, cmd in imported_commands:
            server_id = id_by_key.get(_server_key(server_name, server_host))
            if not server_id:
                result.commands_skipped += 1
                continue
            cmd.server_id = server_id
            config.commands.append(cmd)
            result.commands_added += 1
    else:
        id_by_key = {_server_key(s.name, s.host): s.id for s in config.servers}
        for server in imported_servers:
            key = _server_key(server.name, server.host)
            if key in id_by_key:
                result.servers_skipped += 1
                continue
            config.servers.append(server)
            id_by_key[key] = server.id
            result.servers_added += 1

        existing_cmds = {
            (cmd.server_id, cmd.name.strip().casefold())
            for cmd in config.commands
        }
        for server_name, server_host, cmd in imported_commands:
            server_id = id_by_key.get(_server_key(server_name, server_host))
            if not server_id:
                result.commands_skipped += 1
                continue
            cmd_key = (server_id, cmd.name.strip().casefold())
            if cmd_key in existing_cmds:
                result.commands_skipped += 1
                continue
            cmd.server_id = server_id
            config.commands.append(cmd)
            existing_cmds.add(cmd_key)
            result.commands_added += 1

    if import_settings:
        settings_raw = data.get("settings")
        if isinstance(settings_raw, dict):
            apply_portable_settings(config.settings, settings_raw)
            result.settings_applied = True

    return result


def format_import_summary(result: ImportResult, *, mode: str) -> str:
    lines: list[str] = []
    if mode == "replace":
        lines.append(f"VPS заменено: {result.servers_replaced}")
    else:
        lines.append(f"VPS добавлено: {result.servers_added}")
        if result.servers_skipped:
            lines.append(f"VPS пропущено (уже есть): {result.servers_skipped}")
    lines.append(f"Команд добавлено: {result.commands_added}")
    if result.commands_skipped:
        lines.append(f"Команд пропущено: {result.commands_skipped}")
    if result.settings_applied:
        lines.append("Настройки приложения импортированы.")
    return "\n".join(lines)

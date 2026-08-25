"""Persistent lockout after failed master-password attempts."""

from __future__ import annotations

import json
import os
import tempfile
import time
from dataclasses import asdict, dataclass

from fatty.store import APP_DIR, AppSettings

LOCKOUT_PATH = APP_DIR / "auth-lockout.json"


@dataclass
class AuthLockoutState:
    failed_attempts: int = 0
    locked_until: float = 0.0


def load_lockout() -> AuthLockoutState:
    if not LOCKOUT_PATH.exists():
        return AuthLockoutState()
    try:
        data = json.loads(LOCKOUT_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return AuthLockoutState()
    if not isinstance(data, dict):
        return AuthLockoutState()
    try:
        failed = int(data.get("failed_attempts", 0) or 0)
    except (TypeError, ValueError):
        failed = 0
    try:
        locked_until = float(data.get("locked_until", 0) or 0)
    except (TypeError, ValueError):
        locked_until = 0.0
    return AuthLockoutState(failed_attempts=max(0, failed), locked_until=max(0.0, locked_until))


def save_lockout(state: AuthLockoutState) -> None:
    APP_DIR.mkdir(parents=True, exist_ok=True)
    encoded = json.dumps(asdict(state), ensure_ascii=False, indent=2)
    fd, tmp_name = tempfile.mkstemp(prefix="lockout-", suffix=".json", dir=APP_DIR)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write(encoded)
            handle.write("\n")
        os.replace(tmp_name, LOCKOUT_PATH)
    except Exception:
        if os.path.exists(tmp_name):
            os.remove(tmp_name)
        raise


def lockout_enabled(settings: AppSettings) -> bool:
    return int(getattr(settings, "master_password_max_attempts", 5) or 0) > 0


def _normalize_state(state: AuthLockoutState) -> AuthLockoutState:
    now = time.time()
    if state.locked_until > 0 and now >= state.locked_until:
        return AuthLockoutState()
    return state


def remaining_lockout_seconds(settings: AppSettings, state: AuthLockoutState | None = None) -> int:
    if not lockout_enabled(settings):
        return 0
    state = _normalize_state(state or load_lockout())
    if state.locked_until <= 0:
        return 0
    return max(0, int(state.locked_until - time.time()))


def lockout_message(settings: AppSettings) -> str | None:
    seconds = remaining_lockout_seconds(settings)
    if seconds <= 0:
        return None
    minutes, secs = divmod(seconds, 60)
    if minutes:
        return f"Слишком много неудачных попыток. Повторите через {minutes} мин {secs} с."
    return f"Слишком много неудачных попыток. Повторите через {secs} с."


def record_failed_attempt(settings: AppSettings) -> str:
    if not lockout_enabled(settings):
        return "Неверный мастер-пароль."

    state = _normalize_state(load_lockout())
    if remaining_lockout_seconds(settings, state) > 0:
        return lockout_message(settings) or "Вход временно заблокирован."

    max_attempts = max(1, int(settings.master_password_max_attempts or 5))
    state.failed_attempts += 1
    if state.failed_attempts >= max_attempts:
        minutes = max(1, int(settings.master_password_lockout_minutes or 20))
        state.locked_until = time.time() + minutes * 60
        save_lockout(state)
        return lockout_message(settings) or "Вход временно заблокирован."

    save_lockout(state)
    left = max_attempts - state.failed_attempts
    return f"Неверный мастер-пароль. Осталось попыток: {left}."


def record_success() -> None:
    if LOCKOUT_PATH.exists():
        try:
            LOCKOUT_PATH.unlink()
        except OSError:
            save_lockout(AuthLockoutState())

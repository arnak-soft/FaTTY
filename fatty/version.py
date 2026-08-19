"""Версия приложения: git-тег (сборка и запуск из исходников) или файл внутри exe."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

_CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0) if sys.platform == "win32" else 0
_HASH_ONLY = re.compile(r"^[0-9a-f]+(-dirty)?$", re.IGNORECASE)

_cached: str | None = None


def _repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def _meipass() -> Path | None:
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent))
    return None


def _read_bundled() -> str | None:
    root = _meipass()
    if root is None:
        return None
    path = root / "fatty" / "_version.txt"
    if not path.is_file():
        return None
    text = path.read_text(encoding="utf-8").strip()
    return text or None


def _normalize(raw: str) -> str:
    text = raw.strip()
    if text.startswith("v") and len(text) > 1 and text[1].isdigit():
        text = text[1:]
    if _HASH_ONLY.fullmatch(text):
        return f"0.0.0-g{text}"
    return text


def _git_version() -> str | None:
    try:
        raw = subprocess.check_output(
            ["git", "-C", str(_repo_root()), "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=5,
            creationflags=_CREATE_NO_WINDOW,
        )
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return None
    text = raw.strip()
    if not text:
        return None
    return _normalize(text)


def resolve_version() -> str:
    global _cached
    if _cached is None:
        _cached = _read_bundled() or _git_version() or "0.0.0-dev"
    return _cached


def version_tuple(version: str | None = None) -> tuple[int, int, int, int]:
    """Четвёрка чисел для Windows VERSIONINFO (1.4.0 → 1.4.0.0, 1.4.0-3-gabc → 1.4.0.3)."""
    text = version if version is not None else resolve_version()
    if text.startswith("v") and len(text) > 1 and text[1].isdigit():
        text = text[1:]
    match = re.match(r"^(\d+)(?:\.(\d+))?(?:\.(\d+))?(?:-(\d+)-g)?", text)
    if not match:
        return (0, 0, 0, 0)
    parts = [int(g) if g else 0 for g in match.groups()]
    return (parts[0], parts[1], parts[2], parts[3])


def write_bundled_version(version: str, dest_dir: Path | None = None) -> Path:
    folder = dest_dir if dest_dir is not None else _repo_root() / "build"
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / "_version.txt"
    path.write_text(version.strip() + "\n", encoding="utf-8")
    return path

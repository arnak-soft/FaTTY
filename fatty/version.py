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


def _git(args: list[str]) -> str | None:
    try:
        raw = subprocess.check_output(
            ["git", "-C", str(_repo_root()), *args],
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=5,
            creationflags=_CREATE_NO_WINDOW,
        )
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return None
    return raw.strip()


def _git_ok(args: list[str]) -> bool:
    try:
        completed = subprocess.run(
            ["git", "-C", str(_repo_root()), *args],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=5,
            creationflags=_CREATE_NO_WINDOW,
        )
    except (OSError, subprocess.TimeoutExpired):
        return False
    return completed.returncode == 0


def _git_version() -> str | None:
    """Highest SemVer tag reachable from HEAD, plus -N-gHASH if HEAD is ahead."""
    if not _git_ok(["rev-parse", "--is-inside-work-tree"]):
        return None
    dirty = bool(_git(["status", "--porcelain"]))
    tags = _git(["tag", "--list", "v[0-9]*", "--sort=-version:refname"])
    chosen: str | None = None
    if tags:
        for tag in tags.splitlines():
            tag = tag.strip()
            if tag and _git_ok(["merge-base", "--is-ancestor", tag, "HEAD"]):
                chosen = tag
                break
    if chosen is None:
        short = _git(["rev-parse", "--short", "HEAD"])
        if not short:
            return None
        text = f"0.0.0-g{short}"
    else:
        count = _git(["rev-list", "--count", f"{chosen}..HEAD"]) or "0"
        if count == "0":
            text = _normalize(chosen)
        else:
            short = _git(["rev-parse", "--short", "HEAD"]) or "unknown"
            text = f"{_normalize(chosen)}-{count}-g{short}"
    if dirty:
        text += "-dirty"
    return text


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

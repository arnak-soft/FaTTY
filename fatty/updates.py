"""Проверка обновлений через GitHub Releases (или теги)."""

from __future__ import annotations

import json
import re
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Literal

from fatty import APP_NAME, __version__

GITHUB_OWNER = "arnak-soft"
GITHUB_REPO = "FaTTY"
REPO_URL = f"https://github.com/{GITHUB_OWNER}/{GITHUB_REPO}"
RELEASES_PAGE = f"{REPO_URL}/releases"
API_LATEST_RELEASE = (
    f"https://api.github.com/repos/{GITHUB_OWNER}/{GITHUB_REPO}/releases/latest"
)
API_TAGS = f"https://api.github.com/repos/{GITHUB_OWNER}/{GITHUB_REPO}/tags"

_USER_AGENT = f"{APP_NAME}/{__version__} (+{REPO_URL})"
_TIMEOUT_SEC = 12
_SEMVER = re.compile(r"^v?(\d+)(?:\.(\d+))?(?:\.(\d+))?", re.IGNORECASE)


class UpdateError(Exception):
    """Сеть или ответ GitHub недоступны."""


@dataclass(frozen=True)
class UpdateCheckResult:
    status: Literal["update", "current", "none"]
    current: str
    latest: str | None = None
    page_url: str | None = None
    download_url: str | None = None


def normalize_version(raw: str) -> str:
    text = (raw or "").strip()
    if text.startswith("v") and len(text) > 1 and text[1].isdigit():
        text = text[1:]
    return text


def version_key(raw: str) -> tuple[int, int, int]:
    text = normalize_version(raw)
    match = _SEMVER.match(text)
    if not match:
        return (0, 0, 0)
    major, minor, patch = match.groups()
    return (int(major), int(minor or 0), int(patch or 0))


def is_newer(remote: str, local: str) -> bool:
    return version_key(remote) > version_key(local)


def _http_json(url: str) -> object:
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "User-Agent": _USER_AGENT,
            "X-GitHub-Api-Version": "2022-11-28",
        },
        method="GET",
    )
    try:
        with urllib.request.urlopen(request, timeout=_TIMEOUT_SEC) as response:
            body = response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            return None
        raise UpdateError(f"GitHub ответил HTTP {exc.code}") from exc
    except urllib.error.URLError as exc:
        raise UpdateError(f"Нет связи с GitHub: {exc.reason}") from exc
    except TimeoutError as exc:
        raise UpdateError("Таймаут при обращении к GitHub") from exc
    try:
        return json.loads(body)
    except json.JSONDecodeError as exc:
        raise UpdateError("Некорректный ответ GitHub") from exc


def _pick_asset_url(assets: object) -> str | None:
    if not isinstance(assets, list):
        return None
    names_urls: list[tuple[str, str]] = []
    for item in assets:
        if not isinstance(item, dict):
            continue
        name = str(item.get("name") or "")
        url = str(item.get("browser_download_url") or "")
        if name and url:
            names_urls.append((name.lower(), url))
    for needle in ("setup.exe", "setup", "onefile.exe", "onefile", ".exe"):
        for name, url in names_urls:
            if needle in name:
                return url
    return names_urls[0][1] if names_urls else None


def _from_release(data: dict, current: str) -> UpdateCheckResult:
    tag = normalize_version(str(data.get("tag_name") or data.get("name") or ""))
    if not tag:
        return UpdateCheckResult(status="none", current=current)
    page = str(data.get("html_url") or "") or f"{RELEASES_PAGE}/tag/v{tag}"
    download = _pick_asset_url(data.get("assets"))
    if is_newer(tag, current):
        return UpdateCheckResult(
            status="update",
            current=current,
            latest=tag,
            page_url=page,
            download_url=download or page,
        )
    return UpdateCheckResult(
        status="current",
        current=current,
        latest=tag,
        page_url=page,
    )


def _from_tags(data: object, current: str) -> UpdateCheckResult:
    if not isinstance(data, list) or not data:
        return UpdateCheckResult(status="none", current=current, page_url=RELEASES_PAGE)
    best: str | None = None
    for item in data:
        if not isinstance(item, dict):
            continue
        name = normalize_version(str(item.get("name") or ""))
        if not name or not name[0].isdigit():
            continue
        if best is None or version_key(name) > version_key(best):
            best = name
    if best is None:
        return UpdateCheckResult(status="none", current=current, page_url=RELEASES_PAGE)
    page = f"{REPO_URL}/releases/tag/v{best}"
    if is_newer(best, current):
        return UpdateCheckResult(
            status="update",
            current=current,
            latest=best,
            page_url=page,
            download_url=page,
        )
    return UpdateCheckResult(
        status="current",
        current=current,
        latest=best,
        page_url=page,
    )


def check_for_updates(current: str | None = None) -> UpdateCheckResult:
    """Спросить GitHub о свежей версии. Без опубликованных релизов/тегов — status=none."""
    local = normalize_version(current if current is not None else __version__)
    release = _http_json(API_LATEST_RELEASE)
    if isinstance(release, dict):
        return _from_release(release, local)
    tags = _http_json(API_TAGS)
    return _from_tags(tags, local)

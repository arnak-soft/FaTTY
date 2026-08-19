from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Preset:
    name: str
    command: str
    timeout_sec: int = 180
    login_shell: bool = True


DEFAULT_APP_DIR = "/var/www/bitrix24-billing-app"
DEFAULT_BRANCH = "master"
DEFAULT_PM2 = "billing"


def billing_presets(
    app_dir: str = DEFAULT_APP_DIR,
    branch: str = DEFAULT_BRANCH,
    pm2_name: str = DEFAULT_PM2,
) -> list[Preset]:
    app = (app_dir or DEFAULT_APP_DIR).rstrip("/")
    branch = branch or DEFAULT_BRANCH
    pm2 = pm2_name or DEFAULT_PM2
    return [
        Preset(
            "Deploy billing",
            f"cd {app} && git pull origin {branch} && pm2 restart {pm2}",
            timeout_sec=300,
        ),
        Preset(
            "Git pull",
            f"cd {app} && git pull origin {branch}",
            timeout_sec=180,
        ),
        Preset("PM2 restart", f"pm2 restart {pm2}", timeout_sec=60),
        Preset("PM2 status", "pm2 status", timeout_sec=30),
        Preset(
            "PM2 logs",
            f"pm2 logs {pm2} --lines 120 --nostream",
            timeout_sec=30,
        ),
        Preset(
            "Git status",
            f"cd {app} && git status -sb && echo && git log -8 --oneline",
            timeout_sec=30,
        ),
    ]


def server_presets() -> list[Preset]:
    return [
        Preset(
            "Состояние сервера",
            "hostname; date; uptime; echo; df -hT; echo; free -h",
            timeout_sec=30,
        ),
        Preset(
            "Nginx reload",
            "nginx -t && (systemctl reload nginx || service nginx reload)",
            timeout_sec=30,
        ),
        Preset(
            "Nginx status",
            "systemctl status nginx --no-pager -l || service nginx status",
            timeout_sec=30,
        ),
    ]


def all_presets(
    app_dir: str = DEFAULT_APP_DIR,
    branch: str = DEFAULT_BRANCH,
    pm2_name: str = DEFAULT_PM2,
    include_server: bool = True,
) -> list[Preset]:
    items = billing_presets(app_dir, branch, pm2_name)
    if include_server:
        items = items + server_presets()
    return items

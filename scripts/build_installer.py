"""Собрать Setup.exe через Inno Setup (поверх папки Portable)."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from fatty import __version__, portable_dir_name, version_info  # noqa: E402


def _find_iscc() -> Path | None:
    env = os.environ.get("ISCC")
    if env:
        path = Path(env)
        if path.is_file():
            return path
    which = _which("iscc") or _which("ISCC.exe")
    if which:
        return which
    for base in (
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
        os.environ.get("ProgramFiles", r"C:\Program Files"),
    ):
        candidate = Path(base) / "Inno Setup 6" / "ISCC.exe"
        if candidate.is_file():
            return candidate
    return None


def _which(name: str) -> Path | None:
    for folder in os.environ.get("PATH", "").split(os.pathsep):
        if not folder:
            continue
        path = Path(folder) / name
        if path.is_file():
            return path
    return None


def main() -> int:
    portable = ROOT / "dist" / portable_dir_name()
    if not (portable / "FaTTY.exe").is_file():
        print(f"Portable build not found: {portable}", file=sys.stderr)
        print("Run build.bat (PyInstaller) first.", file=sys.stderr)
        return 1

    iscc = _find_iscc()
    if iscc is None:
        print("Inno Setup 6 not found (ISCC.exe).", file=sys.stderr)
        print("Install from https://jrsoftware.org/isinfo.php", file=sys.stderr)
        print("Or set ISCC to the full path of ISCC.exe.", file=sys.stderr)
        return 3

    ver_info = ".".join(str(n) for n in version_info())
    iss = ROOT / "fatty.iss"
    cmd = [
        str(iscc),
        f"/DMyAppVersion={__version__}",
        f"/DMyVersionInfo={ver_info}",
        f"/DPortableDirName={portable_dir_name()}",
        str(iss),
    ]
    print("Running:", " ".join(cmd))
    result = subprocess.run(cmd, cwd=ROOT)
    if result.returncode != 0:
        return result.returncode

    setup_name = f"FaTTY {__version__} Setup.exe"
    print(f"  dist\\{setup_name}  (installer)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

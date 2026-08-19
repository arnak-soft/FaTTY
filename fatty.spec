# -*- mode: python ; coding: utf-8 -*-
from pathlib import Path

from PyInstaller.utils.hooks import collect_all

ICON_FILE = str(Path("assets") / "app.ico")

datas: list = [(ICON_FILE, "assets"), (str(Path("assets") / "app.png"), "assets")]
binaries: list = []
hiddenimports: list = []
for pkg in ("paramiko", "cryptography", "bcrypt", "nacl", "invoke"):
    pkg_datas, pkg_binaries, pkg_hidden = collect_all(pkg)
    datas += pkg_datas
    binaries += pkg_binaries
    hiddenimports += pkg_hidden

hiddenimports += [
    "fatty",
    "fatty.ui",
    "fatty.store",
    "fatty.crypto",
    "fatty.vault",
    "fatty.ssh_runner",
    "fatty.presets",
    "fatty.single_instance",
]

a = Analysis(
    ["launch.py"],
    pathex=[],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="FaTTY",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
    icon=ICON_FILE,
)

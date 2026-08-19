# -*- mode: python ; coding: utf-8 -*-
from pathlib import Path

from PyInstaller.utils.hooks import collect_all
from PyInstaller.utils.win32.versioninfo import (
    FixedFileInfo,
    StringFileInfo,
    StringStruct,
    StringTable,
    VarFileInfo,
    VarStruct,
    VSVersionInfo,
)

from fatty import APP_NAME, __version__, exe_filename, exe_stem, version_info
from fatty.version import write_bundled_version

ICON_FILE = str(Path("assets") / "app.ico")
SPLASH_FILE = str(Path("assets") / "splash.png")
FILEVERS = version_info()
VERSION_FILE = write_bundled_version(__version__)

if not Path(SPLASH_FILE).is_file():
    raise SystemExit("Missing assets/splash.png. Run: python scripts/make_icon.py")

datas: list = [
    (ICON_FILE, "assets"),
    (str(Path("assets") / "app.png"), "assets"),
    (SPLASH_FILE, "assets"),
    (str(VERSION_FILE), "fatty"),
]
binaries: list = []
hiddenimports: list = []
for pkg in ("paramiko", "cryptography", "bcrypt", "nacl", "invoke"):
    pkg_datas, pkg_binaries, pkg_hidden = collect_all(pkg)
    datas += pkg_datas
    binaries += pkg_binaries
    hiddenimports += pkg_hidden

hiddenimports += [
    "fatty",
    "fatty.version",
    "fatty.ui",
    "fatty.store",
    "fatty.crypto",
    "fatty.vault",
    "fatty.ssh_runner",
    "fatty.sftp",
    "fatty.files_ui",
    "fatty.presets",
    "fatty.single_instance",
    "fatty.splash",
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

splash = Splash(
    SPLASH_FILE,
    binaries=a.binaries,
    datas=a.datas,
    text_pos=None,
    minify_script=True,
    always_on_top=True,
)

version = VSVersionInfo(
    ffi=FixedFileInfo(
        filevers=FILEVERS,
        prodvers=FILEVERS,
        mask=0x3F,
        flags=0x0,
        OS=0x40004,
        fileType=0x1,
        subtype=0x0,
        date=(0, 0),
    ),
    kids=[
        StringFileInfo(
            [
                StringTable(
                    "040904B0",
                    [
                        StringStruct("CompanyName", APP_NAME),
                        StringStruct("FileDescription", APP_NAME),
                        StringStruct("FileVersion", __version__),
                        StringStruct("InternalName", APP_NAME),
                        StringStruct("OriginalFilename", exe_filename()),
                        StringStruct("ProductName", APP_NAME),
                        StringStruct("ProductVersion", __version__),
                    ],
                )
            ]
        ),
        VarFileInfo([VarStruct("Translation", [1033, 1200])]),
    ],
)

exe = EXE(
    pyz,
    a.scripts,
    splash,
    splash.binaries,
    a.binaries,
    a.datas,
    [],
    name=exe_stem(),
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
    icon=ICON_FILE,
    version=version,
)

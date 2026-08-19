from fatty.version import resolve_version, version_tuple

APP_NAME = "FaTTY"
__version__ = resolve_version()


def version_info() -> tuple[int, int, int, int]:
    return version_tuple(__version__)


def exe_stem() -> str:
    return f"{APP_NAME} {__version__}"


def onefile_stem() -> str:
    return f"{exe_stem()} OneFile"


def onefile_filename() -> str:
    return f"{onefile_stem()}.exe"


def portable_dir_name() -> str:
    return f"{exe_stem()} Portable"


def portable_exe_stem() -> str:
    return APP_NAME


def exe_filename() -> str:
    return onefile_filename()

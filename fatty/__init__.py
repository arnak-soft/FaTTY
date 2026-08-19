from fatty.version import resolve_version, version_tuple

APP_NAME = "FaTTY"
__version__ = resolve_version()


def version_info() -> tuple[int, int, int, int]:
    return version_tuple(__version__)


def exe_stem() -> str:
    return f"{APP_NAME} {__version__}"


def exe_filename() -> str:
    return f"{exe_stem()}.exe"

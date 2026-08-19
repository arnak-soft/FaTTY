"""Build assets/app.ico and assets/app.png from the master illustration."""
from __future__ import annotations

from collections import deque
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "assets"
SOURCE = OUT_DIR / "icon-source.png"
PNG_SIZE = 512
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)


def _knockout_dark_backdrop(im: Image.Image, luma_max: int = 36) -> Image.Image:
    """Make the black field outside the rounded square transparent."""
    img = im.convert("RGBA")
    w, h = img.size
    px = img.load()
    assert px is not None

    def is_backdrop(x: int, y: int) -> bool:
        r, g, b, a = px[x, y]
        return a > 0 and (r + g + b) // 3 <= luma_max

    seen = bytearray(w * h)
    q: deque[tuple[int, int]] = deque()
    for start in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
        q.append(start)
        seen[start[1] * w + start[0]] = 1

    while q:
        x, y = q.popleft()
        if not is_backdrop(x, y):
            continue
        px[x, y] = (0, 0, 0, 0)
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < w and 0 <= ny < h:
                i = ny * w + nx
                if not seen[i]:
                    seen[i] = 1
                    q.append((nx, ny))
    return img


def _resize(im: Image.Image, size: int) -> Image.Image:
    return im.resize((size, size), Image.Resampling.LANCZOS)


def main() -> None:
    if not SOURCE.is_file():
        raise SystemExit(f"Missing master icon: {SOURCE}")

    master = _knockout_dark_backdrop(Image.open(SOURCE))
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    png_path = OUT_DIR / "app.png"
    _resize(master, PNG_SIZE).save(png_path, format="PNG")

    images = [_resize(master, size) for size in ICO_SIZES]
    ico_path = OUT_DIR / "app.ico"
    ordered = list(reversed(images))
    ordered[0].save(ico_path, format="ICO", append_images=ordered[1:])

    print(f"Wrote {png_path}")
    print(f"Wrote {ico_path}")


if __name__ == "__main__":
    main()

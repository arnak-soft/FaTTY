"""Generate assets/app.ico and assets/app.png for FaTTY."""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "assets"

NAVY = (11, 33, 54, 255)
NAVY_INNER = (18, 52, 82, 255)
CYAN = (56, 198, 232, 255)
GREEN = (61, 220, 151, 255)
BLUE = (91, 141, 239, 255)
GLOW = (56, 198, 232, 70)


def _u(size: int, value: float) -> int:
    return max(1, int(round(value * size / 256)))


def render(size: int) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    pad = _u(size, 6)
    radius = _u(size, 56)
    draw.rounded_rectangle([pad, pad, size - pad - 1, size - pad - 1], radius=radius, fill=NAVY)

    if size >= 24:
        inset = _u(size, 38)
        inner_r = _u(size, 28)
        draw.rounded_rectangle(
            [inset, inset, size - inset - 1, size - inset - 1],
            radius=inner_r,
            fill=NAVY_INNER,
        )
        # title-bar server LEDs
        cy = inset + _u(size, 22)
        rdot = max(1, _u(size, 7))
        gap = _u(size, 22)
        start = inset + _u(size, 18)
        for i, color in enumerate((GREEN, CYAN, BLUE)):
            cx = start + i * gap
            draw.ellipse([cx - rdot, cy - rdot, cx + rdot, cy + rdot], fill=color)

        left = _u(size, 98)
        right = _u(size, 178)
        top = _u(size, 108)
        bot = _u(size, 188)
        mid_y = (top + bot) // 2
        play = [(left, top), (left, bot), (right, mid_y)]
    else:
        left = _u(size, 86)
        right = _u(size, 186)
        top = _u(size, 72)
        bot = _u(size, 184)
        mid_y = (top + bot) // 2
        play = [(left, top), (left, bot), (right, mid_y)]

    if size >= 48:
        glow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        gdraw = ImageDraw.Draw(glow)
        gdraw.polygon(play, fill=GLOW)
        glow = glow.filter(ImageFilter.GaussianBlur(radius=max(2, _u(size, 10))))
        img = Image.alpha_composite(img, glow)
        draw = ImageDraw.Draw(img)

    draw.polygon(play, fill=CYAN)
    return img


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    sizes = (16, 24, 32, 48, 64, 128, 256)
    images = [render(s) for s in sizes]
    ico_path = OUT_DIR / "app.ico"
    # Largest first so Windows Explorer prefers the 256px PNG frame.
    ordered = list(reversed(images))
    ordered[0].save(ico_path, format="ICO", append_images=ordered[1:])
    png_path = OUT_DIR / "app.png"
    render(256).save(png_path, format="PNG")
    print(f"Wrote {ico_path}")
    print(f"Wrote {png_path}")


if __name__ == "__main__":
    main()

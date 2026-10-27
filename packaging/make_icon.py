#!/usr/bin/env python3
"""Render app/win/boringshot.ico from the logo geometry (docs/assets/logo.svg).

Dev-only tool (needs Pillow); the .ico is committed so builds stay dependency-free.
Each size is drawn on its own at 8x and downsampled. Sizes <= 24 px use a bolder,
simplified cut (thicker strokes, no landing dot) so the tray icon stays legible.
"""
from pathlib import Path

from PIL import Image, ImageDraw

INK, PAPER, LEADER = (0x15, 0x18, 0x1D, 255), (0xF4, 0xF1, 0xEA, 255), (0xFF, 0x6A, 0x2B, 255)
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
SS = 8  # supersampling factor
BRACKETS = (((13, 23), (13, 13), (23, 13)), ((41, 13), (51, 13), (51, 23)),
            ((51, 41), (51, 51), (41, 51)), ((23, 51), (13, 51), (13, 41)))


def render(size: int) -> Image.Image:
    small = size <= 24
    arm, w = (3.5, 7.0) if small else (2.0, 3.5)  # bracket half-width, leader width
    k = size * SS / 64.0
    img = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = lambda p: (p[0] * k, p[1] * k)
    d.rounded_rectangle((0, 0, 64 * k - 1, 64 * k - 1), radius=(10 if small else 14) * k, fill=INK)
    for a, b, c in BRACKETS:  # square caps: each arm is a filled rectangle
        for p, q in ((a, b), (b, c)):
            x0, x1 = sorted((p[0], q[0]))
            y0, y1 = sorted((p[1], q[1]))
            d.rectangle((s((x0 - arm, y0 - arm)), s((x1 + arm, y1 + arm))), fill=PAPER)
    knee, end = ((37, 27), (46, 27)) if small else ((38, 26), (47, 26))
    for p, q in (((27, 37), knee), (knee, end)):  # round caps/joins
        d.line((s(p), s(q)), fill=LEADER, width=round(w * k))
    for p in ((27, 37), knee, end):
        r = w / 2 * k
        d.ellipse((p[0] * k - r, p[1] * k - r, p[0] * k + r, p[1] * k + r), fill=LEADER)
    head = ((18, 46), (22.5, 30.5), (33.5, 41.5)) if small else ((21, 43), (24.6, 33.1), (30.9, 39.4))
    d.polygon(tuple(s(p) for p in head), fill=LEADER)
    if small:
        return img.resize((size, size), Image.LANCZOS)
    r = 2.6 * k
    d.ellipse((47 * k - r, 26 * k - r, 47 * k + r, 26 * k + r), fill=LEADER)
    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    images = [render(n) for n in SIZES]
    out = root / "app" / "win" / "boringshot.ico"
    images[-1].save(out, format="ICO", sizes=[(n, n) for n in SIZES], append_images=images[:-1])
    print(f"wrote {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()

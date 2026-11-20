"""Generate src/win/geditor.ico, the application icon (2026-11-20).

The design (chosen by the user from the proposals, "案 14"): a Windows-style
window with a blue title bar and three buttons, white text lines and a caret,
an Emacs-like purple mode line and a teal echo area, on a dark rounded tile.
The shapes are defined on a 64x64 grid below; every size is drawn from it with
the edges snapped to whole pixels (so that 16 / 20 / 24 px stay sharp), at 4
times the size, and reduced (only the rounded corners get grey pixels).

The .ico holds one PNG per size (Windows Vista and later read PNG entries).
The same input gives the same bytes for the same Pillow version.

    python -B tools/gen_icon.py            # writes src/win/geditor.ico
    python -B tools/gen_icon.py --png DIR  # also writes DIR/geditor-<size>.png

(Not -I: Pillow is usually in the user's site-packages.)
"""

import argparse
import io
import math
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.dont_write_bytecode = True

SIZES = (16, 20, 24, 32, 40, 48, 64, 256)
SCALE = 4  # supersampling

TILE = "#1E2630"
PAPER = "#F4F1EA"
BLUE = "#3A7BD5"
TEXT = "#5C6E80"
PURPLE = "#8E6CEF"
TEAL = "#2EC4B6"

# (x0, y0, x1, y1, colour, corner radius) on the 64 grid, back to front.
# "top" rounds only the top corners (the title bar).
SHAPES = (
    (2, 2, 62, 62, TILE, 13, "all"),
    (7, 10, 57, 54, PAPER, 5, "all"),
    (7, 10, 57, 20, BLUE, 5, "top"),
    (38, 13, 42, 17, PAPER, 0, "all"),
    (44, 13, 48, 17, PAPER, 0, "all"),
    (50, 13, 54, 17, PAPER, 0, "all"),
    (12, 25, 40, 28.5, TEXT, 0, "all"),
    (12, 32, 32, 35.5, TEXT, 0, "all"),
    (34, 30, 36.5, 37.5, TILE, 0, "all"),
    (7, 41, 57, 47, PURPLE, 0, "all"),
    (12, 49.5, 24, 52, TEAL, 0, "all"),
)


def snap(v, size):
    """A 64-grid coordinate as a whole pixel of an image of `size`."""
    return math.floor(v * size / 64 + 0.5)


def draw(size):
    big = size * SCALE
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    for x0, y0, x1, y1, colour, radius, corners in SHAPES:
        a, b = snap(x0, size), snap(y0, size)
        c, e = max(snap(x1, size), a + 1), max(snap(y1, size), b + 1)
        box = (a * SCALE, b * SCALE, c * SCALE - 1, e * SCALE - 1)
        r = radius * size / 64 * SCALE
        if r <= 0:
            d.rectangle(box, fill=colour)
        elif corners == "top":
            d.rounded_rectangle(box, r, fill=colour, corners=(True, True, False, False))
        else:
            d.rounded_rectangle(box, r, fill=colour)
    return image.resize((size, size), Image.LANCZOS)


def png_bytes(image):
    out = io.BytesIO()
    image.save(out, format="PNG", optimize=False)
    return out.getvalue()


def ico_bytes(pngs):
    header = struct.pack("<HHH", 0, 1, len(pngs))
    entries = b""
    data = b""
    offset = 6 + 16 * len(pngs)
    for size, png in pngs:
        dim = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(png), offset + len(data))
        data += png
    return header + entries + data


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--png", type=Path, help="also write one PNG per size into this folder")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    pngs = []
    for size in SIZES:
        image = draw(size)
        pngs.append((size, png_bytes(image)))
        if args.png:
            args.png.mkdir(parents=True, exist_ok=True)
            image.save(args.png / f"geditor-{size}.png")
    target = root / "src" / "win" / "geditor.ico"
    target.write_bytes(ico_bytes(pngs))
    print(f"wrote {target} ({', '.join(str(s) for s in SIZES)} px)")


if __name__ == "__main__":
    main()

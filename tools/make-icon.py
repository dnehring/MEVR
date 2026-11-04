# SPDX-License-Identifier: MIT
"""Draws MEVR's application icon, assets/icon/mevr.ico (MEVR-Setup.exe's, tools/setup/Setup.rc).

    python tools/make-icon.py [--out assets/icon/mevr.ico] [--png <folder>]

The mark is the menu's own brand (src/plugin/menu/MenuDecorators.cpp, "brand": a hexagon with a chevron and a bar,
in a 40-unit box) on a tile of the menu's dark glass (assets/menu/main.rcss: #1F3B50 to #0D1B27, a lit rim), the
hexagon and chevron in its cyan (#7CCBF2), the bar in its orange accent (#FF8F3F). Our own artwork, MIT.

Each size is drawn on its own at 8x and scaled down, with strokes thickened at the small sizes so they stay one pixel
or more wide; every entry is stored as PNG (Windows Vista and later read PNG entries at any size). The output is
deterministic: run it again after changing the drawing, and commit the script and the .ico together. Needs Pillow.
"""
import argparse
import io
import os
import struct

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
SUPER = 8

CYAN = (124, 203, 242)
ORANGE = (255, 143, 63)
GLASS_TOP = (31, 59, 80)
GLASS_BOTTOM = (13, 27, 39)
RIM = (150, 215, 245)

# The brand mark in its 40-unit box (MenuDecorators.cpp).
HEXAGON = [(20, 3), (35, 11.5), (35, 28.5), (20, 37), (5, 28.5), (5, 11.5)]
CHEVRON = [(13, 22), (20, 12), (27, 22)]
BAR = [(16, 26), (24, 26)]


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def draw(size):
    s = size * SUPER
    img = Image.new('RGBA', (s, s), (0, 0, 0, 0))

    # The tile: a rounded square of dark glass, a vertical gradient, a lit rim.
    margin = round(s * (0.03 if size >= 32 else 0.0))
    radius = round(s * 0.22)
    tile = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    gradient = Image.new('RGBA', (s, s))
    gd = ImageDraw.Draw(gradient)
    for y in range(s):
        gd.line([(0, y), (s, y)], fill=lerp(GLASS_TOP, GLASS_BOTTOM, y / (s - 1)) + (255,))
    mask = Image.new('L', (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([margin, margin, s - 1 - margin, s - 1 - margin], radius=radius, fill=255)
    tile.paste(gradient, (0, 0), mask)
    rim = max(SUPER, round(s * 0.012))
    ImageDraw.Draw(tile).rounded_rectangle([margin, margin, s - 1 - margin, s - 1 - margin], radius=radius,
                                           outline=RIM + (110,), width=rim)
    img.alpha_composite(tile)

    # The mark, scaled from its 40-unit box to most of the tile.
    span = (s - 2 * margin) * 0.80
    origin = (s - span) / 2.0
    unit = span / 40.0

    def at(points):
        return [(origin + x * unit, origin + y * unit) for x, y in points]

    # The brand's stroke is 2 units; small icons need at least ~1.3 px.
    stroke = max(2.0 * unit, 1.3 * SUPER if size <= 24 else 1.1 * SUPER)
    if size <= 20:
        stroke = max(stroke, 1.6 * SUPER)
    width = round(stroke)

    marks = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    md = ImageDraw.Draw(marks)
    hexagon = at(HEXAGON)
    md.line(hexagon + hexagon[:2], fill=CYAN + (255,), width=width, joint='curve')
    md.line(at(CHEVRON), fill=CYAN + (255,), width=width, joint='curve')
    for x, y in at(CHEVRON)[::2]:
        md.ellipse([x - width / 2, y - width / 2, x + width / 2, y + width / 2], fill=CYAN + (255,))
    bar = at(BAR)
    md.line(bar, fill=ORANGE + (255,), width=width)
    for x, y in bar:
        md.ellipse([x - width / 2, y - width / 2, x + width / 2, y + width / 2], fill=ORANGE + (255,))
    img.alpha_composite(marks)

    return img.resize((size, size), Image.Resampling.LANCZOS)


def ico(images):
    """An .ico with one PNG-compressed entry per image (ICONDIR, ICONDIRENTRY[], data)."""
    blobs = []
    for image in images:
        out = io.BytesIO()
        image.save(out, format='PNG', optimize=True)
        blobs.append(out.getvalue())
    header = struct.pack('<HHH', 0, 1, len(images))
    offset = len(header) + 16 * len(images)
    entries = b''
    for image, blob in zip(images, blobs):
        w, h = image.size
        entries += struct.pack('<BBBBHHII', w % 256, h % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    return header + entries + b''.join(blobs)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', default=os.path.join(ROOT, 'assets', 'icon', 'mevr.ico'))
    parser.add_argument('--png', help='also write each size as a PNG into this folder, to look at')
    args = parser.parse_args()
    images = [draw(size) for size in SIZES]
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'wb') as fh:
        fh.write(ico(images))
    if args.png:
        os.makedirs(args.png, exist_ok=True)
        for image in images:
            image.save(os.path.join(args.png, f'mevr-{image.size[0]}.png'))
    print(f'{args.out}: {len(images)} sizes ({", ".join(str(s) for s in SIZES)}), {os.path.getsize(args.out):,} bytes')


if __name__ == '__main__':
    main()

# SPDX-License-Identifier: MIT
"""The cockpit's decals as one atlas: labels, panel numbers and hazard stripes, for the hull's DECAL faces
(tools/cockpit/hull_assemble.py bakes them into the shell's texture; research/48 section 24). Drawn here with PIL, which
Blender's Python lacks; hull_assemble runs this with the system Python before it builds.

    python tools/cockpit/decals.py <out folder>

Writes <out folder>/decals.png (2048 x 2048, sRGB) and decals.json: each decal's rectangle in the atlas as Blender's UVs
(u0, v0, u1, v1, v from the bottom) and its aspect. The text is set in Rajdhani Bold (third_party/fonts, SIL OFL 1.1).
No game data: generic markings, nothing of the game's own insignia.
"""
import json
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

SIZE = 2048
FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "third_party", "fonts", "Rajdhani-Bold.ttf")
PLATE, BORDER, INK = (52, 50, 48), (96, 93, 89), (208, 205, 198)
AMBER, BLACK = (214, 142, 34), (36, 34, 31)
# name: (width, height in px, text or None, style). Labels are dark plates with light text, "warn" amber with dark text,
# "hazard" diagonal stripes.
DECALS = {
    "hatch": (1024, 160, "REAR HATCH", "label"),
    "vent-a": (512, 112, "VENT A2", "label"),
    "vent-b": (512, 112, "VENT B2", "label"),
    "life": (768, 112, "LIFE SUPPORT", "label"),
    "comms": (768, 112, "COMMS RELAY", "label"),
    "caution": (896, 128, "CAUTION  HIGH VOLTAGE", "warn"),
    "r-01": (320, 128, "R-01", "label"),
    "l-01": (320, 128, "L-01", "label"),
    "p-03": (320, 128, "P-03", "label"),
    "p-04": (320, 128, "P-04", "label"),
    "hazard": (1024, 128, None, "hazard"),
    "hazard-long": (2048, 96, None, "hazard"),
    "hazard-post": (256, 512, None, "hazard"),
}


def Draw(image, box, text, style):
    x0, y0, x1, y1 = box
    d = ImageDraw.Draw(image)
    if style == "hazard":  # stripes at 45 degrees, drawn on a tile of their own so none runs past the box
        w, h = x1 - x0 + 1, y1 - y0 + 1
        tile = Image.new("RGB", (w, h), BLACK)
        t = ImageDraw.Draw(tile)
        step = max(w, h) / (8 if w > h else 4)
        k = -h
        while k < w + h:
            t.polygon([(k, h), (k + step / 2, h), (k + step / 2 + h, 0), (k + h, 0)], fill=AMBER)
            k += step
        t.rectangle((0, 0, w - 1, h - 1), outline=BLACK, width=6)
        image.paste(tile, (x0, y0))
        return
    back, ink = (AMBER, BLACK) if style == "warn" else (PLATE, INK)
    d.rectangle(box, fill=back)
    d.rectangle((x0 + 8, y0 + 8, x1 - 8, y1 - 8), outline=BORDER if style == "label" else BLACK, width=4)
    size = int((y1 - y0) * 0.62)
    font = ImageFont.truetype(FONT, size)
    while font.getlength(text) > (x1 - x0) * 0.86 and size > 8:
        size -= 2
        font = ImageFont.truetype(FONT, size)
    d.text(((x0 + x1) / 2, (y0 + y1) / 2), text, font=font, fill=ink, anchor="mm")


def Main(out):
    image = Image.new("RGB", (SIZE, SIZE), PLATE)
    rects, x, y, row = {}, 0, 0, 0
    pad = 16
    for name, (w, h, text, style) in sorted(DECALS.items(), key=lambda kv: -kv[1][1]):  # shelves by height
        if x + w > SIZE:
            x, y, row = 0, y + row + pad, 0
        Draw(image, (x, y, x + w - 1, y + h - 1), text, style)
        rects[name] = {"uv": [x / SIZE, 1 - (y + h) / SIZE, (x + w) / SIZE, 1 - y / SIZE], "aspect": w / h}
        x, row = x + w + pad, max(row, h)
    if y + row > SIZE:
        raise SystemExit("decals: the atlas overflows")
    os.makedirs(out, exist_ok=True)
    image.save(os.path.join(out, "decals.png"))
    with open(os.path.join(out, "decals.json"), "w", encoding="utf-8") as f:
        json.dump({"size": SIZE, "font": "Rajdhani Bold (SIL OFL 1.1)", "decals": rects}, f, indent=1)
    print("decals:", len(rects), "into", os.path.join(out, "decals.png"))


if __name__ == "__main__":
    Main(sys.argv[1])

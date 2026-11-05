# SPDX-License-Identifier: MIT
"""The repair button's texture, fixed: Tripo lettered its cover "FEGAIP"; this paints the cover's plate clean there and
sets "REPAIR" in its place (research/48 section 27). Drawn with PIL, which Blender's Python lacks; panel_controls.py
runs this with the system Python.

    python tools/cockpit/panel_textures.py <the model's base colour image> <out.png>

The rectangles are in the 4096-pixel texture's rows from the top, measured on 2026-11-04: the cover's lettering is
upside down there (the cover's UVs turn it half a turn), so the new word is set upright and turned the same way. The
plate under it is patched from the clean plate beside the wrench, just above the old word, with feathered edges, so its
grain and wear carry on. The text is Rajdhani Bold (third_party/fonts, SIL OFL 1.1), the old letters' grey.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "third_party", "fonts", "Rajdhani-Bold.ttf")
SIZE = 4096
WORD = (1765, 1925, 2300, 2082)  # the old word, with a margin: x0, y0, x1, y1
# where the patches come from (the same rows' height, just above the word, clear of the wrench, which spans x 1955 to
# 2140 there, and of the plate's inner border at x 2370) and go
PATCHES = [((1722, 1768, 1952, 1925), (1765, 1925)), ((1722, 1768, 1782, 1925), (1995, 1925)),
           ((2144, 1768, 2364, 1925), (2052, 1925)), ((2144, 1768, 2174, 1925), (2272, 1925))]
CENTRE = (2032, 2003)  # the old word's middle
CAP = 108  # a little under the old letters' 118 px, so the word keeps a margin to the plate's edge
INK = (206, 208, 212)
FEATHER = 10


def Fix(path, out):
    image = Image.open(path).convert("RGB")
    if image.size != (SIZE, SIZE):
        raise SystemExit(f"{path}: expected {SIZE} x {SIZE}, found {image.size}")
    clean = image.copy()
    for (x0, y0, x1, y1), at in PATCHES:
        clean.paste(image.crop((x0, y0, x1, y1)), at)
    mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(mask).rectangle(WORD, fill=255)
    image = Image.composite(clean, image, mask.filter(ImageFilter.GaussianBlur(FEATHER)))

    # the word, upright on a layer of its own, then turned half a turn as the cover's UVs are
    font = ImageFont.truetype(FONT, 10)
    box = font.getbbox("H")
    font = ImageFont.truetype(FONT, round(10 * CAP / (box[3] - box[1])))
    left, top, right, bottom = font.getbbox("REPAIR")
    w, h = right - left + 24, bottom - top + 24
    layer = Image.new("L", (w, h), 0)
    shadow = Image.new("L", (w, h), 0)
    ImageDraw.Draw(layer).text((12 - left, 12 - top), "REPAIR", font=font, fill=255)
    ImageDraw.Draw(shadow).text((15 - left, 15 - top), "REPAIR", font=font, fill=150)  # the old letters' lower-right edge
    layer, shadow = layer.rotate(180), shadow.rotate(180)
    at = (CENTRE[0] - w // 2, CENTRE[1] - h // 2)
    image.paste(Image.new("RGB", (w, h), (28, 28, 30)), at, shadow.filter(ImageFilter.GaussianBlur(1.5)))
    # printed on the plate: the plate's own grain and wear show through a little
    plate = image.crop((at[0], at[1], at[0] + w, at[1] + h))
    image.paste(Image.blend(Image.new("RGB", (w, h), INK), plate, 0.12), at, layer.filter(ImageFilter.GaussianBlur(0.6)))
    image.save(out)
    print(f"RESULT panel_textures: {out}: cover lettering REPAIR at {CENTRE}, cap {CAP} px")


if __name__ == "__main__":
    Fix(sys.argv[1], sys.argv[2])

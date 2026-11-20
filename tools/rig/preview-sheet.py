# SPDX-License-Identifier: MIT
"""Contact sheets of preview-render.py's PNGs for tools/rig/preview-sizes.ps1: one row per view, one column per body.

  python tools/rig/preview-sheet.py <render folder> <out.png> <title> <name>=<label> ...
  python tools/rig/preview-sheet.py --stack <out.png> <sheet.png> ...    (sheets one under the other)

Local only (the player's game data).
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

VIEWS = [('front', 'front'), ('side', 'side'), ('back34', 'three quarters behind')]


def font(size):
    for name in ('segoeui.ttf', 'arial.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def sheet(folder, out, title, cols):
    first = Image.open(os.path.join(folder, f'{cols[0][0]}-front.png'))
    w, h = first.size
    head, label_h, side = 56, 34, 150
    img = Image.new('RGB', (side + w * len(cols), head + label_h * 2 + h * len(VIEWS)), (24, 24, 26))
    d = ImageDraw.Draw(img)
    d.text((14, 12), title, fill=(235, 235, 235), font=font(26))
    for c, (name, label) in enumerate(cols):
        for i, line in enumerate(label.split('|')[:2]):
            d.text((side + c * w + 10, head + i * label_h), line, fill=(220, 220, 220) if i == 0 else (160, 160, 160), font=font(20 if i == 0 else 17))
        for r, (view, view_label) in enumerate(VIEWS):
            tile = Image.open(os.path.join(folder, f'{name}-{view}.png')).convert('RGB')
            img.paste(tile, (side + c * w, head + label_h * 2 + r * h))
            if c == 0:
                d.text((12, head + label_h * 2 + r * h + h // 2 - 12), view_label, fill=(200, 200, 200), font=font(18))
    img.save(out)
    print(out, img.size)


def stack(out, parts):
    ims = [Image.open(p).convert('RGB') for p in parts]
    w = max(i.size[0] for i in ims)
    img = Image.new('RGB', (w, sum(i.size[1] for i in ims)), (24, 24, 26))
    y = 0
    for i in ims:
        img.paste(i, (0, y))
        y += i.size[1]
    # A single image over 8000 px tall is hard to open: halve it then.
    if img.size[1] > 8000:
        img = img.resize((img.size[0] // 2, img.size[1] // 2), Image.LANCZOS)
    img.save(out)
    print(out, img.size)


if __name__ == '__main__':
    if sys.argv[1] == '--stack':
        stack(sys.argv[2], sys.argv[3:])
    else:
        sheet(sys.argv[1], sys.argv[2], sys.argv[3], [a.split('=', 1) for a in sys.argv[4:]])

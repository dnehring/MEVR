# SPDX-License-Identifier: MIT
"""The game's sniper scope art as our scope picture uses it (research/21 section 16).

LE1's HUD reticle movie (GUI_SF_HUD.Reticule, a GFx/SWF in Startup_INT.pcc) draws the sniper rifle's zoomed view in its
`SniperMode` clip: a dirty lens mask (Reticule_I4C), the cyan reticle cut from one atlas (Reticule_I4E: post, stems, arcs,
bracket, some pieces mirrored) and the tube's rings (Reticule_I41) on top. This reads the movie's display list at the
clip's last frame (the zoom-in animation settled) and prints the pieces for src/core/vr/ScopeArt.cpp: each one's texture,
its map from texels (the image's logical size, GFx's DefineExternalImage2 target size) to the movie's pixels, the texels it
covers, its alpha. With the textures it also measures the field (the mask's clear disc) and the reticle's centre, and can
draw the composed view.

The movie and the textures are game data: export them locally, never into the repository:
  tools/cockpit/package/bin/Release/net10.0-windows/cockpit-package.exe export <ME1>/BioGame/CookedPCConsole/Startup_INT.pcc
      --filter GUI_SF_HUD.Reticule --out %LOCALAPPDATA%/MEVR/scope-art
  python tools/scope-art.py %LOCALAPPDATA%/MEVR/scope-art/Reticule.gfx [--textures %LOCALAPPDATA%/MEVR/scope-art]
      [--preview out.png]
"""
import argparse
import math
import os
import struct
import sys


class Bits:
    def __init__(self, data, byte):
        self.data = data
        self.pos = byte * 8

    def u(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.data[self.pos >> 3] >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def s(self, n):
        v = self.u(n)
        return v - (1 << n) if n and v & (1 << (n - 1)) else v

    def fixed(self, n):
        return self.s(n) / 65536.0

    def align(self):
        self.pos = (self.pos + 7) & ~7

    def byte(self):
        return self.pos >> 3


def read_rect(b):
    n = b.u(5)
    r = [b.s(n) for _ in range(4)]
    b.align()
    return r  # xmin, xmax, ymin, ymax (twips)


def read_matrix(b):
    sx = sy = 1.0
    r0 = r1 = 0.0
    if b.u(1):
        n = b.u(5)
        sx, sy = b.fixed(n), b.fixed(n)
    if b.u(1):
        n = b.u(5)
        r0, r1 = b.fixed(n), b.fixed(n)
    n = b.u(5)
    tx, ty = b.s(n), b.s(n)
    b.align()
    return [sx, r0, r1, sy, tx, ty]  # x' = sx x + r1 y + tx, y' = r0 x + sy y + ty


def tags(data, pos, end):
    while pos < end:
        head = struct.unpack_from('<H', data, pos)[0]
        pos += 2
        code, length = head >> 6, head & 0x3f
        if length == 0x3f:
            length = struct.unpack_from('<I', data, pos)[0]
            pos += 4
        yield code, data[pos:pos + length]
        pos += length
        if code == 0:
            break


def cstring(data, pos):
    end = data.index(b'\0', pos)
    return data[pos:end].decode('latin1'), end + 1


def fill_styles(b, data, version):
    count = data[b.byte()]
    b.pos += 8
    if count == 0xff:
        count = struct.unpack_from('<H', data, b.byte())[0]
        b.pos += 16
    fills = []
    for _ in range(count):
        kind = data[b.byte()]
        b.pos += 8
        if kind == 0x00:
            b.pos += 32 if version >= 3 else 24
            fills.append(None)
        elif kind in (0x10, 0x12, 0x13):
            read_matrix(b)
            b.u(4)
            n = b.u(4)
            b.pos += n * (8 + (32 if version >= 3 else 24))
            if kind == 0x13:
                b.pos += 16
            fills.append(None)
        elif kind in (0x40, 0x41, 0x42, 0x43):
            bitmap = struct.unpack_from('<H', data, b.byte())[0]
            b.pos += 16
            fills.append((bitmap, read_matrix(b)))
        else:
            raise ValueError(f'fill style {kind:#x}')
    return fills


def read_place(code, body):
    out = {}
    if code == 26 or code == 70:
        flags = body[0]
        p = 2 if code == 70 else 1
        flags2 = body[1] if code == 70 else 0
        out['depth'] = struct.unpack_from('<H', body, p)[0]
        p += 2
        out['move'] = bool(flags & 1)
        if code == 70 and (flags2 & 0x08 or (flags2 & 0x10 and flags & 0x02)):
            _, p = cstring(body, p)
        if flags & 0x02:
            out['id'] = struct.unpack_from('<H', body, p)[0]
            p += 2
        if flags & 0x04:
            b = Bits(body, p)
            out['matrix'] = read_matrix(b)
            p = b.byte()
        if flags & 0x08:
            b = Bits(body, p)
            has_add, has_mult, n = b.u(1), b.u(1), None
            n = b.u(4)
            mult = [b.s(n) / 256.0 for _ in range(4)] if has_mult else None
            if has_add:
                [b.s(n) for _ in range(4)]
            b.align()
            p = b.byte()
            out['alpha'] = mult[3] if mult else 1.0
        if flags & 0x10:
            p += 2
        if flags & 0x20:
            out['name'], p = cstring(body, p)
    return out


def read_movie(path):
    data = open(path, 'rb').read()
    if data[:3] not in (b'GFX', b'FWS'):
        raise ValueError(f'{path}: not an uncompressed GFx/SWF movie')
    b = Bits(data, 8)
    read_rect(b)
    pos = b.byte() + 4
    movie = {'shapes': {}, 'sprites': {}, 'images': {}, 'exports': {}}
    for code, body in tags(data, pos, len(data)):
        if code in (2, 22, 32, 83):
            cid = struct.unpack_from('<H', body, 0)[0]
            bb = Bits(body, 2)
            bounds = read_rect(bb)
            version = {2: 1, 22: 2, 32: 3, 83: 4}[code]
            if version == 4:
                read_rect(bb)
                bb.pos += 8
            movie['shapes'][cid] = {'bounds': bounds, 'fills': fill_styles(bb, body, version)}
        elif code == 39:
            sid = struct.unpack_from('<H', body, 0)[0]
            frames = [[]]
            for c2, b2 in tags(body, 4, len(body)):
                if c2 in (26, 70):
                    frames[-1].append(('place', read_place(c2, b2)))
                elif c2 == 28:
                    frames[-1].append(('remove', struct.unpack_from('<H', b2, 0)[0]))
                elif c2 == 1:
                    frames.append([])
            movie['sprites'][sid] = frames
        elif code == 1009:  # Scaleform's DefineExternalImage2: id, format, target width and height, export and file names
            cid, _, width, height = struct.unpack_from('<IHHH', body, 0)
            q = 10
            n = body[q]
            q += 1 + n
            n = body[q]
            name = body[q + 1:q + 1 + n].decode('latin1')
            movie['images'][cid] = {'name': os.path.splitext(name)[0], 'w': width, 'h': height}
        elif code == 56:
            n = struct.unpack_from('<H', body, 0)[0]
            q = 2
            for _ in range(n):
                cid = struct.unpack_from('<H', body, q)[0]
                name, q = cstring(body, q + 2)
                movie['exports'][name] = cid
    return movie


def compose(a, b):
    """a after b, both [sx, r0, r1, sy, tx, ty]."""
    return [a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1], a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
            a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5]]


def settled(frames):
    """A sprite's display list after its last frame: depth -> {id, matrix, alpha, name}."""
    shown = {}
    for frame in frames:
        for kind, v in frame:
            if kind == 'remove':
                shown.pop(v, None)
                continue
            d = v['depth']
            if 'id' in v and not v['move']:
                shown[d] = {'id': v['id'], 'matrix': v.get('matrix', [1, 0, 0, 1, 0, 0]), 'alpha': 1.0, 'name': v.get('name', '')}
            elif d in shown:
                if 'id' in v:
                    shown[d]['id'] = v['id']
                if 'matrix' in v:
                    shown[d]['matrix'] = v['matrix']
            if d in shown and 'alpha' in v:
                shown[d]['alpha'] = v['alpha']
    return shown


SKIP = {'distanceMC', 'wideDetail'}  # the zoom and range text, and the frame's widescreen sides (outside our disc)
LAYER = {'Reticule_I4E': 'Reticle'}  # everything else is the tube (mask and rings)


def pieces(movie, clip='SniperMode'):
    out = []

    def walk(cid, matrix, alpha):
        if cid in movie['sprites']:
            for _, e in sorted(settled(movie['sprites'][cid]).items()):
                if e['name'] not in SKIP:
                    walk(e['id'], compose(matrix, e['matrix']), alpha * e['alpha'])
        elif cid in movie['shapes']:
            shape = movie['shapes'][cid]
            for fill in shape['fills']:
                if fill is None or fill[0] not in movie['images']:
                    continue
                image = movie['images'][fill[0]]
                f = fill[1]  # texels -> the shape's twips
                t = compose(matrix, f)
                t = t[:4] + [t[4], t[5]]
                t = [v / 20.0 for v in t]  # twips -> pixels (the matrices' scales carry the 20 twips a pixel)
                xs = [(shape['bounds'][0] - f[4]) / f[0], (shape['bounds'][1] - f[4]) / f[0]]
                ys = [(shape['bounds'][2] - f[5]) / f[3], (shape['bounds'][3] - f[5]) / f[3]]
                out.append({'shape': cid, 'texture': image['name'], 'w': image['w'], 'h': image['h'], 'map': t,
                            'clip': [max(0.0, min(xs)), max(0.0, min(ys)), min(image['w'], max(xs)), min(image['h'], max(ys))],
                            'alpha': alpha, 'layer': LAYER.get(image['name'], 'Tube')})

    walk(movie['exports'][clip], [1, 0, 0, 1, 0, 0], 1.0)
    return out


def measure(found, textures):
    """The field (the mask's clear disc) and the reticle's centre, in the movie's pixels."""
    from PIL import Image
    mask = next(p for p in found if p['texture'] == 'Reticule_I4C')
    image = Image.open(os.path.join(textures, 'Reticule_I4C.png')).convert('RGBA').resize((mask['w'], mask['h']))
    alpha = image.getchannel('A').load()
    sx = sy = n = 0
    for y in range(0, mask['h'], 4):
        for x in range(0, mask['w'], 4):
            if alpha[x, y] < 128:
                sx += x
                sy += y
                n += 1
    cu, cv = sx / n, sy / n
    radii = []
    for angle in range(0, 360, 15):
        r = 0
        while True:
            x = int(cu + r * math.cos(math.radians(angle)))
            y = int(cv + r * math.sin(math.radians(angle)))
            if not (0 <= x < mask['w'] and 0 <= y < mask['h']) or alpha[x, y] >= 128:
                break
            r += 1
        radii.append(r)
    m = mask['map']
    radii.sort()
    field = (m[0] * cu + m[2] * cv + m[4], m[1] * cu + m[3] * cv + m[5], m[0] * radii[len(radii) // 2])  # the median: specks cut some rays short
    # The reticle: the stems' row (the widest piece's brightest row) and the post's column (the tallest unmirrored piece's).
    atlas = Image.open(os.path.join(textures, 'Reticule_I4E.png')).convert('RGBA')
    aa = atlas.getchannel('A').load()
    reticle = [p for p in found if p['layer'] == 'Reticle']
    stem = max(reticle, key=lambda p: (p['clip'][2] - p['clip'][0]) / max(p['clip'][3] - p['clip'][1], 1))
    post = max((p for p in reticle if p['map'][0] > 0), key=lambda p: (p['clip'][3] - p['clip'][1]) / max(p['clip'][2] - p['clip'][0], 1))
    row = max(range(int(stem['clip'][1]), int(stem['clip'][3])), key=lambda y: sum(aa[x, y] for x in range(int(stem['clip'][0]), int(stem['clip'][2]))))
    col = max(range(int(post['clip'][0]), int(post['clip'][2])), key=lambda x: sum(aa[x, y] for y in range(int(post['clip'][1]), int(post['clip'][3]))))
    return field, (post['map'][0] * col + post['map'][4], stem['map'][3] * row + stem['map'][5])


def preview(found, textures, out, size=1000, scale=1.5):
    from PIL import Image, ImageChops, ImageDraw
    canvas = Image.new('RGBA', (size, size), (40, 60, 40, 255))
    c = size / 2
    for p in found:
        tex = Image.open(os.path.join(textures, p['texture'] + '.png')).convert('RGBA').resize((p['w'], p['h']))
        m = [scale * v for v in p['map'][:4]] + [scale * p['map'][4] + c, scale * p['map'][5] + c]
        det = m[0] * m[3] - m[2] * m[1]
        inv = [m[3] / det, -m[1] / det, -m[2] / det, m[0] / det]
        layer = tex.transform((size, size), Image.AFFINE,
                              (inv[0], inv[2], -(inv[0] * m[4] + inv[2] * m[5]), inv[1], inv[3], -(inv[1] * m[4] + inv[3] * m[5])),
                              resample=Image.BILINEAR)
        keep = Image.new('L', (size, size), 0)
        u0, v0, u1, v1 = p['clip']
        corners = [(m[0] * u + m[2] * v + m[4], m[1] * u + m[3] * v + m[5]) for u, v in ((u0, v0), (u1, v0), (u1, v1), (u0, v1))]
        ImageDraw.Draw(keep).polygon(corners, fill=int(255 * p['alpha']))
        layer.putalpha(ImageChops.multiply(layer.getchannel('A'), keep))
        canvas.alpha_composite(layer)
    canvas.save(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('movie')
    ap.add_argument('--textures')
    ap.add_argument('--preview')
    args = ap.parse_args()
    found = pieces(read_movie(args.movie))
    for p in found:
        print(f"// shape {p['shape']}: {p['texture']} ({p['layer']})", file=sys.stderr)
    print('// Generated by tools/scope-art.py from GUI_SF_HUD.Reticule (SniperMode, settled).')
    for p in found:
        m = p['map']
        print(f"    {{Texture::{p['texture'].split('_')[-1]}, Layer::{p['layer']}, {{{m[0]:.6f}f, {m[1]:.6f}f, {m[2]:.6f}f, {m[3]:.6f}f, "
              f"{m[4]:.4f}f, {m[5]:.4f}f}}, {{{p['clip'][0]:.2f}f, {p['clip'][1]:.2f}f, {p['clip'][2]:.2f}f, {p['clip'][3]:.2f}f}}, "
              f"{p['alpha']:.3f}f}},")
    sizes = {p['texture']: (p['w'], p['h']) for p in found}
    print('// logical sizes: ' + ', '.join(f'{k} {w}x{h}' for k, (w, h) in sorted(sizes.items())))
    if args.textures:
        (fx, fy, fr), (rx, ry) = measure(found, args.textures)
        print(f'// field: centre ({fx:.2f}, {fy:.2f}) px, radius {fr:.1f} px; reticle centre ({rx:.2f}, {ry:.2f}) px')
        if args.preview:
            preview(found, args.textures, args.preview)
            print(f'// preview: {args.preview}', file=sys.stderr)


if __name__ == '__main__':
    main()

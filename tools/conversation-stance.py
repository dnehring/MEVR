# SPDX-License-Identifier: MIT
"""Conversations from above (research/69 "Stances from above", 2026-11-12; the owner: "capture the conversational stances from a
top/bottom view so that we can more easily judge the conversations").

The mod writes where everyone stands in a first-person conversation (`mevr convo stance [tag]`, or by itself with `mevr convo
stances on` / [Cinematics] worldStances = 1: after the opening, after each walk, at the end) to %LOCALAPPDATA%\\MEVR\\stances. This
draws them from above, one sheet per conversation, each moment a panel turned so the player looks up the page (each face's
direction in cyan, where the game's look-at aims each head dotted pink; "looks at Shepard's body" names a head turned to
Shepard's own body left on a mark away from the player):

  python tools/conversation-stance.py                      # the conversations of the newest stances (the last hour's)
  python tools/conversation-stance.py --since 2026-11-12T10:00 [--out folder]
  python tools/conversation-stance.py a.json b.json ...
  python tools/conversation-stance.py --heads [--since ...]  # whom each face points at, per moment, as text (no sheets)
  python tools/conversation-stance.py --audit [--since ...]  # per scene: looks at the player against looks at Shepard's body

"You" is where the player's eyes are drawn (the record's `player.eyes`: the held place moved by the host's render-side view
offset, 0.43 m behind and 0.18 m below it with the shipped settings, and the head's tracking); records written before
2026-11-12 19:00 have only the held place, the game's camera.

The player is the blue triangle (the view's wedge, the walking boundary dashed round where the conversation began); the owner
orange, the squad green, others purple, Shepard's own body grey when the view stands elsewhere; a line from each body is its
facing, a red ring the speaker, a yellow one the one spoken to, a dotted line whom it looks at, a dashed arrow a walk under way
(x its end), a small green ring a squadmate's held place; dark grey dots are walls at chest height, tan ones what stands at knee
height (desks, railings, crates). Prints each sheet's path.
"""
import datetime
import glob
import json
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

PANEL = 560
HEAD = 64
MARGIN = 10
COLS = 3

BG = (250, 250, 247)
GRID = (232, 232, 226)
GRID5 = (210, 210, 202)
WALL = (70, 70, 72)
LOW = (196, 160, 110)
PLAYER = (40, 110, 220)
OWNER = (230, 120, 20)
SQUAD = (30, 150, 70)
OTHER = (130, 70, 170)
SHEPARD = (150, 150, 150)
SPEAKER = (220, 30, 30)
LISTENER = (235, 190, 0)
FACE = (0, 170, 210)
AIM = (230, 120, 160)
TEXT = (30, 30, 30)
MUTED = (110, 110, 110)


def font(size, bold=False):
    names = ['segoeuib.ttf' if bold else 'segoeui.ttf', 'arialbd.ttf' if bold else 'arial.ttf']
    for n in names:
        p = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts', n)
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default(size=size)


F_SMALL = font(12)
F_NAME = font(13, True)
F_HEAD = font(15, True)
F_TITLE = font(18, True)


class View:
    """World (Unreal units, x forward, y right, z up; left-handed) to a panel's pixels, the player's facing up the page."""

    def __init__(self, player, yaw_deg, scale, origin):
        self.p = player
        a = math.radians(yaw_deg)
        self.f = (math.cos(a), math.sin(a))
        self.r = (-math.sin(a), math.cos(a))  # Unreal's right of a forward (x, y) on the floor
        self.s = scale  # pixels per unit
        self.o = origin

    def __call__(self, w):
        dx, dy = w[0] - self.p[0], w[1] - self.p[1]
        right = dx * self.r[0] + dy * self.r[1]
        fwd = dx * self.f[0] + dy * self.f[1]
        return (self.o[0] + right * self.s, self.o[1] - fwd * self.s)

    def direction(self, yaw_deg):
        a = math.radians(yaw_deg)
        d = (math.cos(a), math.sin(a))
        return (d[0] * self.r[0] + d[1] * self.r[1], -(d[0] * self.f[0] + d[1] * self.f[1]))


def dashed_line(d, a, b, fill, width=1, dash=6, gap=4):
    length = math.dist(a, b)
    if length < 1:
        return
    ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
    t = 0.0
    while t < length:
        e = min(t + dash, length)
        d.line([(a[0] + ux * t, a[1] + uy * t), (a[0] + ux * e, a[1] + uy * e)], fill=fill, width=width)
        t = e + gap


def dashed_circle(d, c, r, fill, width=1, segments=72):
    for i in range(0, segments, 2):
        a0, a1 = 2 * math.pi * i / segments, 2 * math.pi * (i + 1) / segments
        d.line([(c[0] + r * math.cos(a0), c[1] + r * math.sin(a0)), (c[0] + r * math.cos(a1), c[1] + r * math.sin(a1))], fill=fill, width=width)


def arrow(d, a, b, fill, width=2, dashed=False):
    (dashed_line if dashed else lambda dd, aa, bb, fill, width: dd.line([aa, bb], fill=fill, width=width))(d, a, b, fill=fill, width=width)
    length = math.dist(a, b)
    if length < 4:
        return
    ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
    s = 8
    d.polygon([b, (b[0] - ux * s - uy * s * 0.5, b[1] - uy * s + ux * s * 0.5), (b[0] - ux * s + uy * s * 0.5, b[1] - uy * s - ux * s * 0.5)], fill=fill)


BODY_AWAY_M = 0.3  # Shepard's head bone this far (flat) from the player's drawn eyes: a look at him is no longer at the player


def body_gap(st):
    """How far (m, flat) Shepard's head bone stands from the player's eyes: the record's own measure, else (older records:
    the held place) from the people's list."""
    gap = st['player'].get('bodyFromEyesM')
    if gap is not None:
        return gap
    shep = next((q for q in st['people'] if q.get('shepard') and q.get('head')), None)
    return math.dist(shep['head'][:2], st['player']['at'][:2]) / 100 if shep else None


def looks_at(p, st):
    """Whom a face points at: the game's look-at target when the face points at where it aims (within 20 degrees), the
    player within 20, Shepard's body away from the player within 25 (and nearer it than the player), else None. (Two people
    standing close together are told apart by the game's target, not by the angle alone: Garrus looking at Girard beside
    Shepard's body, 2026-11-12.) A look at Shepard is a look at the player while his head bone is with the player's eyes."""
    face = p.get('face') or {}
    if not face or p.get('hidden') or p.get('shepard') or not p.get('head'):
        return None
    gap = body_gap(st)
    away = gap is not None and gap >= (BODY_AWAY_M if st['player'].get('eyes') else 1.0)
    look = p.get('gameLook') or {}
    aim, target = look.get('aim'), look.get('target')
    if aim and target and math.dist(aim[:2], p['head'][:2]) > 10:
        a = math.degrees(math.atan2(aim[1] - p['head'][1], aim[0] - p['head'][0]))
        if abs((face['yaw'] - a + 180) % 360 - 180) < 20:
            if target == 'you' or target.startswith('BioLookAtTarget'):
                return 'you'
            if target == 'Shepard':
                return "Shepard's body" if away else 'you'
            return target
    you, shep = face.get('toYouDeg'), face.get('toShepardDeg')
    if you is not None and you < 20:
        return 'you'
    if away and shep is not None and shep < 25 and (you is None or shep < you):
        return "Shepard's body"
    return None


def choose_scale(st):
    """Pixels per unit: 40 px a metre unless someone within 8 m would fall off the panel (further ones are pinned to its edge)."""
    upm = st.get('unitsPerMeter', 100)
    player = st['player'].get('eyes') or st['player']['at']  # the drawn eyes (older records: the held place)
    yaw = st['player']['yaw']
    probe = View(player, yaw, 1.0, (0, 0))
    worst = 0.0
    for p in st['people']:
        if math.dist(p['at'][:2], player[:2]) > 8 * upm:
            continue
        x, y = probe(p['at'])
        # the player at (PANEL/2, 0.62 PANEL): room ahead 0.62, behind 0.38, each side 0.5 (less a margin)
        need = max(abs(x) / (0.5 * PANEL - 40), (-y) / (0.62 * PANEL - 40) if y < 0 else y / (0.38 * PANEL - 40))
        worst = max(worst, need)
    scale = 40.0 / upm
    if worst > 0 and 1.0 / worst < scale:
        scale = max(1.0 / worst, 22.0 / upm)
    return scale


def wrap(text, width_px, f, d):
    words, lines, line = text.split(), [], ''
    for w in words:
        t = (line + ' ' + w).strip()
        if d.textlength(t, font=f) > width_px and line:
            lines.append(line)
            line = w
        else:
            line = t
    if line:
        lines.append(line)
    return lines


def draw_panel(st):
    img = Image.new('RGB', (PANEL, PANEL + HEAD), BG)
    d = ImageDraw.Draw(img)
    upm = st.get('unitsPerMeter', 100)
    player = st['player'].get('eyes') or st['player']['at']  # the drawn eyes (older records: the held place)
    yaw = st['player']['yaw']
    scale = choose_scale(st)
    origin = (PANEL / 2, HEAD + PANEL * 0.62)
    v = View(player, yaw, scale, origin)
    m = scale * upm  # pixels a metre

    # The grid, a metre apart, in the world's own axes (so a turn of the player shows as a turned grid).
    reach = PANEL / m + 2
    cx, cy = player[0], player[1]
    for i in range(-int(reach), int(reach) + 1):
        for axis in (0, 1):
            col = GRID5 if i % 5 == 0 else GRID
            if axis == 0:
                a = v((math.floor(cx / upm) * upm + i * upm, cy - reach * upm))
                b = v((math.floor(cx / upm) * upm + i * upm, cy + reach * upm))
            else:
                a = v((cx - reach * upm, math.floor(cy / upm) * upm + i * upm))
                b = v((cx + reach * upm, math.floor(cy / upm) * upm + i * upm))
            d.line([a, b], fill=col, width=1)

    # Walls and furniture: each scan's hits, neighbours joined when they lie close.
    walls = st.get('walls') or {}
    frm = walls.get('from')
    for scan in walls.get('scans', []):
        units = scan['units']
        col = WALL if scan['heightM'] >= 1.0 else LOW
        n = len(units)
        pts = []
        for i, u in enumerate(units):
            if u < 0:
                pts.append(None)
                continue
            a = 2 * math.pi * i / n
            pts.append(v((frm[0] + math.cos(a) * u, frm[1] + math.sin(a) * u)))
        for i, p in enumerate(pts):
            if p is None:
                continue
            q = pts[(i + 1) % n]
            if q is not None and math.dist(p, q) < 0.7 * m:
                d.line([p, q], fill=col, width=3 if col == WALL else 2)
            else:
                d.ellipse([p[0] - 1.5, p[1] - 1.5, p[0] + 1.5, p[1] + 1.5], fill=col)

    # The walking boundary round where the conversation began, and the conversation's circle.
    b = st['player'].get('boundary')
    if b and st['player'].get('boundaryM', 0) > 0:
        dashed_circle(d, v(b), st['player']['boundaryM'] * m, (120, 160, 230), width=1)
    circle = st.get('circle')
    if circle:
        c = v(circle['centre'])
        r = circle['radius'] * scale
        d.ellipse([c[0] - r, c[1] - r, c[0] + r, c[1] + r], outline=(80, 170, 170), width=1)

    # The player's view: a 90-degree wedge out to 3 m.
    pv = v(player)
    for side in (-45, 45):
        e = v((player[0] + math.cos(math.radians(yaw + side)) * 3 * upm, player[1] + math.sin(math.radians(yaw + side)) * 3 * upm))
        d.line([pv, e], fill=(170, 195, 240), width=1)

    # Everyone else standing near (not of the conversation): small dots with a facing tick.
    for o in st.get('others', []):
        q = v(o['at'])
        if not (0 <= q[0] <= PANEL and HEAD <= q[1] <= HEAD + PANEL):
            continue
        d.ellipse([q[0] - 4, q[1] - 4, q[0] + 4, q[1] + 4], fill=(190, 170, 215))
        ox, oy = v.direction(o['yaw'])
        d.line([q, (q[0] + ox * 9, q[1] + oy * 9)], fill=(150, 120, 190), width=2)

    people = st.get('people', [])
    by_name = {p['name']: p for p in people}
    # Gazes first (under the bodies).
    for p in people:
        t = by_name.get(p.get('looksAt') or '')
        if t and t is not p and not p.get('hidden'):
            # Whoever looks at Shepard looks at the player, wherever his body was left.
            to = player if t.get('shepard') else t['at']
            dashed_line(d, v(p['at']), v(to), (160, 160, 160), width=1, dash=2, gap=3)
    for p in people:
        at = v(p['at'])
        start = p.get('start')
        if start and not p.get('shepard') and math.dist(start[:2], p['at'][:2]) > 0.3 * upm:
            s0 = v(start)
            d.ellipse([s0[0] - 3, s0[1] - 3, s0[0] + 3, s0[1] + 3], outline=MUTED)
            dashed_line(d, s0, at, (190, 190, 190), width=1, dash=3, gap=3)
        walk = p.get('walk')
        if walk:
            to = v(walk['to'])
            arrow(d, at, to, (60, 60, 60), width=2, dashed=True)
            d.line([to[0] - 5, to[1] - 5, to[0] + 5, to[1] + 5], fill=(60, 60, 60), width=2)
            d.line([to[0] - 5, to[1] + 5, to[0] + 5, to[1] - 5], fill=(60, 60, 60), width=2)
        if p.get('held'):
            h = v(p['held'])
            d.ellipse([h[0] - 6, h[1] - 6, h[0] + 6, h[1] + 6], outline=SQUAD, width=1)
    for p in people:
        at = v(p['at'])
        far = None
        lo, hi = (8, HEAD + 8), (PANEL - 8, HEAD + PANEL - 8)
        if not (lo[0] <= at[0] <= hi[0] and lo[1] <= at[1] <= hi[1]):
            # Off the panel: on its edge along the line from the player, with its distance.
            far = math.dist(p['at'][:2], player[:2]) / upm
            dx, dy = at[0] - pv[0], at[1] - pv[1]
            t = 1.0
            for lim, c, dc in ((lo[0], pv[0], dx), (hi[0], pv[0], dx), (lo[1], pv[1], dy), (hi[1], pv[1], dy)):
                if dc != 0:
                    k = (lim - c) / dc
                    if 0 < k < t:
                        t = k
            at = (pv[0] + dx * t, pv[1] + dy * t)
        r = max(6, 0.22 * m)
        if p.get('shepard'):
            col = SHEPARD
        elif p.get('owner'):
            col = OWNER
        elif p.get('squad'):
            col = SQUAD
        else:
            col = OTHER
        if p.get('shepard') and math.dist(p['at'][:2], player[:2]) < 0.6 * upm:
            continue  # Shepard's body under the player: the triangle says it
        if p.get('hidden'):
            dashed_circle(d, at, r, col, width=2, segments=24)
        else:
            d.ellipse([at[0] - r, at[1] - r, at[0] + r, at[1] + r], fill=col)
        role = p.get('role')
        if role == 'speaker':
            d.ellipse([at[0] - r - 4, at[1] - r - 4, at[0] + r + 4, at[1] + r + 4], outline=SPEAKER, width=3)
        elif role == 'listener':
            d.ellipse([at[0] - r - 4, at[1] - r - 4, at[0] + r + 4, at[1] + r + 4], outline=LISTENER, width=3)
        dx, dy = v.direction(p['yaw'])
        tip = (at[0] + dx * (r + 0.35 * m), at[1] + dy * (r + 0.35 * m))
        d.line([at, tip], fill=col if not p.get('hidden') else MUTED, width=3)
        # Where the face points (cyan) and where the game's look-at aims (pink, dotted): with Shepard's body left on a mark
        # away from the player, a head the game turns to Shepard looks past the player.
        face = p.get('face') or {}
        if face and not p.get('hidden') and not p.get('shepard') and p.get('head'):
            hp = v(p['head'])
            fx, fy = v.direction(face['yaw'])
            d.line([hp, (hp[0] + fx * 0.9 * m, hp[1] + fy * 0.9 * m)], fill=FACE, width=2)
        aim = (p.get('gameLook') or {}).get('aim')
        if aim and not p.get('hidden') and not p.get('shepard') and p.get('head') and math.dist(aim[:2], p['head'][:2]) > 10:
            dashed_line(d, v(p['head']), v(aim), AIM, width=1, dash=3, gap=3)
        label = p['name'] + (' (hidden)' if p.get('hidden') else '') + ('' if p.get('walks', True) else ' (stays)')
        if far is not None:
            label += f' ({far:.1f} m)'
        whom = looks_at(p, st)
        if whom:
            label += f' - looks at {whom}'
        tw = d.textlength(label, font=F_NAME)
        tx = at[0] + r + 5 if at[0] + r + 5 + tw < PANEL - 4 else at[0] - r - 5 - tw
        d.text((tx, at[1] - 8), label, fill=TEXT, font=F_NAME)

    # Where the scene wants Shepard (a staged scene's new composition): walked to by the simulated stick, never put there.
    wants = st['player'].get('sceneWants')
    if wants:
        q = v(wants)
        d.line([q[0] - 7, q[1] - 7, q[0] + 7, q[1] + 7], fill=PLAYER, width=3)
        d.line([q[0] - 7, q[1] + 7, q[0] + 7, q[1] - 7], fill=PLAYER, width=3)
        d.text((q[0] + 9, q[1] - 6), "the scene's place for you" + (' (walking there)' if st['player'].get('steering') else ''),
               fill=PLAYER, font=F_SMALL)

    # The player on top.
    tri = [(pv[0], pv[1] - 13), (pv[0] - 9, pv[1] + 8), (pv[0] + 9, pv[1] + 8)]
    d.polygon(tri, fill=PLAYER)
    d.text((pv[0] + 12, pv[1] + 2), 'you', fill=PLAYER, font=F_NAME)

    # The scale bar.
    d.line([(14, HEAD + PANEL - 18), (14 + m, HEAD + PANEL - 18)], fill=TEXT, width=2)
    d.text((18 + m, HEAD + PANEL - 27), '1 m', fill=TEXT, font=F_SMALL)

    # The header: the moment and the line.
    d.rectangle([0, 0, PANEL, HEAD - 2], fill=(238, 238, 232))
    d.text((8, 4), f"{st.get('tag', '?')} at {st.get('seconds', 0):.1f} s  (entry {st.get('entry', -1)})", fill=TEXT, font=F_HEAD)
    lines = wrap(st.get('line', '') or '(no line)', PANEL - 16, F_SMALL, d)
    for i, t in enumerate(lines[:2]):
        d.text((8, 25 + 16 * i), t, fill=MUTED, font=F_SMALL)
    d.rectangle([0, 0, PANEL - 1, PANEL + HEAD - 1], outline=(200, 200, 195))
    return img


def sheet(stances):
    first = stances[0]
    n = len(stances)
    cols = min(COLS if n <= 9 else 4, n)
    rows = (n + cols - 1) // cols
    title_h = 74
    w = cols * PANEL + (cols + 1) * MARGIN
    h = title_h + rows * (PANEL + HEAD) + (rows + 1) * MARGIN
    img = Image.new('RGB', (w, h), (255, 255, 255))
    d = ImageDraw.Draw(img)
    d.text((MARGIN, 8), f"{first['conversation']}  (conversation {first['number']}; opening {first.get('opening', '-')}"
                        f"{'; staged' if first.get('staged') else ''})", fill=TEXT, font=F_TITLE)
    last = stances[-1]
    ahead = (last.get('ahead') or {})
    words = ahead.get('now', []) + ahead.get('later', [])
    d.text((MARGIN, 32), 'movement ahead at the last moment: ' + ('; '.join(words) if words else 'none'), fill=MUTED, font=F_SMALL)
    x = MARGIN
    legend = [(PLAYER, 'you'), (OWNER, 'owner'), (SQUAD, 'squad'), (OTHER, 'others'), ((190, 170, 215), 'bystanders'),
              (SHEPARD, "Shepard's body"),
              (SPEAKER, 'speaker'), (LISTENER, 'spoken to'), (FACE, 'face points'), (AIM, "the game's look aims"),
              (WALL, 'walls (chest)'), (LOW, 'low things (knee)')]
    for col, text in legend:
        d.ellipse([x, 54, x + 10, 64], fill=col)
        d.text((x + 14, 50), text, fill=TEXT, font=F_SMALL)
        x += 24 + int(d.textlength(text, font=F_SMALL))
    for i, st in enumerate(stances):
        r, c = divmod(i, cols)
        img.paste(draw_panel(st), (MARGIN + c * (PANEL + MARGIN), title_h + MARGIN + r * (PANEL + HEAD + MARGIN)))
    return img


def angle(origin, a, b):
    """Degrees between origin->a and origin->b."""
    u = [a[i] - origin[i] for i in range(3)]
    v = [b[i] - origin[i] for i in range(3)]
    nu, nv = math.hypot(*u), math.hypot(*v)
    if nu < 1e-6 or nv < 1e-6:
        return 0.0
    return math.degrees(math.acos(max(-1.0, min(1.0, sum(u[i] * v[i] for i in range(3)) / (nu * nv)))))


def print_audit(groups):
    """Per scene: how often a face meant for the player (its game look-at target Shepard or the eyes) points at the player's
    drawn eyes, and how often at Shepard's head bone left away from them (research/69 "Heads at the drawn eyes"); and where the
    game's look-at chain aims (its last link's target, a point along the look, not on the target): its angle off the drawn
    eyes from the looker's head, and how often it is nearer his head bone (by 5 degrees) than the eyes while he stands away.
    The aim does not depend on the skeleton (a hanar's head bone axis is no face: Opold)."""
    print("| Scene | Opening | Shepard's head from your eyes (m) | Looks meant for you | Face at you | Face at his body | Face elsewhere "
          "| Face off your eyes (median, deg) | Aim off your eyes (median, deg) | Aims at his body | Worst face |")
    print('|---|---|---|---|---|---|---|---|---|---|---|')
    for (day, number, name, _part), stances in sorted(groups.items(), key=lambda kv: kv[1][0]['_file']):
        stances.sort(key=lambda s: s['_file'])
        first = stances[0]
        gaps = [g for g in (body_gap(s) for s in stances) if g is not None]
        meant = at_you = at_body = other = aims_body = 0
        offs = []
        aims = []
        worst = (0, '')
        for st in stances:
            eyes = st['player'].get('eyes') or st['player']['at']
            shep = next((q['head'] for q in st['people'] if q.get('shepard') and q.get('head')), None)
            for p in st['people']:
                f = p.get('face')
                if p.get('shepard') or p.get('hidden') or not f or not p.get('head'):
                    continue
                target = (p.get('gameLook') or {}).get('target') or ''
                if not (target in ('Shepard', 'you') or target.startswith('BioLookAtTarget')):
                    continue
                meant += 1
                aim = (p.get('gameLook') or {}).get('aim')
                if aim and math.dist(aim, p['head']) > 10:
                    off_eyes = angle(p['head'], aim, eyes)
                    aims.append(off_eyes)
                    if shep and (body_gap(st) or 0) >= BODY_AWAY_M and angle(p['head'], aim, shep) + 5 < off_eyes:
                        aims_body += 1
                whom = looks_at(p, st)
                if whom == 'you':
                    at_you += 1
                elif whom == "Shepard's body":
                    at_body += 1
                else:
                    other += 1
                if f.get('toYouDeg') is not None:
                    offs.append(f['toYouDeg'])
                    if f['toYouDeg'] > worst[0]:
                        worst = (f['toYouDeg'], f"{p['name']} {f['toYouDeg']} deg at {st.get('seconds', 0):.0f} s ({st.get('tag', '?')})")
        offs.sort()
        aims.sort()
        median = offs[len(offs) // 2] if offs else None
        aim_median = f"{aims[len(aims) // 2]:.0f}" if aims else '-'
        opening = stances[-1].get('opening', '-') + (', staged' if first.get('staged') else '') + \
            (', the view apart' if any(s['player'].get('detached') for s in stances) else '') + \
            (', turned' if any(s['player'].get('turnedDeg') for s in stances) else '')
        print(f"| {name.split('.')[-1]} ({number}) | {opening} | {min(gaps, default=0):.2f}-{max(gaps, default=0):.2f} | {meant} | "
              f"{at_you} | {at_body} | {other} | {median if median is not None else '-'} | {aim_median} | {aims_body} | {worst[1] or '-'} |")
    return 0


def main(argv):
    folder = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'MEVR', 'stances')
    out = folder
    since = None
    files = []
    heads = '--heads' in argv
    audit = '--audit' in argv
    argv = [a for a in argv if a not in ('--heads', '--audit')]
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == '--since':
            since = datetime.datetime.fromisoformat(argv[i + 1])
            i += 2
            continue
        if a == '--out':
            out = argv[i + 1]
            i += 2
            continue
        if os.path.isdir(a):
            files += glob.glob(os.path.join(a, '*.json'))
        else:
            files.append(a)
        i += 1
    if not files:
        files = glob.glob(os.path.join(folder, '*.json'))
        if since is None:
            since = datetime.datetime.now() - datetime.timedelta(hours=1)
    if since is not None:
        files = [f for f in files if datetime.datetime.fromtimestamp(os.path.getmtime(f)) >= since]
    if not files:
        print('no stances to draw (mevr convo stance, or worldStances = 1)')
        return 1
    groups = {}
    for f in sorted(files):
        try:
            with open(f, encoding='utf-8') as fh:
                st = json.load(fh)
        except (OSError, ValueError) as e:
            print(f'skipped {f}: {e}')
            continue
        st['_file'] = f
        # A moment's first record may come before its matinee is named: moments go by their number alone.
        name = 'moment' if st.get('opening') == 'moment' else st['conversation']
        groups.setdefault((os.path.basename(f)[:10], st['number'], name), []).append(st)
    # The numbers start again with each game (a moment's at 900001): a group's records more than two minutes apart are two runs.
    split = {}
    for key, stances in groups.items():
        stances.sort(key=lambda s: s['_file'])
        part, last = 0, None
        for st in stances:
            stamp = datetime.datetime.strptime(os.path.basename(st['_file'])[:23], '%Y-%m-%d_%H-%M-%S-%f')
            if last is not None and (stamp - last).total_seconds() > 120:
                part += 1
            last = stamp
            split.setdefault(key + (part,), []).append(st)
    groups = {(k[0], k[1], stances[-1]['conversation'], k[3]): stances for k, stances in split.items()}
    if audit:
        return print_audit(groups)
    if heads:
        for (day, number, name, _part), stances in sorted(groups.items(), key=lambda kv: kv[1][0]['_file']):
            stances.sort(key=lambda s: s['_file'])
            first = stances[0]
            gaps = [g for g in (body_gap(s) for s in stances) if g is not None]
            print(f"{name.split('.')[-1]} ({number}; opening {stances[-1].get('opening', '-')}{'; staged' if first.get('staged') else ''}; "
                  f"Shepard's head {min(gaps, default=0):.2f}-{max(gaps, default=0):.2f} m from your "
                  f"{'eyes' if first['player'].get('eyes') else 'view (the held place)'}"
                  f"{'; his looks moved to your eyes' if any(s['player'].get('eyesTarget') for s in stances) else ''})")
            for st in stances:
                cells = []
                for p in st['people']:
                    if p.get('shepard') or p.get('hidden') or not p.get('face'):
                        continue
                    f = p['face']
                    cells.append(f"{p['name']}: {looks_at(p, st) or 'elsewhere'} ({f.get('toYouDeg')} deg off you"
                                 f"{', %s off his body' % f['toShepardDeg'] if f.get('toShepardDeg') is not None else ''})")
                gap = body_gap(st)
                print(f"  {st.get('tag', '?')} at {st.get('seconds', 0):.1f} s"
                      f"{' (his head %.2f m from your eyes)' % gap if gap is not None else ''}: " + '; '.join(cells))
        return 0
    os.makedirs(out, exist_ok=True)
    for (day, number, name, _part), stances in sorted(groups.items(), key=lambda kv: kv[1][0]['_file']):
        stances.sort(key=lambda s: (s['_file']))
        if len(stances) > 40:
            # A long scene: the start, every dip and walk, and an even spread of the rest, 40 panels at most.
            keep = [s for s in stances if s.get('tag') in ('begin', 'after-dip', 'opening', 'settled', 'end')]
            rest = [s for s in stances if s not in keep]
            step = max(1, len(rest) // max(1, 40 - len(keep)))
            stances = sorted(keep + rest[::step], key=lambda s: s['_file'])
        stamp = os.path.basename(stances[0]['_file'])[:23]
        path = os.path.join(out, f"{stamp}-{number}-{name.split('.')[-1]}.png")
        sheet(stances).save(path)
        print(f"{path}  ({len(stances)} moments: {', '.join(s.get('tag', '?') for s in stances)})")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

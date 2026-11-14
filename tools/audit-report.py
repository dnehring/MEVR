# SPDX-License-Identifier: MIT
"""What an audit run measured, and what it did not (research/69 "Seeing the body in the audit" and "Broken runs the audit must
catch", 2026-11-13). Reads a session's audit-report.jsonl (tools/conversation-from-save.ps1 -Report: one record per scene) and
the eye snapshots its scenes asked for (%LOCALAPPDATA%\\MEVR\\snapshots, tagged a-<scene>-<seconds>), and prints per scene:

  - the hands replayed; how long arm IK drove the body; how long Shepard's drawn hands were read, inside the view and off the
    controllers (the live audit's totals, game/ConversationWorld StepAudit);
  - the snapshots: how many came, in how many both drawn hands were in view and within 0.3 m of the controllers;
  - the scene's own expectations, and the live audit's rules that tripped;
  - what was NOT measured: the body without arm IK driving it, a scene never shown in the world, an expectation the scene never
    reached, snapshots that never came.

A broken run (a record with `mustFind`) is judged: caught when one of its rules tripped (`expect`: an expectation failed;
`not-measured`: the body was not measured), else BLIND. contact-sheet.png in the session folder puts each scene's snapshots in
a row (the left eye), framed green when both drawn hands are in view and on the controllers, red when not, grey when unmeasured.

    python tools/audit-report.py <session folder> [--snapshots <folder>] [--no-sheet]

Exit code 1 when a broken run was not caught, a scene's body was not measured, or an expectation failed.
"""
import datetime
import glob
import json
import os
import sys

OFF_M = 0.3  # a drawn wrist this far from its controller's grip: the body is not where the player is (the live rule's)
# The live rules that fail a run whatever the baseline (research/69 "Obvious bugs, measured", 2026-11-13): what the player would
# see at once. The stance records' baseline judges the rest (looks, faces, places).
FAIL_RULES = ('eyes-off-body', 'hands-off-controllers', 'body-third-person', 'body-not-drawn', 'person-not-drawn', 'near-plane',
              'moved-by-pick', 'turn-ignored', 'black-panel-on-foot', 'dark-view')
# Pictures (each snapshot in a scene): the left eye one flat colour (a black or white frame), the HUD panel a quarter opaque
# black (the script's black screen as a square ahead of the player, item 21), opaque pixels in the wheel band's outer columns
# (a reply cut off, "us." left floating, 2026-11-11). Measured on good snapshots of 2026-11-12/13: eyes far from flat, the HUD
# panel 0% opaque outside the UI, 0 edge pixels on five wheels.
EYE_FLAT_STD = 2.0
HUD_BLACK_FRACTION = 0.25
WHEEL_EDGE_PIXELS = 8


def load_records(session):
    path = os.path.join(session, 'audit-report.jsonl')
    if not os.path.exists(path):
        return []
    out = []
    with open(path, encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if line:
                out.append(json.loads(line))
    return out


def as_list(v):
    if v is None:
        return []
    return v if isinstance(v, list) else [v]


def shot_key(record):
    key = record.get('key') or record.get('title') or ''
    return ''.join(c for c in key if c.isalnum())


def load_snapshots(folder, since):
    """{tag: snapshot json} for snapshots taken since `since` (a datetime) whose tag starts with a-."""
    out = {}
    for path in glob.glob(os.path.join(folder, '*.json')):
        name = os.path.basename(path)
        try:
            stamp = datetime.datetime.strptime(name[:23], '%Y-%m-%d_%H-%M-%S-%f')
        except ValueError:
            continue
        if stamp < since:
            continue
        try:
            with open(path, encoding='utf-8') as f:
                d = json.load(f)
        except (OSError, ValueError):
            continue
        tag = d.get('tag') or ''
        if tag.startswith('a-'):
            d['_dir'] = folder
            out.setdefault(tag, d)
    return out


def snapshot_hands(d):
    """(verdict, text): 'good' both drawn hands in view and on the controllers, 'bad', 'none' (not measured), or 'skip': no
    scene held the player's view (the game's own camera after a conversation ended: the hearing's forced end, 2026-11-13)."""
    cw = (d.get('mod') or {}).get('conversationWorld') or {}
    if not (cw.get('on') and cw.get('world')):
        return 'skip', 'not in a scene'
    hands = ((d.get('mod') or {}).get('armIk') or {}).get('hands')
    if not hands:
        return 'none', 'hands not measured'
    parts, good = [], True
    for side in ('left', 'right'):
        h = hands.get(side)
        if not h:
            parts.append(f'{side[0].upper()} -')
            good = False
            continue
        ok = h.get('inView') and h.get('fromGripM', 9) < OFF_M
        good = good and ok
        parts.append(f"{side[0].upper()} {h.get('fromGripM', 0):.2f}{'' if h.get('inView') else ' out'}")
    return ('good' if good else 'bad'), ' '.join(parts)


def picture_checks(d):
    """What a snapshot's own pictures show (research/69 "Obvious bugs, measured"): [(check, text)]."""
    try:
        from PIL import Image
    except ImportError:
        return []
    out = []
    folder, stamp = d['_dir'], d.get('stamp') or ''
    fading = any('fade' in (layer.get('name') or '') for layer in as_list(d.get('layers')) if isinstance(layer, dict))
    left = next((e for e in d.get('eyes', []) if e.get('eye') == 'left'), None)
    path = os.path.join(folder, left['file']) if left and left.get('file') else None
    try:
        if path and os.path.exists(path) and not fading:
            px = Image.open(path).convert('L').resize((64, 64)).tobytes()
            mean = sum(px) / len(px)
            std = (sum((p - mean) ** 2 for p in px) / len(px)) ** 0.5
            if std < EYE_FLAT_STD:
                out.append(('eye-flat', f'the left eye one flat colour (level {mean:.0f}, spread {std:.1f})'))
        hud = os.path.join(folder, f'{stamp}-hud.png')
        if stamp and os.path.exists(hud):
            b = Image.open(hud).convert('RGBA').resize((128, 128), Image.NEAREST).tobytes()
            n = len(b) // 4
            black = sum(1 for i in range(n) if b[4 * i + 3] > 200 and max(b[4 * i], b[4 * i + 1], b[4 * i + 2]) < 16)
            if black / n > HUD_BLACK_FRACTION:
                out.append(('hud-black', f'{100 * black / n:.0f}% of the HUD panel opaque black (a black square ahead of the player)'))
        wheel = os.path.join(folder, f'{stamp}-wheel.png')
        if stamp and os.path.exists(wheel):
            a = Image.open(wheel).convert('RGBA').getchannel('A')
            w, h = a.size
            edge = sum(1 for x in list(range(3)) + list(range(w - 3, w)) for y in range(h) if a.getpixel((x, y)) > 128)
            if edge > WHEEL_EDGE_PIXELS:
                out.append(('wheel-cut', f"{edge} opaque pixels in the wheel band's outer columns: a reply cut off at its edge"))
    except OSError:
        pass
    return out


def totals(record):
    """The live audit's totals over the scene's conversations shown in the world (None: none was)."""
    keys = ('sampledS', 'armDrivenS', 'handsSampledS', 'handsInViewS', 'handsOffS', 'thirdPersonS', 'bodyCheckedS', 'bodyUndrawnS',
            'sightCheckedS', 'personUndrawnS', 'nearOverS', 'picksHeld', 'turnsTested')
    peaks = ('handsOffMaxM', 'movedByPickM', 'turnMaxDeg', 'nearMax')
    t = {k: 0.0 for k in keys + peaks}
    t['names'] = []
    world = False
    for s in as_list(record.get('summaries')):
        a = s.get('audit')
        if not s.get('world') or not isinstance(a, dict):
            continue
        world = True
        t['names'].append(s.get('name', '?'))
        for k in keys:
            t[k] += float(a.get(k) or 0.0)
        for k in peaks:
            t[k] = max(t[k], float(a.get(k) or 0.0))
    return t if world else None


def judge(record, shots):
    """One scene: lines to print, and {notMeasured, expectFailed, tripped, caught}."""
    lines, missing = [], []
    hands = record.get('hands') or ''
    lines.append(f"{record.get('key') or record.get('title')} (hands: {hands or 'none replayed'})")
    t = totals(record)
    body_measured = False
    if t is None:
        missing.append('no conversation was shown in the world: the live rules never ran')
    else:
        body_measured = t['armDrivenS'] > 0 and t['handsSampledS'] > 0
        view = 100.0 * t['handsInViewS'] / t['handsSampledS'] if t['handsSampledS'] else 0.0
        lines.append(f"  body: arm IK drove it {t['armDrivenS']:.1f} of {t['sampledS']:.1f} s sampled; the drawn hands read "
                     f"{t['handsSampledS']:.1f} s: both in view {view:.0f}%, off the controllers {t['handsOffS']:.1f} s "
                     f"(at most {t['handsOffMaxM']:.2f} m); third person {t['thirdPersonS']:.1f} s")
        if t['armDrivenS'] <= 0:
            missing.append('the body: arm IK never drove it (no tracked hands?): eyes-off-body, body-third-person and '
                           'hands-off-controllers could not fire')
        elif t['handsSampledS'] <= 0:
            missing.append("the body: Shepard's drawn hands were never read against the controllers")
        lines.append(f"  drawn: the body in view {t['bodyCheckedS']:.1f} s, not drawn {t['bodyUndrawnS']:.1f} s; someone in sight "
                     f"{t['sightCheckedS']:.1f} s, not drawn {t['personUndrawnS']:.1f} s; near plane at most {t['nearMax']:.0f} units")
        if t['armDrivenS'] > 0 and t['bodyCheckedS'] <= 0:
            missing.append('whether the body was drawn: no drawn wrist inside the view (body-not-drawn could not fire)')
        if t['sightCheckedS'] <= 0:
            missing.append('whether the people were drawn: nobody of the scene in view and in sight (person-not-drawn could not fire)')
        tests = as_list(record.get('stickTests'))
        if tests:
            lines.append(f"  the stick: {'; '.join(tests)}; replies picked with it out {int(t['picksHeld'])} (the eyes moved at most "
                         f"{t['movedByPickM']:.2f} m before it was let go); right-stick holds {int(t['turnsTested'])} (turned at most "
                         f"{t['turnMaxDeg']:.0f} deg)")
            if any('picked' in x for x in tests) and t['picksHeld'] <= 0:
                missing.append('a reply picked with the stick out: the live audit saw none (moved-by-pick could not fire)')
            if any('right stick' in x for x in tests) and t['turnsTested'] <= 0:
                missing.append('the right stick held: the live audit saw no hold (turn-ignored could not fire)')
    seen = record.get('sceneSeen')
    start = record.get('startEntry')
    played = ', '.join(f"{c.get('name')} from entry {c.get('firstEntry')}" for c in as_list(record.get('conversations'))) or 'none'
    lines.append(f"  conversations played: {played}")
    wrong = []
    if isinstance(seen, dict) and seen.get('met') is False:
        wrong.append(f"its own conversation ({seen.get('want')}) never played: the save tested another scene")
    if isinstance(start, dict) and start.get('met') is False:
        wrong.append(f"it began at entry {start.get('got')}, not {start.get('want')}: another part of the scene (the save's plot)")
    for w in wrong:
        lines.append(f'  WRONG SCENE: {w}')
    # What the player would have noticed (tools/conversation-session.py's judge over the scene's log): a flash (a screen or panel
    # shown for a few frames and taken away: Opold's shop, 2026-11-13) fails the run; the rest is listed.
    noticed = [str(n) for n in as_list(record.get('noticed'))]
    flashes = [n for n in noticed if n.startswith('flash')]
    if noticed:
        lines.append('  noticed: ' + '; '.join(n[:110] for n in noticed[:6]))
    asked = as_list(record.get('snapshots'))
    came = [(s, shots.get(s.get('tag'))) for s in asked]
    got = [(s, d) for s, d in came if d is not None]
    verdicts = [(s, *snapshot_hands(d)) for s, d in got]
    good = sum(1 for _, v, _ in verdicts if v == 'good')
    bad = [(s, text) for s, v, text in verdicts if v == 'bad']
    none = sum(1 for _, v, _ in verdicts if v == 'none')
    skipped = sum(1 for _, v, _ in verdicts if v == 'skip')
    if asked:
        line = f"  snapshots: {len(got)} of {len(asked)} came; both drawn hands in view and on the controllers in {good}"
        if bad:
            line += f"; not in {len(bad)} (" + ', '.join(f"{s.get('at')} s: {text}" for s, text in bad[:4]) + ')'
        if none:
            line += f"; hands not measured in {none}"
        if skipped:
            line += f"; {skipped} outside the scene (not judged)"
        lines.append(line)
        if len(got) < len(asked):
            missing.append(f'{len(asked) - len(got)} snapshots never came')
    else:
        missing.append('no snapshot was asked for')
    pictures = []
    for (s, d), (_, v, _) in zip(got, verdicts):
        if v != 'skip':
            pictures += [(s, check, text) for check, text in picture_checks(d)]
    if got:
        lines.append('  pictures: ' + ('; '.join(f"{s.get('at')} s: {text}" for s, _, text in pictures[:6]) if pictures else
                                         f"nothing wrong in {sum(1 for _, v, _ in verdicts if v != 'skip')} (eye not flat, no black panel, wheel whole)"))
    expects = as_list(record.get('expectations'))
    failed = [e for e in expects if e.get('met') is False]
    if expects:
        met = sum(1 for e in expects if e.get('met') is True)
        lines.append(f"  expectations: {met} of {len(expects)} met" +
                     ''.join(f"; FAILED at {e.get('at')} s: {e.get('line')}" for e in failed))
        for e in expects:
            if e.get('met') is None:
                missing.append(f"an expectation was never reached: {e.get('line')}")
    tripped = as_list(record.get('tripped'))
    rules = sorted({r.get('rule') for r in tripped if r.get('rule')})
    lines.append('  rules tripped: ' + ('; '.join(f"{r.get('rule')} ({r.get('detail')})" for r in tripped) if tripped else 'none'))
    if not record.get('ok', True):
        missing.append('the scene did not play as asked (see the run log)')
    for m in missing:
        lines.append(f'  NOT MEASURED: {m}')
    checks = sorted({check for _, check, _ in pictures})
    out = {'notMeasured': not body_measured, 'expectFailed': bool(failed), 'tripped': rules, 'caught': None,
           'failRules': [r for r in rules if r in FAIL_RULES], 'pictures': checks, 'wrongScene': wrong + [f'a flash: {f}' for f in flashes]}
    must = as_list(record.get('mustFind'))
    if must:
        hits = [r for r in must if r in rules or r in checks]
        if 'wrong-scene' in must and wrong:
            hits.append('wrong-scene')
        if 'expect' in must and failed:
            hits.append('expect')
        if 'not-measured' in must and not body_measured:
            hits.append('not-measured')
        out['caught'] = bool(hits)
        lines.append(f"  BROKEN RUN ({record.get('breaks') or '?'}): "
                     + (f"caught by {', '.join(hits)}" if hits else f"BLIND: none of {', '.join(must)}"))
    return lines, out, verdicts


def sheet(session, rows):
    """contact-sheet.png: a row of left-eye thumbnails per scene, framed by the hands' verdict."""
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        print('(no contact sheet: Pillow is not installed)')
        return None
    tw, th, label_w, pad, wrap = 240, 245, 230, 6, 8   # a scene's snapshots wrap after 8 a row
    per_row = min(wrap, max([len(v) for _, v in rows] + [1]))
    lines = [max(1, -(-len(v) // wrap)) for _, v in rows]
    width = label_w + per_row * (tw + pad) + pad
    height = sum(n * (th + 34) for n in lines) + pad
    img = Image.new('RGB', (width, max(height, 60)), (24, 24, 28))
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.load_default(size=13)
    except TypeError:
        font = ImageFont.load_default()
    colours = {'good': (60, 200, 90), 'bad': (230, 60, 50), 'none': (130, 130, 130), 'skip': (70, 90, 140)}
    y = pad
    for title, verdicts in rows:
        draw.text((pad, y + 4), title, fill=(230, 230, 230), font=font)
        x = label_w
        for i, (s, verdict, text, d) in enumerate(verdicts):
            if i and i % wrap == 0:
                x, y = label_w, y + th + 34
            left = next((e for e in d.get('eyes', []) if e.get('eye') == 'left'), None)
            path = os.path.join(d['_dir'], left['file']) if left and left.get('file') else None
            if path and os.path.exists(path):
                try:
                    thumb = Image.open(path).convert('RGB')
                    thumb.thumbnail((tw - 8, th - 8))
                    img.paste(thumb, (x + 4, y + 4))
                except OSError:
                    pass
            draw.rectangle([x, y, x + tw - 1, y + th - 1], outline=colours[verdict], width=4)
            draw.text((x + 4, y + th + 2), f"{s.get('at')} s  {text}", fill=colours[verdict], font=font)
            x += tw + pad
        y += th + 34
    out = os.path.join(session, 'contact-sheet.png')
    img.save(out)
    return out


def main(argv):
    args = [a for a in argv[1:] if not a.startswith('--')]
    if not args:
        print(__doc__)
        return 2
    session = args[0]
    snaps = argv[argv.index('--snapshots') + 1] if '--snapshots' in argv else \
        os.path.join(os.environ.get('LOCALAPPDATA', ''), 'MEVR', 'snapshots')
    records = load_records(session)
    if not records:
        print(f'no audit-report.jsonl in {session}: nothing to judge')
        return 1
    since = min(datetime.datetime.strptime(r['started'], '%Y-%m-%dT%H:%M:%S') for r in records) - datetime.timedelta(seconds=5)
    shots = load_snapshots(snaps, since)
    print(f'##### what each scene measured, and what it did not ({session})')
    rows, problems, broken = [], [], []
    for record in records:
        lines, out, verdicts = judge(record, shots)
        for line in lines:
            print(line)
        name = record.get('key') or record.get('title') or '?'
        if out['caught'] is not None:
            broken.append((name, record, out))
        else:
            if out['notMeasured']:
                problems.append(f'{name}: the body was not measured')
            if out['expectFailed']:
                problems.append(f'{name}: an expectation failed')
            for r in out['failRules']:
                problems.append(f'{name}: {r} (a live rule the player would see at once)')
            for c in out['pictures']:
                problems.append(f'{name}: {c} (in a snapshot)')
            for w in out['wrongScene']:
                problems.append(f'{name}: {w}')
        title = name if len(name) <= 30 else name[:29] + '~'
        rows.append((title, [(s, v, text, shots[s['tag']]) for s, v, text in verdicts]))
    if broken:
        print('##### broken runs (each must be caught)')
        for name, record, out in broken:
            print(f"{name}, {record.get('breaks') or '?'}: "
                  + (f"caught ({', '.join(r for r in as_list(record.get('mustFind')))})" if out['caught']
                     else 'BLIND: the audit passed a broken run'))
            if not out['caught']:
                problems.append(f'{name}: a broken run passed clean')
    if '--no-sheet' not in argv:
        path = sheet(session, rows)
        if path:
            print(f'contact sheet: {path}')
    if problems:
        print('##### problems')
        for p in problems:
            print(f'  {p}')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

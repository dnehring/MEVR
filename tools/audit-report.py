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
    """(verdict, text): 'good' both drawn hands in view and on the controllers, 'bad', or 'none' (not measured)."""
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


def totals(record):
    """The live audit's totals over the scene's conversations shown in the world (None: none was)."""
    keys = ('sampledS', 'armDrivenS', 'handsSampledS', 'handsInViewS', 'handsOffS', 'thirdPersonS')
    t = {k: 0.0 for k in keys}
    t['handsOffMaxM'] = 0.0
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
        t['handsOffMaxM'] = max(t['handsOffMaxM'], float(a.get('handsOffMaxM') or 0.0))
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
    asked = as_list(record.get('snapshots'))
    came = [(s, shots.get(s.get('tag'))) for s in asked]
    got = [(s, d) for s, d in came if d is not None]
    verdicts = [(s, *snapshot_hands(d)) for s, d in got]
    good = sum(1 for _, v, _ in verdicts if v == 'good')
    bad = [(s, text) for s, v, text in verdicts if v == 'bad']
    none = sum(1 for _, v, _ in verdicts if v == 'none')
    if asked:
        line = f"  snapshots: {len(got)} of {len(asked)} came; both drawn hands in view and on the controllers in {good}"
        if bad:
            line += f"; not in {len(bad)} (" + ', '.join(f"{s.get('at')} s: {text}" for s, text in bad[:4]) + ')'
        if none:
            line += f"; hands not measured in {none}"
        lines.append(line)
        if len(got) < len(asked):
            missing.append(f'{len(asked) - len(got)} snapshots never came')
    else:
        missing.append('no snapshot was asked for')
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
    out = {'notMeasured': not body_measured, 'expectFailed': bool(failed), 'tripped': rules, 'caught': None}
    must = as_list(record.get('mustFind'))
    if must:
        hits = [r for r in must if r in rules]
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
    tw, th, label_w, pad = 240, 245, 230, 6
    per_row = max([len(v) for _, v in rows] + [1])
    width = label_w + per_row * (tw + pad) + pad
    height = sum(th + 34 for _ in rows) + pad
    img = Image.new('RGB', (width, max(height, 60)), (24, 24, 28))
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.load_default(size=13)
    except TypeError:
        font = ImageFont.load_default()
    colours = {'good': (60, 200, 90), 'bad': (230, 60, 50), 'none': (130, 130, 130)}
    y = pad
    for title, verdicts in rows:
        draw.text((pad, y + 4), title, fill=(230, 230, 230), font=font)
        x = label_w
        for s, verdict, text, d in verdicts:
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

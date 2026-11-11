# SPDX-License-Identifier: MIT
"""The LE1 saves on this machine and the dialogue tests each one serves (research/69 "The saves and their tests").

  python tools/save-dictionary.py [--saves <Save\\ME1>] [--scan <scan.jsonl>] [--list <scenes.json>@<first slot> ...]
         [--near 40] [--out <folder>]

For every .pcsav under the Save folder: its career, level, place and when it was saved, and what it is for.

- A validation save (written by `conversation-sweep.py saves`) stands Shepard a few metres in front of one scene's owner or
  trigger, facing it, on that scene's level. That place identifies the scene: matched on the level within half a metre of
  where the generator puts the player for some scene of the scan, the scene is named with how it starts and the command that
  plays it from the save. Several scenes can share one owner or trigger (Anderson starts both the walk to the Council and the
  Spectre ceremony): the scene list the save was written from (a --list and its first slot: by default the headset sample
  from 80 and the moments sample from 90) decides, and the others at the same place are named as well.
- Any other save (a career's own) lists the scenes of the scan whose owner or trigger stands within --near metres on its
  level: what can be tested from it.

Writes save-dictionary.json, save-dictionary.md (a table) and save-dictionary-list.json (the validation saves' scenes in slot
order, for `tools/conversation-from-save.ps1 -All -List <it> -FirstSlot <first> -Only <slot>`) to --out, by default
%LOCALAPPDATA%\\MEVR. The saves are the player's own data: none of this goes into the repository.
"""
import argparse
import collections
import datetime
import importlib.util
import json
import math
import os
import re
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))


def load_module(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(TOOLS, file))
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


le1 = load_module('le1_save', 'le1-save.py')
sweep = load_module('conversation_sweep', 'conversation-sweep.py')
cs = sweep.cs  # tools/conversation-scan.py, as the sweep loads it

APPROACHES_M = (4.0, 1.5, 2.0, 3.0, 5.0, 6.0)  # the generator's default first, then the ones lists have asked for


def flat(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def scenes_of(scan_path, lists):
    """Every scene the generator can stand a save in front of: conversation starts with a placed owner or an event's
    originator, and the moments with a place. Each: name, level, the place it is stood at (owner/originator) and its facing,
    how it starts. A list's own place for a scene ("at", "yaw", "approachM") wins."""
    scenes = []
    for e in cs.catalogue(scan_path, None, quiet=True):
        via = e.get('via') or {}
        if e.get('ownerAt'):
            at, yaw, what = e['ownerAt'], e.get('ownerYaw') or 0, e.get('ownerClass') or 'the owner'
        elif via.get('originatorAt'):
            at, yaw, what = via['originatorAt'], via.get('originatorYaw') or 0, via.get('originatorClass') or "the event's originator"
        else:
            continue
        event = via.get('eventClass')
        scenes.append({'scene': e['conversation'], 'level': e['map'], 'at': at, 'yaw': yaw, 'before': what, 'event': event,
                       'action': e['actionTail'], 'actionClass': 'BioSeqAct_StartConversation'})
    for mp, rows in sweep.moment_scenes(scan_path).items():
        for s in rows:
            if s.get('ownerAt'):
                scenes.append({'scene': s['conversation'], 'level': mp, 'at': s['ownerAt'], 'yaw': s.get('ownerYaw') or 0,
                               'before': s.get('ownerClass') or "the event's originator", 'event': s.get('event'),
                               'action': s['actionTail'], 'actionClass': s.get('actionClass')})
    overrides = {}
    for path, _ in lists:
        for item in json.load(open(path, encoding='utf-8')).get('scenes', []):
            overrides[item['scene']] = item
    for s in scenes:
        o = overrides.get(s['scene'])
        if o and o.get('at'):
            s['at'], s['yaw'], s['before'] = o['at'], o.get('yaw') or 0, "the list's place"
        s['approaches'] = ((float(o['approachM']),) if o and o.get('approachM') else ()) + APPROACHES_M
        s['title'] = (o or {}).get('title')
    return scenes


def starts(event):
    if not event:
        return '?'
    if 'OnPlayerActivate' in event:
        return 'when you use it'
    if 'Touch' in event:
        return 'when you walk in (a trigger)'
    if 'LevelLoaded' in event:
        return 'by itself when its area loads'
    if 'RemoteEvent' in event:
        return "by another part of the level's script"
    if 'Death' in event:
        return 'when someone dies'
    return 'by the level script'


def read_save(path):
    s = le1.Save.load(path)
    d = s.data
    loc, ts = d['location'], d['timestamp']
    seconds = ts['seconds_since_midnight']
    try:
        saved = datetime.datetime(ts['year'], ts['month'], ts['day'], seconds // 3600, seconds // 60 % 60).isoformat(timespec='minutes')
    except ValueError:
        saved = None
    return {
        'career': str(d['character_id']), 'name': str(d['player']['first_name']), 'level': str(d['base_level_name'] or d['map_name']),
        'map': str(d['map_name']), 'place': [round(loc['x'], 1), round(loc['y'], 1), round(loc['z'], 1)],
        'facing': round(le1.angle_deg(d['rotation']['yaw']), 1), 'saved': saved,
        'playedMin': d['seconds_played'] // 60, 'squad': [h['tag'] for h in d['squad']],
        'export': d.get('no_export') is None,  # a character export (Char\): no world
    }


def identify(info, scenes, near_m, listed=None):
    """(the scene this save was written for, or None; the other scenes at the same place; the scenes near it on its level).
    `listed`: the scene the save's slot has in the list it was written from, which decides between scenes at one place."""
    if info['export']:
        return None, [], []
    level = info['level'].lower()
    here = info['place']
    fits, nearby = [], []
    for s in scenes:
        if (s['level'] or '').lower() != level:
            continue
        for approach in s['approaches']:
            place, face = sweep.stand_before(s['at'], s['yaw'], approach)
            off = flat(here, place)
            turn = abs((info['facing'] - face + 180.0) % 360.0 - 180.0)
            if off < 50.0 and turn < 2.0:
                fits.append((s, off, approach))
                break
        d = flat(here, s['at'])
        if d <= near_m * 100.0:
            nearby.append((d, s))
    nearby.sort(key=lambda p: p[0])
    seen, near = set(), []
    for d, s in nearby:
        if s['scene'] not in seen:
            seen.add(s['scene'])
            near.append({'scene': s['scene'], 'm': round(d / 100.0, 1), 'starts': starts(s.get('event'))})
    fits.sort(key=lambda f: f[1])
    best = next((f for f in fits if listed and f[0]['scene'] == listed), fits[0] if fits else None)
    also = sorted({f[0]['scene'] for f in fits} - ({best[0]['scene']} if best else set()))
    return best, also, near


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--saves', default=os.path.join(os.environ.get('USERPROFILE', '.'), 'Documents', 'BioWare', 'Mass Effect Legendary Edition', 'Save', 'ME1'))
    ap.add_argument('--scan', default=os.path.join(os.environ.get('LOCALAPPDATA', '.'), 'MEVR', 'sweep', 'scan.jsonl'))
    ap.add_argument('--list', action='append', default=[os.path.join(TOOLS, 'conversation-headset-sample.json') + '@80',
                                                        os.path.join(TOOLS, 'conversation-moments-sample.json') + '@90'])
    ap.add_argument('--near', type=float, default=40.0)
    ap.add_argument('--out', default=os.path.join(os.environ.get('LOCALAPPDATA', '.'), 'MEVR'))
    args = ap.parse_args(argv)
    lists = []
    for item in args.list:
        path, _, first = item.rpartition('@') if '@' in os.path.basename(item) else (item, '', '')
        if path and os.path.exists(path):
            lists.append((path, int(first) if first else None))
    scenes = scenes_of(args.scan, lists)
    by_slot = {}  # slot -> the scene a list puts there
    for path, first in lists:
        if first is not None:
            for i, item in enumerate(json.load(open(path, encoding='utf-8')).get('scenes', [])):
                by_slot.setdefault(first + i, item['scene'])
    manifests = {}
    for name in os.listdir(os.path.join(os.environ.get('LOCALAPPDATA', '.'), 'MEVR')):
        if name.startswith('validation-saves') and name.endswith('.json'):
            m = json.load(open(os.path.join(os.environ['LOCALAPPDATA'], 'MEVR', name), encoding='utf-8'))
            for f in m.get('files', []):
                manifests[os.path.normcase(os.path.abspath(f))] = (name, m.get('base'))
    entries = {}
    for root, _, files in os.walk(args.saves):
        for f in sorted(files):
            if not f.lower().endswith('.pcsav'):
                continue
            path = os.path.join(root, f)
            rel = os.path.relpath(path, args.saves)
            try:
                info = read_save(path)
            except Exception as e:  # a profile, a damaged file: named, not read
                entries[rel] = {'file': rel, 'role': 'not a game save', 'detail': f'{type(e).__name__}: {e}'}
                continue
            slot = re.match(r'.+_(\d+)$', os.path.splitext(f)[0])
            best, also, near = identify(info, scenes, args.near, by_slot.get(int(slot.group(1))) if slot else None)
            entry = dict(info, file=rel, slot=int(slot.group(1)) if slot else None)
            manifest = manifests.get(os.path.normcase(os.path.abspath(path)))
            if best:
                s, off, approach = best
                entry.update({
                    'role': 'validation save', 'scene': s['scene'], 'title': s.get('title'), 'starts': starts(s.get('event')),
                    'before': s['before'], 'standM': approach, 'offM': round(off / 100.0, 2), 'action': s['action'],
                    'actionClass': s['actionClass'], 'alsoHere': also, 'written': manifest[0] if manifest else None,
                    'from': os.path.basename(manifest[1]) if manifest and manifest[1] else None,
                })
            elif info['export']:
                entry['role'] = 'character export (no world: a new game or ME2 import)'
            else:
                entry['role'] = 'career save' + (' (the base of validation saves)' if any(
                    m[1] and os.path.normcase(os.path.abspath(m[1])) == os.path.normcase(os.path.abspath(path)) for m in manifests.values()) else '')
            entry['near'] = near[:6]
            entries[rel] = entry
    # The validation saves' scenes in slot order per career (gaps empty): a list conversation-from-save.ps1 -All can take.
    by_career = collections.defaultdict(dict)
    for e in entries.values():
        if e.get('role') == 'validation save' and e.get('slot') is not None:
            by_career[e['career']][e['slot']] = e
    play_lists = {}
    for career, slots in by_career.items():
        first, last = min(slots), max(slots)
        play_lists[career] = {'firstSlot': first, 'scenes': [
            {'scene': slots[i]['scene'], 'title': slots[i].get('title') or slots[i]['scene']} if i in slots else {'scene': '', 'title': '(no save)'}
            for i in range(first, last + 1)]}
    for e in entries.values():
        if e.get('role') == 'validation save' and e.get('slot') is not None:
            pl = play_lists[e['career']]
            e['play'] = (f"tools/conversation-from-save.ps1 -All -Career {e['career']} -List %LOCALAPPDATA%\\MEVR\\save-dictionary-list-{e['career']}.json "
                         f"-FirstSlot {pl['firstSlot']} -Only {e['slot']}")
        elif e.get('role', '').startswith('career save') and e.get('near'):
            e['play'] = (f"tools/conversation-launch.ps1 {e['near'][0]['scene']} -Save {os.path.splitext(os.path.basename(e['file']))[0]} -Load menu"
                         " (writes a copy at the scene; another career's save: copy it into the loaded career first, research/69 \"Eden Prime's train\")")
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'save-dictionary.json'), 'w', encoding='utf-8') as fh:
        json.dump({'generated': datetime.datetime.now().isoformat(timespec='seconds'), 'scan': args.scan, 'saves': entries}, fh, indent=1)
    for career, pl in play_lists.items():
        with open(os.path.join(args.out, f'save-dictionary-list-{career}.json'), 'w', encoding='utf-8') as fh:
            json.dump(pl, fh, indent=1)
    lines = ['# The saves and their dialogue tests', '',
             f"Generated {datetime.datetime.now():%Y-%m-%d %H:%M} by tools/save-dictionary.py from {args.saves} and the scan {os.path.basename(args.scan)}.", '',
             '| Save | Career, level | Role | Scene | Starts | Near (m) | Play |', '|---|---|---|---|---|---|---|']
    for rel in sorted(entries, key=lambda r: (entries[r].get('career') or '~', entries[r].get('slot') if entries[r].get('slot') is not None else -1, r)):
        e = entries[rel]
        near = ', '.join(f"{n['scene']} {n['m']}" for n in e.get('near', [])[:4]) or '-'
        scene = f"{e.get('title') or ''} (`{e['scene']}`)" if e.get('scene') else '-'
        if e.get('alsoHere'):
            scene += '; the same place starts ' + ', '.join(f"`{a}`" for a in e['alsoHere'])
        lines.append(f"| {rel} | {e.get('career', '-')} ({e.get('name', '')}), {e.get('level', '-')}, saved {e.get('saved') or '?'} | {e['role']} | "
                     f"{scene} | {e.get('starts', '-')} | {near} | `{e['play']}` |" if e.get('play') else
                     f"| {rel} | {e.get('career', '-')} ({e.get('name', '')}), {e.get('level', '-')}, saved {e.get('saved') or '?'} | {e['role']} | "
                     f"{scene} | {e.get('starts', '-')} | {near} | - |")
    with open(os.path.join(args.out, 'save-dictionary.md'), 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(lines) + '\n')
    validation = sum(1 for e in entries.values() if e.get('role') == 'validation save')
    print(f"{len(entries)} saves: {validation} validation saves identified, {len(entries) - validation} others -> "
          f"{os.path.join(args.out, 'save-dictionary.md')} (and .json, and {len(play_lists)} slot lists)")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

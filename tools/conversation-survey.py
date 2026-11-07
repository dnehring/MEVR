# SPDX-License-Identifier: MIT
"""Summarize conversation surveys (tools/conversation-survey.ps1, research/59 "The survey").

Reads one or more %LOCALAPPDATA%\\MEVR\\conversation-survey-*.jsonl files (all of them with --all, the newest without arguments),
keeps one record per conversation (the longest played), sorts each into a kind and prints a markdown table plus the counts:

  python tools/conversation-survey.py            the newest survey
  python tools/conversation-survey.py --all      every survey on this machine
  python tools/conversation-survey.py a.jsonl b.jsonl

Kinds (from the mod's `convo: summary` numbers):
  remote      one shot, on Shepard, the whole conversation: a call or transmission (nobody to see in the room)
  framed      a set piece: a third or more of the shots from a camera past 8 m from Shepard's eyes, or half or more of
              them on something his eyes can't see (a boss's entrance, a wide shot): first person loses the composition
  moves       the game put someone on a mark (kept > 0) or something long was let through (letGo > 0)
  player      the player's place moved with Shepard's head bone (reanchors > 0)
  staged      everyone stays where they are, the director cuts between them (shots > 1)
"""
import glob
import json
import os
import sys


def load(paths):
    best = {}
    tried = 0
    for path in paths:
        with open(path, encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                r = json.loads(line)
                tried += 1
                s = r.get('summary')
                if not s:
                    continue
                name = s['name'].split('.')[-1]
                if name not in best or s['seconds'] > best[name][1]['seconds']:
                    best[name] = (r, s)
    return best, tried


def kind(s):
    if s['shots'] <= 1 and s['onShepard'] == s['shots']:
        return 'remote'
    shots = max(s['shots'], 1)
    if s.get('farShots', 0) * 3 >= shots or (s['shots'] >= 4 and s['notSeeable'] * 2 >= shots + s['onShepard']):
        return 'framed'
    if s['kept'] > 0 or s['letGo'] > 0:
        return 'moves'
    if s['reanchors'] > 0:
        return 'player'
    return 'staged'


def main(argv):
    base = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'MEVR')
    files = sorted(glob.glob(os.path.join(base, 'conversation-survey-*.jsonl')))
    if '--all' in argv:
        paths = files
    elif len(argv) > 1:
        paths = argv[1:]
    else:
        paths = files[-1:]
    if not paths:
        print('no survey files')
        return 1
    best, tried = load(paths)
    rows = sorted(best.values(), key=lambda rs: (rs[0]['map'], kind(rs[1]), rs[1]['name']))
    print('| Map | Conversation | Kind | s | Lines | Shots | On Shepard | Cutaway | Not seeable | Far shots | Camera max m | Kept | Drawn | Let go | Walks | Player moved | Max move m |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    counts = {}
    for r, s in rows:
        k = kind(s)
        counts[k] = counts.get(k, 0) + 1
        print(f"| {r['map']} | {s['name'].split('.')[-1]} | {k} | {s['seconds']:.0f} | {s['lines']} | {s['shots']} | {s['onShepard']} | "
              f"{s['cutaway']} | {s['notSeeable']} | {s.get('farShots', '?')} | {s.get('cameraFarM', '?')} | {s['kept']} | {s['drawn']} | {s['letGo']} | {s.get('walks', 0)} | {s['reanchors']} | {s['maxMoveM']:.2f} |")
    print()
    print(f"{len(rows)} conversations from {tried} actions tried ({', '.join(f'{k} {n}' for k, n in sorted(counts.items()))}); "
          f"drawn jumps in total: {sum(s['drawn'] for _, s in rows)}")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

# SPDX-License-Identifier: MIT
"""The sequels' scenes with no place for the player, generated from the conversation scan (research/le2-conversations-in-world.md
section 7; the owner, 2026-11-19: "the 3D screen, from a table generated from the scan, presence-based", never typed by hand).

A conversation the game plays in its conversation mode (a starting entry that is not ambient) none of whose lines' matinees act
on the player (no track whose m_nmFindActor is "Player", no group named for him: Player, Shepard, PC_, HMM_PC_..., HMF_PC_...)
has no place for the player: Shepard is not in the scene (the docking officer's radio talk on ME3, the Illusive Man's calls, a
squadmate's remark on the comm). Its conversation goes to the 3D screen (cw::NoPlaceForPlayer).

    python -I tools/conversation-presence.py --game LE2 <scan.jsonl> [<dlc scan.jsonl> ...] [--write]

The scans come from `tools/conversation-scan` (`--game LE2` or `--game LE3`, the plain scan: CookedPCConsole and each DLC's),
which records each line group's actors (`actors`) since lane FPC2. Their outputs are game data: keep them out of the repository.
Without --write it prints the counts and the table; with --write it rewrites that game's block of
src/core/vr/ConversationPresence.inc (names only: our table, no game text).
"""
import collections
import json
import os
import re
import sys

PLAYER_GROUP = re.compile(r'^(player|shepard|pc_?|hm[mf]_pc.*)$', re.I)
INC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'core', 'vr', 'ConversationPresence.inc')


def key(name):
    """cw::ConversationKey: after the last dot, lower case, without one _dlg or _d."""
    k = name[name.rfind('.') + 1:].lower()
    for suffix in ('_dlg', '_d'):
        if len(k) > len(suffix) and k.endswith(suffix):
            return k[:-len(suffix)]
    return k


def load(paths):
    convs = {}
    lines = collections.defaultdict(list)
    for path in paths:
        for line in open(path, encoding='utf-8'):
            r = json.loads(line)
            kind = r.get('kind')
            if kind == 'conversation':
                convs.setdefault(r['name'], []).append(r)
            elif kind == 'cinematic' and '.Node_Data_Sequence.' in r['path']:
                prefix = r['path'].split('.Node_Data_Sequence.')[0] + '.Node_Data_Sequence'
                lines[(r['package'], prefix)].append(r.get('matinees') or [])
    return convs, lines


def classify(convs, lines):
    out = {}
    for name, copies in convs.items():
        best = max(copies, key=lambda r: len(r.get('nodes', [])))
        nodes = best.get('nodes', [])
        starts = [i for i in best.get('starting') or [] if i < len(nodes)]
        if not starts or all(nodes[i].get('ambient') for i in starts):
            out[name] = 'ambient'  # never the conversation mode: barks
            continue
        seq = (best.get('matineeSequence') or {}).get('path')
        matinees = [m for c in copies for m in lines.get((c['package'], (c.get('matineeSequence') or {}).get('path')), [])]
        if not matinees:
            out[name] = 'unknown'  # no line matinee read: left to the run-time rules
            continue
        player = any('Player' in g.get('actors', []) or (g.get('name') and PLAYER_GROUP.match(g['name']))
                     for mats in matinees for m in mats for g in m.get('groups', []))
        out[name] = 'player' if player else 'noplayer'
    return out


def main(argv):
    if '--game' not in argv:
        sys.exit(__doc__)
    game = argv[argv.index('--game') + 1]
    if game not in ('LE2', 'LE3'):
        sys.exit('--game LE2 or LE3')
    paths = [a for a in argv if a.endswith('.jsonl')]
    convs, lines = load(paths)
    kinds = classify(convs, lines)
    counts = collections.Counter(kinds.values())
    keys = sorted({key(n) for n, k in kinds.items() if k == 'noplayer'})
    print(f'{game}: {len(convs)} conversations: {dict(counts)}; {len(keys)} without a place for the player', file=sys.stderr)
    block = [f'// {game}: tools/conversation-presence.py --game {game} (generated: {len(keys)} of the {counts["player"] + counts["noplayer"]} '
             f'conversations the game plays in its conversation mode, of {len(convs)}).']
    block += [f'    "{k}",' for k in keys]
    if '--write' not in argv:
        print('\n'.join(block))
        return 0
    text = open(INC, encoding='utf-8').read()
    begin, end = f'// BEGIN {game}\n', f'// END {game}\n'
    if begin not in text or end not in text:
        sys.exit(f'{INC}: no {game} block')
    head, rest = text.split(begin, 1)
    _, tail = rest.split(end, 1)
    open(INC, 'w', encoding='utf-8', newline='\n').write(head + begin + '\n'.join(block) + '\n' + end + tail)
    print(f'wrote {len(keys)} names into {INC}', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

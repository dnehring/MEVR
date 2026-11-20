# SPDX-License-Identifier: MIT
"""The per-game plan for every female body (tools/rig/README.md "Every female body", docs/trilogy-hair-plan.md section 12).

Reads a census (tools/rig/census.ps1: %LOCALAPPDATA%\\MEVR\\rig\\census\\<GAME>.csv, the player's game data) and writes
tools/rig/plans/<game>-females.jsonc: one step per distinct body mesh (by its object path; the version most packages hold, a
patch DLC's copy first), "place": "auto", into {MEVR}/rig/<GAME>/MEVR_Bodies_<GAME>.pcc. Prints the census summary (distinct
bodies, copies, versions, armour or full by game/Hair OutfitJiggle's rule) and, with --compare, what other games share.

  python tools/rig/females-plan.py LE3 [--compare LE1 LE2] [--list] [--write]
"""
import argparse
import collections
import csv
import os
import re
import sys

FAMILY = re.compile(r'^(HMF|ASA)_([A-Za-z0-9]+)_', re.I)
# Not bodies: hair, heads, headgear, visors, breathers, hats, the blank world body; every other family is a body: ARM (armour,
# clothes, underwear) and the sequels' character outfits (ME2 MIR, JCK, KSM, SPR...).
PARTS = {'HIR', 'HED', 'HGR', 'HGRPROP', 'VSR', 'BRT', 'BRTA', 'BRTB', 'BRTC', 'HAT', 'BLANK', 'EYE', 'LSH', 'BRW'}
ARMOUR = ('_hvy', '_med', '_lgt')               # game/Hair OutfitJiggle's armour types (on the mesh's path)
FULL = ('hmf_mir_',)                            # its fullBodies default: a bodysuit named like armour
# Rigs (lower case) never fitted as a skirt (tools/rig/Fit.cs), when the owner sees one that shouldn't be.
FIT_EXCLUDE = set()


def census_path(game):
    return os.path.join(os.environ['LOCALAPPDATA'], 'MEVR', 'rig', 'census', game + '.csv')


def read(game):
    rows = []
    with open(census_path(game), newline='') as f:
        for r in csv.DictReader(f):
            if r.get('vertices') in (None, '', 'error'):
                continue
            rows.append(r)
    return rows


def outfit(path):
    n = path.lower()
    if any(p in n for p in FULL):
        return 'full'
    return 'armour' if any(t in n for t in ARMOUR) else 'full'


def pick(copies):
    """The copy to rig: the version most packages hold; among those a patch DLC's (it overrides), else a BIOG_ package's, else
    the shortest file path."""
    by_hash = collections.Counter(c['meshHash'] for c in copies)
    best_hash = by_hash.most_common(1)[0][0]
    pool = [c for c in copies if c['meshHash'] == best_hash]

    def key(c):
        f = c['file'].replace('\\', '/').lower()
        return (0 if 'patch' in f else 1, 0 if os.path.basename(f).startswith('biog_') else 1, len(f), f)
    return sorted(pool, key=key)[0], by_hash


def memory_path(r, packages):
    """The object's path in the game's memory: a level's cooked copy names its package first (BIOG_HMF_ARM_CTH_R.CTHb.<mesh>, a
    forced export), the package's own export doesn't (CTHb.<mesh>) but lives in it."""
    first = r['path'].split('.', 1)[0].lower()
    if first in packages:
        return r['path']
    stem = os.path.splitext(os.path.basename(r['file'].replace('\\', '/')))[0]
    return stem + '.' + r['path']


def bodies(game):
    rows = read(game)
    packages = {os.path.splitext(os.path.basename(r['file'].replace('\\', '/')))[0].lower() for r in rows}
    for r in rows:
        r['inFile'] = r['path']   # the path inside its package (the rig tool's `mesh`)
        r['path'] = memory_path(r, packages)
    groups = collections.defaultdict(list)
    skipped = collections.Counter()
    for r in rows:
        m = FAMILY.match(r['name'])
        if not m or m.group(2).upper() in PARTS:
            skipped['not a body mesh'] += 1
            continue
        if r['name'].upper().endswith('_MEVR') or r['soft'] == '1':
            skipped['ours (already rigged)'] += 1
            continue
        if r['chest2'] != '1' or r['pelvis'] != '1':
            skipped['no Chest2/Pelvis'] += 1
            continue
        groups[r['path'].lower()].append(r)   # the object's full path: one mesh (two meshes may share a name: ME2 Jack's)
    return rows, groups, skipped


def rigs_of(groups):
    """One rig per distinct mesh: memory paths whose chosen copies are the same mesh (one name, identical vertices: a mesh kept
    under two groups of its package) share it. Returns {(name, hash): [paths]}."""
    rigs = collections.defaultdict(list)
    for p in sorted(groups):
        c = pick(groups[p])[0]
        rigs[(c['name'].lower(), c['meshHash'])].append(p)
    return rigs


def rig_names(rigs, groups):
    """Each rig's name in our package: <mesh>_MEVR; two meshes of one name (ME2 Jack's two outfits) get their vertex count, and
    their vertices' hash if that's the same too."""
    by_name = collections.defaultdict(list)
    for key in rigs:
        by_name[key[0]].append(key)
    out = {}
    for keys in by_name.values():
        counts = collections.Counter(pick(groups[rigs[k][0]])[0]['vertices'] for k in keys)
        for k in keys:
            c = pick(groups[rigs[k][0]])[0]
            suffix = ''
            if len(keys) > 1:
                suffix = '_' + c['vertices'] + ('_' + c['meshHash'][:6] if counts[c['vertices']] > 1 else '')
            out[k] = c['name'] + suffix + '_MEVR'
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', choices=('LE1', 'LE2', 'LE3'))
    ap.add_argument('--compare', nargs='*', default=[])
    ap.add_argument('--write', action='store_true')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    rows, groups, skipped = bodies(a.game)
    rigs = rigs_of(groups)
    keys = sorted(rigs, key=lambda k: rigs[k][0])
    chosen = {k: pick(groups[rigs[k][0]])[0] for k in keys}
    versions = [p for p in groups if len({c['meshHash'] for c in groups[p]}) > 1]
    kinds = collections.Counter(outfit(rigs[k][0]) for k in keys)
    fam = collections.Counter(FAMILY.match(chosen[k]['name']).group(0).upper().rstrip('_') for k in keys)
    names = {k[0] for k in keys}
    copies = sum(len(g) for g in groups.values())
    print(f'{a.game}: {len(rows)} HMF_/ASA_ skeletal meshes; {copies} body copies in packages, {len(groups)} object paths = '
          f'{len(keys)} distinct body meshes ({len(names)} names; {kinds["armour"]} armour, {kinds["full"]} full by the name rule); '
          f'{len(versions)} paths with two versions; skipped {dict(skipped)}')
    print('  by family:', dict(sorted(fam.items())))
    bone_sets = collections.Counter(chosen[k]['boneHash'] for k in keys)
    bones = collections.Counter(chosen[k]['bones'] for k in keys)
    print(f'  skeletons: {len(bone_sets)} bone-name sets, bone counts {dict(sorted(bones.items()))}; vertices '
          f'{min(int(chosen[k]["vertices"]) for k in keys)}..{max(int(chosen[k]["vertices"]) for k in keys)}')
    for other in a.compare:
        _, og, _ = bodies(other)
        theirs = rigs_of(og)
        their_names = {k[0] for k in theirs}
        their_counts = {(k[0], pick(og[theirs[k][0]])[0]['vertices']) for k in theirs}
        same_name = [k for k in keys if k[0] in their_names]
        same_verts = [k for k in same_name if (k[0], chosen[k]['vertices']) in their_counts]
        same_mesh = [k for k in same_name if k in theirs]
        print(f'  shared with {other}: {len(same_name)} of {len(keys)} meshes by name, {len(same_verts)} with the same vertex count, '
              f'{len(same_mesh)} with identical vertices')
    mapping = rig_names(rigs, groups)
    if a.list:
        for k in keys:
            c = chosen[k]
            print(f'  {mapping[k]:<38} {outfit(rigs[k][0]):<6} {c["vertices"]:>6} v {c["bones"]:>4} bones, '
                  f'{sum(len(groups[p]) for p in rigs[k])} copies  {" | ".join(rigs[k])} <- {c["file"]}')
    if a.write:
        out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'plans', a.game.lower() + '-females.jsonc')
        lines = [
            '// SPDX-License-Identifier: MIT',
            f'// Every female body of {a.game} (tools/rig/README.md "Every female body", docs/trilogy-hair-plan.md section 12), written by',
            f"// `python tools/rig/females-plan.py {a.game} --write` from the player's census: one step per distinct body mesh, its",
            "// breast and glute bones placed from its own surface (\"place\": \"auto\"); `standsFor`: the mesh's paths in the game's",
            "// memory (the package index MEVR_Bodies_<GAME>.json maps them to the rig). Built on the player's machine from their own",
            '// game files; the package stays local. After each step: armour (game/Hair OutfitJiggle: a quarter of the jiggle) or full,',
            "// by that rule on the mesh's path.",
            "// Every step has \"fit\": \"auto\": a skirt (a dress hanging straight down from the glutes) is fitted under them to the",
            "// game's bare body (`fitReference`, HMF_ARM_NKDa; tools/rig/Fit.cs), unless it is in FIT_EXCLUDE.",
            '{',
            f'  "out": "{{MEVR}}/rig/{a.game}/MEVR_Bodies_{a.game}.pcc",',
            '  "continueOnError": true,',
        ]
        bare = [k for k in keys if k[0] == 'hmf_arm_nkda_mdl']
        if bare:
            c = chosen[bare[0]]
            src = '{' + a.game + '}/' + c['file'].replace('\\', '/')
            lines.append(f'  "fitReference": {{ "source": "{src}", "mesh": "{c["inFile"]}" }},')
        else:
            print('no HMF_ARM_NKDa in', a.game, ': no skirt fit')
        lines.append('  "steps": [')
        steps = []
        for k in keys:
            c = chosen[k]
            src = '{' + a.game + '}/' + c['file'].replace('\\', '/')
            stands = ', '.join(f'"{p_}"' for p_ in (pick(groups[p])[0]['path'] for p in rigs[k]))
            fit = '"fit": "none"' if mapping[k].lower() in FIT_EXCLUDE else '"fit": "auto"'
            steps.append(f'    {{ "source": "{src}", "mesh": "{c["inFile"]}", "name": "{mapping[k]}", "place": "auto", {fit}, '
                         f'"standsFor": [{stands}] }}, // {outfit(rigs[k][0])}')
        if steps:
            steps[-1] = steps[-1].replace(' }, //', ' } //', 1)
        lines += steps + ['  ]', '}', '']
        with open(out, 'w', newline='\n') as f:
            f.write('\n'.join(lines))
        print('wrote', out, len(steps), 'steps')


if __name__ == '__main__':
    sys.exit(main())

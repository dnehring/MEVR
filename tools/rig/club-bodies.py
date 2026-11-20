# SPDX-License-Identifier: MIT
"""The rigs the clubs' women wear (tools/rig/README.md "Club bodies"): every rigged body the census finds in a club level's
packages that keeps all of its jiggle (game/Hair's rule: no armour type in its path, no bodyJiggle share under 1), by the
name in MEVR_Bodies_<GAME>.json. Prints them comma-separated for `mevr-rig clubs --names`; --list explains each.

  python tools/rig/club-bodies.py LE3 [--list]

The club levels are game/Hair's `clubLevels` (docs/trilogy-hair-plan.md section 12; keep the two in step). Reads the census
and the index in %LOCALAPPDATA%\\MEVR\\rig (the player's game data); writes nothing.
"""
import csv
import json
import os
import sys

CLUB_LEVELS = {
    'LE1': ['bioa_sta60_05'],
    'LE2': ['biod_omghub_2', 'biod_omghub_500denvip', 'bioa_omghub200_', 'bioa_omghub300_', 'bioa_omghub400_',
            'bioa_omghub500_', 'bioa_omghub900_'],
    'LE3': ['biod_cithub_wardsflux', 'bioa_cithub_afterlife', 'biod_omg004_9', 'bioa_omg004_900_afterlife'],
}
# game/Hair: bodyJiggle (first match wins), then fullBodies, then the armour types.
BODY_JIGGLE = [('hmf_arm_ashc', 1.0), ('asa_arm_liad', 1.0), ('asa_arm_liab', 1.0), ('_edi', 0.0), ('asa_spr_', 0.5),
               ('asa_arm_sam', 0.5), ('_ksm', 0.25), ('hmf_jck_', 1.0), ('hmf_arm_ash', 0.25), ('asa_arm_lia', 0.25),
               ('hmf_arm_crb', 0.25)]
FULL_BODIES = ['hmf_mir_']
ARMOUR = ['_hvy', '_med', '_lgt']


def share(path):
    p = path.lower()
    for part, s in BODY_JIGGLE:
        if part in p:
            return s
    if any(part in p for part in FULL_BODIES):
        return 1.0
    if any(t in p for t in ARMOUR):
        return 0.25
    return 1.0


def main():
    game = sys.argv[1].upper()
    root = os.path.join(os.environ['LOCALAPPDATA'], 'MEVR', 'rig')
    index = json.load(open(os.path.join(root, game, 'MEVR_Bodies_%s.json' % game)))['meshes']
    rows = list(csv.DictReader(open(os.path.join(root, 'census', game + '.csv'))))
    found = {}
    for r in rows:
        f = r['file'].split('\\')[-1].lower()
        if not any(p in f for p in CLUB_LEVELS[game]) or r.get('chest2') != '1' or r.get('pelvis') != '1':
            continue
        for e in index:
            if e['path'] == r['path'] or e['path'].endswith('.' + r['path']):
                found.setdefault(e['name'], [r['path'], set()])[1].add(f)
    take = sorted(n for n, (p, _) in found.items() if share(p) >= 1.0)
    if '--list' in sys.argv:
        for n, (p, files) in sorted(found.items()):
            s = share(p)
            print('%-34s %-5s %s (%d club file%s)' % (n, 'club' if s >= 1.0 else 'share %.2f, left out' % s, p,
                                                      len(files), '' if len(files) == 1 else 's'))
    else:
        print(','.join(take))


if __name__ == '__main__':
    main()

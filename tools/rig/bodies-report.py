# SPDX-License-Identifier: MIT
"""The per-body report of a females build (tools/rig/build-bodies.ps1's log, %LOCALAPPDATA%\\MEVR\\rig\\<GAME>\\MEVR_Bodies_<GAME>.log):
per body which mounds were placed, the LOD 0 vertices each region weighted, the checks and the body spheres; then the totals.

  python tools/rig/bodies-report.py LE1 [--list]
"""
import os
import re
import sys


def blocks(path):
    head, lines = None, []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('MEVR_Bodies_') and ': ' in line and not line.split(':', 1)[0].endswith('.pcc'):
                if line.split(':', 1)[0].count('.') == 1:
                    if head:
                        yield head, lines
                    head, lines = line, []
                    continue
            if head:
                lines.append(line.strip())
    if head:
        yield head, lines


def main():
    game = sys.argv[1]
    path = os.path.join(os.environ['LOCALAPPDATA'], 'MEVR', 'rig', game, f'MEVR_Bodies_{game}.log')
    rows = []
    for head, lines in blocks(path):
        name = head.split(':', 1)[0].split('.', 1)[1]
        status = 'skipped' if 'skipped' in head else 'FAILED' if 'FAILED' in head else 'built'
        mounds = []
        for l in lines:
            m = re.match(r'place: (breast|glute): (none|apex x ([-0-9.]+) .* \(smoothed z ([0-9.]+)\))', l)
            if m:
                mounds.append(m.group(1) + ('-' if m.group(2) == 'none' else f'@{float(m.group(4)):.1f}'))
        weighted = {}
        for l in lines:
            m = re.match(r'region (\w+), LOD 0: (\d+) vertices weighted', l)
            if m:
                weighted[m.group(1)] = int(m.group(2))
        checks = sum(1 for l in lines if l.startswith('check:'))
        spheres = next((int(m.group(1)) for l in lines for m in [re.match(r'body spheres: (\d+)', l)] if m), 0)
        rows.append((name, status, ' '.join(mounds), weighted, checks, spheres))
    if '--list' in sys.argv:
        for name, status, mounds, w, checks, spheres in rows:
            ws = ' '.join(f'{k[0]}{k[-4:] if k.endswith("Left") else k[-5:]}={v}' for k, v in w.items())
            print(f'{name:<40} {status:<7} {mounds:<26} {ws:<60} check {checks} spheres {spheres}')
    built = [r for r in rows if r[1] == 'built']
    both = [r for r in built if 'breast@' in r[2] and 'glute@' in r[2]]
    breast_only = [r for r in built if 'breast@' in r[2] and 'glute@' not in r[2]]
    glute_only = [r for r in built if 'glute@' in r[2] and 'breast@' not in r[2]]
    lows = [r for r in built if any(v < 20 for v in r[3].values())]
    print(f'{game}: {len(rows)} steps, {len(built)} built ({len(both)} breasts and glutes, {len(breast_only)} breasts only, '
          f'{len(glute_only)} glutes only), {sum(1 for r in rows if r[1] == "skipped")} skipped, {sum(1 for r in rows if r[1] == "FAILED")} failed; '
          f'every built body checked twice: {all(r[4] == 2 for r in built)}; regions under 20 vertices: {len(lows)}')
    if built:
        allw = [v for r in built for v in r[3].values()]
        sph = [r[5] for r in built]
        print(f'  LOD 0 vertices per region {min(allw)}..{max(allw)} (median {sorted(allw)[len(allw) // 2]}); body spheres {min(sph)}..{max(sph)}')
    for r in lows:
        print('  low:', r[0], r[3])


if __name__ == '__main__':
    sys.exit(main())

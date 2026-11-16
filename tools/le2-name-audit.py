# SPDX-License-Identifier: MIT
"""Which engine classes, functions and fields our modules resolve that ME1 has, and whether ME2 (or ME3) has each one.

  python tools/le2-name-audit.py                       # ME1's VRIK and combat modules (the default list below), against ME2
  python tools/le2-name-audit.py game/Comfort game/MainMenuRequest ...   # any modules, as paths under src/ without extension
  python tools/le2-name-audit.py --game LE3 game/ArmIk  # against ME3's names instead

It reads string literals that look like engine paths ("Pkg.Class", "Pkg.Class.Function", and FieldRef{"Pkg.Class",
"Field"}) out of the modules' .cpp/.hpp, and looks each up in LExSDKv2's generated LE1, LE2 and LE3 headers, used here
only as name lists (nothing is compiled against them). For each name the sequel lacks it prints its functions or classes
of the same short name. Names behind constants defined elsewhere are seen only if that file is in the list
(engine/GameAdapter is). It doesn't see native RVAs, bone names or behaviour: those need Ghidra and the game. Results
for the VRIK modules on 2026-11-14: research/le2-vrik-audit.md. tools/parity-ledger.py imports this file (its
functions) for every module and both sequels, per game's preprocessed build of each module.

The SDK is the LExSDKv2 submodule (external/LExSDKv2); a worktree without it checked out uses the main checkout's.
"""
import argparse
import collections
import functools
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SRC = os.path.join(ROOT, 'src')
DEFAULT_MODULES = ['game/HandAim', 'game/ArmIk', 'game/BodyFollow', 'game/BodyScale', 'game/CombatStance', 'game/Fingers',
                   'game/GunKick', 'game/Holsters', 'game/Player', 'game/Sights', 'game/SupportGrip', 'game/ViewLevel',
                   'game/Weapon', 'game/ScopeParts', 'game/scope_parts', 'game/BodyCalibration', 'engine/BonePose',
                   'engine/bone_pose', 'engine/GameAdapter', 'engine/call_function', 'engine/BodyBounds', 'game/Throwables',
                   'game/Vent', 'game/Posture']
GAMES = ('LE1', 'LE2', 'LE3')


def sdk_dir(root=ROOT):
    """LExSDKv2's generated headers (Src/LESDK), in this checkout or, from a worktree, the main checkout. None if absent."""
    own = os.path.join(root, 'external', 'LExSDKv2', 'Src', 'LESDK')
    if os.path.isdir(os.path.join(own, 'LE1')):
        return own
    try:
        common = subprocess.run(['git', '-C', root, 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None
    main = os.path.join(os.path.dirname(os.path.normpath(common)), 'external', 'LExSDKv2', 'Src', 'LESDK')
    return main if os.path.isdir(os.path.join(main, 'LE1')) else None


@functools.lru_cache(maxsize=None)
def engine_names(game, sdk):
    """(functions "Pkg.Class.Fn", classes "Pkg.Class", fields {short class: {field}}) of one game's SDK headers."""
    funcs, classes, fields = set(), set(), collections.defaultdict(set)
    d = os.path.join(sdk, game)
    for f in sorted(os.listdir(d)):
        p = os.path.join(d, f)
        with open(p, encoding='utf-8', errors='replace') as h:
            text = h.read()
        for m in re.finditer(r'^// Function ([\w]+)\.([\w]+)\.([\w]+)', text, re.M):
            funcs.add(f'{m.group(1)}.{m.group(2)}.{m.group(3)}')
            classes.add(f'{m.group(1)}.{m.group(2)}')
        for m in re.finditer(r'^// Class ([\w]+)\.([\w]+)', text, re.M):
            classes.add(f'{m.group(1)}.{m.group(2)}')
        if f.endswith('_classes.hpp'):
            cur = None
            for line in text.splitlines():
                m = re.match(r'^// Class ([\w]+)\.([\w]+)', line)
                if m:
                    cur = m.group(2)
                    continue
                m = re.match(r'^\s+[\w:<>\*\s,]+?\s\**(\w+)(\[\w*\])?\s*;\s*//\s*0x', line)
                if cur and m:
                    fields[cur].add(m.group(1))
    return frozenset(funcs), frozenset(classes), {k: frozenset(v) for k, v in fields.items()}


def names_in(text):
    """The engine-path-looking names a source text names: "Pkg.Class", "Pkg.Class.Fn" and "Pkg.Class:Field"."""
    out = []
    for m in re.finditer(r'"([A-Z][A-Za-z0-9_]*\.[A-Z][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)?)"', text):
        out.append(m.group(1))
    # fields named on a class given by its literal path: FieldRef{"Pkg.Class", "Field"} / {cls, "Field"}
    for m in re.finditer(r'FieldRef\s*\w*\s*\{\s*"([\w]+\.[\w]+)"\s*,\s*"(\w+)"', text):
        out.append(f'{m.group(1)}:{m.group(2)}')
    return out


def lookup(name, game, sdk):
    """(kind, has) for a name in one game's SDK."""
    funcs, classes, fields = engine_names(game, sdk)
    if ':' in name:
        cls, fld = name.split(':')
        return 'field', fld in fields.get(cls.split('.')[1], ())
    if name.count('.') == 2:
        return 'function', name in funcs
    return 'class', name in classes


def is_engine_name(name, sdk, games=GAMES):
    """Whether any of the games' SDKs knows the name (else it's a log tag, a file name...)."""
    return any(lookup(name, g, sdk)[1] for g in games)


def module_text(mod):
    parts = []
    for ext in ('.cpp', '.hpp'):
        p = os.path.join(SRC, mod.replace('/', os.sep) + ext)
        if os.path.isfile(p):
            with open(p, encoding='utf-8', errors='replace') as h:
                parts.append(h.read())
    return '\n'.join(parts)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', choices=('LE2', 'LE3'), default='LE2', help='the sequel to compare ME1 with (default LE2)')
    ap.add_argument('modules', nargs='*', help='paths under src/ without extension')
    args = ap.parse_args(argv)
    sdk = sdk_dir()
    if sdk is None:
        sys.exit('LExSDKv2 is not checked out (external/LExSDKv2), here or in the main checkout')
    game = args.game
    used = collections.OrderedDict()
    for mod in args.modules or DEFAULT_MODULES:
        for name in names_in(module_text(mod)):
            used.setdefault(name, set()).add(mod)
    f2, c2, _ = engine_names(game, sdk)
    rows = []
    for name, mods in used.items():
        kind, in1 = lookup(name, 'LE1', sdk)
        _, in2 = lookup(name, game, sdk)
        if not in1 and not in2:
            continue  # not an engine name ME1 or the sequel has (a log tag, a file name...)
        rows.append((in2, kind, name, sorted(mods)))
    seq = 'ME2' if game == 'LE2' else 'ME3'
    missing = [r for r in rows if not r[0]]
    print(f'{len(rows)} engine names used by these modules that LE1 or {game} has; {seq} has {len(rows) - len(missing)}, '
          f'lacks {len(missing)}\n')
    print(f'{seq} LACKS:')
    for _, kind, name, mods in sorted(missing, key=lambda r: (r[3], r[2])):
        short = name.split('.')[-1].split(':')[-1]
        alt = sorted(x for x in (f2 if kind == 'function' else c2) if x.endswith('.' + short))[:4] if kind != 'field' else []
        print(f'  {kind:8} {name:60} {",".join(m.split("/")[-1] for m in mods):30} {seq} has: {", ".join(alt) or "-"}')
    print(f'\n{seq} HAS (by module):')
    by = collections.defaultdict(list)
    for has, kind, name, mods in rows:
        if has:
            for m in mods:
                by[m].append(name)
    for m, ns in by.items():
        print(f'  {m}: {len(ns)}')


if __name__ == '__main__':
    main()

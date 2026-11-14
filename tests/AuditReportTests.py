# SPDX-License-Identifier: MIT
"""tools/audit-report.py's picture checks on made-up snapshots (research/69 "Obvious bugs, measured"): a flat eye, a black
square on the HUD panel, a wheel reply cut at the band's edge; and their clean counterparts. Skipped without Pillow."""
import importlib.util
import os
import shutil
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('audit_report', os.path.join(here, '..', 'tools', 'audit-report.py'))
ar = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ar)

try:
    from PIL import Image, ImageDraw
except ImportError:
    print('audit report tests: skipped (no Pillow)')
    sys.exit(0)

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print('FAILED:', what)


folder = tempfile.mkdtemp(prefix='mevr-audit-report-')
try:
    def snapshot(stamp, eye, hud=None, wheel=None):
        eye.save(os.path.join(folder, f'{stamp}-left.png'))
        if hud is not None:
            hud.save(os.path.join(folder, f'{stamp}-hud.png'))
        if wheel is not None:
            wheel.save(os.path.join(folder, f'{stamp}-wheel.png'))
        return {'_dir': folder, 'stamp': stamp, 'eyes': [{'eye': 'left', 'file': f'{stamp}-left.png'}], 'layers': [{'name': 'left'}]}

    def checks(d):
        return {c for c, _ in ar.picture_checks(d)}

    room = Image.new('RGB', (256, 256), (90, 80, 70))
    ImageDraw.Draw(room).rectangle([40, 60, 200, 220], fill=(200, 190, 170))
    clear_hud = Image.new('RGBA', (512, 512), (0, 0, 0, 0))
    black_hud = clear_hud.copy()
    ImageDraw.Draw(black_hud).rectangle([60, 60, 450, 450], fill=(0, 0, 0, 255))
    whole = Image.new('RGBA', (1620, 189), (0, 0, 0, 0))
    ImageDraw.Draw(whole).rectangle([300, 60, 1300, 120], fill=(255, 255, 255, 255))
    cut = whole.copy()
    ImageDraw.Draw(cut).rectangle([1500, 70, 1619, 110], fill=(255, 255, 255, 255))

    check(checks(snapshot('2026-11-13_20-00-00-000', room, clear_hud, whole)) == set(), 'a room, a clear HUD and a whole wheel: nothing')
    check('eye-flat' in checks(snapshot('2026-11-13_20-00-01-000', Image.new('RGB', (256, 256), (0, 0, 0)))), 'a black eye is eye-flat')
    check('hud-black' in checks(snapshot('2026-11-13_20-00-02-000', room, black_hud)), 'a black square on the HUD panel is hud-black')
    check('wheel-cut' in checks(snapshot('2026-11-13_20-00-03-000', room, clear_hud, cut)), 'a reply at the band edge is wheel-cut')
    faded = snapshot('2026-11-13_20-00-04-000', Image.new('RGB', (256, 256), (0, 0, 0)))
    faded['layers'].append({'name': 'transitionfade'})
    check('eye-flat' not in checks(faded), 'a black eye under the transition fade is not judged')
finally:
    shutil.rmtree(folder, ignore_errors=True)

print(f"audit report tests: {'ok' if not failures else f'{len(failures)} FAILED'}")
sys.exit(1 if failures else 0)

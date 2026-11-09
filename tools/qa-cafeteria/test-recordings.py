# SPDX-License-Identifier: MIT
"""Check generated input safety and loop geometry without a running game."""
import math
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from mevrrec import read

out = Path(sys.argv[1])
pulse = out / 'pulse.mevrrec'
turn = out / 'turn.mevrrec'
subprocess.run([sys.executable, str(HERE / 'movement.py'), str(pulse), '--seconds', '3', '--lead', '2', '--tail', '2', '--y', '.55'], check=True)
subprocess.run([sys.executable, str(HERE / 'movement.py'), str(turn), '--seconds', '30', '--look-turn'], check=True)
_, p = read(pulse)
_, t = read(turn)
assert len(p) == 630 and len(t) == 2700
assert all(f['left.stickY'] == 0 for f in p[:180] + p[450:])
assert all(abs(f['left.stickY'] - .55) < 1e-6 for f in p[180:450])
for frames in (p, t):
    assert all(a['timeS'] < b['timeS'] for a, b in zip(frames, frames[1:]))
    for f in frames:
        assert f['headValid'] and f['controllersActive']
        assert not any(f[f'{hand}.{button}'] for hand in ('left', 'right') for button in ('primary', 'secondary', 'menu', 'stickClick'))
        assert abs(math.dist(f['leftEye'][4:], f['rightEye'][4:]) - .064) < 1e-6
        assert f['leftFov'][0] < 0 < f['leftFov'][1] and f['rightFov'][0] < 0 < f['rightFov'][1]
assert all(f[f'{hand}.{axis}'] == 0 for f in t for hand in ('left', 'right') for axis in ('trigger', 'squeeze', 'stickX', 'stickY'))
assert abs(abs(sum(a * b for a, b in zip(t[0]['leftEye'][:4], t[-1]['leftEye'][:4]))) - 1) < 1e-5
print('Recording safety, neutral lead/tail, FOV, IPD and turn continuity passed.')

# SPDX-License-Identifier: MIT
"""Offline gates for recorded walks. Does not interact with a running game."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import recorded_route as route
import turn


class RecordedRouteTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.clip = self.root / 'source.mevrrec'
        turn.write(self.clip, 0, -10, 60)

    def mutate(self, frame_number, value_index, value):
        data = bytearray(self.clip.read_bytes())
        offset = route.HEADER.size + frame_number * route.FRAME.size
        values = list(route.FRAME.unpack_from(data, offset))
        values[value_index] = value
        route.FRAME.pack_into(data, offset, *values)
        self.clip.write_bytes(data)

    def test_complete_clip_and_inactive_neutral_controllers(self):
        self.mutate(0, 1, 1)  # Head valid, controllers sleeping, zero input.
        manifest = route.inspect(self.clip)
        self.assertEqual(manifest['inactiveControllerFrames'], 1)
        self.assertFalse(manifest['nativeRouteValidated'])
        self.assertFalse(manifest['recordedWorldWaypoints'])

    def test_truncation_timestamps_and_nonfinite_data(self):
        original = self.clip.read_bytes()
        for mutation in ('truncated', 'timestamp', 'nan', 'head-invalid'):
            with self.subTest(mutation=mutation):
                self.clip.write_bytes(original)
                if mutation == 'truncated':
                    self.clip.write_bytes(original[:-1])
                elif mutation == 'timestamp':
                    self.mutate(1, 0, 0)
                elif mutation == 'nan':
                    self.mutate(1, 2, float('nan'))
                else:
                    self.mutate(1, 1, 2)
                with self.assertRaises(ValueError):
                    route.inspect(self.clip)

    def test_input_state_and_neutral_finish(self):
        original = self.clip.read_bytes()
        for frame, flags, stick in ((0, 1, .7), (2699, 3, .7), (1, 3, 1.5)):
            self.clip.write_bytes(original)
            self.mutate(frame, 1, flags)
            self.mutate(frame, 27, stick)  # Left stick X, after both eye views.
            with self.assertRaises(ValueError):
                route.inspect(self.clip)

    def test_world_and_start_refusals(self):
        text = ('map BIOA_STA00, game mode 0, pawn SFXPawn_Player X (BioVehicleBase: no), other text\n'
                'loc 100 -200 300 rot 0 65530 0')
        observed = route.parse_world(text)
        expected = copy.deepcopy(observed)
        expected['yawUnreal'] = 2
        self.assertEqual(route.check_start(observed, expected)['yawDeltaUnreal'], 8)
        for altered in (text.replace('STA00', 'NOR00'), text.replace('mode 0', 'mode 7'),
                        text.replace('Base: no', 'Base: yes'), 'missing'):
            with self.assertRaises(ValueError):
                route.parse_world(altered)
        for changed in ({'positionUU': [100, -200, 400]}, {'yawUnreal': 4000}):
            with self.assertRaises(ValueError):
                route.check_start(observed, dict(expected, **changed))

    def test_package_hash_pinning(self):
        helper = Path(route.__file__)
        package = self.root / 'package'
        sha = route.inspect(self.clip)['sha256']
        def run(*args):
            return subprocess.run([sys.executable, str(helper), *map(str, args)], capture_output=True, text=True)
        self.assertNotEqual(run('prepare', self.clip, package, '--expected-sha256', '0' * 64).returncode, 0)
        self.assertFalse(package.exists())
        result = run('prepare', self.clip, package, '--expected-sha256', sha)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(run('verify', package).returncode, 0)
        self.assertNotEqual(run('prepare', self.clip, package, '--expected-sha256', sha).returncode, 0)
        manifest = json.loads((package / 'route.json').read_text())
        manifest['durationSeconds'] += 1
        (package / 'route.json').write_text(json.dumps(manifest))
        self.assertNotEqual(run('verify', package).returncode, 0)


if __name__ == '__main__':
    unittest.main()

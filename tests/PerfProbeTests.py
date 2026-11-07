# SPDX-License-Identifier: MIT
"""Synthetic probe rows: next-frame calls must not masquerade as submission or mirror work."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('perf_probe', Path(__file__).resolve().parents[1] / 'tools' / 'perf-probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class ProbeTests(unittest.TestCase):
    def test_full_frame_costs(self):
        row = {
            't_start': 0, 't_present': 3, 't_frame_prepared': 3.2, 't_frame_ready': 3.2,
            't_overlay_begin': 3.3, 't_overlay_end': 3.4,
            't_end_frame_begin': 4, 't_end_frame_end': 4.2,
            't_next_wait_begin': 4.2, 't_next_wait_end': 15,
            't_next_begin_begin': 15, 't_next_begin_end': 15.1,
            't_next_input_begin': 15.1, 't_next_input_end': 15.4,
            't_next_frame_begun_end': 15.5, 't_end': 16,
            'busy_start': 0, 'busy_present': 3, 'busy_end': 5,
            'busy_next_wait_begin': 3.8, 'busy_next_wait_end': 4.8,
            'busy_next_begin_begin': 4.8, 'busy_next_begin_end': 4.81,
            'busy_end_frame_begin': 3.7, 'busy_end_frame_end': 3.72,
        }
        m = probe.frame_metrics(row)
        self.assertAlmostEqual(m['wait_wall'], 10.8)
        self.assertAlmostEqual(m['begin_wall'], .1)
        self.assertAlmostEqual(m['pre_wait'], .2)
        self.assertAlmostEqual(m['input'], .3)
        self.assertAlmostEqual(m['copies'], .7)
        self.assertAlmostEqual(m['tail'], .5)
        self.assertAlmostEqual(m['render_work'], 3.97)
        # Bootstrap can have an own wait as well as N+1's wait. Count both; never overwrite.
        row.update(t_wait_begin=3.2, t_wait_end=3.25)
        self.assertAlmostEqual(probe.frame_metrics(row)['wait_wall'], 10.85)

    def test_old_capture_schema(self):
        row = {'t_start': 0, 't_present': 3, 't_begin_begin': 3.2, 't_begin_end': 3.3,
               't_input_begin': 3.3, 't_input_end': 3.4, 't_frame_begun_end': 3.5,
               't_end_frame_begin': 4, 't_end_frame_end': 5,
               't_wait_begin': 5, 't_wait_end': 15, 't_end': 16,
               'busy_end': 4, 'busy_wait_begin': 3.7, 'busy_wait_end': 3.8,
               'busy_begin_begin': 3, 'busy_begin_end': 3.01,
               'busy_end_frame_begin': 3.6, 'busy_end_frame_end': 3.7}
        m = probe.frame_metrics(row)
        self.assertAlmostEqual(m['pre_wait'], .2)
        self.assertAlmostEqual(m['wait_wall'], 10)
        self.assertAlmostEqual(m['copies'], .5)
        self.assertAlmostEqual(m['render_work'], 3.79)


if __name__ == '__main__':
    unittest.main()

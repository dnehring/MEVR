# SPDX-License-Identifier: MIT
"""Synthetic probe rows: next-frame calls must not masquerade as submission or mirror work."""
import importlib.util
from pathlib import Path
import unittest
import json
import tempfile

spec = importlib.util.spec_from_file_location('perf_probe', Path(__file__).resolve().parents[1] / 'tools' / 'perf-probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)
check_spec = importlib.util.spec_from_file_location('tick_check', Path(__file__).resolve().parents[1] / 'tools' / 'qa-cafeteria' / 'check-tick-trace.py')
tick_check = importlib.util.module_from_spec(check_spec)
check_spec.loader.exec_module(tick_check)


class ProbeTests(unittest.TestCase):
    def test_trace_gate_rejects_missing_or_misplaced_calls(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            meta = {'ticks':1,'tickInstrumentation':{'version':2,'ticksDropped':0,'callsDropped':0,'queuedCalls':1},
                    'files':{'ticks':'ticks.csv','calls':'calls.csv'}}
            (folder/'fixture-probe.json').write_text(json.dumps(meta))
            (folder/'ticks.csv').write_text('tick,t_begin,engine_ms,host_ms,t_hook_begin,hook_ms,t_drain_begin,drain_ms,queued_calls\n0,94,2,1,3,94,4,90,1\n')
            header = 'call,tick,object,function,t_begin,t_end,wall_ms,busy_ms,outcome\n'
            (folder/'calls.csv').write_text(header+'0,0,123,456,4,94,90,1,1\n')
            self.assertTrue(tick_check.check(folder)['passed'])
            # A call placed inside engine work cannot masquerade as the pre-engine queue stall.
            (folder/'calls.csv').write_text(header+'0,0,123,456,94,96,2,1,1\n')
            self.assertFalse(tick_check.check(folder)['passed'])
            (folder/'calls.csv').write_text(header)
            self.assertFalse(tick_check.check(folder)['passed'])

    def test_queued_stall_is_inside_hook(self):
        previous = {'t_begin': 0, 'engine_ms': 2, 'host_ms': 1, 't_hook_begin': -1, 'hook_ms': 4}
        following = {'t_begin': 94, 'engine_ms': 2, 'host_ms': 1, 't_hook_begin': 4, 'hook_ms': 93}
        self.assertEqual(probe.tick_total(following), 93)
        gap = probe.tick_gap(previous, following)
        self.assertEqual(gap['unmeasuredMs'], 1)  # the 90 ms queue work is not an outside-hook pause
        self.assertEqual(gap['scope'], 'outsideMeasuredHook')
        following['t_hook_begin'] = 94
        following['hook_ms'] = 3
        self.assertEqual(probe.tick_gap(previous, following)['unmeasuredMs'], 91)

    def test_legacy_ticks_keep_blind_spot_label(self):
        previous = {'t_begin': 0, 'engine_ms': 2, 'host_ms': 1}
        following = {'t_begin': 94, 'engine_ms': 2, 'host_ms': 1}
        self.assertEqual(probe.tick_total(following), 3)
        self.assertEqual(probe.tick_gap(previous, following),
                         {'fromMs': 3, 'toMs': 94, 'unmeasuredMs': 91, 'scope': 'outsideEngineHostOnly'})

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

# SPDX-License-Identifier: MIT
"""Offline admission, recording and per-scenario comparison checks; never launch or alter a game."""
import copy
import importlib.util
import json
import hashlib
import math
from pathlib import Path
import sys
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import scenario
import turn
import report
import fixture
from mevrrec import read

OUT = Path(sys.argv.pop(1))
OUT.mkdir(parents=True, exist_ok=True)


def ready():
    return dict(spdxLicenseIdentifier='MIT', schemaVersion=1, id='fixture-a', label='Offline fixture', game='LE1', status='ready',
                expectedMap='BIOA_ICE00', start=dict(positionUU=[1, 2, 3], yawUnreal=0),
                steps=[dict(type='walk', name='end', positionUU=[4, 5, 3])],
                prerequisites=['Prepare the private measured save.'], evidence={'nativeRouteValidated': False},
                capture=dict(seconds=90, yawDegrees=0, pitchDegrees=-10, sweepDegrees=60))


class Checks(unittest.TestCase):
    def test_catalog_and_draft_execution(self):
        for path in (HERE / 'scenarios').glob('*.json'):
            s = scenario.load(path)
            if s['status'] == 'draft': self.assertTrue(scenario.validate(s, for_run=True))
        self.assertFalse(scenario.validate(ready(), for_run=True))

    def test_malformed_routes_fail_closed(self):
        bad = [dict(steps=[]), dict(start=None), dict(expectedMap='BIOA_ICE00;quit'), dict(status='validated'),
               dict(steps=[None]), dict(steps=[dict(type='teleport', positionUU=[1, 2, 3])]),
               dict(steps=[dict(type='walk', name='bad', positionUU=[math.nan, 2, 3])]),
               dict(steps=[dict(type='pulse', yawDegrees=0, seconds=1000, stick=1)]), dict(capture={})]
        bad += [dict(prerequisites=None), dict(prerequisites='Prepare save'), dict(prerequisites=['']),
                dict(prerequisites=[123]), dict(evidence=[]), dict(evidence=None),
                dict(evidence={'sources': 'untyped source'}), dict(evidence={'nativeRouteValidated': 'yes'}),
                dict(status='validated', evidence={'routeReceipt': True})]
        for changes in bad:
            s = ready(); s.update(changes)
            self.assertTrue(scenario.validate(s, for_run=True), changes)
        for field in ('expectedMap', 'start', 'capture', 'prerequisites', 'evidence'):
            s = ready(); s['status'] = 'draft'; del s[field]
            self.assertTrue(scenario.validate(s), field)
        s = ready(); s['status'] = 'validated'; s['evidence']['routeReceipt'] = 'private route receipt reference'
        self.assertFalse(scenario.validate(s, for_run=True))

    def test_turn_neutral_continuous_stereo(self):
        path = OUT / 'turn.mevrrec'
        turn.write(path, 90, -10, 60)
        _, frames = read(path)
        self.assertEqual(len(frames), 2700)
        for f in frames:
            self.assertTrue(f['headValid'] and f['controllersActive'])
            self.assertAlmostEqual(math.dist(f['leftEye'][4:], f['rightEye'][4:]), .064, places=6)
            for hand in ('left', 'right'):
                self.assertFalse(any(f[f'{hand}.{key}'] for key in ('trigger', 'squeeze', 'stickX', 'stickY', 'primary', 'secondary', 'menu', 'stickClick')))
        dot = abs(sum(a*b for a, b in zip(frames[0]['leftEye'][:4], frames[-1]['leftEye'][:4])))
        self.assertGreater(dot, .99999)
        self.assertNotEqual(frames[0]['leftEye'], frames[675]['leftEye'])

    def write_run(self, root, name, scenario_id='fixture-a', tick_scope='wholeHook', dropped=0, version=2):
        d = root / ('hub-'+name); d.mkdir(parents=True)
        values = {
            'context.json': dict(ScenarioId=scenario_id, ScenarioStatus='ready', ScenarioSha256='scenario', ExpectedAsiSha256='binary', Preset='High',
                                 FixtureSelectedSha256='selected-save', FixtureInventorySha256='save-set'),
            'route.json': dict(routePassed=True, scenarioSha256='scenario'),
            'restored.json': dict(verifiedFiles=42),
            'machine.json': dict(cpu='fixture CPU', gpu='fixture GPU/driver', os='fixture OS', powerScheme='fixture plan'),
            'final-state.json': dict(mismatches=[], requests=[], processes=[]),
            'fixture-probe.json': dict(tickInstrumentation=dict(version=version, callsDropped=dropped, ticksDropped=0)),
            'analysis.json': [dict(source='fixture-probe.json', renderLevers=dict(sharePass=5), halcyon=dict(sfr2=True),
                resolution=[3840, 3840], accessViolations=0, gpuFramesDropped=0,
                all=dict(interval=dict(p99=8, max=12), over11ms=1, over22ms=0),
                tickSummary=dict(engineMs=dict(max=9), hostMs=dict(max=2), totalMs=dict(max=11), totalScope=tick_scope))]
        }
        for name, value in values.items(): (d/name).write_text(json.dumps(value), encoding='utf-8')
        return d

    def test_report_preserves_workload_and_scope_boundaries(self):
        root = OUT/'report'; root.mkdir()
        self.write_run(root, 'a1')
        self.write_run(root, 'b1', 'fixture-b')
        r = report.summarize(root)
        self.assertTrue(r['passed']); self.assertFalse(r['performanceAccepted']); self.assertEqual(len(r['scenarios']), 2)
        self.assertFalse(report.summarize(root, interval_limit=11)['passed'])
        self.assertFalse(report.summarize(root, min_runs=3)['passed'])
        self.write_run(root, 'a2', tick_scope='engineHostOnly', version=1)
        self.assertFalse(report.summarize(root)['passed'])
        self.write_run(root, 'c1', 'fixture-c', dropped=1)
        self.assertTrue(any('dropped' in e for e in report.summarize(root)['errors']))
        (root/'hub-incomplete').mkdir()
        self.assertTrue(any('incomplete' in e for e in report.summarize(root)['errors']))
        for scope in ('mixed', 'empty', '', 'engineHostOnly'):
            root = OUT / ('scope-' + (scope or 'blank')); root.mkdir()
            self.write_run(root, 'bad-scope', tick_scope=scope)
            self.assertFalse(report.summarize(root)['passed'], scope)

    def test_fixture_identity_is_not_map_position(self):
        profile = OUT/'private-profile'; profile.mkdir()
        selected = profile/'selected.pcsav'; selected.write_bytes(b'original story state')
        expected = hashlib.sha256(selected.read_bytes()).hexdigest()
        a = fixture.fingerprint(profile, selected, expected)
        self.assertEqual(a['selectedSha256'], expected.upper())
        self.assertIn('operator-attested', a['selectionEvidence'])
        self.assertIn('not an engine-confirmed slot', a['selectionEvidence'])
        selected.write_bytes(b'different story state at same position')
        with self.assertRaises(ValueError): fixture.fingerprint(profile, selected, expected)
        b = fixture.fingerprint(profile)
        self.assertNotEqual(a['inventorySha256'], b['inventorySha256'])
        self.assertIsNone(b['selectedSha256'])
        root = OUT/'fixture-report';root.mkdir()
        self.write_run(root, 'one'); second = self.write_run(root, 'two')
        self.assertTrue(report.summarize(root)['passed'])
        context = json.loads((second/'context.json').read_text())
        context['FixtureSelectedSha256'] = 'different-story-save'
        (second/'context.json').write_text(json.dumps(context))
        self.assertFalse(report.summarize(root)['passed'])


if __name__ == '__main__': unittest.main()

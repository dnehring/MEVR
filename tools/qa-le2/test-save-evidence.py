# SPDX-License-Identifier: MIT
"""Real temporary files and protocol frames; no live process, profile or game use."""
import copy
import json
import os
from pathlib import Path
import subprocess
import unittest
from unittest import mock
import importlib.util


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


HERE = Path(__file__).resolve().parent
e = module('save_evidence_test_subject', HERE / 'save-evidence.py')
fixtures = module('save_evidence_run_fixtures', HERE / 'test-export-state.py')
save_fixtures = module('save_evidence_save_fixtures', HERE.parents[1] / 'tests/Le2SaveTests.py')
MARKER = dict(recognized=True, recognitionPolicy='le2-pc-save-empty-row0-v1',
              uiRow=0, index=0, type=0, career='', saveName='', careerCount=0,
              careerCapacity=0, saveNameCount=0, saveNameCapacity=0, saveGameIsNull=True)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.base = fixtures.ExportTests()
        self.base.setUp()
        self.addCleanup(self.base.doCleanups)
        self.run, self.wire, self.case = self.base.run, self.base.wire, self.base.case
        self.put = self.base.put
        self.directory = self.run / 'control'
        self.bind = lambda requested: (self.directory, dict(self.wire))
        self.observation_id = self.base.token
        self.action_id = '44444444-4444-4444-8444-444444444444'
        self.save = save_fixtures.fixture()
        (self.run / 'fixture.pcsav').write_bytes(self.save)
        self.context, self.plan = self.base.context, self.base.plan
        self.career = Path(self.context['profileRoot']) / 'Save/ME2/Career'
        self.career.mkdir(parents=True)
        self.target = self.career / 'Save99.pcsav'
        self.target.write_bytes(self.save)
        (self.career / 'sidecar.txt').write_bytes(b'Existing career metadata')
        self.context.update(phase='game-bound', restorationVerified=False, fixtureTarget=str(self.target),
                            fixtureSha256=e.sha(self.save))
        self.plan['fixture'].update(sha256=e.sha(self.save), targetRelativePath='Save/ME2/Career/Save99.pcsav')
        self.plan['fixtureSelection'] = dict(schemaVersion=1, descriptor=dict(career='Career', saveName='Save99', index=99, type=0),
            fixtureSha256=e.sha(self.save), targetRelativePath=self.plan['fixture']['targetRelativePath'], exeSha256=self.wire['exe'],
            evidence=dict(runId=self.wire['run'], observationSha256='a'*64, captureSha256='b'*64, reviewedBy='Synthetic reviewer'))
        self.put('plan.json', self.plan)
        receipt = e.read(self.run / 'preparation-receipt.json')
        receipt['output_sha256'] = e.sha(self.save)
        self.put('preparation-receipt.json', receipt)
        self.context['preparationReceiptSha256'] = e.s.digest(self.run / 'preparation-receipt.json')
        self.context['planSha256'] = e.s.digest(self.run / 'plan.json')
        self.put('context.json', self.context)
        lock = Path(self.context['lockPath'])
        lock.parent.mkdir(parents=True)
        lock.write_text(str(self.run), encoding='utf-8')
        self.payload = dict(source='diagnostic-host', game='LE2', ready=False, readOnly=True,
            capabilities=dict(observeSaveFlow=True),
            gameTick='123', gameThreadId='88', sampleMonotonicMs='456',
            saveFlow=dict(schemaVersion=1, game='LE2', available=True, saveNewEntrySpent=False,
                save=dict(sameOpenedSaveFlow=True, newSaveObservationBound=True, structuralNewSaveSupported=True,
                    recordsComplete=True, observedRowCount=3, records=[dict(career='Career', saveName='Save99', index=99, type=0),
                    dict(career='Career', saveName='AutoSave', index=-1, type=2),
                    dict(career='Career', saveName='ChapterSave', index=-1, type=3)])))
        self.archive(self.observation_id, 'observe-save-flow', 1, self.payload, 'completed')
        self.capture = 'captures/review/capture.json'
        capture_dir = self.run / 'captures/review'
        capture_dir.mkdir(parents=True)
        (capture_dir / 'frame.png').write_bytes(b'SYNTHETIC IMAGE BYTES NEVER A VISUAL ACCEPTANCE')
        binding = dict(runDirectory=str(self.run), controlDirectory=str(self.directory), identity=self.wire,
                       process=self.context['gameProcess'], planSha256=self.context['planSha256'])
        self.put(self.capture, dict(schemaVersion=1, source='le2-window-capture', status='captured',
            bindingBefore=binding, bindingAfter=binding, binding=binding,
            image=dict(path=str(capture_dir / 'frame.png'), sha256=e.s.digest(capture_dir / 'frame.png'))))

    def archive(self, token, verb, seq, payload, status):
        request = dict(self.wire, kind='request', id=token, sequence=str(seq), deadline='999999',
                       verb=verb, status='pending', bytes='0', payload=b'')
        data = json.dumps(payload).encode()
        result = dict(request, kind='result', status=status, bytes=str(len(data)), payload=data)
        for name, frame in [(f'history/{token}.request', request), (f'history/{token}.result', result),
                            (f'consumed-{seq}.request', request)]:
            (self.directory / name).write_bytes(e.c.encode(frame))
        (self.directory / 'sequence.txt').write_text(str(seq), encoding='ascii')
        return request, result

    def before(self):
        return e.before(self.run, self.case, self.observation_id, self.capture, 'Synthetic UI reviewer', bind=self.bind)

    def action(self):
        payload = dict(source='diagnostic-host', game='LE2', ready=False, readOnly=False,
            capabilities=dict(nativeSaveNew=True),
            menuAction=dict(action='save-new', entered=True, returnedNormally=True, parameterFrameIntact=True,
                            saveCompleted=False, diskInventoryVerifiedByEngine=False, persistenceVerified=False))
        self.archive(self.action_id, 'save-new', 2, payload, 'dispatched')
        self.put('control/history/' + self.action_id + '.save-baseline.json', dict(schemaVersion=1,
            requestId=self.action_id, sequence=2, wireIdentity=self.wire,
            saveBaselineSha256=e.s.digest(self.run / e.BASELINE)))
        (self.career / 'ObservedNew.pcsav').write_bytes(self.save)
        return payload

    def after(self, candidate='ObservedNew.pcsav'):
        return e.after(self.run, self.action_id, candidate, 'Reviewer observed actual new filename', bind=self.bind)

    def test_complete_before_after_real_parser_and_client_preflight(self):
        original = e.inventory(self.career)
        receipt = self.before()
        self.assertEqual(receipt['inventory'], original)
        self.assertEqual(e.inventory(self.career), original)
        (self.directory / 'client.lock').write_text('caller-owned', encoding='ascii')
        self.assertEqual(e.c.verify_save_baseline(self.run, self.directory, self.wire), e.s.digest(self.run / e.BASELINE))
        self.assertEqual((self.directory / 'client.lock').read_text(), 'caller-owned')
        self.action()
        code, report = self.after()
        self.assertEqual(code, 0)
        self.assertFalse(any(report['claims'].values()))
        self.assertEqual((self.run / report['artifact']['path']).read_bytes(), self.save)
        self.assertEqual(report['action']['requestId'], self.action_id)
        self.assertEqual(e.adapter.Save((self.run / report['artifact']['path']).read_bytes()).value('player.credits'), 100)
        with self.assertRaisesRegex(ValueError, 'existing output'):
            self.after()

    def test_baseline_exclusive_and_modified_inventory_refused_by_client(self):
        self.before()
        with self.assertRaisesRegex(ValueError, 'exclusive baseline'):
            self.before()
        (self.career / 'sidecar.txt').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Career inventory changed'):
            e.c.verify_save_baseline(self.run, self.directory, self.wire)
        self.assertFalse((self.directory / 'outstanding.json').exists())

    def test_capture_identity_image_and_review_required(self):
        original = e.read(self.run / self.capture)
        for change in ('status', 'identity', 'image'):
            value = copy.deepcopy(original)
            if change == 'status': value['status'] = 'suspect-black'
            elif change == 'identity': value['bindingAfter']['identity']['lease'] = self.action_id
            else: value['image']['sha256'] = 'c' * 64
            self.put(self.capture, value)
            with self.assertRaises(ValueError): self.before()
        self.put(self.capture, original)
        with self.assertRaises(ValueError):
            e.before(self.run, self.case, self.observation_id, self.capture, ' ', bind=self.bind)

    def test_incomplete_changed_scope_or_duplicate_descriptors_refused(self):
        original = copy.deepcopy(self.payload)
        for key in ('sameOpenedSaveFlow', 'newSaveObservationBound', 'structuralNewSaveSupported', 'recordsComplete'):
            value = copy.deepcopy(original)
            value['saveFlow']['save'][key] = False
            self.archive(self.observation_id, 'observe-save-flow', 1, value, 'completed')
            with self.assertRaises(ValueError): self.before()
        value = copy.deepcopy(original)
        value['saveFlow']['save']['records'][1] = value['saveFlow']['save']['records'][0]
        self.archive(self.observation_id, 'observe-save-flow', 1, value, 'completed')
        with self.assertRaisesRegex(ValueError, 'duplicate'): self.before()

    def test_frozen_files_and_exact_mapping_are_checked(self):
        for name in ('plan.json', 'recipe.json', 'scenario.json', 'fixture.pcsav', 'preparation-receipt.json'):
            path = self.run / name
            original = path.read_bytes()
            path.write_bytes(original + b' ')
            with self.assertRaises(ValueError): self.before()
            path.write_bytes(original)
        self.context['fixtureTarget'] = str(self.career / 'Different.pcsav')
        self.put('context.json', self.context)
        with self.assertRaisesRegex(ValueError, 'Staged fixture path'): self.before()

    def test_uncertainty_pending_and_unarchived_refuse(self):
        self.before()
        for name in ('outstanding.json', 'request.control', 'result.control', 'history/x.error.result'):
            path = self.directory / name
            path.write_bytes(b'uncertain')
            with self.assertRaises(ValueError): e.verify_baseline(self.run, bind=self.bind)
            path.unlink()

    def test_readonly_sequence_can_advance_but_prior_save_cannot(self):
        self.before()
        self.archive(self.action_id, 'ping', 2, dict(source='diagnostic-host', game='LE2'), 'completed')
        e.verify_baseline(self.run, bind=self.bind)
        self.action()
        with self.assertRaisesRegex(ValueError, 'already followed'): e.verify_baseline(self.run, bind=self.bind)

    def test_preexisting_candidate_and_path_escape_refused(self):
        self.before(); self.action()
        for value in ('Save99.pcsav', '../escape.pcsav', 'C:bad.pcsav', 'CON.pcsav', 'ObservedNew.pcsav.'):
            with self.assertRaises(ValueError): self.after(value)

    def test_changed_prior_file_report_preserves_actual_changes(self):
        self.before(); self.action()
        self.target.write_bytes(b'Changed staged save')
        code, report = self.after()
        self.assertEqual(code, 2)
        self.assertIn('Save99.pcsav', report['previousFileChanges'])
        self.assertIn('previous-career-files-changed-or-removed', report['failures'])
        self.assertNotIn('artifact', report)
        self.assertFalse((self.run / f'artifacts/save-{self.action_id}.pcsav').exists())

    def test_unexpected_new_file_or_directory_is_explicit_failure(self):
        self.before(); self.action()
        (self.career / 'unexpected.bin').write_bytes(b'new')
        (self.career / 'new-directory').mkdir()
        code, report = self.after()
        self.assertEqual(code, 2)
        self.assertEqual(set(report['failures']), {'unexpected-additional-files', 'career-directories-changed'})
        self.assertIn('unexpected.bin', report['newFiles'])

    def test_corrupt_candidate_crc_is_not_archived(self):
        self.before(); self.action()
        (self.career / 'ObservedNew.pcsav').write_bytes(self.save[:-1] + bytes([self.save[-1] ^ 1]))
        with self.assertRaisesRegex(ValueError, 'CRC32'): self.after()
        self.assertFalse((self.run / f'artifacts/save-{self.action_id}.pcsav').exists())

    def test_sidecar_baseline_hash_request_and_lease_are_required(self):
        self.before(); self.action()
        name = 'control/history/' + self.action_id + '.save-baseline.json'
        original = e.read(self.run / name)
        for key, value in [('saveBaselineSha256', '0'*64), ('requestId', self.observation_id), ('sequence', 1), ('wireIdentity', {})]:
            bad = dict(original); bad[key] = value
            self.put(name, bad)
            with self.assertRaisesRegex(ValueError, 'sealed'): self.after()

    def test_wrong_status_flag_or_correlation_refuses(self):
        self.before(); payload = self.action()
        for key in ('entered', 'returnedNormally', 'parameterFrameIntact'):
            bad = copy.deepcopy(payload); bad['menuAction'][key] = False
            self.archive(self.action_id, 'save-new', 2, bad, 'dispatched')
            with self.assertRaises(ValueError): self.after()
        request, result = self.archive(self.action_id, 'save-new', 2, payload, 'dispatched')
        result['lease'] = self.observation_id
        (self.directory / 'history' / (self.action_id + '.result')).write_bytes(e.c.encode(result))
        with self.assertRaisesRegex(ValueError, 'Uncorrelated'): self.after()

    def test_consumed_exactly_once_and_newer_than_baseline(self):
        self.before(); self.action()
        source = self.directory / 'consumed-2.request'
        (self.directory / 'consumed-3.request').write_bytes(source.read_bytes())
        with self.assertRaisesRegex(ValueError, 'ambiguous'): self.after()

    def test_action_before_cutoff_and_missing_reviewed_fixture_refused(self):
        self.before(); self.action()
        baseline = e.read(self.run / e.BASELINE)
        baseline['controlSequence'] = 2
        self.put(e.BASELINE, baseline)
        with self.assertRaisesRegex(ValueError, 'sole SaveNew'): self.after()
        self.payload['saveFlow']['save']['records'][0]['index'] = 98
        self.archive(self.observation_id, 'observe-save-flow', 1, self.payload, 'completed')
        with self.assertRaisesRegex(ValueError, 'fixture descriptor is missing'):
            e.observation(self.run, self.observation_id, self.wire, self.career,
                          self.plan['fixtureSelection']['descriptor'])

    def test_tampered_baseline_cannot_rebind_changed_staged_fixture(self):
        self.before()
        self.target.write_bytes(b'Changed staged fixture')
        baseline = e.read(self.run / e.BASELINE)
        baseline['inventory'] = e.inventory(self.career)
        self.put(e.BASELINE, baseline)
        with self.assertRaisesRegex(ValueError, 'unchanged staged fixture'):
            e.verify_baseline(self.run, bind=self.bind)

    def marked_observation(self, **changes):
        save = self.payload['saveFlow']['save']
        save.update(newSaveRow=copy.deepcopy(MARKER), recordCount=len(save['records']),
                    observedRowCount=len(save['records']) + 1)
        save.update(changes)
        self.archive(self.observation_id, 'observe-save-flow', 1, self.payload, 'completed')
        return save

    def test_recognized_ui_marker_preserves_real_records_and_sealed_baseline(self):
        save = self.marked_observation()
        receipt = self.before()
        self.assertEqual(save['observedRowCount'], 4)
        self.assertEqual(save['recordCount'], 3)
        self.assertEqual(e.c.verify_save_baseline(self.run, self.directory, self.wire), e.s.digest(self.run / e.BASELINE))
        self.assertEqual(receipt['sourceObservation']['responseSha256'],
                         e.s.digest(self.directory / 'history' / (self.observation_id + '.result')))
        self.action()
        code, result = self.after()
        self.assertEqual(code, 0)
        self.assertEqual(result['candidateLeaf'], 'ObservedNew.pcsav')
        self.assertFalse(any(result['claims'].values()))

    def test_marker_signature_and_types_are_exact(self):
        self.marked_observation()
        save = self.payload['saveFlow']['save']
        changes = dict(recognized=False, recognitionPolicy='guessed-policy', uiRow=1, index=-1, type=2,
                       career='Career', saveName='NewSave', careerCount=1, careerCapacity=1,
                       saveNameCount=1, saveNameCapacity=1, saveGameIsNull=False)
        for key, value in changes.items():
            with self.subTest(field=key):
                save['newSaveRow'] = dict(MARKER, **{key: value})
                self.archive(self.observation_id, 'observe-save-flow', 1, self.payload, 'completed')
                with self.assertRaisesRegex(ValueError, 'row recognition'): self.before()
        for key, value in MARKER.items():
            if type(value) not in (bool, int): continue
            with self.subTest(wrong_type=key):
                save['newSaveRow'] = dict(MARKER, **{key: 1 if type(value) is bool else False})
                self.archive(self.observation_id, 'observe-save-flow', 1, self.payload, 'completed')
                with self.assertRaisesRegex(ValueError, 'row recognition'): self.before()
        for malformed in (None, [MARKER, MARKER], dict(MARKER, extra=True),
                          {k: v for k, v in MARKER.items() if k != 'saveGameIsNull'}):
            save['newSaveRow'] = malformed
            self.archive(self.observation_id, 'observe-save-flow', 1, self.payload, 'completed')
            with self.assertRaisesRegex(ValueError, 'row recognition'): self.before()

    def test_marker_requires_complete_row_accounting(self):
        save = self.marked_observation()
        for changes in (dict(recordCount=2), dict(recordCount=True), dict(observedRowCount=3),
                        dict(observedRowCount=5), dict(observedRowCount=True), dict(observedRowCount=513)):
            self.marked_observation(**changes)
            with self.assertRaisesRegex(ValueError, 'row accounting'): self.before()
        self.marked_observation()
        del save['recordCount']
        self.archive(self.observation_id, 'observe-save-flow', 1, self.payload, 'completed')
        with self.assertRaisesRegex(ValueError, 'explicit recordCount'): self.before()

    def test_unrecognized_diagnostics_and_empty_real_record_never_promoted(self):
        save = self.marked_observation(recordsComplete=False)
        save['rejectedRowDiagnostics'] = dict(syntheticMarkerRecognized=True, rows=[MARKER])
        self.archive(self.observation_id, 'observe-save-flow', 1, self.payload, 'completed')
        with self.assertRaisesRegex(ValueError, 'complete same-flow'): self.before()
        save['recordsComplete'] = True
        save['records'].append(dict(career='', saveName='', index=0, type=0))
        self.marked_observation()
        with self.assertRaisesRegex(ValueError, 'leaf name'): self.before()

    def test_marker_does_not_replace_or_duplicate_exact_fixture_tuple(self):
        save = self.marked_observation()
        original = copy.deepcopy(save['records'])
        for records, reason in ((original[1:], 'fixture descriptor is missing'),
                                ([], 'fixture descriptor is missing'),
                                (original + [original[0]], 'duplicate descriptor')):
            save['records'] = records
            self.marked_observation()
            with self.assertRaisesRegex(ValueError, reason): self.before()

    def test_no_marker_explicit_count_and_legacy_observations_supported(self):
        save = self.payload['saveFlow']['save']
        save['recordCount'] = len(save['records'])
        self.archive(self.observation_id, 'observe-save-flow', 1, self.payload, 'completed')
        e.observation(self.run, self.observation_id, self.wire, self.career, self.plan['fixtureSelection']['descriptor'])
        save['recordCount'] = 0
        self.archive(self.observation_id, 'observe-save-flow', 1, self.payload, 'completed')
        with self.assertRaisesRegex(ValueError, 'row accounting'): self.before()
        del save['recordCount']
        self.archive(self.observation_id, 'observe-save-flow', 1, self.payload, 'completed')
        self.before()

    def test_binding_changes_and_restored_session_refused(self):
        calls = []
        def unstable(run):
            calls.append(run)
            wire = dict(self.wire)
            if len(calls) > 1: wire['lease'] = self.action_id
            return self.directory, wire
        with self.assertRaisesRegex(ValueError, 'Binding or inventory'):
            e.before(self.run, self.case, self.observation_id, self.capture, 'Reviewer', bind=unstable)
        self.context['phase'] = 'restored'
        self.put('context.json', self.context)
        with self.assertRaisesRegex(ValueError, 'game-bound'): self.before()

    def test_stable_read_detects_mid_read_change(self):
        actual = e.raw
        calls = []
        def change(path, limit):
            value = actual(path, limit)
            calls.append(path)
            if len(calls) == 1: path.write_bytes(value + b'changed')
            return value
        with mock.patch.object(e, 'raw', change):
            with self.assertRaisesRegex(ValueError, 'changed during stable read'): e.stable_bytes(self.target)

    def test_bounds_and_symlink_refused(self):
        with mock.patch.object(e, 'MAX_ENTRIES', 1):
            with self.assertRaisesRegex(ValueError, 'entry bound'): e.inventory(self.career)
        with mock.patch.object(e, 'MAX_TOTAL', 1):
            with self.assertRaisesRegex(ValueError, 'byte bound'): e.inventory(self.career)
        with mock.patch.object(e, 'MAX_FILE', 1):
            with self.assertRaises(ValueError): e.stable_bytes(self.target)
        link = self.career / 'linked.pcsav'
        try: link.symlink_to(self.target)
        except OSError: return  # Windows developer-mode privilege is optional; bounds above still ran.
        with self.assertRaises(ValueError): e.inventory(self.career)

    def test_actual_cpp_decoder_accepts_archived_action_if_available(self):
        binary = os.environ.get('LE2_CONTROL_CPP')
        if not binary: self.skipTest('Set LE2_CONTROL_CPP to the compiled codec executable')
        self.before(); self.action()
        # Same optional entry point as the protocol conformance test suite.
        for suffix in ('request', 'result'):
            path = self.directory / 'history' / (self.action_id + '.' + suffix)
            result = subprocess.run([binary, str(path)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()

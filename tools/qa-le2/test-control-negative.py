# SPDX-License-Identifier: MIT
"""Negative-control admission and recovery with temporary files/fake binding."""
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest

spec = importlib.util.spec_from_file_location('negative', Path(__file__).with_name('control-negative.py'))
n = importlib.util.module_from_spec(spec)
spec.loader.exec_module(n)
c = n.c
IDENTITY = dict(run='aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa', lease='bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb',
                pid='1234', start='134127912301234567', exe='a' * 64, host='b' * 64, mod='c' * 64, game='LE2')


class NegativeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.bind = lambda _: (self.root, IDENTITY.copy())

    def answer(self, case, *, status='rejected', echo_wrong=False, reason=None):
        request = c.decode((self.root / 'request.control').read_bytes())
        (self.root / 'request.control').rename(self.root / ('consumed-' + request['id']))
        payload = json.dumps(dict(source='diagnostic-host', ready=False,
                                  reason=reason or n.REASONS[case])).encode()
        result = dict(request, **IDENTITY)
        result.update(kind='result', status=status, payload=payload, bytes=str(len(payload)))
        if echo_wrong:
            result['lease'] = request['lease']
        c.publish(self.root / 'result.control', c.encode(result))

    def peer(self, case, **options):
        errors = []
        def run():
            try:
                end = time.monotonic() + 3
                while not (self.root / 'request.control').exists():
                    if time.monotonic() > end:
                        raise TimeoutError('no request')
                    time.sleep(.01)
                self.answer(case, **options)
            except Exception as error:
                errors.append(error)
        thread = threading.Thread(target=run)
        thread.start()
        self.addCleanup(thread.join, 4)
        return thread, errors

    def test_expected_rejections_archive_actual_identity_and_original_request(self):
        for case in n.REASONS:
            with self.subTest(case=case):
                thread, errors = self.peer(case)
                result = n.probe(self.root, case, bind=self.bind)
                thread.join()
                self.assertFalse(errors)
                self.assertEqual(result['status'], 'rejected')
                request = c.decode((self.root / 'history' / (result['id'] + '.request')).read_bytes())
                self.assertEqual(request['verb'], 'ping')
                self.assertEqual(request['payload'], b'')
                self.assertEqual(request['lease'] != IDENTITY['lease'], case == 'wrong-lease')
                self.assertEqual(result['lease'], IDENTITY['lease'])
                self.assertFalse((self.root / 'outstanding.json').exists())
                self.assertFalse((self.root / 'client.lock').exists())

    def test_unexpected_completion_or_echoed_wrong_identity_is_retained(self):
        for options in ({'status': 'completed'}, {'echo_wrong': True}, {'reason': 'some other rejection'}):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as tmp:
                self.root = Path(tmp).resolve()
                thread, errors = self.peer('wrong-lease', **options)
                with self.assertRaises(c.ControlError):
                    n.probe(self.root, 'wrong-lease', bind=self.bind)
                thread.join()
                self.assertFalse(errors)
                self.assertTrue((self.root / 'outstanding.json').exists())
                self.assertTrue((self.root / 'result.control').exists())
                with self.assertRaises(c.ControlError):
                    c.submit(self.root, 'ping', bind=self.bind)

    def test_timeout_late_collect_does_not_resend(self):
        with self.assertRaises(c.ControlError):
            n.probe(self.root, 'wrong-lease', timeout=1, bind=self.bind)
        original = (self.root / 'request.control').read_bytes()
        with self.assertRaises(c.ControlError):
            n.probe(self.root, 'expired', bind=self.bind)
        self.assertEqual((self.root / 'request.control').read_bytes(), original)
        with self.assertRaisesRegex(c.ControlError, 'control-negative.py collect'):
            c.collect(self.root, bind=self.bind)
        self.answer('wrong-lease')
        result = n.collect(self.root, bind=self.bind)
        self.assertEqual(result['status'], 'rejected')
        self.assertEqual(len(list((self.root / 'history').glob('*.request'))), 1)
        self.assertFalse((self.root / 'outstanding.json').exists())

    def test_invalid_case_or_held_client_lock_never_publishes(self):
        for case in ('console', 'wrong-game', ''):
            with self.assertRaises(c.ControlError):
                n.probe(self.root, case, bind=self.bind)
        (self.root / 'client.lock').write_text('other-owner')
        with self.assertRaises(FileExistsError):
            n.probe(self.root, 'expired', bind=self.bind)
        self.assertEqual((self.root / 'client.lock').read_text(), 'other-owner')
        self.assertFalse((self.root / 'request.control').exists())

    def test_changed_runtime_identity_keeps_pending_evidence(self):
        calls = 0
        def changed(_):
            nonlocal calls
            calls += 1
            identity = IDENTITY.copy()
            if calls > 1:
                identity['lease'] = 'dddddddd-dddd-4ddd-8ddd-dddddddddddd'
            return self.root, identity
        with self.assertRaisesRegex(c.ControlError, 'Binding changed'):
            n.probe(self.root, 'expired', bind=changed)
        self.assertTrue((self.root / 'outstanding.json').exists())
        self.assertFalse((self.root / 'request.control').exists())


if __name__ == '__main__':
    unittest.main()

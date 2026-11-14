# SPDX-License-Identifier: MIT
"""Attachment lifecycle tests using real session fixtures in temporary directories.

All processes/modules are injected; no native inventory, launch, or commands.
"""
import importlib.util
from pathlib import Path
import unittest
from unittest import mock


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


a = module("control_attach", "control-attach.py")
fixtures = module("attachment_session_fixtures", "test-session.py")
s = a.s


class AttachTests(unittest.TestCase):
    put = fixtures.SessionTests.put
    prepare = fixtures.SessionTests.prepare
    quiet_prepare = fixtures.SessionTests.quiet_prepare

    def setUp(self):
        fixtures.SessionTests.setUp(self)
        self.quiet_prepare()
        s.request_load(self.run, processes=lambda: [])
        self.live = [dict(name="MassEffect2", pid=42, path=str(self.exe),
                         started=s.utc().replace("+00:00", "Z"))]
        s.bind_process(self.run, 42, processes=lambda: self.live)
        self.host = self.asi / "LE2-diagnostic.asi"
        self.put(self.host, b"synthetic diagnostic module; never loaded")
        self.sha = s.digest(self.host)
        self.control = self.run / "control"
        self.invitation = self.locks / "le2-control.attach.json"
        self.intent = self.control / "attach-intent.json"
        self.no_native = mock.patch.object(s, "require_native_context", side_effect=AssertionError("offline test only"))
        self.no_native.start()
        self.addCleanup(self.no_native.stop)

    def invite(self, **overrides):
        options = dict(processes=lambda: self.live, modules=lambda _: [str(self.host)])
        options.update(overrides)
        return a.invite(self.run, self.host, self.sha, **options)

    def restore(self):
        s.reset(self.run, processes=lambda: [])

    def cleanup_invitation(self, processes=lambda: []):
        return a.cleanup(self.run, processes=processes)

    def test_exact_intent_published_without_claiming_handshake(self):
        result = self.invite()
        self.assertEqual(result["status"], "invitation-published-handshake-pending")
        self.assertFalse(result["ready"])
        self.assertEqual(self.invitation.read_bytes(), self.intent.read_bytes())
        descriptor = s.read_json(self.intent)
        identity = descriptor["identity"]
        self.assertEqual(identity["pid"], "42")
        self.assertEqual(identity["start"], a.c.filetime(self.live[0]["started"]))
        self.assertEqual(identity["host"], self.sha)
        self.assertEqual(identity["mod"], self.sha)
        self.assertEqual(descriptor["hostPath"], descriptor["modPath"])
        self.assertEqual(identity["lease"], result["lease"])
        self.assertFalse((self.control / "identity.json").exists())
        self.assertFalse((self.control / "request.control").exists())

    def test_reviewed_module_must_be_mapped_and_unchanged(self):
        with self.assertRaisesRegex(a.c.ControlError, "actual mapped module"):
            self.invite(modules=lambda _: [])
        self.host.write_bytes(b"different build")
        with self.assertRaisesRegex(a.c.ControlError, "actual mapped module"):
            self.invite()
        self.assertFalse(self.control.exists())
        self.assertFalse(self.invitation.exists())

    def test_stale_pid_start_or_competing_game_rejected(self):
        expected = self.live[0].copy()
        cases = [[dict(expected, started="2000-01-01T00:00:00.0000000Z")],
                 [expected, dict(expected, name="MassEffect1", pid=99)], []]
        for live in cases:
            with self.subTest(live=live), self.assertRaisesRegex(a.c.ControlError, "process identity changed"):
                self.invite(processes=lambda: live)
        self.assertFalse(self.control.exists())

    def test_foreign_session_lock_and_game_slot_prevent_invitation(self):
        lock = self.locks / "qa-cafeteria.lock"
        original = lock.read_bytes()
        lock.write_bytes(b"another run")
        with self.assertRaisesRegex(s.SessionError, "own"):
            self.invite()
        self.assertEqual(lock.read_bytes(), b"another run")
        lock.write_bytes(original)
        self.put(self.locks / "game-slot/lock.json", b"another owner")
        with self.assertRaisesRegex(s.SessionError, "Game-slot"):
            self.invite()
        self.assertFalse(self.control.exists())

    def test_existing_foreign_invitation_is_never_overwritten(self):
        self.put(self.invitation, b"foreign invitation")
        with self.assertRaisesRegex(a.c.ControlError, "already exists"):
            self.invite()
        self.assertEqual(self.invitation.read_bytes(), b"foreign invitation")
        self.assertFalse(self.control.exists())

    def test_late_invitation_collision_preserves_foreign_file_and_intent(self):
        publish = a.c.publish
        def collision(path, data):
            if path == self.invitation:
                path.write_bytes(b"late foreign invitation")
            return publish(path, data)
        with mock.patch.object(a.c, "publish", side_effect=collision):
            with self.assertRaises(FileExistsError):
                self.invite()
        self.assertTrue(self.intent.exists())
        self.assertEqual(self.invitation.read_bytes(), b"late foreign invitation")
        with self.assertRaisesRegex(a.c.ControlError, "already exists"):
            self.invite()

    def test_interrupted_publication_retains_intent_and_blocks_retry(self):
        publish = a.c.publish
        def interrupted(path, data):
            if path == self.invitation:
                raise KeyboardInterrupt()
            return publish(path, data)
        with mock.patch.object(a.c, "publish", side_effect=interrupted):
            with self.assertRaises(KeyboardInterrupt):
                self.invite()
        retained = self.intent.read_bytes()
        self.assertFalse(self.invitation.exists())
        with self.assertRaisesRegex(a.c.ControlError, "already exists"):
            self.invite()
        self.assertEqual(self.intent.read_bytes(), retained)

    def test_changed_owner_during_attach_refuses_shared_publication(self):
        publish = a.c.publish
        def change_owner(path, data):
            publish(path, data)
            if path == self.intent:
                (self.locks / "qa-cafeteria.lock").write_bytes(b"new owner")
        with mock.patch.object(a.c, "publish", side_effect=change_owner):
            with self.assertRaisesRegex(s.SessionError, "own"):
                self.invite()
        self.assertTrue(self.intent.exists())
        self.assertFalse(self.invitation.exists())

    def test_changed_process_during_attach_refuses_shared_publication(self):
        with self.assertRaisesRegex(a.c.ControlError, "state changed"):
            self.invite(processes=mock.Mock(side_effect=[self.live, []]))
        self.assertTrue(self.intent.exists())
        self.assertFalse(self.invitation.exists())

    def test_cleanup_requires_restoration_and_quiet_and_preserves_intent(self):
        self.invite()
        original = self.invitation.read_bytes()
        with self.assertRaisesRegex(a.c.ControlError, "restoration"):
            self.cleanup_invitation()
        self.restore()
        with self.assertRaisesRegex(s.SessionError, "active"):
            self.cleanup_invitation(processes=lambda: self.live)
        self.assertEqual(self.invitation.read_bytes(), original)
        self.assertEqual(self.cleanup_invitation()["status"], "owned-invitation-removed")
        self.assertEqual(self.intent.read_bytes(), original)
        self.assertFalse(self.invitation.exists())
        self.assertEqual(self.cleanup_invitation()["status"], "invitation-already-absent")

    def test_restored_label_without_verification_cannot_cleanup(self):
        self.invite()
        self.restore()
        context = s.read_json(self.run / "context.json")
        context["restorationVerified"] = False
        s.write_json(self.run / "context.json", context)
        with self.assertRaisesRegex(a.c.ControlError, "restoration"):
            self.cleanup_invitation()
        self.assertTrue(self.invitation.exists())

    def test_cleanup_never_removes_foreign_invitation(self):
        self.invite()
        self.restore()
        self.invitation.write_bytes(b"foreign invitation")
        with self.assertRaisesRegex(a.c.ControlError, "another owner's file"):
            self.cleanup_invitation()
        self.assertEqual(self.invitation.read_bytes(), b"foreign invitation")

    def test_cleanup_rejects_foreign_intent_even_if_shared_bytes_match(self):
        self.invite()
        self.restore()
        descriptor = s.read_json(self.intent)
        descriptor["identity"]["run"] = "dddddddd-dddd-4ddd-8ddd-dddddddddddd"
        s.write_json(self.intent, descriptor)
        self.invitation.write_bytes(self.intent.read_bytes())
        before = self.invitation.read_bytes()
        with self.assertRaisesRegex(a.c.ControlError, "another run"):
            self.cleanup_invitation()
        self.assertEqual(self.invitation.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()

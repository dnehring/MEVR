# SPDX-License-Identifier: MIT
"""Offline identity/wire/client failure tests; no native processes or requests."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location("control", Path(__file__).with_name("control.py"))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)

IDENTITY = dict(run="aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", lease="bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
                pid="1234", start="134127912301234567", exe="a" * 64, host="b" * 64, mod="c" * 64, game="LE2")


def frame():
    return dict(IDENTITY, kind="request", id="cccccccc-cccc-4ccc-8ccc-cccccccccccc", sequence="1", deadline="20000",
                verb="ping", status="pending", bytes="0", payload=b"")


class WireTests(unittest.TestCase):
    def test_exact_high_precision_identity(self):
        f = frame()
        self.assertEqual(c.decode(c.encode(f)), f)
        # Independently checked with DateTime.Parse(...).ToFileTimeUtc().
        self.assertEqual(c.filetime("2026-11-14T03:52:13.0013306Z"), "134391019330013306")

    def test_all_partial_files_fail_closed(self):
        raw = c.encode(frame())
        for end in range(len(raw)):
            with self.assertRaises(c.ControlError): c.decode(raw[:end])

    def test_corrupt_or_ambiguous_headers(self):
        raw = c.encode(frame())
        variants = [raw + b"x", raw.replace(b"game=LE2", b"game=LE1"), raw.replace(b"pid=1234", b"pid=01234"),
                    raw.replace(b"kind=request\n", b"kind=request\nkind=request\n"), raw.replace(b"\n", b"\r\n"),
                    raw.replace(b"sequence=1", b"sequence=18446744073709551616"),
                    raw.replace(b"host=" + b"b" * 64, b"host=" + b"g" * 64)]
        for value in variants:
            with self.assertRaises(c.ControlError): c.decode(value)

    def test_console_and_response_limits(self):
        for payload in (b"", b"a\nquit", b"a\x00quit", b"a" * 4097):
            with self.assertRaises(c.ControlError): c.encode(dict(frame(), verb="console", payload=payload, bytes=str(len(payload))))
        result = dict(frame(), kind="result", status="completed", payload=b"line1\nline2", bytes="11")
        self.assertEqual(c.decode(c.encode(result)), result)

    def test_probe_fov_wire_has_no_command_input(self):
        probe = dict(frame(), verb="probe-fov")
        self.assertEqual(c.decode(c.encode(probe)), probe)
        for payload in (b"x", b"stat fps", b"\n", b"\0"):
            with self.assertRaises(c.ControlError):
                c.encode(dict(probe, payload=payload, bytes=str(len(payload))))

    @unittest.skipUnless(os.environ.get("LE2_CONTROL_CPP"), "set LE2_CONTROL_CPP to standalone C++ executable")
    def test_cpp_python_conformance(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "frame.control"
            examples = [frame(), dict(frame(), verb="console", payload=b"stat fps", bytes="8"),
                        dict(frame(), verb="observe-save-menu"),
                        dict(frame(), verb="advance-splash"), dict(frame(), verb="open-load-menu"),
                        dict(frame(), verb="open-load-menu", kind="result", status="dispatched"),
                        dict(frame(), verb="probe-fov"),
                        dict(frame(), verb="probe-fov", kind="result", status="dispatched", payload=b'{"fov":90}', bytes="10"),
                        dict(frame(), kind="result", status="completed", payload=b"line1\nline2", bytes="11")]
            for f in examples:
                raw = c.encode(f); p.write_bytes(raw)
                run = subprocess.run([os.environ["LE2_CONTROL_CPP"], str(p)], capture_output=True)
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertEqual(run.stdout, raw)
            p.write_bytes(c.encode(frame()).replace(b"game=LE2", b"game=LE1"))
            self.assertEqual(subprocess.run([os.environ["LE2_CONTROL_CPP"], str(p)], capture_output=True).returncode, 2)
            malformed_probe = c.encode(dict(frame(), verb="probe-fov")).replace(b"bytes=0\n", b"bytes=1\n") + b"x"
            with self.assertRaises(c.ControlError): c.decode(malformed_probe)
            p.write_bytes(malformed_probe)
            self.assertEqual(subprocess.run([os.environ["LE2_CONTROL_CPP"], str(p)], capture_output=True).returncode, 2)


class ClientTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.bind = lambda _: (self.root, IDENTITY.copy())

    def peer(self, *, corrupt=False, console_complete=False, consume=True, status=None):
        errors = []
        def run():
            try:
                deadline = time.monotonic() + 3
                while not (self.root / "request.control").exists():
                    if time.monotonic() > deadline: raise TimeoutError("No request")
                    time.sleep(.01)
                request = c.decode((self.root / "request.control").read_bytes())
                if consume: (self.root / "request.control").unlink()
                request.update(kind="result", status="completed" if request["verb"] not in c.NATIVE_ACTIONS or console_complete else "dispatched")
                if status is not None: request["status"] = status
                if corrupt: request["lease"] = "dddddddd-dddd-4ddd-8ddd-dddddddddddd"
                c.publish(self.root / "result.control", c.encode(request))
            except Exception as error: errors.append(error)
        thread = threading.Thread(target=run)
        thread.start()
        self.addCleanup(thread.join, 4)
        return thread, errors

    def test_request_result_archived(self):
        thread, errors = self.peer()
        result = c.submit(self.root, "ping", bind=self.bind)
        thread.join(); self.assertFalse(errors)
        self.assertEqual(result["status"], "completed")
        self.assertFalse((self.root / "outstanding.json").exists())
        self.assertEqual(len(list((self.root / "history").glob("*"))), 2)
        self.assertFalse((self.root / "client.lock").exists())

    def test_late_uncorrelated_response_blocks_future_commands(self):
        thread, errors = self.peer(corrupt=True)
        with self.assertRaisesRegex(c.ControlError, "Uncorrelated"): c.submit(self.root, "ping", bind=self.bind)
        thread.join(); self.assertFalse(errors)
        with self.assertRaisesRegex(c.ControlError, "pending/uncertain"): c.submit(self.root, "ping", bind=self.bind)
        self.assertTrue((self.root / "outstanding.json").exists())

    def test_correlated_error_preserves_uncertainty_until_terminal_reconciliation(self):
        thread, errors = self.peer(status="error")
        with self.assertRaisesRegex(c.ControlError, "pending intent retained"):
            c.submit(self.root, "console", "stat fps", allow_console=True, bind=self.bind)
        thread.join(); self.assertFalse(errors)
        pending = c.session.read_json(self.root / "outstanding.json")
        error_path = self.root / "history" / (pending["id"] + ".error.result")
        self.assertEqual(c.decode(error_path.read_bytes())["status"], "error")
        self.assertFalse((self.root / "result.control").exists())
        with self.assertRaisesRegex(c.ControlError, "pending/uncertain"):
            c.submit(self.root, "ping", bind=self.bind)
        with self.assertRaisesRegex(c.ControlError, "No result to reconcile"):
            c.collect(self.root, bind=self.bind)
        self.assertTrue((self.root / "outstanding.json").exists())
        terminal = c.decode(error_path.read_bytes())
        terminal["status"] = "dispatched"
        c.publish(self.root / "result.control", c.encode(terminal))
        self.assertEqual(c.collect(self.root, bind=self.bind)["status"], "dispatched")
        self.assertFalse((self.root / "outstanding.json").exists())
        self.assertEqual(c.decode(error_path.read_bytes())["status"], "error")

    def test_authoritative_predispatch_rejection_is_terminal(self):
        thread, errors = self.peer(status="rejected")
        self.assertEqual(c.submit(self.root, "ping", bind=self.bind)["status"], "rejected")
        thread.join(); self.assertFalse(errors)
        self.assertFalse((self.root / "outstanding.json").exists())

    def test_timeout_retains_intent_and_no_retry(self):
        with self.assertRaisesRegex(c.ControlError, "uncertain"): c.submit(self.root, "ping", timeout=1, bind=self.bind)
        original = (self.root / "request.control").read_bytes()
        with self.assertRaises(c.ControlError): c.submit(self.root, "ping", bind=self.bind)
        self.assertEqual((self.root / "request.control").read_bytes(), original)

    def test_console_does_not_claim_gameplay_success(self):
        self.peer(console_complete=True)
        with self.assertRaisesRegex(c.ControlError, "gameplay completion"):
            c.submit(self.root, "console", "stat fps", allow_console=True, bind=self.bind)

    def test_console_requires_opt_in_and_forbids_streamlevelin(self):
        for command, opt in (("quit", False), ("streamlevelin BioP_Nor", True)):
            with self.assertRaises(c.ControlError): c.submit(self.root, "console", command, allow_console=opt, bind=self.bind)
        self.assertFalse(list(self.root.iterdir()))

    def test_probe_opt_in_independent_and_payload_refused_before_binding(self):
        with mock.patch.object(c, "binding", side_effect=AssertionError("must not bind")) as bind:
            for options in ({}, {"allow_console": True}, {"allow_probe_fov": True, "command": "stat fps"}):
                with self.assertRaises(c.ControlError):
                    c.submit(self.root, "probe-fov", bind=bind, **options)
            bind.assert_not_called()
        self.assertFalse(list(self.root.iterdir()))

    def test_menu_action_opt_in_and_payload_refused_before_binding(self):
        with mock.patch.object(c, "binding", side_effect=AssertionError("must not bind")) as bind:
            for verb in c.MENU_ACTIONS:
                for options in ({}, {"allow_console": True, "allow_probe_fov": True},
                                {"allow_menu_actions": True, "command": "arbitrary target"}):
                    with self.assertRaises(c.ControlError): c.submit(self.root, verb, bind=bind, **options)
            bind.assert_not_called()
        self.assertFalse(list(self.root.iterdir()))

    def test_menu_observation_completes_without_native_opt_in(self):
        thread, errors = self.peer()
        result = c.submit(self.root, "observe-save-menu", bind=self.bind)
        thread.join(); self.assertFalse(errors)
        self.assertEqual(result["status"], "completed")
        self.assertFalse((self.root / "outstanding.json").exists())

    def test_menu_completed_response_preserves_pending_intent(self):
        parent = self.root
        for verb in c.MENU_ACTIONS:
            with self.subTest(verb=verb):
                self.root = parent / verb; self.root.mkdir()
                thread, errors = self.peer(status="completed")
                with self.assertRaisesRegex(c.ControlError, "gameplay completion"):
                    c.submit(self.root, verb, allow_menu_actions=True, bind=self.bind)
                thread.join(); self.assertFalse(errors)
                original = (self.root / "result.control").read_bytes()
                with self.assertRaisesRegex(c.ControlError, "gameplay completion"):
                    c.collect(self.root, bind=self.bind)
                self.assertEqual((self.root / "result.control").read_bytes(), original)
                self.assertTrue((self.root / "outstanding.json").exists())

    def test_menu_dispatch_and_rejection_are_correlated_terminal_results(self):
        for verb in c.MENU_ACTIONS:
            for status in ("dispatched", "rejected"):
                with self.subTest(verb=verb, status=status):
                    thread, errors = self.peer(status=status)
                    result = c.submit(self.root, verb, allow_menu_actions=True, bind=self.bind)
                    thread.join(); self.assertFalse(errors)
                    self.assertEqual(result["status"], status)
                    self.assertFalse((self.root / "outstanding.json").exists())

    def test_probe_completed_cannot_clear_pending_or_be_collected(self):
        thread, errors = self.peer(status="completed")
        with self.assertRaisesRegex(c.ControlError, "gameplay completion"):
            c.submit(self.root, "probe-fov", allow_probe_fov=True, bind=self.bind)
        thread.join(); self.assertFalse(errors)
        self.assertTrue((self.root / "outstanding.json").exists())
        result = (self.root / "result.control").read_bytes()
        with self.assertRaisesRegex(c.ControlError, "gameplay completion"):
            c.collect(self.root, bind=self.bind)
        self.assertEqual((self.root / "result.control").read_bytes(), result)
        with self.assertRaisesRegex(c.ControlError, "pending/uncertain"):
            c.submit(self.root, "probe-fov", allow_probe_fov=True, bind=self.bind)

    def test_probe_error_retained_until_late_correlated_dispatched(self):
        thread, errors = self.peer(status="error")
        with self.assertRaisesRegex(c.ControlError, "pending intent retained"):
            c.submit(self.root, "probe-fov", allow_probe_fov=True, bind=self.bind)
        thread.join(); self.assertFalse(errors)
        pending = c.session.read_json(self.root / "outstanding.json")
        error_path = self.root / "history" / (pending["id"] + ".error.result")
        result = c.decode(error_path.read_bytes())
        self.assertEqual(result["verb"], "probe-fov")
        self.assertEqual(result["status"], "error")
        with self.assertRaisesRegex(c.ControlError, "pending/uncertain"):
            c.submit(self.root, "ping", bind=self.bind)
        result["status"] = "dispatched"
        c.publish(self.root / "result.control", c.encode(result))
        self.assertEqual(c.collect(self.root, bind=self.bind)["status"], "dispatched")
        self.assertFalse((self.root / "outstanding.json").exists())
        self.assertTrue(error_path.exists())
        self.assertEqual(len(list((self.root / "history").glob("*.request"))), 1)

    def test_probe_predispatch_rejection_is_terminal(self):
        thread, errors = self.peer(status="rejected")
        result = c.submit(self.root, "probe-fov", allow_probe_fov=True, bind=self.bind)
        thread.join(); self.assertFalse(errors)
        self.assertEqual(result["status"], "rejected")
        self.assertFalse((self.root / "outstanding.json").exists())

    def test_module_generation_change_is_uncertain(self):
        self.peer()
        calls = []
        def bind(_):
            identity = IDENTITY.copy()
            if calls: identity["mod"] = "d" * 64
            calls.append(1)
            return self.root, identity
        with self.assertRaisesRegex(c.ControlError, "identity changed"): c.submit(self.root, "ping", bind=bind)
        self.assertTrue((self.root / "outstanding.json").exists())

    def test_existing_client_lock_is_not_removed(self):
        (self.root / "client.lock").write_text("another-client")
        with self.assertRaises(FileExistsError): c.submit(self.root, "ping", bind=self.bind)
        self.assertEqual((self.root / "client.lock").read_text(), "another-client")

    def test_publish_never_clobbers_inflight_file(self):
        path = self.root / "request.control"
        c.publish(path, b"first")
        with self.assertRaises(FileExistsError): c.publish(path, b"second")
        self.assertEqual(path.read_bytes(), b"first")
        self.assertEqual(list(self.root.glob("*.tmp")), [])

    def test_late_result_can_be_collected_without_redispatch(self):
        with self.assertRaises(c.ControlError): c.submit(self.root, "ping", timeout=1, bind=self.bind)
        request = c.decode((self.root / "request.control").read_bytes())
        (self.root / "request.control").unlink()
        request.update(kind="result", status="completed")
        c.publish(self.root / "result.control", c.encode(request))
        result = c.collect(self.root, bind=self.bind)
        self.assertEqual(result["id"], request["id"])
        self.assertEqual((self.root / "sequence.txt").read_text(), "1")
        self.assertFalse((self.root / "request.control").exists())

    def test_collect_recovers_after_archival_before_pending_cleanup(self):
        with self.assertRaises(c.ControlError): c.submit(self.root, "ping", timeout=1, bind=self.bind)
        request = c.decode((self.root / "request.control").read_bytes())
        (self.root / "request.control").unlink()
        request.update(kind="result", status="completed")
        c.publish(self.root / "history" / (request["id"] + ".result"), c.encode(request))
        self.assertEqual(c.collect(self.root, bind=self.bind)["status"], "completed")
        self.assertFalse((self.root / "outstanding.json").exists())


class BindingTests(unittest.TestCase):
    def test_live_identity_and_mapped_modules_required(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve(); control = root / "control"; control.mkdir()
            exe, host, mod = [root / name for name in ("game.exe", "host.asi", "shadow.dll")]
            for p in (exe, host, mod): p.write_bytes(p.name.encode())
            live = dict(name="MassEffect2", pid=1234, path=str(exe), started="2026-11-14T03:52:13.0013306Z")
            context = dict(phase="game-bound", gameProcess=live.copy(), runId=IDENTITY["run"], exeSha256=c.session.digest(exe), exePath=str(exe))
            identity = dict(IDENTITY, start=c.filetime(live["started"]), exe=c.session.digest(exe), host=c.session.digest(host), mod=c.session.digest(mod))
            c.session.write_json(control / "identity.json", dict(identity=identity, hostPath=str(host), modPath=str(mod)))
            with mock.patch.object(c.session, "load_context", return_value=(root, context)):
                self.assertEqual(c.binding(root, processes=lambda: [live], modules=lambda _: [str(host), str(mod)])[1], identity)
                overlay_root = root / "overlay-junction"
                overlay = overlay_root / "overlay.dll"
                original = c.session.is_link
                with mock.patch.object(c.session, "is_link", side_effect=lambda p: p == overlay_root or original(p)):
                    self.assertEqual(c.binding(root, processes=lambda: [live], modules=lambda _: [str(overlay), str(host), str(mod)])[1], identity)
                    # An unrelated alias cannot stand in for the reviewed host.
                    with self.assertRaisesRegex(c.ControlError, "mapped module"):
                        c.binding(root, processes=lambda: [live], modules=lambda _: [str(overlay_root / host.name), str(mod)])
                with mock.patch.object(c.session, "is_link", side_effect=lambda p: p == host or original(p)):
                    with self.assertRaisesRegex(c.session.SessionError, "reparse"):
                        c.binding(root, processes=lambda: [live], modules=lambda _: [str(host), str(mod)])
                with self.assertRaisesRegex(c.ControlError, "mapped module"):
                    c.binding(root, processes=lambda: [live], modules=lambda _: [str(host)])
                with self.assertRaisesRegex(c.ControlError, "Process identity"):
                    c.binding(root, processes=lambda: [dict(live, started="2026-11-14T03:52:14.0013306Z")], modules=lambda _: [])
                with self.assertRaisesRegex(c.ControlError, "Process identity"):
                    c.binding(root, processes=lambda: [live, dict(live, pid=9999)], modules=lambda _: [])
                mod.write_bytes(b"replaced after build")
                with self.assertRaisesRegex(c.ControlError, "mapped module"):
                    c.binding(root, processes=lambda: [live], modules=lambda _: [str(host), str(mod)])


if __name__ == "__main__": unittest.main()

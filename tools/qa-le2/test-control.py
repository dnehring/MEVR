# SPDX-License-Identifier: MIT
"""Offline identity/wire/client failure tests; no native processes or requests."""
import importlib.util
import json
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


def pulse(**changes):
    return dict(schemaVersion=1, context="party-select", durationMs=100, buttons=0x1000,
                leftTrigger=0, rightTrigger=0, leftX=0, leftY=0, rightX=0, rightY=0) | changes


def pulse_frame(value=None):
    payload = json.dumps(pulse() if value is None else value, separators=(",", ":")).encode()
    return dict(frame(), verb="pad-pulse", payload=payload, bytes=str(len(payload)))


class WireTests(unittest.TestCase):
    def test_character_record_only_accepts_back_without_other_input(self):
        for duration in (1, 1000):
            f = pulse_frame(pulse(context="character-record", buttons=0x2000, durationMs=duration))
            self.assertEqual(c.decode(c.encode(f)), f)
        for field, value in (("buttons", 0x1000), ("buttons", 0x2010),
                             *[(name, 1) for name in c.PULSE_FIELDS[4:]]):
            with self.subTest(field=field, value=value), self.assertRaises(c.ControlError):
                c.validate_pulse(pulse(context="character-record", buttons=0x2000) | {field: value})

    def test_pad_schema_bounds_and_reserved_bits(self):
        for context in ("default", "party-select", "party-confirm", "loadout"):
            for value in (pulse(context=context, durationMs=1), pulse(context=context, durationMs=1000,
                          buttons=0xF3FF, leftTrigger=255, rightTrigger=255,
                          leftX=-32768, leftY=32767, rightX=-32768, rightY=32767)):
                f=pulse_frame(value)
                self.assertEqual(c.decode(c.encode(f)), f)
        bad=[{}, [], None, pulse(extra=1), pulse(schemaVersion=2), pulse(schemaVersion=True),
             pulse(context="orbital"), pulse(context="party-confirm-extra"), pulse(context=1), pulse(buttons=0), pulse(buttons=0x0400),
             pulse(buttons=0x0800), pulse(buttons=65536), pulse(buttons=-1)]
        for key in c.PULSE_FIELDS:
            bad.append({k:v for k,v in pulse().items() if k!=key})
        for key, low, high in [("durationMs",1,1000),("buttons",0,65535),
                              ("leftTrigger",0,255),("rightTrigger",0,255),
                              *[(key,-32768,32767) for key in ("leftX","leftY","rightX","rightY")]]:
            for value in (True, False, 1.0, "1", None, low-1, high+1):bad.append(pulse(**{key:value}))
        for value in bad:
            with self.subTest(value=value), self.assertRaises(c.ControlError):c.validate_pulse(value)
        self.assertEqual(c.validate_pulse(pulse(buttons=0,leftX=-1))["leftX"],-1)

    def test_pad_byte_bound_and_bom(self):
        raw=json.dumps(pulse()).encode()
        padded=raw+b" "*(c.MAX_PULSE_PAYLOAD-len(raw))
        self.assertEqual(c.parse_pulse(padded),pulse())
        for body in (padded+b" ",b"\xef\xbb\xbf"+raw):
            with self.assertRaises(c.ControlError):c.parse_pulse(body)

    def test_pad_json_semantic_not_key_order_policy(self):
        raw=json.dumps(dict(reversed(list(pulse().items()))),indent=2).encode()
        self.assertEqual(c.parse_pulse(raw),pulse())
        duplicate=b'{"schemaVersion":1,'+json.dumps(pulse())[1:].encode()
        for raw in (duplicate,b'{',b'[]',b'null',b'\xff',b' '* (c.MAX_PAYLOAD+1),
                    json.dumps(pulse()).replace('100,','NaN,').encode(),
                    json.dumps(pulse()).replace('100,','1e400,').encode()):
            with self.subTest(raw=raw[:80]), self.assertRaises(c.ControlError):c.parse_pulse(raw)
        with self.assertRaises(c.ControlError):c.encode(dict(frame(),verb="pad-pulse"))
        with self.assertRaises(c.ControlError):c.encode(dict(pulse_frame(),verb="observe-input"))

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
                        dict(frame(), verb="observe-resources"),
                        dict(frame(), verb="observe-orbital"),
                        dict(frame(), verb="observe-dialogue"),
                        dict(frame(), verb="start-dialogue-history"),
                        dict(frame(), verb="observe-dialogue-history"),
                        dict(frame(), verb="stop-dialogue-history"),
                        dict(frame(), verb="observe-weapon"),
                        dict(frame(), verb="observe-input"), pulse_frame(),
                        pulse_frame(pulse(context="party-confirm")),
                        pulse_frame(pulse(context="loadout")),
                        pulse_frame(pulse(context="character-record",buttons=0x2000)),
                        pulse_frame(pulse(context="default",buttons=0,leftX=-32768,rightTrigger=255)),
                        dict(frame(), verb="observe-save-flow"),
                        dict(frame(), verb="open-pause-menu"), dict(frame(), verb="open-save-menu"),
                        dict(frame(), verb="save-new"),
                        dict(frame(), verb="advance-splash"), dict(frame(), verb="open-load-menu"),
                        dict(frame(), verb="observe-fixture"), dict(frame(), verb="load-fixture"),
                        dict(frame(), verb="load-fixture", kind="result", status="dispatched"),
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
            # Build malformed payload frames without asking the Python encoder
            # to accept them; both independent decoders must reject the bytes.
            invalid_pulses = [json.dumps(pulse(buttons=0)),json.dumps(pulse(buttons=0x400)),
                              json.dumps(pulse(context="character-record")),
                              json.dumps(pulse(context="character-record",buttons=0x2000,leftY=1)),
                              json.dumps(pulse(durationMs=True)),json.dumps(pulse(durationMs=1.0)),
                              json.dumps(pulse(leftX=-32769)),json.dumps(pulse(context="orbital")),
                              json.dumps(pulse(context="party-confirm",buttons=0x400)),
                              json.dumps(pulse(context="party-confirm-extra")),
                              json.dumps(pulse(context="loadout",buttons=0x800)),
                              json.dumps(pulse(context="loadout-extra")),
                              json.dumps(pulse(extra=0)),json.dumps({k:v for k,v in pulse().items() if k!="rightY"}),
                              '{"schemaVersion":1,'+json.dumps(pulse())[1:]]
            for payload in invalid_pulses:
                body=payload.encode()
                prefix=c.encode(frame()).replace(b"verb=ping\n",b"verb=pad-pulse\n")
                raw=prefix.replace(b"bytes=0\n",("bytes="+str(len(body))+"\n").encode())+body
                p.write_bytes(raw)
                with self.assertRaises(c.ControlError):c.decode(raw)
                self.assertEqual(subprocess.run([os.environ["LE2_CONTROL_CPP"],str(p)],capture_output=True).returncode,2,payload)
            body=json.dumps(pulse()).encode()
            padded=body+b" "*(c.MAX_PULSE_PAYLOAD-len(body))
            for raw_body, expected in ((padded,0),(padded+b" ",2),(b"\xef\xbb\xbf"+body,2)):
                prefix=c.encode(frame()).replace(b"verb=ping\n",b"verb=pad-pulse\n")
                raw=prefix.replace(b"bytes=0\n",("bytes="+str(len(raw_body))+"\n").encode())+raw_body
                p.write_bytes(raw)
                if expected:
                    with self.assertRaises(c.ControlError):c.decode(raw)
                else:self.assertEqual(c.decode(raw)["payload"],raw_body)
                self.assertEqual(subprocess.run([os.environ["LE2_CONTROL_CPP"],str(p)],capture_output=True).returncode,expected)
            # Reordering/formatting are not an extra wire policy.
            body=json.dumps(dict(reversed(list(pulse().items()))),indent=2).encode()
            f=dict(frame(),verb="pad-pulse",payload=body,bytes=str(len(body)))
            p.write_bytes(c.encode(f))
            self.assertEqual(subprocess.run([os.environ["LE2_CONTROL_CPP"],str(p)],capture_output=True).returncode,0)
            malformed_probe = c.encode(dict(frame(), verb="probe-fov")).replace(b"bytes=0\n", b"bytes=1\n") + b"x"
            with self.assertRaises(c.ControlError): c.decode(malformed_probe)
            p.write_bytes(malformed_probe)
            self.assertEqual(subprocess.run([os.environ["LE2_CONTROL_CPP"], str(p)], capture_output=True).returncode, 2)

            for body in (b"reload", b"{}"):
                raw=c.encode(dict(frame(),verb="observe-weapon")).replace(b"bytes=0\n",("bytes="+str(len(body))+"\n").encode())+body
                with self.assertRaises(c.ControlError):c.decode(raw)
                p.write_bytes(raw)
                self.assertEqual(subprocess.run([os.environ["LE2_CONTROL_CPP"],str(p)],capture_output=True).returncode,2)


class ClientTests(unittest.TestCase):
    def test_character_record_retains_optin_and_archived_back_input(self):
        value = pulse(context="character-record", buttons=0x2000)
        bind = mock.Mock(side_effect=AssertionError("must not bind"))
        with self.assertRaises(c.ControlError):
            c.submit(self.root, "pad-pulse", pulse=value, bind=bind)
        bind.assert_not_called()
        thread, errors = self.peer()
        result = c.submit(self.root, "pad-pulse", pulse=value, allow_native_input=True, bind=self.bind)
        thread.join()
        self.assertFalse(errors)
        self.assertEqual(result["status"], "dispatched")
        archived = c.decode((self.root / "history" / (result["id"] + ".request")).read_bytes())
        self.assertEqual(c.parse_pulse(archived["payload"]), value)

    def test_loadout_retains_optin_and_archived_context(self):
        bind=mock.Mock(side_effect=AssertionError("must not bind"))
        with self.assertRaises(c.ControlError):
            c.submit(self.root,"pad-pulse",pulse=pulse(context="loadout"),bind=bind)
        bind.assert_not_called()
        thread,errors=self.peer()
        result=c.submit(self.root,"pad-pulse",pulse=pulse(context="loadout"),allow_native_input=True,bind=self.bind)
        thread.join();self.assertFalse(errors);self.assertEqual(result["status"],"dispatched")
        archived=c.decode((self.root/"history"/(result["id"]+".request")).read_bytes())
        self.assertEqual(c.parse_pulse(archived["payload"]),pulse(context="loadout"))

    def test_party_confirm_retains_optin_and_archived_context(self):
        bind=mock.Mock(side_effect=AssertionError("must not bind"))
        with self.assertRaises(c.ControlError):
            c.submit(self.root,"pad-pulse",pulse=pulse(context="party-confirm"),bind=bind)
        bind.assert_not_called()
        thread,errors=self.peer()
        result=c.submit(self.root,"pad-pulse",pulse=pulse(context="party-confirm"),allow_native_input=True,bind=self.bind)
        thread.join();self.assertFalse(errors);self.assertEqual(result["status"],"dispatched")
        archived=c.decode((self.root/"history"/(result["id"]+".request")).read_bytes())
        self.assertEqual(c.parse_pulse(archived["payload"]),pulse(context="party-confirm"))

    def test_weapon_readonly_without_action_optin(self):
        thread,errors=self.peer()
        result=c.submit(self.root,"observe-weapon",bind=self.bind)
        thread.join();self.assertFalse(errors);self.assertEqual(result["status"],"completed")
        for body in (b"reload",b"{}",b"0"):
            with self.assertRaises(c.ControlError):
                c.encode(dict(frame(),verb="observe-weapon",payload=body,bytes=str(len(body))))

    def test_cli_weapon_uses_existing_submit(self):
        with mock.patch("sys.argv",["control.py",str(self.root),"observe-weapon"]),mock.patch.object(c,"submit",return_value=dict(frame(),kind="result",status="completed")) as submit,mock.patch("builtins.print"):
            self.assertEqual(c.main(),0)
            self.assertEqual(submit.call_args.args[1],"observe-weapon")
            self.assertFalse(submit.call_args.kwargs["allow_native_input"])

    def test_pad_requires_independent_opt_in_and_schema_before_binding(self):
        bind=mock.Mock(side_effect=AssertionError("must not bind"))
        for options in ({"pulse":pulse()}, {"pulse":pulse(),"allow_console":True,"allow_menu_actions":True},
                        {"allow_native_input":True}, {"allow_native_input":True,"pulse":pulse(buttons=0)},
                        {"allow_native_input":True,"pulse":pulse(),"command":"quit"}):
            with self.assertRaises(c.ControlError):c.submit(self.root,"pad-pulse",bind=bind,**options)
        for verb in ("ping","observe-input","console"):
            with self.assertRaises(c.ControlError):c.submit(self.root,verb,pulse=pulse(),allow_console=True,bind=bind)
        bind.assert_not_called();self.assertFalse(list(self.root.iterdir()))

    def test_pad_published_result_and_exact_archived_payload(self):
        thread,errors=self.peer()
        result=c.submit(self.root,"pad-pulse",pulse=pulse(),allow_native_input=True,bind=self.bind)
        thread.join();self.assertFalse(errors);self.assertEqual(result["status"],"dispatched")
        archived=c.decode((self.root/"history"/(result["id"]+".request")).read_bytes())
        self.assertEqual(c.parse_pulse(archived["payload"]),pulse())
        self.assertNotIn(b"\n",archived["payload"])
        self.assertFalse((self.root/"outstanding.json").exists())

    def test_pad_completed_cannot_claim_consumption(self):
        thread,errors=self.peer(console_complete=True)
        with self.assertRaisesRegex(c.ControlError,"gameplay completion"):
            c.submit(self.root,"pad-pulse",pulse=pulse(),allow_native_input=True,bind=self.bind)
        thread.join();self.assertFalse(errors);self.assertTrue((self.root/"outstanding.json").exists())

    def test_pad_error_retains_intent_no_redispatch(self):
        thread,errors=self.peer(status="error")
        with self.assertRaisesRegex(c.ControlError,"pending intent retained"):
            c.submit(self.root,"pad-pulse",pulse=pulse(),allow_native_input=True,bind=self.bind)
        thread.join();self.assertFalse(errors)
        first=list((self.root/"history").glob("*.request"))
        with self.assertRaisesRegex(c.ControlError,"pending/uncertain"):
            c.submit(self.root,"pad-pulse",pulse=pulse(),allow_native_input=True,bind=self.bind)
        self.assertEqual(first,list((self.root/"history").glob("*.request")))
        self.assertEqual(len(list((self.root/"history").glob("*.error.result"))),1)

    def test_observe_input_readonly_without_input_optin(self):
        thread,errors=self.peer()
        result=c.submit(self.root,"observe-input",bind=self.bind)
        thread.join();self.assertFalse(errors);self.assertEqual(result["status"],"completed")

    def test_cli_pulse_file_uses_existing_submit_and_is_bounded(self):
        path=self.root/"pulse.json";path.write_text(json.dumps(pulse()))
        output=dict(frame(),kind="result",status="dispatched")
        args=["control.py",str(self.root),"pad-pulse","--allow-native-input","--pulse-json",str(path)]
        with mock.patch("sys.argv",args),mock.patch.object(c,"submit",return_value=output) as submit, mock.patch("builtins.print"):
            self.assertEqual(c.main(),0)
            self.assertEqual(submit.call_args.kwargs["pulse"],pulse())
            self.assertTrue(submit.call_args.kwargs["allow_native_input"])
        path.write_bytes(b" "*(c.MAX_PULSE_PAYLOAD+1))
        with mock.patch("sys.argv",args),mock.patch.object(c,"submit") as submit,mock.patch("builtins.print"):
            self.assertEqual(c.main(),2);submit.assert_not_called()

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

    def test_fixture_opt_in_independent_and_target_refused_before_binding(self):
        with mock.patch.object(c, "binding", side_effect=AssertionError("must not bind")) as bind:
            for options in ({}, {"allow_console": True, "allow_probe_fov": True, "allow_menu_actions": True},
                            {"allow_fixture_load": True, "command": "Save_0099"}):
                with self.assertRaises(c.ControlError): c.submit(self.root, "load-fixture", bind=bind, **options)
            bind.assert_not_called()
        self.assertFalse(list(self.root.iterdir()))

    def test_fixture_observation_needs_no_native_opt_in(self):
        thread, errors = self.peer()
        self.assertEqual(c.submit(self.root, "observe-fixture", bind=self.bind)["status"], "completed")
        thread.join(); self.assertFalse(errors)

    def test_resource_observation_is_read_only_and_accepts_no_edits(self):
        with self.assertRaises(c.ControlError):
            c.submit(self.root, "observe-resources", command="credits=999", bind=self.bind)
        self.assertFalse(list(self.root.iterdir()))
        thread, errors = self.peer()
        self.assertEqual(c.submit(self.root, "observe-resources", bind=self.bind)["status"], "completed")
        thread.join(); self.assertFalse(errors)

    def test_fixture_completed_preserves_uncertain_intent(self):
        thread, errors = self.peer(status="completed")
        with self.assertRaisesRegex(c.ControlError, "gameplay completion"):
            c.submit(self.root, "load-fixture", allow_fixture_load=True, bind=self.bind)
        thread.join(); self.assertFalse(errors)
        with self.assertRaisesRegex(c.ControlError, "gameplay completion"):
            c.collect(self.root, bind=self.bind)
        self.assertTrue((self.root / "outstanding.json").exists())

    def test_orbital_observation_cannot_launch_a_probe(self):
        with self.assertRaises(c.ControlError):
            c.submit(self.root, "observe-orbital", command="launch-probe", bind=self.bind)
        self.assertFalse(list(self.root.iterdir()))
        thread, errors = self.peer()
        self.assertEqual(c.submit(self.root, "observe-orbital", bind=self.bind)["status"], "completed")
        thread.join(); self.assertFalse(errors)

    def test_save_flow_observation_needs_no_native_opt_in_or_save_payload(self):
        with self.assertRaises(c.ControlError):
            c.submit(self.root, "observe-save-flow", command="save", bind=self.bind)
        self.assertFalse(list(self.root.iterdir()))
        thread, errors = self.peer()
        self.assertEqual(c.submit(self.root, "observe-save-flow", bind=self.bind)["status"], "completed")
        thread.join(); self.assertFalse(errors)

    def test_new_save_requires_separate_opt_in_and_no_target(self):
        with mock.patch.object(c, "binding", side_effect=AssertionError("must not bind")) as bind:
            for options in ({}, {"allow_menu_actions": True, "allow_fixture_load": True,
                                 "allow_console": True, "allow_probe_fov": True},
                            {"allow_save_new": True, "command": "Save_0100"}):
                with self.assertRaises(c.ControlError):
                    c.submit(self.root, "save-new", bind=bind, **options)
            bind.assert_not_called()
        self.assertFalse(list(self.root.iterdir()))

    def test_new_save_baseline_refusal_publishes_no_intent(self):
        with mock.patch.object(c, "verify_save_baseline", side_effect=c.ControlError("changed career")):
            with self.assertRaisesRegex(c.ControlError, "changed career"):
                c.submit(self.root, "save-new", allow_save_new=True, bind=self.bind)
        self.assertFalse((self.root / "outstanding.json").exists())
        self.assertFalse((self.root / "request.control").exists())
        self.assertFalse((self.root / "sequence.txt").exists())

    def test_new_save_dispatch_records_exact_baseline_without_completion_claim(self):
        thread, errors = self.peer(status="dispatched")
        with mock.patch.object(c, "verify_save_baseline", return_value="a" * 64):
            result = c.submit(self.root, "save-new", allow_save_new=True, bind=self.bind)
        thread.join(); self.assertFalse(errors)
        self.assertEqual(result["status"], "dispatched")
        sidecar = json.loads((self.root / "history" / (result["id"] + ".save-baseline.json")).read_text())
        self.assertEqual(sidecar["saveBaselineSha256"], "a" * 64)
        self.assertEqual(sidecar["wireIdentity"], IDENTITY)
        self.assertEqual(sidecar["sequence"], int(result["sequence"]))

    def test_new_save_completed_retains_uncertainty(self):
        thread, errors = self.peer(status="completed")
        with mock.patch.object(c, "verify_save_baseline", return_value="b" * 64):
            with self.assertRaisesRegex(c.ControlError, "gameplay completion"):
                c.submit(self.root, "save-new", allow_save_new=True, bind=self.bind)
        thread.join(); self.assertFalse(errors)
        self.assertTrue((self.root / "outstanding.json").exists())

    def test_fixture_terminal_results_do_not_claim_loaded_state(self):
        for status in ("dispatched", "rejected"):
            thread, errors = self.peer(status=status)
            self.assertEqual(c.submit(self.root, "load-fixture", allow_fixture_load=True, bind=self.bind)["status"], status)
            thread.join(); self.assertFalse(errors)
            self.assertFalse((self.root / "outstanding.json").exists())

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
        # This case exercises identity reconciliation, not peer scheduling.
        # Publish a real file response when the real request is published so a
        # busy test host cannot turn the assertion into an unrelated timeout.
        publish = c.publish
        requests = []
        def responding_publish(path, data):
            publish(path, data)
            if path == self.root / "request.control":
                request = c.decode(path.read_bytes())
                requests.append(request)
                path.unlink()
                publish(self.root / "result.control",
                        c.encode(dict(request, kind="result", status="completed")))
        calls = []
        def bind(_):
            identity = IDENTITY.copy()
            if calls: identity["mod"] = "d" * 64
            calls.append(1)
            return self.root, identity
        with mock.patch.object(c, "publish", side_effect=responding_publish):
            with self.assertRaisesRegex(c.ControlError, "identity changed"):
                c.submit(self.root, "ping", bind=bind)
        self.assertEqual(len(requests), 1)
        self.assertEqual(len(calls), 2)
        self.assertTrue((self.root / "outstanding.json").exists())
        self.assertTrue((self.root / "result.control").exists())
        self.assertFalse((self.root / "client.lock").exists())
        self.assertEqual(len(list((self.root / "history").glob("*.request"))), 1)
        self.assertEqual(len(list((self.root / "history").glob("*.result"))), 0)

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

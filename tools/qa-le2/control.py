# SPDX-License-Identifier: MIT
"""Run-bound LE2 control client. No shared console.request writes or auto-retries.

Requires a diagnostic runtime producer of control/identity.json and result.control.
The protocol/client alone does not install that producer or prove native calls.
"""
from __future__ import annotations
import argparse
import datetime as dt
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time
import uuid

_spec = importlib.util.spec_from_file_location("le2_control_session", Path(__file__).with_name("session.py"))
session = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(session)

MAGIC = b"MEVR-CONTROL/1\n"
KEYS = ("kind", "run", "lease", "pid", "start", "exe", "host", "mod", "id", "sequence",
        "deadline", "verb", "status", "bytes", "game")
IDENTITY = ("run", "lease", "pid", "start", "exe", "host", "mod", "game")
MAX_PAYLOAD, MAX_FRAME, MAX_LIFETIME_MS = 16384, 17408, 30000
MENU_ACTIONS = ("advance-splash", "open-load-menu", "open-pause-menu", "open-save-menu")
NATIVE_ACTIONS = ("console", "probe-fov", "load-fixture", "save-new", "pad-pulse") + MENU_ACTIONS


class ControlError(ValueError):
    pass


def valid_uuid(value):
    return isinstance(value, str) and bool(re.fullmatch(
        r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", value)) and value != "00000000-0000-0000-0000-000000000000"


def number(value, maximum=(1 << 64) - 1):
    if not isinstance(value, str) or not re.fullmatch(r"0|[1-9][0-9]*", value) or int(value) > maximum:
        raise ControlError("Noncanonical or out-of-range wire number")
    return int(value)


PULSE_FIELDS = ("schemaVersion", "context", "durationMs", "buttons", "leftTrigger", "rightTrigger",
                "leftX", "leftY", "rightX", "rightY")


def validate_pulse(pulse):
    """One bounded native pad state, not a command, recipe, pointer or input script."""
    if not isinstance(pulse, dict) or set(pulse) != set(PULSE_FIELDS):
        raise ControlError("Pad pulse requires exactly the ten declared fields")
    if type(pulse["schemaVersion"]) is not int or pulse["schemaVersion"] != 1:
        raise ControlError("Pad pulse schemaVersion must be integer 1")
    if pulse["context"] not in ("default", "party-select"):
        raise ControlError("Unsupported pad pulse context")
    bounds = {"durationMs": (1, 1000), "buttons": (0, 65535),
              "leftTrigger": (0, 255), "rightTrigger": (0, 255),
              **{name: (-32768, 32767) for name in ("leftX", "leftY", "rightX", "rightY")}}
    for name, (low, high) in bounds.items():
        if type(pulse[name]) is not int or not low <= pulse[name] <= high:
            raise ControlError(f"Pad pulse {name} must be integer {low}..{high}")
    if pulse["buttons"] & ~0xF3FF:
        raise ControlError("Pad pulse has reserved button bits")
    if not any(pulse[name] for name in PULSE_FIELDS[3:]):
        raise ControlError("Entirely neutral pad pulse is not an action")
    return {name: pulse[name] for name in PULSE_FIELDS}


def parse_pulse(payload):
    if not isinstance(payload, bytes) or not payload or len(payload) > MAX_PAYLOAD:
        raise ControlError("Pad pulse JSON must be nonempty and bounded")
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ControlError("Duplicate pad pulse JSON key")
            result[key] = value
        return result
    def nonfinite(value):
        raise ControlError("Nonfinite pad pulse JSON number")
    try:
        return validate_pulse(json.loads(payload.decode("utf-8"), object_pairs_hook=pairs, parse_constant=nonfinite))
    except (UnicodeError, ValueError, TypeError, RecursionError) as error:
        raise ControlError("Invalid pad pulse JSON: " + str(error)) from error


def validate(frame):
    if set(frame) != set(KEYS) | {"payload"} or any(not isinstance(frame[k], str) for k in KEYS):
        raise ControlError("Unexpected fields or types")
    if frame["game"] != "LE2" or any(not valid_uuid(frame[k]) for k in ("run", "lease", "id")):
        raise ControlError("Invalid game or request/lease identity")
    if not number(frame["pid"], (1 << 32) - 1) or any(not number(frame[k]) for k in ("start", "sequence", "deadline")):
        raise ControlError("Zero process, sequence or time identity")
    if any(not re.fullmatch(r"[0-9a-f]{64}", frame[k]) for k in ("exe", "host", "mod")):
        raise ControlError("Invalid module hash")
    payload = frame["payload"]
    if not isinstance(payload, bytes) or len(payload) > MAX_PAYLOAD or b"\0" in payload or number(frame["bytes"]) != len(payload):
        raise ControlError("Invalid payload")
    if frame["verb"] not in ("ping", "observe", "observe-save-menu", "observe-fixture", "observe-resources", "observe-orbital", "observe-input", "observe-save-flow") + NATIVE_ACTIONS:
        raise ControlError("Unsupported verb")
    if frame["kind"] == "request":
        if frame["status"] != "pending":
            raise ControlError("Invalid request status")
        if frame["verb"] == "console":
            if not payload or len(payload) > 4096 or any(c < 32 or c == 127 for c in payload):
                raise ControlError("Native console requires one bounded command")
        elif frame["verb"] == "pad-pulse":
            parse_pulse(payload)
        elif payload:
            raise ControlError("Observation requests have no payload")
    elif frame["kind"] != "result" or frame["status"] not in ("completed", "dispatched", "rejected", "error"):
        raise ControlError("Invalid result status")
    return frame


def encode(frame):
    validate(frame)
    return MAGIC + b"".join((k + "=" + frame[k] + "\n").encode("ascii") for k in KEYS) + b"\n" + frame["payload"]


def decode(raw):
    if len(raw) > MAX_FRAME or not raw.startswith(MAGIC):
        raise ControlError("Size or protocol mismatch")
    rest = raw[len(MAGIC):]
    frame = {}
    for key in KEYS:
        line, sep, rest = rest.partition(b"\n")
        if not sep or not line.startswith((key + "=").encode()) or b"\r" in line or b"\0" in line:
            raise ControlError("Incomplete, duplicate or reordered header")
        try:
            frame[key] = line[len(key) + 1:].decode("ascii")
        except UnicodeError as error:
            raise ControlError("Non-ASCII header") from error
    if not rest.startswith(b"\n"):
        raise ControlError("Missing payload separator")
    frame["payload"] = rest[1:]
    return validate(frame)


def filetime(iso):
    """Preserve .NET's seventh subsecond digit used in process binding."""
    match = re.fullmatch(r"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)(?:\.(\d{1,7}))?Z", iso)
    if not match:
        raise ControlError("Process start must be exact UTC ISO time")
    seconds = dt.datetime.fromisoformat(match[1]).replace(tzinfo=dt.timezone.utc) - dt.datetime(1601, 1, 1, tzinfo=dt.timezone.utc)
    return str((seconds.days * 86400 + seconds.seconds) * 10000000 + int((match[2] or "").ljust(7, "0")))


def mapped_modules(pid):
    # Only a validated integer enters fixed PowerShell source. Host-context
    # validation is performed by session.windows_processes before this call.
    if type(pid) is not int or not 0 < pid < 1 << 32:
        raise ControlError("Invalid PID")
    script = "$ErrorActionPreference='Stop'; @((Get-Process -Id " + str(pid) + ").Modules | ForEach-Object { $_.FileName }) | ConvertTo-Json -Compress"
    output = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
                            capture_output=True, text=True, check=True).stdout
    paths = json.loads(output)
    return [paths] if isinstance(paths, str) else paths


def module_is_mapped(path, inventory):
    """Match the reviewed path without resolving unrelated overlay modules.

    Inventory is process evidence, not a set of paths we own. Only the exact
    selected module must pass our no-reparse policy; aliases never establish
    that identity. Keep the matched raw path check as well as the target check.
    """
    path = session.safe_path(path)
    if not isinstance(inventory, (list, tuple)) or any(
            not isinstance(p, str) or not Path(p).is_absolute() for p in inventory):
        raise ControlError("Invalid mapped module inventory")
    for raw in inventory:
        if str(Path(raw)).casefold() == str(path).casefold():
            return str(session.safe_path(raw)).casefold() == str(path).casefold()
    return False


def binding(run, *, processes=session.windows_processes, modules=mapped_modules):
    run, context = session.load_context(run)
    if processes is session.windows_processes:
        session.require_native_context(context)
    if context.get("phase") != "game-bound" or not context.get("gameProcess"):
        raise ControlError("Run is not bound to a game")
    expected = context["gameProcess"]
    live = processes()
    matches = [p for p in live if p["pid"] == expected["pid"]]
    if len(matches) != 1 or matches[0] != expected or any(p["name"] in ("MassEffect1", "MassEffect2", "MassEffect3") and p["pid"] != expected["pid"] for p in live):
        raise ControlError("Process identity or exclusive game ownership changed")
    directory = session.safe_path(run / "control")
    descriptor_path = session.safe_path(directory / "identity.json")
    if not descriptor_path.is_file():
        raise ControlError("No LE2 runtime control producer: identity.json is absent")
    descriptor = session.read_json(descriptor_path)
    identity = descriptor.get("identity", {})
    if set(identity) != set(IDENTITY) or any(not isinstance(v, str) for v in identity.values()):
        raise ControlError("Invalid runtime identity descriptor")
    validate(dict(identity, kind="request", id=identity["run"], sequence="1", deadline="1",
                  verb="ping", status="pending", bytes="0", payload=b""))
    if identity["run"] != context["runId"] or identity["pid"] != str(expected["pid"]) or identity["start"] != filetime(expected["started"]) or identity["exe"] != context["exeSha256"]:
        raise ControlError("Runtime producer differs from frozen session")
    if session.digest(context["exePath"]) != identity["exe"]:
        raise ControlError("Game executable changed")
    mapped = modules(expected["pid"])
    for role in ("host", "mod"):
        path = session.safe_path(Path(descriptor[role + "Path"]))
        if not module_is_mapped(path, mapped) or session.digest(path) != identity[role]:
            raise ControlError("Actual mapped module differs from runtime producer")
    return directory, identity


def publish(path, data):
    """Publish a complete file without replacing an existing in-flight request."""
    path = session.safe_path(path)
    temp = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        with temp.open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if os.name == "nt":
            os.rename(temp, path)  # Windows rename refuses an existing destination
        else:
            os.link(temp, path)  # same-volume, atomic and non-replacing on POSIX
    finally:
        temp.unlink(missing_ok=True)


def receive(run, directory, identity, frame, bind):
    result_path = session.safe_path(directory / "result.control")
    archived = session.safe_path(directory / "history" / (frame["id"] + ".result"))
    source = result_path if result_path.exists() else archived
    if not source.exists():
        raise ControlError("No result to reconcile; request remains uncertain")
    raw_result = source.read_bytes()
    result = decode(raw_result)
    if result["kind"] != "result" or any(result[k] != frame[k] for k in (*IDENTITY, "id", "sequence", "deadline", "verb")):
        raise ControlError("Uncorrelated result; request outcome is uncertain")
    if bind(run)[1] != identity:
        raise ControlError("Runtime identity changed during request")
    if frame["verb"] in NATIVE_ACTIONS and result["status"] == "completed":
        raise ControlError("Native dispatch cannot claim gameplay completion")
    if session.safe_path(directory / "request.control").exists():
        raise ControlError("Producer has not consumed request; outcome uncertain")
    if result["status"] == "error":
        # An error after Claim may follow entry into native code. Preserve the
        # pending intent until a separate authoritative terminal result arrives.
        # Keep its evidence separate so a late terminal response can reconcile.
        error_archive = session.safe_path(directory / "history" / (frame["id"] + ".error.result"))
        if error_archive.exists():
            if error_archive.read_bytes() != raw_result:
                raise ControlError("Archived error differs; outcome uncertain")
        else:
            publish(error_archive, raw_result)
        result_path.unlink(missing_ok=True)
        raise ControlError("Producer reported an error; outcome uncertain, pending intent retained")
    if archived.exists():
        if archived.read_bytes() != raw_result:
            raise ControlError("Archived response differs; outcome uncertain")
    else:
        publish(archived, raw_result)
    result_path.unlink(missing_ok=True)
    session.safe_path(directory / "outstanding.json").unlink()
    return result


def collect(run, *, bind=binding):
    """Resolve a late correlated response without issuing another request."""
    directory, identity = bind(run)
    token = str(uuid.uuid4())
    lock = session.safe_path(directory / "client.lock")
    with lock.open("x", encoding="ascii") as stream: stream.write(token)
    try:
        pending = session.read_json(session.safe_path(directory / "outstanding.json"))
        if "negativeCase" in pending:
            raise ControlError("Use control-negative.py collect for a pending negative control")
        if not valid_uuid(pending.get("id")) or pending.get("identity") != identity:
            raise ControlError("Pending request belongs to another runtime lease")
        request = decode(session.safe_path(directory / "history" / (pending["id"] + ".request")).read_bytes())
        if any(request[k] != identity[k] for k in IDENTITY) or request["id"] != pending["id"] or request["sequence"] != str(pending["sequence"]):
            raise ControlError("Pending request provenance changed")
        return receive(run, directory, identity, request, bind)
    finally:
        if lock.read_text(encoding="ascii") == token: lock.unlink()


def verify_save_baseline(run, directory, identity):
    """Read-only inventory check; engine dispatch does not authenticate this file."""
    baseline = session.safe_path(Path(run) / "artifacts" / "save-baseline.json")
    before_sha = session.digest(baseline)
    spec = importlib.util.spec_from_file_location("le2_save_evidence", Path(__file__).with_name("save-evidence.py"))
    collector = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(collector)
    collector.verify_baseline(run, bind=lambda requested: (directory, identity))
    if session.digest(baseline) != before_sha:
        raise ControlError("Save baseline changed during preflight")
    return before_sha


def submit(run, verb, command="", timeout=10, allow_console=False, *, allow_probe_fov=False,
           allow_menu_actions=False, allow_fixture_load=False, allow_save_new=False,
           allow_native_input=False, pulse=None, bind=binding):
    if not 1 <= timeout <= 30:
        raise ControlError("Timeout must be 1..30 seconds")
    if verb == "console" and (not allow_console or re.search(r"\bstreamlevelin\b", command, re.I)):
        raise ControlError("Console requires explicit opt-in; streamlevelin is prohibited")
    if verb == "probe-fov" and not allow_probe_fov:
        raise ControlError("Native FOV probe requires explicit opt-in")
    if verb in MENU_ACTIONS and not allow_menu_actions:
        raise ControlError("Native menu action requires explicit --allow-menu-actions")
    if verb == "load-fixture" and not allow_fixture_load:
        raise ControlError("Fixture loading requires explicit --allow-fixture-load")
    if verb == "save-new" and not allow_save_new:
        raise ControlError("New Save requires explicit --allow-save-new and a reviewed file baseline")
    if verb == "pad-pulse":
        if not allow_native_input:
            raise ControlError("Native pad pulse requires explicit --allow-native-input")
        payload = json.dumps(validate_pulse(pulse), separators=(",", ":"), allow_nan=False).encode("utf-8")
    else:
        if pulse is not None:
            raise ControlError("Pulse is only valid with pad-pulse")
        payload = command.encode("utf-8")
    if verb != "console" and command:
        raise ControlError("Command is only valid with console")
    directory, identity = bind(run)
    token = str(uuid.uuid4())
    client_lock = session.safe_path(directory / "client.lock")
    with client_lock.open("x", encoding="ascii") as stream:
        stream.write(token)
    try:
        pending = session.safe_path(directory / "outstanding.json")
        if pending.exists():
            raise ControlError("Previous request is pending/uncertain; reconcile it before further commands")
        request_path = session.safe_path(directory / "request.control")
        result_path = session.safe_path(directory / "result.control")
        if request_path.exists() or result_path.exists():
            raise ControlError("Endpoint has unarchived traffic; no request was sent")
        save_baseline_sha = verify_save_baseline(run, directory, identity) if verb == "save-new" else None
        history = session.safe_path(directory / "history")
        history.mkdir(exist_ok=True)
        sequence_path = session.safe_path(directory / "sequence.txt")
        sequence = int(sequence_path.read_text()) + 1 if sequence_path.exists() else 1
        frame = dict(identity, kind="request", id=token, sequence=str(sequence), deadline=str(time.time_ns() // 1000000 + timeout * 1000),
                     verb=verb, status="pending", payload=payload, bytes=str(len(payload)))
        wire = encode(frame)
        if save_baseline_sha is not None:
            sidecar = {"schemaVersion": 1, "requestId": token, "sequence": sequence,
                       "wireIdentity": identity, "saveBaselineSha256": save_baseline_sha,
                       "scope": "client-reviewed-inventory-preflight-not-engine-save-completion"}
            publish(history / (token + ".save-baseline.json"),
                    (json.dumps(sidecar, indent=2) + "\n").encode("utf-8"))
        # Persist intent before publishing. An interrupted client must not retry.
        session.write_json(pending, {"id": token, "sequence": sequence, "status": "pending", "identity": identity})
        sequence_path.write_text(str(sequence), encoding="ascii")
        publish(history / (token + ".request"), wire)
        publish(request_path, wire)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if result_path.exists():
                return receive(run, directory, identity, frame, bind)
            time.sleep(0.05)
        raise ControlError("No correlated result by deadline; outcome uncertain, no automatic retry")
    finally:
        if client_lock.read_text(encoding="ascii") == token:
            client_lock.unlink()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("run", type=Path)
    p.add_argument("verb", choices=("ping", "observe", "observe-save-menu", "observe-fixture", "observe-resources", "observe-orbital", "observe-input", "observe-save-flow", "collect") + NATIVE_ACTIONS)
    p.add_argument("--command", default="")
    p.add_argument("--timeout", type=int, default=10)
    p.add_argument("--allow-console", action="store_true")
    p.add_argument("--allow-probe-fov", action="store_true")
    p.add_argument("--allow-menu-actions", action="store_true")
    p.add_argument("--allow-fixture-load", action="store_true")
    p.add_argument("--allow-save-new", action="store_true")
    p.add_argument("--allow-native-input", action="store_true")
    p.add_argument("--pulse-json", type=Path, help="Existing bounded JSON pad state; only with pad-pulse")
    a = p.parse_args()
    try:
        pulse = None
        if a.pulse_json is not None:
            if a.verb != "pad-pulse":
                raise ControlError("--pulse-json is only valid with pad-pulse")
            with session.safe_path(a.pulse_json.absolute()).open("rb") as stream:
                pulse = parse_pulse(stream.read(MAX_PAYLOAD + 1))
        result = collect(a.run.absolute()) if a.verb == "collect" else submit(
            a.run.absolute(), a.verb, a.command, a.timeout, a.allow_console,
            allow_probe_fov=a.allow_probe_fov, allow_menu_actions=a.allow_menu_actions,
            allow_fixture_load=a.allow_fixture_load, allow_save_new=a.allow_save_new,
            allow_native_input=a.allow_native_input, pulse=pulse)
        print(json.dumps(dict(result, payload=result["payload"].decode("utf-8", errors="replace")), indent=2))
        return 0 if result["status"] in ("completed", "dispatched") else 2
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        print(f"Control refused: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

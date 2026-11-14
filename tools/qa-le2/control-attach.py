# SPDX-License-Identifier: MIT
"""Invite an already loaded monolithic LE2 diagnostic host into one protected run.

Does not deploy code, launch, dispatch commands, or claim the host answered.
The invitation is exclusive and is never automatically retried or overwritten.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import uuid

_spec = importlib.util.spec_from_file_location("le2_control", Path(__file__).with_name("control.py"))
c = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(c)
s = c.session


def invite(run, host, expected_hash, *, processes=s.windows_processes, modules=c.mapped_modules):
    run, context = s.load_context(run)
    if processes is s.windows_processes:
        s.require_native_context(context)
    if context.get("phase") != "game-bound" or not context.get("gameProcess"):
        raise c.ControlError("Attach requires a bound game process")
    if not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
        raise c.ControlError("Supply the reviewed diagnostic build's lowercase SHA256")
    expected = context["gameProcess"]
    live = processes()
    games = [p for p in live if p["name"].lower() in ("masseffect1", "masseffect2", "masseffect3")]
    if games != [expected] or expected["name"].lower() != "masseffect2":
        raise c.ControlError("Exclusive game process identity changed")
    exe = s.safe_path(context["exePath"])
    if s.safe_path(expected["path"]) != exe or s.digest(exe) != context["exeSha256"]:
        raise c.ControlError("Game executable differs from session")
    host = s.safe_path(host)
    mapped = {str(s.safe_path(p)).casefold() for p in modules(expected["pid"])}
    if str(host).casefold() not in mapped or s.digest(host) != expected_hash:
        raise c.ControlError("Reviewed host is not the actual mapped module")
    identity = dict(run=context["runId"], lease=str(uuid.uuid4()), pid=str(expected["pid"]),
                    start=c.filetime(expected["started"]), exe=context["exeSha256"],
                    host=expected_hash, mod=expected_hash, game="LE2")
    c.validate(dict(identity, kind="request", id=identity["lease"], sequence="1", deadline="1",
                    verb="ping", status="pending", bytes="0", payload=b""))
    directory = s.safe_path(run / "control")
    invitation = s.safe_path(Path(context["lockPath"]).parent / "le2-control.attach.json")
    if directory.exists() or invitation.exists():
        raise c.ControlError("Attach intent already exists; inspect it, never replace or automatically retry")
    descriptor = dict(schemaVersion=1, runDirectory=str(run), identity=identity,
                      hostPath=str(host), modPath=str(host))
    raw = (json.dumps(descriptor, indent=2) + "\n").encode("utf-8")
    # Intent precedes publication. An interruption leaves evidence and prevents retry.
    directory.mkdir()
    c.publish(directory / "attach-intent.json", raw)
    # Recheck ownership and process just before publication; runtime verifies independently.
    _, current = s.load_context(run)
    if current != context or processes() != live:
        raise c.ControlError("Ownership/process state changed before invitation publication")
    c.publish(invitation, raw)
    return dict(status="invitation-published-handshake-pending", ready=False,
                runId=context["runId"], lease=identity["lease"], invitation=str(invitation))


def cleanup(run, *, processes=s.windows_processes):
    """Remove only this run's exact invitation after verified reset and quiet desktop."""
    run, context = s.load_context(run, require_lock=False)
    if processes is s.windows_processes:
        s.require_native_context(context)
    if context.get("phase") != "restored" or not context.get("restorationVerified"):
        raise c.ControlError("Invitation cleanup requires verified session restoration")
    s.require_quiet(processes)
    invitation = s.safe_path(Path(context["lockPath"]).parent / "le2-control.attach.json")
    if not invitation.exists():
        return dict(status="invitation-already-absent")
    intent = s.safe_path(run / "control/attach-intent.json").read_bytes()
    descriptor = json.loads(intent)
    if descriptor.get("runDirectory") != str(run) or descriptor.get("identity", {}).get("run") != context["runId"]:
        raise c.ControlError("Attach intent belongs to another run")
    if invitation.read_bytes() != intent:
        raise c.ControlError("Shared invitation differs; another owner's file was preserved")
    invitation.unlink()
    return dict(status="owned-invitation-removed", runId=context["runId"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("action", choices=("invite", "cleanup"))
    parser.add_argument("--host", type=Path)
    parser.add_argument("--sha256")
    args = parser.parse_args()
    if args.action == "invite" and (not args.host or not args.sha256):
        parser.error("invite requires --host and --sha256")
    if args.action == "cleanup" and (args.host or args.sha256):
        parser.error("cleanup does not accept a host/hash")
    try:
        result = invite(args.run.absolute(), args.host.absolute(), args.sha256) if args.action == "invite" else cleanup(args.run.absolute())
        print(json.dumps(result, indent=2))
        return 0
    except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(f"Control attach refused: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

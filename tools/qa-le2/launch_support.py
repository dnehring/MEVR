# SPDX-License-Identifier: MIT
"""Bounded Steam-forwarding launch; never starts EA/game EXEs or selects a save.

The public CLI has no fake-host bypass. Tests inject providers through Python only.
Plan reads files/processes without writes, including no Python bytecode cache.
"""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import datetime as dt
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time

sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location("le2_launch_session", Path(__file__).with_name("session.py"))
s = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(s)

SOURCE_PIN = "1c435d8df0367642d9fc8572b67b6ae92295967e"
STEAM_ARGS = ["-applaunch", "1328670", "-game", "2", "-autoterminate"]


def issue(code, detail):
    return {"code": code, "detail": str(detail)}


def native_guard(context):
    guard = getattr(s, "require_native_context", None)
    if guard is None:
        raise s.SessionError("session.py native host guard is required; no environment-only fallback")
    guard(context)


def in_job(pid):
    """Fail closed if the running Steam's job membership cannot be inspected."""
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.IsProcessInJob.argtypes = [wintypes.HANDLE, wintypes.HANDLE, ctypes.POINTER(wintypes.BOOL)]
    kernel.IsProcessInJob.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x1000, False, pid)  # QUERY_LIMITED_INFORMATION
    if not handle:
        raise OSError(ctypes.get_last_error(), "Cannot inspect Steam process")
    try:
        result = wintypes.BOOL()
        if not kernel.IsProcessInJob(handle, None, ctypes.byref(result)):
            raise OSError(ctypes.get_last_error(), "Cannot inspect Steam job membership")
        return bool(result.value)
    finally:
        kernel.CloseHandle(handle)


def inventory():
    live = s.windows_processes()  # performs the shared real-desktop guard first
    script = "$ErrorActionPreference='Stop'; @(Get-Process -Name steam -ErrorAction SilentlyContinue | ForEach-Object { [pscustomobject]@{name=$_.ProcessName;pid=$_.Id;path=$_.Path;started=$_.StartTime.ToUniversalTime().ToString('o')} }) | ConvertTo-Json -Compress"
    result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
                            capture_output=True, text=True, check=True)
    steam = json.loads(result.stdout or "[]")
    steam = steam if isinstance(steam, list) else [steam]
    for process in steam:
        process["inJob"] = in_job(process["pid"])
    return live + steam


def check_files(run):
    run, context = s.load_context(run)
    plan = s.read_json(run / "plan.json")
    if not context.get("backupComplete") or context["phase"] not in ("staged", "load-requested"):
        raise s.SessionError("Launch requires a completely backed-up staged/load-requested session")
    game = s.safe_path(plan["gameRoot"])
    exe = s.safe_path(game / "Game/ME2/Binaries/Win64/MassEffect2.exe")
    target = s.relative_file(s.safe_path(plan["profileRoot"]), plan["fixture"]["targetRelativePath"])
    if s.safe_path(context["exePath"]) != exe or context["exeSha256"] != plan["exeSha256"]:
        raise s.SessionError("Executable identity differs from frozen plan")
    if s.safe_path(context["fixtureTarget"]) != target or context["fixtureSha256"] != plan["fixture"]["sha256"]:
        raise s.SessionError("Fixture identity differs from frozen plan")
    checks = [(exe, context["exeSha256"]), (target, context["fixtureSha256"]),
              (run / "fixture.pcsav", context["fixtureSha256"]),
              (run / "scenario.json", context["scenarioSha256"]),
              (run / "recipe.json", context["recipeSha256"]),
              (run / "preparation-receipt.json", context["preparationReceiptSha256"])]
    for path, expected in checks:
        if s.digest(s.safe_path(path)) != expected:
            raise s.SessionError(f"Prepared session artifact changed: {path.name}")
    for protected in context["protected"]:
        backup = s.relative_file(run, protected["backup"])
        if s.snapshot(backup) != protected["before"]:
            raise s.SessionError("Protected backup no longer matches its manifest")
    locks = s.safe_path(context["lockPath"]).parent
    if any(p.name != "qa-cafeteria.lock" for p in locks.glob("*.lock")):
        raise s.SessionError("Another shared session lock is present")
    if not context.get("coordinatedOwner"):
        raise s.SessionError("Session lacks coordinated owner")
    return run, context


def capability(receipt_path, context):
    if not receipt_path:
        raise s.SessionError("Reviewed installed launcher capability receipt is required; use the normal picker manually")
    path = s.safe_path(receipt_path)
    receipt = s.read_json(path)
    if (receipt.get("schemaVersion") != 1 or receipt.get("kind") != "reviewed-le2-launcher-selection"
            or receipt.get("provider") != "LEBinkProxy" or receipt.get("sourcePin") != SOURCE_PIN
            or receipt.get("arguments") != ["-game", "2", "-autoterminate"]
            or receipt.get("method") not in ("source-matched-build", "observed-game2-selection")
            or not str(receipt.get("reviewedBy", "")).strip()
            or not str(receipt.get("reviewNotes", "")).strip()):
        raise s.SessionError("Capability receipt needs attributed installed-binary review, not a support boolean")
    root = s.safe_path(context["gameRoot"]) / "Game/Launcher"
    for filename, key in (("MassEffectLauncher.exe", "launcherExeSha256"), ("bink2w64.dll", "proxySha256")):
        if s.digest(s.safe_path(root / filename)) != receipt.get(key):
            raise s.SessionError(f"Reviewed installed launcher file differs: {filename}")
    evidence = receipt.get("evidence")
    if not isinstance(evidence, list) or not evidence:
        raise s.SessionError("Capability receipt requires preserved review evidence")
    for artifact in evidence:
        source = s.relative_file(path.parent, artifact["path"])
        if s.digest(source) != artifact.get("sha256"):
            raise s.SessionError("Launcher capability evidence hash mismatch")
    return {"receiptSha256": s.digest(path), "sourcePin": SOURCE_PIN,
            "launcherExeSha256": receipt["launcherExeSha256"], "proxySha256": receipt["proxySha256"],
            "method": receipt["method"], "reviewedBy": receipt["reviewedBy"],
            "evidence": evidence, "nativeSelectionVerifiedByThisTool": False}


def check_processes(live, steam_path, context, *, polling=False):
    steam = [p for p in live if p["name"].lower() == "steam"]
    if len(steam) != 1 or not steam[0].get("path") or s.safe_path(steam[0]["path"]) != steam_path:
        raise s.SessionError("Exactly one already-running Steam at the specified executable path is required")
    if steam[0].get("inJob") is not False:
        raise s.SessionError("Running Steam must be verified outside every job object")
    for process in live:
        name = process["name"].lower()
        if name == "steam":
            continue
        if polling and name == "masseffect2":
            continue  # session.bind_process performs exact exe/hash/start checks
        if polling and name == "masseffectlauncher":
            expected = s.safe_path(Path(context["gameRoot"]) / "Game/Launcher/MassEffectLauncher.exe")
            if process.get("path") and s.safe_path(process["path"]) == expected:
                continue
        raise s.SessionError(f"Unowned game/launcher/VR process present: {name}:{process['pid']}")
    return steam[0]


def plan(run, steam_exe, receipt_path=None, *, host_guard=native_guard, processes=inventory):
    report = {"schemaVersion": 1, "game": "LE2", "mode": "plan", "status": "blocked",
              "canLaunch": False, "launched": False, "ready": False, "dependencies": [],
              "hostInventory": "not-inspected",
              "command": {"executable": str(steam_exe), "arguments": STEAM_ARGS},
              "transport": "forward-to-already-running-Steam-outside-job",
              "operatorBoundary": "Select the dedicated save through Load Game; channel handshake and gameplay readiness are separate."}
    try:
        run, context = check_files(run)
        report.update(runId=context["runId"], runDirectory=str(run), phase=context["phase"],
                      exePath=context["exePath"], exeSha256=context["exeSha256"],
                      fixtureTarget=context["fixtureTarget"], fixtureSha256=context["fixtureSha256"])
        steam = s.safe_path(steam_exe)
        if steam.name.lower() != "steam.exe" or not steam.is_file():
            raise s.SessionError("SteamExe must name an existing steam.exe")
        report["command"]["executable"] = str(steam)
        report["steamExeSha256"] = s.digest(steam)
        if (run / "launch-attempt.json").exists():
            report["dependencies"].append(issue("prior_attempt_requires_recovery", "No automatic retry; inspect the retained attempt and live process before recovery"))
    except (ValueError, OSError, KeyError, TypeError) as error:
        report["dependencies"].append(issue("invalid_prepared_session", error))
        return report
    try:
        report["launcherCapability"] = capability(receipt_path, context)
    except (ValueError, OSError, KeyError, TypeError) as error:
        report["dependencies"].append(issue("operator_selection_required", error))
    try:
        host_guard(context)
        report["steamProcess"] = check_processes(processes(), steam, context)
        report["hostInventory"] = "verified"
    except (ValueError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        report["hostInventory"] = "unavailable-or-blocked"
        report["dependencies"].append(issue("native_host_or_process_blocked", error))
    report["canLaunch"] = not report["dependencies"]
    report["status"] = "launch-admissible-not-ready" if report["canLaunch"] else "operator-required"
    return report


def forward_to_steam(command):
    # Same forwarding mechanism as launch-game.ps1 -Steam. The client is in the
    # harness job; the verified existing Steam outside it owns the launch chain.
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    return subprocess.Popen([command["executable"], *command["arguments"]], startupinfo=startup)


def launch(run, steam_exe, receipt_path, timeout=120, *, host_guard=native_guard,
           processes=inventory, dispatch=forward_to_steam, monotonic=time.monotonic, sleep=time.sleep):
    if not 1 <= timeout <= 180:
        raise s.SessionError("Timeout must be 1..180 seconds")
    report = plan(run, steam_exe, receipt_path, host_guard=host_guard, processes=processes)
    report["mode"] = "launch"
    if not report["canLaunch"]:
        return report
    run = s.safe_path(run)
    attempt = run / "launch-attempt.json"
    # Reserve before dispatch. An interruption leaves a durable uncertainty marker.
    report.update(status="dispatch-pending", attemptedAt=s.utc(), processBound=False,
                  preflightAdmissible=True, canLaunch=False)
    with attempt.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.flush()
        os.fsync(stream.fileno())
    try:
        _, context = check_files(run)
        host_guard(context)
        check_processes(processes(), s.safe_path(steam_exe), context)
        capability(receipt_path, context)
        if context["phase"] == "staged":
            s.request_load(run, processes=lambda: [p for p in processes() if p["name"].lower() != "steam"])
        # Recheck all guards immediately before the one external request.
        _, context = check_files(run)
        host_guard(context)
        current = check_processes(processes(), s.safe_path(steam_exe), context)
        if current != report["steamProcess"] or s.digest(steam_exe) != report["steamExeSha256"]:
            raise s.SessionError("Steam identity changed before dispatch")
        if capability(receipt_path, context) != report["launcherCapability"]:
            raise s.SessionError("Launcher capability changed before dispatch")
        report["status"] = "dispatch-uncertain"
        report["dispatchAt"] = s.utc()
        s.write_json(attempt, report)
        dispatch(report["command"])
        report["status"] = "steam-requested"
        report["launched"] = False  # Steam accepting a request does not prove game launch.
        s.write_json(attempt, report)
        deadline = monotonic() + timeout
        while monotonic() < deadline:
            _, context = s.load_context(run)
            host_guard(context)
            live = processes()
            current = check_processes(live, s.safe_path(steam_exe), context, polling=True)
            if current != report["steamProcess"]:
                raise s.SessionError("Steam owner changed while waiting")
            games = [p for p in live if p["name"].lower() == "masseffect2"]
            if games:
                if len(games) != 1:
                    raise s.SessionError("Multiple ME2 processes appeared")
                started = dt.datetime.fromisoformat(games[0]["started"].replace("Z", "+00:00"))
                if started <= dt.datetime.fromisoformat(report["dispatchAt"]):
                    raise s.SessionError("ME2 process predates this launch request")
                bound = s.bind_process(run, games[0]["pid"], processes=lambda: [p for p in live if p["name"].lower() != "steam"])
                report.update(status="process-bound-operator-load-required", launched=True,
                              processBound=True, gameProcess=bound["gameProcess"])
                break
            sleep(min(1, max(0, deadline - monotonic())))
        else:
            report.update(status="timeout-operator-required")
            report["dependencies"].append(issue("selection_or_launch_unobserved", "No exact ME2 process observed; no keyboard fallback, retry or automatic termination"))
    except (ValueError, OSError, KeyError, TypeError, RuntimeError, subprocess.SubprocessError) as error:
        report.update(status="failed-operator-required")
        report["dependencies"].append(issue("launch_failed", error))
    report["finishedAt"] = s.utc()
    s.write_json(attempt, report)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("plan", "launch"))
    parser.add_argument("--run", required=True)
    parser.add_argument("--steam-exe", required=True)
    parser.add_argument("--capability-receipt")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args(argv)
    try:
        if args.mode == "plan":
            result = plan(args.run, args.steam_exe, args.capability_receipt)
        else:
            result = launch(args.run, args.steam_exe, args.capability_receipt, args.timeout)
    except (ValueError, OSError, KeyError, TypeError) as error:
        result = {"schemaVersion": 1, "game": "LE2", "status": "error", "ready": False,
                  "dependencies": [issue("invalid_invocation_or_session", error)]}
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0 if args.mode == "plan" or result.get("processBound") else 2


if __name__ == "__main__":
    raise SystemExit(main())

# SPDX-License-Identifier: MIT
"""Protected LE2 fixture sessions. See --help; never changes the OpenXR runtime.

prepare backs up the complete MELE user profile, LE2 config and Win64 directory
before staging a frozen fixture. request-load records an operator handoff;
the operator starts the normal launcher and selects the staged save through Load Game. attach records
evidence without declaring the scenario passed. reset archives test output and
restores the original bytes, directory presence and file timestamps.

Native state/VR acceptance is evaluated by scenario.py, not this lifecycle tool.
Only the Python API accepts injected process/lock providers for isolated tests.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import uuid

SCHEMA = 1


class SessionError(ValueError):
    pass


def utc():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def write_json(path, value):
    path = Path(path)
    temp = path.with_name(path.name + ".tmp")
    with temp.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temp, path)


def is_link(path):
    path = Path(path)
    if path.is_symlink():
        return True
    try:
        return bool(getattr(path.lstat(), "st_file_attributes", 0) &
                    getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400))
    except FileNotFoundError:
        return False


def safe_path(path):
    """Resolve absolute paths only after refusing links in every ancestor."""
    path = Path(path)
    if not path.is_absolute():
        raise SessionError(f"Expected absolute path: {path}")
    for entry in (path, *path.parents):
        if is_link(entry):
            raise SessionError(f"Symlink/reparse path refused: {entry}")
    return path.resolve()


def inside(path, root):
    return path == root or root in path.parents


def relative_file(root, value):
    # Reject Windows forms even when tests run on POSIX.
    text = str(value).replace("\\", "/")
    parts = text.split("/")
    if not text or text.startswith("/") or ":" in text or any(p in ("", ".", "..") for p in parts):
        raise SessionError(f"Unsafe relative file: {value}")
    result = safe_path(root.joinpath(*parts))
    if not inside(result, root) or result == root:
        raise SessionError("Target escaped protected root")
    return result


def snapshot(path):
    """Record tree shape and content. Reject every reparse point before copying."""
    path = safe_path(path)
    if not path.exists():
        return {"kind": "absent", "files": {}, "directories": []}
    if path.is_file():
        info = path.stat()
        return {"kind": "file", "files": {".": {"sha256": digest(path), "bytes": info.st_size,
                                                     "mtimeNs": info.st_mtime_ns}}, "directories": []}
    if not path.is_dir():
        raise SessionError(f"Unsupported file type: {path}")
    files, dirs = {}, []
    for parent, children, names in os.walk(path, followlinks=False):
        for name in children + names:
            child = Path(parent) / name
            if is_link(child):
                raise SessionError(f"Symlink/reparse entry refused: {child}")
        for name in children:
            dirs.append((Path(parent) / name).relative_to(path).as_posix())
        for name in names:
            child = Path(parent) / name
            if not child.is_file():
                raise SessionError(f"Unsupported file type: {child}")
            info = child.stat()
            files[child.relative_to(path).as_posix()] = {
                "sha256": digest(child), "bytes": info.st_size, "mtimeNs": info.st_mtime_ns}
    return {"kind": "directory", "files": dict(sorted(files.items())), "directories": sorted(dirs)}


def same_content(a, b):
    return a == b


def copy_snapshot(source, destination, manifest):
    destination = Path(destination)
    if destination.exists():
        raise SessionError(f"Archive destination already exists: {destination}")
    if manifest["kind"] == "absent":
        return
    if manifest["kind"] == "file":
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    else:
        destination.mkdir(parents=True)
        for relative in manifest["directories"]:
            relative_file(destination, relative).mkdir(parents=True, exist_ok=True)
        for relative in manifest["files"]:
            target = relative_file(destination, relative)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(relative_file(Path(source), relative), target)
    if not same_content(snapshot(destination), manifest):
        raise SessionError(f"Copy verification failed: {source}")


def stage_fixture(source, target):
    """Never clobber a slot created after preflight, even during a long backup."""
    target = safe_path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    with Path(source).open("rb") as reader, target.open("xb") as writer:
        shutil.copyfileobj(reader, writer)
        writer.flush()
        os.fsync(writer.fileno())
    shutil.copystat(source, target)


def windows_processes():
    if os.name != "nt":
        raise SessionError("Native sessions require Windows; use test-session.py for offline tests")
    # Fixed command: no input paths or user-supplied strings enter PowerShell source.
    script = "$ErrorActionPreference='Stop'; @(Get-Process | Where-Object { $_.ProcessName -match '^(MassEffect[123]|MassEffectLauncher|MEVR-VRLauncher|vrmonitor|vrserver|vrcompositor|vrdashboard|vrwebhelper|vrstartup)$' } | ForEach-Object { [pscustomobject]@{name=$_.ProcessName;pid=$_.Id;path=$_.Path;started=$_.StartTime.ToUniversalTime().ToString('o')} }) | ConvertTo-Json -Compress"
    result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
                            capture_output=True, text=True, check=True)
    data = json.loads(result.stdout or "[]")
    return data if isinstance(data, list) else [data]


def require_quiet(processes):
    live = processes()
    if live:
        raise SessionError("Game/launcher/VR session is active; coordinate and close it before staging or restoration: " +
                           ", ".join(f"{p['name']}:{p['pid']}" for p in live))


def default_locks():
    if not os.environ.get("LOCALAPPDATA"):
        raise SessionError("LOCALAPPDATA is required for the shared session lock")
    return safe_path(Path(os.environ["LOCALAPPDATA"]) / "MEVR")


def load_scenario(path):
    module_path = Path(__file__).with_name("scenario.py")
    if not module_path.is_file():
        raise SessionError("scenario.py is required to validate the bound scenario")
    spec = importlib.util.spec_from_file_location("le2_qa_scenario", module_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.load(path, for_run=True)


def validate_plan(plan, run, scenario_loader=load_scenario):
    if plan.get("schemaVersion") != SCHEMA:
        raise SessionError("Unknown session plan version")
    if not isinstance(plan.get("coordinatedOwner"), str) or not plan["coordinatedOwner"].strip():
        raise SessionError("coordinatedOwner must identify the explicitly coordinated session owner")
    profile = safe_path(plan["profileRoot"])
    game = safe_path(plan["gameRoot"])
    run = safe_path(run)
    if run.exists():
        raise SessionError("Use a new run directory")
    if not profile.is_dir():
        raise SessionError("Complete MELE profile root must exist")
    exe = safe_path(game / "Game/ME2/Binaries/Win64/MassEffect2.exe")
    if not exe.is_file() or digest(exe) != plan.get("exeSha256", "").lower():
        raise SessionError("Reviewed LE2 executable hash mismatch")
    frozen = safe_path(plan["fixture"]["path"])
    if not frozen.is_file() or digest(frozen) != plan["fixture"].get("sha256", "").lower():
        raise SessionError("Prepared fixture hash mismatch")
    target = relative_file(profile, plan["fixture"]["targetRelativePath"])
    if target.suffix.lower() != ".pcsav" or not inside(target, profile / "Save/ME2"):
        raise SessionError("Fixture target must be a .pcsav beneath profileRoot/Save/ME2")
    if target.exists():
        raise SessionError("Fixture target must be a new dedicated save slot")
    scenario_path = safe_path(plan["scenario"]["path"])
    if digest(scenario_path) != plan["scenario"].get("sha256", "").lower():
        raise SessionError("Scenario hash mismatch")
    scenario = scenario_loader(scenario_path)
    recipe = safe_path(plan["recipePath"])
    receipt_path = safe_path(plan["fixture"]["receiptPath"])
    receipt = read_json(receipt_path)
    if (receipt.get("schema_version") != 1 or receipt.get("operation") != "prepare" or
            receipt.get("game") != "LE2" or receipt.get("save_version") != 30 or
            receipt.get("input_sha256") != scenario["fixture"]["baseSaveSha256"] or
            receipt.get("output_sha256") != digest(frozen) or
            receipt.get("recipe_sha256") != digest(recipe) or
            digest(recipe) != scenario["recipe"]["sha256"]):
        raise SessionError("Preparation receipt does not bind the scenario base, recipe and prepared fixture")
    protected = [profile, game / "Game/ME2/BioGame/Config", game / "Game/ME2/Binaries/Win64"]
    protected.extend(safe_path(p) for p in plan.get("additionalProtectedPaths", []))
    protected = [safe_path(p) for p in protected]
    for i, path in enumerate(protected):
        if path.parent == path or inside(run, path) or inside(path, run):
            raise SessionError("Run directory and protected roots must be separate, non-root paths")
        for previous in protected[:i]:
            if inside(path, previous) or inside(previous, path):
                raise SessionError("Protected paths overlap")
    if inside(frozen, run) or inside(scenario_path, run):
        raise SessionError("Inputs cannot be inside new run directory")
    return profile, game, exe, frozen, target, scenario_path, scenario, protected


def prepare(plan_path, run, *, processes=windows_processes, locks=None, scenario_loader=load_scenario):
    plan_path = safe_path(plan_path)
    plan = read_json(plan_path)
    profile, game, exe, fixture, target, scenario_path, scenario, protected = validate_plan(plan, run, scenario_loader)
    run = safe_path(run)
    locks = safe_path(locks) if locks is not None else default_locks()
    if any(inside(locks, p) or inside(p, locks) for p in protected) or inside(locks, run) or inside(run, locks):
        raise SessionError("Shared locks must be separate from protected/run roots")
    require_quiet(processes)
    locks.mkdir(parents=True, exist_ok=True)
    if list(locks.glob("*.lock")) or (locks / "game-slot/lock.json").exists():
        raise SessionError("An existing QA/session lock requires its owner's release or recovery")
    lock = locks / "qa-cafeteria.lock"  # interoperates with existing ME1 QA runners
    with lock.open("x", encoding="utf-8") as stream:
        stream.write(str(run))
        stream.flush()
        os.fsync(stream.fileno())
    mutation_started = False
    try:
        require_quiet(processes)  # close acquisition race before backup
        if (locks / "game-slot/lock.json").exists():
            raise SessionError("Game-slot owner appeared during acquisition")
        run.mkdir(parents=True)
        context = {"schemaVersion": SCHEMA, "runId": str(uuid.uuid4()), "created": utc(),
                   "phase": "backing-up", "coordinatedOwner": plan["coordinatedOwner"],
                   "runDirectory": str(run), "lockPath": str(lock), "profileRoot": str(profile),
                   "gameRoot": str(game), "exePath": str(exe), "exeSha256": digest(exe),
                   "fixtureTarget": str(target), "fixtureSha256": digest(fixture),
                   "scenarioSha256": digest(scenario_path), "scenarioId": scenario["id"],
                   "planSha256": digest(plan_path),
                   "protected": [], "evidence": [], "nativeAcceptance": "pending", "vrAcceptance": "pending"}
        write_json(run / "context.json", context)
        shutil.copy2(plan_path, run / "plan.json")
        for index, path in enumerate(protected):
            before = snapshot(path)
            backup = run / "before" / str(index)
            copy_snapshot(path, backup, before)
            context["protected"].append({"path": str(path), "backup": f"before/{index}", "before": before})
            write_json(run / "context.json", context)
        # Freeze both inputs before any profile mutation.
        shutil.copy2(fixture, run / "fixture.pcsav")
        shutil.copy2(scenario_path, run / "scenario.json")
        recipe = safe_path(plan["recipePath"])
        receipt = safe_path(plan["fixture"]["receiptPath"])
        shutil.copy2(recipe, run / "recipe.json")
        shutil.copy2(receipt, run / "preparation-receipt.json")
        context["recipeSha256"] = digest(recipe)
        context["preparationReceiptSha256"] = digest(receipt)
        if digest(run / "recipe.json") != scenario["recipe"]["sha256"]:
            raise SessionError("Recipe changed while freezing")
        frozen_receipt = read_json(run / "preparation-receipt.json")
        if (frozen_receipt.get("input_sha256") != scenario["fixture"]["baseSaveSha256"] or
                frozen_receipt.get("output_sha256") != context["fixtureSha256"] or
                frozen_receipt.get("recipe_sha256") != context["recipeSha256"]):
            raise SessionError("Preparation receipt changed while freezing")
        shutil.copy2(Path(__file__), run / "session-source.py")
        if digest(run / "fixture.pcsav") != context["fixtureSha256"] or digest(run / "scenario.json") != context["scenarioSha256"]:
            raise SessionError("Input changed while freezing")
        context["phase"] = "staging"
        context["backupComplete"] = True
        write_json(run / "context.json", context)
        require_quiet(processes)
        load_context(run)
        mutation_started = True
        stage_fixture(run / "fixture.pcsav", target)
        if digest(target) != context["fixtureSha256"]:
            raise SessionError("Staged fixture hash mismatch")
        context["phase"] = "staged"
        context["stagedAt"] = utc()
        write_json(run / "context.json", context)
        return context
    except BaseException:
        # Before mutation, original state is intact. After mutation retain lock and
        # complete backups; reset is explicit and refuses restoration into a live game.
        if not mutation_started and lock.exists() and lock.read_text(encoding="utf-8") == str(run):
            lock.unlink()
        raise


def load_context(run, require_lock=True):
    run = safe_path(run)
    context = read_json(safe_path(run / "context.json"))
    if context.get("schemaVersion") != SCHEMA or context.get("runDirectory") != str(run):
        raise SessionError("Invalid session context")
    if digest(safe_path(run / "plan.json")) != context["planSha256"]:
        raise SessionError("Frozen session plan changed")
    plan = read_json(run / "plan.json")
    profile, game = safe_path(plan["profileRoot"]), safe_path(plan["gameRoot"])
    expected = [profile, game / "Game/ME2/BioGame/Config", game / "Game/ME2/Binaries/Win64"]
    expected.extend(safe_path(p) for p in plan.get("additionalProtectedPaths", []))
    if context.get("backupComplete") and [str(p) for p in expected] != [p["path"] for p in context["protected"]]:
        raise SessionError("Protected roots differ from frozen session plan")
    if context["profileRoot"] != str(profile) or context["gameRoot"] != str(game):
        raise SessionError("Session roots differ from frozen plan")
    lock = safe_path(context["lockPath"])
    if lock.name != "qa-cafeteria.lock":
        raise SessionError("Unexpected shared lock path")
    if require_lock and (not lock.is_file() or lock.read_text(encoding="utf-8") != str(run)):
        raise SessionError("Run does not own the shared lock")
    if require_lock and (lock.parent / "game-slot/lock.json").exists():
        raise SessionError("Game-slot ownership appeared; coordinate its release before continuing")
    return run, context


def request_load(run, *, processes=windows_processes):
    """Record the manual launcher boundary; never start an EA process inside a harness job."""
    run, context = load_context(run)
    if context["phase"] != "staged":
        raise SessionError("Load request requires a staged session")
    require_quiet(processes)
    exe = safe_path(context["exePath"])
    if digest(exe) != context["exeSha256"] or digest(context["fixtureTarget"]) != context["fixtureSha256"]:
        raise SessionError("Executable or staged fixture changed")
    context["phase"] = "load-requested"
    context["loadRequestedAt"] = utc()
    context["loadInstructions"] = ("Start Mass Effect 2 through the normal Steam/EA launcher outside this harness; "
                                   "bind its exact process, then select this dedicated save through Load Game: " +
                                   context["fixtureTarget"] + ". Automatic profile selection is unproven.")
    write_json(run / "context.json", context)
    return context


def bind_process(run, pid, *, processes=windows_processes):
    """Bind the actual game after Steam/EA startup; never infer ownership from PID alone."""
    run, context = load_context(run)
    if context["phase"] not in ("load-requested", "game-bound"):
        raise SessionError("Process binding requires load-requested session")
    live = processes()
    chosen = [p for p in live if p["pid"] == pid]
    if len(chosen) != 1 or chosen[0]["name"].lower() != "masseffect2":
        raise SessionError("PID is not a live ME2 process")
    entry = chosen[0]
    if not entry.get("path") or safe_path(entry["path"]) != safe_path(context["exePath"]):
        raise SessionError("Process executable path differs")
    if digest(entry["path"]) != context["exeSha256"]:
        raise SessionError("Process executable hash differs")
    if dt.datetime.fromisoformat(entry["started"].replace("Z", "+00:00")) <= dt.datetime.fromisoformat(context["stagedAt"]):
        raise SessionError("Process predates this staged session")
    games = [p for p in live if p["name"].lower() in {"masseffect1", "masseffect2", "masseffect3"}]
    if len(games) != 1:
        raise SessionError("Another game process is present")
    context["gameProcess"] = entry
    context["phase"] = "game-bound"
    context["boundAt"] = utc()
    write_json(run / "context.json", context)
    return context


def attach(run, files):
    """Archive evidence, never accept it as a pass. Evaluation belongs to scenario.py."""
    run, context = load_context(run)
    if context["phase"] not in ("staged", "load-requested", "game-bound"):
        raise SessionError("Session is not active")
    result = []
    for file in files:
        file = safe_path(file)
        if not file.is_file() or inside(file, run):
            raise SessionError("Evidence must be a separate regular input file")
        stamp = file.stat().st_mtime_ns
        sha = digest(file)
        out = run / "evidence" / f"{len(context['evidence']) + len(result):04d}-{file.name}"
        out.parent.mkdir(exist_ok=True)
        if out.exists():
            raise SessionError("Evidence destination already exists; preserve interrupted output")
        shutil.copy2(file, out)
        if digest(out) != sha:
            raise SessionError("Evidence changed while copying")
        result.append({"file": out.relative_to(run).as_posix(), "sha256": sha,
                       "sourceMtimeNs": stamp, "collectedAt": utc()})
    context["evidence"].extend(result)
    context["evidenceCollectedAt"] = utc()
    write_json(run / "context.json", context)
    return context


def reset(run, *, processes=windows_processes):
    run, context = load_context(run, require_lock=False)
    if context["phase"] == "restored" and not Path(context["lockPath"]).exists():
        require_quiet(processes)
        if any(snapshot(item["path"]) != item["before"] for item in context["protected"]):
            raise SessionError("Already restored session subsequently changed; do not reset newer user data")
        return context
    run, context = load_context(run)
    if not context.get("backupComplete"):
        raise SessionError("Backup was incomplete; no automatic restoration is permitted")
    require_quiet(processes)
    # Preflight every original backup and destination before moving any live data.
    for item in context["protected"]:
        path = safe_path(item["path"])
        backup = relative_file(run, item["backup"])
        if inside(run, path) or inside(path, run) or path.parent == path:
            raise SessionError("Unsafe restoration target")
        if not same_content(snapshot(backup), item["before"]):
            raise SessionError(f"Backup changed; restore refused: {backup}")
        snapshot(path)  # rejects new reparse points
    context["phase"] = "restoring"
    write_json(run / "context.json", context)
    for index, item in enumerate(context["protected"]):
        load_context(run)  # ownership may change while a large archive is copied
        path = safe_path(item["path"])
        # Retry-safe: each attempt preserves the currently present tree. Existing
        # prior archives are never overwritten or deleted after interrupted reset.
        archive = run / "after" / f"{index}-{uuid.uuid4().hex}"
        current = snapshot(path)
        if current["kind"] != "absent":
            copy_snapshot(path, archive, current)
        archive_entry = {"path": archive.relative_to(run).as_posix(), "state": current,
                         "archivedAt": utc(), "restored": False}
        item.setdefault("archives", []).append(archive_entry)
        write_json(run / "context.json", context)
        if current["kind"] != "absent":
            require_quiet(processes)
            load_context(run)
            if snapshot(path) != current:
                raise SessionError("Protected data changed during archival; retry with quiet ownership")
            safe_path(path)
            # Removal is confined to the explicitly protected and preflighted target.
            if path.is_dir():
                shutil.rmtree(path)
            else:
                path.unlink()
        backup = relative_file(run, item["backup"])
        copy_snapshot(backup, path, item["before"])
        if not same_content(snapshot(path), item["before"]):
            raise SessionError(f"Restoration verification failed: {path}")
        item["restoredAt"] = utc()
        archive_entry["restored"] = True
        write_json(run / "context.json", context)
    context["phase"] = "restored"
    context["restoredAt"] = utc()
    context["restorationVerified"] = True
    write_json(run / "context.json", context)
    lock = safe_path(context["lockPath"])
    if lock.read_text(encoding="utf-8") != str(run):
        raise SessionError("Shared lock ownership changed; not releasing")
    lock.unlink()
    return context


def status(run):
    run, context = load_context(run, require_lock=False)
    return {"runId": context["runId"], "phase": context["phase"], "scenarioId": context["scenarioId"],
            "fixtureSha256": context["fixtureSha256"], "nativeAcceptance": "pending",
            "vrAcceptance": "pending", "restorationVerified": context.get("restorationVerified", False),
            "instructions": "Select the staged save through normal Load Game; record actual scene/state and inputs. "
                            "Use scenario.py evidence evaluation. Close owned game/VR processes before reset."}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("prepare", help="Acquire coordinated lock, back up and stage a ready fixture")
    p.add_argument("plan", type=Path)
    p.add_argument("run", type=Path)
    for command in ("request-load", "status", "reset"):
        p = sub.add_parser(command)
        p.add_argument("run", type=Path)
    p = sub.add_parser("bind-process", help="Record exact live ME2 process identity after normal startup")
    p.add_argument("run", type=Path)
    p.add_argument("pid", type=int)
    p = sub.add_parser("attach", help="Archive evidence without claiming acceptance")
    p.add_argument("run", type=Path)
    p.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "prepare":
            result = prepare(args.plan.absolute(), args.run.absolute())
        elif args.command == "bind-process":
            result = bind_process(args.run.absolute(), args.pid)
        elif args.command == "attach":
            result = attach(args.run.absolute(), [p.absolute() for p in args.files])
        else:
            result = globals()[args.command.replace("-", "_")](args.run.absolute())
        print(json.dumps(result, indent=2))
        return 0
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())


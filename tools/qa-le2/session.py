# SPDX-License-Identifier: MIT
"""Protected LE2 fixture sessions. See --help; never changes the OpenXR runtime.

prepare backs up the complete MELE user profile, LE2 config and Win64 directory
before staging a frozen fixture. request-load records an operator handoff;
the operator starts the normal launcher and selects the staged save through Load Game. attach records
evidence without declaring the scenario passed. reset archives test output and
restores the original bytes, directory presence and file timestamps.
archived-saves inventories verified LE2 save bytes from a completed reset.

Native state/VR acceptance is evaluated by scenario.py, not this lifecycle tool.
Only the Python API accepts injected process/lock providers for isolated tests.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import ntpath
import os
from pathlib import Path
import re
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


def _local_appdata_key(value):
    """Compare Windows paths without consulting a possibly redirected filesystem."""
    if not isinstance(value, str) or not value or not ntpath.isabs(value) or not ntpath.splitdrive(value)[0]:
        raise SessionError("Cannot establish desktop LOCALAPPDATA: missing or non-absolute path")
    normalized = ntpath.normcase(ntpath.normpath(value))
    parts = normalized.split("\\")
    # Actual observed failure: ...\Packages\sandbox.{GUID}\AC. Reject before
    # invoking any OS or process probe; a matching redirected known folder does
    # not turn this package-scoped view into desktop evidence.
    if any(part == "packages" and i + 1 < len(parts) and parts[i + 1].startswith("sandbox.")
           for i, part in enumerate(parts)):
        raise SessionError("Sandbox-redirection scoped LOCALAPPDATA; native lifecycle requires the approved desktop host context")
    return normalized


def _windows_host_evidence():
    """Read process-token scope and unredirected Known Folder; never change them."""
    if os.name != "nt":
        raise SessionError("Native sessions require Windows; use injected providers for offline tests")
    import ctypes
    from ctypes import wintypes

    class GUID(ctypes.Structure):
        _fields_ = [("data1", wintypes.DWORD), ("data2", wintypes.WORD),
                    ("data3", wintypes.WORD), ("data4", ctypes.c_ubyte * 8)]

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    advapi = ctypes.WinDLL("advapi32", use_last_error=True)
    shell = ctypes.WinDLL("shell32", use_last_error=True)
    ole = ctypes.WinDLL("ole32", use_last_error=True)
    kernel.GetCurrentProcess.argtypes = []
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    advapi.OpenProcessToken.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE)]
    advapi.OpenProcessToken.restype = wintypes.BOOL
    advapi.GetTokenInformation.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                          wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    advapi.GetTokenInformation.restype = wintypes.BOOL
    advapi.IsTokenRestricted.argtypes = [wintypes.HANDLE]
    advapi.IsTokenRestricted.restype = wintypes.BOOL
    shell.SHGetKnownFolderPath.argtypes = [ctypes.POINTER(GUID), wintypes.DWORD, wintypes.HANDLE,
                                          ctypes.POINTER(ctypes.c_void_p)]
    shell.SHGetKnownFolderPath.restype = wintypes.LONG
    ole.CoTaskMemFree.argtypes = [ctypes.c_void_p]
    ole.CoTaskMemFree.restype = None
    token = wintypes.HANDLE()
    if not advapi.OpenProcessToken(kernel.GetCurrentProcess(), 0x0008, ctypes.byref(token)):  # TOKEN_QUERY
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        app_container, size = wintypes.DWORD(), wintypes.DWORD()
        if not advapi.GetTokenInformation(token, 29, ctypes.byref(app_container),  # TokenIsAppContainer
                                          ctypes.sizeof(app_container), ctypes.byref(size)):
            raise ctypes.WinError(ctypes.get_last_error())
        if size.value != ctypes.sizeof(app_container) or app_container.value not in (0, 1):
            raise SessionError("Unexpected TokenIsAppContainer response; native host scope is unknown")
        ctypes.set_last_error(0)
        restricted = bool(advapi.IsTokenRestricted(token))
        if not restricted and ctypes.get_last_error():
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        kernel.CloseHandle(token)
    folder_id = GUID.from_buffer_copy(uuid.UUID("f1b32785-6fba-4fcf-9d55-7b8e7f157091").bytes_le)
    address = ctypes.c_void_p()
    try:
        result = shell.SHGetKnownFolderPath(ctypes.byref(folder_id), 0x00010000, None, ctypes.byref(address))
        # KF_FLAG_NO_PACKAGE_REDIRECTION, deliberately no KF_FLAG_CREATE.
        if result != 0 or not address.value:
            raise SessionError(f"Unredirected LocalAppData lookup failed (HRESULT {result & 0xffffffff:08x})")
        local_appdata = ctypes.wstring_at(address)
    finally:
        if address.value:
            ole.CoTaskMemFree(address)
    return {"localAppData": local_appdata, "appContainer": bool(app_container.value), "restricted": restricted}


def require_native_host():
    """Return verified desktop LocalAppData, or refuse ambiguous/scoped context.

    This is a narrow guard against known redirection/restricted-token evidence,
    not proof of complete process visibility, desktop ownership or quietness.
    No environment repair, elevation, folder creation or inventory happens here.
    """
    environment = _local_appdata_key(os.environ.get("LOCALAPPDATA"))
    try:
        evidence = _windows_host_evidence()
    except (OSError, AttributeError) as error:
        raise SessionError(f"Cannot verify desktop host context: {error}") from error
    if type(evidence.get("appContainer")) is not bool or type(evidence.get("restricted")) is not bool:
        raise SessionError("Cannot verify desktop host token scope")
    if evidence["appContainer"] or evidence["restricted"]:
        raise SessionError("AppContainer/restricted process token; native lifecycle requires the approved desktop host context")
    canonical = _local_appdata_key(evidence.get("localAppData"))
    if environment != canonical:
        raise SessionError("LOCALAPPDATA differs from Windows unredirected LocalAppData; desktop process/coordination scope is unverified")
    return safe_path(Path(evidence["localAppData"]))


def require_native_context(context):
    """Require this frozen run's lock to use the verified desktop coordination root."""
    root = default_locks()
    if safe_path(context["lockPath"]) != root / "qa-cafeteria.lock":
        raise SessionError("Frozen run uses a different desktop coordination root; review recovery in the approved host context")
    return root


def windows_processes():
    desktop = require_native_host()
    # Fixed command: no input paths or user-supplied strings enter PowerShell source.
    script = "$ErrorActionPreference='Stop'; $inventory=@(Get-Process | Where-Object { $_.ProcessName -match '^(MassEffect[123]|MassEffectLauncher|MEVR-VRLauncher|vrmonitor|vrserver|vrcompositor|vrdashboard|vrwebhelper|vrstartup)$' } | ForEach-Object { [pscustomobject]@{name=$_.ProcessName;pid=$_.Id;path=$_.Path;started=$_.StartTime.ToUniversalTime().ToString('o')} }); [pscustomobject]@{localAppData=$env:LOCALAPPDATA;processes=$inventory} | ConvertTo-Json -Depth 4 -Compress"
    result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
                            capture_output=True, text=True, check=True)
    try:
        data = json.loads(result.stdout)
    except (ValueError, TypeError) as error:
        raise SessionError("Missing/malformed desktop process inventory; cannot infer quietness") from error
    if not isinstance(data, dict) or _local_appdata_key(data.get("localAppData")) != _local_appdata_key(str(desktop)):
        raise SessionError("Process-inventory shell has a different LOCALAPPDATA scope")
    inventory = data.get("processes")
    if not isinstance(inventory, list) or any(not isinstance(p, dict) or not isinstance(p.get("name"), str)
                                             or type(p.get("pid")) is not int or p["pid"] <= 0 for p in inventory):
        raise SessionError("Invalid desktop process inventory; cannot infer quietness")
    return inventory


# Stable identity even when test code patches a module attribute. Only callers
# explicitly supplying fake providers opt into the offline dependency-injection path.
_NATIVE_PROCESSES = windows_processes


def require_quiet(processes):
    live = processes()
    if live:
        raise SessionError("Game/launcher/VR session is active; coordinate and close it before staging or restoration: " +
                           ", ".join(f"{p['name']}:{p['pid']}" for p in live))


def default_locks():
    return safe_path(require_native_host() / "MEVR")


def load_scenario(path):
    module_path = Path(__file__).with_name("scenario.py")
    if not module_path.is_file():
        raise SessionError("scenario.py is required to validate the bound scenario")
    spec = importlib.util.spec_from_file_location("le2_qa_scenario", module_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.load(path, for_run=True)


def validate_fixture_selection_text(plan):
    """Catch host text limits before backups; the host still checks the full binding."""
    if "fixtureSelection" not in plan:
        return
    selection = plan["fixtureSelection"]
    if not isinstance(selection, dict):
        raise SessionError("fixtureSelection must be an object")
    descriptor, evidence = selection.get("descriptor"), selection.get("evidence")
    if not isinstance(descriptor, dict) or not isinstance(evidence, dict):
        raise SessionError("fixtureSelection descriptor and evidence must be objects")
    # Keep these limits aligned with Le2ControlHost.cpp DescriptorComponent and
    # FrozenFixture. C++ string::size counts UTF-8 bytes, not Python characters.
    for field, value, max_bytes, max_units in (
            ("descriptor.career", descriptor.get("career"), 1024, 255),
            ("descriptor.saveName", descriptor.get("saveName"), 1024, 255),
            ("evidence.reviewedBy", evidence.get("reviewedBy"), 256, None)):
        label = "fixtureSelection." + field
        if not isinstance(value, str) or not value or "\0" in value:
            raise SessionError(f"{label} must be nonempty text without NUL")
        try:
            byte_count = len(value.encode("utf-8"))
            unit_count = len(value.encode("utf-16-le")) // 2
        except UnicodeEncodeError as exc:
            raise SessionError(f"{label} must be strict Unicode text") from exc
        if byte_count > max_bytes:
            raise SessionError(f"{label} exceeds {max_bytes} UTF-8 bytes")
        if max_units is not None and unit_count > max_units:
            raise SessionError(f"{label} exceeds {max_units} UTF-16 code units")
        if max_units is None and not value.strip(" \t\r\n"):
            raise SessionError(f"{label} must identify the reviewer and mapping basis")


def validate_plan(plan, run, scenario_loader=load_scenario):
    if plan.get("schemaVersion") != SCHEMA:
        raise SessionError("Unknown session plan version")
    if not isinstance(plan.get("coordinatedOwner"), str) or not plan["coordinatedOwner"].strip():
        raise SessionError("coordinatedOwner must identify the explicitly coordinated session owner")
    validate_fixture_selection_text(plan)
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
    native_locks = default_locks() if processes is _NATIVE_PROCESSES else None
    if native_locks is not None and locks is not None and safe_path(locks) != native_locks:
        raise SessionError("Native lifecycle must use the verified desktop coordination root")
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
    if processes is _NATIVE_PROCESSES:
        require_native_host()
    run, context = load_context(run)
    if processes is _NATIVE_PROCESSES:
        require_native_context(context)
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
    if processes is _NATIVE_PROCESSES:
        require_native_host()
    run, context = load_context(run)
    if processes is _NATIVE_PROCESSES:
        require_native_context(context)
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
    if processes is _NATIVE_PROCESSES:
        require_native_host()
    run, context = load_context(run, require_lock=False)
    if processes is _NATIVE_PROCESSES:
        require_native_context(context)
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


def archived_saves(run):
    context_path = safe_path(run / "context.json")
    context_sha = digest(context_path)
    run, context = load_context(run, require_lock=False)
    if (context["phase"] != "restored" or context.get("restorationVerified") is not True
            or context.get("backupComplete") is not True):
        raise SessionError("Save inventory requires a verified session reset")
    plan = read_json(run / "plan.json")
    profile = Path(context["profileRoot"])
    staged_relative = relative_file(run, plan["fixture"]["targetRelativePath"]).relative_to(run).as_posix()
    staged = profile / staged_relative
    if staged != Path(context["fixtureTarget"]) or plan["fixture"]["sha256"] != context["fixtureSha256"]:
        raise SessionError("Staged save identity differs from frozen plan")
    item = context["protected"][0]  # load_context checks the frozen plan's profile-first ordering
    before = {path.casefold(): metadata for path, metadata in item["before"]["files"].items()}
    staged_relative = staged_relative.casefold()
    archives = item.get("archives", [])
    if not archives or not any(a.get("restored") is True for a in archives):
        raise SessionError("Verified profile archive is missing")
    rows, seen_archives = [], set()
    for entry in archives:
        archive_id = entry["path"]
        if (not re.fullmatch(r"after/0-[0-9a-f]{32}", archive_id) or archive_id in seen_archives):
            raise SessionError("Invalid or repeated profile archive path")
        seen_archives.add(archive_id)
        archive = relative_file(run, archive_id)
        seen_paths = set()
        for original, recorded in entry["state"]["files"].items():
            key = original.casefold()
            if not (key.startswith("save/me2/") and key.endswith(".pcsav")):
                continue
            if key in seen_paths:
                raise SessionError("Ambiguous save path in archive")
            seen_paths.add(key)
            source = relative_file(archive, original)
            if not source.is_file() or source.stat().st_size != recorded["bytes"] or digest(source) != recorded["sha256"]:
                raise SessionError(f"Archived save differs from reset receipt: {archive_id}/{original}")
            if key == staged_relative:
                change = "staged-unchanged" if recorded["sha256"] == context["fixtureSha256"] else "staged-modified"
            elif key not in before:
                change = "new-path"
            else:
                change = "unchanged" if before[key]["sha256"] == recorded["sha256"] else "modified"
            rows.append({"originalRelativePath": original, "archiveRelativePath": source.relative_to(run).as_posix(),
                         "archiveRestored": entry["restored"], "sha256": recorded["sha256"],
                         "bytes": recorded["bytes"], "change": change})
    if digest(context_path) != context_sha or digest(run / "plan.json") != context["planSha256"]:
        raise SessionError("Session evidence changed during save inventory")
    return {"runId": context["runId"], "planSha256": context["planSha256"],
            "contextSha256": context_sha, "saves": rows}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("prepare", help="Acquire coordinated lock, back up and stage a ready fixture")
    p.add_argument("plan", type=Path)
    p.add_argument("run", type=Path)
    for command in ("request-load", "status", "reset", "archived-saves"):
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


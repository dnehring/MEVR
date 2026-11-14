# SPDX-License-Identifier: MIT
"""Inspect explicitly named archived MEVR captures; never contact a running game.

This produces offline-recording-inspection, NOT scenario runtime-state or input
evidence. Game/process identity is unknown in these legacy formats. Inputs must
be archived outside live MEVR/MELEVR data folders. No --latest or request writer.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import stat
import struct
import sys

MAX_BYTES = 256 * 1024 * 1024
MISSING = ["game/process identity", "run/scenario/fixture binding", "loaded executable/mod hashes",
           "selected save and loaded map", "weapon identity, loaded ammo and reserve ammo",
           "dialogue interrupt window and accepted branch", "planet/probe/fuel/mineral state",
           "game-consumed native/VR input events", "normal save/reload persistence"]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def archive_path(value):
    path = Path(value).absolute()
    for part in (path, *path.parents):
        if part.is_symlink() or (part.exists() and getattr(part.lstat(), "st_file_attributes", 0) &
                                 getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)):
            raise ValueError("Symlink/reparse paths are refused")
    path = path.resolve()
    local = os.environ.get("LOCALAPPDATA")
    if local:
        for name in ("MEVR", "MELEVR"):
            live = (Path(local) / name).resolve()
            if path == live or live in path.parents:
                raise ValueError("Use an archived copy outside live MEVR/MELEVR data folders")
    # Do not write or inspect in known game/profile trees, including relocated installs.
    lowered = [part.casefold() for part in path.parts]
    if "mass effect legendary edition" in lowered or any(
            lowered[i] in ("game", "save") and lowered[i + 1] in ("me1", "me2", "me3")
            for i in range(len(lowered) - 1)):
        raise ValueError("Use a private archive outside game/profile trees")
    return path


def read_stable(path):
    path = archive_path(path)
    before = path.stat()
    if not path.is_file() or before.st_size > MAX_BYTES:
        raise ValueError("Expected regular archive file no larger than 256 MiB")
    data = path.read_bytes()
    after = path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError("Archive changed while reading")
    return data


def read_json(data):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("Duplicate JSON key: " + key)
            result[key] = value
        return result
    def finite(value):
        result = float(value)
        if not math.isfinite(result):
            raise ValueError("Nonfinite JSON number")
        return result
    return json.loads(data.decode("utf-8-sig"), object_pairs_hook=pairs,
                      parse_float=finite, parse_constant=finite)


def inspect_snapshot(path, data):
    body = read_json(data)
    required = {"stamp", "files", "errors", "eyes", "layers", "frame", "unixMs", "replay", "controllers"}
    if not isinstance(body, dict) or not required <= body.keys():
        raise ValueError("Not the archived TestLoop snapshot structure")
    if any(not isinstance(body[key], list) for key in ("files", "errors", "eyes", "layers")):
        raise ValueError("Malformed snapshot lists")
    if type(body["frame"]) is not int or type(body["unixMs"]) is not int:
        raise ValueError("Invalid snapshot frame/time")
    names = set(body["files"])
    issues = ["producer: " + str(error) for error in body["errors"]]
    eyes, seen = [], set()
    for eye in body["eyes"]:
        if not isinstance(eye, dict) or eye.get("eye") not in ("left", "right") or eye["eye"] in seen:
            raise ValueError("Invalid or duplicate eye record")
        seen.add(eye["eye"])
        for key in ("file", "worldFile"):
            if eye.get(key) is not None:
                names.add(eye[key])
        eyes.append({key: eye.get(key) for key in ("eye", "file", "worldFile", "size", "hasWorld", "pose", "fov")})
    for layer in body["layers"]:
        if not isinstance(layer, dict):
            raise ValueError("Invalid layer record")
        if layer.get("file") is not None:
            names.add(layer["file"])
    files = []
    for name in sorted(names):
        # Current producer writes sibling PNGs; no arbitrary referenced path is opened.
        if (not isinstance(name, str) or not name or any(c in name for c in ("/", "\\", ":"))
                or name in (".", "..") or not name.lower().endswith(".png")):
            raise ValueError("Snapshot artifact must be a sibling PNG filename")
        child = archive_path(path.parent / name)
        if not child.exists():
            files.append({"file": name, "available": False})
            issues.append("missing image: " + name)
            continue
        raw = read_stable(child)
        size = None
        if len(raw) >= 33 and raw[:8] == b"\x89PNG\r\n\x1a\n" and raw[8:16] == b"\0\0\0\rIHDR":
            size = list(struct.unpack_from(">II", raw, 16))
        if size is None or 0 in size:
            issues.append("invalid PNG header: " + name)
        files.append({"file": name, "available": True, "sha256": sha(raw), "bytes": len(raw),
                      "sizeFromHeader": size})
    if seen != {"left", "right"}:
        issues.append("both eye records are not present")
    by_name = {item["file"]: item for item in files}
    for eye in eyes:
        item = by_name.get(eye["file"], {})
        if not item.get("available"):
            issues.append("missing composed image for " + eye["eye"])
        elif item.get("sizeFromHeader") != eye["size"]:
            issues.append("eye/image dimensions differ for " + eye["eye"])
        if eye["hasWorld"] is not True:
            issues.append("world not confirmed in " + eye["eye"])
    if body["replay"] is not None:
        issues.append("replay declared; cannot establish live physical input")
    return {"format": "MEVR TestLoop snapshot", "capturedUnixMs": body["unixMs"],
            "frame": body["frame"], "declaredReplay": body["replay"],
            "declaredSessionFocused": body.get("sessionFocused"), "eyes": eyes, "files": files,
            "modStatusPresent": isinstance(body.get("mod"), dict), "issues": issues,
            "limits": ["PNG headers and hashes only; pixels were not decoded or visually reviewed.",
                       "Current repository producer is LE1/Halcyon-only; archive bytes lack game/run attestation.",
                       "mod.weapon is a pose/status description, not an LE2 ammo observation."]}


def inspect_recording(path, data):
    parser_path = Path(__file__).resolve().parents[1] / "mevrrec.py"
    spec = importlib.util.spec_from_file_location("mevr_archived_recording", parser_path)
    parser = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(parser)
    if len(data) < parser.HEADER.size:
        raise ValueError("Truncated recording header")
    magic, version, header, frame, reserved, created = parser.HEADER.unpack_from(data)
    if (magic != parser.MAGIC or version != 1 or header != parser.HEADER.size or
            frame != parser.FRAME.size or reserved != 0):
        raise ValueError("Unsupported recording layout (only exact v1 accepted)")
    complete, trailing = divmod(len(data) - header, frame)
    if complete == 0:
        raise ValueError("Recording has no complete frames")
    previous = None
    # Validate fixed layout before invoking the existing repository decoder.
    for offset in range(header, header + complete * frame, frame):
        values = parser.FRAME.unpack_from(data, offset)
        if values[0] < 0 or (previous is not None and values[0] <= previous):
            raise ValueError("Recording frame times must increase")
        if any(isinstance(value, float) and not math.isfinite(value) for value in values):
            raise ValueError("Recording contains nonfinite samples")
        previous = values[0]
    created, frames = parser.read(path)
    if sha(read_stable(path)) != sha(data):
        raise ValueError("Recording changed during decoding")
    return {"format": "MEVRREC v1", "decoderSha256": sha(parser_path.read_bytes()),
            "createdUnixMs": created, "completeFrames": len(frames), "trailingBytes": trailing,
            "durationSeconds": frames[-1]["timeS"] - frames[0]["timeS"],
            "headValidFrames": sum(f["headValid"] for f in frames),
            "controllersActiveFrames": sum(f["controllersActive"] for f in frames),
            "issues": ["partial final frame excluded"] if trailing else [],
            "limits": ["Recorded poses/buttons do not establish game consumption or reload/interrupt/probe events.",
                       "Format has no game, process, scenario, loaded-binary or replay-origin identity.",
                       "Head/controller flags cannot establish a physical headset or human input."]}


def inspect(kind, source, expected_sha):
    if kind not in ("snapshot", "recording"):
        raise ValueError("Unsupported archive kind")
    path = archive_path(source)
    data = read_stable(path)
    if not isinstance(expected_sha, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", expected_sha):
        raise ValueError("Explicit archive SHA256 required")
    if sha(data) != expected_sha.lower():
        raise ValueError("Archive hash mismatch")
    result = inspect_snapshot(path, data) if kind == "snapshot" else inspect_recording(path, data)
    if sha(read_stable(path)) != expected_sha.lower():
        raise ValueError("Archive changed during inspection")
    return {"schemaVersion": 1, "source": "offline-recording-inspection", "acceptance": "not-evaluated",
            "gameAttribution": "unknown", "runtimeEvidenceEligible": False, "inputEvidenceEligible": False,
            "input": {"file": path.name, "sha256": sha(data), "bytes": len(data)},
            "inspectorSha256": sha(Path(__file__).read_bytes()), "diagnostics": result,
            "missingForLe2Scenario": MISSING.copy()}


def write_report(destination, report):
    output = archive_path(destination)
    if output.suffix.lower() != ".json":
        raise ValueError("Inspection output must be a new .json file")
    # Parent must already exist, and x forbids overwriting an input or prior report.
    with output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("snapshot", "recording"))
    parser.add_argument("archive", type=Path)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        write_report(args.output, inspect(args.kind, args.archive, args.sha256))
        print("Wrote offline archive inspection; native/VR acceptance remains pending.")
        return 0
    except (OSError, ValueError, TypeError, KeyError, struct.error) as error:
        print("error: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

# SPDX-License-Identifier: MIT
"""MEVR Setup's web and Mass Effect Modder paths, tested offline (research/33 section 10). Nothing reaches the internet.

A local HTTP server on 127.0.0.1 answers for GitHub (MEVR-Setup.exe --test-server sends every request there:
https://api.github.com/x -> http://127.0.0.1:<port>/api.github.com/x). On a fake game folder and a scratch
%LOCALAPPDATA% under the temp folder (never the real game, never the player's own %LOCALAPPDATA%\\MEVR) it runs:

  1. the update check (--check-update) against every answer GitHub can give: no release (404), an older, the same and
     a newer release, a pre-release, malformed JSON, a foreign page, the API quota (403), a server error, a body too
     large, a refused connection and a server that never answers (the 30 s receive timeout; skip with --quick);
  2. Mass Effect Modder's download (--get-mem): the release lookup and asset picking, a redirect like GitHub's, the
     listed size and SHA-256, a short or overlong body, no Content-Length, a 404, no Windows build, a foreign URL, an
     unsafe file name, the exe in a subfolder, an archive without it, the unpacking with Windows' tar.exe and (if
     installed) the 7-Zip fallback; the archive's exe is a stand-in MEM (tools/setup/FakeMem.cpp);
  3. a texture pack through the stand-in MEM (--install-pack): the command lines MEM gets, the .mfl, a stale work folder,
     a .mem as it is, a failing MEM, the manifest's record, the VR game settings applied again over MEM's, then
     --uninstall --remove-local-data (the mod gone, the textures kept and said so, the scratch %LOCALAPPDATA%\\MEVR gone);
  4. "Install after I quit" (--request --wait-pid, through --preview's real start): the request file the in-game menu
     writes, read and removed, the page, the wait for the game's exit;
  5. the voice orders' files (--install-voice, research/33 section 14): the real downloads served from the dev machine's
     own copies (tools/fetch-voice.ps1's archives in %TEMP% or --voice-cache, models/voice), so the pinned sizes and
     SHA-256 sums are checked for real; the model not published yet, a wrong file, a wrong digest, the model tier, a
     larger recognizer added and dropped, a same-size change fetched again, the game's request, Remove, Uninstall.
     Skipped (not failed) when those copies aren't there.

  python -I tools/setup/test-network.py --setup build/Release/MEVR-Setup.exe --fake-game build/fake-game/Release/MassEffect1.exe
      --fake-mem build/fake-mem/Release/MassEffectModderNoGui.exe --repo . [--quick] [--keep] [--voice-cache <folder>]

ctest runs it as mevr-setup-network. Exit 0 when every check passed.
"""

import argparse
import hashlib
import http.server
import json
import lzma
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib
from pathlib import Path

FAILURES = []
MEM_API = "/api.github.com/repos/MassEffectModder/MassEffectModder/releases/latest"
MEM_ASSET = "/github.com/MassEffectModder/MassEffectModder/releases/download/v999/MassEffectModderNoGui-v999.7z"
MEM_CDN = "/objects.githubusercontent.com/mem/MassEffectModderNoGui-v999.7z"
UPDATE_API = "/api.github.com/repos/dnehring/MEVR/releases/latest"


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    if not ok:
        FAILURES.append(what)


# ---- A 7z archive, written here (LZMA2, one solid folder) so the test needs no archiver --------------------------------
def _num(value):
    """7z's variable-length NUMBER."""
    first, mask, i = 0, 0x80, 0
    while i < 8:
        if value < (1 << (7 * (i + 1))):
            first |= (value >> (8 * i)) & 0xFF
            break
        first |= mask
        mask >>= 1
        i += 1
    out = bytes([first])
    for _ in range(i):
        out += bytes([value & 0xFF])
        value >>= 8
    return out


def make_7z(entries):
    """entries: [(name with '/', bytes)] -> a .7z (LZMA2, 1 MiB dictionary) as 7-Zip writes one."""
    data = b"".join(content for _, content in entries)
    packed = lzma.compress(data, format=lzma.FORMAT_RAW, filters=[{"id": lzma.FILTER_LZMA2, "dict_size": 1 << 20}])
    h = bytearray()
    h += b"\x01\x04"                                        # Header, MainStreamsInfo
    h += b"\x06" + _num(0) + _num(1) + b"\x09" + _num(len(packed)) + b"\x00"   # PackInfo
    h += b"\x07\x0b" + _num(1) + b"\x00"                    # UnpackInfo, Folder: 1, not external
    h += _num(1) + b"\x21\x21" + _num(1) + bytes([16])      # 1 coder: LZMA2 (id 0x21), props: dict 2^20
    h += b"\x0c" + _num(len(data)) + b"\x00"                # CodersUnpackSize, End
    h += b"\x08\x0d" + _num(len(entries))                   # SubStreamsInfo, NumUnpackStream
    if len(entries) > 1:
        h += b"\x09" + b"".join(_num(len(c)) for _, c in entries[:-1])
    h += b"\x0a\x01" + b"".join(struct.pack("<I", zlib.crc32(c)) for _, c in entries) + b"\x00\x00"
    names = b"".join(name.encode("utf-16-le") + b"\x00\x00" for name, _ in entries)
    h += b"\x05" + _num(len(entries)) + b"\x11" + _num(len(names) + 1) + b"\x00" + names + b"\x00\x00"
    header = bytes(h)
    start = struct.pack("<QQI", len(packed), len(header), zlib.crc32(header))
    return b"7z\xbc\xaf\x27\x1c\x00\x04" + struct.pack("<I", zlib.crc32(start)) + start + packed + header


# ---- The server ---------------------------------------------------------------------------------------------------------
class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.routes = {}     # path -> callable(handler)
        self.requests = []   # (path, user agent, accept)

    def set(self, path, responder):
        with self.lock:
            self.routes[path] = responder

    def take_requests(self):
        with self.lock:
            out, self.requests = self.requests, []
            return out


STATE = State()


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        with STATE.lock:
            STATE.requests.append((self.path, self.headers.get("User-Agent", ""), self.headers.get("Accept", "")))
            responder = STATE.routes.get(self.path)
        try:
            if responder is None:
                send(self, 404, b'{"message": "Not Found"}')
            else:
                responder(self)
        except (ConnectionError, OSError):
            pass


def send(h, status, body, headers=None, length=True):
    h.send_response(status)
    for k, v in (headers or {}).items():
        h.send_header(k, v)
    if length:
        h.send_header("Content-Length", str(len(body)))
    h.end_headers()
    h.wfile.write(body)


def json_answer(status, obj, headers=None):
    body = obj if isinstance(obj, bytes) else json.dumps(obj).encode()
    return lambda h: send(h, status, body, dict({"Content-Type": "application/json"}, **(headers or {})))


def redirect(location):
    def respond(h):
        h.send_response(302)
        h.send_header("Location", location)
        h.send_header("Content-Length", "0")
        h.end_headers()
    return respond


def truncated(body):
    def respond(h):
        h.send_response(200)
        h.send_header("Content-Length", str(len(body)))
        h.end_headers()
        h.wfile.write(body[: len(body) // 2])
        h.wfile.flush()
        h.close_connection = True
        h.connection.shutdown(socket.SHUT_RDWR)
    return respond


def silent(seconds):
    def respond(h):
        time.sleep(seconds)
    return respond


# ---- Running the setup --------------------------------------------------------------------------------------------------
class Setup:
    def __init__(self, exe, scratch, env):
        self.exe = Path(exe)
        self.scratch = scratch
        self.env = env

    def run(self, args, exe=None, extra_env=None, timeout=180):
        out = self.scratch / "setup-out.txt"
        if out.exists():
            out.unlink()
        env = dict(self.env, **(extra_env or {}))
        # Never the player's own %LOCALAPPDATA%: everything this test runs reads and writes the scratch one.
        assert Path(env["LOCALAPPDATA"]).resolve().is_relative_to(self.scratch.resolve())
        p = subprocess.run([str(exe or self.exe)] + [str(a) for a in args] + ["--out", str(out)], env=env, timeout=timeout,
                           stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        text = out.read_text(encoding="utf-8", errors="replace") if out.exists() else ""
        print(f"---- MEVR-Setup {' '.join(str(a) for a in args)} (exit {p.returncode})")
        print("     " + text.rstrip().replace("\n", "\n     "), flush=True)
        return p.returncode, text


def snapshot(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob("*")) if p.is_file()}


def tar_reads_lzma():
    try:
        version = subprocess.run([os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "tar.exe"), "--version"],
                                 capture_output=True, text=True, timeout=30).stdout
    except OSError:
        return False, ""
    return "liblzma" in version, version.strip()


def find_7zip():
    for base in (os.environ.get("ProgramW6432"), os.environ.get("ProgramFiles"), os.environ.get("ProgramFiles(x86)")):
        if base and Path(base, "7-Zip", "7z.exe").exists():
            return Path(base, "7-Zip", "7z.exe")
    return None


# ---- 1. The update check ------------------------------------------------------------------------------------------------
def test_update_check(setup, base, quick):
    print("\n== 1. The update check (--check-update)")
    page = "https://github.com/dnehring/MEVR/releases/tag/"

    def release(tag, **extra):
        return dict({"tag_name": tag, "name": "MEVR " + tag, "html_url": page + tag, "prerelease": False, "draft": False}, **extra)

    # This setup's own version, from the one place it's written (it was a literal v0.1.0 here, so the "same version" case
    # failed once the version moved to 0.1.1).
    own = re.search(r"project\(MEVR VERSION (\d+\.\d+\.\d+)", Path(__file__).resolve().parents[2].joinpath("CMakeLists.txt").read_text(encoding="utf-8"))
    own_tag = "v" + (own.group(1) if own else "0.1.0")
    cases = [
        ("no release published (404)", json_answer(404, {"message": "Not Found"}), 0, "no release", "No release is published"),
        ("an older release", json_answer(200, release("v0.0.9")), 0, "latest", "newer than the latest release (v0.0.9)"),
        ("the same version", json_answer(200, release(own_tag)), 0, "latest", "This is the latest version."),
        ("a newer release", json_answer(200, release("v99.2.0")), 0, "newer", "Version v99.2.0 is out."),
        ("a newer pre-release", json_answer(200, release("v99.3.0-beta", prerelease=True)), 0, "latest", "This is the latest version."),
        ("a newer draft", json_answer(200, release("v99.4.0", draft=True)), 0, "latest", "This is the latest version."),
        ("malformed JSON", json_answer(200, b'{"tag_name": "v9", '), 1, "failed", "wasn't a release"),
        ("a JSON array", json_answer(200, b"[]"), 1, "failed", "wasn't a release"),
        ("a page that isn't github.com", json_answer(200, release("v99.0.0", html_url="https://example.com/x")), 1, "failed", "wasn't a release"),
        ("the API quota (403, X-RateLimit-Remaining: 0)",
         json_answer(403, {"message": "API rate limit exceeded"}, {"X-RateLimit-Remaining": "0"}), 1, "rate limited", "limit"),
        ("a 429", json_answer(429, {"message": "slow down"}), 1, "rate limited", "limit"),
        ("a 403 that isn't the quota", json_answer(403, {"message": "forbidden"}, {"X-RateLimit-Remaining": "12"}), 1, "failed", "HTTP 403"),
        ("a server error (500)", json_answer(500, b"oops"), 1, "failed", "HTTP 500"),
        ("a body over 4 MB", json_answer(200, b'{"x": "' + b"a" * (5 << 20) + b'"}'), 1, "failed", "too large"),
    ]
    for name, responder, code, state, text in cases:
        STATE.set(UPDATE_API, responder)
        STATE.take_requests()
        rc, out = setup.run(["--check-update", "--test-server", base])
        reqs = STATE.take_requests()
        check(rc == code and f"update: {state}\n" in out and text in out, f"update check, {name}: {state}, '{text}'")
        if name == "a newer release":
            check(f"page: {page}v99.2.0" in out, "update check, a newer release: its page offered")
            check(len(reqs) == 1 and reqs[0][0] == UPDATE_API and reqs[0][1].startswith("MEVR-Setup/") and
                  "application/vnd.github+json" in reqs[0][2], "update check: one GET of the Releases API, our User-Agent and Accept")
        elif "page:" in out:
            check(False, f"update check, {name}: no page offered")
    # The placeholder: no request at all.
    STATE.take_requests()
    rc, out = setup.run(["--check-update", "--repo", "https://github.com/OWNER/REPO", "--test-server", base])
    check(rc == 0 and "update: off" in out and not STATE.take_requests(), "update check, the placeholder repository: off, nothing asked")
    rc, out = setup.run(["--check-update", "--repo", "https://github.com/someone/fork.git", "--test-server", base])
    reqs = STATE.take_requests()
    check(rc == 0 and reqs and reqs[0][0] == "/api.github.com/repos/someone/fork/releases/latest", "update check, another repository (--repo): its API")
    # Nothing listening.
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        closed = s.getsockname()[1]
    rc, out = setup.run(["--check-update", "--test-server", f"http://127.0.0.1:{closed}"])
    check(rc == 1 and "update: failed" in out and "couldn't connect" in out, "update check, a refused connection: 'couldn't connect'")
    # The test server is only ever 127.0.0.1 or https.
    for bad in ("http://localhost:1", "http://127.0.0.2:80", "http://example.com", "ftp://127.0.0.1", "http://127.0.0.1:80/x", "file:///C:/"):
        rc, _ = setup.run(["--check-update", "--test-server", bad])
        check(rc == 2, f"--test-server {bad}: refused")
    if quick:
        print("     (the 30 s timeout case skipped: --quick)")
        return
    STATE.set(UPDATE_API, silent(40))
    started = time.monotonic()
    rc, out = setup.run(["--check-update", "--test-server", base], timeout=120)
    took = time.monotonic() - started
    check(rc == 1 and "no answer in time" in out and 25 < took < 60, f"update check, a server that never answers: timed out after {took:.0f} s")


# ---- 2. Mass Effect Modder's download -----------------------------------------------------------------------------------
def mem_release(base, size, sha, **asset_extra):
    asset = dict({"name": "MassEffectModderNoGui-v999.7z",
                  "browser_download_url": "https://github.com" + MEM_ASSET.removeprefix("/github.com"), "size": size,
                  "digest": "sha256:" + sha}, **asset_extra)
    return {"tag_name": "v999", "name": "For LE only", "assets": [
        {"name": "MassEffectModderNoGui-v999-linux.7z", "browser_download_url": "https://github.com/x/linux.7z", "size": 5},
        {"name": "MassEffectModder-v999.7z", "browser_download_url": "https://github.com/x/gui.7z", "size": 6},
        asset]}


def test_mem_download(setup, base, local, fake_mem):
    print("\n== 2. Mass Effect Modder's download (--get-mem)")
    tools = local / "MEVR" / "tools"
    exe = tools / "MassEffectModderNoGui.exe"
    stub = fake_mem.read_bytes()
    has_lzma, tar_version = tar_reads_lzma()
    seven = find_7zip()
    print(f"     tar.exe: {tar_version.splitlines()[0] if tar_version else 'not found'}; 7-Zip: {seven or 'not installed'}")
    can_unpack = has_lzma or seven is not None

    def serve(archive, release=None, asset=None):
        sha = hashlib.sha256(archive).hexdigest()
        STATE.set(MEM_API, json_answer(200, release if release is not None else mem_release(base, len(archive), sha)))
        STATE.set(MEM_ASSET, redirect(base + MEM_CDN))
        STATE.set(MEM_CDN, asset or (lambda h: send(h, 200, archive, {"Content-Type": "application/octet-stream"})))

    def fresh():
        shutil.rmtree(tools, ignore_errors=True)

    def leftovers():
        return sorted(p.name for p in tools.glob("*.part")) if tools.exists() else []

    archive = make_7z([("MassEffectModderNoGui.exe", stub)])
    fresh()
    serve(archive)
    STATE.take_requests()
    rc, out = setup.run(["--get-mem", "--test-server", base])
    reqs = [r[0] for r in STATE.take_requests()]
    check(reqs == [MEM_API, MEM_ASSET, MEM_CDN], f"MEM: the release, then the asset through GitHub's redirect ({reqs})")
    check(f"MassEffectModderNoGui-v999.7z ({len(archive)} bytes, sha256 {hashlib.sha256(archive).hexdigest()})" in out,
          "MEM: the Windows CLI asset picked (not the Linux or GUI build), its size and SHA-256 read")
    if can_unpack:
        check(rc == 0 and exe.exists() and exe.read_bytes() == stub, "MEM: downloaded, unpacked (our LZMA2 .7z), the exe in the tools folder")
        check((tools / "MassEffectModderNoGui-v999.7z").read_bytes() == archive and not leftovers(), "MEM: the archive whole, no .part left")
        run = subprocess.run([str(exe), "--version"], capture_output=True, text=True, timeout=30)
        check(run.returncode == 0 and "stand-in" in run.stdout, "MEM: the unpacked exe runs")
    else:
        check(rc == 1 and "7-Zip" in out and "7-zip.org" in out, "MEM: this Windows can't open 7z (no liblzma, no 7-Zip): the message says what to do")
    # Again over what's there (the archive and exe replaced).
    rc, out = setup.run(["--get-mem", "--test-server", base])
    check((rc == 0) == can_unpack and not leftovers(), "MEM: downloaded again over the first (replaced, no .part)")

    # The exe in a subfolder of the archive; an archive without it.
    fresh()
    serve(make_7z([("MEM/readme.txt", b"read me\r\n"), ("MEM/MassEffectModderNoGui.exe", stub)]))
    rc, out = setup.run(["--get-mem", "--test-server", base])
    if can_unpack:
        check(rc == 0 and exe.read_bytes() == stub, "MEM: the exe found in a subfolder of the archive")
    fresh()
    serve(make_7z([("readme.txt", b"no exe here\r\n")]))
    rc, out = setup.run(["--get-mem", "--test-server", base])
    if can_unpack:
        check(rc == 1 and "isn't in the archive" in out, "MEM: an archive without the exe: 'isn't in the archive'")
    fresh()
    serve(b"this is not a 7z archive at all" * 10)
    rc, out = setup.run(["--get-mem", "--test-server", base])
    check(rc == 1 and "couldn't unpack" in out and not exe.exists(), "MEM: a broken archive: 'couldn't unpack', what was tried named")

    # What GitHub lists against what arrives.
    sha = hashlib.sha256(archive).hexdigest()
    cases = [
        ("a short body (the connection drops)", None, truncated(archive), ("stopped early", "the connection was dropped")),
        ("no Content-Length, size as listed", None, lambda h: send(h, 200, archive, length=False), None),
        ("no Content-Length, fewer bytes than listed", mem_release(base, len(archive) + 100, sha),
         lambda h: send(h, 200, archive, length=False), f"the release lists {len(archive) + 100}"),
        ("more bytes than listed", mem_release(base, len(archive) - 10, sha), lambda h: send(h, 200, archive, length=False), "larger than"),
        ("a Content-Length other than listed", mem_release(base, len(archive) + 1, sha), None, "the release lists"),
        ("a SHA-256 other than listed", mem_release(base, len(archive), "0" * 64), None, "SHA-256"),
        ("no digest listed", mem_release(base, len(archive), "x", digest=None), None, None),
        ("a 404 for the asset", None, lambda h: send(h, 404, b"Not Found"), "the server answered 404"),
    ]
    for name, release, asset, error in cases:
        fresh()
        serve(archive, release, asset)
        rc, out = setup.run(["--get-mem", "--test-server", base])
        if error is None:
            check((rc == 0) == can_unpack and (exe.exists() == can_unpack), f"MEM, {name}: accepted")
        else:
            errors = error if isinstance(error, tuple) else (error,)
            check(rc == 1 and any(e in out for e in errors) and not exe.exists() and not leftovers() and
                  not (tools / "MassEffectModderNoGui-v999.7z").exists(), f"MEM, {name}: refused ('{' or '.join(errors)}'), nothing left behind")
    # What the release lookup refuses.
    lookups = [
        ("no Windows CLI build", {"tag_name": "v999", "assets": [{"name": "MassEffectModderNoGui-v999-linux.7z",
                                                                   "browser_download_url": "https://github.com/x/l.7z"}]},
         "no Windows command-line build"),
        ("a download URL not on github.com", mem_release(base, 1, sha, browser_download_url="https://evil.example/mem.7z"), "isn't on github.com"),
        ("an unsafe file name", mem_release(base, 1, sha, name="MassEffectModderNoGui\\..\\..\\evil.7z"), "plain file name"),
        ("malformed JSON", b"{", "wasn't a release"),
    ]
    for name, release, error in lookups:
        fresh()
        STATE.set(MEM_API, json_answer(200, release if isinstance(release, bytes) else release))
        STATE.take_requests()
        rc, out = setup.run(["--get-mem", "--test-server", base])
        check(rc == 1 and error in out and len(STATE.take_requests()) == 1, f"MEM lookup, {name}: refused before any download ('{error}')")
    STATE.set(MEM_API, json_answer(403, {"message": "API rate limit exceeded"}, {"X-RateLimit-Remaining": "0"}))
    rc, out = setup.run(["--get-mem", "--test-server", base])
    check(rc == 1 and "limit" in out, "MEM lookup, the API quota: said so")

    # The unpackers: 7-Zip's own archive through tar.exe, and the fallback to 7-Zip when tar can't read one.
    if seven is not None:
        work = setup.scratch / "7zip-work"
        shutil.rmtree(work, ignore_errors=True)
        work.mkdir()
        shutil.copy(fake_mem, work / "MassEffectModderNoGui.exe")
        for label, switches, tar_ok in (("7-Zip's -mx=9 for an exe (BCJ2 + LZMA2, as a 7-Zip-made release would be)", ["-mx=9"], has_lzma),
                                        ("a filter tar.exe doesn't know (Swap4)", ["-m0=Swap4", "-m1=LZMA2"], False)):
            made = work / "mem.7z"
            if made.exists():
                made.unlink()
            r = subprocess.run([str(seven), "a", "-t7z", *switches, str(made), str(work / "MassEffectModderNoGui.exe")],
                               capture_output=True, text=True, timeout=120)
            if r.returncode != 0:
                print(f"     (7-Zip couldn't make '{label}': skipped)")
                continue
            # Whether Windows' tar.exe unpacks this one by itself (listing isn't enough: the headers read, the data may not).
            into = work / "tar-out"
            shutil.rmtree(into, ignore_errors=True)
            into.mkdir()
            listing = subprocess.run([os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "tar.exe"), "-xf", str(made),
                                      "-C", str(into)], capture_output=True, text=True, timeout=60)
            check((listing.returncode == 0) == tar_ok, f"MEM: {label}: tar.exe {'unpacks' if tar_ok else 'can not unpack'} it by itself")
            fresh()
            serve(made.read_bytes())
            rc, out = setup.run(["--get-mem", "--test-server", base])
            check(rc == 0 and exe.exists() and exe.read_bytes() == stub,
                  f"MEM: {label}: unpacked{'' if listing.returncode == 0 else ' (7-Zip took over where tar.exe failed)'}")
    # Leave the stand-in in place for the pack tests.
    fresh()
    tools.mkdir(parents=True)
    shutil.copy(fake_mem, exe)


# ---- 3. A texture pack through MEM, then uninstall ----------------------------------------------------------------------
def make_fake_game(scratch, fake_game, repo):
    root = scratch / "game" / "Mass Effect Legendary Edition"
    win64 = root / "Game" / "ME1" / "Binaries" / "Win64"
    config = root / "Game" / "ME1" / "BioGame" / "Config"
    cooked = root / "Game" / "ME1" / "BioGame" / "CookedPCConsole"
    for d in (win64, config, cooked):
        d.mkdir(parents=True, exist_ok=True)
    shutil.copy(fake_game, win64 / "MassEffect1.exe")
    # Another proxy loader in front of a stand-in original: the setup uses it as it is (no RAD DLL needed).
    (win64 / "bink2w64.dll").write_bytes(b"a stand-in proxy loader (test-network.py)")
    (win64 / "bink2w64_original.dll").write_bytes(b"a stand-in for RAD's DLL (test-network.py)")
    (config / "GamerSettings.ini").write_bytes(b"[SystemSettings]\r\nFullscreen=True\r\nResX=2560\r\nResY=1440\r\nUseVSync=True\r\n"
                                               b"MotionBlur=True\r\nFilmGrain=True\r\nDynamicShadows=True\r\n\r\n[HDR]\r\n"
                                               b"DesiredRange=DynamicRange_HDR\r\n")
    (config / "BIOEngine.ini").write_bytes(b"[Engine.GameEngine]\r\nSmoothFrameRate=TRUE\r\nMaxSmoothedFrameRate=62\r\n")
    (cooked / "SFXTest.pcc").write_bytes(b"\xc1\x83\x2a\x9e" + bytes(4092))
    payload = scratch / "payload"
    payload.mkdir()
    (payload / "LE1MEVR.asi").write_bytes(b"stand-in LE1MEVR.asi (test-network.py)")
    (payload / "bink2w64.dll").write_bytes(b"stand-in loader (test-network.py)")
    shutil.copytree(repo / "third_party" / "fonts", payload / "fonts")
    shutil.copytree(repo / "assets" / "menu", payload / "menu")
    return root, payload


def read_manifest(root):
    path = root / "Game" / "ME1" / "Binaries" / "Win64" / "ASI" / "MEVR-install.json"
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else None


def stub_calls(tools):
    log = tools / "fake-mem.log"
    if not log.exists():
        return []
    return [line[len("call: "):].split(" | ") for line in log.read_text(encoding="utf-8").splitlines() if line.startswith("call: ")]


def test_packs(setup, root, payload, local):
    print("\n== 3. A texture pack through the stand-in MEM (--install-pack), then uninstall")
    tools = local / "MEVR" / "tools"
    work = local / "MEVR" / "packs"
    win64 = root / "Game" / "ME1" / "Binaries" / "Win64"
    gamer = root / "Game" / "ME1" / "BioGame" / "Config" / "GamerSettings.ini"
    marked = root / "Game" / "ME1" / "BioGame" / "CookedPCConsole" / "SFXTest.pcc"
    downloads = setup.scratch / "downloads"
    downloads.mkdir()
    before = snapshot(root)
    rc, out = setup.run(["--install", "--game", root, "--payload", payload, "--source", "Folder", "--quality", "performance"])
    check(rc == 0 and (win64 / "ASI" / "MEVR-Setup.exe").exists(), "install on the fake game (another proxy loader: used as it is)")
    check(b"MotionBlur=False" in gamer.read_bytes(), "install: the VR game settings (MotionBlur=False)")

    # The player turns film grain back on in the game (ours stays off only where they left it).
    gamer.write_bytes(gamer.read_bytes().replace(b"FilmGrain=False", b"FilmGrain=True"))
    # A stale work folder from an earlier version of the pack.
    (work / "alot-le1").mkdir(parents=True)
    (work / "alot-le1" / "ALOT-old-version.mem").write_bytes(b"an earlier unpack")
    pack = downloads / "ALOT_for_ME1_test.7z"
    pack.write_bytes(b"a stand-in pack archive (the stand-in MEM doesn't read it)")
    rc, out = setup.run(["--install-pack", "alot-le1", "--file", pack, "--game", str(root) + "\\"])
    calls = stub_calls(tools)
    check(rc == 0 and "ALOT for ME1 is installed" in out, "pack: ALOT for ME1 installed through the stand-in MEM")
    alot = work / "alot-le1"
    expected = [
        ["--unpack-archive", "--input", str(pack), "--output", str(alot), "--ipc"],
        ["--set-game-data-path", "--gameid", "1", "--path", str(root) + "\\"],
        ["--install-mods", "--gameid", "1", "--input", str(alot / "install.mfl"), "--verify", "--ipc"],
    ]
    check(calls == expected, "pack: MEM's three command lines exactly (the game path's trailing backslash intact)")
    if calls != expected:
        print("     got:", calls, "\n     expected:", expected)
    mfl = (alot / "install.mfl").read_bytes().decode("utf-8")
    check(mfl == f"{alot / 'ALOT_for_ME1_test-1.mem'}\r\n{alot / 'Sub' / 'ALOT_for_ME1_test-2.mem'}\r\n",
          "pack: the .mfl lists the unpacked .mem files, sorted, and not the earlier version's")
    check(not (alot / "ALOT-old-version.mem").exists(), "pack: the work folder emptied before unpacking")
    check(marked.read_bytes().endswith(b"ThisIsMEMEndOfFileMarker"), "pack: MEM's marker on the game's SFXTest.pcc")
    m = read_manifest(root)
    packs = m.get("packs", []) if m else []
    check(len(packs) == 1 and packs[0].get("id") == "alot-le1" and packs[0].get("name") == "ALOT for ME1" and packs[0].get("method") == "mem"
          and packs[0].get("installedAt"), "pack: recorded in the manifest (id, name, method, time)")
    g = gamer.read_bytes()
    check(b"MotionBlur=False" in g and b"ResX=3072" in g and "GamerSettings.ini: 1 of our values set again" in out,
          "pack: the one VR game setting MEM wrote over set again (MotionBlur off), the resolution as installed")
    check(b"FilmGrain=True" in g, "pack: a setting the player changed since the install stays theirs (FilmGrain)")
    check("[0] Checking the game" in out and "Installing textures" in out and "[100] Done" in out, "pack: MEM's [IPC] stages read as progress")

    # A .mem as it is: no unpacking. Started from the game's own copy of the setup (as "Install after I quit" does).
    mem = downloads / "LEUITM test.mem"
    mem.write_bytes(b"a stand-in .mem")
    (tools / "fake-mem.log").unlink()
    rc, out = setup.run(["--install-pack", "leuitm-le1", "--file", mem, "--game", root], exe=win64 / "ASI" / "MEVR-Setup.exe")
    calls = stub_calls(tools)
    check(rc == 0 and [c[0] for c in calls] == ["--set-game-data-path", "--install-mods"], "pack: a .mem file goes to MEM as it is (no unpacking)")
    check((work / "leuitm-le1" / "install.mfl").read_bytes().decode("utf-8") == f"{mem}\r\n", "pack: its .mfl names it, spaces and all")
    m = read_manifest(root)
    check([p.get("id") for p in m.get("packs", [])] == ["alot-le1", "leuitm-le1"], "pack: both on record")

    # MEM fails: nothing recorded.
    isl = downloads / "Static Lighting test.7z"
    isl.write_bytes(b"x")
    rc, out = setup.run(["--install-pack", "isl-le1", "--file", isl, "--game", root], extra_env={"MEVR_FAKE_MEM_FAIL": "install-mods"})
    check(rc == 1 and "MEM failed (exit code 1): the stand-in was told to fail in install-mods" in out, "pack: MEM failing: its [IPC]ERROR shown")
    check("isl-le1" not in [p.get("id") for p in read_manifest(root).get("packs", [])], "pack: a failed install isn't recorded")
    rc, out = setup.run(["--install-pack", "isl-le1", "--file", isl, "--game", root], extra_env={"MEVR_FAKE_MEM_FAIL": "set-game-data-path"})
    check(rc == 1 and "didn't accept the game folder" in out, "pack: MEM refusing the game folder: said so")
    rc, out = setup.run(["--install-pack", "isl-le1", "--file", isl, "--game", root], extra_env={"MEVR_FAKE_MEM_FAIL": "unpack-archive"})
    check(rc == 1 and "unpacking failed" in out, "pack: MEM failing to unpack: said so")
    rc, out = setup.run(["--install-pack", "nope", "--file", isl, "--game", root])
    check(rc == 1 and "No texture pack called 'nope'" in out, "pack: an unknown pack refused")
    rc, out = setup.run(["--install-pack", "isl-le1", "--file", downloads / "missing.7z", "--game", root])
    check(rc == 1 and "isn't there" in out, "pack: a missing download refused")

    rc, out = setup.run(["--status", "--game", root])
    check("textures marked by MEM: yes" in out and "texture packs installed: ALOT for ME1 (alot-le1, mem, " in out and
          "LEUITM (Updated Improved Textures) (leuitm-le1" in out and "Mass Effect Modder: downloaded" in out, "status: the packs, the marker, MEM")

    # Uninstall: the mod goes, the textures stay (and it says so), the scratch %LOCALAPPDATA%\MEVR goes with --remove-local-data.
    rc, out = setup.run(["--uninstall", "--remove-local-data", "--game", root])
    check(rc == 0 and "texture packs stay (ALOT for ME1, LEUITM (Updated Improved Textures))" in out, "uninstall: says the texture packs stay")
    check(not (local / "MEVR").exists() and "deleted " in out, "uninstall --remove-local-data: MEM and the pack work files gone (the scratch folder)")
    after = snapshot(root)
    changed = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
    check(changed == [str(Path("Game/ME1/BioGame/CookedPCConsole/SFXTest.pcc"))],
          f"uninstall: the game folder byte for byte as before, but for MEM's textures ({changed})")
    check(marked.read_bytes().endswith(b"ThisIsMEMEndOfFileMarker"), "uninstall: the textures (MEM's marker) left in place")


# ---- 4. "Install after I quit" ------------------------------------------------------------------------------------------
def test_request(setup, root, payload, local):
    print("\n== 4. \"Install after I quit\" (--request --wait-pid)")
    rc, _ = setup.run(["--install", "--game", root, "--payload", payload, "--source", "Folder", "--no-settings"])
    asi_setup = root / "Game" / "ME1" / "Binaries" / "Win64" / "ASI" / "MEVR-Setup.exe"
    check(rc == 0 and asi_setup.exists(), "install again: the setup's copy in the ASI folder")
    request = local / "MEVR" / "setup.request"
    request.parent.mkdir(parents=True, exist_ok=True)
    # What plugin/SetupRequest writes (json::Write of page, packs, pid), then the game starts ASI\MEVR-Setup.exe --request --wait-pid.
    game = subprocess.Popen([sys.executable, "-I", "-c", "import time; time.sleep(6)"])
    request.write_text(json.dumps({"page": "content", "packs": [], "pid": game.pid}, indent=2), encoding="utf-8")
    shots = setup.scratch / "request-shots"
    started = time.monotonic()
    rc, out = setup.run(["--preview", shots / "waiting", "--warp", "--request", "--wait-pid", game.pid], exe=asi_setup, timeout=240)
    took = time.monotonic() - started
    game.wait()
    parts = out.split("== ")
    first = next((p for p in parts if p.startswith("started")), "")
    waiting = next((p for p in parts if p.startswith("while waiting")), "")
    after = next((p for p in parts if p.startswith("after the game exited")), "")
    check(rc == 0, "request: the setup's copy started as the game starts it (exit 0)")
    check("page: content" in first and "request text: Choose what to add below" in first, "request: opens on Optional content, 'Choose what to add below'")
    check(f"game: {root}" in first and "game ok: 1" in first and "installed: 1" in first, "request: found its own game from the ASI folder")
    check("waiting: 1" in first and "waiting: 1" in waiting and "Changes start once the game has exited." in waiting,
          "request: waits while the game still runs (installs disabled)")
    check("waiting: 0" in after and took > 3, f"request: goes on once the game has exited ({took:.0f} s)")
    check(not request.exists(), "request: the request file read and removed")
    check((shots / "waiting" / "request-start.png").exists() and (shots / "waiting" / "request-after-exit.png").exists(),
          "request: the page rendered (PNGs)")

    # A request naming packs (the format allows it), and a broken one.
    request.write_text(json.dumps({"page": "content", "packs": ["alot-le1", "nope", "alot-le1"], "pid": 1}), encoding="utf-8")
    rc, out = setup.run(["--preview", shots / "named", "--warp", "--request"], exe=asi_setup, timeout=240)
    check(rc == 0 and "request text: You asked for: ALOT for ME1." in out and not request.exists(), "request: named packs shown (unknown ids dropped)")
    request.write_text("{ not json", encoding="utf-8")
    rc, out = setup.run(["--preview", shots / "broken", "--warp", "--request"], exe=asi_setup, timeout=240)
    check(rc == 0 and "page: content" in out and "request text: Choose what to add below" in out and not request.exists(),
          "request: a broken request file: Optional content anyway, the file removed")
    rc, out = setup.run(["--uninstall", "--game", root])
    check(rc == 0, "uninstall after the request tests")


VOICE_TAG_API = "/api.github.com/repos/dnehring/MEVR/releases/tags/voice-model-1"
VOICE_MODEL_ASSET = "model.gguf"
VOICE_MODEL_URL = "/github.com/dnehring/MEVR/releases/download/voice-model-1/" + VOICE_MODEL_ASSET
VOICE_MOONSHINE = "/github.com/moonshine-ai/moonshine/releases/download/v0.1.5/moonshine-voice-windows-x86_64.tar.gz"
VOICE_LLAMA = "/github.com/ggml-org/llama.cpp/releases/download/b12128/llama-b12128-bin-win-cpu-x64.zip"
VOICE_MODELS = "/huggingface.co/moonshine-ai/moonshine-voice-assets/resolve/main/model/{size}-streaming-en/quantized_26_08_21/{name}"
VOICE_FILES = ["streaming_config.json", "frontend.model.ort", "frontend.weights.ort", "encoder.ort", "adapter.ort", "cross_kv.ort",
               "decoder_kv.ort", "tokenizer.bin"]


def file_answer(path):
    """The file's bytes, read when asked (the archives are tens of MB)."""
    def respond(h):
        send(h, 200, Path(path).read_bytes(), {"Content-Type": "application/octet-stream"})
    return respond


def find_voice_sources(repo, cache):
    """The real downloads, from where tools/fetch-voice.ps1 leaves them on the dev machine: Moonshine's and llama.cpp's
    archives in %TEMP% (or --voice-cache), the models in models/voice. None for what's missing."""
    places = [Path(cache)] if cache else []
    places.append(Path(tempfile.gettempdir()))
    def first(name):
        return next((p / name for p in places if (p / name).is_file()), None)
    models = {}
    for size in ("tiny", "small"):
        d = Path(repo) / "models" / "voice" / f"{size}-streaming-en"
        if all((d / f).is_file() for f in VOICE_FILES):
            models[size] = d
    return first("moonshine-voice-windows-x86_64-v0.1.5.tar.gz"), first("llama-b12128-bin-win-cpu-x64.zip"), models


def test_voice(setup, base, root, payload, local, repo, cache):
    print("\n== 5. The voice orders' files (--install-voice, --check-voice, --remove-voice; install/Voice)")
    moonshine, llama, models = find_voice_sources(repo, cache)
    if moonshine is None or llama is None or "tiny" not in models or "small" not in models:
        print("  (skipped: the real downloads aren't on this PC: run tools/fetch-voice.ps1 -Llm, or pass --voice-cache "
              "with moonshine-voice-windows-x86_64-v0.1.5.tar.gz and llama-b12128-bin-win-cpu-x64.zip)")
        return
    asi = root / "Game" / "ME1" / "Binaries" / "Win64" / "ASI"
    voice = asi / "voice"
    for size, d in models.items():
        for name in VOICE_FILES:
            STATE.set(VOICE_MODELS.format(size=size, name=name), file_answer(d / name))
    # GitHub's release downloads redirect to its CDN, as the real ones do.
    STATE.set(VOICE_MOONSHINE, redirect("/objects.githubusercontent.com/voice/moonshine.tar.gz"))
    STATE.set("/objects.githubusercontent.com/voice/moonshine.tar.gz", file_answer(moonshine))
    STATE.set(VOICE_LLAMA, file_answer(llama))
    gguf = b"GGUF a stand-in for the fine-tuned model (test-network.py)" + bytes(4000)
    gguf_sha = hashlib.sha256(gguf).hexdigest()
    STATE.set(VOICE_MODEL_URL, redirect("/objects.githubusercontent.com/voice/model.gguf"))
    STATE.set("/objects.githubusercontent.com/voice/model.gguf", lambda h: send(h, 200, gguf))
    def release(digest):
        return {"tag_name": "voice-model-1", "assets": [{"name": VOICE_MODEL_ASSET, "size": len(gguf), "digest": "sha256:" + digest,
                                                          "browser_download_url": "https://github.com" + VOICE_MODEL_URL.removeprefix("/github.com")}]}
    voice_args = ["--game", root, "--test-server", base, "--repo", "https://github.com/dnehring/MEVR"]

    # The release carries MEVR-Voice.dll: Install puts it in ASI\voice with the mod.
    (payload / "voice").mkdir(exist_ok=True)
    (payload / "voice" / "MEVR-Voice.dll").write_bytes(b"stand-in MEVR-Voice.dll (test-network.py)")
    rc, out = setup.run(["--install", "--game", root, "--payload", payload, "--no-settings", "--ignore-version"])
    check(rc == 0 and (voice / "MEVR-Voice.dll").exists(), "voice: Install puts the release's MEVR-Voice.dll in ASI\\voice")
    check("voice orders: not installed" in out and "MEVR-Voice.dll present" in out, "voice: the status says not installed, the DLL there")

    # 1. The model isn't published yet (no such release: 404): the rest installs, said so.
    STATE.take_requests()
    rc, out = setup.run(["--install-voice"] + voice_args, timeout=600)
    reqs = [r[0] for r in STATE.take_requests()]
    check(rc == 0 and "isn't published yet" in out, "voice: model not published: the rest installs, said so")
    check(VOICE_TAG_API in reqs and VOICE_LLAMA not in reqs, "voice: the model looked up first; llama.cpp not fetched without it")
    check((voice / "onnxruntime.dll").exists() and hashlib.sha256((voice / "onnxruntime.dll").read_bytes()).hexdigest() ==
          "dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea", "voice: onnxruntime.dll out of Moonshine's release, checked")
    tiny = voice / "models" / "tiny-streaming-en"
    check(all((tiny / f).read_bytes() == (models["tiny"] / f).read_bytes() for f in VOICE_FILES), "voice: the tiny models in place")
    check(not (voice / "llm").exists() and not (voice / "models" / "small-streaming-en").exists(), "voice: nothing else")
    m = read_manifest(root)
    tags = sorted({f.get("content", "") for f in m["files"] if f.get("content")})
    check(tags == ["voice/models-tiny", "voice/recognizer"], f"voice: recorded as content ({tags})")
    check(any(p["id"] == "voice" and p["method"] == "download" for p in m["packs"]), "voice: 'Voice orders' on record in packs")
    check("voice orders: recognizer, models-tiny" in out and "texture packs installed: none" in out, "voice: status lines (not a texture pack)")
    leftovers = list((local / "MEVR" / "downloads" / "voice").glob("*")) if (local / "MEVR" / "downloads" / "voice").exists() else []
    check(not leftovers, f"voice: the download folder emptied ({[p.name for p in leftovers]})")

    # 2. A model file that isn't what Hugging Face serves (another SHA-256): refused, nothing of it placed; then right.
    bad = VOICE_MODELS.format(size="small", name="tokenizer.bin")
    STATE.set(bad, lambda h: send(h, 200, b"x" * 249974))
    rc, out = setup.run(["--install-voice", "--voice-small"] + voice_args, timeout=600)
    check(rc != 0 and "SHA-256" in out, "voice: a file with another SHA-256 is refused")
    check(not (voice / "models" / "small-streaming-en" / "tokenizer.bin").exists(), "voice: the bad file isn't placed")
    STATE.set(bad, file_answer(models["small"] / "tokenizer.bin"))
    rc, out = setup.run(["--install-voice", "--voice-small"] + voice_args, timeout=600)
    check(rc == 0 and all((voice / "models" / "small-streaming-en" / f).exists() for f in VOICE_FILES), "voice: the small models after a retry")

    # 3. The model published with another digest than its file's: refused; then right: llama.cpp and the model, the
    #    small models taken out (not chosen now).
    STATE.set(VOICE_TAG_API, json_answer(200, release("0" * 64)))
    rc, out = setup.run(["--install-voice"] + voice_args, timeout=600)
    check(rc != 0 and not (voice / "llm" / "model.gguf").exists(), "voice: the model with another digest is refused")
    STATE.set(VOICE_TAG_API, json_answer(200, release(gguf_sha)))
    rc, out = setup.run(["--install-voice"] + voice_args, timeout=600)
    llm = voice / "llm"
    check(rc == 0 and (llm / "model.gguf").read_bytes() == gguf, "voice: the published model as ASI\\voice\\llm\\model.gguf")
    check(all((llm / f).exists() for f in ("llama.dll", "ggml.dll", "ggml-base.dll", "libomp.dll", "ggml-cpu-x64.dll")) and
          not (llm / "llama-server.exe").exists() and not (llm / "mtmd.dll").exists(), "voice: llama.cpp's DLLs, not its tools")
    check(not (voice / "models" / "small-streaming-en").exists() and "taking out Recognizer model (small)" in out,
          "voice: the small models taken out when no longer chosen")

    # 4. Checks: by size, by SHA-256; a file changed without changing its size is fetched again.
    rc, out = setup.run(["--check-voice", "--deep", "--game", root])
    check(rc == 0 and "voice files are as installed" in out, "voice: --check-voice --deep passes")
    enc = tiny / "encoder.ort"
    data = bytearray(enc.read_bytes())
    data[100] ^= 0xFF
    enc.write_bytes(bytes(data))
    rc, quick = setup.run(["--check-voice", "--game", root])
    rc2, deep = setup.run(["--check-voice", "--deep", "--game", root])
    check(rc == 0 and rc2 != 0 and "encoder.ort" in deep, "voice: a same-size change: the quick check passes, the deep one finds it")
    STATE.take_requests()
    rc, out = setup.run(["--install-voice"] + voice_args, timeout=600)
    reqs = [r[0] for r in STATE.take_requests()]
    check(rc == 0 and enc.read_bytes() == (models["tiny"] / "encoder.ort").read_bytes(), "voice: Install fetches the changed file again")
    check(VOICE_MODELS.format(size="tiny", name="encoder.ort") in reqs and VOICE_LLAMA not in reqs and VOICE_MODEL_URL not in reqs,
          "voice: only that file is downloaded again")

    # 5. The request from the game names the voice orders; Remove; Uninstall takes the rest.
    request = local / "MEVR" / "setup.request"
    request.parent.mkdir(parents=True, exist_ok=True)
    request.write_text(json.dumps({"page": "content", "packs": ["voice"], "pid": 1}), encoding="utf-8")
    shots = setup.scratch / "voice-shots"
    rc, out = setup.run(["--preview", shots, "--warp", "--request", "--game", root], timeout=240)
    check(rc == 0 and "request text: You asked for: Voice orders." in out and "(installed)" in out.split("voice:")[-1].splitlines()[0],
          "voice: the game's request names the voice orders; the card says installed")
    rc, out = setup.run(["--remove-voice", "--game", root])
    check(rc == 0 and not (voice / "onnxruntime.dll").exists() and not llm.exists() and not (voice / "models").exists(),
          "voice: Remove takes the downloads out")
    check((voice / "MEVR-Voice.dll").exists() and "voice orders: not installed" in out, "voice: MEVR-Voice.dll stays with the mod")
    m = read_manifest(root)
    check(not any(p["id"] == "voice" for p in m["packs"]) and not any(f.get("content") for f in m["files"]), "voice: off the record")
    rc, out = setup.run(["--install-voice", "--no-voice-llm"] + voice_args, timeout=600)
    check(rc == 0 and (voice / "onnxruntime.dll").exists() and not llm.exists(), "voice: --no-voice-llm: the recognizer only")
    rc, out = setup.run(["--uninstall", "--game", root])
    check(rc == 0 and not voice.exists(), "voice: Uninstall takes ASI\\voice out with the mod")
    (payload / "voice" / "MEVR-Voice.dll").unlink()
    (payload / "voice").rmdir()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--setup", required=True)
    parser.add_argument("--fake-game", required=True)
    parser.add_argument("--fake-mem", required=True)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--quick", action="store_true", help="skip the 30 s timeout case")
    parser.add_argument("--keep", action="store_true", help="keep the scratch folder")
    parser.add_argument("--voice-cache", help="a folder with Moonshine's and llama.cpp's release archives (default: %%TEMP%%)")
    args = parser.parse_args()
    for f in (args.setup, args.fake_game, args.fake_mem):
        if not Path(f).exists():
            print(f"not built: {f}")
            return 1
    temp = Path(tempfile.gettempdir()).resolve()
    scratch = Path(tempfile.mkdtemp(prefix="mevr-setup-net-")).resolve()
    assert scratch.is_relative_to(temp)
    local = scratch / "local"
    local.mkdir()
    env = dict(os.environ, LOCALAPPDATA=str(local))
    env.pop("MEVR_FAKE_MEM_FAIL", None)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    print(f"scratch {scratch}, server {base}")
    setup = Setup(args.setup, scratch, env)
    try:
        test_update_check(setup, base, args.quick)
        test_mem_download(setup, base, local, Path(args.fake_mem))
        root, payload = make_fake_game(scratch, Path(args.fake_game), Path(args.repo))
        test_packs(setup, root, payload, local)
        test_request(setup, root, payload, local)
        test_voice(setup, base, root, payload, local, Path(args.repo), args.voice_cache)
    finally:
        server.shutdown()
        if not args.keep:
            shutil.rmtree(scratch, ignore_errors=True)
    print(f"\n{'all checks passed' if not FAILURES else str(len(FAILURES)) + ' FAILED: ' + '; '.join(FAILURES)}")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())

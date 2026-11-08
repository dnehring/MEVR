# SPDX-License-Identifier: MIT
"""Offline updater integration: real Setup, a local release server, disposable game and LOCALAPPDATA.
Never runs or changes the player's game. Includes the downloaded installer's UI handoff in WARP preview mode.
"""
import argparse
import hashlib
import http.server
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import threading
import time
import zipfile

spec = importlib.util.spec_from_file_location("network", Path(__file__).with_name("test-network.py"))
net = importlib.util.module_from_spec(spec)
spec.loader.exec_module(net)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--setup", required=True)
    parser.add_argument("--fake-game", required=True)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    repo, exe = Path(args.repo).resolve(), Path(args.setup).resolve()
    version = re.search(r"project\(MEVR VERSION (\d+\.\d+\.\d+)", (repo / "CMakeLists.txt").read_text()).group(1)
    tag, name = "v" + version, "MEVR-" + version + ".zip"
    prefix = "https://github.com/dnehring/MEVR/releases/download/" + tag + "/"
    scratch = Path(tempfile.mkdtemp(prefix="mevr-updates-")).resolve()
    local = scratch / "local"
    local.mkdir()
    env = dict(os.environ, LOCALAPPDATA=str(local))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), net.Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    setup = net.Setup(exe, scratch, env)
    check = net.check
    print(f"scratch: {scratch}", flush=True)
    try:
        root, payload = net.make_fake_game(scratch, Path(args.fake_game).resolve(), repo)
        rc, _ = setup.run(["--install", "--game", root, "--payload", payload, "--no-settings"])
        check(rc == 0, "initial disposable installation")
        asi = root / "Game/ME1/Binaries/Win64/ASI"
        config = root / "Game/ME1/BioGame/Config"
        manifest = asi / "MEVR-install.json"

        def old_version(value="0.0.1"):
            data = json.loads(manifest.read_text())
            data["modVersion"] = value
            data["gameSettings"] = False
            manifest.write_text(json.dumps(data), encoding="utf-8")

        old_version()
        (asi / "MELEVR.ini").write_bytes(b"; custom profile\r\n[active]\r\nprofile=Mine\r\n[hotkeys]\r\nenabled=1\r\n[profile:Mine]\r\nmenuKey=121\r\nvrMode=3\r\nsfr2ResX=4321\r\n")
        (asi / "LE1MEVR.ini").write_bytes("[Calibration]\r\nheight=1.77\r\n[FuturePlugin]\r\nlabel=私の設定\r\n".encode("utf-16"))
        (config / "GamerSettings.ini").write_bytes(b"[SystemSettings]\r\nResX=4321\r\nAntiAliasing=True\r\n")
        (config / "BIOEngine.ini").write_bytes(b"[SystemSettings]\r\nMotionBlur=True\r\n")
        saves = root / "untouched-saves"
        saves.mkdir()
        (saves / "career.pcsav").write_bytes(b"save sentinel")
        protected = [asi / "MELEVR.ini", asi / "LE1MEVR.ini", config / "GamerSettings.ini", config / "BIOEngine.ini", saves / "career.pcsav"]
        saved = {p: p.read_bytes() for p in protected}
        # Self-authored PE stand-ins with the package version: never load them as game DLLs.
        binary = exe.read_bytes()
        entries = {"MEVR-Setup.exe": binary, "payload/LE1MEVR.asi": binary, "payload/bink2w64.dll": binary}
        for directory in ("fonts", "menu"):
            for file in (payload / directory).rglob("*"):
                if file.is_file(): entries["payload/" + file.relative_to(payload).as_posix()] = file.read_bytes()

        def archive(extra=None, windows_paths=False):
            buffer = io.BytesIO()
            with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as z:
                for path, data in dict(entries, **(extra or {})).items():
                    z.writestr(path.replace("/", "\\") if windows_paths else path, data)
            return buffer.getvalue()

        package = archive(windows_paths=True)  # PowerShell Compress-Archive's Windows paths

        def serve(body=package, digest=True, checksum=False, bad_hash=False, responder=None, asset_url=None, release_tag=tag):
            sha = "0" * 64 if bad_hash else hashlib.sha256(body).hexdigest()
            item = dict(name=name, size=len(body), browser_download_url=asset_url or prefix + name)
            if digest: item["digest"] = "sha256:" + sha
            assets = [dict(name=name.replace(".zip", "-symbols.zip"), size=1, browser_download_url=prefix + "symbols.zip"), item]
            if checksum:
                sum_name = "MEVR-" + version + "-SHA256SUMS.txt"
                sums = (sha + "  " + name + "\n").encode()
                assets.append(dict(name=sum_name, size=len(sums), browser_download_url=prefix + sum_name))
                net.STATE.set("/" + (prefix + sum_name).split("://", 1)[1], lambda h: net.send(h, 200, sums))
            net.STATE.set(net.UPDATE_API, net.json_answer(200, dict(tag_name=release_tag, html_url="https://github.com/dnehring/MEVR/releases/tag/" + release_tag,
                prerelease=False, draft=False, assets=assets)))
            net.STATE.set("/" + (prefix + name).split("://", 1)[1], responder or (lambda h: net.send(h, 200, body)))

        def download():
            return setup.run(["--download-update", "--game", root, "--test-server", base])

        before = net.snapshot(root)
        serve()
        rc, text = download()
        check(rc == 0 and "prepared:" in text, "verified digest download, symbols ignored, Windows ZIP paths accepted")
        check(net.snapshot(root) == before, "download and staging do not modify the game")
        prepared = Path(next(line[10:] for line in text.splitlines() if line.startswith("prepared: ")))
        check(prepared.read_bytes() == binary, "downloaded installer bytes match")

        serve(digest=False, checksum=True)
        rc, _ = download()
        check(rc == 0, "published SHA256SUMS fallback")
        cache_before = set((local / "MEVR/updates").iterdir())
        failures = [
            ("missing checksum", dict(digest=False)),
            ("wrong SHA-256", dict(bad_hash=True)),
            ("truncated download", dict(responder=net.truncated(package))),
            ("foreign repository asset", dict(asset_url="https://github.com/other/repo/releases/download/" + tag + "/" + name)),
            ("traversal", dict(body=archive({"../escaped.txt": b"bad"}))),
            ("alternate data stream", dict(body=archive({"payload/LE1MEVR.asi:evil": b"bad"}))),
            ("case-insensitive duplicate", dict(body=archive({"PAYLOAD/le1mevr.ASI": b"bad"}))),
            ("wrong binary version", dict(body=archive({"payload/LE1MEVR.asi": b"not a PE"}))),
        ]
        for label, options in failures:
            serve(**options)
            rc, _ = download()
            check(rc != 0, label + " refused")
            check(net.snapshot(root) == before, label + ": game untouched")
            check(set((local / "MEVR/updates").iterdir()) == cache_before, label + ": no incomplete cache left")

        # Real process lock: a stand-in launcher under ONLY the fake game root. Wait until it exits.
        launcher = root / "Game/Launcher/MassEffectLauncher.exe"
        launcher.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(Path(os.environ["SystemRoot"]) / "System32/cmd.exe", launcher)
        process = subprocess.Popen([str(launcher), "/d", "/c", "ping -n 5 127.0.0.1 >nul"], creationflags=subprocess.CREATE_NO_WINDOW)
        started = time.monotonic()
        try:
            rc, text = setup.run(["--apply-update", "--game", root], exe=prepared)
        finally:
            process.wait(timeout=15)
        check(rc == 0 and time.monotonic() - started > 2 and "Waiting for Mass Effect" in text, "apply waits for this game's launcher to exit")
        check(all(p.read_bytes() == contents for p, contents in saved.items()), "all custom INIs and saves preserved byte-for-byte")
        check((asi / "LE1MEVR.asi").read_bytes() == binary and (asi / "MEVR-Setup.exe").read_bytes() == binary, "mod and installed Setup updated")
        check(json.loads(manifest.read_text())["modVersion"] == version, "installed manifest records new version")

        old_version("99.0.0")
        newer = net.snapshot(root)
        rc, _ = setup.run(["--apply-update", "--game", root], exe=prepared)
        check(rc != 0 and net.snapshot(root) == newer, "newer installed version cannot be downgraded")

        # UI handoff uses the same App::Tick path as the window, after an old-process wait.
        old_version()
        parent = subprocess.Popen([os.environ["COMSPEC"], "/d", "/c", "ping -n 5 127.0.0.1 >nul"], creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            rc, text = setup.run(["--preview", scratch / "handoff-preview", "--warp", "--finish-update", "--game", root,
                                  "--wait-pid", str(parent.pid)], exe=prepared)
        finally:
            parent.wait(timeout=15)
        check(rc == 0 and json.loads(manifest.read_text())["modVersion"] == version, "downloaded UI waits for old Setup then automatically applies update")
        check(all(p.read_bytes() == contents for p, contents in saved.items()), "UI handoff preserves settings and saves")
        print("Preview:", scratch / "handoff-preview")
    finally:
        server.shutdown()
        if not args.keep: shutil.rmtree(scratch)
    print("all update checks passed" if not net.FAILURES else "FAILED: " + "; ".join(net.FAILURES))
    return 1 if net.FAILURES else 0


if __name__ == "__main__":
    raise SystemExit(main())

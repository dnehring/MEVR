# SPDX-License-Identifier: MIT
"""Python client to actual C++ Endpoint, with fake identity/temp files, never a game."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time
import uuid

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("le2_control", root / "tools/qa-le2/control.py")
control = importlib.util.module_from_spec(spec)
spec.loader.exec_module(control)


def main(executable):
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    process = subprocess.Popen([executable, "--fixture-loop"], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, startupinfo=startup)
    directory = None
    try:
        descriptor = json.loads(process.stdout.readline())
        assert descriptor.get("fixtureOnly") is True
        run = Path(descriptor["runDirectory"])
        assert run.parent.name.startswith("mevr-le2-control-test-")
        directory = run / "control"
        identity = descriptor["identity"]

        def bind(requested):
            assert Path(requested) == run
            current = json.loads((directory / "identity.json").read_text())
            assert current["identity"] == identity
            return directory, identity

        for verb in ("ping", "observe"):
            result = control.submit(run, verb, timeout=5, bind=bind)
            assert result["status"] == "completed"
            payload = json.loads(result["payload"])
            assert payload["source"] == "diagnostic-host" and payload["ready"] is False
            assert payload["capabilities"]["nativeConsole"] is False
            assert "not validated" in payload["unavailable"]["map"]
        result = control.submit(run, "console", "exit", timeout=5, allow_console=True, bind=bind)
        assert result["status"] == "rejected"
        assert len(list((directory / "history").glob("*.result"))) == 3
        assert len(list(directory.glob("consumed-*.request"))) == 3
        assert not (directory / "request.control").exists()
        assert not (directory / "outstanding.json").exists()
        # Deliberately wrong lease: producer must answer with its ACTUAL identity
        # and reject before any work, never mirror the supplied lease.
        wrong = dict(identity, lease=str(uuid.uuid4()), kind="request", id=str(uuid.uuid4()), sequence="4",
                     deadline=str(time.time_ns() // 1000000 + 5000), verb="ping", status="pending", bytes="0", payload=b"")
        control.publish(directory / "request.control", control.encode(wrong))
        deadline = time.monotonic() + 5
        while not (directory / "result.control").exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        rejection = control.decode((directory / "result.control").read_bytes())
        assert rejection["status"] == "rejected" and rejection["lease"] == identity["lease"]
        print("Python/C++ filesystem smoke passed: ping, observe, disabled console, wrong lease; fake process only")
    finally:
        if directory is not None and directory.is_dir():
            (directory / "stop.fixture").touch(exist_ok=False)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()  # only this owned offline test process
            process.wait()
        stderr = process.stderr.read()
        if process.returncode != 0:
            raise RuntimeError(f"Offline fixture process failed: {stderr}")


if __name__ == "__main__":
    main(sys.argv[1])

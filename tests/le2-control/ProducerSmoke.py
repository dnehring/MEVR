# SPDX-License-Identifier: MIT
"""Python client to actual C++ Endpoint, with fake identity/temp files, never a game."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
from unittest import mock

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("le2_control", root / "tools/qa-le2/control.py")
control = importlib.util.module_from_spec(spec)
spec.loader.exec_module(control)
spec = importlib.util.spec_from_file_location("le2_negative", root / "tools/qa-le2/control-negative.py")
negative = importlib.util.module_from_spec(spec)
spec.loader.exec_module(negative)


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

        for verb in ("ping", "observe", "observe-save-menu", "observe-resources", "observe-save-flow", "observe-orbital"):
            result = control.submit(run, verb, timeout=5, bind=bind)
            assert result["status"] == "completed"
            # The observer's 16 KiB budget is measured with compact JSON. The
            # actual host must not add pretty-print whitespace on the wire.
            assert b"\n" not in result["payload"], "host response must use compact JSON"
            payload = json.loads(result["payload"])
            assert payload["source"] == "diagnostic-host" and payload["ready"] is False
            assert payload["capabilities"]["nativeConsole"] is False
            assert payload["capabilities"]["nativeMenuActions"] is False
            assert payload["capabilities"]["nativeSaveMenuActions"] is False
            assert payload["capabilities"]["nativeSaveNew"] is False
            if verb == "observe-save-flow":
                assert payload["capabilities"]["observeSaveFlow"] is False
                assert payload["saveFlow"]["available"] is False
            if verb == "observe-save-menu":
                assert payload["capabilities"]["observeSaveMenu"] is False
                assert payload["saveMenu"]["available"] is False
            if verb == "observe-resources":
                assert payload["capabilities"]["observeResources"] is False
                assert payload["resourceObservation"]["available"] is False
            if verb == "observe-orbital":
                assert payload["capabilities"]["observeOrbital"] is False
                assert payload["orbitalObservation"]["available"] is False
                assert payload["readOnly"] is True
            assert "not validated" in payload["unavailable"]["map"]
        result = control.submit(run, "console", "exit", timeout=5, allow_console=True, bind=bind)
        assert result["status"] == "rejected"
        assert len(list((directory / "history").glob("*.result"))) == 7
        assert len(list(directory.glob("consumed-*.request"))) == 7
        assert not (directory / "request.control").exists()
        assert not (directory / "outstanding.json").exists()
        result = control.submit(run, 'probe-fov', timeout=5, allow_probe_fov=True, bind=bind)
        assert result['status'] == 'rejected'
        assert json.loads(result['payload'])['reason'] == 'native FOV probe is disabled'
        for verb in control.MENU_ACTIONS:
            result = control.submit(run, verb, timeout=5, allow_menu_actions=True, bind=bind)
            assert result['status'] == 'rejected'
            assert json.loads(result['payload'])['reason'] == 'native menu actions are disabled'
        # Only the caller inventory preflight is stubbed in this fake-process
        # fixture. The actual C++ Endpoint must independently reject saving.
        with mock.patch.object(control, 'verify_save_baseline', return_value='a' * 64):
            result = control.submit(run, 'save-new', timeout=5, allow_save_new=True, bind=bind)
        assert result['status'] == 'rejected'
        assert json.loads(result['payload'])['reason'] == 'native new save is disabled'
        for case in ('wrong-lease', 'expired'):
            rejection = negative.probe(run, case, timeout=5, bind=bind)
            assert rejection['status'] == 'rejected' and rejection['lease'] == identity['lease']
        # Neither rejection may poison the next legitimate request.
        assert control.submit(run, 'ping', timeout=5, bind=bind)['status'] == 'completed'
        assert len(list((directory / 'history').glob('*.result'))) == 15
        assert len(list(directory.glob('consumed-*.request'))) == 15
        assert not (directory / 'outstanding.json').exists()
        print('Python/C++ filesystem smoke passed: observations, disabled console/FOV/menu, wrong lease, expiry and recovery; fake process only')
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

# SPDX-License-Identifier: MIT
"""Offline launch tests. Fake installations/providers only; no process dispatch."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest
import shutil

sys.dont_write_bytecode = True


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


l = module("launch_support", "launch_support.py")
fixtures = module("session_test_fixtures", "test-session.py")
s = l.s


class LaunchTests(unittest.TestCase):
    put = fixtures.SessionTests.put
    prepare = fixtures.SessionTests.prepare
    quiet_prepare = fixtures.SessionTests.quiet_prepare

    def setUp(self):
        fixtures.SessionTests.setUp(self)
        self.quiet_prepare()
        self.steam = self.root / "Steam/steam.exe"
        self.put(self.steam, b"fake Steam never executed")
        self.launcher = self.game / "Game/Launcher/MassEffectLauncher.exe"
        self.proxy = self.launcher.parent / "bink2w64.dll"
        self.put(self.launcher, b"fake launcher never executed")
        self.put(self.proxy, b"fake reviewed proxy never executed")
        self.proof = self.root / "proof.txt"
        self.put(self.proof, b"Synthetic offline fixture: NOT native support evidence")
        self.cap = self.root / "capability.json"
        self.cap_data = {"schemaVersion": 1, "kind": "reviewed-le2-launcher-selection",
                         "provider": "LEBinkProxy", "sourcePin": l.SOURCE_PIN,
                         "arguments": ["-game", "2", "-autoterminate"],
                         "method": "source-matched-build", "reviewedBy": "offline test only",
                         "reviewNotes": "Fake fixture, cannot authorize a native run",
                         "launcherExeSha256": s.digest(self.launcher), "proxySha256": s.digest(self.proxy),
                         "evidence": [{"path": "proof.txt", "sha256": s.digest(self.proof)}]}
        s.write_json(self.cap, self.cap_data)
        self.steam_process = {"name": "steam", "pid": 77, "path": str(self.steam),
                              "started": "2026-01-01T00:00:00+00:00", "inJob": False}
        self.live = [self.steam_process]
        self.dispatched = []

    def inspect_plan(self, receipt=True, guard=lambda _: None):
        return l.plan(self.run, self.steam, self.cap if receipt else None,
                      host_guard=guard, processes=lambda: self.live)

    def launch(self, dispatch=None, **kwargs):
        return l.launch(self.run, self.steam, self.cap, host_guard=lambda _: None,
                        processes=lambda: self.live, dispatch=dispatch or self.dispatch, **kwargs)

    def dispatch(self, command):
        self.dispatched.append(command)
        self.live = [self.steam_process, {"name": "MassEffect2", "pid": 88,
                     "path": str(self.exe), "started": s.utc()}]

    def test_plan_exact_command_and_zero_writes(self):
        before = s.snapshot(self.root)
        result = self.inspect_plan()
        self.assertTrue(result["canLaunch"])
        self.assertFalse(result["ready"])
        self.assertEqual(result["command"], {"executable": str(self.steam),
                         "arguments": ["-applaunch", "1328670", "-game", "2", "-autoterminate"]})
        self.assertEqual(before, s.snapshot(self.root))
        self.assertFalse(self.dispatched)

    def test_unknown_host_is_dependency_not_empty_inventory(self):
        def blocked(_):
            raise s.SessionError("redirected LOCALAPPDATA")
        before = s.snapshot(self.root)
        result = self.inspect_plan(guard=blocked)
        self.assertFalse(result["canLaunch"])
        self.assertEqual(result["hostInventory"], "unavailable-or-blocked")
        self.assertEqual(before, s.snapshot(self.root))

    def test_absent_capability_requires_operator_without_mutation(self):
        result = self.inspect_plan(receipt=False)
        self.assertEqual(result["dependencies"][0]["code"], "operator_selection_required")
        self.assertFalse(result["canLaunch"])
        self.assertFalse((self.run / "launch-attempt.json").exists())

    def test_boolean_or_wrong_binary_is_not_capability(self):
        s.write_json(self.cap, {"supportsGame2": True})
        self.assertFalse(self.inspect_plan()["canLaunch"])
        s.write_json(self.cap, self.cap_data)
        self.put(self.proxy, b"different loader")
        self.assertFalse(self.inspect_plan()["canLaunch"])

    def test_capability_artifact_hash_and_traversal(self):
        self.put(self.proof, b"tampered")
        self.assertFalse(self.inspect_plan()["canLaunch"])
        self.cap_data["evidence"][0]["path"] = "../proof.txt"
        s.write_json(self.cap, self.cap_data)
        self.assertFalse(self.inspect_plan()["canLaunch"])

    def test_live_wrong_game_and_existing_launcher_refused(self):
        for name in ("MassEffect1", "MassEffect3", "MassEffectLauncher", "vrserver"):
            with self.subTest(name=name):
                self.live = [self.steam_process, {"name": name, "pid": 2}]
                self.assertFalse(self.inspect_plan()["canLaunch"])
                self.assertFalse(self.launch()["canLaunch"])
        self.assertFalse(self.dispatched)

    def test_steam_in_job_wrong_path_or_ambiguous_refused(self):
        self.steam_process["inJob"] = True
        self.assertFalse(self.inspect_plan()["canLaunch"])
        self.steam_process["inJob"] = False
        self.live = [self.steam_process, dict(self.steam_process, pid=99)]
        self.assertFalse(self.inspect_plan()["canLaunch"])
        self.live = [dict(self.steam_process, path=str(self.root / "elsewhere/steam.exe"))]
        self.assertFalse(self.inspect_plan()["canLaunch"])

    def test_unowned_or_conflicting_lock_refused(self):
        self.put(self.locks / "qa-hubs.lock", b"other owner")
        self.assertFalse(self.inspect_plan()["canLaunch"])
        (self.locks / "qa-hubs.lock").unlink()
        self.put(self.locks / "qa-cafeteria.lock", b"other run")
        self.assertFalse(self.inspect_plan()["canLaunch"])

    def test_game_slot_and_changed_exe_refused(self):
        self.put(self.locks / "game-slot/lock.json", b"{}")
        self.assertFalse(self.inspect_plan()["canLaunch"])
        (self.locks / "game-slot/lock.json").unlink()
        self.put(self.exe, b"changed game")
        self.assertFalse(self.inspect_plan()["canLaunch"])

    def test_corrupt_backup_and_staged_save_refused(self):
        self.put(self.target, b"changed save")
        self.assertFalse(self.inspect_plan()["canLaunch"])
        self.put(self.target, self.fixture.read_bytes())
        self.put(self.run / "before/1/GamerSettings.ini", b"bad backup")
        self.assertFalse(self.inspect_plan()["canLaunch"])

    def test_success_only_binds_process_never_save_or_readiness(self):
        result = self.launch()
        self.assertEqual(len(self.dispatched), 1)
        self.assertEqual(result["status"], "process-bound-operator-load-required")
        self.assertTrue(result["processBound"])
        self.assertFalse(result["ready"])
        context = s.read_json(self.run / "context.json")
        self.assertEqual(context["phase"], "game-bound")
        self.assertEqual(context["nativeAcceptance"], "pending")
        self.assertEqual(self.target.read_bytes(), self.fixture.read_bytes())

    def test_wrong_installation_or_old_process_cannot_bind(self):
        def wrong(command):
            self.dispatch(command)
            self.live[-1]["path"] = str(self.root / "wrong/MassEffect2.exe")
        result = self.launch(dispatch=wrong)
        self.assertFalse(result["processBound"])
        self.assertEqual(result["status"], "failed-operator-required")

    def test_old_process_cannot_bind_even_if_executable_matches(self):
        def stale(command):
            self.dispatch(command)
            self.live[-1]["started"] = "2026-01-01T00:00:00+00:00"
        result = self.launch(dispatch=stale)
        self.assertFalse(result["processBound"])
        self.assertIn("predates", result["dependencies"][-1]["detail"])

    def test_wrong_game_after_dispatch_is_not_killed_or_retried(self):
        def wrong(command):
            self.dispatched.append(command)
            self.live = [self.steam_process, {"name": "MassEffect1", "pid": 12}]
        result = self.launch(dispatch=wrong)
        self.assertFalse(result["processBound"])
        self.assertEqual(len(self.dispatched), 1)
        self.assertEqual(self.live[-1]["name"], "MassEffect1")

    def test_timeout_leaves_recovery_marker_and_prevents_retry(self):
        clock = [0.0]
        result = self.launch(dispatch=lambda command: self.dispatched.append(command), timeout=2,
                             monotonic=lambda: clock[0], sleep=lambda n: clock.__setitem__(0, clock[0] + n))
        self.assertEqual(result["status"], "timeout-operator-required")
        self.assertFalse(result["launched"])
        self.assertFalse(self.inspect_plan()["canLaunch"])
        self.launch()
        self.assertEqual(len(self.dispatched), 1)

    def test_interrupted_dispatch_records_uncertainty_without_retry(self):
        def interrupt(_):
            raise KeyboardInterrupt()
        with self.assertRaises(KeyboardInterrupt):
            self.launch(dispatch=interrupt)
        self.assertEqual(s.read_json(self.run / "launch-attempt.json")["status"], "dispatch-uncertain")
        self.assertFalse(self.inspect_plan()["canLaunch"])

    def test_cli_plan_is_read_only_and_has_no_test_bypass(self):
        before = s.snapshot(self.root)
        result = subprocess.run([sys.executable, "-B", str(Path(l.__file__)), "plan", "--run", str(self.run),
                                 "--steam-exe", str(self.steam)], capture_output=True, text=True, check=True)
        self.assertIn('"canLaunch": false', result.stdout)
        self.assertEqual(before, s.snapshot(self.root))
        result = subprocess.run([sys.executable, "-B", str(Path(l.__file__)), "plan", "--fake-host"],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)

    @unittest.skipUnless(shutil.which("pwsh"), "PowerShell wrapper test requires pwsh")
    def test_powershell_plan_wrapper_has_no_side_effects(self):
        before = s.snapshot(self.root)
        script = str(Path(__file__).with_name("launch.ps1"))
        command = [shutil.which("pwsh"), "-NoProfile", "-File", script,
                   "-RunDirectory", str(self.run), "-SteamExe", str(self.steam),
                   "-Python", sys.executable, "-Plan"]
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        self.assertIn('"mode": "plan"', result.stdout)
        self.assertIn('"canLaunch": false', result.stdout)
        self.assertEqual(before, s.snapshot(self.root))
        conflict = subprocess.run(command + ["-Launch"], capture_output=True, text=True)
        self.assertNotEqual(conflict.returncode, 0)
        self.assertFalse((self.run / "launch-attempt.json").exists())


if __name__ == "__main__":
    unittest.main()

# SPDX-License-Identifier: MIT
"""Offline lifecycle tests: temporary fake installations only, never native startup."""
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location("session", Path(__file__).with_name("session.py"))
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)

SANDBOX_APPDATA = r"C:\Users\test\AppData\Local\Packages\sandbox.{12345678-1234-1234-1234-123456789abc}\AC"
DESKTOP_APPDATA = r"C:\Users\test\AppData\Local"


class HostContextTests(unittest.TestCase):
    def evidence(self, **changes):
        return dict({"localAppData": DESKTOP_APPDATA, "appContainer": False, "restricted": False}, **changes)

    def test_observed_sandbox_is_rejected_before_os_or_process_probe(self):
        for path in (SANDBOX_APPDATA, SANDBOX_APPDATA.upper().replace("\\", "/")):
            with self.subTest(path=path), mock.patch.dict(s.os.environ, LOCALAPPDATA=path), \
                    mock.patch.object(s, "_windows_host_evidence") as probe, \
                    mock.patch.object(s.subprocess, "run") as shell:
                for operation in (s.require_native_host, s.default_locks, s.windows_processes):
                    with self.assertRaisesRegex(s.SessionError, "Sandbox-redirection"):
                        operation()
                probe.assert_not_called()
                shell.assert_not_called()

    def test_restricted_token_and_redirected_or_unknown_folder_are_refused(self):
        cases = [self.evidence(appContainer=True), self.evidence(restricted=True),
                 self.evidence(localAppData=r"C:\Users\another\AppData\Local"),
                 self.evidence(localAppData=SANDBOX_APPDATA), self.evidence(localAppData=None),
                 self.evidence(appContainer=0), self.evidence(restricted=None)]
        for evidence in cases:
            with self.subTest(evidence=evidence), mock.patch.dict(s.os.environ, LOCALAPPDATA=DESKTOP_APPDATA), \
                    mock.patch.object(s, "_windows_host_evidence", return_value=evidence):
                with self.assertRaises(s.SessionError):
                    s.require_native_host()

    def test_failed_os_query_and_missing_environment_fail_closed(self):
        with mock.patch.dict(s.os.environ, LOCALAPPDATA=DESKTOP_APPDATA), \
                mock.patch.object(s, "_windows_host_evidence", side_effect=OSError("token unavailable")):
            with self.assertRaisesRegex(s.SessionError, "Cannot verify"):
                s.require_native_host()
        with mock.patch.dict(s.os.environ, {}, clear=True), mock.patch.object(s, "_windows_host_evidence") as probe:
            with self.assertRaises(s.SessionError):
                s.require_native_host()
            probe.assert_not_called()

    def test_matching_desktop_folder_accepts_case_and_separator_normalization(self):
        with mock.patch.dict(s.os.environ, LOCALAPPDATA=DESKTOP_APPDATA.upper().replace("\\", "/") + "/"), \
                mock.patch.object(s, "_windows_host_evidence", return_value=self.evidence()), \
                mock.patch.object(s, "safe_path", side_effect=lambda p: p):
            self.assertEqual(s.require_native_host(), Path(DESKTOP_APPDATA))

    def test_inventory_requires_scoped_nonempty_envelope(self):
        bad = ["", "[]", "null", "{", json.dumps({"processes": []}),
               json.dumps({"localAppData": SANDBOX_APPDATA, "processes": []}),
               json.dumps({"localAppData": r"C:\elsewhere", "processes": []}),
               json.dumps({"localAppData": DESKTOP_APPDATA, "processes": None}),
               json.dumps({"localAppData": DESKTOP_APPDATA, "processes": [{"name": "MassEffect2", "pid": True}]})]
        for output in bad:
            with self.subTest(output=output), mock.patch.object(s, "require_native_host", return_value=Path(DESKTOP_APPDATA)), \
                    mock.patch.object(s.subprocess, "run", return_value=mock.Mock(stdout=output)):
                with self.assertRaises(s.SessionError):
                    s.windows_processes()

    def test_inventory_preserves_quiet_and_active_results(self):
        for processes in ([], [{"name": "MassEffect2", "pid": 42, "path": "example", "started": "now"}]):
            with self.subTest(processes=processes), mock.patch.object(s, "require_native_host", return_value=Path(DESKTOP_APPDATA)), \
                    mock.patch.object(s.subprocess, "run", return_value=mock.Mock(stdout=json.dumps(
                        {"localAppData": DESKTOP_APPDATA, "processes": processes}))):
                self.assertEqual(s.windows_processes(), processes)
                if processes:
                    with self.assertRaisesRegex(s.SessionError, "active"):
                        s.require_quiet(s.windows_processes)
                else:
                    s.require_quiet(s.windows_processes)


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.profile = self.root / "profile"
        self.game = self.root / "installation"
        self.run = self.root / "run"
        self.locks = self.root / "locks"
        self.exe = self.game / "Game/ME2/Binaries/Win64/MassEffect2.exe"
        self.config = self.game / "Game/ME2/BioGame/Config"
        self.asi = self.exe.parent / "ASI"
        self.loader = self.exe.parent / "bink2w64.dll"
        self.put(self.exe, b"fake executable; never launched")
        self.put(self.profile / "Save/ME2/Original/Save_0001.pcsav", b"original save")
        self.put(self.profile / "GamerProfile", b"global profile")
        (self.profile / "Empty").mkdir()
        self.put(self.config / "GamerSettings.ini", b"original settings")
        self.fixture = self.root / "fixture.pcsav"
        self.put(self.fixture, b"prepared fixture")
        self.recipe = self.root / "recipe.json"
        s.write_json(self.recipe, {"schema_version": 1, "set": {"player.credits": 42}})
        self.scenario = self.root / "scenario.json"
        self.scenario_data = {"id": "test", "fixture": {"baseSaveSha256": "a" * 64},
                              "recipe": {"sha256": s.digest(self.recipe)}}
        s.write_json(self.scenario, self.scenario_data)
        self.receipt = self.root / "receipt.json"
        s.write_json(self.receipt, {"schema_version": 1, "operation": "prepare", "game": "LE2",
                                   "save_version": 30, "input_sha256": "a" * 64,
                                   "output_sha256": s.digest(self.fixture),
                                   "recipe_sha256": s.digest(self.recipe)})
        self.target_relative = "Save/ME2/MEVR-QA/Save_0001.pcsav"
        self.target = self.profile / self.target_relative
        self.plan = {"schemaVersion": 1, "coordinatedOwner": "isolated offline test",
                     "profileRoot": str(self.profile), "gameRoot": str(self.game),
                     "exeSha256": s.digest(self.exe), "recipePath": str(self.recipe),
                     "fixture": {"path": str(self.fixture), "sha256": s.digest(self.fixture),
                                 "targetRelativePath": self.target_relative, "receiptPath": str(self.receipt)},
                     "scenario": {"path": str(self.scenario), "sha256": s.digest(self.scenario)}}
        self.plan_path = self.root / "plan.json"
        self.original = {p: s.snapshot(p) for p in (self.profile, self.config, self.asi, self.loader)}

    def put(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def prepare(self, **kwargs):
        s.write_json(self.plan_path, self.plan)
        return s.prepare(self.plan_path, self.run, locks=self.locks,
                         scenario_loader=lambda _: self.scenario_data, **kwargs)

    def quiet_prepare(self):
        return self.prepare(processes=lambda: [])

    def assertRestored(self):
        for path, state in self.original.items():
            self.assertEqual(s.snapshot(path), state)
        self.assertFalse((self.locks / "qa-cafeteria.lock").exists())

    def fixture_selection(self):
        return {"schemaVersion": 1,
                "descriptor": {"career": "MEVR-QA", "saveName": "Save_0001", "index": 1, "type": 0},
                "fixtureSha256": s.digest(self.fixture), "exeSha256": s.digest(self.exe),
                "targetRelativePath": self.target_relative,
                "evidence": {"runId": "12345678-1234-1234-1234-123456789abc",
                             "observationSha256": "a" * 64, "captureSha256": "b" * 64,
                             "reviewedBy": "offline synthetic mapping review"}}

    def test_fixture_selection_text_rejected_before_backups_or_live_writes(self):
        cases = [("evidence", "reviewedBy", "x" * 257, "256 UTF-8 bytes"),
                 ("evidence", "reviewedBy", "\u00e9" * 129, "256 UTF-8 bytes"),
                 ("evidence", "reviewedBy", " \t\r\n", "identify the reviewer"),
                 ("evidence", "reviewedBy", "review\0note", "without NUL"),
                 ("evidence", "reviewedBy", None, "nonempty text")]
        for field in ("career", "saveName"):
            cases.extend(("descriptor", field, value, reason) for value, reason in (
                ("x" * 256, "255 UTF-16 code units"),
                ("\U0001f600" * 128, "255 UTF-16 code units"),
                ("\u00e9" * 513, "1024 UTF-8 bytes"),
                ("\ud800", "strict Unicode"), ("", "nonempty text")))
        for section, field, value, reason in cases:
            with self.subTest(section=section, field=field, reason=reason):
                self.plan["fixtureSelection"] = self.fixture_selection()
                self.plan["fixtureSelection"][section][field] = value
                processes = mock.Mock(return_value=[])
                with mock.patch.object(s, "copy_snapshot") as backup, \
                        mock.patch.object(s, "stage_fixture") as stage:
                    with self.assertRaisesRegex(s.SessionError, reason):
                        self.prepare(processes=processes)
                    processes.assert_not_called()
                    backup.assert_not_called()
                    stage.assert_not_called()
                self.assertFalse(self.run.exists())
                self.assertFalse(self.locks.exists())
                self.assertFalse(self.target.exists())
                self.assertRestored()

    def test_fixture_selection_text_exact_boundaries_are_accepted(self):
        for reviewer in ("x" * 256, "\u00e9" * 128):
            for leaf in ("x" * 255, "\u754c" * 255, "\U0001f600" * 127 + "x"):
                with self.subTest(reviewer_bytes=len(reviewer.encode("utf-8")), leaf=leaf):
                    selection = self.fixture_selection()
                    selection["evidence"]["reviewedBy"] = reviewer
                    selection["descriptor"].update(career=leaf, saveName=leaf)
                    s.validate_fixture_selection_text({"fixtureSelection": selection})
        # No optional selection remains valid for the existing operator workflow.
        s.validate_fixture_selection_text({})

    def test_valid_fixture_selection_prepare_and_reset_preserves_binding(self):
        self.plan["fixtureSelection"] = self.fixture_selection()
        self.plan["fixtureSelection"]["evidence"]["reviewedBy"] = "\u00e9" * 128
        self.assertEqual(self.quiet_prepare()["phase"], "staged")
        self.assertEqual(s.read_json(self.run / "plan.json")["fixtureSelection"],
                         self.plan["fixtureSelection"])
        s.reset(self.run, processes=lambda: [])
        self.assertRestored()

    def test_full_profile_restore_archives_new_and_deleted_files(self):
        c = self.quiet_prepare()
        self.assertEqual(c["phase"], "staged")
        self.assertEqual(self.target.read_bytes(), self.fixture.read_bytes())
        self.put(self.profile / "GamerProfile", b"changed global profile")
        self.put(self.profile / "Save/ME3/NewCareer/new.pcsav", b"test generated")
        (self.profile / "Save/ME2/Original/Save_0001.pcsav").unlink()
        self.put(self.config / "New.ini", b"new config")
        self.put(self.asi / "new.asi", b"generated mod")
        self.put(self.loader, b"generated loader")
        restored = s.reset(self.run, processes=lambda: [])
        self.assertRestored()
        self.assertTrue(restored["restorationVerified"])
        archive = self.run / restored["protected"][0]["archives"][0]["path"]
        self.assertEqual((archive / "Save/ME3/NewCareer/new.pcsav").read_bytes(), b"test generated")
        self.assertEqual(s.status(self.run)["nativeAcceptance"], "pending")
        self.assertEqual(s.reset(self.run, processes=lambda: [])["phase"], "restored")
        self.put(self.profile / "new-user-save.pcsav", b"new user data")
        with self.assertRaisesRegex(s.SessionError, "subsequently changed"):
            s.reset(self.run, processes=lambda: [])

    def test_staging_interruption_keeps_owned_lock_and_recovers(self):
        def interrupted(source, target):
            self.put(target, b"partial stage")
            raise KeyboardInterrupt()
        with mock.patch.object(s, "stage_fixture", side_effect=interrupted):
            with self.assertRaises(KeyboardInterrupt):
                self.quiet_prepare()
        self.assertTrue((self.locks / "qa-cafeteria.lock").exists())
        s.reset(self.run, processes=lambda: [])
        self.assertRestored()

    def test_restore_interruption_retries_without_losing_first_archive(self):
        self.quiet_prepare()
        relative = "Save/ME2/NewCareer/AutoSave.pcsav"
        self.put(self.profile / relative, b"preserve me")
        copy_snapshot = s.copy_snapshot
        def interrupted(source, target, manifest):
            if Path(target) == self.profile:
                raise KeyboardInterrupt()
            return copy_snapshot(source, target, manifest)
        with mock.patch.object(s, "copy_snapshot", side_effect=interrupted):
            with self.assertRaises(KeyboardInterrupt):
                s.reset(self.run, processes=lambda: [])
        archives = list((self.run / "after").glob("0-*"))
        self.assertEqual(len(archives), 1)
        self.assertEqual((archives[0] / relative).read_bytes(), b"preserve me")
        s.reset(self.run, processes=lambda: [])
        self.assertRestored()
        self.assertTrue(archives[0].exists())
        row, = [row for row in s.archived_saves(self.run)["saves"] if row["change"] == "new-path"]
        self.assertEqual(row["archiveRelativePath"], (archives[0] / relative).relative_to(self.run).as_posix())
        self.assertFalse(row["archiveRestored"])

    def test_corrupt_backup_refuses_before_any_original_changes(self):
        self.quiet_prepare()
        staged = s.snapshot(self.profile)
        self.put(self.run / "before/1/GamerSettings.ini", b"corrupted")
        with self.assertRaisesRegex(s.SessionError, "Backup changed"):
            s.reset(self.run, processes=lambda: [])
        self.assertEqual(s.snapshot(self.profile), staged)
        self.assertTrue((self.locks / "qa-cafeteria.lock").exists())

    def test_live_session_refuses_prepare_and_reset(self):
        active = lambda: [{"name": "MassEffect2", "pid": 123}]
        with self.assertRaisesRegex(s.SessionError, "active"):
            self.prepare(processes=active)
        self.assertFalse(self.run.exists())
        self.quiet_prepare()
        staged = s.snapshot(self.profile)
        with self.assertRaisesRegex(s.SessionError, "active"):
            s.reset(self.run, processes=active)
        self.assertEqual(staged, s.snapshot(self.profile))

    def test_existing_qa_and_game_slot_ownership_refused(self):
        for name in ("qa-cafeteria.lock", "game-slot/lock.json"):
            with self.subTest(name=name):
                lock = self.locks / name
                self.put(lock, b"other owner")
                with self.assertRaisesRegex(s.SessionError, "owner"):
                    self.quiet_prepare()
                self.assertEqual(lock.read_bytes(), b"other owner")
                lock.unlink()

    def test_foreign_lock_prevents_restore(self):
        self.quiet_prepare()
        (self.locks / "qa-cafeteria.lock").write_text("other run")
        with self.assertRaisesRegex(s.SessionError, "own"):
            s.reset(self.run, processes=lambda: [])

    def test_archived_saves_identifies_new_changed_and_staged_bytes_after_reset(self):
        self.quiet_prepare()
        with self.assertRaisesRegex(s.SessionError, "verified session reset"):
            s.archived_saves(self.run)
        original = "Save/ME2/Original/Save_0001.pcsav"
        added = "Save/ME2/NewCareer/AutoSave.pcsav"
        self.put(self.profile / original, b"modified existing save")
        self.put(self.profile / added, b"new autosave")
        self.put(self.profile / "Save/ME1/Other/Save_0002.pcsav", b"other game")
        s.reset(self.run, processes=lambda: [])
        self.assertRestored()
        report = s.archived_saves(self.run)
        rows = {row["originalRelativePath"]: row for row in report["saves"]}
        self.assertEqual({path: row["change"] for path, row in rows.items()},
                         {original: "modified", added: "new-path", self.target_relative: "staged-unchanged"})
        self.assertEqual(report["contextSha256"], s.digest(self.run / "context.json"))
        recovered = self.run / rows[added]["archiveRelativePath"]
        self.assertEqual(recovered.read_bytes(), b"new autosave")
        self.assertEqual(rows[added]["sha256"], s.digest(recovered))
        recovered.write_bytes(b"bad autosave")
        with self.assertRaisesRegex(s.SessionError, "differs from reset receipt"):
            s.archived_saves(self.run)

    def test_target_traversal_and_existing_slot_refused(self):
        for target in ("../outside.pcsav", "Save/ME2/../../outside.pcsav", "C:/outside.pcsav",
                       "Save/ME1/wrong.pcsav", "Save/ME2/Original/Save_0001.pcsav"):
            self.plan["fixture"]["targetRelativePath"] = target
            with self.subTest(target=target), self.assertRaises(s.SessionError):
                self.quiet_prepare()

    def test_nested_run_and_overlapping_protection_refused(self):
        self.run = self.profile / "run"
        with self.assertRaisesRegex(s.SessionError, "separate"):
            self.quiet_prepare()
        self.run = self.root / "run"
        self.plan["additionalProtectedPaths"] = [str(self.profile / "Save")]
        with self.assertRaisesRegex(s.SessionError, "overlap"):
            self.quiet_prepare()

    def test_symlink_entry_is_refused(self):
        link = self.profile / "linked"
        try:
            link.symlink_to(self.recipe)
        except OSError:
            self.skipTest("Symlink creation unavailable to test user")
        with self.assertRaisesRegex(s.SessionError, "Symlink/reparse"):
            self.quiet_prepare()
        self.assertFalse((self.locks / "qa-cafeteria.lock").exists())

    def test_fixture_hash_and_receipt_are_checked(self):
        self.plan["fixture"]["sha256"] = "b" * 64
        with self.assertRaisesRegex(s.SessionError, "fixture hash"):
            self.quiet_prepare()
        self.plan["fixture"]["sha256"] = s.digest(self.fixture)
        receipt = s.read_json(self.receipt)
        receipt["input_sha256"] = "b" * 64
        s.write_json(self.receipt, receipt)
        with self.assertRaisesRegex(s.SessionError, "receipt"):
            self.quiet_prepare()

    def test_load_request_and_binding_are_not_load_or_acceptance(self):
        self.quiet_prepare()
        with mock.patch.object(s.subprocess, "Popen", side_effect=AssertionError("must never launch")):
            c = s.request_load(self.run, processes=lambda: [])
        self.assertEqual(c["phase"], "load-requested")
        entry = {"name": "MassEffect2", "pid": 42, "path": str(self.exe), "started": s.utc()}
        c = s.bind_process(self.run, 42, processes=lambda: [entry])
        self.assertEqual(c["phase"], "game-bound")
        self.assertEqual(c["nativeAcceptance"], "pending")
        entry["started"] = "2000-01-01T00:00:00+00:00"
        with self.assertRaisesRegex(s.SessionError, "predates"):
            s.bind_process(self.run, 42, processes=lambda: [entry])

    def test_attach_is_immutable_collection_not_acceptance(self):
        self.quiet_prepare()
        evidence = self.root / "state.json"
        self.put(evidence, b'{"passed":true}')
        c = s.attach(self.run, [evidence])
        self.assertEqual(c["phase"], "staged")
        self.assertEqual(c["nativeAcceptance"], "pending")
        artifact = self.run / c["evidence"][0]["file"]
        self.assertEqual(s.digest(artifact), c["evidence"][0]["sha256"])
        self.assertEqual(s.request_load(self.run, processes=lambda: [])["phase"], "load-requested")

    def test_late_slot_collision_does_not_overwrite_new_save(self):
        stage = s.stage_fixture
        def collision(source, target):
            self.put(target, b"late save")
            stage(source, target)
        with mock.patch.object(s, "stage_fixture", side_effect=collision):
            with self.assertRaises(FileExistsError):
                self.quiet_prepare()
        self.assertEqual(self.target.read_bytes(), b"late save")
        s.reset(self.run, processes=lambda: [])
        archive = self.run / s.read_json(self.run / "context.json")["protected"][0]["archives"][0]["path"]
        self.assertEqual((archive / self.target_relative).read_bytes(), b"late save")

    def test_modified_plan_or_protected_paths_refused(self):
        self.quiet_prepare()
        context = s.read_json(self.run / "context.json")
        context["protected"][0]["path"] = str(self.root / "unrelated")
        s.write_json(self.run / "context.json", context)
        with self.assertRaisesRegex(s.SessionError, "roots differ"):
            s.reset(self.run, processes=lambda: [])

    def test_game_slot_appearing_after_prepare_blocks_launch_and_reset(self):
        self.quiet_prepare()
        self.put(self.locks / "game-slot/lock.json", b"new owner")
        with self.assertRaisesRegex(s.SessionError, "Game-slot ownership"):
            s.request_load(self.run, processes=lambda: [])
        with self.assertRaisesRegex(s.SessionError, "Game-slot ownership"):
            s.reset(self.run, processes=lambda: [])

    def test_reparse_ancestor_detection_without_symlink_privilege(self):
        original = s.is_link
        with mock.patch.object(s, "is_link", side_effect=lambda p: Path(p) == self.profile or original(p)):
            with self.assertRaisesRegex(s.SessionError, "Symlink/reparse"):
                self.quiet_prepare()

    def test_native_prepare_in_sandbox_cannot_stage_or_own_private_lock(self):
        with mock.patch.dict(s.os.environ, LOCALAPPDATA=SANDBOX_APPDATA), \
                mock.patch.object(s.subprocess, "run", return_value=mock.Mock(stdout="")) as shell:
            with self.assertRaisesRegex(s.SessionError, "Sandbox-redirection"):
                self.prepare()
            shell.assert_not_called()
        self.assertFalse(self.run.exists())
        self.assertFalse(self.locks.exists())
        self.assertRestored()

    def test_native_load_bind_reset_in_sandbox_preserve_staged_run(self):
        self.quiet_prepare()
        before = {p: s.snapshot(p) for p in (self.run, self.locks, self.profile, self.game)}
        with mock.patch.dict(s.os.environ, LOCALAPPDATA=SANDBOX_APPDATA), \
                mock.patch.object(s.subprocess, "run", return_value=mock.Mock(stdout="")) as shell:
            for operation in (lambda: s.request_load(self.run), lambda: s.bind_process(self.run, 42),
                              lambda: s.reset(self.run)):
                with self.assertRaisesRegex(s.SessionError, "Sandbox-redirection"):
                    operation()
                for path, state in before.items():
                    self.assertEqual(s.snapshot(path), state)
            shell.assert_not_called()

    def test_native_paths_cannot_use_different_coordination_root(self):
        desktop = self.root / "verified-desktop"
        with mock.patch.object(s, "require_native_host", return_value=desktop):
            with self.assertRaisesRegex(s.SessionError, "verified desktop coordination root"):
                self.prepare()
        self.assertFalse(self.run.exists())
        self.quiet_prepare()
        before = s.snapshot(self.run)
        with mock.patch.object(s, "require_native_host", return_value=desktop), \
                mock.patch.object(s.subprocess, "run") as shell:
            for operation in (lambda: s.request_load(self.run), lambda: s.bind_process(self.run, 42),
                              lambda: s.reset(self.run)):
                with self.assertRaisesRegex(s.SessionError, "different desktop coordination root"):
                    operation()
                self.assertEqual(s.snapshot(self.run), before)
            shell.assert_not_called()

    def test_explicit_offline_providers_work_in_sandbox_without_host_probe(self):
        with mock.patch.dict(s.os.environ, LOCALAPPDATA=SANDBOX_APPDATA), \
                mock.patch.object(s, "_windows_host_evidence", side_effect=AssertionError("offline only")), \
                mock.patch.object(s.subprocess, "run", side_effect=AssertionError("offline only")):
            self.quiet_prepare()
            s.request_load(self.run, processes=lambda: [])
            s.bind_process(self.run, 42, processes=lambda: [
                {"name": "MassEffect2", "pid": 42, "path": str(self.exe), "started": s.utc()}])
            s.reset(self.run, processes=lambda: [])
            self.assertRestored()


if __name__ == "__main__":
    unittest.main()

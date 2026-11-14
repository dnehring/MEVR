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
        self.put(self.profile / "NewSave.pcsav", b"preserve me")
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
        self.assertEqual((archives[0] / "NewSave.pcsav").read_bytes(), b"preserve me")
        s.reset(self.run, processes=lambda: [])
        self.assertRestored()
        self.assertTrue(archives[0].exists())

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


if __name__ == "__main__":
    unittest.main()

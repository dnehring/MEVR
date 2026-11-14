# SPDX-License-Identifier: MIT
"""Cross-module preparation/restoration checks in temporary fake installations."""
import importlib.util
import contextlib
import io
import datetime as dt
from pathlib import Path
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


session = module("workflow_session", HERE / "session.py")
scenario = module("workflow_scenario", HERE / "scenario.py")
wire = module("workflow_wire", ROOT / "tests/Le2SaveTests.py")
save_tool = wire.le2


class WorkflowTests(unittest.TestCase):
    def test_each_catalog_template_uses_real_recipe_receipt_and_session_validation(self):
        for template in sorted((HERE / "scenarios").glob("*.json")):
            with self.subTest(template=template.name), tempfile.TemporaryDirectory() as temp:
                root = Path(temp).resolve()
                profile, game, run = root / "profile", root / "game", root / "run"
                profile.mkdir()
                (profile / "GamerProfile").write_bytes(b"original profile")
                exe = game / "Game/ME2/Binaries/Win64/MassEffect2.exe"
                exe.parent.mkdir(parents=True)
                exe.write_bytes(b"FAKE EXE NEVER LAUNCHED")
                base = root / "base.pcsav"
                base.write_bytes(wire.fixture())
                prepared, receipt, recipe_path = root / "prepared.pcsav", root / "receipt.json", root / "recipe.json"
                recipe = {"schema_version": 1, "id": "offline-integration-probe",
                          "expected_input_sha256": session.digest(base),
                          "expect": {"player.probes": 7}, "set": {"player.probes": 1}}
                session.write_json(recipe_path, recipe)
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(save_tool.main(["prepare", str(base), str(prepared), "--recipe",
                                                     str(recipe_path), "--receipt", str(receipt)]), 0)
                bound = scenario.load(template)
                bound["status"] = "ready"
                bound["fixture"]["baseSaveSha256"] = session.digest(base)
                bound["recipe"] = {"path": "recipe.json", "sha256": session.digest(recipe_path)}
                bound["expectedStart"] = {"map": "Biop_Nor", "checks": [
                    {"field": "resources.probes", "op": "eq", "value": 1}]}
                bound_path = root / "scenario.json"
                session.write_json(bound_path, bound)
                scenario.load(bound_path, for_run=True)
                plan = {"schemaVersion": 1, "coordinatedOwner": "synthetic integration test",
                        "profileRoot": str(profile), "gameRoot": str(game),
                        "exeSha256": session.digest(exe), "recipePath": str(recipe_path),
                        "fixture": {"path": str(prepared), "sha256": session.digest(prepared),
                                    "receiptPath": str(receipt),
                                    "targetRelativePath": "Save/ME2/TEST_ONLY/Save_0001.pcsav"},
                        "scenario": {"path": str(bound_path), "sha256": session.digest(bound_path)}}
                plan_path = root / "plan.json"
                session.write_json(plan_path, plan)
                initial_profile, initial_bin = session.snapshot(profile), session.snapshot(exe.parent)
                context = session.prepare(plan_path, run, processes=lambda: [], locks=root / "locks")
                self.assertEqual(context["phase"], "staged")
                target = Path(context["fixtureTarget"])
                self.assertEqual(save_tool.Save.load(target).value("player.probes"), 1)
                # A frozen relative recipe needs the documented override.
                frozen = scenario.load(run / "scenario.json", True, run / "recipe.json")
                self.assertEqual(frozen["fixture"]["baseSaveSha256"], session.digest(base))
                self.assertEqual(session.request_load(run, processes=lambda: [])["phase"], "load-requested")
                identity = {"runId": context["runId"], "scenarioSha256": context["scenarioSha256"],
                            "baseSaveSha256": session.digest(base),
                            "preparedSaveSha256": context["fixtureSha256"],
                            "gameExeSha256": context["exeSha256"],
                            "recipeSha256": context["recipeSha256"]}
                self.assertFalse(scenario.bind_session(identity, run)["processBound"])
                process = {"pid": 12345, "name": "MassEffect2", "path": str(exe),
                           "started": (dt.datetime.now(dt.timezone.utc) + dt.timedelta(seconds=1)).isoformat()}
                session.bind_process(run, 12345, processes=lambda: [process])
                self.assertTrue(scenario.bind_session(identity, run)["processBound"])
                self.assertFalse(scenario.bind_session(identity, run)["restorationVerified"])
                with self.assertRaisesRegex(ValueError, "preparedSaveSha256"):
                    scenario.bind_session({**identity, "preparedSaveSha256": "0" * 64}, run)
                with self.assertRaisesRegex(ValueError, "preparation receipt"):
                    scenario.bind_session({**identity, "baseSaveSha256": "0" * 64}, run)
                state = {"source": "runtime-observation", "state": {
                    "map": "Biop_Nor", "resources": {"probes": 1}}}
                # Validator mechanics only: this synthetic state is never runtime proof.
                self.assertEqual(scenario.check_start(frozen, state), [])
                state["state"]["resources"]["probes"] = 2
                self.assertTrue(scenario.check_start(frozen, state))
                (profile / "GamerProfile").write_bytes(b"simulated save selection")
                (exe.parent / "new-log.txt").write_text("synthetic log", encoding="utf-8")
                restored = session.reset(run, processes=lambda: [])
                self.assertTrue(restored["restorationVerified"])
                self.assertTrue(scenario.bind_session(identity, run)["restorationVerified"])
                self.assertEqual(session.snapshot(profile), initial_profile)
                self.assertEqual(session.snapshot(exe.parent), initial_bin)
                self.assertEqual(session.digest(base), recipe["expected_input_sha256"])


if __name__ == "__main__":
    unittest.main()

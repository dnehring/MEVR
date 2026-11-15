# SPDX-License-Identifier: MIT
"""Game identity for the existing QA workflow; descriptions do not enable a runner.

LE3 has read-only prefix inspection, not prepared fixtures or native sessions.
Never promote an inspection/no-op copy receipt into preparation evidence.
"""
import re

GAMES = {
    "LE2": {"save_version": 30, "exe": "Game/ME2/Binaries/Win64/MassEffect2.exe",
            "save_root": "Save/ME2", "config": "Game/ME2/BioGame/Config",
            "bin": "Game/ME2/Binaries/Win64", "native_session_supported": True,
            "save_tool": "tools/le2-save.py", "save_scope": "full-decode", "mutations": True},
    "LE3": {"save_version": 59, "exe": "Game/ME3/Binaries/Win64/MassEffect3.exe",
            "save_root": "Save/ME3", "config": "Game/ME3/BioGame/Config",
            "bin": "Game/ME3/Binaries/Win64", "native_session_supported": False,
            "save_tool": "tools/le3-save.py", "save_scope": "prefix-and-whole-file-crc", "mutations": False},
}


def describe(game):
    if not isinstance(game, str) or game not in GAMES:
        raise ValueError("Unknown QA game")
    return dict(game=game, **GAMES[game])


def require_session_game(game):
    contract = describe(game)
    if not contract["native_session_supported"]:
        raise ValueError(f"{game} native sessions are disabled; read-only save inspection is not fixture readiness")
    return contract


def validate_preparation_receipt(receipt, game, *, input_sha256, output_sha256, recipe_sha256):
    contract = require_session_game(game)
    if not isinstance(receipt, dict):
        raise ValueError("Preparation receipt must be an object")
    hashes = (input_sha256, output_sha256, recipe_sha256)
    if any(not isinstance(h, str) or re.fullmatch(r"[0-9a-f]{64}", h) is None for h in hashes):
        raise ValueError("Expected preparation hashes must be lowercase SHA256")
    if (receipt.get("schema_version") != 1 or receipt.get("operation") != "prepare" or
            receipt.get("game") != game or receipt.get("save_version") != contract["save_version"] or
            receipt.get("full_decode") is False or receipt.get("fixture_ready") is False or
            receipt.get("input_sha256") != input_sha256 or receipt.get("output_sha256") != output_sha256 or
            receipt.get("recipe_sha256") != recipe_sha256):
        raise ValueError("Preparation receipt has wrong game, stale hashes or insufficient validation")

---
type: Gotcha
title: A test run in the game overwrites the player's saves
description: Any run of the real game for a test (a map opened from the command line, the Mako drop, a resumed save, a conversation's end) can autosave into the player's own career folder and rewrite the profile, so the player's Resume then loads the test's save.
tags: [le1, test-loop, saves, fake-headset, shared-machine]
status: stable
timestamp: 2026-11-06T11:00:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-06T11:00:00Z }
bitten: [2026-11-05, 2026-11-06, 2026-11-06]
---

# Symptom

The player's newest save is a stranger: Resume loads a fresh Shepard, the Mako on another planet or a test position
instead of where they stopped. The save folder (`Documents\BioWare\Mass Effect Legendary Edition\Save\ME1`) shows the
career's `*_AutoSave.pcsav` written during someone's test, often much smaller than before, and `PROFILE\GamerProfile.pcsav`
and `Launcher_Profile` with the same time.

| date | what ran | what it wrote |
|---|---|---|
| 2026-11-05 | the Mako exit crash repro on the fake headset (the user's own save, `tools/mako-start.ps1`) | `PROFILE\GamerProfile.pcsav`, `VR00\VR00_AutoSave.pcsav`; put back from a backup |
| 2026-11-06 10:46 | a swapchain check: `UNC20`'s party pick and the Mako drop | `John01\John01_AutoSave.pcsav` 5061 -> 2159 bytes over the player's Eden Prime autosave from 30 minutes before, the profile and `Launcher_Profile`; found by a backup comparison and put back |
| 2026-11-06 12:39 | a conversation measurement: `open BIOA_STA00` from the front end, the medical bay conversation played to its end | the conversation's end into gameplay autosaved: `John01\John01_AutoSave.pcsav`, `PROFILE\GamerProfile.pcsav`, `Launcher_Profile`, `TrilogyGamerProfile` (its time); found by the hash comparison after the run and put back (research/59) |

# Cause

The game autosaves on level entry and at scripted points (the Mako drop, a party pick) into the career that is loaded,
and a map opened from the command line or a fresh Shepard still writes into a career folder and the profile. Nothing in
our test tools keeps the game from saving, and several sessions share one machine and one player.

# Check

Before a run, and after it, compare the save folder with a backup: file times and hashes of `Save\ME1\**` and the game's
`BioGame\Config\*.ini`. A file written during the run's window is the run's.

# Fix in place

Procedure, not code: **back up `Documents\BioWare\Mass Effect Legendary Edition` (robocopy) and the inis (`ASI\LE1MEVR.ini`,
`ASI\MELEVR.ini`, `BioGame\Config\BIOEngine.ini`, `BIOGame.ini`, `GamerSettings.ini`) before every run of the game, and
compare and restore after it**, keeping the run's own files aside. If a run is found later, restore from the newest backup
taken before it. A memory note says the same for each session.

# Citations

[1] [tools/mako-start.ps1](../../../tools/mako-start.ps1): `open BIOA_UNC20` and the squad screen's party pick, after which the
level's rover drop autosaves.
[2] [tools/launch-game.ps1](../../../tools/launch-game.ps1): `-Map` opens a map with a fresh Shepard; without it, Enter at the
title resumes the player's newest save.

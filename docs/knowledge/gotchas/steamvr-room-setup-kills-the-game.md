---
type: Gotcha
title: SteamVR's room setup kills the game at the title on the fake headset
description: With the null driver's chaperone universe unregistered, vrmonitor launches room setup as a scene app after every SteamVR start and vrserver kills the running game 5 s later, with no crash record.
tags: [steamvr, fake-headset, test-loop, launch]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-27]
---

# Symptom

On the fake headset the game dies a few seconds after the title: Steam shows it running, then not; no breadcrumbs, no
error in the log. The player's words: "it does the null headset but I can't actually engage with the game".

# Cause

The null driver's chaperone universe is "2". Not registered in SteamVR's `config\chaperone_info.vrchap`, the
calibration state is 201 (universe uninitialised): vrmonitor logs "Launching room setup, because calibration state was
201", room setup is a scene app, vrserver sends the running scene app a Quit event and kills it 5 s later ("Kill process
MassEffect1 because it didn't quit in time"). It happened after every SteamVR restart (three deaths on 2026-10-27).

# Check

vrserver's log: "Found universe 2 in chaperone file"; vrmonitor's: "Chaperone does not indicate room setup".
`tools/fake-headset.ps1 -Status` prints whether universe 2 is registered.

# Fix in place

`tools/fake-headset.ps1 -On` backs the chaperone file up (`%LOCALAPPDATA%\MEVR\fake-headset\`) and adds universe 2 (a
1 x 1 m square, standing and seated at identity) before starting SteamVR; `-Off` restores the backup. The game must not
be running while the script changes drivers.

# Citations

[1] [research/35, "Setup for measuring without the headset", the chaperone bullet](../../../research/35-performance-attempt-log.md).
[2] [tools/fake-headset.ps1](../../../tools/fake-headset.ps1): the header.

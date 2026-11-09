<!-- SPDX-License-Identifier: MIT -->
# Citadel regression routes

The Citadel uses `BIOA_STA00` as its persistent map. Presidium (`STA20`), C-Sec/docks (`STA30`), Wards (`STA60`)
and Tower (`STA70`) are streamed areas. The runner must check both the map and the starting coordinates/deck;
map identity alone cannot distinguish a usable hub save from the arrival sequence or a different area.

## Coverage

| Scenario | Intended workload | Current evidence |
|---|---|---|
| `citadel-csec` | Short interior circulation through C-Sec, followed by a stationary sweep | Historical C-Sec save and spot exist; exact yaw and controller path pending |
| `citadel-presidium` | Promenade movement and long scenery sight lines | Historical heavy static scenery sample; walking route pending |
| `citadel-wards-flux` | Market-to-club transition, stairs and ambient characters | Historical market/club samples; connecting route pending |

These manifests deliberately remain `draft`, with unknown start poses and no invented waypoints. The shared
harness must refuse them until a checked controller route is available. The local save inventory has a suitable
post-arrival C-Sec save; its identity and hash stay in private QA evidence rather than shipping player data.

## Acquisition attempt on 2026-11-08

A protected High-setting fake-headset launch succeeded. The first desktop game-window state capture then hung
before any menu input was sent. The Citadel save was not selected, no controller route was attempted, and no
performance sample was taken. The owned game was closed through the test-loop console, all 42 protected files
were restored and hash-verified, and the QA lock was removed. No game/SteamVR processes or requests remained;
the original ASI and global Virtual Desktop runtime were retained.

Private receipt: `scratch/qa-hubs/citadel/session-20261108-204032-958/`, including `context.json`,
`owned-process.json`, `begin-transcript.txt`, `finish-transcript.txt`, `acquisition.json`, `restored.json` and
`final-state.json`. `scratch/qa-hubs/citadel/begin-session.ps1` and `finish-session.ps1` preserve the exact guarded
setup/recovery commands. A retry should inspect the game's own snapshot/quad output instead of querying the
3D application's desktop window state. Save-selection and walking remain unvalidated.

The private retry helpers are `scratch/qa-hubs/citadel/menu-step.ps1` and `menu-pulse.py`. They use the game's
own snapshot/replay APIs: inspect an image, then supply that latest observation, image hash and observed menu
label for one short controller input. Every input consumes its observation and requires a fresh image afterward;
expired observations cannot drive a retry. They are syntax/recording-format checked only and have not yet
demonstrated save selection. No automated menu sequence or current Citadel route is claimed.

A second protected launch was prepared, then paused at the user's request before any snapshot, menu input,
save selection or walking. The owned process received normal replay-stop/exit commands; recovery restored and
hash-verified all 42 protected files, archived two test changes, removed the QA lock, and found no remaining
game/SteamVR processes, requests or mismatches. The installed ASI and global Virtual Desktop runtime were retained.
Receipt: `scratch/qa-hubs/citadel/session-20261108-210817-830/`. All other-map work is on hold; do not resume native
acquisition until explicitly authorized. The existing manifests remain draft.

## Evidence boundary

[Research 50](../../../research/50-citadel-crowds.md) measured C-Sec, the Presidium, Wards, Flux and Chora's Den.
Those old samples used forced stream events, ghost mode and pawn-location writes. Their coordinates identify
places worth testing, but do not prove walkable connections, save reproducibility or current-build performance.
The new routes must use ordinary controller movement and game interaction, with no pawn writes or forced streams.

## Native validation procedure

1. Obtain exclusive game/SteamVR ownership and hash-check the reviewed ASI.
2. Back up the full profile/save tree, game settings, ASI INIs/binary and affected SteamVR files with the shared harness.
3. Load the chosen gameplay save through normal game menus. Confirm `BIOA_STA00`, gameplay mode, exact position and yaw.
4. Walk short checked segments, stop at collisions, and record waypoints and arrival images. Do not infer a route from endpoints.
5. Preload the turn replay and settle before a clean 90-second capture; collect no extra screenshots or replay loads inside it.
6. Exit only the owned game, restore every protected file, verify hashes and retain the restoration receipt.

Arrival images and route transcripts establish traversal. Clean head-turn captures establish stationary workload
measurements. Traversal replay replacement can itself hitch and is excluded from the clean performance gate.

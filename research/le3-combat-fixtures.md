<!-- SPDX-License-Identifier: MIT -->
# ME3 combat fixture acquisition

2026-11-15. Follow-up to the [save foundation](le3-save-foundation.md) and
[reuse audit](../docs/le3-port-reuse-audit.md). The goal is a normally loadable
weapon test checkpoint, followed by a separate hostile-combat checkpoint.
Neither is established by a save's class name or a successful renderer capture.

## Existing saves and tools

The fresh read-only inventory still contains six saves in one career. Four
Normandy saves have a VanguardNonCombat player and no player weapon entries.
The chapter save has a combat Vanguard and three weapon entries, but its earlier
native test enters cinematic/injured modes rather than ordinary combat. The
ending NG+ save reports an injured player and completed end state. Do not repeat
the rejected chapter test without new evidence or start another campaign merely
to obtain a fixture.

Reuse `tools/le3-save.py` for decoding and independent roundtrip-checked resource
variants. Its permitted scalar edits do not reconstruct a mission, loadout or
streaming state. Reuse `tools/le2-halcyon-run` with `-Game LE3 -RendererPreview`
for ownership, backups, normal descriptor loading and restoration. Drive existing
`console.ps1`, `replay.ps1 -Inputs`, `snapshot.ps1` and the walk/single-A generators;
do not create another request channel. ME1's `power-arena.ps1` and galaxy-use
commands contain ME1 world/native assumptions and are not ME3 fixture shortcuts.

## Installed Citadel range sequence

Read-only inspection of the installed base `BioD_CitHub_Embassy` package, using
the existing LegendaryExplorerCore scanner and its compatible nightly assemblies,
found the authored `Main_Sequence.Shooting_Range` entry/exit flow. These are
original observations; extracted package data remains private.

- A normal door interaction and confirmation start range streaming.
- Entry disables saving, interrupts ambient dialogue and fades the screen before
  switching to the combat pawn and moving to the authored range start.
- The range uses streaming state `stream_shootingrange` and the authored
  `start_shootingrange` destination.
- Exit switches back to the casual/noncombat representation, returns to
  `start_embassy_shootingrange`, removes the range streaming state and restores
  saving through the normal sequence.

Therefore acquire a native checkpoint **outside the range**, enter by the door,
and let the game create its combat pawn/weapons. Do not force streaming, invoke
an isolated remote event, toggle the pawn directly or edit a map name to mimic
this sequence. The source audit identifies a candidate route; it does not prove
the current career can access it or that its weapon set is suitable.

The range can test weapon poses, optics, ammunition and reload observations.
It cannot substitute for hostile AI, damage, targeted powers, squad behavior,
melee admission or class-specific outcomes.

## Native acquisition evidence

Protected session `20261115-184202-305-LE3-map-acquire` normally loaded Save 31
and moved Shepard through the Normandy CIC doorway using the existing stick
replay. Nine captures and native state samples retain ordinary mode 0. Four
single-A trials did not enter galaxy mode; the final eye image still shows the
table several metres ahead. This is an incomplete approach, not evidence that
Use or galaxy input is broken. The read-only galaxy observer correctly reports
no active map context outside the map.

The run exited normally, restored profile/game files with zero differences and
released the shared game lock. Independent post-run hashes matched the saved
null-driver, SteamVR, chaperone and LE1 ini receipts. No new save was acquired.

The longer approach `20261115-184731-505-LE3-map-approach` reaches the table;
17 captures remain in mode 0, with no selected interaction at the table and a
door selected farther on. The shared interaction status reports no installed
gaze hook. Restoration again reports zero differences and independently matching
VR/LE1 hashes. Do not repeat the straight walk: validate the
[audited ME3 gaze adapter](le3-interaction-gaze.md) and exact interaction point first.

The subsequent gaze-adapter run `20261115-190610-153-LE3-gaze-selection` identifies
that table as the **War Room**. Looking down-left selects its point of interest,
shows the native War Terminal prompt and a normal A press opens War Assets
(mode 9). The earlier CIC/galaxy-table identification was incorrect. This proves
one interaction path and supplies an exact navigation landmark; it does not
establish galaxy access or acquire a combat checkpoint. Its five captures have
no errors and restoration again reports zero differences with matching VR/LE1 hashes.

### Normandy route and native door

The installed Normandy packages distinguish the War Terminal (`poi_strikeTeam`,
`BioD_Nor_204aWarRoom`) from the actual galaxy console (`placu_galaxyMap`,
`BioD_Nor_203CIC`). The console's authored location is approximately (2, 3131, 97).
The base Normandy package identifies the War Room door at (280, 6478, 0),
the security/War Room door at (1050, 4705, 0), and the security/CIC door at
(1050, 3880, 0). These are navigation landmarks, not teleport destinations or
proof of an unobstructed straight path.

Protected run `20261115-191348-134-LE3-navigation` normally loads Save 31 in the
War Room at (-2267, 6498, 91). Its native prompt identifies the east exit as
**Tech Labs / A Open**, selecting `BioD_Nor_200...SFXDoor_6`. A press at roughly
4.91 m does not open it; after approaching to the observed 2.76 m selection
distance, one A press visibly opens the door and clears selection. A subsequent
left sidestep and forward walk crosses it to (1081, 6462, 91), still in ordinary
mode 0 with the same player/world. Forward input off-centre stops against the
door frame; farther inside, the direct sideways route stops against a railing.
Neither collision establishes an input defect. Inspect the corridor before
continuing toward the security doors.

This adds native door selection, Use and passage evidence to the War Assets
interaction. It does not establish galaxy access, a new save or a combat fixture.
The bounded run ended normally, restored with zero differences and released the
shared lock; all four independently checked VR/LE1 environment hashes matched.

### Reusable native CIC checkpoint

The follow-up protected run `20261115-192601-688-LE3-navigation` repeats the
door route, goes around the Tech Labs railing, passes the authored security
corridor and reaches the actual CIC galaxy platform. The direct side approach
stops at the platform's collision; the walkable rear approach is around
Y=3668, followed by alignment near X=0 and movement toward the console.
Replay durations are not position guarantees: inspect native coordinates and
both-eye captures at each landmark rather than treating the route as a blind macro.

The game's existing `BioPlayerController.QuickSave` exec creates a checkpoint
at approximately **(-56, 3468, 130)**. A private copy is preserved before cleanup:
`galaxy-console-quick-save.pcsav` in that session directory, SHA-256
`f3ecb9f11cc21720ce7eea96d2bf812c073ec4d875512d544bc1993f0fb14b81`.
It remains a VanguardNonCombat Normandy checkpoint, not a weapon fixture.

The existing protected `load.ps1` normally reloads this new QuickSave. Its receipt
verifies the load transition, exact descriptor, ordinary gameplay, unchanged
source hash and unchanged runtime artifacts. The world/pawn identities change;
the new pawn returns to the saved position. The final stereo capture shows the
galaxy platform from that position. The existing full v59 codec and independently
pinned TSE oracle also agree on every field and byte-identical reconstruction.
The oracle's generated resource probe is offline only and was never installed.

The native **Galaxy Map / A Use** prompt is observed on the side approach, but
the subsequent attempts do not establish mode 11/12 entry. The read-only galaxy
observer still reports no consistent map context. Do not confuse the decorative
galaxy hologram in the CIC with an active travel interface. Next inspect the
console interaction from the reloaded checkpoint; no repeat walk from Save 31
is necessary. Any future fixture staging must occur after the protected profile
backup and preserve the original career/save identity and hashes.

All 23 captures report no snapshot errors. Cleanup restores the original
QuickSave byte-for-byte, reports zero profile/game differences, restores all four
independently checked VR/LE1 environment hashes and releases the game lock.
Both newly created native saves remain private in the session directory.

Next: establish actual galaxy entry and native bindings, then
use available native travel to reach the Citadel. Confirm range admission and
weapon identity before treating it as a fixture. Make a native save outside the
range, preserve a private copy before restoration (an overwritten QuickSave is
restored to its original bytes), decode it and verify a normal reload. Only then
prepare bounded resource variants with the existing independent oracle flow.

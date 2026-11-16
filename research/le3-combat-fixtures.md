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

Next: navigate to the actual galaxy console, observe map entry and native bindings, then
use available native travel to reach the Citadel. Confirm range admission and
weapon identity before treating it as a fixture. Make a native save outside the
range, preserve a private copy before restoration (an overwritten QuickSave is
restored to its original bytes), decode it and verify a normal reload. Only then
prepare bounded resource variants with the existing independent oracle flow.

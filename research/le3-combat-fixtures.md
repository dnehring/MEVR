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

### Citadel availability audit

The next protected run (`20261115-194205-273-LE3-navigation`) proves actual
galaxy entry, orbit exit and relay activation; see
[native galaxy results](le3-galaxy.md). It also exposes why Save 31 is a poor
Citadel candidate. Read-only inspection of the installed galaxy package identifies
Serpent Nebula as `SFXCluster_0`, with visibility conditional 2744 and usability
conditional 79. LegendaryExplorerCore's existing CND reader/decompiler identifies
the latter as the Citadel-in-Widow check, requiring plot integer 10185 to be zero.
Save 31 stores 38, as do the chapter and ending saves. Do not attempt to repair
this by toggling one flag: it would not reconstruct a coherent earlier campaign.

The original AutoSave, QuickSave and LegendSave omit that integer. Absence is a
candidate for the normal default, not standalone proof of native access. The
original AutoSave is already at the console, approximately (-2, 3389, 131), and
is therefore the next untouched candidate. Its SHA-256 is
`6c408498e8920036378100b8e878f5bf043327af0cd8cbcf9873f9860a2c9cc2`.
The original QuickSave is at the War Room table, and LegendSave elsewhere in the
War Room. The earlier inventory grouped all four as noncombat Normandy saves;
that was insufficient for choosing a route and missed these important differences.

The conditional audit uses the already installed LegendaryExplorerCore API,
not a new plot codec. The installed `Conditionals.cnd` SHA-256 is
`059867744f2f1619ae9e099e4e7a07cfaeaabaf9c94d705c8b0090a91ecc1a75`.
Raw decompilation, plot dumps and saves remain private. No plot value was changed.

The subsequent protected run (`20261115-195711-266-LE3-navigation`) loads
that untouched AutoSave normally and enters the native Anadius system map
(mode 11, camera state 3, world 120300). Capture `2026-11-15_19-59-20-698`
shows Cronos Station with **Assault the Illusive Man's Base** available. This
establishes a useful earlier mission candidate, not Citadel admission or a
combat-ready checkpoint. The run is closed; restoration reports zero profile
and game differences and releases the lock. The wrapper exits nonzero with an
ownership-guard error during concurrent stop handling; do not label the entire
run a clean pass. A subsequent process check finds no Mass Effect game running.

### Reuse the ME1 story rewind workflow

`tools/le1-save.py diff <before> <after> --rewind-out beat.json` records changed
plot booleans, integers and floats with their earlier values. Applying
`write <later> <out> --rewind beat.json` restores those values in a new save.
Optional `--with-maps` also restores the affected maps' saved world state,
including other progress made in those maps. A known before/after pair or an
existing suitable recipe is needed; a late save alone does not reconstruct its
entire campaign history.

Reuse this comparison-and-recipe workflow for ME3, not the ME1 binary writer,
plot IDs or legacy-map serialization. The existing `tools/le3-save.py` already
fully decodes and round-trips v59 saves, but its `prepare` edit allowlist covers
resources and existing weapon ammunition only. Story rewind is **not yet
implemented for ME3**. Extend that existing adapter with game-specific typed
plot comparisons, explicit source hashes and expected values, then audit ME3
quest and saved-world dependencies before permitting writes. Validate private
outputs with the existing independent TSE oracle and a normal protected native
load; successful serialization alone does not prove a replayable story beat.
Prefer the untouched earlier AutoSave for initial fixture acquisition while
this additional capability remains unimplemented.

### Normal mission entry from the original AutoSave

Run `20261115-200511-376-LE3-navigation` reaches Cronos Station with native
left-stick steering, enters orbit, docks and accepts the briefing reply
**We go now!**. The game then plays the authored pre-assault cabin sequence;
another native reply advances it. This establishes normal mission admission,
but the bounded run ends before combat. See the
[dialogue evidence](le3-dialogue-cinema.md) for the pad and capture details.

The game updates LegendSave during the transition. The attempted private copy
fails its process-identity check, and restoration completes before preservation;
there is **no retained new checkpoint** from this run. The original saves are
restored byte-for-byte. The private wrapper now preserves changed native saves
after stopping the owned writer and before restoration, with restoration still
in a `finally`. Run `20261115-203330-839-LE3-navigation` subsequently proves this
path after correcting PowerShell JSON-array enumeration: changed AutoSave and
LegendSave files are retained and pass the independent TSE oracle. Their hashes
are `460b22fa4fb52400f2ce202ffbe5a3e3c1d94a35c2fe093f9594bb01a718339d`
and `eef6ab6ab367940fde7337be667a7939a8a7a2aefcb0c496dddb1c0583c31cd5`.
Both are still Normandy noncombat checkpoints without weapons; normal reload
and combat admission are not established for these copies.

A further read-only decode identifies an overlooked existing candidate: the
original LegendSave already has active `stream_Cat004Intro` and
`stream_Cat004Intro_Liara`, while the original AutoSave does not. Its hash remains
`13cff3ae3445bd28a93fdf33cfe9d7b6a0b71e33ed0995b841f5935b1c86447f`.
Audit normal loading of that original checkpoint before repeating map travel.
These streaming records suggest the pre-assault sequence; they do not prove its
native entry state or a combat-ready load. No save fields were edited.

The existing load helper and in-mod command currently accept only save types
0 through 3 and reject LegendSave. The LE3 SDK declares `SaveGameType_Legend = 5`,
distinct from export type 4. Audit its native descriptor/name and loading route
before extending that allowlist; do not rename it to QuickSave as a shortcut.

Next: audit the original LegendSave's normal load route for the hostile mission
fixture. For a separate range fixture, verify Citadel availability from the
untouched original AutoSave and use native travel. Confirm range admission and
weapon identity before treating it as a fixture. Make a native save outside the
range, preserve a private copy before restoration (an overwritten QuickSave is
restored to its original bytes), decode it and verify a normal reload. Only then
prepare bounded resource variants with the existing independent oracle flow.

## Every ME3 save on the machine, by level (lane WP, 2026-11-16)

Looking for a save that loads on a mission with a weapon in hand (for the holsters' put-away, research/le2-vrik.md "ME3's
holsters, from its own script"), every `.pcsav` under the profile's `Save\ME3`, `%LOCALAPPDATA%\MEVR` (session backups and
retained native saves) and `local-tools` was hashed and the 19 distinct files decoded read-only (`tools/le3-save.py
inventory`, copies in a scratch folder; no save was written). There is still one career, John_12_Vanguard. Its six saves and
the navigation runs' retained ones (`cic-quick-save`, `galaxy-console-quick-save`, `pax-auto-save` and ten `native-saves`
AutoSave/LegendSave copies of 2026-11-15) are all `BioP_Nor` with a `SFXPawn_PlayerVanguardNonCombat` pawn and no weapon
records; the chapter save is `Biop_End001` (London, the injured Shepard's mode) and the NG+ save the ending. So there is
**no ME3 combat fixture on this machine yet**: the Citadel range route and the LegendSave audit above stay the next step, and
every ME3 weapon check (holsters, the put-away, only holsters draw, the reload by hand) waits for it.

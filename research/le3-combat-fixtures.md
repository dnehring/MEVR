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

## A combat fixture by console (lane AU2, 2026-11-18)

**Every check below that waits for "the first combat save" can run now, without a save.** From Save 31 on the Normandy the
console opens an N7 map and the game's own screens lead into an ordinary fight in about two minutes. Session
`%LOCALAPPDATA%\MEVR\sessions\20261118-090535-499-LE3-me3-fixture` (restore: 0 differences).

1. A renderer-preview stretch as usual: `prep.ps1 -Holder <you> -Game LE3 -RendererPreview`, `launch.ps1 ...`, `load.ps1 ...
   -Save John_12_Vanguard_021024_46e6e39\Save_0031.pcsav`.
2. `tools\le2-halcyon-run\me3-combat.ps1 -Holder <you>` does the rest by console, step by step:
   - `open BioP_SPDish` (the single-player copy of Firebase Dagger, an N7 mission; the other `BioP_SP*` maps are the same kind).
     After about 40 s: `mevr state` says world `Biop_SPDish`, pawn `SFXPawn_PlayerSoldier` (the map's own class, not the
     career's Vanguard), game mode 9: ME3's Squad Selection screen.
   - `mevr omni party`: the screen's own A on its confirmation, `SFXGUI_TeamSelect.FinalizeParty(true, 0)` (ME3's handler of the
     PartySelect panel; ME2's is `BioSFHandler_PartySelection`). Shepard alone (nobody was picked). Then the Weapon Loadout screen.
   - `mevr omni close`: B on the loadout keeps the map's loadout. The shuttle's intro plays on the 3D screen (game mode 7, then 8),
     about 20 s.
   - Game mode 0: Shepard on foot with an **Avenger assault rifle** drawn (magazine 30, spare 210) and a **Predator pistol**;
     Cerberus **troopers, a centurion, a gunner and its turret** within 1 to 17 m, firing at once. The script turns on `god`.
3. The map's start autosaves into the career's folder (`AutoSave.pcsav`, 36 KB); the stretch's restore puts the career back.
   A `QuickSave` there was kept privately (`%LOCALAPPDATA%\MEVR\AU2\fixtures`, never in the repository); loading it is not
   checked (the console route is shorter and edits nothing).

What the fixture showed at once (all fixed in the same landing, and seen after the fix in the same game):
- **ME3's BioPawn has no `IsHostile`**: the hand power wheel's lock, the squad context, VR cover's shooters and the squad's
  orders asked `SFXGame.BioPawn.IsHostile`, found nothing and saw no hostile at all (`mevr context` listed only cover; `mevr cover
  pawns`: "BioPawn.IsHostile, IsDead or Actor.Trace wasn't found"; the lock's scan returned before its loop). The question is
  `Engine.Pawn.IsHostile` on ME3 (game/Melee already used it): one name per game, `engine::adapter::kIsHostileFunction`. After:
  `mevr context` lists `E1 assault trooper x1 | behind near level`, `E2 turret x2`, `E5 centurion x1 ... hidden`; `mevr cover
  pawns` six hostiles, each exposed or behind cover.
- **The put-away works** (`[Holsters] le3PutAway = 1`, `holsters.ps1 -Game LE3`, `holsters-092405\lines.txt`): `let go at
  shoulderRight: the assault rifle goes back in its holster (SFXInventoryManager.SetCurrentWeapon(None) ...)`, `holstered after
  0.95 s`; four trigger pulls each `the game wanted to draw your weapon (Function SFXGame.SFXGameModeBase.FireWeapon): refused`;
  `grab at hipRight: drawing the pistol (BioPlayerController.SwitchWeapon; was holstered)`, `in hand after 0.06 s`; let go at the
  hip `holstered after 0.66 s`; let go in front of the chest, back after the 0.20 s grace. **`le3PutAway = 1` is ME3's default
  since** (the owner's answer of 2026-11-18); an old file's `0` moves to 1 once (marker `le3PutAwayMoved`, seen in the game:
  `hands: [Holsters] le3PutAway 0 -> 1`). The menu's Holster button on ME3 stays greyed until the owner has seen it.
- **A roll** (a replayed `forward+a`, `make-walk-recording.py --plan stand:1.5,forward+a:0.2,forward:0.6,stand:4`): ME3 plays
  `SFXCustomAction_PlayerEvadeForward` and the camera goes `SFXCameraMode_Interpolate` to `SFXCameraMode_Roll` for 2 s. The view
  stayed first person and level (camera pitch and roll 0 throughout, `mevr camera trace`; with `slomo 0.15` a snapshot mid-roll
  shows the level horizon with Shepard's rolling back and legs below the eyes), but: the traversal module reported the evade as a
  traversal move, so the floor lock's MoveHold followed the game's camera down with the body (the eye ended 49 units lower and
  stepped up to 16 units a frame); view level, the snap turn and the camera shakes stood down (Roll isn't one of ME1's five camera
  names). Fixed by the owner's choice for rolls (docs/trilogy-plan.md "Answered 2026-11-18"): an evade is no floor move
  (`traversal::FloorMoving`), and ME3's action cameras (`vr::IsActionCamera`: Roll, Melee, HitReaction, EnterCover) count as
  the body's (`engine::adapter::kActionCameras`, ME3 only). After: `floor lock: raw camera Z -181.2 ... final eye 1.7526 m`, then
  `-190.8 ... final eye 1.7526 m` through the roll (the eye at the player's height, the correction taking the camera's dip), no
  `traversal move began`, and view level holding through it. Not seen yet: a melee's, a hit reaction's or a cover entry's camera.

### The omni-blade with the gun put away, and the body hidden in a roll (lane AU8, 2026-11-18)

The owner's answers of 2026-11-18 (docs/trilogy-plan.md "Answered 2026-11-18, later"). Session
`%LOCALAPPDATA%\MEVR\sessions\20261118-155608-952-LE3-au8-me3` (restore: 0 differences).
- **The class's heavy melee plays with no weapon in hand.** `TryHeavyMelee` returns at once without one (its first test is
  `Pawn.Weapon`); with one it starts the pawn's custom action 58 through `BioPlayerController.StartCustomActionWithSyncPartner`
  (out of cover, not storming, not aiming back). `game/Melee`'s ME3 adapter now starts action 58 itself for the right fist with
  the weapon away (`DispatchFist`; the admission `Refusal` lets the right hand through with no weapon, every other check kept;
  Mod.cpp's hand availability counts the fist, grip and trigger held with nothing else, as released controls on ME3,
  `melee::FistAway`). Seen: after `mevr weapon away` (the put-down, `SetCurrentWeapon(None)`), `mevr melee punch` ->
  `StartCustomActionWithSyncPartner(58)` returned true and the observed action was `SFXCustomAction_SoldierPunch` (the
  Soldier's own heavy melee, the heavy family); the omni-blade drawn on Shepard's forearm (snapshot `16-07-19-890`); with a
  trooper 0.6 m ahead (14 degrees) its `MeleeImpact` gave `SFXPawn_Trooper_0` `TakeDamage` through `ImpactWithPower` and the
  trooper was gone from `mevr cover pawns` after. A second punch at once was refused by the game's own admission while the
  first action ran ("movement ignored"); the shared cooldown (`[Throwables] meleeCooldownS`) is Strike's for every route.
  **The camera was the game's third-person melee camera during it** (`SFXCameraMode_Melee`: the snapshot shows Shepard's back):
  the renderer's first-person states cover Explore, Combat and TightAim only. Not this lane's to change; the action cameras'
  first person (the owner's answer for rolls and melees) is open. The console's diagnostics need the game window in front
  (`ActionForeground`); a replayed fist stroke reached nothing on the fake headset (no `melee:` line; why is open).
- **The body hidden in a roll** (`[Hands] rollHidesBody`, 1 by default): the renderer's first-person hides take
  `MevrSeams::HideBodyForAction` (`vr::HidesBodyForAction`: `SFXCameraMode_Roll` or a blend into or out of it): the body and
  head, and the gun in hand (Pawn.Weapon's Mesh, owner-no-see), which a first snapshot mid-roll showed floating 2 m ahead with
  the body gone. Seen with AU2's replayed forward A under `slomo 0.15`: `roll: Shepard's body hidden from the eyes
  (SFXCameraMode_Interpolate, from SFXCameraMode_Combat to SFXCameraMode_Roll)`, then `shown again (SFXCameraMode_Combat)` when
  the camera came back; mid-roll the view level with no body and no rifle; the holstered pistol still shows (a small piece about
  2 m ahead, on the rolling hip: not Pawn.Weapon).

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

### First on the combat save: VR cover and no cover snapping (lane FP3, 2026-11-17)

**VR cover on ME3: covered = no damage, exposed = full damage, as ME2's `mevr cover hit` check.** Since lane FP3 ME3 refuses
the game's cover snapping by default (`[Hands] noCover = 1`, ME1's and ME2's key and default; the owner's rule 7), and with it
VR cover's damage rule is on by default too (`game/Le3Cover.inc`: it answers `IsInCover` only while the snapping is refused).
Neither was seen in a fight: Save 31 is the Normandy, with no hostile and (the run's `mevr cover links`) no reachable cover
(research/trilogy-first-person.md). ME3 has no synthetic hit (`mevr cover hit` says so): the check takes real enemy fire. In
the same renderer-preview stretch, before anything else, with a replay that holds tracked hands (`make-walk-recording.py
--hands view --plan stand:600`, `replay.ps1 -Inputs`; since lane FX3 the refusal needs only VR on foot, as ME1's and ME2's, but
the A presses and the crouch below are the replay's):

1. `mevr cover links 6`: an authored cover within a few metres; walk there (`set` the pawn's place or a stick plan).
2. Next to it: `mevr cover acquire wide` must answer `TryAcquireCover(wide, test only) returned FALSE (FindCover refused 1
   times ...)` and the log `posture: refused a cover entry: SFXGameModeDefault.FindCover found cover for TryAcquireCover ...
   ([Hands] noCover: no snapping in VR)`; a replayed A there (a plan's `a` segment) leaves Shepard where he stands (`mevr cover
   where`: `in cover no`). `mevr cover snap on` then the same A: Shepard snaps (the A/B); `mevr cover snap auto` back.
3. Crouched behind that cover (a plan's `crouch` segment) with the enemy beyond it: each hit logs `vr cover: LE3 answered
   IsInCover TRUE for this hit through <CoverLink> slot <n>: the game's own cover rules decide the damage` and the shields and
   health don't move (the HUD, or `vr cover:` heartbeat's counts); standing in the open: `vr cover: a hit from ... in the open`
   lines, no `answered` line, and the damage the game deals anyway. A covered hit with `no authored cover slot within reachM
   qualified` is the game's full damage: note where it stood.
4. Any fault, a `call function:` warning, damage taken while the line says TRUE, or no damage in the open: make the rule
   opt-in again (Le3AnswerCover's gate) and report to the coordinator before anything else.

### The cover checks lane FX3 added (2026-11-17), in the same stretch

Lane FX3 made the refusal one rule for the three games (`ik::posture::RefuseCoverSnap`: VR on foot with `[Hands] noCover`,
decided each tick and kept until the next; research/trilogy-first-person.md section 2), hid ME3's "take cover" prompt as ME2's,
and pinned ME3's renderer to first person in cover. None of them could be seen on Save 31 (no cover on the Normandy). At the
cover found in step 1 above:

5. **No snap while anything else is up.** With the hand power wheel open (X held: `mevr powerwheel open`), then with the squad
   wheel open (the left trigger held in the replay, or `mevr place aim 0` for the arc), then at the end of a roll (a replayed
   B or the game's evade) and a cast (`mevr powerwheel push`): each time `mevr cover acquire wide` returns FALSE with `FindCover
   refused 1 times`, and a replayed A next to the cover leaves `mevr cover where` at `in cover no`. Before FX3 each of these
   let the snap through (the refusal also asked for the host's controls, no menu or wheel and no custom action).
6. **The prompt.** Standing next to the cover with the snapping refused, the HUD panel (`-hud.png` of a snapshot) shows no
   take-cover prompt, the log has `posture: the HUD's cover prompt (the game's ShowCoverWidget) is hidden again with its own
   HideCoverWidget while the snapping is refused (SFXGUIInteraction ...)` once and the heartbeat `posture: this period the
   HUD's cover prompt shown N times, hidden again N times`. With `mevr cover snap on` the prompt comes back (the A/B). If the
   heartbeat counts no prompt at all next to cover, ME3 shows it from script (a CallFunction the ProcessEvent handler doesn't
   see): report it.
7. **First person in cover.** `mevr cover snap on`, A next to the cover (Shepard snaps): the eyes stay first person (a
   snapshot; MELE3VR_Log has `[MEVR-BASELINE] ... first person in cover (CoverThirdPerson)` only if the ini asked for third
   person, and no `[COVEROFFSET] ... released to game (cover)` line). `mevr cover snap auto` afterwards.

### The weapon checks to run on the first combat save (lane WP's list)

The owner makes an ME3 save in combat (2026-11-17). With it, in one renderer-preview stretch (`<career>` and `<file>` the
new save's; `$h` your holder):

```powershell
$t = 'tools\le2-halcyon-run'
& "$t\prep.ps1" -Holder $h -Game LE3 -RendererPreview -Tag me3-weapons
& "$t\launch.ps1" -Holder $h -Game LE3 -RendererPreview
& "$t\load.ps1" -Holder $h -Game LE3 -RendererPreview -Save "<career>\<file>.pcsav"
# 1. holsters with the default (le3PutAway = 0): a grab at a slot switches weapons; letting go leaves the gun in the hand
& "$t\holsters.ps1" -Holder $h -Game LE3
# 2. the reload by hand: one shot, the gesture, a full magazine refused, the held-grip shot
& "$t\reload.ps1" -Holder $h -Game LE3
& "$t\launch.ps1" -Holder $h -Game LE3 -RendererPreview -Stop
& "$t\restore.ps1" -Holder $h -Game LE3 -RendererPreview
```

What each must show (`holsters-<time>\lines.txt`, `reload-<time>\lines.txt` and `console.txt` in the stretch's folder):
1. `holsters: the player carries:` lists the save's weapons with their families; `grab at shoulderRight: drawing the assault rifle
   (BioPlayerController.SwitchWeapon; was ...)` and `the assault rifle is in hand after N s` (if the save carries one; else the
   pistol at `hipRight`); letting go there does nothing (the session's one line `holsters: a gun let go stays in the hand: ME3
   keeps a gun out on a mission ...`), never a `TryHolster` line; with no weapon away the four trigger pulls fire (no `refused`).
2. `reload.ps1`: `mevr weapon pistol` draws it (or use the family the save carries: edit the script's `mevr weapon` line),
   `mevr reload status` before (`magazine M, used 0, spare S`), after the shot (`M-1, 1, S`), after the gesture
   (`reload: reloaded after N s: magazine M, used 0, spare S-1`, or ME3's own numbers if its pistol counts differently); the full
   magazine: `a squeeze at the pouch: no clip`; the held-grip shot fires (`used 1`).
3. **The put-away** (`le3PutAway = 1`): the preview's ini is protected during a stretch, so this needs its own stretch whose
   `ASI\LE3MEVR.ini` carries `[Holsters] le3PutAway = 1` from prep (or a headset session). Then `holsters.ps1 -Game LE3` must show
   `let go at shoulderRight: the assault rifle goes back in its holster (SFXInventoryManager.SetCurrentWeapon(None): ME3's own
   put-down, ...)`, `the assault rifle is holstered after N s` (Pawn.Weapon None), the four trigger pulls each `holsters: the game
   wanted to draw your weapon (Function SFXGame.SFXGameModeBase.FireWeapon): refused`, the gun still away after them (`mevr reload
   status`: `no gun`), the hip grab drawing the pistol again (`drawing the pistol (BioPlayerController.SwitchWeapon; was holstered)`,
   `in hand after N s`), and no stuck state: a power cast, a melee, cover and a reload with the gun away and after, the HUD's weapon
   panel back. Only then make `le3PutAway = 1` ME3's default (ME1's behaviour) and note it in docs/trilogy-plan.md T25.
4. **The heavy weapon kept** (lane CTL, 2026-11-17; the owner's answer to docs/trilogy-plan.md question 13; unit-tested in
   `tests/BodySlotsSmgTests.cpp`, never seen in the game: the Normandy's pawn carries nothing). Needs a heavy weapon in hand:
   one the mission offers as a pickup (use it), or a save made holding one. Then:
   - the log says once `holsters: a heavy weapon is in hand (<its class>): a grab at a slot is refused while it is ...`, and
     a snapshot's `"mod"."holsters"` has `"switchLosesHeld": true`;
   - `make-holster-recording.py --slot shoulder` replayed with `-Inputs` (the assault rifle's slot, closest to where the heavy
     weapon is held), then `--slot hip`: each `holsters: grab at shoulderRight: the assault rifle stays in its holster: a heavy
     weapon is in hand and a switch would throw it away ...` (the refusal buzz), no `drawing the ...` line, no
     `BioPlayerController.SwitchWeapon` in `mevr calltrace 12 switchweapon putdownweapon tossweapon` and the heavy weapon still
     `Pawn.Weapon` after (`mevr reload status` names it, or the HUD's weapon panel shows it);
   - `mevr weapon pistol` answers `the pistol wasn't drawn: a heavy weapon is in hand ...`;
   - the game's own weapon wheel (left trigger, `[Squad] style = native`, or `mevr calltrace` around a pick) still switches
     away (ME3 then throws the heavy weapon itself, as on a pad), after which `the heavy weapon is out of the hand: grabs at
     the slots draw again` and a hip grab draws the pistol.
   A heavy weapon drawn by a grab, or a `SFXHeavyWeapon.PutDownWeapon` in the trace after a grab: the rule failed; report it.

### The powers and the squad on the first combat save (lane SQ, 2026-11-17)

ME1's power wheel, the lock and push, the self-cast gestures, the squad's placement by an arc and the flat squad order
run on ME3 through the same scripts as on ME2 (`tools/le2-halcyon-run/powers.ps1`, `place.ps1`, recordings from
`tools/make-power-recording.py`; research/55 "The whole wheel on Save 4", research/54 "ME2 placement and orders on Save 4"). In the same
renderer-preview stretch as the weapon checks above (after them, or in a stretch of its own):

```powershell
$t = 'tools\le2-halcyon-run'
# (prep, launch, load as above)
& "$t\powers.ps1" -Holder $h -Game LE3                       # the wheel, every tile, the tap, the coach, every self-cast, every push
& "$t\place.ps1" -Holder $h -Game LE3                        # the squad wheel, the arc to the ring, one squadmate, double B, the left grip
tools\console.ps1 "mevr squad" "mevr context" "mevr powerwheel roster"
```

What each must show (`powers-<time>\verdicts.txt` and `lines.txt`, `place-<time>\verdicts.txt` and `lines.txt`):
1. `roster on the wheel (N)`: the save's class powers that ME3's HUD shows, `No power` last; `PASS the wheel opens on X`.
2. `PASS tile i (<power>) by the hand|stick` for every tile; the coach's snapshot (`snapshots.txt`, `coach`) shows the card
   where the wheel was.
3. Self-casts (ME3's buffs on the chest, ammo powers on the drawn gun, Unity raised above the head: `SelfGestureFor`):
   `PASS <power> by the open hand ...`, the log's `gesture attempt: ... cast` and then `gesture target hidden: cooling down`.
4. Every targeted power: `aiming at <pawn>, N m`, then `PASS <power> by the lock and the push` with the log's `<power> locked
   on <pawn>` and `push: <power> at <pawn> ...: native request accepted`. With no hostile in range the push is refused by
   name (`needs a locked enemy`). `-Drone` is Freedom's Progress's fixture only: on ME3 stand in sight of a hostile.
5. `place.ps1`: `PASS B held, the squad picked up, the arc let go: both go to the ring` (each `fromPointM` under 1.5; ME1
   measured 0.6 m), `PASS the hand moved left: the first squadmate alone`, `PASS B double-tapped`; the log's `squad place:
   <name>'s AI after the order: ...` says what each AI did with the order (state, queued orders, hold point). The move
   marker (`UpdateMoveToIndicator`, placed before every hold order) is ME3's too; a `the game's move marker ... wasn't placed`
   line is a failure to report.

### The crosshair hidden on the first combat save (lane CH, 2026-11-17)

ME3's crosshair is hidden under the hand reticle since lane CH (research/le3-hud-panel.md "The crosshair hidden"), but the
Normandy has no weapon, so nobody has seen it go. In the same renderer-preview stretch as the weapon checks (`mevr weapon` draws
one), with a gun in hand on foot:

1. `tools\console.ps1 "mevr reticles"` must print `gameMode=0 gameplay=true VR suppression request=true`, a line `movie=...
   SFXSFHandler_Reticle ...` with `_root._alpha=0 savedAlpha=<the game's, 100 or so>`, `reticle=SFXGUI_CrosshairReticle ...`
   (the shotgun's or the heavy weapon's for those), `suppress=true scope=false`. The log has `[MEVR-CROSSHAIR] movie=...` with
   the same values.
2. `tools\snapshot.ps1 -Tag ch-crosshair`: the `-hud.png` layer has the HUD's weapon and health clips and **no crosshair at its
   centre**; `-left.png` shows our ring at the hand's aim and nothing at the panel's centre. A crosshair still drawn there: the
   game draws it from another movie; write down `mevr reticles`'s line and the snapshot, and report it.
3. The A/B, the crosshair back: `tools\console.ps1 "mevr omni computer"` (the Mission Computer, GUI mode 9: the request ends)
   then `"mevr reticles"`: `gameplay=false ... suppress=false`, `_root._alpha=<the saved value>`, and the log's
   `[MEVR-CROSSHAIR] restore ... (no longer asked) ok=true`; `"mevr omni close"`, and the alpha is 0 again.
4. The scope view: with a sniper rifle (`mevr weapon sniper`, if the save carries one) zoomed with the game's own zoom (LT on a
   pad, if anything reaches it), `mevr reticles` says `zoomed=1 zoomReticle=SFXGUI_StandardSniperZoomReticle ... scope=true
   suppress=false` and the alpha is the game's: the scope's picture shows on the panel as the game draws it. With no way to zoom
   in VR, write that down instead.
5. Anything else (a `restore` that never comes, `_root._alpha=?`, a fault in `GetVariableFloat`): set `screenReticleHide` back to
   false in `plugin/RendererLe3.cpp` and report to the coordinator.

### Melee on the first combat save (lane MEL, 2026-11-17)

ME3's melee moved onto the trilogy's one path (research/20 "The trilogy's one path", `game/Melee`): its native admission,
`TryMelee`/`TryHeavyMelee` and the action's observation are ME3's adapter under ME1's dispatcher, its staged gesture rules
(neutral priming, the heavy chamber) are `ik::melee::Gestures`' `Staged` rules. It stays opt-in (the owner's question 7), and B is
kept from the game on foot on ME3 as on ME1 and ME2 (the owner, 2026-11-17), so **ME3 has no melee on foot by default** until the
owner says yes. New for ME3 with the move: ME1's enemy in reach (`[Throwables] meleeNeedsEnemy`, `meleeReachM` 2.0) and the shared
cooldown (`meleeCooldownS`); no punch with the weapon away (`ME3 has no melee with the weapon away yet`). Save 31 (the Normandy)
can show only the refusals and B's state. On the combat save, a gun out, a hostile within 2 m:

```powershell
$t = 'tools\le2-halcyon-run'
# (prep, launch, load as above)
& "$t\melee-b.ps1" -Holder $h -Game LE3        # B double-tapped and held: no TryMelee; the canary (meleeButton = 1): TryMelee
tools\console.ps1 "mevr melee"                  # requests false; gestures false; ... B kept from the game (squad placement's)
tools\console.ps1 "mevr melee requests on" "mevr calltrace 20 melee"
tools\console.ps1 "mevr melee light"            # TryMelee (light request dispatched ...), then the log's `melee: observed requested family action ...`
tools\console.ps1 "mevr melee heavy"            # TryHeavyMelee; the observed action of SFXCustomAction_PlayerHeavyMeleeBase's family
tools\console.ps1 "mevr melee punch"            # the right hand's light request (ME3 has no fist route): as `light`
tools\console.ps1 "mevr melee gestures on"      # then a replay: python tools\make-power-recording.py x.mevrrec 'rgrip:0.2:2.8' 'lpunch:1.2' 'lrest:1.7:2.3'
tools\console.ps1 "mevr melee requests off" "mevr melee gestures off"
```

What each must show:
1. `melee-b.ps1`: `PASS B double-tapped and held on foot: no TryMelee` and `PASS the canary ...: TryMelee in the trace` (the
   canary proves the trace sees ME3's B binding; Save 31 already shows both).
2. `light` with nobody within 2 m: `refused: no enemy within 2.0 m ...; the nearest hostile is ...`; within reach:
   `TryMelee (...)` and an observed action of the requested family within 0.35 s; `heavy` the same with the heavy family. A
   `different family` line, `dispatched; no action start observed`, or a refusal by ME3's admission (`native action or
   unresolved admission pending`, `reload or zoom blocks melee`, `weapon not ready`) names what to look at.
3. The replayed open left hand stroke with the gun in the right hand: one `melee: stroke N hand 0: ...` line (staged rules: the
   hand must have been neutral first, the replay's rest is). With the right grip squeezed (the gun held) the right hand never
   strikes on ME3 (its staged availability wants every control released): a difference from ME1 to put to the owner with
   question 7.

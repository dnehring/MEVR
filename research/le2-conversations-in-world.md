<!-- SPDX-License-Identifier: MIT -->
# ME2 conversations in the world, first person: ME1's rules on the sequels' module (2026-11-19)

**Why.** The owner, 2026-11-19: "We need to bring the first person dialogue option into me2 and me3 ... One of the big
changes from me1 is that there are a lot of dynamic scenes where we have walking movement in the dialogue. Like meeting
tali in me2 for the first time. Or the miranda scene where we meet her to discuss her plans. I think the rules we have in
me1 are possibly sufficient about these. We just need to make sure we stay on the same theme of a unified system, applied
to all 3 games." Then: "I know they were deferred and I'm changing priorities to make them a part of release." This
reverses docs/trilogy-plan.md rule 9's deferral (T49, ledger G36). The default stays ME1's: the 3D cinema screen;
first person is the experimental option `[Cinematics] conversations = world` (Comfort > Conversations > First person).

**What this note is.** The implementation guide for ME2 on the one module the sequels share,
`src/game/SequelConversationWorld.{hpp,cpp}` (another session's ME3 core slice, bff358d3 and its follow-ups, which the
owner called "not complete ... enough to view some scenes in VR"), and the record of what ME2's own data and code say. ME1's
`game/ConversationWorld.cpp` (8,000 lines) is not ported a second time: a rule the sequels need is called from the shared
core `core/vr/ConversationWorld` (cw::...), and a rule that exists only in ME1's file is moved there with a unit test when a
sequel needs it. Read [research/59](59-conversations-in-world.md) and [research/69](69-conversations-everywhere.md) first.

## 1. How ME2 moves people in a conversation (read from the packages)

Tools: `tools/conversation-scan` (C#) with `--game LE2`, which since this lane also records ME2's own start action, each
line matinee group's walking gestures and each move track's first/last key and path; outputs are game data and stay in
`scratch/` (gitignored).

```powershell
dotnet build tools/conversation-scan/ConversationScan.csproj -c Release -o <private-build> -p:LexDir=<LegendaryExplorer> -p:BaseIntermediateOutputPath=<private-obj>
<private-build>/conversation-scan.exe "<ME2>\BioGame\CookedPCConsole" scratch\fpc\le2-scan.jsonl --game LE2
```

1857 packages, 62,966 records, 913 conversations (1,038 with the DLC copies), 1,041 starts. Findings:

1. **ME2 starts most staged conversations with its own action, `SFXSeqAct_StartConversation`** (560 of 1,041 starts;
   `BioSeqAct_StartConversation` 481). It has ME1's links (Conv, Owner, Stage) plus Node1..NodeN actor variables. The scan
   missed every one of them until this lane (Tali's meeting had "no start").
2. **People walk inside a line by gesture animations with root motion**, not by stage marks: `BioEvtSysTrackGesture`
   entries such as `WalktoNode_Forward`, `WalktoNode_90DegreeTurnRight`, `Wander_LongWalkEnter`, `Wander_LongWalkForward`,
   `DL_EnterWalk5p_Turn90R`, `DL_ExitTurn_90L_Walk5p`, `DL_WanderSouth/North/East/West`, `Wander_Pacing` (the scan's
   `walkGestures` per group: the actor's `m_nmFindActor` and the animations). 355 conversations have someone else walk by a
   gesture, **283 have the player (Shepard) walk by one**. ME1 has no such tracks (its conversations move people with the
   stage's marks, `SetPositionRotator`). Whether a given "Wander" gesture moves the pawn or only shifts weight is [verify]
   per animation (root motion is in the animation, not in the package's track data).
3. **Move tracks on actors exist too** (7,753 `InterpTrackMove` in 24,334 line matinees, most of them the cameras' booms:
   groups `Cam1`, `cam01`, `Camera` ...): actor groups (`joker_sit`, `edi`, `player`, `owner`, `wrex_sitting` ...) in about 3%
   of lines. `BioEvtSysTrackSwitchCamera` (the stage's cameras, ME1's way) is on 13,836 lines; a director track
   (`InterpGroupDirector`) on 4,885.
4. **The walking scenes the owner names start with a level matinee, not a line**: Tali's meeting at Freedom's Progress is
   `BioD_ProFre_201Tali` sequence `Tali_Conversation`: Shepard opens `SFXDoor_5` → a plot check ("Find Tali?") →
   `BioSeqAct_BioToggleCinematicMode` → **`ANIMCUTSCENE_ProFre_200_TaliReIntro`** (13.2 s; a director, two camera booms, and
   imported animation with a move track on every actor: `Tali_`, `PC_`/`HMF_PC_` (Shepard, male and female), `Miranda_`,
   `Jacob_`, `Leader_` (Prazza), two quarians) → cinematic mode off → two plot transitions → three `SeqAct_Teleport`s
   ("Teleport hench1", "hench2", "Teleport player to side", to `BioPathPoint_0..2`) → `SFXSeqAct_StartConversation_0`
   (`profre_tali_intro_d_dlg`, with a Stage and five nodes: the player, the owner, `hench_vixen`, `hench_leading` ...).
   The conversation itself (53 entries, 56 replies, 67 line matinees) has gesture walks on 8 lines (Prazza and Tali) and no
   actor move track. 214 level matinees in ME2 move Shepard this way (ME1: 21); 42 are EndGm1, 35 EndGm2, 18 the Normandy's
   cabins, 9 ProNor, 5 ProFre.
5. **The Miranda scene** "where we meet her to discuss her plans" is, by the data, one of two (the owner to confirm,
   question 1 of the lane's report): `procer_vixen_intro_d` (Lazarus station's shuttle bay, the first time Shepard meets
   Miranda in person: gesture walks on 8 of 50 lines, Shepard on 4, Miranda on 4, Wilson, Jacob; a black screen on one) or
   `norvx_starter_h` (Miranda's first conversation in her Normandy office, her role and the mission: Shepard walks by
   gesture on 6 of 55 lines, `WalktoNode_Forward`, `Wander_LongWalkEnter`; a stage). The owner's VR career (`VR_31`) is at
   Freedom's Progress, after Lazarus: neither is reachable from its saves without a new game or a later save; the older
   career (`John_12`) is on the Normandy (`Biop_Nor`), [verify] whether `norvx_starter_h` is still unplayed there.

**What maps to which of ME1's rules** (research/69):

| ME2 movement | ME1's rule | On the sequels' module |
|---|---|---|
| a level matinee that walks Shepard before the conversation (Tali's re-intro) | a moment (`worldMoments = view`: the script's Shepard walks hidden, the view stays the player's and joins him at rest) | **not yet**: plays on the 3D screen (cinematic mode 6 is a cutscene); question 2 |
| the teleports before the start ("Teleport player to side") | the game's fade to black always moves the player (`worldBlackMoves`); a teleport within the same space walked or carried | the opening rule (`cw::DecideOpening`): a mark within a walk is where the player stood, Shepard pinned there |
| a stage's placements at the start and per line | withheld at the source (`SetPositionRotator` pre-hook) and undone after the fact (`worldStay`) | undone after the fact (0.3-6 m in one camera update); the source hook's ME2 address is known (section 2), not hooked yet |
| Shepard's gesture walks in a line (283 conversations) | "the player is never moved by the game" | Shepard pinned on the player's place every camera update (`worldShepardStays`) |
| NPC gesture walks (Prazza, Tali, Miranda) | "the game's own authored animation must be kept" | kept: continuous moves under 30 units an update are the person's own |
| `SFXInterpTrackSetPlayerNearClipPlane` (127 lines), the director's per-shot near plane | `worldNearPlane` | **ported in this lane**: the on-foot plane written back after each camera update |
| the conversation camera's letterbox | `[NOLETTERBOX]` | cleared after each camera update (the ME3 slice) |
| depth of field | off while held, never around a load | `depth_of_field::Hold` (the ME3 slice's 3 s rule) |

## 2. ME2's natives for ME1's three hooks (Ghidra, LE2 project)

`tools/ghidra/decompile.ps1 -Game LE2 <rva>`; vtables from the spliced exe's `.rdata` (a local script over
`%LOCALAPPDATA%\MEVR\ghidra\bin\LE2-60AEEFA5\MassEffect2.exe`: a class's vtable starts where `ProcessEvent` (ME2 `0xD8D50`)
sits at +0x240). Numbers only; no code here.

| ME1 (LE1 RVA) | ME2 | How found | State |
|---|---|---|---|
| `UBioConversation::SetPositionRotator` `0xC862D0`, vtable slot 0x2C0 | the virtual at slot **0x2B8** of `UBioConversation` (`execSetPositionRotator` `0xB4A320` calls `*this + 0x2B8`) | the exec thunk from the natives dump (`natives-LE2-60AEEFA5.csv`) | slot known; the class vtable's address not pinned (the shape search over 1,610 vtables found candidates, none matching LE1's layout one slot earlier: [verify] by reading the class default object's vtable in the game, `mevr` console) |
| `ABioWorldInfo::InterruptConversation` `0xB60460`, slot 0x9F0 | the virtual at slot **0x8D0** of `ABioWorldInfo` (`execInterruptConversation` `0xB44BC0`) | the exec thunk | slot known; address [verify] the same way |
| `AActor::MoveWithInterpMoveTrack` `0x4B75F0`, Actor vtable +0x558 | **`0x6B9C50`**, Actor vtable +0x590 (LE1's first 32 bytes match once; already named `_guess` in the LE2 project) | byte pattern + 20 vtable slots | ready for a hook; the move track's `GetLocationAtTime` is slot **0x388** there (LE1 0x390), read from the same function |

`engine/ConversationHooks` installs nothing on ME2 yet (its LE2 RVAs are 0): the source hook is the next step of section 4
item 3, after the after-the-fact undo has been seen in the game.

## 3. What was built (lane FPC)

- `game/SequelConversationWorld` on ME2 (`MEVR_GAME_LE2`): its reader of the live conversation is ME1's
  (`SFXGame.BioWorldInfo.m_oCurrentConversation`, a `BioConversation`, in the conversation mode 5), the people are ME1's
  (`m_SpeakerList[].aSpeaker`, the owner, the speaker and listener now), the player `m_pConversationPlayer`; everything after
  the reader is ME3's code path. New on both sequels: ME1's near-plane rule. ReflectCheck rows in `CheckThespian`.
- Host: LE2 serves HostServices v33 `setConversationWorld` (ModLoader); `plugin/WorldUi`'s conversation hold is the
  sequels' already; the sticks are ME1's (the owner's answer: the left stick walks only with the wheel down, `cw::StickWalks`; the right stick turns about the drawn eyes, `[Comfort] turn`; the ME3 slice's right-stick wheel in `plugin/PadMerge` removed; it had been the owner's ME3
  request kept on ME2).
- ME2's renderer (`src/halcyon/le2`, `[MEVR]`): `me2_xr.cpp`'s cinematic decision treats a held conversation like the
  galaxy map's held cinematic (no flat panel, no 3D screen, no VR cine; `mevrConvoWorld`), and publishes it as a world view
  (arm IK, the body); `calcview_hook.cpp` holds the main view at the player's place in modes 5 and 6 while the mod holds a
  conversation (`MevrSeams::WorldViewHoldBegin`); `engine_probe.cpp` keeps first person's hides then (`WorldViewHeld` 1).
- Our menu: Comfort > Conversations offers First person on ME2 and ME3 (it wrote nothing there before); `world` is the
  key's value, as ME1's.

### 3.1 In the game (fake headset, 2026-11-19 23:36)

Stretch `20261119-233023-121-LE2-fpc` (`prep.ps1 -Holder FPC`, `launch.ps1`, `load.ps1 -Save
John_12_Soldier_190924_4ee650d\Save_0036.pcsav`: the older career on the Normandy's crew deck), then
`conversation-world.ps1 -Start fire:SFXGame.SFXSeqAct_StartConversation:Dialog_Samara.SFXSeqAct_StartConversation_0
-Walk 0.8` (the folder `convo-samara-*`: steer.txt, console.txt, lines.txt; snapshots `2026-11-19_23-36-*`, `23-37-*`):

1. Samara's starter (`normw_starter_h`) entered mode 5 and was held in the world: `[VRSTATE] gm 5 live alive -> 3D screen
   (MEVR)` for 0.43 s, then `-> VR` (the opening's settle; nothing publishes "a conversation is coming": FPD3's seam
   `ConversationWorldOrComing` is the cure), `[MEVR-HELDVIEW] ... first person's hides kept`, the letterbox cleared, depth of
   field off, the wheel's panel fixed in the room.
2. The director's near plane was 45 and 233 units in two shots; the on-foot plane (10) was written back about 900 times.
3. The left stick walked the player 0.98 m (`walked 98 units`); no speaker was moved by the stage (`0 undone`).
4. Switching the mode off mid-scene (`mevr convo world auto` with the file's 3D screen) put the scene on the 3D screen at once
   (`-> 3D screen (MEVR)`) and the held view ended: the option-off path is today's.
5. **Wrong:** the start was fired from the corridor, 10.4 m from Samara's mark behind a wall. The slice's opening sets
   `sightToMark = true` always, so it decided a walk-in and held the eyes where the player stood, facing a wall (the snapshot
   `2026-11-19_23-37-02-741-left.png`). ME1 asks the line of sight (`InSight`, three lines): this would have been a cut.
   Next: the opening's facts from `Engine.Actor.FastTrace`, as ME1's.
6. Not seen: the body (the eyes faced a wall), the wheel's picture, Tali's meeting (the VR career's autosave is after it; Save
   4 at the spaceport is before it, a mission's walk and fight away), the Miranda scene.

### 3.2 Tali's reintroduction (fake headset, 2026-11-20 00:02-00:09)

The owner's walking-scene check ("I meant tali on freedoms progress"). Stretch `20261120-000003-113-LE2-fpc-tali`, how it was
reached (script only, no save written): `load.ps1 -Save VR_31_Soldier_141126_336072f\Save_0004.pcsav` (the spaceport, same
mission), `god`, `mevr instances SFXGame.SFXPawn_PlayerSoldier` and `mevr setloc 0 -9600 16600 -12060` (8.5 m north of
`SFXDoor_5`: the level streamed `BioD_ProFre_201Tali` in and Shepard landed on its floor), then
`conversation-world.ps1 -Start fire:SFXGame.BioSeqAct_CancelHint:Tali_Conversation` (the first action after the door's
"Opened" event: the level's own chain runs from there, the cutscene, the plot transitions, the three teleports, the start).

1. First run (`convo-tali-*`): the cutscene played in mode 6 on the 3D screen, then the conversation began (mode 5) and the
   module **left it on the 3D screen as if a load had opened it** (the slice's rule: "not 3 s on foot just before"; the cutscene
   lasted 13 s). Held by hand (`mevr convo world on`), the snapshot `2026-11-20_00-05-27-140-left.png` shows the scene in first
   person in stereo: Tali an arm's length in front, Prazza with his rifle, two quarians behind, the room around (the near plane
   kept: 852 writes).
2. Fixed (`gOnlyScenesSinceFoot`: only the cinematic and conversation modes since the last stretch on foot is no load) and run
   again (`convo-tali2-000752`): `[VRSTATE] gm 6 -> 3D screen (MEVR)` for the cutscene, then `gm 5 -> VR` at once (no 3D-screen
   frame at the conversation's start: the early hold), the opening a cut to Shepard's mark ("where he stood isn't known" after
   the cutscene), the letterbox cleared, the director's 100-unit near plane replaced.
3. Not yet: the cutscene itself in first person (the owner's answer 2: ME1's moment rule for ME2's 214 level cutscenes that walk
   Shepard: not built), the body and the wheel's picture in a snapshot (the eyes looked level at Tali), the circle, the looks.

## 4. What is left, in order (each item for ME2 and ME3 alike; say what a game lacks)

**The owner's answers (2026-11-19, research/le3-conversations-in-world.md section 9: all (a), every game) set the order's
defaults:** ME1's sticks (the left stick walks only with the wheel down and steers it when up; the right stick turns: undo the
slice's swap in `plugin/PadMerge` and gate the module's walk on the wheel, ME1's `cw::StickWalks`); ME1's moment rule for
Shepard walked by a line or a level matinee (`worldMoments = view`, walk-along, `worldBlackMoves`), his walk withheld at the
source (ME3: FPD3's hooks; ME2: `0x6B9C50` and the `SetPositionRotator` slot) instead of the pin, never a guided walk; scenes
with no place for the player on the 3D screen from a generated presence table.

1. **ME2 in the game** (fake headset; `tools/le2-halcyon-run`): an ordinary standing conversation, then Tali's meeting from
   the VR career's autosave, with the option on and once off. Judge by snapshots of the eyes (`tools/snapshot.ps1`), not log
   lines: the body drawn, the eyes at the player's height, the wheel on the room panel, a clean return to gameplay.
2. **Looks at the player's eyes** (ME1's `worldLookEyes`, `StepEyes`): ME2's look-at chain is ME1's (`BioPawn.m_oLookAtData`,
   `SetLookAtTarget`, all in ME2's SDK); ME3 lacks `BioLookAt` (its own `SFXLookAt`?: [verify] FPD3's note). A shared rule
   `cw::LookTargetFor(...)` would take ME1's choice out of `game/ConversationWorld.cpp`.
3. **People kept at the source**: hook ME2's `SetPositionRotator` (slot 0x2B8; pin the address first) with
   `engine/ConversationHooks`'s existing pre-hook and its `KeepProvider`; the after-the-fact undo then counts zero, as on ME1.
4. **Walking to marks with the game's AI**: ME2 lacks ME1's `BioAiController.PushMoveToPoint`,
   `DoesGameModeAllowMovement` and `BioSeqData_MoveToPoint` (`tools/le2-name-audit.py`); ME2's own AI move is the next read
   (Ghidra: what `SFXSeqAct_MoveToPoint`-like Kismet actions call). Until then a mark is not walked to (people stay put).
   **The shared walk helper** (asked 2026-11-19 by the coordinator for this lane and lane EXIT; not started on this branch):
   ME1's walk lives in `game/ConversationWorld.cpp` (`StartAiWalk`, `WalkToMark`, `EndWalk`/`ReleaseAi`, `CanWalk`,
   `HasPosture`, the answer handler `OnAllowMovement`), bound to its `Stay` records and globals (`gStay`, `gC`, `gTearingDown`).
   The interface to extract it to, `game/AiWalk.{hpp,cpp}` with a per-game adapter:
   - `std::optional<Walk> Start(ue3::Object* pawn, ue3::Vector to, std::string& why)`: ME1's checks (a live Pawn, a
     BioAiController, not hidden, `HasPosture`), then the four holds raised for this walker only (the AI enable counters to 1,
     the mode's movement ban answered true for its controller, `SetDesiredSpeed(1)`, `m_bInGestures` cleared) and
     `PushMoveToPoint(vLoc, 10, walking, precision)`; `why` names the refusal (then the caller keeps the person put).
   - `Step(Walk&, double dt) -> Walking | Arrived | Stalled`: arrival within the move's tolerance + 15 units, a stall when it
     moved under 8 units in 2 s (ME1's `kAiArriveUnits`, `kAiStallS`).
   - `End(Walk&, char const* why)`: the move terminated (`BioSeqData_MoveToPoint.Terminate`), every hold given back by as much
     as it was raised, the gesture mode back unless the conversation is being torn down; idempotent.
   - Adapters: ME1 the names above; ME2 lacks the four ME1 names (above); ME3 per research/le3-conversations-in-world.md 2.6.
   ME1 must keep its behaviour: `game/ConversationWorld.cpp` then calls the helper, and its regression set
   (`tools/conversation-audit.ps1 -Canaries`, one launch under the run lock) runs once after the move.
5. **The circle and the squad's formation** (`cw::MakeCircle`, `cw::SquadFormation`): shared rules already; the facts
   (who speaks within 4 lines: `m_EntryList`/`m_ReplyList` exist on ME2) from the reader.
6. **Subtitle and wheel placement** (ME1's subtitle under the speaker, the wheel at the hand): the sequels fix the
   conversation's whole movie on one room panel today (the owner's ME3 request: "not static ... follows my head").
7. **Moments**: Tali's re-intro (section 1 item 4) is ME1's moment (`worldMoments = view`); `AuthoredMoment`'s move-track
   hook on ME2 is `0x6B9C50`. Needs the owner's answer to question 2 before it becomes a default for 214 matinees.
8. **The rest of ME1's walking scenes**: walk-along (`worldWalkAlong`), the game's black moving the player
   (`worldBlackMoves`), rides (`RideBase`, `Actor.Base`), seats (`StepSeat`), eye height over his floor.

## 6. Lane FPC2 (2026-11-20): at the source, moments, the presence table, the floor marker

The brief's items 1, 2, 3 and 5, built on both sequels on the one module; checked in the game on ME2 (fake headset), not yet on
ME3 (section 6.7). Numbers only from the decompiler; no code quoted.

### 6.1 The placements at the source (item 1)

**ME2's natives, found** (the live objects' vtables read in the game: `mevr convo vtables`, a probe in the sequels' console that
prints a live object's vtable slot as an exe offset, whether .pdata calls it a function start, and its first bytes):

| What | ME2 RVA | How | Pattern (offset 0) |
|---|---|---|---|
| `UBioConversation::SetPositionRotator` (LE1 0xC862D0) | **0xA2D180** | the live conversation's vtable 0x1252488, slot 0x2B8 (the class's own vtable 0x1252840, stored by its InternalConstructor 0xA29410, holds `_purecall` there: an offline search through the class registration finds the wrong vtable); decompiled: (conversation, actor, FVector*, FRotator*), a pawn stopped, moved only for a place that is not zero, dropped to the floor, turned: LE1's shape | `48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 48 8D 6C 24 A9 48 81 EC 90 00 00 00` |
| `ABioWorldInfo::InterruptConversation` (LE1 0xB60460) | **0xB06390** | the live BioWorldInfo's vtable 0x1292CA8, slot 0x8D0 | `48 89 5C 24 08 57 48 83 EC 20 48 8B D9 48 8B FA 48 8B 89 4C` (LE1's first 16 bytes) |
| `AActor::MoveWithInterpMoveTrack` (LE1 0x4B75F0) | **0x6B9C50** | LE1's first 16 bytes, unique; the track's `GetLocationAtTime` at vtable +0x388 | `48 8B C4 55 53 56 57 41 56 41 57 48 8D A8 58 FE` |

ME3's (research/le3-conversations-in-world.md section 3) are pinned the same way: the controller's `SetPositionRotator` 0xA29DF0
(five arguments, a null controller from two interp tracks' per-key placements) and `MoveWithInterpMoveTrack` 0x6BAFF0 (+0x398).
ME3's interruption is the manager's four-argument `InterruptAmbientConversations`: not hooked (nothing flows on ME3 yet).

`engine/ConversationHooks` (one file, per-game typedefs): the placement hook no longer needs the interruption's address; the move
track's place slot is per game (`kTrackPlaceSlot`); `MoveTrackEnd` (new) asks a track where its walk ends (its position curve's
last key, `Engine.InterpTrackMove.PosTrack.Points[].InVal`, then `MoveTrackPlace` at that time).

`game/SequelConversationWorld` answers both questions (`KeepAtSource`, `OnMoveTrack`): after the opening the stage never places
Shepard nor the people kept (the facing still applied: the game's own zero-place path), a track never moves Shepard (ME1's
`trackHeld`), and every actor a track moves is noted so the after-the-fact undo leaves its authored walk alone (ME1's
`trackWalks`); `cw::Insists` from the core replaced the slice's copy. The pin and the undo stay as the fallback, counted apart in
the conversation's end line (`at the source: Shepard's placements N, people's N, Shepard's track moves N; ... put back N times
after the fact`). What only the fallback covers: ME2's gesture walks' root motion [verify which conversations still count there].

**Mod.cpp:** on ME2 the sequels' module owns the hooks and their providers; ME1's `ConversationWorld`, `ConversationFlow` and
`AuthoredMoment` no longer tick on ME2 (their handles are ME1's, `ue3` without `kEveryGame`; ME1's module published its armed
state after the sequels' every tick, and would have set the move track's provider to ME1's). ME1's `InWorld`, `BodyAway` and
`MomentViewOn` answer with the sequels' module on ME2 and ME3, so arm IK runs in a held conversation's frame and the body follows
the player (ArmIk.cpp's gate asked ME1's `InWorld`, false on the sequels until now).

### 6.2 Moments: the level cutscenes that walk Shepard (item 2)

ME1's rule (`worldMoments = view`), in the sequels' module (`TickMoment`): in the cinematic mode, once a matinee's move track walks
Shepard's pawn (the move-track hook notes it; `cw::StepProvisional`: wait for a track while the game's camera is still the
player's, a quarter of a second once a camera of the scene's own has the view, at most 6 s), the script's Shepard walks as
authored (nothing withheld: the level counts on where it puts him) and the view stays the player's where the player stood:
head-tracked, both eyes, ME1's sticks (the left walks inside 4 m of Shepard's place, the right turns about the eyes), his pawn
hidden while the view stands more than 0.6 m from it, the body the player's (`BodyAway`). At the end the view joins him (a dip from
black, `cw::State::cut`, when farther than 1.5 m: ME1's opening limit), and its place is the place on foot for the conversation
that follows (its opening judges from there). Left to the 3D screen by rule: no track of Shepard's (`mevr convo world`'s "without
a track of Shepard's"), his pawn hidden by the script (no place for the player), a cutscene not begun from the player's own view
(around a load, a movie, a screen). The host: the armed bit covers the cinematic mode (`MevrSeams::ConversationWorldOrComing`;
ME2's `me2_xr.cpp` mode 6 through it, `[MEVR]`), and the sequels' `TransitionFadeLayer` dips on the state's `cut` as LE1's does.

**Tali's reintroduction, in the game** (fake headset, 2026-11-20 01:16-01:28, stretch `20261120-011558-816-LE2-fpc2-tali`;
`load.ps1 -Save VR_31_Soldier_141126_336072f\Save_0004.pcsav`, `god`, `mevr setloc` 8.5 m north of `SFXDoor_5` to stream
`BioD_ProFre_201Tali` in, then a second `mevr setloc 0 -9600 15250 -12060` just inside the room, `conversation-world.ps1 -Start
fire:SFXGame.BioSeqAct_CancelHint:Tali_Conversation`):

1. `[VRSTATE] gm 6 live alive -> VR` from the cutscene's first frame (no 3D-screen frame), `moment N: a matinee's move track walks
   Shepard 0.0 s into the cutscene: held in first person`; snapshots `2026-11-20_01-27-09` to `01-27-17` show the scene in stereo
   from the player's place in the room: Miranda walking past, Tali, Prazza and the quarians, Jacob with his gun.
2. **ME2's ANIMCUTSCENE parks Shepard's pawn at the world origin** for the cutscene (`mevr state` mid-cutscene: the pawn at (0, 0,
   0)); his part is played by the matinee's own `PC_`/`HMF_PC_` actor. So the moment's "apart" is the whole level: his pawn is
   hidden by the rule, nothing is drawn at the origin, and the script's three teleports put him at the conversation's place.
3. The moment ended `a conversation began after 13.2 s: the view joins Shepard 8.76 m away under a dip`; `transition fade: the world
   -> the world: from black over 0.35 s`; the conversation opened `stay` from the moment's place (`gm 5 -> VR`).
4. Not yet: a test that opens `SFXDoor_5` the way play does (the chain fired from its `CancelHint` leaves the door shut: from
   outside the view faced the closed door for 13 s, the first two runs); whether the stand-in `PC_` actor is drawn where the player
   stands (the snapshots show nobody at the player's place) [verify with a stance record].

**Counts (offline, the scans):** ME2 has 214 level matinees that move Shepard (lane FPC's scan) and ME3 100 that move him more than
1.5 m of the 910 that bind him (lane FPD3's): all of them are held by rule unless the run-time rules above say otherwise; nothing is
listed by hand. In the game this session: ME2, 3 moments held (Tali's re-intro three times), 0 to the screen. The per-cutscene
counts by reason come from `mevr convo world` (`moments N held, N without a track of Shepard's, N with his pawn hidden, N not from
the player's view`) in a sweep; no sweep ran.

### 6.3 Scenes with no place for the player: the presence table (item 3)

`tools/conversation-scan` records each line group's actors (`actors`: every track's `m_nmFindActor`), and reads ME3's
conversations with ME2's inspection (the plain scan, `--game LE3`, since this lane). `tools/conversation-presence.py --game LE2|LE3
<scans> --write` classifies every conversation and writes `src/core/vr/ConversationPresence.inc`: a conversation the game plays in
its conversation mode (a starting entry that is not ambient) none of whose lines acts on the player (no actor `Player`, no group
named for him) has no place for him. `cw::NoPlaceForPlayer(name, game)` (unit-tested) sends it to the 3D screen at its start
(`mevr convo world`'s count; log `none of its lines acts on the player (the presence table)`).

| | conversations | ambient only | played in the conversation mode | of which no place for the player |
|---|---|---|---|---|
| ME2 (CookedPCConsole and DLC) | 1,225 | 600 | 625 | **109** (the Illusive Man's calls, the squad's comm remarks, dossier videos, tutorials) |
| ME3 (CookedPCConsole and DLC) | 2,240 | 1,397 | 842 | **156** (the docking officer's `tempsf_citadel_docking_v`, Anderson's logs, the endings' radio) |

**Proposal for ME1 (not built):** the same criterion over ME1's scan would replace the curated `cw::IsSetPiece` table; ME1's
current table was chosen after its own criterion (camera share) and the owner's scene reviews, so the change needs their yes.

### 6.4 The floor marker (item 5)

`cw::StepMarker` (core, unit-tested, `TestFloorMarker`): in a first-person scene only, a need in the owner's order (the scene waits
for the player somewhere, a place the game's black screen would take him, where Shepard's scripted walk ends), fading in over
0.4 s, out over 0.5 s once the player stands within 0.3 m (back when he walks 0.6 m off) or the need passes. `cw::State` carries
it (`marker`, `markerNeed`, `markerAt` in tracking space, `markerAlpha`, `markerRadiusM`: host and mod deploy together), and
`plugin/SquadPlaceDraw` draws it in squad placement's own layer when the squad's arc is not up (all three hosts, no swapchain of its
own): a thin pale ring and a fainter inner one, a hair above the floor; snapshot `"squadPlace"."floorMarker"`. Sources built: the
walk's end in a moment, on the sequels (`TickMoment`) and on ME1 (`StepMomentMarker`; its own commit, landed 2026-11-20 after
lane FPC3 ran ME1's regression set with it: 16 scenes, no regression of its own, all 11 canaries caught, the marker logged in the
council walk at 8.4 m and the Spectre walk, the ring in snapshot `2026-11-20_02-18-06-160`). Log `convo: marker:`.
**Not built:** the "waits" source (which trigger or volume the script waits on) and the "black move" source (the teleport's place
before the black), on any game. **In the game:** on ME2's Tali the track's last key came back as (0, 0, 0) (the ANIMCUTSCENE's
pawn parked at the origin, item 6.2.2), refused by the marker's sanity check (`no walk's end, no marker`): no marker drawn. ME1's
walk with Anderson to the council is the check left (not run: ME1's regression set and the marker need one ME1 stretch).

### 6.5 What else changed

- The sequels' state's `cut` is a module-wide counter (`gCutSerial`): a Cut opening and a moment's join past 1.5 m dip.
- A held conversation's end leaves its place as the place on foot (gFoot, gFootPov), for a moment or an opening right after it.

### 6.6 What a snapshot of the conversation shows (open)

Tali's conversation in first person (snapshot `2026-11-20_01-27-20-617-left.png`): two dark shapes fill the bottom corners: the
inside of Shepard's own shoulders (the slice's eyes 8 units ahead and 4 above his head bone, `PlaceFromHead`). ME1's rule is
`StepEyeHeight` (item 44) and the first-person eye offset: port it next (the eyes at the player's height over his floor, ahead of
the collar).

### 6.7 Not done in this lane

ME3's checks (an ordinary Citadel conversation, a walking one, Joker seated, the docking officer to the screen) and an ME3 stretch;
looks at the player's eyes (ME2 `BioPawn.SetLookAtTarget`, ME3 `SFXModule_LookAt`); the circle and the squad's formation with
`game/AiWalk`; subtitle and wheel placement and `ConversationHandover`; walk-along (the matinee paused when the player falls
behind), the game's black moving the player, rides and seats on the sequels; the stance records and `scene_audit` rules on the
sequels; a canary per new rule.

## 7. Lane FPC3 (2026-11-20): the eyes, rides, the handover, ME3's fixtures

The brief's items 0 to 6 on both sequels; what was built, how to check it, what the game showed. Numbers only from the code and
the logs.

### 7.0 ME1's regression set with the floor marker (item 0)

FPC2's ME1 commit (1553434b, on this branch c453e66e: the floor marker where a scripted walk of Shepard ends) with ME1's set
(`tools/conversation-audit.ps1`, session `2026-11-20_02-08-40-audit`, the players' build `0.3.2+c453e66eb4`, before lane EXIT
landed): 16 scenes played from their saves (Kirrahe's from `squad-saves\John02_70` not played: the save is missing), every scene's
body measured where a wrist was in view, 2 findings (`face-off-you`: Benezia 89 degrees at line 53, Udina 77 at the ceremony's
opening: both known, research/69 item 38), 1 regression against the baseline (`lav70_asariscientist_dlg`: faces off the eyes, median
13 to 23 degrees; lane EXIT's long run showed the same Liara verdict from a baseline written from short runs, with the exits off
too: not the marker's). **The marker in the game:** `convo: marker: moment: Shepard's walk ends there ..., 8.40 m from the player`
in the walk with Anderson to the council (`SarenCouncil.SeqAct_Interp_0`), and 24.04 m in the Spectre ceremony's walk
(`PlayerBecomesSpectre.SeqAct_Interp_2`), one marker each; the snapshot `2026-11-20_02-18-06-160-left.png` shows the thin pale ring
on the walkway ahead of the player, Anderson beyond it; at 02-18-15 (the player at the walkway's end beside Anderson) no ring.
The host drew it in squad placement's layer (`squad place: last 10 s: 191 frames drawn`). The snapshots' JSON carries no
`squadPlace` block (no host feeds `squad_place_draw::StatusJson` to the test loop: section 6.4's "snapshot
"squadPlace"."floorMarker"" isn't wired). **The canaries** (`conversation-audit.ps1 -Canaries`, after lane EXIT landed: main
9e93a920 merged, the players' build `0.3.2+bd33ccc2a1`, sessions `2026-11-20_02-31-02-canary-*` to `02-56-19-canary-popped-out`):
all 11 broken runs caught, each by its own rule (body left at the mark; the empty chamber by `expect`; no hands; the five of
Opold's game; the view scaled about the pivot by `eye-height`/`opening-off-mark`; the matinee's fade by `dark-view`/`eye-flat`;
EXIT's popped-out by `vanished-in-view`); exit 0.

### 7.1 The eyes (item 1)

**The cause of 6.6.** The slice held the view at `PlaceFromHead`: Shepard's head bone (the pawn's mesh, bone `Head`) plus 8 units
along his facing and 4 up. The arm IK's own measure on foot (`arm IK: first person camera vs Shepard's head bone`, every 5 s in
first person, the headset at the recenter point) on ME2 is about **24 forward, 13 up** (LE2 runs of 2026-11-14, lanes B to F, the
fake headset with no floor lock: `forward 23.7..25.5, up 12.8..13.5`; with the floor lock driven by a replay, 2026-11-20, `forward
28.8, up 25.3`): the conversation's eyes sat about 16 units back and 9 low against first person on foot, inside the collar.
ME1's constant is the same measure on ME1 (`eyeFromHead` 8.9, 1.7, 29.3).

**The rule now (ME1's, `game/SequelConversationWorld`):**

1. **The opening's eyes** (ME1's `FinishOpening` / the view on foot): a Stay or WalkIn with a fresh view on foot (the place
   on foot known within 10 s, `gFootPov` valid) holds the eyes where the player had them on foot: `eyeOffset = foot POV -
   foot pawn`, the facing the foot view's yaw (never turned at the opening; the slice turned the player to the stage's facing).
   A Cut, or no fresh view on foot: his head bone at the mark plus the first-person eyes from it in the facing's yaw.
2. **The eyes from the head bone, learned** (`cw::NoteEyeFromHead`, core, unit-tested): on foot in mode 0, with the world mode
   wanted and the first-person camera the pawn's (`player::FloorLockSample().cameraOwned`), each tick the game's own camera
   (`CameraCache.POV`: the renderer adds the head's lean after it) against his animated head bone, in the POV's yaw frame, is a
   sample when the host's view offset (the lean from the recenter point) is under 5 units (the body follower slides the body
   with a lean) and inside the sanity box (forward and right within 60/30 units, up -45..60); the first sample is taken whole,
   later ones at a gain of 0.05. Fallback without a sample: ME2 (24, 0, 13); ME3 the slice's (8, 0, 4) until a measure (7.3).
   Log `conversation world: on foot the first-person eyes are F forward, R right, U up from Shepard's head bone (N samples ...)`
   at the first sample and every 60 s.
3. **The eyes' height** (`cw::EyeHeightStep`, core, unit-tested; ME1's `StepEyeHeight`, research/69 item 44): each tick after
   the opening, the host's drawn eye centre (`host_view.centreZ`: the held place plus the head's lean; the floor lock stands
   down in a held conversation, so it is the raw centre) is brought to Shepard's cylinder floor (`pin.z -
   CylinderComponent.CollisionHeight`) plus the player's live standing eye (`stageEyeM`, the floor lock's STAGE eye): 1 m/s in the
   opening's first 1.5 s, 0.3 m/s after, nothing within 1 cm, nothing without a standing eye (0.5..2.6 m). The step goes into
   `eyeOffset.z`. Log `conversation world: conversation N: the drawn eyes are X m over the floor under Shepard, the player's Y m:
   the view goes up|down by Z m`; the end line says where the eyes came from and how far their height was brought. The fake
   headset's STAGE eye is 1.7526 m (the null driver's), so it acts there too (since the replay drives the floor sample,
   2026-11-19).
4. ME1 keeps its own inline `StepEyeHeight` (the same numbers but its eye fallback `gFloorNow`); moving ME1 onto
   `cw::EyeHeightStep` is a no-op refactor left for an ME1 batch (its compiled code is untouched here).

**Check:** a snapshot of a held conversation's eyes (Tali's on ME2, an ME3 one): no dark shapes in the bottom corners; the
opening line's `the eyes from the view on foot (x, y, z from his pawn)` or `his head bone (...)`.

**In the game (fake headset, 2026-11-20 03:15-03:34; ME2 stretch `20261120-031526-824-LE2-fpc3`, ME3 `...-LE3-fpc3-le3`):**

1. **The "shoulders" of 6.6 were Shepard's own hands.** The runner's replays (`tools/steer-common.ps1`'s `Steer-Play`, every
   `conversation-world.ps1` run) hold the tracked hands up in view (`make-walk-recording.py --hands view`: 0.25 m ahead of the eyes,
   0.08 m under them, 0.09 m to each side), so arm IK draws Shepard's armoured fists in the bottom corners. Tali's conversation with
   the same replay (snapshot `2026-11-20_03-17-23-352-left.png`) shows them as FPC2's did; the same conversation with the hands at the
   chest (`--hands chest`, `2026-11-20_03-18-45-612-left.png`) has clear corners: Tali, Prazza, the quarians and the room; looking
   down 55 degrees (`2026-11-20_03-19-09-957-left.png`) shows the carbon-weave forearms and gloves that filled the corners. A check
   of the eyes needs `--hands chest` (or no replay).
2. **ME2, Tali's reintroduction** (the moment, then the conversation): `opening stay ... the eyes from the view on foot (0, -35, 85
   from his pawn)`, held at (-9619, 14809, -11972) against the slice's (-9621, 14835, -11979): 26 units further forward, 7 up; the
   eye height brought 0.02 m. The learned eyes on foot: `29.4 forward, -2.2 right, 23.1 up` (1 sample at Freedom's Progress), then
   `16.7 forward, -0.6 right, 14.5 up` (145 samples on the Normandy).
3. **ME2, Samara's starter** (`normw_starter_h`, fired from the Normandy's corridor: a Cut 15.4 m away): `the eyes from his head
   bone (the eyes as learned on foot) (-4, -8, 78 from his pawn)`, `the drawn eyes are 1.66 m over the floor under Shepard, the
   player's 1.75 m: the view goes up by 0.09 m`. The view faced the observation window (his pawn's facing): ME1 faces the owner,
   now on the sequels too (commit "a Cut opening faces the conversation's owner"); with it the view turned to -13.9 degrees, toward
   Samara 2.9 m away, but **Samara was not drawn** (snapshot `2026-11-20_03-21-30-434-left.png`: the window and stars, nobody) [verify
   culling or hiding: the sequels have no `person-not-drawn` rule yet; ME1 reads `PrimitiveComponent.LastRenderTime`].
4. **ME3, the docking officer** (Save_0090 loads into it): `citprs_citadel_docking_v_D.citprs_citadel_docking_v_dlg` stayed on the
   3D screen **by the load rule, not the presence table**: the table has `tempsf_citadel_docking_v` but not this one, because every
   line's `Conversation` group names `Player` (his VO element and gesture tracks: Shepard speaks, off screen) though the scene is a
   comm call staged in a void (held by hand with `mevr convo world on`: `2026-11-20_03-29-24-446-left.png`, the officer seated in
   black space, the wheel below). The criterion "a line acts on the player" counts the player's own voice: ME3's video calls (`_v_`)
   need another rule (question 3 of the lane's report). Held by hand: `opening cut ... the eyes from his head bone (the game's
   measure) (10, 3, 79 from his pawn)`, height +0.09 m. ME3 never stood on foot in the run (the save loads into the call and our
   menu's first-run guide kept the input): no ME3 measure of the eyes from the head bone yet (the fallback stays the slice's 8, 0, 4
   until one).

### 7.2 Rides (item 4, part)

ME1's `RideBase` (research/69 item 42) on both sequels: each camera update before the pin, each conversation tick and each moment
tick, the pawn's `Engine.Actor.Base` and its place; while the same base moves 0.01..500 units since the last read (under 250 ms
apart), the delta moves the pin, the boundary's centre, where the scene wanted Shepard, the kept places of those whose base is
the same, and a moment's view (`gM.feet`, taken by `TickMoment`). The pin is the reason it matters on the sequels: it puts
Shepard back where the player stands each camera update, and on a lift that was where the ride began (the ME3 audit's row 23:
36 full ME3 conversations move a shuttle, car or lift). Logs `conversation world: what Shepard stands on moves (<base>, X m this
tick): the player rides with it`; the end line `the player rode X m with what Shepard stood on`. LE3 ReflectCheck row
`Engine.Actor.Base` added (ME2 and ME1 had one). Not seen in a ride yet.

### 7.3 The game's fade to black moves the player (item 4, part)

ME1's `worldBlackMoves` (research/69 items 40 and 41) in a held conversation on both sequels (`StepBlackMove`): the source hook
records every placement and track move of Shepard it withholds (`gC.sceneWants`, `sceneAsks`); a new one more than 1.5 m
(`cw::OpeningLimits{}.stayUnits`) from the pin, while the camera's fade is half black or more (`Engine.Camera.bEnableFading` and
`FadeAmount`, read after each camera update) or was within 1.5 s, moves the pin there (the camera update's pin then puts his pawn
there) under the host's dip (`++gCutSerial`). Curated walks keep the old rule (`cw::IsCuratedWalk` with `[Cinematics]
worldCuratedWalks`; none built in for the sequels). `[Cinematics] worldBlackMoves = 0` turns it off (ME1's key and default 1). A
step within 1.5 m stays where the player is (ME1 walks it by the simulated stick; the sequels have no steer). Log `conversation
world: conversation N: under the game's fade to black the scene wants Shepard X m away: the player goes with him under a dip`;
the end line counts the moves. **Not read yet:** the script's GUI black screen (ME2's `BioSeqAct_BlackScreen`, ME3's
`BioSFHandler_BlackScreen`, research/le3-conversations-in-world.md 5.4): a black that is only GUI moves nobody. Not seen in the
game (no fixture yet: a conversation whose stage moves Shepard under a fade).

### 7.4 The handover into a GUI screen (item 3, part)

ME1's research/69 item 38 rule on the sequels: `MevrSeams::WorldUiOwnsGui(mode)` (plugin/MenusLe2; ME2's `d3d_capture.cpp` and
ME3's `UpdatePresentationState` ask it for their GUI mode) answers true for the game's GUI mode while
`MevrSeams::ConversationHandover()` holds (the mod's "handing over" byte, 150 ms fresh; the mod publishes it for 300 ms after a
conversation shown in the world ends). Host only; not seen in the game (a conversation that ends into a shop or a GUI screen).

### 7.5 ME3's console for fixtures

`mevr instances <Package.Class> [text]`, `mevr activate <n> [input]` and `mevr setloc <n> X Y Z` (ME2's lines) on ME3 too, so
`tools/le2-halcyon-run/conversation-world.ps1 -Game LE3 -Start fire:SFXGame.SFXSeqAct_StartConversation:<text>` works there.

### 7.6 What the code says about the rest (items 3 to 5), for the next lane

1. **Looks at the player's eyes** (ME1's `StepEyes`, `worldLookEyes`): ME1 moves a look at Shepard to a `BioLookAtTarget` at
   the drawn eyes only while his body stands away from them (his head bone more than 0.5 m from the drawn eyes, flat: a walk-in
   by the view, a moment). On the sequels Shepard is kept on the pin, under the player's eyes, in every conversation (no walk-in
   by the view exists there): the stand-in would never switch on in a conversation. It matters only in moments, and ME2's
   ANIMCUTSCENEs park his pawn at the world origin and play a stand-in actor whose faces are the matinee's (ME1 found the same in
   its six moments: "nobody had a look-at target"). Port it with moments' walk-along, not before; ME2's names are ME1's
   (`BioPawn.m_oLookAtData`, `SetLookAtTarget`), ME3's `SFXModule_LookAt.ChangeTarget` (research/le3-conversations-in-world.md 2.4).
2. **Seats** (ME1's `StepSeat`): the pin is the obstacle. A Stay opening puts Shepard back where the player stood, so a stage
   placement onto a chair is withheld and the sit plays on the spot. The rule that fits ME1's: "the scene seats Shepard" makes the
   opening a Cut (his place is his seat), from a generated table (research/le3-conversations-in-world.md 5.3: the scan must record
   the gesture pose names per group; ME3 37 conversations seat Shepard); then `StepSeat` (the view follows his head down and up,
   no walking) on his skeleton's thresholds, which must be measured on ME2 and ME3 first (ME1's: standing head 0.70-0.82 m over his
   pawn's place, seated under 0.62).
3. **The game's black moving the player**: built for the camera's fade (7.3). Missing: the script's GUI black screen on the
   sequels (ME2's `BioSeqAct_BlackScreen`, ME1's route in game/AuthoredMoment; ME3's three routes ending in
   `BioSFHandler_BlackScreen`, research/le3-conversations-in-world.md 5.4), and in moments. Then the floor marker's "black move"
   source is where the black would take the player in a curated walk (the only place the player isn't moved).
4. **Walk-along** (ME1's `StepWalkAlong`): applies when the player follows the script's Shepard with companions; under the owner's
   current answer (a) for ME2's stand-in cutscenes the view stays where the player stood, so there is nothing to walk along with on
   ME2; ME3's moments that walk Shepard's real pawn are where it applies (research/le3-conversations-in-world.md 5.5's open
   point: pausing a conversation line's matinee may desync the line).
5. **The circle and the squad's formation** with `game/AiWalk` (ME2 `PrecisionMoveTo`, ME3 `MoveToSlot`): not started here.
6. **The floor marker's "waits" source**: the scene waits for the player at a trigger or volume; needs the level's script read (the
   scan's `waits` for each start) on all three games. Not started.

## 8. People leaving a scene on ME2 (lane EXIT2, 2026-11-20)

ME1's rule (research/conversation-exits.md: hold the script's removal of one of the scene's people while the player sees them,
walk them away with the game's AI, apply once out of sight) on the sequels, from the same module `game/ConversationExit`; the
whole guide, per game, is that note's section 9. What this module gave it: one accessor, `ScenePeople` (the reader's people of
the live conversation), and the cut counter's `DipFromBlack`/`CutCount`; the armed word on foot now carries `cut` (the host's
`FootCut`, a dip over a removal on foot). ME2's first fixture is Tali's reintroduction (section 3.2's route): at
`profre_tali_intro_d_dlg`'s end the level's `BioSeqAct_ModifyPropertyPawn_0` deactivates Tali, Prazza and two guards in the
clear; `tools/le2-halcyon-run/conversation-exit.ps1` reaches it by script (`mevr convo exit end` ends the conversation by the
world's own call so the script's end runs).

### 8.1 Lane EXIT3 (2026-11-20): ME2's walk is the Kismet's own; ME3 in the game

The guide is research/conversation-exits.md sections 11 (ME3) and 12 (ME2's walk). What this module's lanes should know:

- **ME2's AI walk** (`game/AiWalk`'s ME2 adapter, also what a circle or a mark walk on ME2 can use): `SFXAI_Core.MoveToGoal` to
  the level's navigation point nearest the point (the `MovingToGoal` state: a path, the AI's own pace, about 1.7-2 m/s in Tali's
  room), arrival when the state pops; `PrecisionMoveTo` (0.3 m/s) only without a goal. A navigation point "with a path" is
  asked with UE3's constraints first (`Goal_AtActor.AtActor`, `Path_TowardGoal.TowardGoal`, then `FindPathToward(goal, false,
  2000)`): without them every search fails.
- **ME3's people in a hub** are `SFXStuntActor`s (only the player is a pawn there): no AI walks them; the sequels' reader
  (`m_aSpeakerData`) lists only who has spoken, so the exit module also reads the controller's `m_pOwner`, `m_pSpeaker`,
  `m_pListener`, `m_aAllFoundActors` and `m_aExtraParticipants`.
- Seen once on ME3 (before lane FPC3 landed; not checked after): at a held conversation's end the view turned from the held yaw
  (0) to the pawn's own facing on foot, so people in view at the end were out of view a tick later (section 11, run 2).
- Test aids on ME3: `mevr convo exit skip` (SkipNode), `reply <n>` (QueueReply), `people` (bearing and sight of each person);
  the runner's `-StartInput`, `-HeadYaw` and its ME3 advance loop.

## 9. Lane FPC4 (2026-11-20): records from above, looks at the eyes, ME3 in the game, seats, walk-along, the marker's black move

What was built on both sequels, how to check it, what the game showed. Numbers only from the code and the logs.

### 9.1 The records from above and the drawn rules (items 2 and 7)

`game/SequelConversationWorld` writes ME1's stance records (research/69 "Stances from above"), the same JSON, so
`tools/scene_audit.py`'s rules and `tools/conversation-stance.py` (`--findings`, `--heads`, `--audit`, the sheets) judge the
sequels' scenes unchanged:

1. **Switches:** `mevr convo stances on|off|auto` (over `[Cinematics] worldStances`, 0 by default, ME1's key), `mevr convo stance
   [tag]` for one record now. With them on: `opening` 1.5 s after the place is taken, `after-dip` 0.5 s after a cut, `line` at each
   new entry, `moved` when someone of the scene moved a metre or turned 40 degrees (or the drawn eyes 0.75 m), `tick` every 2 s,
   `end`; a moment's `moment-begin`, `moment` every 1.5 s, `moment-end`. Files `%LOCALAPPDATA%\MEVR\stances\<stamp>-<number>-
   <scene>-<tag>.json`, log `conversation world: stance: <file> (<tag>)`.
2. **What a record holds** (ME1's fields, read on each game): `game` (LE2, LE3), the player's held place, the drawn eyes (the held
   frame's head), `bodyFromEyesM` (Shepard's drawn head bone from the eyes, flat), `hands` (arm IK's MeasureHands), `bodyDrawnAgoS`
   and each person's `drawnAgoS` (`PrimitiveComponent.LastRenderTime` against `WorldInfo.TimeSeconds`), `inSight` (head inside the
   head's view, 35 degrees each way, a clear `FastTrace` from the eyes, within 12 m), `nearPlane` (after the write-back), `face`
   (the head bone's X axis; `axesToAimDeg` gives all three axes' angles to where the look-at chain aims, the measure that names the
   face's axis on a skeleton: the one ME3 record with a chain had it hidden and aimed at the world's origin, no measure; [verify]
   the X axis on ME2's and ME3's skeletons before trusting `toYouDeg`), `gameLook` (each game's chain, 9.2), `others` (everyone shown within 20 m). No wall scan and no
   circle yet.
3. **The rules, unchanged and not forked:** `tools/scene_audit.py` groups records by game (a sequel's scene key is `LE2:<name>`,
   ME1's unchanged) and its tests (`tests/SceneAuditTests.py`) judge sequel-shaped records: a person in sight never drawn is
   `person-not-drawn`, the body in view undrawn `body-not-drawn`.
4. **The canary:** `mevr convo hide body|person on|off` (the sequels' broken runs, ME1's words) hides the player's body or the
   owner's mesh while a scene is held; `tools/le2-halcyon-run/conversation-world.ps1 -Stances -Hide person` must end with a
   `person-not-drawn` finding in its `findings.txt` (the runner also writes `--heads` and the sheets there). `-Hands view|chest`
   picks where the replayed hands are held (chest for the eyes' snapshots, view for the body's drawn check).

### 9.2 Looks at the player's drawn eyes (item 3)

ME1's `worldLookEyes` on both sequels (`EyesStep`; the core's `cw::EyesStandIn`, ME1's 50 units on and 35 off, unit-tested):
while Shepard's head as the scene animates it (`ShepardSceneHead`: arm IK's animated head when fresh, else the bone) stands that
far (flat) from where the player's eyes are drawn, a `SFXGame.BioLookAtTarget` (made once by `Actor.Spawn` on the controller, the
class default's `bStatic`/`bNoDelete` cleared for the call, kept as a `ue3::Kept`) stands at the drawn eyes and every look the
game aims at Shepard's pawn is moved to it at its own rank, given back when his head is with the eyes again or the scene ends.
Each game's chain is `game/SequelLook.inc`: ME2's is ME1's (`BioPawn.m_oLookAtData`: `m_oTarget`, `m_nRank`, `m_Controllers`;
`BioPawn.SetLookAtTarget`), ME3's a module (`Engine.Actor.Modules` holds an `SFXModule_LookAt`: `m_pTarget`, `m_nRank`,
`m_Controllers`; `ChangeTarget(Target, a_eTransition, nRank)`). `mevr convo eyes on|off|auto` (A/B), log `conversation world:
... eyes:`, the records' `eyesTarget` and each person's `gameLook.target` ("you" for the stand-in).
**Seen on ME3** (the embassies, 2026-11-20 04:36): with Shepard's head 0.51 m from the drawn eyes the stand-in came on and
`SFXStuntActor_4's look at Shepard (rank 99999) moved to the player's eyes` (the module's own target read back as the stand-in).
Pinned under the player (every opening on the sequels) his head stays within 0.1-0.2 m of the eyes and the stand-in rests, as
section 7.6 said: it acts when the player's own steps or lean take the eyes away from his body, and in moments.

### 9.3 ME3 in the game (item 1)

Stretch `20261120-041838-817-LE3-fpc4-le3` (fake headset, `prep.ps1 -Game LE3 -RendererPreview`, `load.ps1 -Save
John_12_Vanguard_021024_46e6e39\Save_0090.pcsav`; restored with 0 differences):

1. **The save loads into a conversation** (the rapid transit terminal: "Presidium Commons ... Embassies ... Hospital"), so
   `load.ps1` waits for gameplay and times out; the game is in mode 7. `mevr convo pick 2` took the cab to the embassies.
2. **Why `mevr thespian pick` was refused** (FPC3's question): not our menu's first-run guide (it never opened in either run: it
   opens on foot after the calibration's prompt) but the host's spoken-input lease, held only while the XR session has input
   focus; the fake headset's session never has it (the snapshot's `sessionFocused false`). The refusal now names it on the
   sequels (`thespian: another menu owns input (the host's spoken-input lease is not held: ...)`). The runner's inis mark the
   guide as shown anyway (`[Menu] controlsShown = 1`), and the conversations' test pick is `mevr convo pick <position>` (the
   wheel's own `SelectConversationEntry` on the location holding that reply, ME1's console word).
3. **ME3's hub characters are SFXStuntActors, not pawns**, and not on the world's pawn list (`mevr convo people` at the
   embassies: one pawn, Shepard, and 26 stunt actors). The module's people take them now (`IsPerson`, `PersonMesh` = their
   `BodyMesh`; a scan once a second as game/Hair's), and the ME3 reader no longer returned before the owner, speaker and listener
   when the speaker data was empty. Since the merge with lane EXIT3 the reader also takes the start's found actors
   (`m_aAllFoundActors`) and extra participants (`m_aExtraParticipants`), and is the one reading of ME3's scene people:
   `game/ConversationExit` takes it through `ScenePeople` (its own copy removed). **Consequence for item 4:** a stunt actor has no AI controller, so `game/AiWalk` can't walk
   ME3's hub people into a circle (their composition stays the authored one, research/le3-conversations-in-world.md 5.2).
4. **The eyes on foot are drawn 32 units above ME3's game camera** (the floor lock's correction, in the view anchor's frame:
   `conversation world: on foot the eyes are drawn 32 units from the game's camera (+0, +0, +32)`). A Stay opening held from the
   camera drew the eyes 0.32 m low (`the drawn eyes are 1.43 m over the floor under Shepard, the player's 1.75 m: the view goes up
   by 0.32 m`); the eyes on foot are now taken from the drawn view (the anchor's origin, at the recenter point): `the eyes from
   the view on foot (-35, 1, 87 from his pawn)`, no rise. **ME3's learned eye offset** (item 1's last check): first 10.8 forward,
   -27.2 up (976 samples) against the drawn head bone, which the body follower and body scale move (the fake headset's 1.75 m eye
   scales ME3's Shepard up); against the animated head now **31.2-33.3 forward, -0.2..-3.4 right, 9.5-10.3 up** (354 samples),
   with arm IK's own measure 33.1-34.3 forward, 4.9-9.5 up. ME3's fallback is 34, 0, 5.
5. **`citprs_khalisah2_d_dlg`** (`Khalisah.SFXSeqAct_StartConversation_2`; `_1` started nothing in this plot): opening `stay`,
   records `opening` .. `line` .. `end`, `0 findings`, Shepard's drawn head 0.09-0.19 m from the eyes (session folder
   `convo-khal3-043435`, its `findings.txt` and sheet). **Nobody to see:** its owner and speaker is `SFXStuntActor_4`, 1.8 m in
   front of the player, `bHidden` and never drawn (`drawnAgoS` 600); on the 3D screen the director shows Shepard alone, framed
   against a door, in every shot (snapshots `2026-11-20_04-35-50-391-left.png`, `..._04-35-58-478-left.png`): Khalisah speaks
   from off screen. In first person the player faces the embassies' sign (`2026-11-20_04-34-53-992-left.png`). It is a scene
   with no place for the player that neither the presence table (Shepard has lines) nor a rule of ME1's names (ME1's run-time
   presence rules look at Shepard's pawn and the owner's distance): question 2 of the lane's report.
6. **Not reached on ME3:** a walking conversation (`citprs_miranda_talk1_m_dlg`, `citapt_liara_m_dlg`: not loaded in this
   save's embassies; the hub's other wards are a cab ride and a plot away), Joker seated (`cat004_hangar_shuttle_m_dlg`, the
   Cronos route), a level cutscene that walks Shepard, the handover, rides and the black move (no fixture in reach). The
   `Samara.SFXSeqAct_StartConversation_0` start in `BioD_CitHub_EmbassyP3` started Khalisah's conversation again (conversation 2:
   its chain shares the start), [verify] with the conversation name before using it as a fixture.

### 9.4 Seats and walk-along (item 5)

- **Seats:** ME1's inline `StepSeat` is the core's `cw::StepSeat` now (ME1's numbers, unit-tested; ME1's own code untouched).
  On the sequels each tick of a held conversation: his animated head against his pawn's place; standing (0.70 m or more,
  not crouched) it learns his standing height, 15 cm under it he sits, the held view follows his head (1 m/s, 4 cm deadband), the
  stick doesn't walk, StepEyeHeight waits while the view follows, the legs keep the seat (`BodyAway`); 8 cm from standing he gets
  up and the view comes up. ME3's Shepard stands with his head 0.75 m above his place (the embassy records): ME1's thresholds fit.
  The pin still keeps a Stay opening's Shepard where the player stood, so a stage placement onto a chair is withheld and the sit
  plays on the spot (section 7.6's obstacle; the owner's question "seats opening as a Cut at his seat" stands). Not seen in a seat.
- **Walk-along:** ME1's `StepWalkAlongTick` in the sequels' moments (`StepWalkAlong`; the core's `cw::StepWalkAlong`, ME1's keys
  `worldWalkAlong`, `worldWalkAlongPauseM` 3, `ResumeM` 1.5, `MaxS` 120): the matinee is the move track instance's outer
  `SeqAct_Interp` (a `ue3::Kept`, `bPaused` written through the FieldRef's journal), a companion is anyone its groups move within
  5 m of Shepard's pawn, a line is someone's FaceFX on ME2 (ME3's `IsPlayingFaceFXAnim` takes a name and its sound tracks are
  Wwise: no line read, the pause comes at once). ME2's cutscenes park the pawn at the origin and walk a stand-in, so nobody walks
  within 5 m of it: no pause there. Not seen in the game (no ME3 moment reached).

### 9.5 The floor marker's black-move source (item 6, part)

In a held conversation (`StepBlackMove`), when the game's fade to black wanted Shepard somewhere and the player was **not** moved
(a curated walk, `worldBlackMoves = 0`, or a step within "where he stands", which ME1 walks by the stick and the sequels leave to
the player) the marker shows there (`MarkerNeed::BlackMove`, `cw::StepMarker`, drawn by the host's squad-place layer as the walk's
end is) until the player stands in it; log `convo: marker: conversation N: under the game's fade to black ...`, the end line's
`N floor markers`. ME1's own black-move source (the council walk, curated) is not built: an ME1 change for its batch. The "waits"
source (a trigger or volume the script waits on) is not built on any game: it needs the level's script read per start (the scan).
Not seen in the game (no fixture whose fade wanted Shepard within 1.5 m).

### 9.6 ME2 in the game (item 2's Samara, Tali's moment and conversation, the end of a held conversation)

Stretch `20261120-045217-563-LE2-fpc4-le2` (fake headset, `prep.ps1`, restored with 0 differences):

1. **Why Samara wasn't drawn** (FPC3's `2026-11-20_03-21-30-434`; `load.ps1 -Save John_12_Soldier_190924_4ee650d\Save_0036.pcsav`,
   `conversation-world.ps1 -Start fire:SFXGame.SFXSeqAct_StartConversation:Dialog_Samara.SFXSeqAct_StartConversation_0 -Stances
   -Hands chest`): the records named it at once (`person-not-drawn: SFXPawn_Samara_01_0 ... in sight, not drawn`), and `mevr convo
   people` showed nothing hiding her (actor and mesh flags clear). Holding the game's camera at the player's place
   (`mevr convo camera on`, ME1's `worldCamera`, off by default) changed nothing. **She kneels in meditation** (her head bone 0.7 m
   over the floor, facing the window) **29 degrees under a level gaze**, and ME2's eye image is 16:9 (4352x2448): she was below the
   picture. Looking down 30 degrees (`make-walk-recording.py --pitch -30`) she is there and drawn at once (snapshot
   `2026-11-20_04-58-54-251-left.png`: Samara kneeling a metre ahead, her line's subtitle and the wheel above her). Not a bug of the
   scene; a false "in sight" of the records' cone (ME1's 35 degrees each way), which now follows each sequel's eye image up and
   down (ME2 0.70 x 9/16, ME3 0.70 x 2216/3072). With it the level run is clean (8 records) and **the canary** (`mevr convo hide
   person on`, the head pitched to her) is caught: `person-not-drawn: SFXPawn_Samara_01_0 ... not drawn for 2.5 s`. Her face is 150
   degrees off the player all scene: she speaks with her back turned (the authored pose).
2. **Tali's reintroduction** (`load.ps1 -Save VR_31_Soldier_141126_336072f\Save_0004.pcsav`, `god`, `mevr setloc` 8.5 m north of
   `SFXDoor_5`, 12 s, `mevr setloc 0 -9600 15250 -12060`, `conversation-world.ps1 -Start fire:Engine.SequenceAction:Tali_Conversation.
   BioSeqAct_CancelHint_0`: `SFXGame.BioSeqAct_CancelHint` isn't found as a class in this load, `Engine.SequenceAction` finds the
   action): the moment held from the first frame, records `moment-begin` .. `moment-end`, the join under a dip 4.06 m away, the
   conversation opening `stay` with **the eyes from the view on foot (1, -35, 87 from his pawn)**, 15 records **clean**: Tali 7-15
   degrees off the player's eyes and looking at them, Prazza (`BioPawn_1`) 13 degrees, Jacob and Miranda looking at Tali. The
   moment's records: `hands-off-controllers` 2.98 m (the cutscene parks Shepard's pawn at the world's origin and plays a stand-in:
   the body is nowhere near the player during it, owner question 1 of FPC3's), and at `moment-end` (the cut to the conversation)
   a `look-at-body` (Tali) and a `person-not-drawn` (Jacob, 6.6 s), both at the transition. **Walk-along** read six companions
   within 5 m of the parked pawn, 180 m from the player ("a line is playing: the walk pauses at its end": it never paused because
   the line went on); it now never acts past 30 m from the view, and the second run logged no walk-along line.
3. **The end of a held conversation** (lane EXIT3's ME3 finding: the view turned in one tick from the held yaw to the pawn's
   facing): `conversation 1 ends: the held facing -15.5 deg, the controller's -1.4, the pawn's -1.4` (Samara), `-88.1 / -88.1 /
   -106.8` (Tali): the controller and the pawn are now turned to the held facing as the hold lets go (`FaceOnFoot`; `mevr convo
   endfacing on|off`), so the first frame on foot looks where the last held one did. Not rechecked on ME3's Prothean fixture
   (`citprs_proth_char_moment_m_dlg`, research/conversation-exits.md section 11).
4. **ME2's eyes:** on foot the eyes are drawn 2-17 units from the game's camera (ME3's 32); learned against the animated head
   23.5 forward, -6.0 right, 11.0 up (arm IK's 24, 13).
5. **Moments this stretch:** 2 held (Tali's reintroduction, twice across a reload), 0 to the 3D screen. ME3: 0 (none reached).

## 10. Lane FPC5 (2026-11-20): the circle and the squad, the subtitle and the wheel, the marker's waits, ME1's end facing

The brief's items 1 to 5 on both sequels and ME1's part. Numbers only from the code and the logs.

### 10.1 The conversational circle and the squad's formation (item 1)

ME1's rules (research/69 item 28 and its second design) on the sequels, in `game/SequelCircle.inc` (included by
`game/SequelConversationWorld.cpp`), every rule from the core:

1. **When the circle forms:** after the opening, once the player speaks or is spoken to now or within `worldCircleLookahead` (4)
   lines on any branch (`cw::PlayerSoon` over the dialogue's graph; people talking among themselves move nobody). It forms round
   the player's drawn eyes toward the conversation's owner (`cw::MakeCircle`), as many places as the conversation's shown people
   plus the player.
2. **The graph:** `SFXGame.BioConversation`'s `m_EntryList` (`nSpeakerIndex`, `nListenerIndex`, `ReplyListNew[].nIndex`) and
   `m_ReplyList[].EntryList`, ME1's names on both sequels (ME2's conversation object is its data; ME3's data is the controller's
   `m_pConvData`). A speaker index is -2 the player, -1 the owner, else ME2's `m_SpeakerList[i].aSpeaker`, ME3's tag
   `m_aSpeakerList[i]` matched against the controller's `m_aSpeakerData` then `m_aAllFoundActors` (`nmSpeakerTag`).
3. **Who walks:** whoever speaks or is spoken to now, or takes part within the lines read ahead, joins (`cw::JoinCircle`): the
   owner stays where it stands (`worldCircleOwnerStays`), anyone within `worldCircleInRangeM` (3.5 m) of the player joins where it
   stands, farther than `worldCircleMaxWalkM` (12 m) it stays out, else it walks to the nearest free place with `game/AiWalk`
   (ME2: `SFXAI_Core.MoveToGoal` to the navigation point nearest the place, `PrecisionMoveTo` without one; ME3:
   `BioAiController.MoveToSlot`), the walkers `worldCircleStaggerS` (0.6 s) apart, facing the circle's middle on arrival
   (`BioPawn.BioSetDesiredRotation`, the game's own turn). Non-walkers never walk (`CanCircleWalk`): no pawn (ME3's hub
   `SFXStuntActor`s: the authored composition stays), not `PHYS_Walking`, seated (the head under 0.62 m above the place), or a
   matinee's track moves it. A place without floor under it (a trace down past the feet) is not walked to.
4. **The squad** (`worldSquadGather`, 1.7 m out and 0.5 m back): once, after the opening, a squadmate the conversation's start
   moved more than a metre off where it stood with the player (on foot within 5 s before, within 10 m of Shepard, 2 m of his
   height, on a floor) is put back (ME1's `PutBackSquad`; not after a Cut, the player is at the mark then); then each walks to its
   own side of the formation (`cw::SquadFormation`, `cw::AssignSpots`), facing the owner, put there at once under a Cut's dip.
   **No HOLD write on the sequels:** ME1 keeps its walkers there with the squad's own HOLD formation written into
   `BioBaseSquad.Members` (`vHoldPosition`, `eFormation`); ME2's member data (`FMemberData`) has `vHoldPosition` but no formation
   field, and its hold is an AI order the henchman answers aloud (`HenchmanOrderHoldPosition`, research/54 "ME2 placement");
   after the walk the AI's enable counters are back as the conversation had them, so the squadmate stands where it arrived
   ([verify] per scene: a squadmate whose follow logic runs in a conversation would drift back).
5. **The keep-in-place** (`OnUpdateCamera`) leaves our walkers' steps alone (`CircleWalking`), and a place we set by
   `SetLocation` (the put back, a Cut's placement) is the kept place (`NoteMovedByUs`).
6. **Switches:** `[Cinematics] worldCircle`, `worldCircleGapM`, `worldCircleNearM`, `worldCircleMaxWalkM`, `worldCircleStaggerS`,
   `worldCircleInRangeM`, `worldCircleOwnerStays`, `worldCircleLookahead`, `worldSquadGather`, `worldSquadGatherSideM`,
   `worldSquadGatherBackM` (ME1's keys and defaults); `mevr convo circle on|off|auto`, `mevr convo squad on|off|auto`.
7. **Records and rules:** the stance records carry each person's `walk` (`phase`, `to`, `kind`: circle or squad), `walks` (whether
   the AI could walk it) and the `circle` (centre, radius, places, members), so `tools/conversation-stance.py` draws them.
   `tools/scene_audit.py`'s new rule **`owner-walked`** (severity 2): the conversation's owner 1.5 m or more from its first place
   by a walk of ours (a walk's `kind`; ME1's records carry no kind, so ME1's set is never judged by it). **Broken runs:** `mevr
   convo circle ownerwalks on` (the owner walks 2 m to its side) must end with `owner-walked`; `mevr convo squad behind on` (the
   formation's places straight behind the player) with the existing `squad-behind`. Logs `conversation world: circle:` and
   `conversation world: squad:` (each join, walk, arrival, stall, refusal), the end line `conversation world: circle: conversation
   N (...): N joined, N walked ...`.

### 10.2 The subtitle under the speaker and the wheel in front (item 2)

ME1's placement (research/59): the mod publishes ME1's state fields in a held conversation (`game/SequelConversationUi.inc`,
`FillConversationUi`): `wheel` (the wheel's handler's `m_bDisplayingWheel`), `wheelHand` (`[Cinematics] wheelHand`, front by
default), `wheelWidthM` 0.40, `wheelDistanceM` 0.12, `subtitleHeightDeg` 4, and `subtitle` with `subtitleAt` (the speaker's head
bone, Shepard's own lines under the one he talks to, `subtitleBelowM` 0.22 under it and `subtitleTowardM` 0.15 toward the eyes:
`cw::SubtitlePoint`), the crops from `subtitleCrop`/`wheelCrop`. The host's new **`plugin/ConversationUi`** (game-independent;
the sequels' overlays call it before the HUD panel is drawn) blits each band of this frame's UI onto its own quad and cuts it out
of the panel: the subtitle at `subtitleAt`, facing the eyes, the same angular height at any distance (`vr::interact::Place`, the
interaction label's way), on a swapchain of its own (`convosub` in snapshots); the wheel placed where it opens
(`wheel_panel::Place`: in front of the head, 0.45 m out and 0.35 m down, or above a hand), on `plugin/WheelPanel`'s swapchain
(LE1's sharing: no wheel of the game's is up in a conversation), at most 2048 px wide. The rest of the conversation movie stays on
the panel fixed in the room (the owner's ME3 request of 2026-11-19). Not while Thespian mode hides the game's wheel.

**The bands are ME1's.** The conversation movie draws its 1280x720 stage across the frame's width, centred top to bottom, on
both sequels as on ME1, so `cw::DefaultSubtitleCrop` (stage y 480..586) and `DefaultWheelCrop` (586..712) fit (measured on the
HUD snapshots of lane FPC4: ME2's 16:9 frame, Samara's line at rows 0.771-0.798 and the wheel at 0.822-0.938, inside the bands
0.667-0.814 and 0.814-0.989; ME3's 3072x2216 frame, the docking officer's line at 0.705-0.733 and a six-reply wheel at
0.755-0.881, inside 0.630-0.745 and 0.745-0.881: its bottom row ends at the band's edge). LE1's `HalcyonOverlays` has its own copy
of these steps; moving it onto `plugin/ConversationUi` is an ME1 host change for a batch with ME1's regression set.

### 10.3 The floor marker's Waits source (item 3)

The core's **`cw::WaitTrigger` / `cw::Armed` / `cw::WaitingTrigger`** (unit-tested, `TestWaitingTrigger`): a trigger is armed when
its touch event is enabled and one-shot with a touch left (`MaxTriggerCount` > 0 and `TriggerCount` under it; an unlimited one is a
door's or an ambient line's, never a scene's wait); the scene waits there when it is armed, contains where the scene wants
Shepard, and neither his pawn nor the player's feet stand in it. **`game/SceneWaits`** (every game, `ue3` handles): the level's
`Engine.SeqEvent_Touch` events (its games' subclasses too) whose `Originator` stands within 30 m of the wanted place, gathered at
most every 3 s (a walk over the object table), judged at most every 0.25 s with `Actor.PointCheckComponent` (ME1's trigger stop's
own check) and the pawn's cylinder as the box; log `scene waits: the script waits on <trigger> ...`.
- **The sequels:** in a held conversation, from where the scene wants Shepard: the stage's placements and track moves withheld
  (`gC.sceneWants`), else the opening's mark when the player's place is elsewhere (a Stay or WalkIn: `gC.mark`). The pin moves with
  the stick, so a player who walks into the trigger fires its touch with Shepard's pawn.
- **ME1** (its own commit): in a moment, from where the script wants Shepard (`gMoment.wanted`); his pawn stands there (the
  script's Shepard), so only a trigger nobody touches is found. ME1's conversations never wait by construction (a mark inside a
  trigger is a walk-in by the view, his pawn on the mark touches it) and get no Waits source.
- **ME1's black-move source** (its own commit): in a moment, under the game's fade to black in a curated walk (the walk with
  Anderson to the Council), where the player walks on to him: his place, until the player stands in it or the view joins him.

### 10.4 ME1's end facing (item 4)

ME1's `End` recorded the held facing for the next scene (`gFoot.rotation`) but never turned the controller or the pawn, so the
first frame on foot took the controller's yaw: after a Cut opening facing the owner (`FinishOpening`) or the right stick's turns
the view turned at once by their difference. The rule is now the core's **`cw::FacingOnFoot`** (unit-tested,
`TestFacingOnFoot`), used by ME1's `End` (commit of its own) and the sequels' `FaceOnFoot`; log `convo: conversation N ends: the
held facing X deg, the controller's Y, the pawn's Z` (ME1), `mevr convo endfacing off` the broken run.

### 10.5 ME2 in the game (fake headset, 2026-11-20 11:47-12:13)

Stretch `20261120-114652-676-LE2-fpc5` (`prep.ps1 -Holder FPC5`; restored with 0 differences). Tali's reintroduction as in section
6.2 (`load.ps1 -Save VR_31_Soldier_141126_336072f\Save_0004.pcsav`, `conversation-world.ps1 -Before 'god','mevr instances
SFXGame.SFXPawn_PlayerSoldier','mevr setloc 0 -9600 16600 -12060','sleep:12','mevr setloc 0 -9600 15250 -12060','sleep:3' -Start
'fire:Engine.SequenceAction:Tali_Conversation.BioSeqAct_CancelHint_0' -Stances -Hands chest -Pick 1`), Samara's starter (`load.ps1
-Save John_12_Soldier_190924_4ee650d\Save_0036.pcsav`, `-Start fire:SFXGame.SFXSeqAct_StartConversation:Dialog_Samara.
SFXSeqAct_StartConversation_0 -Pitch -25`):

1. **The circle** formed round the player toward Tali at 0.4 s (`5 places, 1.8 m across`); Prazza (2.9 m), Miranda (1.1 m) and Jacob
   (1.3 m) were all within the 3.5 m talking range and joined where they stood: nobody needed a walk in this scene (no ME2
   conversation with a speaker beyond 3.5 m was reached). The broken run's owner walk went through `game/AiWalk` and arrived 0.30 m
   from its place: the AI walk works in the conversation mode.
2. **The squad**, three faults found and fixed in order (commits ecf9f97c, debc8dc2): ME2's roster is empty in the conversation mode
   (`squad: ready (game mode 5); 0 squadmate(s)`: it reads the squad from the player's pawn on foot only), so the squad as it stood
   on foot stands in; the cutscene's tracks had moved both squadmates within the last second, so the track check is now 0.3 s and the
   formation starts a second after the opening; ME2's `MoveToGoal` went to the navigation point nearest each place, 1.4-1.8 m off it
   (Jacob ended 2.9 m away behind the player), so each walker now walks to a goal actor of ours at its place
   (`ai_walk::StartToGoal`). Then: `Miranda walks 1.6 m ... after 0.9 s`, `Jacob walks 1.2 m ... after 1.5 s`, both `arrived 0.30
   m from its place` 1.7-2.0 s later (session folder `convo-clean-120823`, its sheet and `findings.txt`).
3. **The audit:** the clean run has no owner-walked, squad-behind or speaker-behind (before the formation: Miranda speaker-behind 146
   degrees, Jacob squad-behind 161); its two remaining findings are face-off-you for the squadmates (44 degrees: their bodies face the
   owner, ME1's formation facing, and the game's look at the player turns their heads only so far). **The broken run** (`mevr convo
   circle ownerwalks on`, `mevr convo squad behind on`, `convo-canary2-120949`): owner-walked (Tali 1.7 m) and squad-behind (Jacob
   161, Miranda 175), both caught.
4. **The subtitle and the wheel:** `[MEVR-CONVO] the dialogue wheel 0.12 m from in front of the head, at (0.00, -0.25, -0.51) m, 0.70 m
   wide; the UI's part (340, 1992) 3672x429 of 4352x2448 (the default band)`, `[MEVR-CONVO] the subtitle at the speaker: ... (4.0 deg
   tall); the UI's part (272, 1632) 3808x361`; snapshot `2026-11-20_12-01-20-014-left.png` (Tali's wheel in front, its three replies
   whole) and `2026-11-20_12-12-12-282-left.png` (Samara's line under her head, the wheel in front). The first run showed the wheel
   on its quad **and** on the panel: a screen's UI was drawn unmasked (`hud_panel::Frame::screenMasks` since). The VR_31 career has
   the game's subtitles off (no line in any band); John_12 has them on. The wheel in front sits 26 degrees under a level gaze: out of
   the fake headset's 16:9 eye (18 degrees down), in a real headset's view.
5. **The marker's Waits source** ran in every held conversation: no trigger found in Tali's or Samara's (nothing waits there; no
   marker in an ordinary conversation). No fixture whose scene waits for the player was reached on any game.
6. **The end facing** (FPC4's rule, now through `cw::FacingOnFoot`): `conversation 5 ends: the held facing -14.0 deg, the
   controller's 68.7, the pawn's -1.4: both turned`.
7. **Moments this stretch:** ME2 6 held (Tali's reintroduction, once per load), 0 to the 3D screen.

### 10.6 ME1's regression set with the end facing and the marker's sources (item 5)

`tools/conversation-audit.ps1` (session `2026-11-20_12-14-06-audit`, the players' build at debc8dc2 with main 715dbd81 merged):
16 scenes, every body measured where a wrist was in view.
- **The end facing:** ME1 showed the jump in 5 of 13 conversations (`convo: conversation N ends: the held facing ..., the
  controller's ...`: Opold -146.0 against 56.2, Avina -21.1 against 180.0, the Spectre induction -141.2 against 0.0, the hearing
  -155.0 against 180.0, Wrex at Fist's -177.9 against 171.7); both are turned to the held facing now.
- **The black-move marker:** in the walk with Anderson to the Council `moment: the script moved Shepard under the game's fade to
  black in a curated walk ...`, `convo: marker: moment: under the game's fade to black the script put Shepard 3.00 m from the
  player in a curated walk: a marker there`; the walk's end marker as before (8.40 m, 24.04 m in the Spectre walk).
- **Against the baseline:** lav70's faces median 13 -> 23 degrees (the baseline's, as lanes EXIT and FPC3 found); Girard "looks at
  his body 0 -> 1" (the `begin` record at 0.0 s, Shepard's body 0.53 m from the eyes before the opening took its place: no look
  after it); a vanished-in-view at the hearing (`sta90_council_saren_dlg`, and `sta90_captain_dlg`) that was the records naming
  BioPawn_7 "Ambassador Udina" between two records (`tools/scene_audit.py` keys people by their `id` since: both clean when the
  run is judged again). Opold's 0.03 s of the flat panel at 12:17:33.146 came 7 ms before the mod's `End` (12:17:33.153): the game's
  shop screen a frame before the handover word, not the changed code.
- **The canaries** (`conversation-audit.ps1 -Canaries`, sessions `2026-11-20_12-29-33-canary-*` to `12-52-01-canary-popped-out`):
  every broken run caught, exit 0 (body left at the mark, the empty chamber by `expect`, the right stick ignored, the floating view
  by eye-height and opening-off-mark, the dark walk by dark-view and eye-flat, the popped-out person by vanished-in-view, and the
  rest of the set's).

## 11. Lane FPC-ME3 (2026-11-20): the presence rules at the opening, the cab-ride hang, ME3's checks

ME3's checks of lanes FPC to FPC4's work, and the owner's new presence rule. Numbers only from the code and the logs.

### 11.1 Everyone but Shepard hidden: the 3D screen (item 0, the owner's rule (a), 2026-11-20)

**The owner's decision** ("Yes, by rule (a)"): a conversation where every speaker except Shepard is hidden at the opening (ME3's
Khalisah interview `citprs_khalisah2_d_dlg`, the video calls) goes to the 3D screen at its start; a run-time rule in the spirit
of ME1's presence rules, not a hand list, not first person.

**The rule** (`core/vr/ConversationWorld`: `cw::PresenceFacts`, `cw::PresenceAtOpening`, `cw::Name(Presence)`; unit-tested in
`tests/ConversationWorldTests.cpp`): the facts read from the live scene once the stage has placed everyone decide, in ME1's order,

1. `ShepardHidden`: his pawn hidden by the script (ME1's first rule);
2. `ShepardAbsent`: farther than `worldAbsentM` (15 m) from the conversation's owner and no line between them (ME1's second);
3. `OthersHidden` (new): the conversation's people but Shepard (the owner, the speakers, the listener, ME3's found actors and
   extra participants: the reader's `Speakers`, persons only) found, and every one of them hidden (the actor's `bHidden` or its
   body's `HiddenGame`). Nobody found but Shepard (a terminal, a hologram that isn't a person) keeps its place.

**Where it acts:** `game/SequelConversationWorld`, at the end of the opening's settle (0.35 s, when the place would be taken), on
ME2 and ME3 alike, under ME1's keys: `[Cinematics] worldAutoScreen` (`on`, the default: the 3D screen; `log`: logged only, held in
the world; `off`), `worldAbsentM`. The verdict is logged at every opening (`conversation world: conversation N: the presence rules
at the opening: a place for the player (its people: SFXStuntActor_0 shown)`, or `... no place for the player in this scene
(everyone of the scene but Shepard is hidden: the presence rules at the opening; its people: ...; the owner 1.8 m away, in
sight): on the 3D screen (worldAutoScreen = on)`); `mevr convo world` counts them (`N conversations with no place for the player
on the 3D screen (M by the presence rules at the opening)`). A conversation held by hand (`mevr convo world on`) only logs the
verdict and stays held.

**ME1:** its own presence rules (`game/ConversationWorld`'s `Begin`) are the first two only; they don't cover the new case (the
Khalisah shape: the speaker 1.8 m from Shepard, hidden, Shepard shown). ME1 is not changed (its compiled code: `game/
ConversationWorld` 19,796 sections, 0 changed; `core/vr/ConversationWorld` 0 changed, the new function's 6 sections new).
Moving ME1's two rules onto `cw::PresenceAtOpening` is a no-op refactor left for an ME1 batch; adding the third to ME1 is the
owner's call.

**The docking officer's comm call** (`citprs_citadel_docking_v_dlg`, Save_0090's opening conversation, held by hand): its owner is
a point of interest (no person), its listener the officer, a stunt actor shown and drawn (`SFXStuntActor_0 hidden=0`, drawn 0.0 s
ago, 3.7 m away in its void): **the rule does not catch it** (a place for the player). It stays on the 3D screen only because the
game opens it around a load (the load rule). A comm call staged as a person in a void is not "everyone hidden".

### 11.2 The cab ride's hang: no scale set while the world isn't drawn

**What happened** (fake headset, 2026-11-20 11:15-11:28, stretch `<session>-LE3-fpcme3`): Save_0090 loads into the docking
officer's wheel (the rapid transit: Presidium Commons, Refugee Camp, Embassies, Purgatory, Hospital). Held in the world by hand
(`mevr convo world on`, twice: the first set the override, the second held the conversation the module had begun), the module
switched the engine's depth of field off at the opening (11:20:55): the slice's rule "only for a conversation begun after 3 s on
foot" assumed such a conversation is never placed. `mevr convo pick 2` (Embassies, wheel location 5); the officer's lines played,
the conversation ended at 11:22:14 in game mode 0; game/SystemSettings' menu-return `scale set DynamicShadows ...
LightEnvironmentShadows` at 11:22:14.7; the module's `depth of field on again (after 3 s on foot)` at 11:22:17.6. **ME3's cab ride
loads in game mode 0**, GWorld unchanged: both fell into the load, which never finished (the Citadel loading picture, snapshot
`2026-11-20_11-24-29-776`, `render levers: scene renders per frame 0` and `objects=135284` for six minutes). The same hang as the
slice's at the Citadel's arrival (2026-11-19 20:56, commit `950abe3f`). FPC4 and EXIT3 took the same ride without holding the
conversation (no depth of field switch) and it finished.

**The fix** (gotchas/me2-scale-set-during-load, its "Fix in place"):

1. `game/WorldDrawn` (`world_drawn::Tick` once a game tick from `Mod.cpp`, the sequels' for now; `world_drawn::Now(withinMs =
   500)`): the world drawn just now, the player's body drawn within the last 0.5 s of wall time (its mesh's
   `PrimitiveComponent.LastRenderTime` moving; a loading screen draws no scene and the world's clock may stand still).
2. Every `scale set` the mod sends on the sequels waits for it, retried, never dropped: `game/SystemSettings`' `Poll` (a new
   level, a return to gameplay, the shadows, an edit, the first apply; `#if` LE2/LE3, tests `mevr-le2-system-settings`,
   `mevr-le3-system-settings`); `game/DepthOfFieldHold` (`Off`/`Back` say what is wanted, `Step` sends it each tick: the sequels'
   conversations and ME2's galaxy projector flow). Logs `system settings: a scale set waits: the world isn't drawn just now`,
   `conversation world: the depth of field back on waits: the world isn't drawn just now (...)`.
3. A conversation opened around a load and held by hand never switches the depth of field.

**Check:** hold the docking officer's conversation by hand, pick a ride: the ride's load finishes (`mevr state` at the
destination, `tools/game-watch.ps1 -UntilWorld` exit 0), the log has the waits and the switch back after the load.

**The scale set was not the whole cause** (the stretches `...-LE3-fpcme3c` and `-fpcme3d`, 2026-11-20 13:05-13:18, the gate
built): with the docking officer's conversation held by hand the cab ride hung again although nothing was sent during the load
(`system settings: a scale set waits: the world isn't drawn just now` at the conversation's end); the same with the end's turn off
(`mevr convo endfacing off`), and the source hooks had no call at all (`placement 0 calls`). With the world mode off (`mevr convo
world off`) the same pick loaded in about 15 s (`-fpcme3d` 13:14, `-fpcme3e` 13:22; `tools/game-watch.ps1`/the lane's watch: drawn
on foot at the embassies). So **holding a conversation the game opened around a load stops the load that follows**: the ME3 slice
at the Citadel's arrival (2026-11-19), the cab ride twice (2026-11-20). The mechanism is [verify] (Save_0090 loads into this
conversation while the hub may still stream; the held main view is the suspect, as at the arrival). Players never hold such a
conversation (the load rule leaves it on the 3D screen); `mevr convo world on` now refuses it with a line (`conversation N not held
by hand: the game opened it around a load ...`) unless `mevr convo world on force`. Not reached: a conversation begun on foot that
ends into a cab ride (the docking start isn't loaded at the embassies), the player's own path; the rapid transit after arrival is
a GUI screen, not a conversation [verify].

**Khalisah's interview** (`-fpcme3e`, 13:22, fired from foot 9 s after the ride): `conversation 1: no place for the player in this
scene (everyone of the scene but Shepard is hidden: the presence rules at the opening; its people: SFXStuntActor_4 hidden; the owner
1.8 m away, in sight): on the 3D screen (worldAutoScreen = on)`; snapshot `2026-11-20_13-22-41-001-left.png` shows the 3D screen
(the director's shot of Shepard against a door). Before it, 0.35 s of the view on foot while the opening settled.

**Unexplained (2026-11-20 11:44:11):** the next stretch's game (`20261120-114240-*-LE3-fpcme3b`, Save_0090 loaded into the
docking officer's wheel on the 3D screen, mode 7) vanished while `load.ps1` polled it: both logs stop mid-stream, no crash report,
nothing in Windows' Application log, SteamVR's `vrserver.txt` only "exited after 58.87 seconds"; nothing of the coordinator's
stopped it. Not seen again yet.

### 11.3 Moments: the game's black takes the view with him; the seat in a moment

ME1's moment rules the sequels lacked (section 7.6 item 3): in a moment, a jump of Shepard's pawn past 30 units in a tick (ME1's
`kJumpUnits`) while the camera's fade is black or was within 1.5 s (`gBlackMs`, read in each camera update) takes the view with him
whatever the distance, at the same place relative to him, turned to his facing there, under a dip (`worldBlackMoves`; log
`conversation world: moment N: under the game's fade to black the script moved Shepard X m: the view goes with him`). ME2's
ANIMCUTSCENEs park his pawn at the world's origin: never followed there. Without the black, the view stays the player's and joins
him at rest (`worldMoments = view`). While the view is with him (his pawn shown, within 0.4 m), the view sits with him (core
`cw::StepSeat`, ME1's numbers, as in the conversations; the owner's "every sit-down in first person"). ME3's first fixture is
Purgatory's bar (`BioD_CitHub_WardsFlux`, `Main_Sequence.Drinking`: used at `SFXPointOfInterest_1`; `SeqAct_Interp_0` at the bar,
`SeqAct_Interp_2` walks him 1.5 m under a fade and a black-screen track, `SeqAct_Teleport_1` to a booth, `SeqAct_Interp_3` seats
him there, `SeqAct_Teleport_2` back).

### 11.4 The face axis

The records give each head bone axis's angle to where the game's look-at chain aims (`axesToAimDeg`; `python -I
scratch/.../faceaxis.py`: the median per axis over every record with an aim). **ME2: +X** (lane FPC4's Tali records: Tali +X 23,
Prazza 33, Jacob 19, Miranda 18 degrees, the other axes 64-92): ME1's `kFaceAxis 0` holds. **ME3: +X too** (the Prothean's
character moment below: Javik's and his double's heads `SFXStuntActor_1` 26, `SFXStuntActor_301` 26 degrees on +X, 92-116 on the
others; the hanar `SFXStuntActor_2` 74 on +X, a head with no face axis, ME1's `faceAxisUnknown`). The records' `toYouDeg` judges
heads on both sequels unchanged.

### 11.5 ME3 in the game (stretch `20261120-133029-875-LE3-fpcme3f`, main 60a1ed15 merged)

1. **A conversation whose lines walk Shepard** (item 1): the Prothean's character moment (`citprs_proth_char_moment_m_dlg`, from
   Save_0090's embassies: its remote event, the teleport, the start's input 1; `conversation-world.ps1 -Before ... -StartInput 1`):
   opening cut 11.3 m away, the eyes from his head bone as learned on foot (32.8 forward, 9.4 up), **2,220 of Shepard's track moves
   withheld at the source** in 35 s (ME1's `trackHeld`: the player's place is the player's), 7 people kept, 21 records from above,
   `0 findings` (session folder `convo-proth-133223`, its sheet). The owner's `citprs_miranda_talk1_m_dlg` (the docks) was not
   reached: the docking officer's ride to the Refugee Camp, the other routes and Purgatory's moments are the next stretch's.
2. **The wheel's picture** (FPC5's placement, asked by the coordinator): snapshot `2026-11-20_13-33-11-443-left.png`: the wheel in
   front, low in the centre, readable, Javik 2 m ahead at the window. **No subtitle in any of the six snapshots**, and the module
   still logs `wheel and subtitles fixed in the room 0.9 m ahead, 0.25 m below the eyes` on ME3: FPC5's subtitle under the speaker
   doesn't show there yet (reported to lane FPC5's owner; not changed here).
3. **Not reached on ME3:** Joker seated (`citprs_joker_talk1_d_dlg`, Presidium Commons: the ride and its start), a scene that
   seats Shepard (the hospital's Ashley/Kaidan, Purgatory's Aria), a level cutscene that walks him (Purgatory's bar, section 11.3),
   rides, the black move, the handover, the end facing on the Prothean fixture (the runner turned the world mode off before its
   end). Moments this lane: ME3 0 held, 0 to the screen (none reached); ME2 none run.

## 12. Lane FPC6 (2026-11-20): ME3's subtitles, ME3's fixtures by ride, the first circle walk-up

What was found and changed on both sequels, how to check it. Numbers only from the code and the logs. ME3 stretch
`20261120-135521-550-LE3-fpc6-le3` (fake headset, `prep.ps1 -Game LE3 -RendererPreview`, Save_0090; relaunched once for a host
change; restored with 0 differences), ME2 stretch `20261120-144049-857-LE2-fpc6-le2` (restored with 0 differences).

### 12.1 ME3's subtitle under the speaker (item 1)

1. **Not a fault of the placement: the career's subtitle option is off.** `mevr convo subtitles` (ME3, new) prints the game's own
   subtitle list (`BioWorldInfo.m_pSubtitles`, a `SFXGame.BioSubtitles`: its entries with their render mode, 0 none, 1 default,
   2 top, 3 bottom, 4 above the wheel; `m_bSubtitleVisible`), the controller's `m_bHideCurrentSubtitle` and the player's option
   (`SFXProfileSettings.GetSubtitleConfigOption`, the profile's setting 30: 0 on, 1 off). Save_0090's career: `the option off`.
   With the option off ME3 keeps only the last line above the wheel (mode 4) while the wheel is up; during spoken lines its list
   is empty, on the 3D screen as in first person (the refugee leader's conversation with the world mode off: 24 s of lines, 0
   entries; snapshot `2026-11-20_14-18-06-437-left.png`, no subtitle on the screen). So lane FPC-ME3's six blank snapshots were
   lines with nothing to show. In a held conversation the probe logs each change: `conversation world: the game's subtitles:
   the option on, visible 1, ... 1 entries [mode 4, 126 chars, running]`.
2. **With the option on** (`mevr convo subtitles on`, a test aid: the options screen's own `SetProfileSettingValueId(30, 0)` on
   `Engine.OnlinePlayerStorage`, then `BioPlayerController.UpdateLocalProfileSettingsCache`; the runner's restore puts the profile
   back) every spoken line reached the list (mode 4 on ME3 for all of them) and the quad under the speaker: Avina's line at her
   hologram (`2026-11-20_14-10-31-336-left.png`), Shepard's line under the refugee leader he talks to
   (`2026-11-20_14-23-22-244-left.png`).
3. **Fixed: the band's width.** A 126-character line wraps wider than ME1's stage x 80..1200 on ME3's frame: its first and last
   letters ("She", "your") stayed on the panel (the snapshot's `hud.png`, rows 0.671-0.696 at both sides). `plugin/ConversationUi`
   (the sequels' hosts only) now takes ME1's rows across the frame's whole width.
4. **Check:** a held ME3 conversation with the option on: `convosub.png` holds the whole line, `hud.png` has nothing at those rows.

### 12.2 ME3's fixtures from Save_0090 (item 2)

**The ride:** Save_0090 loads into the docking officer's wheel (world mode off). Its positions: 0 Presidium Commons, 1 Refugee Camp,
2 Embassies, 3 Purgatory, 4 Hospital, 5 No thanks (`mevr convo pick <position>`; wheel locations 0 middle right, clockwise to 5 top
right). After each pick `tools\game-watch.ps1 -UntilWorld` (all exit 0, 15-25 s). **ME3's starts here take input 0**
(`conversation-world.ps1 -StartInput 0`): input 1 started nothing at the Refugee Camp. The pawn for `mevr setloc` is
`mevr instances SFXGame.SFXPawn_Player TheWorld` (without the filter index 0 is an archetype, and setloc moves nothing visible).

1. **Not loaded in this career's plot:** Miranda's `citprs_miranda_talk1_m_dlg` (its start is in `BioD_CitHub_DockP2`; the Refugee
   Camp loads `BioD_CitHub_Underbelly`/`UnderbellyP3`), Joker's `citprs_joker_talk1_d_dlg` (`PresidiumP2`; the Presidium loads
   `Presidium`/`PresidiumP3`), Aria's `citwrd_rp1_aria_*` at Purgatory. `cat004_hangar_shuttle_m_dlg` needs the Cronos route.
2. **Refugee Camp:** Avina (`Avina.SFXSeqAct_StartConversation_1`) and the refugee leader (`Refugee_Leader.SFXSeqAct_StartConversation_0`,
   `citund_refugee_leader_d_dlg`, a person walks 6.5 m in its lines) held in first person; Kelly, Conrad and the others' starts
   began nothing in this plot. **Found and fixed: a Cut opening took the eyes from his head bone before his mesh followed the
   stage's teleport** (conversation 5, 30 m teleport: `the eyes from his head bone (-399, -91, 81 from his pawn)`, the body 4 m off
   the eyes all scene, `eyes-off-body` 6 records): a Cut now waits up to 1.5 s more for his head bone to stand within 80 units
   (flat) of his pawn (log `his head bone stands N units from his pawn ... the opening waits for it`), then takes his head over his
   pawn. Two later Cut openings (53 m and 44 m teleports at the Presidium) took the eyes 8-12 units from the pawn, no wait needed.
   **Found and fixed: the records listed ME3's stage and cameras** (a `CameraActor` judged `too-close`): people only now.
3. **Purgatory's `Drinking` sequence:** fired at `Drinking.SeqAct_Teleport_0` (`conversation-world.ps1 -Start
   fire:Engine.SequenceOp:Drinking.SeqAct_Teleport_0 -StartInput 0`; the `SeqEvent_Used` at `SFXPointOfInterest_1` leads there):
   each fire is one drink (`SeqAct_Interp_0`, 10.8 s), held as a moment (`a matinee's move track walks Shepard 0.0 s into the
   cutscene: held in first person`), the view joins at the end. Fired from the entrance the teleport put Shepard 83 m away at the bar
   and the view stayed (moment 2; a player uses it at the bar). At the bar (moments 3-5): **his animated head leans 1.05 m forward
   onto the bar while his pawn stays under the player's view, so the player sees Shepard's back and head in front of them**
   (snapshots `2026-11-20_14-30-31-320-left.png`, `14-31-23-668-left.png`; records `bodyFromEyesM` 0.81-1.12; the seat rule doesn't
   see it: his head drops 0.15-0.18 m). ME1's moment hides his pawn only by the pawn's distance (60 units), and follows his head only
   when seated: owner question (report). The fourth drink went on foot with the game's drunk filter (no Aria scene in this plot).
4. **Presidium:** Oraka (`Aria2.SFXSeqAct_StartConversation_1`, `citwrd_aria2_oraka_d_dlg`) and Sommers
   (`SEQ_Vengence_Sommers.SFXSeqAct_StartConversation_0`, Shepard walks 2.6 m in its lines) held: Cut openings, the stage's
   placements of the others withheld at the source (4), 0 findings and one `face-off-you` (41 degrees).
5. **The end facing on ME3** (FPC4's rule, through `cw::FacingOnFoot`): `conversation 2 ends: the held facing 24.9 deg, the
   controller's -171.3, the pawn's -0.0: both turned` (Sommers), the same at the refugee leader's end.
6. **Not seen on ME3:** seats (none of the reachable scenes seats Shepard by the seat rule), rides, the game's black moving the
   player, the handover into a GUI screen.
7. **Moments this lane:** ME3 5 held (Purgatory's drinks), 0 to the 3D screen; ME2 0 (no cutscene run).

### 12.3 A circle walk-up seen in the game (item 3)

ME2, VR_31's Save_0004 at Freedom's Progress, Tali's room streamed in as section 6.2 does, the player 8 m from Tali, then her
conversation's own start fired without its cutscene (`conversation-world.ps1 -Start
fire:SFXGame.SFXSeqAct_StartConversation:Tali_Conversation.SFXSeqAct_StartConversation_0`): opening walk in, the circle formed round
the player toward Tali (5.4 m, the owner stays), **`BioPawn_1 speaks and walks 5.4 m into the circle`**. ME2's `MoveToGoal` cleared
its goal 1.45 m short after 3.97 m and the walk was logged as stalled, the speaker left facing his walk: such a walk (within 1.6 m,
at least 1 m walked and at least as far as is left) now counts as arrived near its place and turns to the circle (`... walk ended
1.45 m from its place after 3.97 m (the AI's own stop): near enough, it turns to 118 deg`). Snapshot
`2026-11-20_14-47-00-053-left.png` (Prazza in front of the player after his walk, Tali beyond). The squad walked as before (Miranda
arrived 0.67 m off; Jacob's walk refused by the AI at once, 4.6 m from his place, `squad-behind` 173 degrees: his place had no path;
[verify] in a scene begun as play begins it). ME3's hub people are stunt actors with no AI: no walk-up there (section 9.3).

### 12.4 The presence rule on ME2 in the game (item 4): no fixture reached

None of the 23 starts loaded on the Normandy in John_12's Save_0036 (decks 3 and the henchmen's rooms) is in the generated table
(`scratch` match of the loaded starts against `src/core/vr/ConversationPresence.inc`); the table's Normandy entries are on other
decks (`nor_ai_tour_cargo_d` deck 4, `nor_yeoman_a` the CIC), which the elevator's GUI streams in, and Freedom's Progress's
(`profre_cover_tutorial_a`, `profre_hard_shield_tut_a`) have no start action. Not checked in the game; the next lane needs a deck
change by script (the elevator's Kismet) or a save on the CIC deck.

### 12.5 A scene that waits for the player (item 5)

No scan of the triggers a start's script waits on exists yet (`game/SceneWaits` judges at run time): no fixture found this lane, on
any game. The run-time source logged no wait in any held scene here (Avina, the refugee leader, Oraka, Sommers, Tali).

## 13. Lane FPC7 (2026-11-20): the player's head drives Shepard ("Your head drives him")

The owner's decision of 2026-11-20 for section 12.2 item 3 (Purgatory's drink): of (a) the view follows his head (a 1 m drift:
rejected for comfort), (b) his body hidden while his head is more than 0.6 m from the eyes and (d) **the player's head drives him,
as on foot**, the owner chose (d) with (b) as the fallback. One rule for the three games. ME3 stretch
`20261120-163450-338-LE3-fpc7` (fake headset, `prep.ps1 -Game LE3 -RendererPreview`, Save_0090, restored with 0 differences).

### 13.1 What happened to Shepard's bones in the drink before (item 1)

1. **On the sequels arm IK never ran in a moment.** `game/ArmIk`'s `OnFoot()` admits game mode 0, a wrist screen or
   `conversation_world::InWorld()`; on the sequels `InWorld()` is `sequel_conversation_world::InWorld()`, a held *conversation*
   (`gC.on && gC.placed`). A moment is ME3's cinematic mode (8): `Stop("not on foot")`, the body the matinee's. ME3's drink
   (`Main_Sequence.Drinking`, `SeqAct_Interp_0`) moves the body bones about 1 m forward onto the bar while the pawn stays: the
   drawn head 0.81-1.12 m from the eyes (records), the player saw his back.
2. **Where arm IK ran, the distance test dropped it.** `hand_targets::Current` keeps the view's frame (first person) only while the
   view is within 45 units of his animated head (75 to leave; `ik::FirstPersonAnchor`), unless the scene says the body is away
   (`BodyAway().on`). `ArmIk.cpp` and `BodyFollow.cpp` read `BodyAway()` only on LE1 (`#if defined(MEVR_GAME_LE1)`), so on the
   sequels a held conversation whose animation took his head 0.75 m off fell to the third person: the body the animation's there
   too. On ME1 a moment with the view with him (`!gC.detached`) has `BodyAway().on` false (it is on only detached or in a
   conversation), the same fall.
3. **ME1's hide is by distance and off by default.** `HidePawn` returns at once with `worldBody = player` (the default); the
   sequels' moment hides his pawn only past 60 units between the pawn and the view's place (`gM.hid`), never for his bones.
4. **The arms.** ME1: arm IK runs in moments and conversations (the arms on the controllers). The sequels: no arm IK in a moment
   (the arms the matinee's); in a conversation the arms on the controllers.

### 13.2 The rule (item 2)

- **Core** `cw::StepHeadDrive` (`core/vr/ConversationWorld`): facts `scene`, `withHim`, `seated`, `dip`, `headFromEyesM` (his drawn
  head bone from the drawn eyes, flat), `dtS`. `drive` = scene, with him, not seated, not under a dip. Driven: past `hideAboveM`
  0.6 for `dwellS` 0.25 hides, under `showBelowM` 0.4 for 0.25 shows; NaN holds. Out of the scene, apart or seated a hide is let go
  at once (`Shown`); under a dip nothing changes. `ik::kHeadDriveBendDegrees` 80 (the torso's bend limit while driven; on foot
  35) and `ik::kHeadDriveTiltDegrees` 30 with the new `TorsoGoal::maxTiltDegrees` (default 180: no limit, on foot unchanged): the
  lean's aim kept within 30 degrees of upright, so a body the capped slide (`bodyMoveMaxM`) leaves short leans toward the head,
  never lies back to reach it (with the bend alone the 2 m case lay back far enough to "absorb" it). Tests:
  `tests/HeadDriveTests.cpp` (`MEVR_HEAD_DRIVE_REPORT=1` prints): LE1's Shepard skeleton, the player standing, the animation
  leaning him; a 1 m lean (the body 0.37 m forward, the spine 88 degrees) 0.98 -> 0.08 m, never hidden (with the on-foot 35
  degrees 0.31 m); the drink's shape 1.17 -> 0.18 m; a 2 m lean 2.22 -> 0.79 m, hidden after 0.24 s, once; seated: no drive,
  no hide; the rule's hysteresis, dip, NaN and release.
- **Arm IK, every game:** `conversation_world::BodyAwayState` gains `headDrives` and `torsoOnly`. `OnFoot()` admits a head-driven
  scene; `hand_targets::Current(..., bodyAway || headDrives, ...)` keeps the view's frame; `game/BodyFollow` sets the bend and the
  tilt. The slide keeps its on-foot limit, or ME1's unlimited one where `BodyAway().on`.
- **The arms per game:** ME1 keeps them on the controllers (as before in its moments and conversations). The sequels' moments
  (`torsoOnly`): the torso and the planted legs only, the arms reach for the animation's hand bones (`bone_pose::AnimatedHands`)
  carried by the torso's last slide and lift, scaled as drawn, so the glass stays in his hand at the bar's height. Carried with
  the straightened chest instead (the first try), the hands rose into the air in front of the eyes. The sequels' conversations:
  the controllers, as before.
- **Each module steps it** where it holds the view: the sequels' `TickMoment` (with him: his pawn shown, within 0.4 m of the
  player's place) and the conversation tick (his pawn at the pin), ME1's `TickMoment` and conversation tick (not detached). The
  dip: 0.4 s after the scene's cut counter changes. The hide: `Actor.SetHidden` only over a pawn the scene shows; hidden, his mesh is
  posed each tick by `SkeletalMeshComponent.ForceSkelUpdate` (a hidden mesh isn't posed, its head would never come back; ME1's
  walk-in noted it); on the sequels a hide the view leaves is handed to the moment's distance rule (`gM.hid`), no show between.
- **Logs:** `conversation world: moment N: the player's head drives Shepard's body (...)`, `... his body hidden for the player (the
  head drive's fallback)`, `... shown again (...)`, `... drove Shepard's body for X s (his drawn head at most Y m from the eyes),
  hidden N times`; ME1's with `convo:`. **Records:** `player.headDrive`, `player.bodyHiddenBy` (`"head-drive"`), on the sequels
  `player.armsScene`. **Audit** (`tools/scene_audit.py`): a body hidden by the rule is not `body-not-drawn`; `eyes-off-body` judges a
  moment's records too where they carry `headDrive` (the view with him; before, moments were left out); `hands-off-controllers` leaves
  the scene's arms (`armsScene`) alone. ME1's live audit skips a body the rule hid. **Console:** `mevr convo headdrive
  [on|off|auto]` on all three games; off is the broken run.

### 13.3 ME3 in the game: Purgatory's drink (item 2)

Save_0090, the docking officer's position 3 (`mevr convo pick 3`, `game-watch.ps1 -UntilWorld`), Shepard put at the bar (`mevr
instances SFXGame.SFXPawn_Player TheWorld`, `mevr setloc 0 -17549 -2078 -176700`), each drink `conversation-world.ps1 -Start
fire:Engine.SequenceOp:Drinking.SeqAct_Teleport_0 -StartInput 0 -Hands chest -Stances`. **The first fire after the ride starts
nothing** (game mode stays 0; the second plays); the fourth drink of a series walks him to a booth (the view joins him 18.9 m away,
not with him, no drive), the fifth is at the bar again.

1. **Broken run** (`-Before 'mevr convo headdrive off'`, `convo-drinkoff-164149`): his drawn head 0.08-1.05 m from the eyes; findings
   `eyes-off-body` (6 records, at most 1.05 m) and `hands-off-controllers`; snapshot `2026-11-20_16-42-03-982-left.png` (his back
   and head in front of the eyes, as FPC6's).
2. **The rule** (`convo-drinkon3-164517`): `the player's head drove Shepard's body for 10.8 s (his drawn head at most 0.18 m from the
   eyes), hidden 0 times`; records 0.10-0.16 m; **0 findings**; snapshot `2026-11-20_16-45-26-453-left.png`: first person over the
   bar, his hand with the glass at the bar's height, nobody in front of the eyes.
3. **A replayed lean** (`make-walk-recording.py --plan stand:4,lean:6,stand:600 --lean-drop 0 --lean-forward 0.3 --hands chest
   --ease 0.8`, the drink fired during it): the drawn eyes moved 30 units forward in the records (y -2142 -> -2112) and his drawn
   head 26 units with them (-2127 -> -2101), 0.12-0.17 m from the eyes throughout; snapshot `2026-11-20_16-46-26-805-left.png`.
4. **The fallback** (a drink without controllers: no arm IK, nothing to drive): `his drawn head stays 0.81 m from the player's eyes,
   past what his body can take: his body hidden`, shown again at the moment's end (8.2 s hidden).
5. **Moments this lane on ME3:** 6 held (5 drinks at the bar, 1 to the booth), 0 to the 3D screen.

### 13.4 ME2 and ME1 (items 3 and 4)

1. **ME2** (stretch `20261120-171302-891-LE2-fpc7-le2`, VR_31's Save_0004, Tali's room streamed in as section 6.2, her
   conversation's start fired, hands at the chest; restored with 0 differences): the rule drove Shepard in the held conversation, his
   drawn head 0.09-0.10 m from the eyes in every record (3.73 m at the first driven tick only, before arm IK's first write: the
   0.25 s dwell keeps that from hiding him), no hide; one finding, `squad-behind` (Miranda behind the player at the opening, as
   Jacob was in section 12.3). **No ME2 moment with his pawn under the view was reached:** ME2's ANIMCUTSCENEs park his pawn at the
   world's origin (section 6.2), so the view is never with him there and the rule never acts; a level matinee that animates his own
   pawn in place (as ME3's drink does) is unseen on ME2. Moments this lane on ME2: 0.
2. **ME1** (research/69 item 45): the regression set drove him in all 16 scenes, `eyes-off-body` and the body rules never tripped,
   one hide of 0.6 s in Liara's intro moment as the script's track carried him off; ME1 moments with the view with him: 2 driven
   (the Spectre walk, Liara's intro), at most 0.12 m once driven.
3. **ME1's compiled code:** with the lane's first commit only ArmIk, BonePose, Mod and TorsoPose differ from main among the top-level
   LE1 objects (`TorsoGoal`'s new field and the head drive's reads); the ME1 commit changes `ConversationWorld` alone (212 objects
   compared, non-debug sections).

### 13.5 Left

- The owner's look in the headset (notes/headset-checklist.md): the drink's arms reaching for the animation's glass, a lean over the
  bar, the fallback's vanish.
- ME2: a level matinee that animates his pawn in place; ME3: a conversation in the world with the rule (none run this lane: the
  drinks are moments).
- The first fire of `Drinking.SeqAct_Teleport_0` after the ride plays nothing (the runner fires twice).

## 5. How to check ME1 is unchanged

The LE1 objects of every translation unit this lane touched (Mod, ConsoleRequest, ReflectCheck, ModLoader, PadMerge,
MainMenuSettings, SequelConversationWorld) are compared with main's build, COFF sections without the debug ones
(`scratch/fpc/objcmp.py`, private). Result in the lane's report.

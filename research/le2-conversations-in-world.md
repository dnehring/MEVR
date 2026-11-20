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

## 5. How to check ME1 is unchanged

The LE1 objects of every translation unit this lane touched (Mod, ConsoleRequest, ReflectCheck, ModLoader, PadMerge,
MainMenuSettings, SequelConversationWorld) are compared with main's build, COFF sections without the debug ones
(`scratch/fpc/objcmp.py`, private). Result in the lane's report.

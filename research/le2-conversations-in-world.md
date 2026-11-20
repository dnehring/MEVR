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
  sequels' already; the controllers' right stick steers the dialogue wheel while held (`plugin/PadMerge`, the owner's ME3
  request kept on ME2).
- ME2's renderer (`src/halcyon/le2`, `[MEVR]`): `me2_xr.cpp`'s cinematic decision treats a held conversation like the
  galaxy map's held cinematic (no flat panel, no 3D screen, no VR cine; `mevrConvoWorld`), and publishes it as a world view
  (arm IK, the body); `calcview_hook.cpp` holds the main view at the player's place in modes 5 and 6 while the mod holds a
  conversation (`MevrSeams::WorldViewHoldBegin`); `engine_probe.cpp` keeps first person's hides then (`WorldViewHeld` 1).
- Our menu: Comfort > Conversations offers First person on ME2 and ME3 (it wrote nothing there before); `world` is the
  key's value, as ME1's.

## 4. What is left, in order (each item for ME2 and ME3 alike; say what a game lacks)

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
5. **The circle and the squad's formation** (`cw::MakeCircle`, `cw::SquadFormation`): shared rules already; the facts
   (who speaks within 4 lines: `m_EntryList`/`m_ReplyList` exist on ME2) from the reader.
6. **Subtitle and wheel placement** (ME1's subtitle under the speaker, the wheel at the hand): the sequels fix the
   conversation's whole movie on one room panel today (the owner's ME3 request: "not static ... follows my head").
7. **Moments**: Tali's re-intro (section 1 item 4) is ME1's moment (`worldMoments = view`); `AuthoredMoment`'s move-track
   hook on ME2 is `0x6B9C50`. Needs the owner's answer to question 2 before it becomes a default for 214 matinees.
8. **The rest of ME1's walking scenes**: walk-along (`worldWalkAlong`), the game's black moving the player
   (`worldBlackMoves`), rides (`RideBase`, `Actor.Base`), seats (`StepSeat`), eye height over his floor.

## 5. How to check ME1 is unchanged

The LE1 objects of every translation unit this lane touched (Mod, ConsoleRequest, ReflectCheck, ModLoader, PadMerge,
MainMenuSettings, SequelConversationWorld) are compared with main's build, COFF sections without the debug ones
(`scratch/fpc/objcmp.py`, private). Result in the lane's report.

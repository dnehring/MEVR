<!-- SPDX-License-Identifier: MIT -->
# 74. The hands census: scripted scenes where Shepard's hands act

The owner, 2026-11-20: "Scale some of these scripted things, like taking a drink at the bar at Purgatory, where we actually
just have to take the drink ourselves. There are probably quite a few of these opportunities throughout." The design proposed
with it, **not approved yet** (this census is what it is approved from): at the moment Shepard's authored animation would act
with his hands, the mod pauses the matinee (as the walk-along rule already pauses one), arm IK gives his arms to the
controllers, the prop becomes grabbable, a gesture of that kind is detected (glass to the mouth, a press, a take or a give, a
salute, a handshake), then the matinee resumes and runs to its end, so every effect the game has stays the game's. Scenes are
sorted into gesture kinds **by rule from their data, never per scene**; what someone else does to him (fights, being shoved,
kisses) stays authored.

Lane CEN1, 2026-11-20, offline (no game run). Everything below is read from the installed packages; nothing has been seen in
the game, so every statement about behaviour in play is **[verify]**.

**In one paragraph:** of 120,000 scripted scenes in the three games (level matinees and conversation lines, one copy each),
41,000 have Shepard in them; in **720** his hands do one of the six simple gestures: ME1 79, ME2 145, ME3 496 (drink 74, press
313, take 168, give 20, salute 25, handshake 120). 89% are conversation lines (ME3: 443 lines in 150 conversations); **79
are level matinees** (ME1 2, ME2 24, ME3 53, in 67 sequences), and **28 of those start from a use** the player makes (ME2 7,
ME3 21; ME1 none): Purgatory's drink, Kasumi's bar on ME2's Normandy, Tali's picture in ME3's cabin, ME3's elevator keypads,
the Citadel casino's machines. Purgatory's drink is the best candidate of all three games by the census's own score.

## 1. What was scanned, and how to run it again

| Game | Folders read | Packages | Scenes (one copy) | Level / lines | With Shepard (level) | Time |
|---|---|---|---|---|---|---|
| ME1 | `ME1\BioGame\CookedPCConsole` (ME1 LE has no DLC folders) | 2,247 | 22,538 | 5,911 / 16,627 | 6,299 (96) | 20 s quiet, 150 s beside a running game |
| ME2 | `ME2\BioGame\CookedPCConsole` and 29 `DLC\DLC_*\CookedPCConsole` (`DLC_00_Shared` to `DLC_UPD_Patch03`) | 3,218 | 30,002 | 4,373 / 25,629 | 13,467 (294) | 30 s |
| ME3 | `ME3\BioGame\CookedPCConsole` and 13 DLC folders with packages (`DLC_CON_APP01`, `_DH1`, `_END`, `_GUN01`, `_GUN02`, `DLC_EXP_Pack001`-`003` and `003_Base`, `DLC_HEN_PR`, `DLC_METR_Patch01`, `DLC_OnlinePassHidCE`, `DLC_UPD_Patch02`) | 4,456 | 67,620 | 14,273 / 53,347 | 21,480 (875) | 47-63 s |

The install is the Steam one in CLAUDE.md; read only, at below-normal priority, while ME3 ran for another lane. Skipped by
name: other languages' `_LOC_` packages (English `_LOC_INT` is read: conversation lines live there), `_SND`, `_ART`, `_LIGHT`,
`_T`, `Textures*`. Raw records: ME1 40,942 scenes, ME2 44,147, ME3 124,372; the copies dropped are DLC re-packages of a base
package (ME2 6,396, ME3 26,732; `DLC_METR_Patch01` carries a copy of `BioD_CitHub_WardsFlux`) and a conversation's lines in each
package that carries the conversation (ME1 18,404: its cab ride `sta00_cab` is in 12 Citadel packages; ME2 7,749; ME3 30,020).

```powershell
dotnet build tools/conversation-scan/ConversationScan.csproj -c Release -o <private-build> -p:LexDir=<lex> -p:BaseIntermediateOutputPath=<private-obj>
<private-build>\conversation-scan.exe "<Game>\ME3\BioGame" scratch\hands-census\le3.jsonl --hands-census --game LE3 --threads 4
python tools/hands-census.py summarize scratch\hands-census\le1.jsonl scratch\hands-census\le2.jsonl scratch\hands-census\le3.jsonl --out scratch\hands-census
python tools/hands-census.py show scratch\hands-census\scenes.jsonl WardsFlux       # one place's classified scenes
python tools/hands-census.py names scratch\hands-census\scenes.jsonl handshake      # the names that drove a kind
python -I tests/HandsCensusTests.py                                                  # ctest mevr-hands-census
```

The scanner's records are documented in `tools/conversation-scan/README.md` "Hands census"; the rule is in
`tools/hands-census.py`'s docstring. Output (game names and object paths, about 380 MB raw, 60 MB classified) stays in
`scratch/hands-census/` (gitignored): `scenes.jsonl` and `scenes.csv` (every scene with Shepard), `uses.jsonl`, `summary.json`.

## 2. Who is Shepard in a scene

A scene is a `SeqAct_Interp` (a level matinee, or a conversation line's: the sequels' and ME1's lines each have one under the
conversation's `Node_Data_Sequence`) with its `InterpData`, or an `InterpData` nothing plays. Its groups are bound to actors by
the `SeqAct_Interp`'s variable links (one per group name). Shepard is a track's actor when:

| How | Games | Strength |
|---|---|---|
| The group's link is `SeqVar_Player`; a `BioSeqVar_ObjectFindByTag` for `player`; a `SeqVar_Named` or a sub-sequence's `SeqVar_External` that resolves to one (followed out through the enclosing `Sequence`/`SequenceReference`, three levels) | all | explicit (high) |
| The group finds `Player` itself (`m_nmSFXFindActor`, ME3's groups) | ME3 | explicit |
| A conversation track's actor is `Player` (`m_nmFindActor`) | ME2, ME3 | explicit |
| **An untagged conversation track** (`sActorTag` empty): ME1 tags every actor's track (`Owner`, a speaker's tag) but the player's. Evidence: of the gesture tracks in Shepard's own lines (the replies, joined by the line matinee's `BioSeqEvt_ConvNode` node ID to the conversation's `nExportID`s), 7,994 are untagged against 2,062 tagged for others and 1,369 for the owner; in the owner's lines the owner's tracks are tagged (12,020) | ME1 | medium **[verify]** |
| Only the group's name (`PC_`, `HMM_PC_`, `HMF_FEMALEPC_`, `Player`, `Shep...`) | all | medium |
| A group named for him bound to a `SkeletalMeshActor`: a stunt double in a cutscene, not his pawn | ME1 1, ME2 1, ME3 8 | low |

ME1 conversations had no Shepard tracks at all until the untagged rule: without it ME1's lines would count no gesture. In ME1
**level** matinees he is bound 56 times by the player variable, 5 by tag, 60 by group name only (the cutscenes' `PC_` groups).

## 3. The classification rule

Every animation name played on Shepard (an `InterpTrackAnimControl` key's sequence, a `BioEvtSysTrackGesture` key's gesture,
transition or pose animation) gets a **word kind**, the first that matches (the regular expressions are `WORDS` in
`tools/hands-census.py`):

| Word kind | Words (case-insensitive) | Examples from the data |
|---|---|---|
| handshake | `ShakeHand`, `HandShake` (not "Denied") | `2P_ShakeHand`, `2P_HandShake`, `2P_Handshake_Bro_A`, `2P_AffectionateHandshake_Action` |
| salute | `Salut` | `DP_SaluteEnter/Idle/Exit`, `DL_Salute` |
| exchange | `Exchange` | `2P_ExchangeOne`, `2P_ExchangeTwo` (an object handed over: give or take, below) |
| other-actor | kiss, hug, punch, shove, slap, stab, takedown, collapse, choke, fight, block, death, getup, knock, stagger, grabbed, romance, sex, holding hands, a hand on the shoulder or cheek, drag, tango, `DG_` (damage), restrain, arrest; and any other two-person `2P_` animation that doesn't only sit or walk | `2P_Kissing_Male_Female_A_Idle`, `2P_HoldHand_A_Idle`, `2P_PunchFace_Shep`, `DG_Stomach`, `HMM_2P_BarFight` |
| drink | drink, cheers, toast, sip, pour, bartender | `DP_LeanBar_Drink`, `WI_Sit_Drink_En_Tw_V2`, `Cheers_A`, `WI_Bartender_Pour` |
| press | button, keypad, typing, console, terminal, keyboard, panel, lever, switch, computer, `CorePush`, a tap, a hatch | `FC_ButtonPress01`, `WI_KeypadArmed_Loop`, `CB_KeypadArmed_Rifle_Enter`, `WI_TypeStanding1Idle`, `ForceFieldTap` |
| give | give, hand over, offer | `DP_SitByBed_LeanIn_GiveObject`, `GiveObjectAffectionate_Action` |
| take | pick up, take, catch, receive, retrieve, collect | `DP_PickUpObj_Stand_Enter`, `DP_TakePistol`, `DP_DogCatch_StepForward`, `WI_PickUpFrame_Enter` |
| other-hands | omni-tool, PDA/datapad, weapon words (pistol, rifle, aim, draw, holster, fire, reload), examine, communicator, repair, hack, a door held/closed/pried, a refused handshake, grab (a thing), revive | `WI_OmniIdle`, `DP_PistolThreatenIdle`, `FC_Communicator_Enter`, `HMM_AM_Revive` |
| not-hands | the dialogue and body sets (`DL_`, `AM_`, `DP_`, `WI_`, `EX_`, `CB_`, `AR_`, `FC_`, PoseBreaker, Wander, WalktoNode, HalfTurn, HoldCeiling ...) and body words (walk, sit, stand, lean, turn, idle, look, nod, point, wave, shrug, injured, dance ...) | `DL_HeadShake`, `DL_PointRight`, `AM_WeightShift01`, `DL_Injured_WalkForward_Enter` |
| (none) | anything else: a scene-coded baked animation | `GLO_00_B_Sovereign_3`, `Cit004_Goodbye_2`, `End002_crucible_dock_1` |

The **scene's kind**: the first of other-actor, handshake, salute, exchange, drink, press, give, take that one of his names
has; an exchange becomes give or take by its props (another actor's prop track holds the same thing first: take; he holds it
first: give; his own prop key comes after the exchange starts: take; else take with "direction unknown"); with no such name,
**his props** decide (a glass, bottle, cup or flask in his hand: drink; an object that passes between him and another actor:
take or give; a weapon, the omni-tool or any other prop he only holds: other-hands); then other-hands if a name is; then the
level matinee's designer comment (`m_aObjComment`, "Shep Drinking"; a line's comment is its text and is never read) at low
confidence; then not-hands when every name is body vocabulary; else **unclassified**. Props are `BioEvtSysTrackProp`'s keys
(`nmProp`, `pPropMesh`, `bEquip`; its actions `Fire`, `Expand`, `EnableIK` are no things) and, beside a level matinee, the
`SeqAct_AttachToActor`s whose Target is the player (a conversation's sequence holds every line's attaches, so not for lines).

**Confidence:** high = the word on a track bound to him explicitly, or a drink name and a glass prop together; medium = he is
bound by the group's name or the ME1 untagged rule, or the kind comes from a prop or a transfer; low = a stunt double, or the
comment alone. "Pause at" (`pauseAt`) is the first driving key's time.

## 4. The counts

Lines are conversation lines (one per line, every copy of a conversation counted once); "conv." counts distinct conversations.

| Kind | ME1 total (high/med/low) | level, lines (conv.) | ME2 total (h/m/l) | level, lines (conv.) | ME3 total (h/m/l) | level, lines (conv.) |
|---|---|---|---|---|---|---|
| drink | 0 | - | 14 (10/4/0) | 2, 12 (8) | 60 (40/20/0) | 1, 59 (15) |
| press | 6 (0/6/0) | 2, 4 (4) | 52 (47/3/2) | 17, 35 (19) | 255 (254/0/1) | 48, 207 (67) |
| take | 49 (0/49/0) | 0, 49 (26) | 50 (23/27/0) | 5, 45 (22) | 69 (57/12/0) | 4, 65 (39) |
| give | 0 | - | 6 (5/1/0) | 0, 6 (4) | 14 (14/0/0) | 0, 14 (6) |
| salute | 3 (0/3/0) | 0, 3 (2) | 1 (1/0/0) | 0, 1 (1) | 21 (21/0/0) | 0, 21 (13) |
| handshake | 21 (0/21/0) | 0, 21 (14) | 22 (22/0/0) | 0, 22 (15) | 77 (76/1/0) | 0, 77 (36) |
| **six gestures** | **79** | 2, 77 (43) | **145** | 24, 121 (65) | **496** | 53, 443 (150) |
| other-hands | 198 (2/196/0) | 4, 194 (69) | 631 (628/3/0) | 32, 599 (202) | 1,641 (1,638/3/0) | 177, 1,464 (271) |
| not-hands | 5,910 (4,384/1,526/0) | 32, 5,878 (234) | 12,363 (6,874/5,489/0) | 78, 12,285 (503) | 18,700 (10,173/8,527/0) | 526, 18,174 (607) |
| other-actor | 57 (1/56/0) | 4, 53 (17) | 183 (171/12/0) | 17, 166 (65) | 483 (481/2/0) | 61, 422 (122) |
| unclassified | 55 (0/0/55) | 54, 1 | 145 (0/0/145) | 143, 2 | 160 (0/0/160) | 58, 102 (18) |

Not-hands at medium are mostly lines where his track plays no animation (a look-at or facing track only). How the takes and
gives were decided: ME1's 49 takes are all exchanges with no prop on him (ME1 attaches props by Kismet, not by track: direction
unknown); ME2's takes 20 by name, 27 exchanges of unknown direction, 3 by props, its 6 gives all by props (a prop passing
between actors, or held before the exchange); ME3's takes 48 by name, 10 unknown exchanges, 11 by props, its gives 11 by name
(`GiveObject`), 3 by props.

**What the level gesture scenes drive** (so the gesture can be judged safe to hand over): ME3's 53: 39 have camera cuts (a
director track), 11 a fade or black screen, 8 another actor with an animation; what their outputs lead to: a teleport 10, a plot
transition 8, a conversation start 6, another matinee 4, a camera fade 3, an award 1; 21 start from a use (drink 1, press 17,
take 3). ME2's 24: 24 with cuts, 17 with fades, 17 with another actor; plot transitions 2, mission XP 2, conversation starts 2,
a treasure award 1; 7 from a use (drink 1, press 5, take 1). Lines: in ME3 343 of the 443 gesture lines have another actor
animating, 80 a prop on Shepard; ME2 90 and 26 of 121.

**Uses** (Kismet use events, context only: a use that plays no matinee on Shepard is a button press already, and the games'
native uses (doors, loot containers) raise no Kismet event unless the level listens):

| | ME1 | ME2 | ME3 |
|---|---|---|---|
| Use events | 1,598 | 1,496 | 1,634 |
| by what is used | unknown object 968 (ME1's placeables' events carry no originator in the package), a person 269, door 105, console 65, loot 26, other 165 | interaction point 620, unknown 374, person 187, loot 122, console 44, door 30, other 119 | interaction point 701, unknown 570, person 129, loot 97, door 65, console 51, traversal 14, other 7 |
| play a matinee within 12 actions | 63 | 154 | 541 |
| play one on Shepard | 2 | 35 | 53 |
| play one of the six gestures | 0 | 7 | 17 |

## 5. The best candidates, per game

The census's score: the kind (drink 10, press/take/give 9, handshake/salute 8), confidence (+6 high, +3 medium), a level
matinee over a line (+2), a prop in his hand (+2), a scene a use starts (+2: the player chooses it, it can be repeated), a scene
over 30 s (-2); at most a third of the list of one kind first. Cameras don't count (first person drops them).

**ME3** (all explicit bindings):
1. drink, `BioD_CitHub_WardsFlux` `Drinking.SeqAct_Interp_0`, 10.8 s, from the use at `SFXPointOfInterest_1`: **Purgatory's
   bar** (section 6).
2. take, `BioD_Nor_100Cabin` `Tali_Picture.SeqAct_Interp_5`, 28.3 s, from a use: Shepard picks up the picture frame in his cabin
   (`WI_PickUpFrame_Enter/Loop/Exit`, props `Tali_Picture`, `PictureFrame02`).
3. press, `BioD_Omg001_110Corridors` `02_elevator.SeqAct_Interp_2`, 4.5 s, from a use: an Omega (DLC) elevator keypad
   (`CB_KeypadArmed_Rifle_Enter`, `WI_KeypadArmed_Loop`); the same in `Omg001_310Exit`, `500SideStreet`, `BioD_CitSam_710Elvt`.
4. press, `BioD_CerMir_250Pools` `Miranda_vid.SeqAct_Interp_0`, 10.0 s, from a use (a keypad).
5. press, `BioD_CitCas_200Casino` `Mini-games.Quasar.Player_Placement.SeqAct_Interp_0`, 1.5 s, from a use: the Citadel
   casino's machines (Quasar, Race, Roulette; the arcade's in `BioD_CitCas_310Arc_Deco1`), `WI_TypeStanding1Enter` at 0.7 s.
6. take, `BioD_Lev002_350LabsA` `Video_Tour_3.SeqAct_Interp_2`, 8.5 s, from a use (`DP_PickUpObj_Stand_Enter`, `_Exit_Keep`).
7. drink, `cit001_meet_follower_m_d` (`BioD_Cit001_115Joker_LOC_INT`), lines with a glass at Joker's table
   (`WI_Sit_Drink_En_Tw_V2`, `Glass_Garrus`, `GlassShort02`).
8. drink, `citcas_ash_m_D` (Citadel DLC, Ashley at the casino bar: `DP_LeanBar_Drink_LessHeadshake`, `Brandy_Glass_Drinking`).
9. take, `kro002_shuttle_ride_in_m_d`, 10.5 s: `DP_TakePistol` with `Pistol_Predator` (a pistol handed to him in the shuttle).
10. give, `citapt_miranda_m_D`, 11 s: `2P_ExchangeOne` with the glasses passing between them (`MirandaGlass_01`, `GlassShort01`).

**ME2** (explicit):
1. drink, `BioD_Nor_280Kasumi` `Drinks.SeqAct_Interp_3`, 6.6 s, from a use: **Kasumi's bar on the Normandy**
   (`DP_LeanBar_Drink` at 1.16 s, props `Glass`, `GlassShort_World`).
2. take, `BioD_PtyMtL_300Vault` `Talk_objects.REF_AwardTreasure.SeqAct_Interp_0`, 17.1 s, from a use (Kasumi's DLC vault:
   `DP_PickUpObj_Stand_Enter` at 3.2 s; an award follows).
3. press, `BioD_N7BldInv1_100` `MessageBoxes.SeqAct_Interp_9`, 14.4 s, from a use (`FC_ButtonPress02` at 0.34 s).
4. press, `BioD_N7Crash` `SeqAct_Interp_4`, 19.9 s, from a use (`FC_ButtonPress03`).
5. press, `BioD_N7Geth2_100` `Geth_Console.SeqAct_Interp_0`, 12.7 s, from a use (`WI_TypeStanding2` at 5.4 s); also
   `N7Spdr1_500` `Ponts_of_Interest`, `N7VIQ2_100` `Plot`.
6. drink, `cithub_barkeep_d_d` (the Citadel's lounge bar, `DP_LeanBar_Drink` with a glass).
7. drink, `nor_doctor_d_D` (Chakwas's brandy in the med bay: `WI_Reclining_Drink`, `Glass_Left`).
8. give, `arvlvl1_kenson_d_d` (Arrival): `2P_ExchangeOne`, a `HeavyPistol` passing between them.
9. take, `twrli1_liaravideo_d_d` (Lair of the Shadow Broker): `DP_PickUpMid_End`, `Data_Disk2`.
10. take, `shdli5_relationship_02_h_D`: `2P_ExchangeOne` with `Dogtags_Hold` and `DogTag_Case_01` (the dog tags).

**ME1** (no level matinee a use starts; lines by the untagged rule, so medium):
1. press, `BIOA_LAV70_06_DSG` `AsariScientist.CD_LAV70_06_Liara_Intro.SeqAct_Interp_0`, 9.1 s: `ForceFieldTap` (Liara's
   force field on Therum), group `Player` by name only.
2. press, `BIOA_LAV70_08_DSG` `StartFinalFight.krogan_boss_fight_intro.SeqAct_Interp_0`: `ForceFieldTap` at 5.1 s.
3. press, `ice20_anoleis_N` (Noveria): `FC_ButtonPress03` at 2.5 s.
4-6. take, `ice20_opold_N`, `ice20_lorikquiin_N`, `ice20_mallenecalis_N`: `2P_ExchangeOne/Two` (Noveria's hand-overs;
   direction unknown).
7-9. handshake, `ice60_julethventralis_N`, `pro20_powell_N`, `sta20_garrus_N` (`2P_HandShake`).
10. salute, `sta30_snap_inspection_N` (`DL_Salute`).

## 6. The scenes the owner would notice

**Purgatory (ME3)**: `Drinking.SeqAct_Interp_0` (10.8 s; comment "Shep Drinking") holds five shots (`SFXSceneGroup` Shot1, 2,
3, 4, 7), each with its own director (5 cuts), cameras, a `Player` group (the first bound by `SeqVar_Player`, the others find
`Player` by name), a `Drinking_Glass` group (a visibility track: the glass on the bar) and a `Blend_Point`. Shot 1, male:
`DP_LeanBar_HeadUp_Enter` 0.68 s, **`DP_LeanBar_Drink` 3.89 s**, `DP_LeanBar_HeadUp_Exit` 7.93 s; props: the glass and its
liquid (`GlassShort02`, `GlassShort02_Liquid_Drink`) **equipped at 4.37 s**, the liquid gone at 6.22 s, the glass put down at
7.13 s. Shot 2 drinks `DP_LeanBar_Drink_LessHeadshake` with the same keys. Which shot plays is ME3's (by the drink count? [verify]).
What leads to it: the use, then `BioSeqAct_BioToggleCinematicMode`, a teleport to the bar, `SFXSeqAct_SetTargetable`; after it:
switches and bools (the drink count), the cinematic mode off, a black screen, `SeqAct_Interp_2` (the walk to the booth under a
fade: classified other-actor by `NorROM_PostDream_Garrus_A_Enter`, a romance walk the word rule reads as romance [verify]),
`SeqAct_Interp_3` (seated, not-hands). Also at Purgatory: `citwrd_pilot_char_moment_d_D` (a drink with Joker at the bar, the
same props), `citwrd_james_club_m_D` (a handshake and a salute with James's marines). research/le2-conversations-in-world.md
12.2 item 3 already plays the drink as a moment on the fake headset (`conversation-world.ps1 -Start
fire:Engine.SequenceOp:Drinking.SeqAct_Teleport_0 -StartInput 0`): the fixture exists.

**Chora's Den and the Wards (ME1, `BIOA_STA60_*`)**: 29 gesture lines, no level matinee: exchanges (Jax, the gambler Schells,
a turian guard, the reporter, Zabaleta) and handshakes (Chorban, Septimus, a gang member, Terra Firma's). ME1's bar scenes have
no drink animation on Shepard.

**Afterlife (ME2, `BioD_OmgHub_220DenBar`, `230DenDance`)**: two drinks at the bar (`omghub_order_drinks_d`,
`omghub_newbartender_d_d`: `DP_LeanBar_Drink` with a glass), exchanges (`omghub_anto`, `omghub_ish`, `omghub_kenn`), a datapad
given (`omghub_asari_ruler_d_D`), handshakes (`omghub_gavorn_d_D`, Zaeed's recruitment `zyavtl_zaeed_acq_d_D`).

**The Normandy**: ME1 none. ME2 19: Kasumi's bar (level, from a use), Chakwas's brandy, Hackett's datapad given
(`arvend_hackett_d_D`), the AI core and engine consoles (`norcr3_ai_core_d_D`, `norcr3_ai_engines_d_D`), handshakes (Legion's
first talk `norgt_relationship00_h_D`, an epilogue `bchlml_epilogue_h_D`), a salute (`norasa_debrief_d_D`). ME3 51: Tali's picture (level, from a
use), the cabin's intercom presses (`BioD_Nor_100CabinConv` `Intercomm.*`: six character moments, `FC_ButtonPress01/03`), the
admiral's datapad (`norgth_admiral_inv_hub_d_D`: give and take), a drink with Tali (`norhen_tali_mr_cermir_d_D`: `WI_Sit_Drink_En_Tw_V2` on Shepard), salutes
(Hackett's video `expak1_hackett_vid_m_d`, Adams `nor_adams_d_D`, a krogan mission's video), handshakes (Garrus, the pilot, a female krogan, after the second krogan mission), Hackett's speech at the end
(`norend_hackett_speech_v_D`: a salute and a handshake 28-36 s in).

**The Citadel**: ME1 46 lines (the exchanges and handshakes above, the cab console `sta00_cab_N`, the Spectre induction's
handshake `sta90_council_spectre_induction_N`, Kahoku's). ME2 7 (the lounge's barkeep drink, the Council's handshake
`citprs_council_d_D`, exchanges at the first contact `citasl_first_contact_d_D`). ME3 183 (with the Citadel DLC): Purgatory, the casino and arcade
machines (level, from uses), the apartment party's drinks and bottles (`citapt_edi_m_D`, `citapt_kaidan_m_D`), Aethyta's bar
(`citprs_aethyta_d_D`), gifts in the hospital (`citprs_ash_hospital_m_D`: a book given; `citprs_kaid_hosp_return_m_D`: a whisky
bottle), Garrus's catch (`citprs_garrus_char_moment_m_d`: `AM_Obj_Catch`), Ashley joining (`citprs_cat3_ashjoins_m_D`: handshake).

## 7. What the rule couldn't classify

**Unclassified: ME1 55, ME2 145, ME3 160**, nearly all whole-body baked cutscene animations named for their scene: ME1's
`ANIMCUTSCENE`-style matinees (`JUG_80_C_Nukeversion2_*`, `JUG_80_B_Sarenversion2_*`, the romance scenes `ROM_02_Leadin`,
`ROM_03_X_Finale`), ME2's (`Arv_110_MedWake_RB_*`, `EndGm2_PS_Hench1_Alive_*`, `N7HMR_NorIntro`), ME3's (`Cit004_Goodbye_*`,
`End002_crucible_dock_*`, `Male_Red_Carpet_17066_1`, the memorial wall's `Wheel02_A_Inv02_*`). Their hands may well act (a
goodbye's embrace, the memorial's name plate), but the names don't say, and each is a long single-take animation with other
actors synchronised to it: hand them over only after looking at them (they are outside the rule by design).

Known soft spots of the word rule [verify]: ME1's untagged-track inference (section 2); 86 exchanges of unknown direction
(counted as take); `NorROM_` animations read as romance by the word "romance" only when spelt out (the Purgatory booth walk is
`other-actor` through it); ME3's `WI_TypeStanding` at the casino machines is the player stepping up to a minigame (a press that
opens a screen, research/53's world screens); "point" and "wave" are read as talking gestures; weapon props held in a line are
other-hands, not a give.

## 8. What building the handover would need (an implementation guide)

**One mechanism for every kind** (shared module, three adapters, as every system: docs/trilogy-parity-ledger.md):

1. **Which scenes.** A generated table, as `cw::IsSetPiece` and `cw::FlowEntries` are: `tools/hands-census.py` would gain a
   `--table` writing `core/vr/HandScenes.inc` (game, package, matinee path, kind, pause time, the prop's names, the gesture's
   window), generated, never typed; or the same rule ported to the core and run on the playing matinee's groups at its start
   (the mod reads a moment's tracks live already: `authored_moment::PlayerTrackInstance`). Start with the table for the
   prototype (exact times known offline), the live rule once the kinds are proven.
2. **When the scene plays**, it is already a held moment in first person (ME1 `game/AuthoredMoment` + `game/ConversationWorld`
   `TickMoment`; the sequels `game/SequelConversationWorld`, research/le2-conversations-in-world.md 6.2, 11.3): the view is the
   player's, cameras are dropped. The body must be drawn under the view and arm IK must drive both arms (as in a wrist screen:
   `view_anchor::Hold`, the on-foot frame).
3. **The pause**: at `pauseAt` (the gesture key, or the prop key for drink and take), set `Engine.SeqAct_Interp.bPaused` as
   the walk-along rule does (`gInterpPaused` in `game/ConversationWorld.cpp` and `game/SequelConversationWorld.cpp`, every game;
   journalled; released at the moment's end and at unload). Everything in the matinee waits: other actors hold their pose.
4. **The gesture**, judged on the controllers in the player's frame (unit-tested rules in a new `core/vr/HandScene`); a
   timeout (8-10 s) and B let the authored animation play, so a scene never stalls.
5. **The resume**: clear `bPaused`; where the player already did what the animation does, move the matinee past it (the
   `SeqAct_Interp`'s position: a set-position call or a written `Position` [verify which]) to the key after the gesture, so it
   isn't played twice; the matinee then runs to its end and its outputs (plot, teleports, awards) are the game's.

**Per kind:**

| Kind | The gesture | The prop | Where to pause | Cameras, other actors | Risk |
|---|---|---|---|---|---|
| drink | the hand holding the glass within ~12 cm of the mouth (the eyes' midpoint 8 cm down) for 0.4 s, tilted | already in his hand: pause just after the equip key (Purgatory 4.37 s), the glass rides on the hand bone arm IK writes [verify the attachment follows the IK'd bone in the same frame]; resume at the "liquid gone" key (6.22 s) | the prop's equip key | none at the bar (Purgatory: no other actor in the matinee); the bartender in conversation lines | the lean-bar pose leans his head 1.05 m onto the bar (research/le2-conversations-in-world.md 12.2 item 3): the body must stand under the player first |
| press | a fingertip push (the pointer's touch rule, `vr::game_pointer` touch: 0.5 cm in, research/42) at the point the animation's hand reaches at its peak (read his hand bone at the key's middle, then pause) | none | the press key (`FC_ButtonPress*`, `WI_Keypad*`) | the level ones are short (0.5-15 s), from uses; a door or elevator follows | the button's place is only the animation's hand: a console may be modelled elsewhere [verify] |
| take | the grip closing within ~10 cm of the thing (the other actor's hand bone at the exchange frame, or the world prop: Purgatory's `Drinking_Glass` group, Tali's `PictureFrame02`) | the prop key equips it into his hand on resume; until then our grip holds the world copy or nothing | the exchange or pick-up key | the giver frozen holding it out (a two-person animation paused: both wait) | direction unknown for 86 exchanges (ME1 all): read the other actor's prop track first, else skip |
| give | the hand holding his prop within ~10 cm of the other's hand, the grip opened | his prop (datapad, bottle, book) | the exchange key | as take | as take |
| salute | the right hand's fingertips within ~12 cm of the brow, palm down, 0.4 s | none | the salute's Enter key | nobody touches him; the easiest | conversation lines only (no level salute): the line's audio must not be cut (pause at its end, as walk-along does) |
| handshake | the right grip closed within ~10 cm of the other's right hand (their hand bone in the paused frame), a little up-down | none | the `2P_ShakeHand` key | the other actor paused mid-reach, hand out | always in a conversation line; the line's timing (its VO, the next node) waits on the matinee [verify that a paused line holds the conversation] |
| other-hands | none generic: leave authored (the omni-tool and weapon ones belong to their own systems) | | | | |
| other-actor | stays authored | | | | |

**Order of work:** (a) the drink at Purgatory, level matinee, from a use, no other actor, the prop already in hand, a fixture
that already runs; (b) the same machinery on Kasumi's bar (ME2) and the ME3 conversation drinks (lines: the pause and the
line's audio); (c) salute and handshake (no props: a target at the head or at the other's hand; 145 lines); (d) press (the
level keypads, elevators and casino machines: 22 scenes from uses); (e) take and give last (directions and world props).
ME1 has little to gain (79 lines, no level scene from a use): its hands come last.

## 9. [verify]

- ME1's untagged conversation tracks are the player's (section 2): a line with an untagged `2P_HandShake` played and watched.
- `SeqAct_Interp.bPaused` holds a conversation line's matinee and its conversation together, and the line's VO resumes where it
  stopped (walk-along pauses only level matinees).
- A prop attached by `BioEvtSysTrackProp` follows Shepard's hand bone after arm IK writes it, in the frame it is drawn.
- Which of Purgatory's five shots plays, and why (the drink count, at random).
- The DLC mount order the census assumes (a DLC's copy over the base game's, the later folder by name over an earlier one; ME3's
  `DLC_EXP_Pack003_Base` then sorts over `DLC_EXP_Pack003`, the opposite of their real order).
- Every pause time is a key's time in the data; how long before it the hand starts moving is the animation's (not read: the
  AnimSequences of conversation sets are mostly in other packages, so `animLength` is often unknown).

## 10. Owner questions

1. **Build it at all, and which kinds first?** Recommendation: yes, drink first, then salute and handshake, then press; take and
   give last; other-hands and other-actor stay authored.
2. **Purgatory's drink as the prototype?** Recommendation: yes (section 8 (a)); the alternative with no conversation timing at
   all is Kasumi's bar on ME2's Normandy.
3. **How strict should a gesture be?** Recommendation: lenient (a clear motion toward the target within its radius, held 0.4 s),
   with a timeout (about 8 s) and B after which the authored animation plays, never a stuck scene; a short haptic when it's taken.
4. **Conversation lines too, or level scenes only at first?** 89% of the gestures are lines (pausing them touches the
   conversation's timing). Recommendation: level scenes from uses first, lines after the pause is proven on one.

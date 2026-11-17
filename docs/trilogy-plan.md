<!-- SPDX-License-Identifier: MIT -->
# The trilogy plan: one work list, by feature

**As of 2026-11-16, main `f67d10dd`.** This is the work list for MEVR on all three games. It replaces the per-game plans
as the place to pick work from; they stay as the record of how each game got here (each has a banner pointing back).

**Why it exists.** The owner, 2026-11-16: "We likely need to unify approaches to ensure consistency among all games. We
would find that some work would not leverage the prior art in ME1 and it lead to a lot of waste." Until today five plans
and handoffs each decided reuse for themselves, and lanes picked work per game and built it per game. The
[parity ledger](trilogy-parity-ledger.md) counted the result: 12 cells where a sequel has its own copy of something ME1
has (a "fork"), three seam vocabularies, and 33 ME1 systems missing on ME2 and 32 on ME3.

**How to use it.**
1. Read [the rules](#the-rules-once). They are the owner's decisions; nothing below reopens them.
2. Find the feature in [the work list](#the-work-list). Its entry says where the shared code lives, what is left per game,
   the check that closes it, what it waits for and who has it.
3. Read that feature's rows in the [ledger](trilogy-parity-ledger.md) (the ids in brackets) before touching code. The ledger
   holds the state and the evidence; this page holds the work and its order. Where they disagree, the ledger's cell is the
   state and this page is out of date: fix it.
4. Take a feature for every game at once. An entry that leaves a game for later says why.

**Keeping it current.** When a change moves a ledger cell, the lane runs `python tools/parity-ledger.py --write`, rereads
its rows, and edits this page's entry in the same landing: the state line, what is left, who has it. A feature whose every
game reads `done` or `n/a` moves to [Closed](#closed-checks-only). New work that isn't a ledger row gets an entry here
first, then a ledger row.

## The rules, once

Each rule has who decided it, when, and where it was said. "The continuation plan" is
[le2-continuation-plan.md](le2-continuation-plan.md); its section "The owner's answers (2026-11-14)" is cited as
"answer N".

### What the game should be

| # | Rule | Decided | Said in |
|---|---|---|---|
| 1 | **ME2 and ME3 continue ME1.** "This should really just feel like a continuation of the prior mod, not a radically new experience... Everything should really try to use ME1 as default and only deviate where necessary." ME1's systems, settings, defaults and resources (menus, panels, fonts, sounds, haptics) on every game. | the owner, 2026-11-14 | continuation plan "The rule"; [le2-from-le1.md](le2-from-le1.md) section 0; for ME3, [the LE3 plan](le3-port-implementation-plan.md) "Outcome and boundaries" |
| 2 | **Port, don't redesign.** A step that changes behaviour names the game fact that forces it (ME2 reloads thermal clips; ME3's powers are custom actions). | the owner's rule, written 2026-11-14 | continuation plan "The rule" |
| 3 | **One code path.** No per-game copy of a module. See [the architecture contract](#the-architecture-contract). | 2026-11-14; restated by the owner 2026-11-16 | continuation plan "The rule"; LE3 plan "Architecture contract"; ledger "Why it exists" |
| 4 | **No head aim.** The hand aims; the head only looks. VRIK is fast-tracked so nothing needs it. | the owner, 2026-10-08 (ME1) and 2026-11-14 (ME2) | CLAUDE.md "Renderer"; le2-from-le1.md section 0; ROADMAP "Not planned" |
| 5 | **No camera recoil, from the first shot.** It "wasn't popular". The view stays level, the gun kicks in the hand, the firing shakes are off at their source. | the owner, 2026-11-14 | continuation plan M3.1; research/le2-vrik-audit.md "No camera recoil on ME2 either" |
| 6 | **The stick doesn't pitch the view** (ME1's `decoupledPitch`). | the owner, 2026-11-14 | le2-from-le1.md section 0 |
| 7 | **No cover snapping.** "We won't snap to cover for VR, it's not a realistic experience. It risks the same problems of our skeleton not matching our players position in space." Physical crouch and the VR cover rule instead; the game's cover protection is kept. | the owner, 2026-11-14 | answer 7; research/23-cover.md sections 6-7 |
| 8 | **Double A vaults, with weapons held**, from where the player stands. Disabling cover never disables vaults, climbs or mission triggers. A two-handed hold must vault too; vaulting with one hand on the gun is to be weighed ("For VR we may need to allow for vault with two handed, but we may want to consider vaulting with one hand. The controls could get sloppy."): both are measured, the hold is kept until the owner picks. | the owner, 2026-11-14; the two-handed hold 2026-11-16 | answer 7; research/le2-traversal.md; [le2-port-implementation-plan.md](le2-port-implementation-plan.md) "Product decisions"; the owner's answers of 2026-11-16 (the lanes' notes) |
| 9 | **Conversations and cutscenes on ME1's 3D cinema screen.** First-person conversations in the world are deferred on ME2 and ME3; Thespian mode may use the cinema screen. | the owner, 2026-11-14 and 2026-11-15 | le2-from-le1.md section 0; continuation plan M6; LE3 plan "Outcome and boundaries" |
| 10 | **Authored seated scenes (shuttle rides) allow no traversal and no combat gestures**, and keep head tracking and the dialogue controls. Decided from the native scene's state; physically seated play in ordinary gameplay keeps movement. | the owner, 2026-11-15 | continuation plan M6; [le2-m6-implementation-guide.md](le2-m6-implementation-guide.md) "Current scope"; LE3 plan |
| 11 | **Interrupts are on the triggers**, as the game has them on a pad (LT Paragon, RT Renegade). Nothing of ours claims a trigger in the conversation mode. | the owner, 2026-11-14 | answer 1; continuation plan M5.2 |
| 12 | **SDR for now; HDR is out of play.** `[Colour] hdrDisplay = -1` on every game; the picture work is the best SDR picture in dark scenes. | the owner, 2026-11-14 | answers 5 and 6; research/le2-hdr.md; research/le2-picture.md |
| 13 | **The Hammerhead gets the Mako's controls and cockpit**, in a pass of its own after ME2's first release. | the owner, 2026-11-14; kept out of the current pass 2026-11-15 | answer 3; M6 guide's opening |
| 14 | **The galaxy map opens over the Normandy's projector, with the CIC around it**, as in ME1. A flat table placed from the head is an intermediate step, not parity. | the owner's correction, 2026-11-15 | [galaxy-projector-handoff.md](galaxy-projector-handoff.md) "Intended result"; LE3 plan "Galaxy" |
| 15 | **Menu rows a game doesn't have yet stay greyed.** "Keep them greyed until I can see them." | the owner, 2026-11-14 | answer 8 |
| 16 | **The stick-click hold first.** `\` stays the menu key; holding both stick clicks always wins and is named first in the controller guide. Nothing depends on a controller's menu or system button. | the owner, 2026-11-14 | answer 8; ROADMAP "Not planned" |
| 17 | **Squad weapons stay on the left-trigger menu.** The weapon row inside the right-B placement wheel was built and reverted at the owner's request. | the owner, before 2026-11-15 | M6 guide B "Preserve the approved interaction" |
| 18 | **The game stays the authority** for damage, ammo, inventory, cooldowns, resources and plot. VR asks the game to act through its own functions and keeps no writable second copy. A reload is done by hand but the game counts the ammo. | 2026-10-07; 2026-11-13 | [trilogy-design.md](trilogy-design.md) rule 3; le2-port-implementation-plan.md "Product decisions"; LE3 plan rule 5 |
| 19 | **No new player-facing default without the owner's yes.** ME1's value is the default; a lane reports a recommendation instead of flipping one. | the owner, 2026-11-11; the lanes' rules, 2026-11-16 | research/le2-picture.md section 5 |
| 32 | **Everything consistent.** "Ideally I want everything consistent. Me3 I'd prefer other elements stay open if it helps with immersion." A per-game difference in behaviour needs a reason the game forces; our menu treats the game's layers the same on every game, and our own quads stay hidden under it on all three (the owner, 2026-11-16, question 10). | the owner, 2026-11-16 | research/trilogy-seams.md "ME3 as ME1", "One recenter rule" |
| 30 | **Sights on the sequels use ME1's systems for now** ("sight details can use me1's systems for now"): ME1's dot, holo, scope, ADS, assist and convergence, each game's names behind the adapter; per-model optic profiles wait for measured numbers. | the owner, 2026-11-16 | the owner's answers of 2026-11-16; T24 |
| 31 | **The trigger never draws a holstered weapon, on any game** ("trigger draw a holstered weapon? No, none of the games should do that."): only a grab at a holster draws (ME1's `[Holsters] onlyHolstersDraw`). | the owner, 2026-11-16 | the owner's answers of 2026-11-16; T25 |

### How the work is done

| # | Rule | Decided | Said in |
|---|---|---|---|
| 20 | **ME2 first, then ME3.** ME2 ships after M5 (engine access, the host's services, VRIK, the menus, the cinema screen and interrupts); the rest follows that release. | the owner, 2026-11-14 | answer 4; continuation plan M7; ROADMAP "Next" |
| 21 | **Parallel sessions**, each lane in its own worktree, coordinated by one session. | the owner, 2026-11-14 | answer 2 |
| 22 | **Scripts, not computer use.** Runs are driven by launch, save, console, test-loop and replay scripts; a check that needs what no script reaches waits for the script. | the owner, 2026-11-14 | continuation plan M1.6 and "Who and when" |
| 23 | **One game run at a time**, across all three games and every session: `tools/le2-halcyon-run/run-lock.ps1`. A game running with the lock free is someone else's. Saves and settings are backed up once at the start of a stretch and restored once at its end. | 2026-11-14; widened to three games 2026-11-15 | continuation plan "Who and when"; LE3 plan "Work packages" |
| 24 | **Prune worktrees when work lands** (`python tools/clean-worktrees.py`), keeping those with unlanded work or private evidence. | the owner, 2026-11-14 | CLAUDE.md "Test loop"; LE3 plan "Parallel execution and landing order" |
| 25 | **ME1 must not change.** A change to code LE1 uses gets a same-pose LE1 check on the fake headset, before and after. | 2026-11-14; the lanes' rules, 2026-11-16 | continuation plan M2.2; research/le2-traversal.md "LE1 unchanged" is the pattern |
| 26 | **Land only after `build-trilogy`, `build`, `build-mono` and ctest pass**, through `tools/land-on-main.py`. The owner pushes. | 2026-11-14 | continuation plan "Who and when" |
| 27 | **Four kinds of evidence, never merged:** understood from the source, built and unit-tested, seen in the game by script, accepted in a headset. A compiled adapter, a dispatched call or a drawn layer is not a working feature. | 2026-10-07; 2026-11-15 | trilogy-design.md rule 7; LE3 plan "Parallel execution and landing order" |
| 28 | **Sources we may use:** our own code, Halcyon's GPL renderers, Overlord (GPL-3.0) for reload and hand ownership. Never a closed binary (the Titanfall VR mods are closed). LExSDKv2 has no licence: no players' build links it. | 2026-11-13; 2026-10-26 | [le2-port-reuse-map.md](le2-port-reuse-map.md); CLAUDE.md "Licensing rules" |
| 29 | **Never `streamlevelin`; never `tools/game-slot.ps1 return` without asking; never change the machine's OpenXR runtime or another game's ini to make a test work.** | 2026-11-09 to 2026-11-15 | LE3 plan rule 6 and "Reuse the save/editor and test flow"; M6 guide |

### The architecture contract

From the LE3 plan's "Architecture contract: implement each feature once" (2026-11-15), which holds for all three games:

1. **Existing boundaries first.** Solvers, gesture machines and view descriptions in `src/core`; native queries and
   commands on the game thread in `src/game` and `src/engine`; rendering and input services in `src/plugin`; small
   versioned host/mod messages. Refactor a per-game conditional into an adapter where a third game shows the need.
2. **Semantic contexts.** Shared code gets "conversation", "galaxy map", "GUI", never a mode number (ME3's
   conversation/cinematic/GUI/movie/galaxy/orbit are 7/8/9/10/11/12; ME2's 5/6/7/8/9/10).
3. **Explicit capabilities.** A service is served only when its renderer part, native adapter, lifecycle and ownership
   exist. `not LE1` must not silently mean ME2, nor `not LE2` mean LE1.
4. **One owner** of the XR session, Present, the ProcessEvent chain, the controller samples and each hand. One physical
   action makes at most one native request. No script call off the game thread.
5. **Native authority.** An adapter says requested, rejected, started, completed or interrupted; a void call is not
   success. Cached objects, pending orders and late speech are bound to the world, player and scene they were made in.
6. **One delivery and testing system.** Three ASIs are normal; one menu, one recogniser, one runner, one save-tool
   interface, one evidence format.

**The ledger's rule** (2026-11-16): build a feature as **the shared core plus a small per-game adapter** for native facts
(addresses, class and field names, game modes), never as another game-specific copy. A copy is a "fork" in the ledger and
is work to remove.

## Lanes on 2026-11-16

Who has what today. "The other session" works outside the coordinating session's lanes: don't touch its files.

| Lane | Takes | Owns | State |
|---|---|---|---|
| U1 | one seam vocabulary and a capability set | `src/halcyon/MevrSeams.h`, the three `mevr_bridge.h`, `plugin/Renderer.hpp` | started |
| U2 | one sequel bridge, one overlay composition | `plugin/HalcyonLe2Bridge`, `HalcyonLe3Bridge`, `HalcyonOverlays`, `OverlayFrame` | after U1 |
| EY | one eye-image pipeline | `plugin/EyeCopy`, `Colour`, `Upscale` | after U1 |
| R2 | ME2's native addresses for ME1's render levers | `engine/Addresses.hpp` LE2 rows, `research/sequel-render-levers.md` (LE2 part) | started |
| R3 | ME3's native addresses for ME1's render levers | `engine/Addresses.hpp` LE3 rows, `research/sequel-render-levers.md` (LE3 part) | started |
| PL | the render levers and the perf probe shared by all three hosts | `plugin/RenderLevers*`, `DrawReplay*`, `PerfProbe*` | started |
| GF | the game-side forks and the audit's small defects | `game/SquadWeapons`, `Le2Squad`, `PowerWheel`, the traversal observers, `ScreenReticle`, `HudPieces`, `SystemSettings` | started |
| C2 | VRIK's rest for both sequels | `game/HandAim`, `ArmIk`, `Weapon`, `Sights`, `Holsters`, the reload, `engine/BonePose`, `core/ik` | resumed |
| PLAN | this page | `docs/trilogy-plan.md`, the banners | this landing |
| the other session | ME3's galaxy projector, then ME2's | `game/Le3Galaxy*`, `plugin/GalaxyHologram`, `plugin/Le3SceneCapture` | running |

## The work list

**State words**, one per game on each entry's State line:

| word | meaning | counts as open |
|---|---|---|
| `done` | nothing left for this game | no |
| `headset` | seen working by script; only a check in a physical headset is left | no (it is in [Headset-only checks](#headset-only-checks)) |
| `verify` | the code is there; a check in the game by script is left (the ledger's **U**) | yes |
| `build` | code is left to write: an adapter, or a shared refactor that must leave the game unchanged | yes |
| `check first` | find out whether the game has the problem before porting anything | yes |
| `later` | deliberately left; the entry says why | no |
| `n/a` | the game lacks the mechanic | no |

**Totals** (counted from the State lines below): 53 features. Open (verify, build or check first): **LE1 9, LE2 43,
LE3 47**.

| game | done | n/a | headset | verify | build | check first | later |
|---|---|---|---|---|---|---|---|
| LE1 | 42 | 2 | 0 | 0 | 9 | 0 | 0 |
| LE2 | 0 | 0 | 7 | 10 | 25 | 8 | 3 |
| LE3 | 0 | 0 | 3 | 11 | 29 | 7 | 3 |

LE1's nine are shared refactors that must leave ME1 unchanged; ME1's own product work is in
[ROADMAP.md](../ROADMAP.md) "Now" and mapped under [Shipping](#me1). No feature is `done` on a sequel: even the ones
seen working by script still owe a headset check. Ids in brackets are ledger rows (R = renderer table, G = gameplay
table) and its numbered Forks.

### Foundations

These unblock the most: almost every other entry gets smaller once they land.

#### T01 One seam vocabulary and renderer capabilities [Seams; Forks 14; R32, R33, R34; G53]
- **State:** LE1 build · LE2 build · LE3 build
- **Lives in:** `src/halcyon/MevrSeams.h` (26 functions in six groups: session and frame, view, eye image, composition,
  input and threads, capabilities), implemented once in the host; `RendererCaps` in `plugin/Renderer.hpp`.
- **Left, every game:** each Halcyon tree includes the one header in place of its own `mevr_bridge.h` signatures (they
  differ today in `OnViewBuilt`, `WrapSessionFunctions`, `QueueProcessEvent`, `HudPanelDraw`, `CopyIntoImage`); host
  modules and `BuildServices` ask a capability bit in place of `#if defined(MEVR_WITH_HALCYON_LEx)`. Old names keep
  working as thin aliases while callers move.
- **Closes when:** the generated seam table in the ledger shows one vocabulary; LE1's same-pose snapshots are identical
  before and after; in a run, a key typed in another window toggles nothing on ME2 and ME3 (R33), and LE3's host log
  has no `process event: off the game thread:` (R34).
- **Waits for:** nothing. **Who:** U1.

#### T02 One sequel bridge, one overlay composition, one menu glue [Forks 1, 2, 13; R20; R17]
- **State:** LE1 build · LE2 build · LE3 build
- **Lives in:** one `OverlayFrame` (U1). Since lane U2 (2026-11-16): `plugin/HalcyonSequelBridge.cpp` for the seams the
  two sequel bridges repeated (session, frame, view, recenter, controller merge, cinema screen, galaxy settings),
  `plugin/SequelOverlays.cpp` as the one composition for both sequels (capabilities plus each game's adapter,
  `sequel_bridge::game` in `HalcyonLe2Bridge.cpp` / `HalcyonLe3Bridge.cpp`); LE1 still composes in
  `plugin/HalcyonOverlays.cpp`. research/trilogy-seams.md "One sequel bridge", "One overlay composition". Since lane U3
  (2026-11-16, rule 32): ME3's adapter answers like ME2's for our menu, the ring and the traversal cue (three facts gone),
  and one recenter rule holds for all three games ("ME3 as ME1", "One recenter rule").
- **Left, every game:** LE1 onto the shared composition one layer at a time, each with a same-pose pair (the note lists
  the five blocks); LE1's label quad becomes `HudPieceLayers::Interaction` with ME1's default crop; `plugin/MenusLe2`
  renamed `Menus` and called by LE1's bridge too; one `PlacePanel` (ME2's renderer takes `panelsInRoom`). (ME3's merged
  bridge ran with a save loaded on 2026-11-16, lane U3: Save 31, replayed hands, a recenter, our menu; "Lane U3's checks".)
  `plugin/HandReticle` no longer falls back to the controller's aim on a stale aim ray, all three games (dd8e0ca8; same note, "Found").
- **Closes when:** LE1 same-pose snapshots show the same layers at the same places; an LE2 and an LE3 snapshot set shows
  every layer the old compositions drew; an LE3 run with `[Debug] swapchainCap = N` refuses and recovers as LE1's did on
  2026-11-06 (R17).
- **Waits for:** T01. **Who:** U2; U3 (the ME3-only facts, the recenter rule).

#### T03 One eye-image pipeline: colour, tonemap, FSR, the HDR display answer [Forks 7, 8; R13, R14, R15, R16]
- **State:** LE1 V (same pose identical) · LE2 V (colour, FSR) · LE3 V colour, FSR U (2026-11-16, lane EY: research/27
  section 9)
- **Lives in:** `core/vr/Colour`, `core/vr/Upscale`, `plugin/Colour`, `plugin/Upscale`, `plugin/EyeCopy`,
  `plugin/HdrDisplayPolicy`, seam `MevrSeams::EyeImageSize`. One colour, tonemap and FSR core and one display-answer
  policy in all three hosts; the sequels reach them from `EyeCopy` (`CopyIntoImage`), LE1 from its blit's own
  `EyeColour*`/`EyeUpscale` seams (one core, two call shapes: research/27 9.3).
- **Left:** **ME3:** FSR seen in a run with a backbuffer below the recommended eye (the preview's 3072x1728 is wider);
  its HUD panel's colour once the panel is on. **LE1:** its FSR path in a run whose render is below the eye (the fake
  headset's eye is smaller than LE1's render); optional, its blit onto `CopyIntoImage` with a fill argument after a
  same-pose check of its four blit paths. **The sequels:** the render-size and Performance rows (greyed until the owner
  has seen them). **The owner:** the picture defaults for dark scenes (research/27 9.6).
- **Closes when:** LE1's eye snapshots are unchanged (they are, neutral and Quest); an ME2 and an ME3 colour-profile
  snapshot A/B shows the profile applied (both: within one 8-bit step of the CPU reference); an eye rendered below the
  headset's size comes out FSR-scaled on each sequel (ME2 yes, ME3 left); ctest `mevr-eye-copy-warp`, `mevr-colour-shader`
  and `mevr-upscale-shader` pass on the one stage (they do: the WARP test runs the real FSR passes).
- **Waits for:** T01. **Who:** EY.

#### T04 Engine access on our reflection layer; no LExSDKv2 in a players' build [G52, G50, G55; G02's fallback]
- **State:** LE1 done · LE2 done · LE3 verify
- **Lives in:** `src/ue3`, `engine/GameAdapter.hpp`, `engine/Addresses.hpp`, `engine/ReflectCheck`. How it was done and
  how to check a build: research/34 section 6.4.
- **Done 2026-11-16 (seen in the game: ME2 development `reflect: 637 of 637`, ME2 monolithic to gameplay with `reflect:
  no LExSDKv2 cross-check in this build`, ME3's engine probe `621 of 621`; research/34 section 6.4):** no module reads through `engine::legacy` on any game
  (HeadTracking, HandAim, ConsoleRequest, Reflections, `engine::World`, Mod.cpp's gate; `engine/Legacy` is the LE2
  diagnostic host's only); `MEVR_LE2_LESDK` / `MEVR_LE3_LESDK` (default = `MEVR_HOT_RELOAD`), so a monolithic build of
  all three games configures without the LExSDKv2 submodule and its ASIs hold none of it; `tools/package-release.ps1`
  refuses a tree whose LE1, LE2 or LE3 ASI links it; `game/CallTrace` runs on every game; ME3's `Engine.Actor.Velocity`
  handle is declared (`game/BodyFollow`).
- **Left, ME3:** a monolithic ME3 build run to gameplay, and its head tracking on the layer in a renderer run.
  **Both:** `kModulesOnLayer` stays false on both: the 10 `ready` modules left
  are ME1 features on ME1's names and addresses (BodyBounds, CoverAcquire, VoicePlace, SoundPlace, AuthoredMoment,
  ConversationFlow, ConversationWorld, GalaxyDump, GalaxyMap, SquadAck); each moves to `kEveryGame` and `Up()` with its
  own feature's entry, not here.
- **Closes when:** a monolithic LE2 and LE3 build links no `LESDK-LEx` and logs `reflect: no LExSDKv2 cross-check in this
  build`; the development builds still log `reflect: N of N checks agree`; the generated module table has no "LExSDKv2"
  note left.
- **Waits for:** nothing. **Who:** nobody (T04's lane ended 2026-11-16).

#### T05 One test runner and one save-tool interface [Forks 10; G54; R25]
- **State:** LE1 build · LE2 build · LE3 build
- **Lives in:** today `tools/fake-headset-session.ps1` (LE1), `tools/le2-halcyon-run` (ME2, with an ME3 mode),
  `tools/qa-le2` and the diagnostic LE2 host (`MEVR_LE2_DIAGNOSTIC`), three launchers, and two save contracts
  (`tools/le1-save.py` rewinds; `tools/le2-save.py` recipes and receipts, which `le3-save.py` imports).
- **Left:** one runner with a `-Game` parameter over the one lock; one save-tool interface that does both rewind and
  recipe; the diagnostic host retired once the Halcyon host covers its observers. Two LE1 defects the ME2 notes found and
  nobody fixed: `fake-headset-session.ps1` deploys before it backs up, and `console.request` names no game, so a file
  meant for one game can be consumed by another (le2-from-le1.md sections 3.3, 3.5 and 5). **ME3:** an accepted combat
  fixture (the six existing saves have none: research/le3-combat-fixtures.md), then companion, ladder/vault, melee and
  conversation checkpoints; save rewinds beyond resource edits. **ME2:** a pre-Veetor save for the interrupts (T15), a
  reachable Normandy fixture for the galaxy (T42).
- **Closes when:** the same command with `-Game LE1|LE2|LE3` backs up, launches, loads a save by script, runs a script,
  restores with zero differences and releases the lock; a console request for the wrong game is refused by name.
- **Waits for:** nothing; the ME3 combat fixture blocks every `verify` in ME3's combat entries (T22-T26, T34, T44-T46).
  **Who:** nobody yet.

### Renderer and performance

#### T06 Stereo: two passes and per-eye pose history [R01, R27]
- **State:** LE1 done · LE2 headset · LE3 headset
- **Lives in:** upstream's SFR in `src/halcyon/le2` and `le3` (`calcview_hook.cpp`), `core/vr/StereoPoseTag.hpp`,
  `core/vr/EyePublication`.
- **Left:** physical fusion in a headset on both sequels. The checklist had no ME3 line until this plan added one.
- **Waits for:** the owner's headset. **Who:** nobody (headset).

#### T07 The perf probe on every host [R26]
- **State:** LE1 build · LE2 build · LE3 build
- **Lives in:** `plugin/PerfProbe`, `core/vr/FrameProbe`, `tools/perf-probe.ps1` and `.py`.
- **Left:** `PerfProbe` includes Halcyon LE1 headers: split those out (the LE1 part of the work). **ME2, ME3:** the
  seams at each renderer's Present, `xrWaitFrame` and `xrBeginFrame`.
- **Closes when:** `tools/perf-probe.ps1` produces frames, ticks and a probe JSON on each game and `perf-probe.py` names
  the limiting thread; on ME3, mirror cadence 1 against 8 on one fixture (me3-overall-goal-handoff.md section 4).
- **Waits for:** nothing (T01 makes the seams one declaration). Every performance entry below is unmeasurable on the
  sequels without it. **Who:** PL.

#### T08 The render levers: one-Draw stereo, the replay, Umbra, kept shader states [R02, R03, R04, R05, R07, R08, R09]
- **State:** LE1 done · LE2 build · LE3 build
- **Lives in:** `plugin/RenderLevers`, `plugin/DrawReplay`, `core/vr/ScreenMatrix`, `core/d3d11/ShaderReflect`. The
  recorder and replay are already game-independent; what is per game is an address table and a seam.
- **Left, ME2 and ME3:** first `keepBoundShaderStates` (R08: the cheapest win, about 5 ms cold on LE1; sample the sequel's
  render thread for `CreateInputLayout` first with `tools/sample-render-thread.ps1`). Then one-Draw (R02: the render
  entry, finish, tail start and deleter addresses, the views field and clone stride, a `RecordPass1View` in the SFR
  branch, a skip of the second Draw) with the replay's `Record*` seams (R03). Then find out whether the sequels run Umbra
  at all (R07) before `umbraEyeMatrix`, `umbraQueryWait` and `minScreenRadiusPx` (R09), and whether their shaders take
  `ScreenToShadowMatrix`/`ScreenToWorld` (R04).
- **The second eye (2026-11-16, lanes SP and SP2):** the second eye's 4-5 ms is Umbra's results read spinning for
  queries that are never ready; on a quiet machine after life-size stereo the recommendation is ME2
  `umbraShareResults=1` with `sfrReplayRetry=0`, ME3 `umbraShareResults=1` (render work 11.0 -> 5.8 and 11.5 -> 4.8 ms,
  the second eye still occluded, the pictures today's), prepared unlanded on branch `sp2-second-eye-defaults` for the
  owner's yes (research/sequel-render-levers.md "The batch before the headset test"). One-Draw stays "not now".
- **Closes when:** an A/B under a replayed head at one heavy fixture per game (`tools/measure-levers.ps1`) shows each
  lever's cost and a correct second eye (`tools/eye-diff.ps1`).
- **Waits for:** T07 to measure; R2 and R3 for the addresses. **Who:** R2 and R3 (addresses), PL (the shared module and
  the measuring).

#### T09 Life-size stereo and each eye's own frustum [R23, R06]
- **State:** LE1 done · LE2 headset · LE3 headset (both seen working by script on the fake headset, research/30 9.3)
- **Lives in:** `plugin/MetricStereo`, `core/vr/MetricStereo` (all three hosts since 2026-11-16, lane LS); the common seams
  `PlanStereo`, `NoteStereoSubmit`, `LifeSizeStereo` (`src/halcyon/MevrSeams.h`); render-side LE1's render_hook.cpp
  (`ApplyMetricEyeOffset`, `ApplyMetricFrustumSEH`), the sequels' one header `src/halcyon/MevrMetricView.h`; per-game
  facts in each tree's `MevrPlanStereo` (me2_xr.cpp) and the baselines (le2 `ApplyMevrBaseline`, le3 `LoadValues`).
- **Done (research/30 section 9):** upstream on the sequels read, modelled and measured (ME2 -0.9 degrees per eye
  divergent, as ME1's upstream; ME3's crossed shift put infinity near 1.2 m); both sequels now render each pass from its
  eye's real place at 100 uu per metre, no convergence, the lean from the eyes' centre at 1:1 forward, each eye through
  its own frustum in a window widened to hold both eyes (ME1's), pinned as their only configuration (`[MEVR-STEREO]`
  lines; `stereo.measure` or `[Debug] upstreamStereo = 1` brings upstream back for a measurement). ME2 measured +2 to
  +7 px on far scenery, the HUD panel unchanged; ME3 +0.66 degrees per eye in a 2.8 m room (expected 0.65), in mode 1
  while its game UI is still in the eyes (mode 2 follows once ME3's HUD panel takes the UI).
- **Left:** the owner's headset check (depth, size, comfort, nothing popping at an eye's outer edge on an asymmetric
  headset, which the fake headset can't show); the wider culling window's cost (lane SP's levers).
- **Closes when:** `stereo-check.py` reads parallel far scenery and a pistol at 0.5 m reads 0.5 m on each sequel, and the
  owner's headset check (notes/headset-checklist.md section 8).
- **Waits for:** the owner's headset. **Who:** lane LS (2026-11-16).

#### T10 Frame pacing for streamed headsets [R24]
- **State:** LE1 done · LE2 build · LE3 build
- **Lives in:** `plugin/QuestPacing`; seams `EarlyFrameWait`, `FullFrameLoop`, `QuestPacing*`.
- **Left, ME2 and ME3:** an `xrWaitFrame` seam in each renderer's `RunFrame`; the sequels run upstream's pacing.
- **Closes when:** a streamed Quest holds full rate after a load on each sequel (a headset check; the null driver can't
  show it).
- **Waits for:** T01, T07. **Who:** nobody yet.

#### T11 The engine's settings, applied by the mod [R11, R12]
- **State:** LE1 done · LE2 done · LE3 done (2026-11-16, lane SP2: the close condition below met)
- **Lives in:** `game/SystemSettings` (`scale set` after each level change, each boolean through its opposite).
- **Left:** ~~gate `LightEnvironmentShadows False` on the host's replay~~ done 2026-11-16 (GF2, HostServices v43
  `secondEyeIsReplay`; elsewhere the player's own value). On ME2 the key alone changed nothing visible at Freedom's
  Progress; the ground shadows there are `DynamicShadows`, which ME1's value keeps off on every game: the owner's
  answer of 2026-11-16 evening ("The second eye and dynamic shadows"). ~~Lane SP measures each second-eye option per
  game with and without them~~ done (lane SP2, research/sequel-render-levers.md "The batch before the headset test"):
  with each recommended option the shadows are right in both eyes on both sequels (Miranda's and her gun's at Freedom's
  Progress, Shepard's on the Normandy), about 0.4 ms; on or off is the owner's call. ~~ME3: no receipt yet that `scale
  set` applies at all~~: it does (the log's `system settings: scale set DynamicShadows True ...` and the frame changes in
  both eyes). Still unshown on ME3: `MotionBlur` and `FoliageDrawRadiusMultiplier` (the ledger's R12).
- **Closes when:** an ME2 and an ME3 snapshot show character shadows; ME3's log has the `system settings:` lines and an
  A/B of `DynamicShadows` changes the frame. Met on both (stretches `LE2-20261116-201015`, `LE3-20261116-202316`).
- **Waits for:** nothing; once T08 brings the replay to a sequel the gate follows the capability. **Who:** GF.

#### T12 The motion-blur guard [R10]
- **State:** LE1 done · LE2 check first · LE3 verify
- **Lives in:** `plugin/MotionBlurGuard`, with a per-game proxy-pair table in `engine/Addresses.hpp`.
- **Left, ME2:** blur is off only through T11's `MotionBlur False`. Take a head-turn snapshot pair looking for one-eye
  smear before porting anything. **ME3:** seven proxy pairs guard the recenter; other scenes and the stale-copy case are
  open (research/le3-recenter-transition-failure.md).
- **Waits for:** nothing. **Who:** nobody yet.

#### T13 Recenter, and the tilted start removed [R28]
- **State:** LE1 done · LE2 build · LE3 build
- **Lives in:** `core/vr/Recenter`, `plugin/RecenterTilt`, seam `RecenterOrientation`.
- **Left, ME2 and ME3:** both recenter yaw-only and never call `RecenterOrientation`, so SteamVR's upside-down start
  reaches them (`RendererLe2::RecenterTiltDeg` answers 60 and nothing reads it).
- **Closes when:** a replayed tilted base space is levelled by a recenter on each sequel (`[MEVR-RECENTER]` in the log).
- **Waits for:** T01 (the seam is in its view group). **Who:** U1 brings the seam; the call in each sequel has nobody yet.

#### T14 The HUD panel, the hand reticle, the game's crosshair hidden [R18, R19]
- **State:** LE1 done · LE2 headset · LE3 verify
- **Lives in:** `plugin/HudPanel`, `plugin/HandReticle`, `game/ScreenReticle`.
- **Left, ME3:** the gameplay HUD is on the panel since 2026-11-16 (lane HU: `hudPanelGameplay`, ME1's `Hud()` defaults;
  the stereo plan in mode 2 by itself; research/le3-hud-panel.md); its combat HUD is unseen (no combat fixture). The crosshair
  isn't hidden: `hideScreenReticle` isn't served and `ScreenReticle` stands down on ME3 (ME3 has no
  `BioSFHandler_Reticule`), so in combat it would be drawn on the panel.
- **Closes when:** an ME3 gameplay snapshot has the HUD on the panel layer and clean eyes, with the ring and no crosshair.
- **Waits for:** T02 for the layer order. **Who:** GF has `ScreenReticle`'s latent default; HU (2026-11-16) the ME3 HUD panel; ME3's crosshair has nobody.

#### T15 The cinema screen, its backdrop, the fade, and interrupts [R21]
- **State:** LE1 done · LE2 verify · LE3 verify
- **Lives in:** `plugin/CineScreen`, `core/vr/CineScreen`, `plugin/CinemaBackdrop`, `plugin/TransitionFade`,
  `plugin/SequelCinema`; interrupts through `PadMerge::ConversationTriggers`.
- **Left, ME2:** the timed interrupt at Veetor's door: a replayed trigger inside the prompt's window takes it and one
  outside doesn't. It needs a save that normal-loads before Veetor (research/le2-interrupt-acquisition.md). **ME3:** no
  separate-eye cinema acceptance yet: snapshots in a conversation with ME2's run script, then its interrupt windows.
- **Waits for:** T05's fixtures. Part of ME2's first release (M5). **Who:** nobody yet.

#### T16 Scene-capture plumbing [R31; Forks 9]
- **State:** LE1 build · LE2 build · LE3 build
- **Lives in:** LE1's hooks inside `plugin/RenderLevers`; ME3's own `plugin/Le3SceneCapture`; shared already:
  `plugin/SceneCaptureTarget.hpp`, `core/vr/SceneCaptureScope`, `game/SceneCaptureRig`.
- **Left:** one scope helper parameterised by each game's addresses, which LE1 and ME3 both call (the LE1 part is a
  refactor). **ME2:** its capture hooks; the scope capture (T24) and the galaxy window (T42) come with them.
- **Closes when:** a scene capture renders into its own target without taking an eye on each game (LE1's symptom:
  research/47 "Why a capture took an eye").
- **Waits for:** the other session's galaxy work on `Le3SceneCapture`, and PL's on `RenderLevers`. **Who:** split between
  them; the unifying step has nobody yet.

#### T17 Performance modes and the first start's eye size [R35]
- **State:** LE1 done · LE2 build · LE3 build
- **Lives in:** `core/vr/PerformanceModes`, `plugin/MainMenuSettings`, `main_menu::FirstStartEyeSize`.
- **Left, ME2 and ME3:** the rows stay greyed (rule 15) until the levers they bundle exist there.
- **Waits for:** T03 (FSR), T08. **Who:** nobody yet.

#### T18 The front end and the presentation hold [R22]
- **State:** LE1 done · LE2 check first · LE3 check first
- **Lives in:** `plugin/PresentationHold`. A fix for Halcyon ME1's start-up flips; port only if the sequels flip.
- **Left:** snapshots from launch to the title on each sequel.
- **Waits for:** nothing. **Who:** nobody yet.

#### T19 The body's bounds for the culling camera [R29]
- **State:** LE1 done · LE2 check first · LE3 check first
- **Lives in:** `engine/BodyBounds`. Its cause on LE1 is Halcyon LE1's view offset; ArmIk's bounds flag already runs
  everywhere.
- **Left:** LE1's repro on each sequel: look down with the head turned
  ([the gotcha](knowledge/gotchas/culling-camera-ahead-of-the-eye.md)). Port only if the body drops out.
- **Waits for:** T21 on ME3 for a body to look at. **Who:** nobody yet.

#### T20 The spectator mirror [R30]
- **State:** LE1 done · LE2 later · LE3 later
- **Lives in:** `game/Spectator`, `plugin/Spectator`.
- **Why later:** no plan names it and no player asked; it is a desktop view, not the VR experience. Take it with T02
  if a recording needs it.

### Body and weapons

#### T21 The body: arm IK, fingers, body follow, body scale, floor lock [G01, G03, G13, G14, G15]
- **State:** LE1 done · LE2 build · LE3 verify
- **Lives in:** `game/ArmIk`, `engine/BonePose`, `core/ik`, `game/Fingers`, `game/BodyFollow`, `game/BodyScale`,
  `plugin/FloorLock`. Guide: [vrik-porting-guide.md](vrik-porting-guide.md).
- **Left, ME2:** the floor lock's correction is measured on Save 4 (the final eye is the STAGE eye, 1.7526 m) and held
  through the vault's root-motion steps (`vr::floor_lock::MoveHold`, research/le2-traversal.md "The floor lock through the
  vault"; arm IK across the steps is on main since dc7c6f98); the calibration's manual height isn't wired; fingers and
  body scale have no in-game check. **ME3:** "the legs keep the run" is off (no Velocity read: declare the handle `kEveryGame`); fingers, body scale
  unverified. **Both:** body scale needs a recorded floor fixture (the fake headset's floor is at its head).
- **Closes when:** snapshot `mod.armIk.hands` puts each wrist on its controller through a vault; `mod.fingers` follows
  a replayed trigger and grip; the ME3 null-height run's script passes on ME2's Save 4.
- **Waits for:** nothing (T04 declared the ME3 Velocity handle, 2026-11-16: see it in a walk). **Who:** C4 (C2's successor).

#### T22 Aim and the gun: hand aim, the drawn gun, the two-handed grip [G02, G04, G05]
- **State:** LE1 done · LE2 verify · LE3 verify
- **Lives in:** `game/HandAim`, `game/Weapon`, `game/SupportGrip`, `plugin/PadMerge`.
- **Left, ME2:** the support grip has only its unit test: a replay with the left grip in the zone, then snapshot
  `supportGrip`. **ME3:** hand aim, the weapon frame (the SMG as its own type) and the grip all need the combat fixture.
- **Closes when:** shots land along the barrel with the controller turned 30 degrees off the view, through a one- to
  two-hand change, on each sequel.
- **Waits for:** T05's ME3 combat fixture. **Who:** C2.

#### T23 No camera recoil: the view level, the gun kick, the shakes off [G09, G10, G11]
- **State:** LE1 done · LE2 verify · LE3 build
- **Lives in:** `game/ViewLevel`, `game/GunKick` (`core/ik/GunKick`), `game/CameraShake`.
- **Left, ME2:** the aim-assist zeroing is unmeasured, and which gun shakes exist is unverified. **ME3:** none of the
  three runs: `ViewLevel` is `#if LE1 || LE2`, the gun kick has no shot source, `CameraShake` is a stub. ME3 has all 29
  names `ViewLevel` uses (`python tools/le2-name-audit.py --game LE3 game/ViewLevel`): a handler on ME3's recoil call
  gives the level view and the kick's shot source together.
- **Closes when:** the view's pitch and roll stay 0 through 20 shots and `gun kick: first kick` logs, on each sequel.
- **Waits for:** T05's ME3 combat fixture. **Who:** nobody yet (nearest: C2).

#### T24 Sights and scopes [G06, G07]
- **State:** LE1 done · LE2 build · LE3 build
- **Lives in:** `game/Sights`, `core/vr/Ads`, `plugin/SightLayers`, `game/ScopeParts`, `game/ScopeCapture`,
  `plugin/ScopeCaptureSource`. Plan: research/le2-le3-vr-optics.md "LE2 implementation and first headset run", steps 1-3.
- **Left, ME2 and ME3:** the mod's side is ME1's path on every game since 4e6cc18b (rule 30; research/le2-vrik.md "The
  sights on the sequels, mod side"): the alignment assist, the shots' convergence, a scope's aimed fire, ADS and the
  lens part's hide. Seen on ME2 by script (`tools/le2-halcyon-run/sights.ps1`, Save 4, 2026-11-16): the pistol's dot in
  the right eye and the shots on what the sight line hits; the sniper rifle's ADS, its picture over the scope, its
  `Scope_Eye` sight bone and aimed fire 10.0 -> 0.0. Left: the scope's picture comes from the eye copy (`ScopeCapture`
  is compiled out and `setScopeCapture` isn't served: T16); per-model optic profiles (ME1's `vr::sights::OpticFor` is
  keyed by mesh name, and ME2 reuses ME1's names for other models: its Mantis is `WPN_SNPa_MDL`) need a game key and
  measured numbers per model; on the fake headset ME2's pistol ring stands a hand's width left of the gun (the eyes take
  the monitor's shape there: a headset check). **ME3:** all of it in a game; aim modes and installed optic mods change
  the profile.
- **Closes when:** a scoped shot hits the sight line and the `sights:` lines name the game's own sight bones, with a
  sniper-rifle fixture per game (ME2's: done 2026-11-16, but for the scope capture).
- **Waits for:** T16 for the scope capture on ME2; T05's ME3 fixture. **Who:** C4.

#### T25 Holsters, and only holsters draw [G08]
- **State:** LE1 done · LE2 verify · LE3 build
- **Lives in:** `game/Holsters` (one module, each game's names in its adapter), `core/ik/BodySlots`.
- **Done on ME2** (2026-11-16, research/le2-vrik.md "Holsters on the sequels"): the draw (`BioPlayerController.SwitchWeapon`,
  then `BioPawnBehavior.SetActionState(1)`), the holster (`SFXGameModeDefault.TryHolster`), the right grip holding the gun
  (no RB from it; RB is the right stick click), and only holsters draw (rule 31: the trigger, the aim trigger, reload and
  the weapon cycling refused with the weapon away, `BioPlayerInput.IsCombatEnabled` answered false for their call).
  Seen by script: the assault rifle at the right shoulder and the pistol at the right hip drawn and holstered, four
  trigger pulls with the weapon away refused, three vaults with the right grip holding the pistol; ME1 at the same pose
  unchanged. **The SMG's slot** (lane WP, 2026-11-16, research/le2-vrik.md "A body slot for the SMG"): `thighRight`, the
  front of the right thigh, a fifth `core/ik/BodySlots` slot (off on ME1, which has no SMG); the default, a recommendation
  until the owner's headset check; not seen in the game (Save 4's Soldier carries none). **ME3** (lane WP, research/le2-vrik.md
  "ME3's holsters, from its own script"): ME3 has no holstered state on a mission and its TryHolster only leaves cover, so
  TryHolster is never called there; the holster is the game's own put-down into no weapon
  (`SFXInventoryManager.SetCurrentWeapon(None)`) behind `[Holsters] le3PutAway`, **0 by default** (unverified: no ME3 save on
  the machine loads on a mission with a weapon), and only holsters draw is ME2's rule with "away" = weapons carried, none
  equipped. ME2's release grace seen since (back after 0.20 s, holstered after 0.93 s). **Left, ME2:** the SMG's slot with a
  class that carries one. **ME3:** the combat fixture, then holsters.ps1 -Game LE3
  with `le3PutAway = 1`: draw, put away, draw again, four trigger pulls with the gun away, no stuck state; then its default.
- **Closes when:** a replayed grip at the hip slot draws the pistol and letting go there holsters it, on each sequel.
- **Waits for:** T05's ME3 combat fixture. Part of ME2's first release (M3.4). **Who:** WP (after C4).

#### T26 The reload by hand [G41]
- **State:** LE1 n/a · LE2 verify · LE3 build
- **Lives in:** `game/Reload` (the adapter), `core/vr/PhysicalReloadRequest.hpp` (the coordinator, 18 tests) and `core/vr/ReloadGesture` (the gesture:
  the pouch, the clip held, the receiver entered from outside, Overlord's contact invariants; unit-tested with the
  coordinator, 2026-11-16, research/le2-vrik.md "The reload by hand: the gesture's core"), with `core/ik/BodySlots` for
  the clip pouch and `core/ik/Vent`'s receiver zone. ME1 has heat, not clips: its counterpart is the vent.
- **Built** (lane WP, 2026-11-16, research/le2-vrik.md "The reload by hand: the game's side"): `game/Reload`, one module for
  both sequels (off on ME1): the pouch a left-hand body slot (`[Reload] pouch`, the left hip), the receiver `[Vent]`'s zone, the
  coordinator's identities and grant, the request through the button's own `SFXGameModeDefault.TryReload` (state-aware, never
  DoReload or R), the game's ammo observed for the feedback. **Seen on ME2** (Save 4): 12/0/60 -> one shot 11/1/60 -> the
  replayed gesture 12/0/59 (`reloaded after 1.50 s`), a full magazine refused; the game's R and automatic reload stay.
- **Seen:** nothing yet (no clip in the hand or going in); the game's assets, the clip prop and the gesture's options for
  the owner to choose (the hip pouch rejected 2026-11-16): research/thermal-clips.md.
- **Left:** empty, no-reserve and cancelled reloads; contact geometry from Overlord's reviewed pieces (research/
  le2-overlord-reload-reuse.md); ME3's run with a combat fixture.
- **Closes when:** the Freedom's Progress pistol baseline (12/0/60, 11/1/60, 12/0/59) is reproduced by a replayed
  gesture instead of `R`; empty, partial, no-reserve and cancelled reloads conserve ammo; ME3 the same with its fixture.
- **Waits for:** T05's ME3 combat fixture for ME3. Part of ME2's first release (M3.5). **Who:** WP (after C4).

#### T27 Guided calibration and hand calibration [G16]
- **State:** LE1 done · LE2 headset · LE3 headset
- **Lives in:** `plugin/Calibration`, `game/BodyCalibration`, `game/HandCalibration`.
- **Left:** a headset run on each sequel: the fake headset has no interaction profile, so nothing by script can show it.

#### T28 The game's camera collision off in first person [G12]
- **State:** LE1 done · LE2 check first · LE3 check first
- **Lives in:** `game/CameraCollision`.
- **Left:** find out whether Halcyon ME2/ME3's first person pulls back near geometry (LE1's symptom: the eye behind the
  knees, research/69). Port only if it does. **Who:** nobody yet.

#### T29 Weapon drawn like exploring: the constant eye, linear walking, the draw rate [G18]
- **State:** LE1 done · LE2 build · LE3 build
- **Lives in:** `game/CombatStance`, `core/vr/CombatStance`, seam `ConstantFpEye`.
- **Left, ME2 and ME3:** port per item, the linear walk and stick first (the one players notice: research/38). ME2 lacks
  `BioPawnBehavior`'s ramp fields: decide per game what stands in for them.
- **Closes when:** `movement: response:` shows a run within 0.1 s of the push and half a push walking, on each sequel.
- **Waits for:** nothing. No plan covered this on ME2 before this page. **Who:** nobody yet.

### Movement

#### T30 Comfort: turning, the vignette, walking toward the hand; one movement-admission query [G17; Forks 11]
- **State:** LE1 done · LE2 verify · LE3 build
- **Lives in:** `game/Comfort`, `core/vr/Comfort`, `plugin/Vignette`.
- **Left, ME2:** its protected run timed out before the game started: two right-stick flicks, a hand-yawed walk, a menu
  opened with the sticks held ([le2-m6-native-checks.md](le2-m6-native-checks.md)). **ME3:** the same check, and its
  movement-admission test exists six times (`Comfort`, `ArmIk`, `Posture`, `Sprint`, `Melee`, `Le3Traversal`): one
  `game/Player` query that returns the reasons, which ME2 can use too.
- **Waits for:** nothing. **Who:** nobody yet.

#### T31 Cover without snapping, and the physical crouch [G39, G40]
- **State:** LE1 done · LE2 verify · LE3 verify
- **Lives in:** `engine/CoverAcquire`, `game/VrCover`, `core/vr/VrCover`, `game/Posture`; ME3's facts in
  `game/Le3Cover.inc`.
- **Left, ME2:** recheck crouch, protection and the vault on the current build with standing and seated calibration.
  **ME3:** no-snap is only a diagnostic opt-in, and the damage rule and crouch have no in-game result.
- **Closes when:** `mevr cover hit` shows no damage covered and full damage exposed, and A never snaps, on each sequel.
  ME2 also: the shields' wait before recharging is two thirds behind cover (`mevr cover regen`), whole exposed (the faster
  recharge, granted 2026-11-16 and built by lane GR: research/23 section 7.5; ME1 and ME3 have no such rule).
- **Waits for:** T05's ME3 combat fixture. See [question 2](#open-owner-questions). **Who:** lane GR (the recharge).

#### T32 Sprint [G58]
- **State:** LE1 headset · LE2 headset · LE3 headset (each run on the fake headset 2026-11-16: research/trilogy-sprint.md)
- **Lives in:** `game/Sprint`, `core/vr/Sprint`: one module and rule for the three since 2026-11-16 (lane GR, the owner's
  "Unified sprint", [question 6](#open-owner-questions)): StormOn runs, its intent (`bWantsToStorm`) is withdrawn by both
  hooks' post-handlers (the binding's hold reaches StormOn through ProcessEvent: the old script-hook skip never fired, so
  ME1 ran the game's own sprint until then), the faster run raises the run speeds the game reads. Per game: ME3's native
  admission, ME2 not in the game's own cover. No ME2 fact forces its native Storm.
- **Left:** the sprint's feel and heading in a headset on each game (ME1's faster run is new to players: it never ran before);
  ME3 off the Normandy; a sprint with a weapon drawn.

#### T33 Traversal: vault, climb, ladder, evade [sequel-only; Forks 4]
- **State:** LE1 n/a · LE2 build · LE3 build
- **Lives in:** `core/vr/Traversal`, `game/Traversal` (settings, commands, dispatch), `game/Le2Traversal.cpp` (ME2's
  observer since 2026-11-16), `game/Le3Traversal.cpp`, `plugin/TraversalCue`.
- **Left:** ~~ME2's block moves to `game/Le2Traversal.cpp`~~ done 2026-11-16 (GF2, ledger Forks 4 step 1); both observers
  on `ActionLifecycle` (steps 2 and 3); the admission stays per game. **ME2:** the owner's choices
  ([question 3](#open-owner-questions)). The two-handed hold vaults since 2026-11-16 (rule 8: the weapon's zoom cleared for
  the admission's one call; research/le2-traversal.md "The two-handed hold"); letting go of the support grip mid-move is
  sloppy (arm IK in third person for 0.7 s): the hold is kept. Arm IK across the move and the floor lock through it are done (T21): three
  vaults with every tick's wrists within 0.15 m of the controllers and no third-person frame, the eye riding the game's
  camera until it settles (research/le2-traversal.md "The floor lock through the vault"). ME3's moves reach the same hold
  through `traversal::Moving()`'s LE3 branch (not seen in a game); any other root-motion move must set
  `PawnSample::traversalMove` too. **ME3:** ladders need a lifecycle of their own (the shared kinds are None, Vault,
  ClimbUp); a stable horizon and camera hand-back; no traversal in authored seated scenes (rule 10); every gesture is
  opt-in until seen.
- **Closes when:** `tools/le2-halcyon-run/vault.ps1` still vaults three times on ME2 after the move, and an ME3 fixture
  shows a ladder, a vault and an evade each start, end and hand the camera back.
- **Waits for:** T05's ME3 fixtures. **Who:** GF (the observers); C4 (the arm IK, the floor lock, the two-handed hold).

#### T34 Melee by gesture [G43; Forks 12]
- **State:** LE1 build · LE2 build · LE3 build
- **Lives in:** `core/ik/Throwing` (the swing), LE1's routing in `game/Throwables`, ME3's in `game/Melee`.
- **Left:** LE1 moves onto `ik::melee::Gestures` with a per-game dispatch (a refactor). **ME2:** nothing exists and no
  plan covered it; its lead is `TryMelee`. **ME3:** light and heavy routes are staged and opt-in; all six class heavy
  actions, the storm variants and the cover and Atlas cases are separate contexts (research/le3-melee-audit.md).
- **Closes when:** one deliberate stroke makes one native request with its admission, impact and recovery seen, per game.
- **Waits for:** T05's fixtures. See [question 7](#open-owner-questions). **Who:** nobody yet.

#### T35 Vehicles: the Mako, the Hammerhead, the Atlas [G44]
- **State:** LE1 done · LE2 later · LE3 later
- **Lives in:** `game/Mako*`, `game/Cockpit*`, `core/vr/MakoControls`, `core/vr/CockpitPanel`.
- **Why later:** the owner put the Hammerhead in a pass of its own after ME2's first release (rule 13): the Mako's
  control geometry and panel code, not its physics writes. ME3 has no player vehicle; its Atlas and mounted guns need a
  safe fallback so they can't enter Shepard's input path, which is part of ME3's campaign pass (T52).

#### T36 The game's pause without window focus [G45]
- **State:** LE1 done · LE2 check first · LE3 check first
- **Lives in:** `game/FocusPause`.
- **Left:** check whether ME2 and ME3 freeze (`bPlayersOnly`) when the window loses focus. **Who:** nobody yet.

### Menus and the game's UI

#### T37 Our main menu, its pause, and the way into the game's menus [G19, G20, G21]
- **State:** LE1 done · LE2 verify · LE3 build
- **Lives in:** `plugin/MainMenu`, `plugin/MainMenuSettings`, `src/plugin/menu`, `assets/menu`, `game/MenuPause`,
  `game/GameMenuRequest`.
- **Left, ME2:** the Holster button was never exercised. **ME3:** the pause (`BioWorldInfo.PauseGame`) has no in-game
  result; the Holster button is greyed ("Not on ME3 yet", lane OM2, 2026-11-16) until lane WP's `holsters::PutAway()` and
  `[Holsters] le3PutAway` on by default (game/GameMenuRequest's `Run(Action::Holster)` then calls it on LE3).
- **Waits for:** T25 for Holster. **Who:** nobody yet.

#### T38 The omni-tool: the rail, the wrist tap, the gauntlet [G22]
- **State:** LE1 done · LE2 headset · LE3 headset
- **Lives in:** `plugin/OmniToolRail`, `plugin/RailArt`, `game/OmniTool`, `game/GameArt`.
- **Done, ME3 (lane OM, 2026-11-16):** the gauntlet is ME3's own client effect on Shepard's forearm; the rail in ME3's
  Mission Computer colours; the entries switch pages and Close returns to gameplay (B and its release). ME3's own frames,
  tabs and fonts are not read (bitmap fills, a font library that isn't a live object): research/le3-native-gui.md "The
  omni-tool on ME3".
- **Done, ME3 (lane OM2, 2026-11-16):** a replayed wrist tap and a fingertip press on the rail's Squad opened the Mission
  Computer and its squad record.
- **Left, ME3:** the headset look; Holster on our menu's top bar (G21, greyed until lane WP's ME3 holster call).
- **Who:** lane OM, OM2.

#### T39 The game's screens in the room and on the wrist, the pointer, the minigames [G23, G26, G24]
- **State:** LE1 done · LE2 build · LE3 build
- **Lives in:** `game/WorldUi`, `plugin/WorldUi`, `plugin/GamePointer`, `game/GuiCursor`, `core/vr/GuiEvents`,
  `game/UiWorldFigure`, `plugin/OmniPad`.
- **Done, ME3 (lane OM2, 2026-11-16):** `setWorldUi` served; the Mission Computer and the map on the wrist, War Assets in
  the room toward the War Terminal, the squad record (3D Shepard) on the native panel, the level behind ours (their UI world
  turned off while they stand), B and Close back to gameplay (research/le3-native-gui.md "The game's screens on ME3").
- **Left, ME2:** the squad member in the equipment screens (`UiWorldFigure`); ME2's hack and bypass minigames are
  different games from ME1's and have no VR treatment ([le2-validation-coverage.md](le2-validation-coverage.md) "Hacking
  and bypass"); research and loadout screens. **ME3:** a pointer click on a world screen reaches the right pixel as GUI
  events but ME3's movie mouse doesn't follow it (the page didn't open); behind a screen the eye is the GUI camera's, not
  first person's (no render hold; Shepard's body in view); the squad member (`UiWorldFigure`); the headset. ME3 has no
  bypass minigame.
- **Closes when:** a replayed wrist tap opens the rail, an entry opens the game's screen on the wrist, a ray click and a
  fingertip press land where they point, and B returns to gameplay, on each sequel.
- **Waits for:** T02. **Who:** nobody yet.

#### T40 Using things: selection by gaze, the label at the object [G27]
- **State:** LE1 done · LE2 verify · LE3 verify
- **Lives in:** `engine/SelectionGaze`, `game/Interact`, `HudPieceLayers::Interaction`.
- **Left:** since 2026-11-16 (lane HU) ME2's gaze hooks its target search (`0xAC0530`, ME3's contract) and both sequels
  show the label at the object by default with a band measured per game (research/40 section 6). Left on both: A's use
  after a gaze pick in the same run, two usable objects in different head and body directions, a long name, the headset.
- **Closes when:** with two usable objects in different head and body directions, the label and A agree, per game.
- **Who:** HU (2026-11-16).

#### T41 HUD pieces: the wrist status and the gun's panel [G28]
- **State:** LE1 done · LE2 headset · LE3 build
- **Lives in:** `game/HudPieces`, `plugin/HudPieceLayers`, `core/vr/HudPieces`.
- **Left, ME3:** `setHudPieces` isn't served, and `HudPieces` compiles LE1 field names in its `#else`: ME3's native
  wrist crop (me3-overall-goal-handoff.md, L3-4).
- **Waits for:** T14 on ME3 (on since 2026-11-16) and ME3's combat HUD. **Who:** HU (2026-11-16).

#### T42 The galaxy map and scanning [G25; Forks 3]
- **State:** LE1 build · LE2 build · LE3 build
- **Lives in:** `game/GalaxyMap`, `plugin/GalaxyHologram`, `game/SceneCaptureRig`, `core/vr/GalaxyHologram`; ME3's
  `game/Le3GalaxyCapture`, `Le3GalaxyRetention`, `Le3GalaxyProjector`; ME2's opt-in flat table in its bridge.
- **Left:** [galaxy-projector-handoff.md](galaxy-projector-handoff.md) "Remaining work" 1-5, in its order: validate ME3's
  final-target fix; hold the main view in the CIC; projector composition, labels and pointer; lifecycle and native
  gameplay (ME3's scan pulse, Reaper pursuit, retrieval); then the same flow on ME2 with its own adapter (its probes and
  mining: one launch per press). The shared pieces to make on the way: `vr::galaxy_map::ReadTableSettings`, one input
  lease for both sequels, one `capture_rig::StereoWindow::Step` that ME1's `StepWindow` and ME3's capture both call (the
  LE1 part).
- **Closes when:** the native map shows over the physical projector with the CIC in stereo around it, on each sequel;
  one probe becomes zero after exactly one launch on ME2.
- **Waits for:** T16. **Who:** the other session (ME3, then ME2).

#### T43 Controller input [G49]
- **State:** LE1 done · LE2 headset · LE3 headset
- **Lives in:** `plugin/PadMerge`, `game/GamepadMapping`, `plugin/XrHost`, `core/xr/Input`.
- **Left:** the both-sticks hold in a headset on each sequel (rule 16).

### Powers, the squad, voice, conversations

#### T44 Powers at the left hand [G29; Forks 6]
- **State:** LE1 done · LE2 verify · LE3 verify
- **Lives in:** `game/PowerWheel`, `core/vr/PowerWheel`, `plugin/PowerWheel`; ME3's facts in `game/Le3Powers.hpp`.
- **Left:** one sequel power reader (ME2's roster is read twice: `le2_squad::Powers` and `PowerWheel`'s own).
  **ME2:** a push that lands on a hostile, controller selection, evolved powers, other weapons (research/55 "Still to
  verify"). **ME3:** everything past the metadata: a cast, its target, its cooldown, a grenade refusal.
- **Waits for:** T05's fixtures. **Who:** GF (the reader); the checks have nobody.

#### T45 The squad's weapons wheel [G30, G57; Forks 5]
- **State:** LE1 build · LE2 build · LE3 build
- **Lives in:** `game/SquadWeapons`, `core/vr/SquadMenu`, `plugin/SquadPanel`, `core/vr/WheelCrop`.
- **Done 2026-11-16:** one tick for all three games (GF, Forks 5); ME2's names from `SFXPawn.GetPrettyName` and an
  equip seen on ME2 (GF2: Miranda's pistol, 6 of 6 pulls; ledger G30).
- **Left:** one `Tick` over a per-game roster and equip adapter (today `Le2SquadMenu.inc` re-implements LE1's `Tick` for
  both sequels; the LE1 part is a refactor). The sequels run the drawn "custom" style only; ME1's default is the game's
  own wheel cut onto a hand quad (`setWheelPanel`, LE1-only): port it or name the game fact that rules it out.
- **Closes when:** with two squadmates, every carried weapon can be given and the recipient's equipped weapon is seen to
  change; LE1's same-pose check passes. Detail for ME2: [M6 guide](le2-m6-implementation-guide.md) package B.
- **Who:** GF.

#### T46 Squad orders, placement by an arc, the squad's answer [G31, G32, G34]
- **State:** LE1 done · LE2 build · LE3 build
- **Lives in:** `game/SquadContext`, `game/SquadOrders`, `game/SquadPlace`, `game/SquadAck`, `core/vr/Squad*`; ME2's
  facts in `game/Le2Squad.hpp`.
- **Left, ME2 and ME3:** none of it has an in-game result. The acknowledgement doesn't exist on either
  (`SquadHandleChatterEvent` and `PlaySoundSet` are in neither game). Placement is opt-in (`[SquadOrders] place = 1`)
  where ME1's is on: flipping it is the owner's yes after a seen result (rule 19). **ME3 adds** healing and revive.
- **Closes when:** each living squadmate is seen to attack the target given, cast the power given, walk to the ring and
  hold, and come back on a double tap. Detail for ME2: [M6 guide](le2-m6-implementation-guide.md) package C and
  [le2-m6-squad-implementation.md](le2-m6-squad-implementation.md); for ME3: research/le3-squad.md.
- **Waits for:** T05's fixtures (ME2: Miranda and Jacob on Freedom's Progress). **Who:** nobody yet.

#### T47 Voice orders [G33]
- **State:** LE1 done · LE2 verify · LE3 verify
- **Lives in:** `src/voice`, `plugin/Voice`, `plugin/VoiceModel`, `game/Voice`, `core/vr/SquadParser`.
- **Left:** a microphone run, or `voice.request wav`, per game (ME2's protected voice run timed out before launch).
  **ME3:** its model tier compiles in only when LE2 is built too (the llama block in `CMakeLists.txt` tests
  `TARGET halcyon-le2`). Detail for ME2: [M6 guide](le2-m6-implementation-guide.md) package D.
- **Waits for:** T46. **Who:** nobody yet.

#### T48 Thespian mode [G35]
- **State:** LE1 done · LE2 build · LE3 verify
- **Lives in:** `game/Thespian`, `plugin/Thespian`, `core/voice/ScriptMatch`, `core/vr/Thespian`.
- **Left, ME2:** replies are taken by native slot, with no mute and no `SkipNode`, so Shepard's recording plays after the
  player has read the line; normal and Investigate replies and both interrupts unproven. **ME3:** the reader works on
  three native wheels; spoken selection is unproven and Shepard's playback stays until a mute path is verified. See
  [question 8](#open-owner-questions). Detail for ME2: [M6 guide](le2-m6-implementation-guide.md) package E.
- **Closes when:** a spoken reply and the same reply taken by the stick leave the same plot state (saves compared), for
  an ordinary reply and an Investigate reply, on each sequel.
- **Who:** nobody yet.

#### T49 First-person conversations in the world [G36]
- **State:** LE1 done · LE2 later · LE3 later
- **Lives in:** `game/ConversationWorld`, `ConversationFlow`, `AuthoredMoment`, `engine/ConversationHooks`.
- **Why later:** the owner deferred it on both sequels (rule 9). It is the largest gap by size (about 10,000 lines stand
  down). ME1's is experimental and still being worked (ROADMAP "Now").
- **Rules that carry over** (the owner, 2026-11-16, "this will be a bigger deal in me2"; research/69 items 41 and 42): the
  game's fade to black always moves the player with Shepard (curated walks excepted); what Shepard stands on carries the
  player (lifts, trams, shuttles: `Engine.Actor.Base`); freedom of movement everywhere except seated scenes, and every
  sit-down supported in first person, the view sitting with Shepard's head (`StepSeat`, `cw::IsSeatedScene`; the owner: "any
  of the types of sit down based interactions that happen across all three games"; research/69 item 43). ME2's lifts,
  shuttle rides, cockpit chairs and bar and couch scenes make these the port's first checks; sits inside a matinee (no
  conversation) are not covered yet.

#### T50 Audio: two ears, and sounds placed where they are [G37, G38]
- **State:** LE1 done · LE2 check first · LE3 check first
- **Lives in:** `plugin/OpenAlSoft`, `engine/VoicePlace`, `game/SoundPlace`.
- **Left:** check `MassEffect2.exe` and `MassEffect3.exe` imports for OpenAL32: both are believed to play through
  Wwise, which would make the OpenAL Soft switch n/a and mean a placement hook of its own. No plan covers that hook.
- **Who:** nobody yet.

### Shipping

#### T51 Our ASI loader [G46]
- **State:** LE1 done · LE2 verify · LE3 verify
- **Lives in:** `src/loader`.
- **Left:** every sequel run so far used LEBinkProxy: one run per game with our `bink2w64.dll` in its Binaries.

#### T52 Setup, report a problem, the release package, the campaign pass [G47]
- **State:** LE1 done · LE2 verify · LE3 verify
- **Lives in:** `src/install`, `tools/setup`, `plugin/ReportRequest`, `tools/package-release.ps1`.
- **Built (2026-11-16, [research/33](../research/33-installer.md) section 17):** one installer, the game a parameter
  (`install/Game.hpp`): install, Repair, the VR settings, launch, report and uninstall byte for byte for each game, its
  own manifest per game; `MEVR-Setup --for LE2`; the window's game switcher; `ReportRequest` in every host. Tested on
  fake games only (`tools/setup/test-fake-game.ps1`); ME1 byte for byte as before. **Not offered:** one switch,
  `package-release.ps1 -Games LE1,LE2` (the payload and `MEVR-Setup.ini [Setup] games`), the owner's call.
- **Left, ME2 and ME3:** an install, repair and uninstall of each on the real game; the launcher's own loader starting
  `-game 2` and `3` (T51: `launch::GameArguments`, then `kOurLauncherBoots`); the voice files and OpenAL Soft for the
  sequels. The package refuses a sequel that links LExSDKv2 (T04). The campaign and DLC exception register
  ([le2-validation-coverage.md](le2-validation-coverage.md); LE3 plan "Fixture and evidence matrix").
- **Waits for:** T51 (Launch without the picker); T04 landed 2026-11-16 (a monolithic sequel build links no LExSDKv2). **Who:** the campaign pass: nobody yet.

#### T53 Crash reports and a safe quit [G48]
- **State:** LE1 done · LE2 check first · LE3 check first
- **Lives in:** `plugin/CrashBreadcrumbs`, `plugin/KeptLocks`, the ExitProcess hook.
- **Left:** breadcrumbs are in every host. Check whether ME2 and ME3 fault at quit as LE1 did
  ([the gotcha](knowledge/gotchas/quit-time-fault-in-a-deleted-lock.md)); `KeptLocks` is LE1-only.

### Closed (checks only)

| ledger row | what | state |
|---|---|---|
| R25 | the test loop: snapshots, recording, replay | runs on all three (ME2's `xrWaitFrame`/`xrBeginFrame` not wrapped: T07) |
| G42 | physical grenades | n/a on both sequels: ME2 has none, ME3's are powers (T44) |
| G51 | the reflection (mirror) fix | n/a wherever the host owns VR, LE1 included |
| G53 | the host/mod split, hot reload, HostServices | shared and verified on all three |
| G56 | the classic menu's VR hands tab and pointer | not ported: see [Dropped](#dropped-with-reasons) |

## The order

First what unblocks the most, then what the owner plays first: ME2's first release, then ME3's first proof, then the
rest in the order players would notice.

| # | Feature | Why here | Who |
|---|---|---|---|
| 1 | T01 the seam vocabulary | T02, T03, T13 and every new sequel seam wait for it | U1, started |
| 2 | T07 the perf probe on every host | nothing about sequel performance can be measured without it | PL, started |
| 3 | T02 one bridge, one overlay composition | every quad is wired three times until it lands | U2, after U1 |
| 4 | T03 one eye-image pipeline | FSR and the tonemap reach the sequels for free | EY, after U1 |
| 5 | T04 engine access, no LExSDKv2 | done 2026-11-16 but for a monolithic ME3 run: no players' build links it | nobody |
| 6 | T25 holsters, T26 the reload, T24 the sights | ME2's first release needs M3.3-M3.5; combat is what the owner plays first | C2 |
| 7 | T15 interrupts at Veetor (with T05's pre-Veetor save) | the last open check of M5 | nobody |
| 8 | T08 the render levers, `keepBoundShaderStates` first | the sequels render every frame twice | R2, R3, PL |
| 9 | T52 Setup for ME2 | built 2026-11-16 for all three, not offered (the owner's switch); left: a real-game install | T52 lane, done |
| 10 | T05 one runner, and ME3's combat fixture | every ME3 combat check waits for the fixture | nobody |
| 11 | T45, T44, T33, T11, T14: the game-side forks and defects | small, already taken | GF |
| 12 | T42 the galaxy projector | already running | the other session |
| 13 | T21 arm IK across root-motion steps; T22, T23 on ME3 | ME3's first bounded proof: body, pistol, firing | C2 |
| 14 | T09 measure the sequels' stereo, then T13, T12, T18, T19, T28, T36, T50, T53 | cheap checks that decide whether work exists | nobody |
| 15 | T29 linear walking, T30 comfort, T31 cover | what a player feels in the first minute | nobody |
| 16 | T39, T40, T41, T37, T38 on ME3; T40 on ME2 | menus and HUD parity | nobody |
| 17 | T46, T47, T48, T34 | squad, voice, Thespian, melee | nobody |
| 18 | T10, T17, T16 | pacing, performance modes, capture plumbing unified | nobody |
| 19 | T35 the Hammerhead; T49; T20 | later, by decision | - |

Items 5, 7, 9 and 10 have nobody and sit in the first ten: they are the next lanes to start.

## Shipping per game

### ME1

Shipped (0.1.0 onward). Its own product work is [ROADMAP.md](../ROADMAP.md) "Now", which this page doesn't replace:

| ROADMAP "Now" | where it touches this list |
|---|---|
| character shadows back in both eyes | T11's `LightEnvironmentShadows` and T08's replay matrices |
| standing still without swaying (`steadyIdle`) | T21; a headset check decides the default |
| first-person conversations (experimental) | T49 |
| performance in the heaviest scenes | T08 |
| code-signed downloads | T52 ([code-signing.md](code-signing.md)) |

### ME2: ships after M5 (rule 20)

M1 to M5 of the continuation plan make the first release; M6's modules follow it. What that release still lacks:

| needed | feature | state |
|---|---|---|
| M3.3 the gun in the hand: sights' mod side, the support grip seen | T24, T22 | build, verify |
| M3.4 holsters | T25 | build |
| M3.5 the reload by hand | T26 | build |
| M5.2 interrupts taken and missed at Veetor | T15 | verify (needs a save) |
| M7.1 a players' build without LExSDKv2 | T04 | build |
| M7.2 MEVR Setup for ME2, report a problem | T52 | verify (fake games only) |
| our loader seen working in ME2 | T51 | verify |
| M7.3 the headset pass | [Headset-only checks](#headset-only-checks) | the owner |

The ledger's blockers, in one place: Setup and report-a-problem are LE1-only (G47); both sequels' player builds still
link LExSDKv2 (G47, G50, G02); every ME2 run so far used LEBinkProxy, not our loader (G46). Whether the first release
also waits for life-size stereo or the render levers is [question 4](#open-owner-questions).

### ME3: the LE3 plan's gates

From [le3-port-implementation-plan.md](le3-port-implementation-plan.md) "Parallel execution and landing order" and L3-10:

1. **The first bounded proof:** the correct process and one host, a normal save load, a tracked body and pistol, firing,
   the shared menu and cinema input, one observed light melee. Done so far: the process, the load, the body, the menu.
   Open: the pistol, firing and melee, which all wait for a combat fixture (T05).
2. Then each of heavy and class melee, the reload, traversal, squad and powers, and the galaxy earns its own gate.
3. **Release (L3-10):** only the selected game's files touched, uninstall byte for byte, no LExSDKv2, a campaign and DLC
   matrix, Quest 3 through Virtual Desktop standing and seated, and performance results.

ME3's release bar as written is higher than ME2's: see [question 5](#open-owner-questions). The same blockers apply (T04,
T51, T52).

## Headset-only checks

The list is [notes/headset-checklist.md](../notes/headset-checklist.md); this section only says where each sequel's lines
are, so nothing is written twice. Add a line there when you build something that needs the headset.

| what | where in the checklist |
|---|---|
| ME2: arms on the controllers, shots along the barrel, no camera recoil (T21-T23) | section 2, "ME2 (lane C ..." |
| ME2: the cinema screen and the interrupt prompt (T15) | "The game's start", "ME2 cinema (M5 ..." |
| ME2: the vault's distance, cue and comfort choices (T33) | section 4, "ME2's vault (lane E ..." |
| ME2: our menu, the omni-tool rail and gauntlet, the both-sticks hold (T37, T38, T43) | section 5, the three "ME2" lines |
| ME2: dark scenes and the picture defaults (T03) | section 6, "ME2's dark scenes" |
| ME2: the renderer's first headset run (T06) | section 7, "ME2 with Halcyon's renderer" |
| ME2 and ME3: everything the plans named that the checklist lacked (stereo fusion, floor lock, calibration, HUD legibility, sprint, squad, voice, galaxy, ME3's body, cinema, menus) | section 8, "The sequels, gathered from the plans (2026-11-16)" |

The LE3 plan's acceptance headset is a Quest 3 through Virtual Desktop; the null driver's roughly 31 fps says nothing
about stutter or pacing in a real headset.

## Open owner questions

Each is asked once. "Today" is what the build does if nobody answers; no default changes without a yes (rule 19).

**Answered 2026-11-16 (evening)** (the owner's words in quotes; the questions below stay for the record):

- **2, faster shield recharge in VR cover:** "Faster recharge behind cover, yes." Lane GR builds it; how "in cover" is judged
  outside a shot is GR's reported choice.
- **4, ME2's first release:** "It should always be life sized stereo for all games." T09 is required for every game's
  release (lane LS); one-Draw (T08) is not required.
- **5, ME3's release bar:** "Release is in alpha, so we'll be doing more work to get feature parity." ME3 ships as an
  alpha on a subset like ME2, parity after.
- **6, sprint:** "Unified sprint." One sprint rule for all games (lane GR).
- **7, ME3's roll/evade input:** "I'm curious if the analog direction will allow vault and roll." Lane GR researches
  whether the stick's direction can tell a vault from a roll; nothing is built before the owner reads the design. The design
  (2026-11-16): research/le2-traversal.md "The stick's direction: vault or roll" (yes on ME3, which already reads the stick;
  ME2 has no roll; the owner's choice: whether a single A may still roll).
- **8, Thespian on the sequels:** "Thespian is experimental, same with first person." Offered as experimental on the
  sequels, as on ME1, and first-person conversations too.
- **10 (lane U3's), our quads under our menu:** keep them hidden on every game (today's behaviour).
- **The second eye and dynamic shadows:** settings may differ per game ("I imagine we'd want to consider second-eye
  settings per game if they are all so different"), and "With our me1 second eye logic it breaks the dynamic shadows.
  So if we go down that path then we will need to disable dynamic shadows." Lane SP measures each option per game with
  and without dynamic shadows.
- **Earlier the same day:** the sequels' sights use ME1's systems for now; vaulting with a two-handed hold is allowed, with
  one hand to be considered ("The controls could get sloppy"; lane C4 compares); the trigger never draws a holstered
  weapon in any game; "Ideally I want everything consistent" (a per-game difference needs a reason the game forces).

**Asked by the plans and never answered**

1. **The picture defaults on ME2** (research/le2-picture.md section 5; the same question comes to ME3).
   - PS VR2 and other wired headsets: keep ME1's **neutral** (today), or a brighter profile for ME2's dark scenes?
   - Streamed Quests: keep ME1's **quest** profile (today; it puts 39% of a dark ME2 frame under the LCD's black floor),
     or an ME2 Quest preset without the mid-tone darkening and with a black lift (contrast 1.06, saturation 1.12,
     mid-tones 1.0, black +0.03; 11-12% under the floor)? The second needs a per-game preset, a new code path.
   - ME2's starting gamma: leave the game's `DisplayGamma` at 2.2 (today), or start it at 2.4-2.6 so the game itself
     lifts the shadows?
2. **Faster shield recovery in VR cover** (research/23-cover.md section 7). ME2 shortens the wait before shields recharge
   for a pawn in cover (multiplier 1.5). The VR cover rule answers only inside the damage call, so a VR player behind
   cover gets the damage cut but not the faster recharge (today). Grant it, or not? If granted, "in cover" isn't per shot
   any more: judged against the nearest hostile, or against every awake one?
3. **The vault's choices on ME2** (research/le2-traversal.md "What is left").
   - Whose facing counts: the pawn's (today: a player who turns in the room but not with the stick is refused), or the
     head's, by turning the pawn to the obstacle at the commit?
   - With the weapon away the game refuses the vault (today). Keep that, draw on the double tap, or admit it holstered?
   - The cue ("Vault" over the obstacle): off (today), or on by default once seen?
   - The comfort mask during the move: none (today), the vignette, or a blink? And the view: the game's (today) or held
     level?
   - The window the game asks for (within 0.39 m, facing within 45 degrees): keep it, or widen it?
   - Both hands on the gun through the vault (today, measured: the hands stay on their controllers and the gun) or the support
     grip let go for it (measured sloppy: arm IK falls to third person for 0.7 s while the game leaves its zoom camera)?

**Raised by putting the plans side by side**

4. **What ME2's first release must have beyond M1-M5.** M1-M5 include neither life-size stereo (ME1's default; the older
   ME2 and ME3 plans both made "metric stereo" a gate) nor one-Draw stereo (for ME1's 0.1.0 the owner judged two full
   renders too slow to ship, 2026-11-06). Ship ME2 on Halcyon's two-render, 50-units-per-metre stereo (today's plan), or add T09, or T08, or
   both to the first release? T09's measurement and T07's probe give the numbers to decide with.
5. **ME3's release bar.** ME2 ships on a first-release subset with the rest after. The LE3 plan asks for every
   applicable package and a campaign and DLC matrix before any release. Does ME3 ship on ME2's bar (body, aim, holsters,
   reload, menus, cinema, interrupts) or on the full matrix?
6. **Sprint on the sequels.** ME2 keeps its native Storm "by design"; ME3 runs ME1's VR sprint module with a native
   admission. One rule for both, or is there an ME2 fact that forces the difference?
7. **ME3's heavy melee.** Proposed and staged: arm deliberately, then strike ("chamber and strike"), never "swing
   faster". Approve that, or another input? And which input rolls and evades, given A is use and the double A vaults?
8. **Thespian mode on the sequels while Shepard still speaks.** Neither sequel can mute or skip Shepard's recording yet,
   so a line the player read aloud is said again. Offer the mode that way (experimental, as on ME1), or keep its row
   greyed until the skip works?
9. **Squad weapons' style on the sequels.** ME1's default is the game's own weapon wheel at the hand; both sequels run
   only our drawn menu. Is the drawn menu the sequels' style for good, or is the game's wheel at the hand still wanted?
10. **Our own quads under our menu** (put to the owner by the coordinator, 2026-11-16; research/trilogy-seams.md "ME3 as
    ME1"). On every game our menu (and the running calibration) is the only quad of ours while it is drawn; the world stays
    under it. Keep the HUD panel, the ring and the wheels visible under it too? **Answered 2026-11-16, the owner: no; they
    stay hidden under our menu on all three games (today's behaviour).** Nothing to build.

**Defaults waiting for a seen result and a yes** (not questions yet): `[SquadOrders] place` on the sequels (ME1's is on),
`[Traversal] cue`, ME3's melee and traversal gestures, ME3's no-snap (a diagnostic opt-in), the galaxy projector replacing
the flat presentation, `exploreStance` and `steadyIdle` on ME1.

## Where the old plans' items went

Every package, milestone and remaining-work item of the old plans, and the entry that carries it now.

| old item | here |
|---|---|
| continuation plan M1 (engine access) | T04; M1.6 the scripted save load: T05 |
| M2 (host services), its "What is left of M2" | T01, T02; the floor lock's correction and manual height: T21; the unserved services: T45, T40, T44, T47 |
| M3.1-M3.3 (hand aim, body, the gun) | T22, T23, T21, T24 |
| M3.4, M3.5 (holsters, reload) | T25, T26 |
| M4 (menu, calibration, omni-tool) | T37, T27, T38, T39 |
| M5 (cinema screen, interrupts) | T15 |
| M6 table: comfort; wrist and gun HUD; labels; cover, crouch, sprint; powers; roster and placement; weapons, orders, voice; Thespian and world conversations; galaxy | T30; T41; T40; T31, T32; T44; T46; T45, T46, T47; T48, T49; T42 |
| M6 guide packages A, B, C, D, E, F, G | T40, T45, T46, T47, T48, T49, T42 |
| M7.1, M7.2, M7.3 | T04, T52, [Headset-only checks](#headset-only-checks) |
| le2-port-implementation-plan P0, P1, P2 | T01, T04, T06 |
| P3, P4, P5, P6 | T21-T24; T26; T30-T33; T25, T44-T47, T34 |
| P7, P8, P9 | T15, T39, T48; T42, T35; T05, T52 |
| its "Minimal ME2 proof" table | the "Closes when" lines of T06, T21, T22, T26, T31, T33, T44, T08 |
| le2-port-reuse-map: pinned sources, the Overlord candidates | rule 28; T26 |
| le2-from-le1 3.1 "carry these ME1 fixes in" | T09, T01 (R34), T53, T02 (R17), T11, T08, T03, T13 |
| le2-from-le1 3.2, 3.3, 3.5 | T04; T05; T05 |
| le2-from-le1 3.6, 3.7, 3.8 | the feature entries; T15; T42, T35 |
| LE3 plan L3-0, L3-1, L3-2 | T05, T04, T01/T02/T06 |
| L3-3, L3-4 | T21, T22; T37-T41, T15, T48 |
| L3-5, L3-6 | T24, T25, T26; T44-T47 |
| L3-7, L3-8, L3-9, L3-10 | T34; T30-T33; T42; T52, T04, T51 |
| its "Specific adapter work" paragraphs | the "Left, ME3" lines of T26, T44, T34, T33, T48, T42 |
| le3-port-reuse-audit's table | each row's feature; "Holsters": T25; "Scripted actors and campaign": T35, T52 |
| me3-overall-goal-handoff "Remaining work" 1-8 | lanes; T42; T05; T22-T26, T44-T46, T34; T39-T41, T33; the "Closes when" lines; T07, T10; T52 |
| its section 4 (mirror cadence, performance) | T07 |
| galaxy-projector-handoff "Remaining work" 1-5 | T42 (1-4 ME3, 5 ME2), T16 |
| trilogy-design's mechanics table | rules 4-11, 13, 14, 18; its change log stays the record of mechanic changes |
| ROADMAP "Now", "Next" | [Shipping](#shipping-per-game) |
| ledger "Found while auditing" | T11 (shadows), T03 (tonemap), T14 and T41 (latent LE1 names), T47 (llama gating), T13 (`RecenterTiltDeg`), T44 (`mappedPower` in ME2 snapshots: GF) |

Every ledger row: R01 T06 · R02-R05 T08 · R06 T09 · R07-R09 T08 · R10 T12 · R11, R12 T11 · R13-R16 T03 · R17 T02 · R18,
R19 T14 · R20 T02 · R21 T15 · R22 T18 · R23 T09 · R24 T10 · R25 closed · R26 T07 · R27 T06 · R28 T13 · R29 T19 · R30 T20 ·
R31 T16 · R32-R34 T01 · R35 T17 · G01 T21 · G02 T22 · G03 T21 · G04, G05 T22 · G06, G07 T24 · G08 T25 · G09-G11 T23 · G12
T28 · G13-G15 T21 · G16 T27 · G17 T30 · G18 T29 · G19-G21 T37 · G22 T38 · G23, G24 T39 · G25 T42 · G26 T39 · G27 T40 · G28
T41 · G29 T44 · G30 T45 · G31, G32 T46 · G33 T47 · G34 T46 · G35 T48 · G36 T49 · G37, G38 T50 · G39, G40 T31 · G41 T26 ·
G42 closed · G43 T34 · G44 T35 · G45 T36 · G46 T51 · G47 T52 · G48 T53 · G49 T43 · G50 T04 · G51 closed · G52 T04 · G53
T01 · G54 T05 · G55 T04 · G56 closed · G57 T45 · G58 T32. Forks 1, 2, 13 T02 · 3 T42 · 4 T33 · 5 T45 · 6 T44 · 7, 8 T03 ·
9 T16 · 10 T05 · 11 T30 · 12 T34 · 14 T01 · 15 stays per renderer.

## Where the old plans disagreed

Each with both sides and the side this page takes.

| # | one side | the other | taken here |
|---|---|---|---|
| 1 | The galaxy map on a flat panel or table first, the hologram "a later milestone" (le2-port-implementation-plan "Product decisions"; le2-from-le1 3.8; continuation plan M6's `nativeTable`) | "A smaller floating flat screen ... does not satisfy this requirement"; "ME2's unfinished presentation cannot stand in" (galaxy-projector-handoff; LE3 plan "Galaxy") | the later one: the owner's correction of 2026-11-15 (rule 14) |
| 2 | "Stable stereo and metric scale" is P2's completion evidence, and "metric stereo in both eyes" is L3-2's gate; le2-from-le1 says to carry life-size stereo in at import | the continuation plan's M2 never lists it, and both sequels run upstream's 50 units per metre | open: T09, [question 4](#open-owner-questions) |
| 3 | The owner wants native HDR and "the default goes back once HDR works" (answer 5); the ME3 handoff still lists "native HDR opt-in" as unaccepted work | "We can take the HDR lane out of play" (answer 6, later the same day) | the later one (rule 12); the HDR work is dropped |
| 4 | "Computer use is a fallback, not the scenario driver" (le2-port-implementation-plan); "computer use is only a fallback for launch" (galaxy-projector-handoff, 2026-11-15) | "Runs are driven by script only, no computer use" (the owner, 2026-11-14) | the owner's (rule 22): a launch that fails is fixed in the launch script |
| 5 | `tools/qa-le2` "is the LE2 runner; don't port `fake-headset-session.ps1` to it"; its protected runner "becomes the ME2 test runner" (le2-from-le1 3.3; continuation plan "Who and when") | `tools/le2-halcyon-run` is the runner every lane uses, and "extend existing runners ... rather than introducing another launcher" (LE3 plan rule 6); the ledger counts three launchers | one runner with `-Game` (T05), grown from `le2-halcyon-run` |
| 6 | One lock agreed between `game-slot\lock.json` and `qa-cafeteria.lock` (le2-from-le1 3.3 step 3) | a third lock, `tools/le2-halcyon-run/run-lock.ps1`, is the one all lanes use | `run-lock.ps1` (rule 23); the other two fold into it under T05 |
| 7 | ME2 ships after M5 with the rest after (the owner, answer 4) | P9 and L3-10 ask for the campaign and DLC matrix and all packages before a release | ME2: the owner's. ME3: [question 5](#open-owner-questions) |
| 8 | ME2's sprint is the game's native Storm "by design" (continuation plan M6) | P5 and le2-from-le1 3.6 list `game/Sprint` for porting, and ME3 runs it (L3-8) | "Unified sprint" (the owner, 2026-11-16): one `game/Sprint` for the three (lane GR) |
| 9 | The Hammerhead's "physical cockpit is a later explicit gate", and "do not reuse Mako physics writes or weapon assumptions" (le2-port-implementation-plan P8; reuse map) | "ME1's Mako controls and cockpit, for now" (the owner, answer 3) | the owner's, with the reuse map's limit kept: the control geometry and panel code, not the physics writes (rule 13) |
| 10 | "Use physical cover with equivalent native combat benefits" (le2-port-implementation-plan "Product decisions") | the VR cover rule gives the damage cut only; the faster shield recharge is "open, the owner's choice" (research/23) | "Faster recharge behind cover, yes" (the owner, 2026-11-16): built, research/23 section 7.5 |
| 11 | Interrupts "pop up beside the matching hand" (trilogy-design's table) | the prompt shows on the cinema screen's UI layer, taken by the triggers (continuation plan M5.2) | the later one (rule 11) |
| 12 | Vehicles and the galaxy "on a cinema screen / panel to start"; "physical heat-sink reload everywhere", ME1 included; one holster layout with the SMG at the left hip (trilogy-design) | ME1 shipped a cockpit, a projector hologram, the vent instead of a reload, and the shotgun at the left hip | what ME1 shipped is the reference (rule 1); trilogy-design is historical |
| 13 | "Do not enable the current non-LE2 Thespian branch: it contains LE1 assumptions" (LE3 plan "Cinema and dialogue") | the ME3 handoff and the ledger record ME3's own Thespian adapter reading three native wheels | the later one: the adapter replaced that branch |
| 14 | "M6 follows the first ME2 release", and that release has M3, which includes holsters (M3.4) and the reload (M3.5) (continuation plan M6, M7) | on 2026-11-15 the owner authorized M6's parity work in parallel and its packages A-G were built; holsters are still missing and the reload is linked into nothing (ledger G08, G41) | both stand: M6's code is on main, and T25 and T26 are still owed to ME2's first release |
| 15 | Squad placement is on by default (ME1) | opt-in on both sequels | not a disagreement to settle here: an unseen feature stays off until seen (rules 15, 19) |

## Dropped, with reasons

Items in the old plans that this page deliberately doesn't carry.

- **Native HDR on ME2 and ME3** (research/le2-hdr.md steps 1-6; the ME3 handoff's "native HDR opt-in and headset
  appearance"). The owner took HDR out of play on 2026-11-14. T03 keeps the tonemap only because it comes free with the
  one pipeline.
- **The classic menu's VR hands tab and menu pointer on the sequels** (G56). Our main menu replaced it; the sequels'
  developer fallback is upstream's own menu.
- **trilogy-design's `games/` directory layout, the MassFPS first-person base and `SDK_TARGET_LEn` branching through
  LExSDKv2.** The code is in `src/core`, `game`, `plugin`, `engine` and `ue3`; first person is Halcyon's; LExSDKv2 is
  being removed.
- **The P0-P9, wave A/B/C and "at most three workers" dispatch, named `work/le2-*` branches and per-package handoff
  form** (le2-port-implementation-plan, le2-from-le1 section 4). Lanes, this page's entries and the ledger replace them.
  The evidence the handoff form asked for is rule 27.
- **Pad pulses and their focus logging** (le2-from-le1 3.4). Replays with `-Inputs` and `GuiKey` events replaced pulses,
  and the diagnostic host that sent them is to be retired (T05).
- **Moving the diagnostic observers onto `ue3` handles one by one** (continuation plan M1.5; le2-from-le1 3.2 step 5).
  The Halcyon host's modules are moved instead (T04); the observers go with the diagnostic host.
- **The stale status headers in four ME2 research notes** (le2-from-le1 3.9). Those notes are records now; their later
  dated sections stand.
- **The Titanfall VR source question** (le2-port-reuse-map). Settled: closed, never used (rule 28).
- **The 60% and 5/9 completion figures** (galaxy handoff; M6 guide). Their authors withdrew them as measures; the
  ledger's totals are the count.
- **ROADMAP's "Later" ideas** (walking around the room, the game's own scope reticles) and the release checklist's
  remaining ME1 decisions. ME1 product work, not trilogy parity: they stay where they are.
- **A VR treatment of ME2's hack and bypass minigames beyond the game's own screen on the wrist.** No plan designed one;
  T39 carries "the native screen, usable" and nothing more until the owner asks.

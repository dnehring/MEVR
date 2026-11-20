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
| 9 | **Conversations and cutscenes on ME1's 3D cinema screen by default.** First-person conversations in the world are **release scope on ME2 and ME3** (the owner changed priorities 2026-11-19: "I know they were deferred and I'm changing priorities to make them a part of release"): ME1's own, one module for the sequels (`game/SequelConversationWorld`), experimental and off by default on all three games as on ME1. Thespian mode may use the cinema screen. | the owner, 2026-11-14 and 2026-11-15 (deferred); 2026-11-19 (release scope) | le2-from-le1.md section 0; continuation plan M6; LE3 plan "Outcome and boundaries"; T49 |
| 10 | **Authored seated scenes (shuttle rides) allow no traversal and no combat gestures**, and keep head tracking and the dialogue controls. Decided from the native scene's state; physically seated play in ordinary gameplay keeps movement. | the owner, 2026-11-15 | continuation plan M6; [le2-m6-implementation-guide.md](le2-m6-implementation-guide.md) "Current scope"; LE3 plan |
| 11 | **Interrupts are on the triggers**, as the game has them on a pad (LT Paragon, RT Renegade). Nothing of ours claims a trigger in the conversation mode. | the owner, 2026-11-14 | answer 1; continuation plan M5.2 |
| 12 | **SDR for now; HDR is out of play.** `[Colour] hdrDisplay = -1` on every game; the picture work is the best SDR picture in dark scenes. | the owner, 2026-11-14 | answers 5 and 6; research/le2-hdr.md; research/le2-picture.md |
| 13 | **The Hammerhead gets the Mako's controls and cockpit**, in a pass of its own after ME2's first release. | the owner, 2026-11-14; kept out of the current pass 2026-11-15 | answer 3; M6 guide's opening |
| 14 | **The galaxy map opens over the Normandy's projector, with the CIC around it**, as in ME1. A flat table placed from the head is an intermediate step, not parity. | the owner's correction, 2026-11-15 | [galaxy-projector-handoff.md](galaxy-projector-handoff.md) "Intended result"; LE3 plan "Galaxy" |
| 15 | **Menu rows a game doesn't have yet stay greyed.** "Keep them greyed until I can see them." | the owner, 2026-11-14 | answer 8 |
| 16 | **The stick-click hold first.** `\` stays the menu key; holding both stick clicks always wins and is named first in the controller guide. Nothing depends on a controller's menu or system button. | the owner, 2026-11-14 | answer 8; ROADMAP "Not planned" |
| 17 | **Squad weapons stay on the left trigger, and the trigger is only ever the game's own weapon wheel**, with the left hand open and with both hands on the gun: no drawn menu of ours ("just remove the custom menu and just always default to the classic menu"). The weapon row inside the right-B placement wheel was built and reverted at the owner's request. | the owner, before 2026-11-15; 2026-11-18 | M6 guide B "Preserve the approved interaction"; T45 |
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

**Totals** (counted from the State lines below, 2026-11-17 after lane MEL's T34): 53 features.
Open (verify, build or check first): **LE1 7, LE2 35, LE3 41** (were 8, 36, 41 after the review's fixes; 9, 43, 47 on 2026-11-16).

| game | done | n/a | headset | verify | build | check first | later |
|---|---|---|---|---|---|---|---|
| LE1 | 42 | 2 | 2 | 0 | 7 | 0 | 0 |
| LE2 | 2 | 1 | 11 | 12 | 17 | 6 | 4 |
| LE3 | 1 | 1 | 6 | 15 | 21 | 5 | 4 |

LE1's seven are shared refactors that must leave ME1 unchanged; ME1's own product work is in
[ROADMAP.md](../ROADMAP.md) "Now" and mapped under [Shipping](#me1). A sequel's `done` is a feature with nothing left for
that game (T04, engine access without LExSDKv2 on ME2; T11, the engine's settings applied by the mod, on both); everything else seen working by script still
owes a headset check. Ids in brackets are ledger rows (R = renderer table, G = gameplay table) and its numbered Forks.

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
- **R32, ME3's baseline** (lane FP3, 2026-11-17; research/trilogy-first-person.md section 1): `ApplyMevrBaseline` in le3
  `me2_menu.cpp` after every ini load, ME2's list: first person with the head hidden (rule 1; ME3 started in third person),
  no head aim (rule 4), the stick doesn't pitch the view (rule 6), flat menus and conversations as cinematics on the cinema
  screen, never in the world (ME1's choices, rule 9), no profile hotkeys, no R recenter, the menu key `\` (rule 16). Seen on
  Save 31 (the fake headset); the headset is the checklist's ME3 "First person" line. Lane FX3 (2026-11-17) pinned first
  person in cover too (upstream ME3 went third person there, `CoverThirdPerson`); its check is the combat save's.

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
  the second eye still occluded, the pictures today's) (research/sequel-render-levers.md "The batch before the headset
  test"); **the defaults since 2026-11-17** (the owner's yes; lane DEF: per-game `umbraShareResultsDefault` and
  `sfrReplayRetryDefault` in each `kLayout`, an ini's `[Performance]` key still wins). One-Draw stays "not now".
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
- **Done after the review (lane FXR, 2026-11-17, research/30 9.4):** no pass renders upstream's stereo any more (the
  latch takes the last stereo plan or the eyes' centre, never mode 0; the cinema screen's last pair routed as rendered; a
  frame shown flat expires the plan), a view whose CPU window can't hold the eyes renders on the symmetric window,
  `umbraEyeMatrix` and ME3's command gate guarded on the render thread (the gate a table row), tests for all of it; the
  PS VR2's eyes replayed on both sequels: ME2 parallel, nothing missing at the outer edges, LE1 unchanged at the same pose.
- **ME3 on the shared symmetric window (2026-11-17, the owner's answer to question 12; lane DEF):** mode 1 by default
  (`MELE3VR.ini [VR] MetricEyeFrustum = 0`, LE1's `sfr2MetricEyeFrustum`; not pinned by the baseline), so each lamp's flare
  sits on its lamp in both eyes; still life-size (both seen with the PS VR2's eyes replayed, research/30 9.5). ME1 and ME2
  keep mode 2. The cost is sharpness: each eye's crop of the window at the backbuffer's 16:9 has 24% fewer pixels per
  degree across and 36% fewer down than mode 2 (a render size with the window's own shape would give the vertical back).
- **ME3's flare path found and fixed, the window's shape and size measured (lane FL, 2026-11-17,
  research/le3-flares-and-window.md; all off by default, question 17):** the reflection pass (`0x26B780`) projects through
  `+0x260` and unprojects through `+0x210`, which `[INVMAT]` makes the eye's in mode 2; `plugin/FlareWindow` (ME3's host
  only, `[Stereo] flareWindow = 1`) gives that one call the window's own inverse: in mode 2 the flares sit on their lamps
  in both eyes, at no measurable cost (ME2's pass unprojects through `+0x2E0` and is right; ME1 has no UE3 lens flare). The
  sequels' fill window takes upstream's side-by-side half-eye aspect and stands 1.56x taller than the eyes:
  `[Stereo] windowEyes = 1` (both trees) takes their union. ME3 renders any size whose sides are multiples of 8 (3072x2216
  and 4056x1728 seen; 4054 fell flat: `0x36CDC0` rounds the scene targets up). Per eye, px/deg across x down with the
  PS VR2's eyes: today 14.6 x 7.3; the eyes' window 14.6 x 11.3; mode 2 + the fix 19.2 x 11.3; 3072x2216 in mode 1
  14.6 x 14.6, in mode 2 19.2 x 14.6. Life-size in every variant (`stereo-check.py`, the same nine directions).
- **ME3 back on mode 2 at 3072x2216 (the owner's answer to question 17, 2026-11-17; lane DEF2):** `MetricEyeFrustum = 1`,
  `flareWindow = 1` and `RenderW/RenderH` 3072x2216 are ME3's defaults (code, `src/halcyon/le3/runtime/MELE3VR.ini` which the
  deploy and Setup install, `vr-game-settings.ps1 -Game LE3`, the runner); `windowEyes` stays off. Save 31 with the PS VR2's
  eyes: `[MEVR-STEREO] stereo mode 2`, each eye 19.2 x 14.6 px/deg from a 3072x2216 target, the flares on their lamps in both
  eyes (left -25 px, the streak's own asymmetry; right +5 px; with `flareWindow = 0` off the lamp again), `stereo-check.py`
  +0.69 to +1.29 degrees per eye crossed at FL's nine directions, as before. **Found:** with the aspect window and the taller
  render, the CPU (culling) window is +-1.8418 x +-2.6571, twice the eyes' height (the fill takes the backbuffer's half-width
  aspect, now 1536/2216): question 17 (b)'s `windowEyes` brings it to the eyes'; its cost at 3072x2216 is unmeasured.
- **Left:** a headset look at a lamp-lit room on ME3; `windowEyes` after a HUD check (question 17 (b)); `[DISTFIX]` in mode 2
  unmeasured (no scene with distortion yet); the owner's headset check (depth, size, comfort, nothing popping at an
  eye's outer edge); the wider culling window's cost (lane SP's levers).
- **Closes when:** `stereo-check.py` reads parallel far scenery and a pistol at 0.5 m reads 0.5 m on each sequel, and the
  owner's headset check (notes/headset-checklist.md section 8).
- **Waits for:** the owner's headset; question 17 (b), the eyes' window. **Who:** lane LS (2026-11-16); ME3's flares and
  window, lane FL (2026-11-17); ME3's defaults, lane DEF2 (2026-11-17).

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
  Progress, Shepard's on the Normandy), about 0.4 ms; **on on ME2 and ME3 since 2026-11-17** (the owner's yes, lane DEF:
  `engine::adapter::kVrDynamicShadows`; off on ME1, its cost lever). ~~ME3: no receipt yet that `scale
  set` applies at all~~: it does (the log's `system settings: scale set DynamicShadows True ...` and the frame changes in
  both eyes). Still unshown on ME3: `MotionBlur` and `FoliageDrawRadiusMultiplier` (the ledger's R12).
- **Closes when:** an ME2 and an ME3 snapshot show character shadows; ME3's log has the `system settings:` lines and an
  A/B of `DynamicShadows` changes the frame. Met on both (stretches `LE2-20261116-201015`, `LE3-20261116-202316`).
- **Waits for:** nothing; once T08 brings the replay to a sequel the gate follows the capability. **Who:** GF.

#### T12 The motion-blur guard [R10]
- **State:** LE1 done · LE2 verify · LE3 verify
- **Lives in:** `plugin/MotionBlurGuard`, with a per-game proxy-pair table in `engine/Addresses.hpp`.
- **Done, ME2 (lane AU6, 2026-11-18, d9ba2593):** five audited proxy pairs, installed and healthy on Save 4 (81 conversions in
  40 s, 0 unknown), the menu-return reapply standing down (research/le2-motion-blur-guard.md). **Left:** the headset (no
  one-eye smear on a fast turn); **ME3:** seven proxy pairs guard the recenter; other scenes and the stale-copy case are
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
  is hidden since lane CH (2026-11-17; research/le3-hud-panel.md "The crosshair hidden"): `screenReticleHide` on, the sequels'
  one request (`plugin/ScreenReticleRequest.hpp`), `ScreenReticle`'s LE3 adapter zeroing the alpha of ME3's reticle movie
  (`SFXSFHandler_Reticle`) in gameplay, the scope view left to the game. Unseen in combat: its check is the combat save's
  (research/le3-combat-fixtures.md "The crosshair hidden").
- **Closes when:** an ME3 gameplay snapshot has the HUD on the panel layer and clean eyes, with the ring and no crosshair.
- **Waits for:** T02 for the layer order; the owner's ME3 combat save. **Who:** GF has `ScreenReticle`'s latent default; HU (2026-11-16) the ME3 HUD panel; CH (2026-11-17) ME3's crosshair.

#### T15 The cinema screen, its backdrop, the fade, and interrupts [R21]
- **State:** LE1 done · LE2 verify · LE3 verify
- **Lives in:** `plugin/CineScreen`, `core/vr/CineScreen`, `plugin/CinemaBackdrop`, `plugin/TransitionFade`,
  `plugin/SequelCinema`; interrupts through `PadMerge::ConversationTriggers`.
- **ME3 (lane AU1, 2026-11-18):** the 3D screen is ME3's default as on ME1 and ME2 (the owner): every corroborated cinematic,
  whatever its game mode (it was admitted in modes 7 and 8 only), and our menu offers it (one capability,
  `renderer::Caps::cineScreen`, read by the menu and by `MevrSeams::CineScreenWanted`; the menu's "not available yet" gone).
  Not run in the game.
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
- **State:** LE1 done · LE2 eye size done, modes build · LE3 eye size done, modes build
- **Lives in:** `core/vr/PerformanceModes`, `plugin/MainMenuSettings`, `main_menu::FirstStartEyeSize`; the sequels' target rule
  `core/vr/RenderTarget`, their renderers' `renderer::GetRenderTarget`/`SetRenderTarget`.
- **Done (lane AU4, 2026-11-18):** the render size follows the headset as ME1's (the owner's "Follows headset like me1"): at the
  first VR start 0.85 of the eye, ME3 in multiples of 8, ME2 at least 16:9 (it draws no HUD on a near-square frame), written to
  `MELE2VR.ini`/`MELE3VR.ini` and GamerSettings for the next launch; Display's Render resolution and Upscale rows work on both.
  Seen on the fake headset: ME2 4096x2304 -> 2944x1656 (render thread 10.0 -> 8.1 ms, GPU 8.7 -> 6.1 ms), its HUD whole;
  ME3 at 1624x1656 in the combat fixture with its whole HUD. research/le3-flares-and-window.md section 6.
- **Done (lane AU9, 2026-11-18):** ME2's shape measured at 4:3 and 16:9 (question 18): it stays at least 16:9. At 4:3
  (3264x2448) every screen checked was whole (HUD, our menu, the pause menu, room screens, a conversation on the 3D screen,
  the galaxy map) except the game's power wheel at the hand (the cut-out took the frame's top band), for about 5% less GPU
  (0.4 ms) and 0.3 ms of render thread. research/le3-flares-and-window.md section 6.1.
- **Left, ME2 and ME3:** the Performance modes stay greyed (rule 15) until the levers they bundle exist there; the PS VR2's
  size in a headset.
- **Waits for:** T03 (FSR), T08. **Who:** nobody yet.

#### T18 The front end and the presentation hold [R22]
- **State:** LE1 done · LE2 done · LE3 done (fake headset; the owner's headset check open)
- **Lives in:** `plugin/PresentationHold` and `core/vr/PresentationHold` for all three games (the sequels through the common seams
  `FrontEndHold`, `FrontEndOnScreen`, `FrontEndBeforeTitle`, `HoldPlainVr`, `HoldLoadTail`; the game's mode mapped by
  `engine/GameAdapter.hpp`); the sequels' renderers call it in `me2_xr.cpp` (ME2) and `d3d_capture.cpp UpdatePresentationState`
  (ME3). research/56 "The sequels".
- **Done (lane FE, 2026-11-18):** the owner's "the menus are not reliably using our theater viewer" (ME2, 2026-11-17). Before:
  ME2's title on the flat panel and every load's end 0.27 s on a black 3D screen; ME3's title on Halcyon's flat cine panel. Now
  the title and its menus on ME1's 3D cinema screen on both, the boot and loads flat, ME2's load tail kept as the load.
  `[Cinematics] frontEnd` is ME1's key and default.
- **Left:** the owner's headset look at launch, title, main menu and a load on ME2 and ME3 (notes/headset-checklist.md).
- **Waits for:** nothing. **Who:** done (lane FE).

#### T19 The body's bounds for the culling camera [R29]
- **State:** LE1 done · LE2 done (pad seen) · LE3 built
- **Lives in:** `engine/BodyBounds`. Its cause on LE1 is Halcyon LE1's view offset; ArmIk's bounds flag already runs
  everywhere.
- **Done (lane AU4, 2026-11-18, the ME2 audit's REN-28):** the pad on every game (lane R2's and R3's addresses, the module on
  `runtime::Up()`); ME2's Save 4 with replayed hands: the hook in, the body's two meshes padded by 150 units.
- **Left:** LE1's repro on each sequel: look down with the head turned
  ([the gotcha](knowledge/gotchas/culling-camera-ahead-of-the-eye.md)); ME3's hook reached with tracked hands.
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
- **Shared since 2026-11-18:** the body down (or up) to the head (`[Hands] crouchBody`, `TorsoGoal::crouch`, research/29 "The
  eyes inside the body"): squats and bends no longer put the eyes inside the body, on every game whose skeleton has both eye
  bones and both legs; measured offline on LE1's skeleton, the in-game run `tools/eyes-in-body.ps1` (ME1) not yet run. **Up
  too by default since 2026-11-19** (`crouchBodyUp = 1`, the owner's yes; an old file's 0 moved once): an L3 crouch while the
  player stands brings the body up to the view, the hands on the controllers (research/29 "The body up to a standing head").
- **Shared since 2026-11-18:** hand contact with other characters (`game/HandContact`, `[HandContact]`, research/73, ledger
  G59; the owner's design): the drawn hand stops at a person's body and slides along it while the controller goes on, a buzz
  on contact, nobody pushed; every game, on by default, built and unit-tested, not yet run in a game.
- **Shared since 2026-11-18 (lane ANC, the owner's ME2 session's "almost magnetic" cover):** a crouch keeps first person
  (`ik::FirstPersonAnchor`: with the floor lock, or a crouch on foot out of cover, only the horizontal distance to Shepard's
  animated head decides; ME2's crouch had put his head 75 units from the view and arm IK's third-person anchor dropped the drawn
  body 0.5-0.76 m back to the pawn at every crouch) and a run never lowers the body (`TorsoGoal::crouchMaxDrop`: no deeper than
  the player's own head went; ME2's run had the body down 12-18 cm with the player standing): research/29 "The crouch that went
  third person" and "A run never lowers the body"; unit-tested (`tests/CrouchAnchorTests.cpp`, `tests/EyesInBodyTests.cpp`),
  ME1 too; in the game `tools/le2-halcyon-run/crouch-anchor.ps1` (not run).
- **Backing up with the weapon away** (lane M2W, 2026-11-18): the legs run 120 degrees off the motion on ME1 and ME2 (no
  backward run exploring): [question 19](#open-owner-questions), research/29 "Backing up on the sequels".
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
  `Scope_Eye` sight bone and aimed fire 10.0 -> 0.0. **The scope capture on both sequels since 0601ef40** (lane AU3, ME1's
  module, the sequel hosts serve `setScopeCapture`; no post-process chain there, `engine::adapter::kCapturePostProcess`): on
  ME2's Save 4 the picture came from the capture at 3.5x and 7x (`sights.pictureSource`); ME3's not seen yet (its fixture has
  no sniper; `mevr give <class>` needs the class loaded, and ME3 loads a weapon's package for the loadout only). Left: per-model
  optic profiles (ME1's `vr::sights::OpticFor` is
  keyed by mesh name, and ME2 reuses ME1's names for other models: its Mantis is `WPN_SNPa_MDL`) need a game key and
  measured numbers per model; on the fake headset ME2's pistol ring stands a hand's width left of the gun (the eyes take
  the monitor's shape there: a headset check). **ME3:** all of it in a game; aim modes and installed optic mods change
  the profile.
- **Closes when:** a scoped shot hits the sight line and the `sights:` lines name the game's own sight bones, with a
  sniper-rifle fixture per game (ME2's: done 2026-11-16, but for the scope capture).
- **Waits for:** a sniper rifle in ME3's combat fixture (the scope capture's check there). **Who:** C4; the capture AU3.

#### T25 Holsters, and only holsters draw [G08]
- **State:** LE1 done · LE2 verify · LE3 build
- **Lives in:** `game/Holsters` (one module, each game's names in its adapter), `core/ik/BodySlots`.
- **Done on ME2** (2026-11-16, research/le2-vrik.md "Holsters on the sequels"): the draw (`BioPlayerController.SwitchWeapon`,
  then `BioPawnBehavior.SetActionState(1)`), the holster (`SFXGameModeDefault.TryHolster`), the right grip holding the gun
  (no RB from it; the right stick click's hold opens the power wheel, ME1's rule since 2026-11-17), and only holsters draw (rule 31: the trigger, the aim trigger, reload and
  the weapon cycling refused with the weapon away, `BioPlayerInput.IsCombatEnabled` answered false for their call).
  Seen by script: the assault rifle at the right shoulder and the pistol at the right hip drawn and holstered, four
  trigger pulls with the weapon away refused, three vaults with the right grip holding the pistol; ME1 at the same pose
  unchanged. **The SMG's slot** (lane WP, 2026-11-16, research/le2-vrik.md "A body slot for the SMG"): `thighRight`, the
  front of the right thigh, a fifth `core/ik/BodySlots` slot (off on ME1, which has no SMG); the default, a recommendation
  until the owner's headset check; not seen in the game (Save 4's Soldier carries none). **The small of the back as the
  other option** (lane SG, 2026-11-17, the owner's ask; research/le2-vrik.md "The SMG at the small of the back"):
  `[Holsters] smgSlot = thighRight | lowerBack`, thighRight still the default (**the owner's question:** which default);
  `lowerBack` from the games' own lower-back socket (`EASlot_LowerBack`, `Socket_06`: the shotgun's in both games; the
  games hang the SMG at the pistol's `EASlot_Holster`, the left side of the waist, never at the lower back), and a right hand
  lost there keeps its last place on the body for `lostHandHoldS` (0.75 s) so a squeeze while the headset can't see it still
  draws. **ME3** (lane WP, research/le2-vrik.md
  "ME3's holsters, from its own script"): ME3 has no holstered state on a mission and its TryHolster only leaves cover, so
  TryHolster is never called there; the holster is the game's own put-down into no weapon
  (`SFXInventoryManager.SetCurrentWeapon(None)`) behind `[Holsters] le3PutAway`, **1 by default since 2026-11-18** (the owner;
  lane AU2 ran `holsters.ps1 -Game LE3` on the combat fixture: put away at the shoulder in 0.95 s, four trigger pulls refused,
  the pistol drawn at the hip and put away again, the release grace; research/le3-combat-fixtures.md "A combat fixture by
  console"; an old file's 0 moves once, marker `le3PutAwayMoved`), and only holsters draw is ME2's rule with "away" = weapons carried, none
  equipped. ME2's release grace seen since (back after 0.20 s, holstered after 0.93 s). **Left, ME2:** the SMG's slot with a
  class that carries one. **ME3:** done (above); left: a power cast, cover and a reload with the gun away and after, and the
  menu's Holster button (greyed on ME3 until the owner has seen the put-away).
  **The review of 57ae5b4e** (lane FX2, 2026-11-17, research/le2-vrik.md "The sequels' controls after the review of
  57ae5b4e"): ME1's power wheel hold on both sequels (an R3 tap quick-cast power 2 on ME2); a grab where the game allows no
  combat refused (`IsCombatEnabled`); ME2's refused holster felt as a refusal; `ZoomedAimError`'s flag put back in a
  `__finally`. Run on ME2 before and after. **The owner's answers of 2026-11-17** (questions 13-15, lane CTL,
  research/le2-vrik.md "The right stick click, the heavy weapon and ME1's refused draw"): the click the mod owns is kept from
  the game on the sequels (no R3: ME2's area map and radar, ME3's point of interest and area map; the map on the omni-tool's
  Map entry), ME1's R3 still reaching the game (Halcyon LE1's recenter reads it there, ME1's R3 is a zoomed scope's step);
  ME3's heavy weapon in hand refuses a grab at a slot (`body_slots::Input::switchLosesHeld`, unit-tested; ME2's switch keeps
  its heavy weapon); a draw where the game allows no combat is refused with the refusal buzz on every game, ME1 included.
  ME3's heavy weapon waits for a combat fixture (research/le3-combat-fixtures.md).
  **The owner's firefights, 2026-11-18** (lane CV2, research/le2-vrik.md "A gun let go in a fight"): "in firefights I'd let go
  and it would still be in my hand": ME2's TryHolster refuses in combat (6 of 16 let-gos in the run). Now a let-go in a fight
  holsters by TryHolster's own last step (`BioPawnBehavior.SetActionState(0)`; `[Holsters] combatHolster = 0` the old
  refusal), a let-go mid-reload, mid-power or mid-transition waits up to 4 s, and a draw nobody asked for (the wheels' close,
  a fight's start, a script: `SetActionState(1)` routes no hook refuses) is put back at once unless a power needs it. Not run
  (the owner in the headset).
- **Closes when:** a replayed grip at the hip slot draws the pistol and letting go there holsters it, on each sequel; on ME2
  also a let-go in a fight and after the hand power wheel's close.
- **Waits for:** T05's ME3 combat fixture. Part of ME2's first release (M3.4). **Who:** WP (after C4).

#### T26 The reload by hand [G41]
- **State:** LE1 n/a · LE2 later · LE3 later
- **Why later:** the owner, 2026-11-16: "putting my left hand to the hip makes no sense ... So don't enable that flow."
  Off by default on both sequels since then (`[Reload] enabled = 0`; an older file's 1 moved to 0 once, marker
  `enabledMoved`; lane RL); the code stays for a gesture redesigned with the owner. The game's own reload button and its
  automatic reload work as always. What follows is the record of the first gesture.
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
- **Waits for:** the gesture's redesign with the owner (its options: research/thermal-clips.md); then T05's ME3 combat fixture for ME3. Was part of ME2's first
  release (M3.5) until the owner's decision. **Who:** nobody until the redesign.

#### T27 Guided calibration and hand calibration [G16]
- **State:** LE1 done · LE2 headset · LE3 headset
- **Lives in:** `plugin/Calibration`, `game/BodyCalibration`, `game/HandCalibration`; the measurements in one file for the
  three games, `core/vr/CalibrationFile` (`%LOCALAPPDATA%\MEVR\calibration.ini`).
- **Done (lane AU7, 2026-11-18, 88e9a775; research/25 section 17.1):** one calibration for the trilogy (the owner's answer to
  the ME2 audit's question 4): the host takes each controller type once from the newest of the three games' inis at attach,
  every game reads and writes the shared file; `[Calibration] use`, `eye`, `grabOffset` stay per game; a
  `[Calibration.<key>.<GAME>]` section overrides keys for one game. Fake headset: ME1 applied the same values before and after
  (147 of 147 values copied identical), ME2 on Save 4 applied ME1's, no first-run prompt.
- **Left:** a headset run on each sequel: the fake headset has no interaction profile, so nothing by script can show it
  (`[Debug] calibrationKey` stands in for the key in dev builds); ME3's read of the shared file on Save 31.

#### T28 The game's camera collision off in first person [G12]
- **State:** LE1 done · LE2 n/a · LE3 n/a
- **Lives in:** `game/CameraCollision` (LE1); on the sequels Halcyon's own `FpApplyOffset` clears the flag.
- **Done (lane FP3, 2026-11-17; research/trilogy-first-person.md section 3):** `mevr camera trace` (ME3 too now, with the
  camera mode's `bCollisionEnabled` on the sequels) read the flag 0 in every tick on ME2's Save 4 and ME3's Save 31, and the
  game camera's offset from the pawn didn't move while the stick pressed Shepard into walls forward and backward. Nothing to
  port.
- **Left, ME3:** the `SFXCameraMode_Interpolate` object's own flag isn't cleared (only its To): trace a draw on the combat
  fixture; if `modeCollision` reads 1 there with the offset dropping, clear it in `FpApplyOffset`'s Interpolate branch as
  ME2 does. **Who:** whoever runs the ME3 combat fixture.

#### T29 Weapon drawn like exploring: the constant eye, linear walking, the draw rate [G18]
- **State:** LE1 done · LE2 build · LE3 build
- **Lives in:** `game/CombatStance`, `core/vr/CombatStance`, seam `ConstantFpEye`.
- **Done (0601ef40, lane AU3):** section C on both sequels: the pawn's acceleration by ME1's rule (`cs::WantedAccelRate`;
  neither sequel has the four speed ramps), ME2's start/stop/skid flags off (ME3 has none), `movement: response:` measured.
  ME2's Save 4: a straight full push 0, 398, 400 u/s at 0, 0.1, 0.2 s (was 0, 49, 122). ME3's Save 31: no change (its start
  is `SFXModule_Locomotion`'s timers, not the acceleration).
- **Left, ME2 and ME3:** ME2's stop (374 -> 158 u/s in 0.1 s, ~60 still at 0.8 s, before and after: its lever unread); the
  speed from the push (half a push walks at the game's 75 u/s; the move functions unfound: `mevr calltrace`); the constant
  eye (ME2's camera moves ~7 cm on a draw, and its modes' hooks differ, so a seam in `FpApplyOffset` asking ME1's rule, not a
  table pin); the draw rate (each game's WeaponEquip node); ME3's start and stop are `SFXModule_Locomotion`'s.
  The constant eye's seam needs no new game fact: `SFXCameraMode.HookOffset` is at +0x1A8 on ME2 (`Offset` +0x19C) and at
  ME1's +0xC0 on ME3 (`Offset` +0xB4), so one `MevrSeams::ConstantFpEye` (ME1's `HalcyonBridge` body, its mode read per game)
  called where le2's and le3's `FpApplyOffset` write a standing state's offset gives every game ME1's rule.
- **Done (lane AU5, 2026-11-18):** the constant eye on both sequels, one seam `MevrSeams::ConstantFpEye` in
  `plugin/HalcyonSequelBridge.cpp` asked by le2's and le3's `FpApplyOffset` (research/38 "The sequels' constant eye"). Seen on
  ME3: its Combat mode hooks (-30, 20, 90) against Explore's (0, 0, 120), and the renderer's one Offset had put the eye 0.7 m
  behind Shepard's head in every fight; now Combat's Offset is (5, 20, 35), the eye where exploring has it. Seen on ME2 too
  (Save 4: Combat hooks (-30, 0, 100), Offset now (-65, 0, 15), the camera where exploring has it). ME2's move function is
  `PlayerWalking.PlayerMoveCombatOrExplore` (traced; for the speed from the push).
  ME3's start: its player pawn has no `SFXModule_Locomotion` (`mevr loco`), the root-motion flags are 0; AU3's slow start
  included a turn (the replay reversed the stick); exploring 0, 12, 230, 400 u/s at 0..0.4 s, combat 350 within 0.1 s.
- **Done (lane AU6, 2026-11-18):** ME3's explore eye at Shepard's face (dadb7461; the owner's answer below): forward 34 units of
  the head bone exploring, 26 in combat, arm IK first person in a fight, and the sniper's scope capture seen on ME3 (3.5x, 7x).
  The speed from the push on both sequels (c8f6987e: `BioPawn.fMoveMag` after their move functions; half a push 167 u/s on ME2,
  146 on ME3, was 75), ME2's stop at once exploring (the move function's 0.5 s blend skipped: 0 in 65 ms, was 0.5 s), the draw
  rate on both (ME2's rifle in hand 0.16 s, was 0.62; holstered 0.16 s, was 1.13 on ME2 and 0.66-0.95 on ME3). Research/38
  "The sequels' walk and draw".
- **Done (lane AU9, 2026-11-18):** ME3's exploring start and stop (the owner's option A): the limiter is the explore tree's
  `BioAnimNodeLocomotionStart` nodes asking the pawn for their animation's root motion as acceleration (RMM_Accel) for the
  start's first ~0.25 s, and `BioAnimNodeLocomotionStop` the same after a let-go; ME1's `[Hands] startStopAnims = 0` answers both
  with RMM_Ignore in VR on foot (`UpdateStartSteps`). Exploring on the Normandy a full push runs at 0.1 s (0, 389, 389 u/s at
  0, 0.1, 0.2 s; was 0, 3-12, 152-253), half a push 189 at once; the stop stays a stop (the stop animation had walked the pawn
  on 16-38 units over 0.5 s). The response's per-tick trace (`movement: response ticks`) and `mevr loco start game|on`.
  research/38 "ME3's exploring start and stop".
- **Done (lane M2W, 2026-11-18):** ME2's late start ("a noticeable delay" in the headset): the same nodes and request list as
  ME3's (a turned start 0, 0, 7, 206-284 u/s with RootMotionMode 3 throughout, the stop animation's 3 after every let-go;
  ME1's flags already off), so AU9's `UpdateStartSteps` runs on ME2 too, its trace names the requests there and `mevr loco
  start game|on` is ME2's switch (research/38 "ME2's late start"). [verify]: `tools/le2-halcyon-run/posture-walk.ps1 -Phases
  start`.
- **Left:** the stance (off on ME1 too); a sideways slip at the end of a push after a 180-degree explore turn on ME3 (with and
  without the fix; research/38 item 5); ME2's start in the game with the fix.
- **Closes when:** `movement: response:` shows a run within 0.1 s of the push and half a push walking, on each sequel.
- **Waits for:** nothing. **Who:** AU3 (the walk), AU5 (the constant eye); the rest nobody yet.

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
  **The owner's headset run, 2026-11-18** (lane CV2, research/23 sections 7.6 and 7.7): "almost magnetic" cover: the run's log
  shows no cover entry of the game's (IsInCover never true, both A presses at cover refused); the drawn body's jump at each
  crouch (arm IK's third-person anchor) is fixed since lane ANC (2026-11-18, T21: a crouch keeps first person); a cover watch (`posture: cover watch:`) names whatever moves Shepard
  near cover next time. Protection: 0 of 164 hits covered: the eye line was open over the barriers to hovering drones; **the
  level look** (the game's own cone ignores the shooter's height) on ME2 and ME3, `[Cover] judge = either` by default, and
  every hit's detail logged. Not run (the owner in the headset).
  **ME3:** no snapping by default since 2026-11-17 (lane FP3: `[Hands] noCover`, ME1's and ME2's key and default; VR cover's
  real-slot damage rule follows it, every answer logged; research/trilogy-first-person.md section 2), but neither has an
  in-game result (Save 31 has no cover): research/le3-combat-fixtures.md "First on the combat save". Lane FX3 (2026-11-17):
  one refusal rule for the three games (`ik::posture::RefuseCoverSnap`, unit-tested; ME3's had also asked for the host's
  controls and native movement and failed open), the take-cover prompt hidden as ME2's, first person kept in cover; their
  checks are "The cover checks lane FX3 added" in the same fixture note. The crouch has no in-game result either.
  **Lane M2W (2026-11-18, the owner's ME2 session; research/23 section 8):** the crouch follows the player (`[Hands]
  crouchFollow`, on: a change the game makes against the real posture, not L3's, put back once its reason is over; ME2 stands
  a crouched Shepard up to walk with the weapon away, so he crouches again when he stops; the game insisting wins), L3 is the
  crouch toggle on the sequels as on ME1 (`[Hands] crouchClick`, on; kept from the game, whose L3 is ME2's map and objective
  radar and ME3's point of interest), and the margin and the lean (`crouchMinDropM` 0.25, `crouchLeanRatio` 1.0), on by
  default on every game since the owner's answer to [question 20](#open-owner-questions) (2026-11-18). In the game: `tools/le2-halcyon-run/posture-walk.ps1 -Phases crouch,l3`.
  **Lane CCV (2026-11-19, the owner's yes; research/23 section 7.8):** crouched behind a low wall counts as cover whatever the
  real eyes: while the game has Shepard crouched (L3, the physical crouch, a seated player's L3), every look of VR cover traces
  from his crouched head over his floor (`vr::cover::JudgingEyes`: his animated head bone, every game, ME1 included), never above the real eyes;
  each hit's line names the eyes; `tools/le2-halcyon-run/crouched-cover.ps1` checks it on ME2.
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
  ([question 3](#open-owner-questions)); its first two taken 2026-11-18 (lane CV2, research/le2-traversal.md "The vault
  Freedom's Progress requires": a barrier the mission requires couldn't be crossed): the game asked with the head's facing
  (the pawn turned to the slot at the commit; `[Traversal] facing = head`) and with the weapon away as if drawn
  (`[Traversal] holstered = 1`); the third ("C", 2026-11-19, lane RLD: research/le2-traversal.md "The step to the vault"): the
  cue on by default and a double A within 1.5 m and 60 degrees steps Shepard to the slot by the game's own walk, then vaults
  (the shared rules `vr::traversal::StepWalk`, ME2's adapter in `Le2Traversal.cpp`; ME3's requests have no step yet: its double
  tap is an opt-in, and its ladder cue shows only with it); seen on the owner's barrier (`CoverLink_5` slot 1) from 1.2 m and 45
  degrees, three of three. The two-handed hold vaults since 2026-11-16 (rule 8: the weapon's zoom cleared for
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
- **State:** LE1 headset · LE2 headset · LE3 verify
- **Lives in:** since 2026-11-17 (lane MEL; research/20 "The trilogy's one path") one path for the three games:
  `core/ik/Melee` (`ik::melee::Gestures`, the one gesture core over `core/ik/Throwing`'s swing: rules `Physical`, ME1's two
  detectors exactly, and `Staged`, ME3's rules with the heavy chamber), `game/Melee` (the one dispatcher, `Strike`,
  with ME1's order of reasons, `RouteMelee` and `MeleeWait`, over each game's adapter: ME1 `TryMelee`/`ActivatePower
  (Pistol_Whip)`, ME2 `TryMelee` and the held weapon's `MeleePowerName`, ME3 its native admission and `TryMelee`/
  `TryHeavyMelee`). `game/Throwables` only feeds ME1's strokes in its body frame (grenades are n/a on the sequels).
- **The owner (2026-11-17):** B on ME2 and ME3 works as on ME1: squad placement (hold B, the arc; a double B regroups), never
  the game's melee on foot; melee is ME1's fist gesture. `melee::AdjustPad` keeps B from the game on foot in VR on every game
  (`[Throwables] meleeButton = 0`); squad placement stands down only while B really reaches the game (`BIsGameButton`).
- **Seen 2026-11-17 by script:** ME1 unchanged at the same pose (the same melee answers, `reflect` count, `hostView` and pixels);
  ME2 on Save 4: a strike with the pistol out hit a drone, either hand's replayed stroke reaches `TryMelee`, B never reaches
  the game's melee (a call trace, with its canary), placement unchanged. **ME2 has no punch with the weapon away, forced by the
  game:** holstered, its pawn holds no weapon and its melee is the held weapon's; the fist is refused by name (ME1's punch
  would need a draw, which only holsters do).
- **ME3's stroke melee on by default (the owner, 2026-11-17, "All recommendations"; lane DEF2):** its requests and gestures
  switches start on (`[Throwables] melee = 0` the off switch, as on ME1 and ME2; `melee: ME3's stroke melee on by default`
  at start, `mevr melee` says `requests true; gestures true`, seen on Save 31); the heavy "chamber and strike" stays opt-in
  until [question 7](#open-owner-questions). ME1's and ME2's builds of `Mod.cpp` and `Melee.cpp` preprocess to the same text.
- **Left:** ME1 and ME2: the feel in a headset (the checklist). **ME3:** its light, heavy and
  B checks wait for the combat save (research/le3-combat-fixtures.md "Melee on the first combat save"); all six class heavy
  actions, the storm variants and the cover and Atlas cases are separate contexts (research/le3-melee-audit.md). ME2's and
  ME3's `LeaveWorld` on B (outside combat, the level's own "leave" events) is the top bar's Leave since lane LV (2026-11-17,
  [question 16](#open-owner-questions), research/28 "Leave"), shown only where a level's leave event would act.
- **Closes when:** one deliberate stroke makes one native request with its admission, impact and recovery seen, per game.
- **Waits for:** the headset (ME1, ME2); T05's ME3 combat fixture and question 7 (ME3's heavy melee). **Who:** MEL
  (2026-11-17); ME3's default, DEF2 (2026-11-17).

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
- **Done, ME2 and ME3 (lane LV, 2026-11-17; question 16):** a third top-bar entry, **Leave**: the level's own way out
  (`SFXGameModeBase.LeaveWorld`, what their B tap called on foot), shown only on foot, out of combat and with an armed
  `SFXSeqEvt_LeaveWorld` loaded (HostServices v44 `setGameMenuOffers`; `vr::game_menu::LeaveOffer`, unit-tested); never on
  ME1 (research/28 "Leave"). Seen on the fake headset: hidden on ME2's Save 4 and the Normandy (no event) and during
  `BioP_N7Mine`'s squad selection; offered there on foot, pressed: `LeaveWorld` called and the level's event triggered. ME1 at
  the same pose identical (the open menu's layer included). Left: ME3's in a Citadel DLC level (no save reaches one).
- **Done, every game (lane SET, 2026-11-18; research/25 section 20; the owner: "Yes, share them"):** one settings file for the
  three games, `%LOCALAPPDATA%\MEVR\settings.ini` (`core/vr/SettingsKeys`, `core/vr/SettingsFile`): a row's change in any game's
  menu reaches the three; the keys a rule keeps per game (`PerGame`: the performance levers, `[Debug]`, the first-start and
  first-run markers, `[Body.Shepard]`, `[Arms]`, the calibration's settings, paths) go to `[<section>.<GAME>]`. The first launch
  makes it from ME1's `ASI\LE1MEVR.ini`, takes ME2's and ME3's in (a value that differs stays that game's own, logged), marks
  the old files; a key changed later in a game's old file applies to that game. Unit-tested (every key of an ME1 file reads
  back the same); **left:** the first launch on the owner's files (its `settings:` lines), a menu change in ME2 seen in ME1 and
  ME3, on the fake headset or in the headset (notes/headset-checklist.md); the owner's question on the 7 values ME2 keeps
  (Open owner questions).
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
- **Done, ME3 (lane FX3, 2026-11-17):** the display the wrist screens project from is the player's own live `OmniHand_Loop`
  (a pooled, stopped or squadmate's one was taken before); `Stop`'s `bAllowCooldown` has its ReflectCheck row.
- **Done, ME3 (lane OMX, 2026-11-17):** Close leaves every screen the rail opens. Lane CTL's `mevr omni close` from the area
  map stayed in the Mission Computer: the map's B release starts its close animation and each B restarted it, so the map
  gave way only after the last of 8 back-outs. A back-out on ME3 now gives each screen one B and waits for the next screen
  (`vr::game_menu::StepBackOut`, unit-tested; ME1 and ME2 keep their tick rule, LE1's object code identical). Checked on
  Save 31 from the map, squad record, journal, Mission Computer, Codex (no page: the Mission Computer) and Equipment (none),
  the page switches, and after the game's own Start and `ShowAreaMap` (research/le3-native-gui.md "Close from the area
  map").
- **Left, ME3:** the headset look; Holster on our menu's top bar (G21, greyed until lane WP's ME3 holster call).
- **Who:** lane OM, OM2, OMX.

#### T39 The game's screens in the room and on the wrist, the pointer, the minigames [G23, G26, G24]
- **State:** LE1 done · LE2 build · LE3 build
- **Lives in:** `game/WorldUi`, `plugin/WorldUi`, `plugin/GamePointer`, `game/GuiCursor`, `core/vr/GuiEvents`,
  `game/UiWorldFigure`, `plugin/OmniPad`.
- **Done, ME3 (lane OM2, 2026-11-16):** `setWorldUi` served; the Mission Computer and the map on the wrist, War Assets in
  the room toward the War Terminal, the squad record (3D Shepard) on the native panel, the level behind ours (their UI world
  turned off while they stand), B and Close back to gameplay (research/le3-native-gui.md "The game's screens on ME3").
- **Done, ME3 (lane OM3, 2026-11-17):** the pointer's clicks take on world screens (ME3's movie mouse moved by
  `SFXGUIMovie.SendMouseEvent`, past the pad mode's mouse gate: a ray and a fingertip opened the Journal from the Mission
  Computer, War Assets' entries respond); Shepard's view on foot held at render time behind every screen of ours (ME1's hold,
  shared now: `plugin/WorldUi` `HoldViewBegin/End`, `MevrSeams::WorldViewHoldBegin/End`, `WorldViewHeld`), his head hidden
  and, for a screen in the room, his body (research/le3-native-gui.md "The movie's mouse and the view behind a screen").
- **Done, ME3 (lane FX3, 2026-11-17):** no calls on the old room's movies after a room change (every game's kept screens
  forgotten, each checked for its class before a call); a movie whose script asks for the UI world again is told again; the
  first state of an opening holds the view (every game) and ME3 keeps the last clean eyes through a reserved opening
  (`MevrSeams::WorldUiHoldFrame`, LE1's guard); the Atlas and injured Shepard hold no view on foot (research/le3-native-gui.md
  "Room changes, the UI world coming back, the first frame and the omni-tool's display"; seen on Save 31 but the Atlas). An opening the game starts itself
  still shows one MENU_MONO frame on ME3, as on ME1.
- **Built, ME2 and ME3 (lane AU1, 2026-11-18):** the sequels' own screens by their GUI libraries' tags (research/le2-menus.md
  "The sequels' own screens"): ME2's choice screen (research terminal, weapon lockers, stores, bonus power, the mission's
  completion) and ME3's in the room toward what was used (new `ChoiceGUI`); ME2's bypass and hacking minigames as ME1's
  `SkillGame` on the wrist, their whole stage; ME3's stores as ME1's shop, its weapon bench as ME1's equipment, its
  accomplishments, message boxes, private terminal, mail and elevator (new `Terminal`) in the room; ME1's crops kept to ME1's
  movies; ME3's flat panel at ME1's 1.4 m and 2.35 m (ME2's too since lane AU4, b7cd8eec: code and shipped ini, upstream's
  exact 1.6 m / 2.2 m moved once). Not run in the game: each tag's mode and anchor is [verify].
- **Left, ME2:** the squad member in the equipment screens (`UiWorldFigure`) and the armour locker (`Personalization`, its
  Shepard the UI world's: Halcyon's panel); the game check of the screens above ([le2-validation-coverage.md](le2-validation-coverage.md)
  "Hacking and bypass": the minigames are other games than ME1's). **ME3:** the squad member (`UiWorldFigure`), the armour
  locker; the game check; the headset. ME3 has no bypass minigame.
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
- **State:** LE1 build · LE2 verify · LE3 verify
- **Lives in:** `game/GalaxyProjector` (every game: the held view, the render hold, the stereo window), ME1's adapter
  `game/GalaxyMap`, `plugin/GalaxyHologram` (the window, the held view `HoldViewBegin/End`, `Holds`), `game/SceneCaptureRig`,
  `core/vr/GalaxyHologram`; the sequels' one flow `game/GalaxyProjectorFlow` with its adapters `game/Le2GalaxyCapture` (ME2)
  and `game/Le3GalaxyCapture` (ME3, + `Le3GalaxyRetention`; `Le3GalaxyProjector` is the read-only diagnostic).
- **Done 2026-11-17 (lane GX):** ME3 opens its map over the CIC's projector: the view held at Shepard's, the CIC in stereo,
  the map a stereo disc on the projector, its Flash UI on the panel there, X back to the room (galaxy-projector-handoff.md
  "2026-11-17"). ME1's `StepWindow` and the held view are the shared `galaxy_projector` code ME3 calls.
- **Done 2026-11-17 (lane GX3):** ME3's relay travel and arrival on the projector (the relay movie stays in mode 11: the
  window is drawn only over world frames now), the opening's cinematic held at Shepard's (no flat or wrong-camera frame: 0
  unheld main views in five openings), ME1's interlude rule for the map's mode changes, the retention re-armed after 20 s,
  the blue band (the projector's own lens flare) hidden with the hologram (galaxy-projector-handoff.md "GX3").
  Second run: the relay movie alone in the eyes, recenter inside the map keeps the hold, a reload in the map falls back to the
  flat native map until X (ME3 loads into the map's mode), the opening's 2.5 s measured as a Kismet delay (owner's choice).
- **Done 2026-11-17 (lane GX2):** ME2 the same, on the sequels' one flow (`game/GalaxyProjectorFlow`, ME2's adapter
  `game/Le2GalaxyCapture`): A at the console, the CIC in stereo, the map a stereo disc on the projector with its UI, X back
  to the room; the depth of field off near the projector (ME2's render thread faulted with it on); no pointer (the map's own
  controls); ME2's opt-in flat table retired (galaxy-projector-handoff.md "2026-11-17: ME2's").
- **Done 2026-11-17 (lane GX4):** ME3 on the sequels' one flow (its adapter about 55 lines; every GX3 rule is the flow's, so
  ME2 has them); ME1's opening-wait cut on both (the GalaxyMap sequence's own 2.5 s delay to 0.05 s in the CIC: ME3 opens in
  0.11 s, ME2 in 0.19 s, ME3's first map frame as shipped); ME3's room lease refreshed through the shorter opening (it ran out
  inside it); ME3 travel and arrival re-run on the flow; a reload in the map reported (the save loads with the CIC's art neither
  loaded nor requested: no projector, the flat panel until X); the opening's first cinematic frames held by a cue the host reads
  (built, not yet seen); ME2's map buttons found to be GUI events; `mevr galaxy capture tab-left|tab-right|opening-keep|
  opening-cut` (galaxy-projector-handoff.md "GX4").
- **Built 2026-11-18 (lane GXR); in the game: ME2's system and galaxy views (a planet and the relay picked, the Normandy flown
  there on the map's stick, selected, A; a cluster picked in the galaxy view, the map's cursor put on it, the relay jump) and
  ME3's system and cluster views (a station picked into its orbit view; a system flown into from the cluster
  view; ME3's A is the map's own `SelectGalaxyItem`, not a GUI event):** the right controller's ray picks on both sequels' maps (ME1's
  way), the stick stays the game's: `core/vr/GalaxyRay` (unit-tested) and `game/GalaxyRay` (one module for ME2 and ME3, called by
  the flow): the map's selectable objects projected through the map camera, the nearest to the ray's point on the map's panel
  hovered, the trigger picks it: the galaxy view's cursor put on it and the map's A (its GUI event), or, where the map's cursor
  is the Normandy (ME2's cluster and system views), the ship flown there on the map's own stick (never moved by us: the flight
  and its fuel stay the game's) and A on arrival; in ME2's orbit the ray aims the scanner's reticle where it meets the planet
  (`SFXGameModeOrbital.ReticleRot`), the game's LT and RT scan and probe there. `[Galaxy] rayPick`, `rayScan`; `mevr galaxy ray
  ...` (galaxy-projector-handoff.md "2026-11-18"). The PC map's own mouse mode was read offline: it ignores the mouse in the pad
  mode, which is why the earlier pointer clicks did nothing. Left: ME2's orbit scanner by ray (the planet view's "Start Scanning"
  wasn't reached by script: GUI A and `HandleSelectPlanet` didn't start it), ME3's galaxy view (galaxy-projector-handoff.md
  "2026-11-18").
- **Left:** ME2's travel and arrival through the flow (a relay jump: `tab-right`, then `mevr guikey 12`), ME2's console that
  stopped opening the map in a relaunch (open, not the flow), the opening cue seen on both; ME3's system scan (a replayed LT
  started none), orbit (mode 12) and the probe (no scannable planet in the fixture's reach), the captures' post-process chain,
  the headset; on ME2 orbit and the scanner in the window (its probes and mining: one launch per press);
  `vr::galaxy_map::ReadTableSettings` and one input lease for ME3; ray picking and ME2's scanner by ray in the game (GXR's
  code, above: the flight on the written stick, the scanner's frame, ME3's A as a GUI event are the open questions).
- **The owner, 2026-11-17 (night)** ("whichever gives us the great approximate experience to what we have on me1"): ME3's
  map over the projector **stays on by default**; the **depth of field off near the projector is accepted** (ME2's, forced by
  its render fault). Ray picking on both maps and ray aiming of ME2's scanner come next, from a lane after GX4 (the flow's
  `flow::Adapter::pointAt` is the hook for it: a hit on the window as a map position plus a select; not built).
- **Closes when:** the native map shows over the physical projector with the CIC in stereo around it, on each sequel;
  one probe becomes zero after exactly one launch on ME2.
- **Waits for:** T16. **Who:** GX (ME3), GX3 (ME3's remaining steps), GX2 (ME2), GX4 (one flow), 2026-11-17.

#### T43 Controller input [G49]
- **State:** LE1 done · LE2 headset · LE3 headset
- **Lives in:** `plugin/PadMerge`, `game/GamepadMapping`, `plugin/XrHost`, `core/xr/Input`.
- **Left:** the both-sticks hold in a headset on each sequel (rule 16).
- **Done (lane M2W, 2026-11-18):** the sequels keep the left stick click from the game while it is the mod's crouch toggle
  (`Options::modCrouchClick` across, `keepCrouchClick` host-only, as the right click's `keepPowerClick`); LE1 unchanged.

### Powers, the squad, voice, conversations

#### T44 Powers at the left hand [G29; Forks 6]
- **State:** LE1 done · LE2 headset · LE3 verify
- **Lives in:** `game/PowerWheel`, `core/vr/PowerWheel`, `plugin/PowerWheel`; ME3's facts in `game/Le3Powers.hpp`.
- **Left:** one sequel power reader (ME2's roster is read twice: `le2_squad::Powers` and `PowerWheel`'s own).
  **ME2:** seen by script on Save 4, 2026-11-17 (lane SQ, `tools/le2-halcyon-run/powers.ps1 -Drone`: every tile by the
  hand and the stick, the tap, the coach, chest and gun self-casts, the lock and the push at a drone; research/55 "The
  whole wheel on Save 4"); left: Slam's effect on its target, evolved powers, other weapons, the headset. **ME3:** the
  roster read on Save 31; everything past it waits for the combat save (`powers.ps1 -Game LE3`,
  research/le3-combat-fixtures.md).
- **Waits for:** T05's fixtures (ME3). **Who:** GF (the reader); the checks: SQ.

#### T45 The squad's weapons wheel [G30, G57; Forks 5]
- **State:** LE1 build · LE2 build · LE3 build
- **Lives in:** `game/SquadWeapons`, `core/vr/SquadMenu` (`RouteTrigger`), `plugin/WheelPanel`, `core/vr/WheelCrop`.
- **Done 2026-11-16:** one tick for all three games (GF, Forks 5); ME2's names from `SFXPawn.GetPrettyName` and an
  equip seen on ME2 (GF2: Miranda's pistol, 6 of 6 pulls; ledger G30).
- **Done 2026-11-18 (the owner, in the headset on ME2: "just remove the custom menu and just always default to the classic
  menu"; question 9):** our drawn menu is gone on every game (`TickCustom`, the paged inventory, `plugin/SquadPanel`,
  `[Squad] style = custom`, which an old file moves to native once, `gamepad::kFlagSquadMenu`, HostServices v11's
  `setSquadMenu`, retired). One rule says who a pull is (`squad_menu::RouteTrigger`, `TestTriggerRoute`): ME1 on foot with the
  left hand off the gun opens the game's wheel itself (unchanged); everywhere else, the sequels always, the pull is the game's
  own LB, whose hold opens the same wheel. The sequels keep the roster and `GiveWeapon` (`game/Le2SquadWeapons.inc`) for T46.
- **Left:** the owner's check on ME2 and ME3 with the left hand open (notes/headset-checklist.md); whether ME1 with both
  hands on the gun should also open the wheel itself (no 0.3 s hold, no tap that swaps Shepard's weapon) rather than through
  the game's LB is not asked yet.
- **Built 2026-11-18 (lane AU1):** the game's weapon and power wheels on their own quad at the hand on every game: LE1's code
  moved unchanged out of `HalcyonOverlays.cpp` into `plugin/WheelPanel` (the crop's measurement, the mod's `setWheelPanel`
  settings, the placement, the quad, its log lines), composed by LE1's overlays and by `SequelOverlays.cpp` (before the HUD
  panel is cleared, as the HUD pieces); the wheel's mode through `engine/GameAdapter.hpp` (ME2's 1 and 2, ME3's 3 and 4).
  Not seen in a sequel yet: `[MEVR-WHEEL]` lines and a `wheel` layer in a snapshot with a wheel up.
- **Closes when:** with two squadmates, the left trigger opens only the game's weapon wheel at the hand (the left hand open
  and on the gun), every carried weapon can be given from it and the recipient's equipped weapon is seen to change; LE1's
  same-pose check passes. Detail for ME2: [M6 guide](le2-m6-implementation-guide.md) package B.
- **Who:** GF.

#### T46 Squad orders, placement by an arc, the squad's answer [G31, G32, G34]
- **State:** LE1 done · LE2 verify · LE3 build
- **Lives in:** `game/SquadContext`, `game/SquadOrders`, `game/SquadPlace`, `game/SquadAck`, `core/vr/Squad*`; ME2's
  facts in `game/Le2Squad.hpp`.
- **Done 2026-11-17 (lane SQ):** ME2's placement seen on Save 4 (`tools/le2-halcyon-run/place.ps1`): the squad wheel,
  the arc, both squadmates holding 0.27-0.38 m from the ring, one alone, the double B back; the hold order needed the
  squadmate's move marker placed first (`BioPlayerController.UpdateMoveToIndicator`, `le2_squad::PlaceMoveIndicator`,
  ME3's too). Placement is on by default on ME2 as on ME1 (rule 19: ME1's value). research/54 "ME2 placement and orders
  on Save 4".
- **Left, ME2 and ME3:** attack and power orders in combat; ME3's everything (the combat save: `place.ps1 -Game LE3`).
  **ME3 adds** healing and revive.
- **Done (lane AU4, 2026-11-18):** the acknowledgement (G34): `game/SquadAck` hears the AI's own `TriggerVocalizationEvent` on
  both sequels (ME2's Save 4: moves 7, follows 8, 4 of 4 in a 3 s window); a standing order's "calm" reads the game's own
  combat flag on both (ME3's was "a gun is held"); research/54 "The squad's answer and calm on the sequels". Not seen: ME3's
  answer (no squad in its fixtures), the combat flag true in a real fight.
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
- **State:** LE1 done · LE2 in progress · LE3 in progress (release scope since 2026-11-19, rule 9)
- **Lives in:** ME1 `game/ConversationWorld`, `ConversationFlow`, `AuthoredMoment`, `engine/ConversationHooks`; the sequels
  `game/SequelConversationWorld` (one module, a per-game reader of the live conversation) on ME1's shared rules in
  `core/vr/ConversationWorld`; the host's v33 `setConversationWorld`, `plugin/WorldUi`'s conversation hold
  (`MevrSeams::WorldViewHoldBegin`), `MevrSeams::ConversationInWorld` in each sequel renderer's cinematic decision.
- **Why now:** the owner changed priorities on 2026-11-19 and made it part of the release on both sequels. The default stays
  ME1's: the 3D cinema screen; first person experimental, off unless chosen (Comfort > Conversations > First person).
- **Done since (lane FPC2, 2026-11-20, research/le2-conversations-in-world.md section 6):** the placements and Shepard's track
  moves withheld at the source on both sequels (ME2's `SetPositionRotator` 0xA2D180 and `InterruptConversation` 0xB06390 found
  through the live vtables; ME3's 0xA29DF0 and 0x6BAFF0); moments (ME1's `worldMoments = view`) for the level cutscenes that walk
  Shepard, Tali's re-intro held in first person on ME2 with a dip at the join; the generated presence table (ME2 109, ME3 156
  conversations to the 3D screen, `cw::NoPlaceForPlayer`); the floor marker's rule and drawing (all three games; the walk's end
  in moments as its first source).
- **Done since (lane FPC3, 2026-11-20, section 7 there):** the eyes (ME1's opening keeps the view on foot; at his mark the
  first-person eyes from his head bone, learned on foot, `cw::NoteEyeFromHead`; their height the player's over his floor,
  `cw::EyeHeightStep`, ME1's `StepEyeHeight`); a Cut faces the conversation's owner; rides (ME1's `RideBase`: what Shepard
  stands on moves the pin); the camera's fade to black moves the player with Shepard (`worldBlackMoves`); ME1's handover into a
  GUI screen (`MevrSeams::ConversationHandover` in `WorldUiOwnsGui`); ME3's console fixtures (`mevr instances`, `activate`,
  `setloc`). Seen on ME2 (Tali's, Samara's) and ME3 (the docking officer): the eyes and their height; the presence table misses
  ME3's comm calls whose player VO track names `Player` (the docking officer stayed on the screen by the load rule).
- **Done since (lane FPC4, 2026-11-20, section 9 there):** ME1's records from above on both sequels (`mevr convo stance[s]`,
  judged by `tools/scene_audit.py` unchanged: person-not-drawn, body-not-drawn and the rest; the canary `mevr convo hide
  body|person`); ME1's eyes stand-in (`worldLookEyes`: ME2 `BioPawn.SetLookAtTarget`, ME3 `SFXModule_LookAt.ChangeTarget`; seen
  on ME3); ME1's `StepSeat` (core `cw::StepSeat`) and walk-along (`worldWalkAlong`) in the sequels' conversations and moments;
  the floor marker's black-move source; ME3 in the game (the embassies): its hub characters are SFXStuntActors, now the scene's
  people; the eyes on foot taken where the renderer draws them (ME3's 32 units above the game's camera), ME3's eye offset
  learned 31-33 forward, 10 up; why `mevr thespian pick` was refused (no input focus on the fake headset) and `mevr convo pick`.
- **Left:** the circle and the squad's formation (`game/AiWalk`; ME3's hub people are stunt actors with no AI controller), the
  subtitle and wheel placement, the script's GUI black screen, the floor marker's "waits" source (and ME1's black-move source),
  ME3's video calls and off-screen speakers (Khalisah's at the embassies) in the presence rules, the seats and walk-along seen in
  the game, ME3's walking conversation, Joker seated and a level cutscene, the face axis on ME2's and ME3's skeletons. Each item
  on ME2 and ME3 alike.
- **Closes when:** an ordinary conversation, Tali's meeting and the Miranda scene play in first person on the fake headset
  with the body drawn, the eyes at the player's height and a clean return to gameplay, and the 3D screen unchanged with the
  option off (snapshots and stances under `%LOCALAPPDATA%\MEVR\sessions`); then the owner's headset pass.
- **The owner's answers, 2026-11-19** (research/le3-conversations-in-world.md section 9, all three (a), on all three games; the
  defaults of the first-person mode): (1) the sticks are ME1's: the left stick walks only while the wheel is down and steers it
  when it is up, the right stick turns (the slice's right-stick wheel goes); (2) Shepard walked by a line or a level matinee is
  ME1's moment rule with ME1's current defaults (`worldMoments = view`, walk-along, the game's black moving the player with
  curated walks excepted), Shepard's walk withheld at the source through each game's hooks instead of the slice's pin, never an
  automatic guided walk to a mark (`worldWalkInS = -1`); (3) scenes with no place for the player go to the 3D screen from a table
  generated from the scan (presence-based), never typed by hand.
- **Who:** lane FPC (ME2, then ME3 on the same module), after another session's ME3 core slice (bff358d3 and its follow-ups).
- **Rules that carry over** (the owner, 2026-11-16, "this will be a bigger deal in me2"; research/69 items 41 and 42): the
  game's fade to black always moves the player with Shepard (curated walks excepted); what Shepard stands on carries the
  player (lifts, trams, shuttles: `Engine.Actor.Base`); freedom of movement everywhere except seated scenes, and every
  sit-down supported in first person, the view sitting with Shepard's head (`StepSeat`, `cw::IsSeatedScene`; the owner: "any
  of the types of sit down based interactions that happen across all three games"; research/69 item 43). ME2's lifts,
  shuttle rides, cockpit chairs and bar and couch scenes make these the port's first checks; sits inside a matinee (no
  conversation) are not covered yet.
- **People leaving a scene walk away first** (the owner, 2026-11-19, of Udina vanishing after the council hearing; lane EXIT,
  research/conversation-exits.md): the shared rule `core/vr/ConversationExit` (unit-tested), ME1's adapter
  `game/ConversationExit` with `engine/ExitHooks` (the deactivation, BioActorBehavior.SetActive through ProcessEvent),
  `[Cinematics] worldExit = walk` by default with the first-person mode; on the fake headset Udina walks off with the game's AI
  and goes once out of sight, the audit set's `popped-out` canary is caught (2026-11-20). **ME2 and ME3 since lane EXIT2**
  (2026-11-20, section 9 there): the same module with a sequel adapter; ME2's `Active` row is `Engine.Actor.SetActive` through
  ProcessEvent (`AActor::execSetActive` `0x4BC510`, Ghidra), ME3's `BioSeqAct_SetActive` is script, so the hook (`0x4BFAF0`)
  steps over a withheld call on the script's own frame; the people from the sequels' module, its cut counter for the dip (and
  on foot through the armed word); on ME2's fake headset Tali and Prazza walk off after her reintroduction and go under the dip, the broken
  run is caught by `vanished-in-view`. **ME2's walk is the Kismet's own since lane EXIT3** (section 12 there): `SFXAI_Core.MoveToGoal`
  to a navigation point along the AI's path (the places include navigation points the eyes can't see with a path, asked with UE3's
  constraints), so Tali and Prazza walk out of sight in 2-3 s and go with no dip; ModifyPropertyPawn's Activated is `0xA7A010`.
  **ME3 in the game since lane EXIT3** (section 11 there): its hub people are stunt actors (held,
  never walked: no AI), read from the controller's owner, found actors and extras too; the hub's crew members (`hench_` tags) are
  squadmates (owner question 7); at the Prothean's moment the script path held three people in view, the op's loop and the level's
  script went on, the broken run is caught; ME3's walk (`MoveToSlot`) still unchecked (no pawn in a hub conversation).
  ME3's line visibility tracks counted (about 287 in 50 conversations hide people), not held:
  owner question 6. Teleports of a person in view next on all three (section 7 there).

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
- **State:** LE1 done · LE2 done · LE3 done
- **Lives in:** `plugin/CrashBreadcrumbs`, `plugin/KeptLocks`, the ExitProcess hook.
- **Done (lane AU4, 2026-11-18):** `KeptLocks` in every host; the sequels' `[EXITKILL]` hook hands the quit to ours
  (`MevrSeams::HostEndsProcess`: before, it terminated first and our exit line, `NoteProcessExit` and the kept locks' summary
  never ran). Console quits: ME2 `41 deletion(s) of the game's static locks skipped`, ME3 `47`, no `crash-exit.txt`.
  ([the gotcha](knowledge/gotchas/quit-time-fault-in-a-deleted-lock.md))

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
| 12 | T42 the galaxy projector | ME3 running (2026-11-17), then ME2 | GX |
| 13 | T21 arm IK across root-motion steps; T22, T23 on ME3 | ME3's first bounded proof: body, pistol, firing | C2 |
| 14 | T09 measure the sequels' stereo, then T13, T12, T18, T19, T28, T36, T50, T53 | cheap checks that decide whether work exists | nobody |
| 15 | T29 linear walking, T30 comfort, T31 cover | what a player feels in the first minute | nobody |
| 16 | T39, T40, T41, T37, T38 on ME3; T40 on ME2 | menus and HUD parity | nobody |
| 17 | T46, T47, T48, T34 | squad, voice, Thespian, melee | nobody |
| 18 | T10, T17, T16 | pacing, performance modes, capture plumbing unified | nobody |
| 19 | T35 the Hammerhead; T20 | later, by decision | - |

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
| M3.5 the reload by hand | T26 | later: off until its gesture is redesigned with the owner (2026-11-16) |
| M5.2 interrupts taken and missed at Veetor | T15 | verify (needs a save) |
| M7.1 a players' build without LExSDKv2 | T04 | build |
| M7.2 MEVR Setup for ME2, report a problem | T52 | verify (fake games only) |
| our loader seen working in ME2 | T51 | verify |
| first-person conversations in the world, experimental, off by default (rule 9, since 2026-11-19) | T49 | build, verify |
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

First-person conversations in the world (T49) are in ME3's release too since 2026-11-19 (rule 9), experimental and off by
default. ME3's release bar as written is higher than ME2's: see [question 5](#open-owner-questions). The same blockers apply (T04,
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

**Answered 2026-11-19:** "Should crouching behind a low wall count as cover even when your real eyes stay above it?" (lane SMOKE's
finding: the owner's natural crouch lowered his eyes to 1.52 m over walls about 1 m tall, 0 of 164 hits covered): "Yes I think so,
for seated players this should of course be necessary." **Built (lane CCV, T31):** while the game has Shepard crouched, VR cover
judges from his crouched head (his animated head bone), every game (research/23 section 7.8).

**Asked 2026-11-18, night** (lane SET, one settings file for the trilogy, T37; research/25 section 20.4): the first launch keeps,
for ME2, its own value where its file differed from ME1's: `[Hands] steadyIdle` (ME1 1, ME2 0), `[Sights] pistol` (none /
dot), `[Sights] reticle` (game / duplex), `[Voice] device` (the Oculus headset microphone / the default), `[Voice] model`
(small / tiny), `[WorldUi] wrist` (the same screens, another order), `[Cockpit] script` (cockpit-v2 / cockpit). Today: ME2 keeps
them (`[<section>.LE2]`) until a menu change shares one. Share all seven as ME1's at once, or leave each to a menu change?

**Answered 2026-11-18, night:** settings shared across the games ("I assume the settings for 2 would carry into me3", then "Yes,
share them"). **Built (lane SET):** T37's entry.

**Answered 2026-11-18, later** (the questions the audit lanes AU2-AU7 raised; the owner's words in quotes):

- **The shouldered gun on ME2 and ME3** (lane AU2's trace, docs/knowledge/audits/me2/weapons-and-sights.md): "Your
  recommendation": option B. No LT from the support grip; each shouldered shot's own calculations (accuracy, spread, ME3's
  sniper damage and shotgun cone) are made as zoomed for their duration only; the camera zoom, the scope sway and the
  melee/roll admission blocks never apply. ME2's sniper time dilation not included (option C). **Built (lane AU8, be1a47ba):**
  `game/ZoomedShots`; not the shotgun cone (a timer's ProcessEvent: research/le2-vrik.md "The shouldered gun on the sequels").
- **ME3's omni-blade with the gun put away:** "Option A": a fist stroke calls ME3's heavy melee action directly (the class's
  own heavy melee; biotic for Vanguards and Adepts), once a check shows it plays with no weapon equipped. **Built (lane AU8,
  2c549a7a):** it plays (research/le3-combat-fixtures.md "The omni-blade with the gun put away").
- **Shepard's body during a roll:** "Yes for now": hidden while the roll lasts. **Built (lane AU8, e4d35c24):** the body, head and
  gun in hand, `[Hands] rollHidesBody`.
- **ME2's render shape (question 18):** "Not sure, whichever leads to the better user experience": the lane measures both
  (cost, and every ME2 screen, the HUD, menus, the conversation wheel and the room screens at 4:3) and picks 4:3 only if
  nothing breaks, else keeps 16:9. **Lane AU9: 16:9 kept** (at 4:3 the power wheel at the hand showed the wrong part of the
  frame, for a 5% GPU saving; research/le3-flares-and-window.md section 6.1).
- **The sequels' eye distance** (26-34 units ahead of Shepard's head bone, ME1's about 4): "Need to validate in headset,
  whatever gets us closest to feeling like we are attached to the body." A headset A/B, prepared by a lane
  (notes/headset-checklist.md); today's value stands until then. **Lane AU9: built**: our menu's Advanced > Tracking > Eye
  position (headset test), Today / Between / At the face (`[Hands] eyeTest` 0/1/2, live): on the fake headset the eye 29, 18
  and 4 units ahead of Shepard's head bone on ME2, 29-32, 17-20 and about 3 on ME3 (research/38 "The sequels' eye distance,
  an A/B").
- **ME3's exploring start** (about 0.2 s to most of a run, ME1's 0.1 s): "Option A": the mod speeds it up, once the
  limiter is found (not `SFXModule_Locomotion`, which the player pawn lacks).
- **The VR hands tab's arm sample (`[Arms] armLengthM`):** "Your recommendation": it stays per game; the guided
  calibration's arm length is already shared.

**Answered 2026-11-18** (the questions of the ME2 and ME3 audits, docs/knowledge/audits/me2/summary.md and
me3/summary.md; the owner's words in quotes):

- **ME2's fifth reach zone, the SMG at the right thigh** (on by default): "Right thigh may work for now." Kept.
- **The sniper's magnification:** "sniper zoom is unified." One rule for the three games, read as ME1's (3.5x, then 7x
  after a flick, from a real zoomed render; rule 1). Today ME2 shows 8.0x and ME3 4.0x. **Lane AU2, 2026-11-18:** the levels
  are ME1's on both sequels (3.5x, 7x after a flick; game/Sights `ReadGameZoom`). **Lane AU3, 0601ef40:** the zoomed
  render is ME1's scope capture on both sequels (seen on ME2 at 3.5x and 7x; ME3 not yet).
- **ME3's default for conversations and cutscenes:** "Likely yes if it's default for the other two games." It is: ME1's 3D
  cinema screen is ME3's default, and ME3's menu (which offers Flat only and calls the screen "not available yet") is to
  offer it as ME1's and ME2's do.
- **ME3's render size:** "Follows headset like me1." The render target from the headset's eye size (ME1: 0.85 of it,
  FSR on), not a fixed 3072x2216; this replaces question 17 (c)'s answer of 2026-11-17. ME2's fixed 4096x2304 follows the
  same rule (rule 1; the ME2 audit's finding 12). **Built by lane AU4 (2026-11-18, T17):** the first VR start's rule on both
  sequels; ME2's target at least 16:9 (it draws no HUD on a near-square frame: question 18).
- **ME3, a gun let go is put away:** "Obviously yes, that should be default." `[Holsters] le3PutAway = 1` by default.
- **ME3's melee:** "Me3 melee will require letting go of the guns and doing a fist to summon the Omni blade for strikes."
  The hand holding a gun never strikes; an empty hand's fist brings out the omni-blade, and the strokes strike with it.
- **ME3, the left-hand push with a grenade equipped:** "Yes": it throws the grenade.
- **ME3, the omni-tool rail's Squad entry greyed until its figure is ported:** "That's fine for now."
- **Halcyon ME2's `VehicleHeadMode = 3`** (the head aims the Hammerhead's cannon): "vehicle head mode likely shouldn't be
  on. We'll be building out the models for first person default." Pinned to 1 by ME2's baseline (free look: the head turns
  the view, the stick aims; ME1's `makoHeadAim = 0`) until the Hammerhead gets the Mako's controls and a first-person
  cockpit. Upstream's own **`ConvoFirstPerson`** pinned off with it (one first-person conversation for the trilogy, ME1's).
- **ME3, a roll or hit reaction with snap turn on:** "I'm not sure, since I don't know how rolls should appear in VR," then
  "For roll we'll do your recommendation": a roll (and melee, a hit reaction, a cover entry) stays first person, the view
  level (never tumbling) and sliding with the body to where the move ends, the floor held, the snap turn still the
  player's, an optional vignette (as ME2's vault).
- **ME3's explore eye:** "Me3 eyes should match behavior from other games." Option (a) of research/38 "The sequels' constant
  eye": the renderer's standing rows (-35, 0, 65), the eye 35 units ahead of the hook bone as ME2's (lane AU6, dadb7461): forward
  34 units of Shepard's head bone exploring, 26 in combat (ME2 24-29), arm IK first person in a fight.
- **Earlier the same day** (the ME2 audit): no haptic cue at the chest on ME2 (no grenade slot there); one calibration for
  the trilogy unless a game's VRIK differs (built: T27); the shouldered gun's accuracy without the game's zoom (the proposal in
  docs/knowledge/audits/me2/weapons-and-sights.md, not built).

**Answered 2026-11-17** (lane REV's review of 57ae5b4e raised both):

- **B on the sequels** (ME2's B is `Console_Melee`: `TryExitCover 1 | TryMelee | LeaveWorld`; placement's hold and double tap
  also meleed): "B on ME2 and ME3 works as on ME1": squad placement (hold B, the arc; a double B regroups), never the game's
  melee on foot; melee is ME1's fist gesture. Built by lane MEL (T34): one melee path for the three games, B kept from the game
  on foot in VR on every game, ME1's fist and weapon strokes on ME2. ME3's melee stayed opt-in (question 7) until the
  owner's "All recommendations" later that day: **ME3's stroke melee is on by default since then** (lane DEF2, below); only
  its heavy "chamber and strike" waits for question 7.
- **ME3's sharpness, question 17** (the owner, 2026-11-17: "All recommendations"): (a) yes, mode 2 with the flare fix as ME3's
  defaults; (c) yes, 3072x2216; (b) the eyes' window only after a look at the HUD panel in a scene with a HUD (stays off).
  And **ME3's stroke melee on by default**, as on ME1 and ME2, the heavy one out of it. Built by lane DEF2: question 17's
  entry below, T09, T34.
- **The reload button on the sequels** (X is the hand power wheel there; the game's reload is X): "automatic reload for now".
  Checked by lane MEL on ME2's Save 4: the pistol fired dry reloads by itself (research/20 "The trilogy's one path"); nothing
  changed.

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
- **The reload by hand (T26):** "putting my left hand to the hip makes no sense ... So don't enable that flow." Off by
  default on ME2 and ME3 (lane RL); the gesture is to be redesigned with the owner.
- **The second eye and dynamic shadows:** settings may differ per game ("I imagine we'd want to consider second-eye
  settings per game if they are all so different"), and "With our me1 second eye logic it breaks the dynamic shadows.
  So if we go down that path then we will need to disable dynamic shadows." Lane SP measures each option per game with
  and without dynamic shadows. **Answered 2026-11-17, the owner ("All recommendations"), built by lane DEF:** the second
  eye's defaults per game as lane SP2 measured them (ME2 `umbraShareResults = 1` and `sfrReplayRetry = 0`, ME3
  `umbraShareResults = 1`, ME1 unchanged: `plugin/RenderLeversTable.hpp`); dynamic shadows **on** on ME2 and ME3 (0.2-0.3 ms,
  each eye its own render) and off on ME1 (`engine::adapter::kVrDynamicShadows`; `vr-game-settings.ps1` and Setup write
  False for LE1 only; a player's `[SystemSettings] DynamicShadows` still wins). **Superseded the same night** by the
  owner's ME2 headset test ("disable the shadows entirely for both eyes": they differed between the eyes): dynamic and
  character shadows off on ME2 and ME3 (`kVrDynamicShadows` false for every game, `kVrCharacterShadowsOff`; 9f556c1a).
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
   - Whose facing counts: the pawn's, or the head's, by turning the pawn to the obstacle at the commit? **Today (since
     2026-11-18, lane CV2): the head's** (`[Traversal] facing = head`), because a barrier Freedom's Progress requires
     couldn't be crossed (research/le2-traversal.md "The vault Freedom's Progress requires"); `facing = pawn` the old rule.
   - With the weapon away: keep the refusal, draw on the double tap, or admit it holstered? **Today (since 2026-11-18):
     admitted holstered** (`[Traversal] holstered = 1`, nothing drawn); 0 the old refusal.
   - ~~The cue ("Vault" over the obstacle): off, or on by default once seen?~~ and ~~The window the game asks for (within
     0.39 m, facing within 45 degrees): keep it, or widen it?~~ **Answered "C" (both), the owner, 2026-11-19:** the cue on by
     default (`[Traversal] cue = 1`), and a double A within 1.5 m, the head within 60 degrees, steps Shepard to the slot with
     the game's own walk and vaults (`stepTo`, `stepReachM`, `stepFacingDeg`; lane RLD, research/le2-traversal.md "The step to
     the vault").
   - The comfort mask during the move: none (today), the vignette, or a blink? And the view: the game's (today) or held
     level?
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
   (ME3's light stroke melee is on by default since 2026-11-17, lane DEF2; this question is only the heavy one, which stays
   opt-in: `mevr melee heavy-gestures on`.)
8. **Thespian mode on the sequels while Shepard still speaks.** Neither sequel can mute or skip Shepard's recording yet,
   so a line the player read aloud is said again. Offer the mode that way (experimental, as on ME1), or keep its row
   greyed until the skip works?
9. **Squad weapons' style on the sequels.** ME1's default is the game's own weapon wheel at the hand; both sequels run
   only our drawn menu. Is the drawn menu the sequels' style for good, or is the game's wheel at the hand still wanted?
   **Answered 2026-11-18, the owner (in the headset on ME2): the game's wheel.** "I think we should just remove the custom
   menu and just always default to the classic menu to change their weapons." Built (T45): the drawn menu is removed on all
   three games.
10. **Our own quads under our menu** (put to the owner by the coordinator, 2026-11-16; research/trilogy-seams.md "ME3 as
    ME1"). On every game our menu (and the running calibration) is the only quad of ours while it is drawn; the world stays
    under it. Keep the HUD panel, the ring and the wheels visible under it too? **Answered 2026-11-16, the owner: no; they
    stay hidden under our menu on all three games (today's behaviour).** Nothing to build.
11. **The SMG's slot on the sequels** (T25; lane SG, 2026-11-17, research/le2-vrik.md "The SMG at the small of the back").
    Both are built: the front of the right thigh (`[Holsters] smgSlot = thighRight`, today) and the small of the back
    (`lowerBack`, the owner's ask). Which is the default? What the games themselves do: they hang the SMG at the pistol's
    holster on the left side of the waist (only one of the two is shown when both are carried) and the shotgun at the lower
    back. The lower back is out of an inside-out headset's view (a lost controller's place is kept 0.75 s there) and a hand
    reaching it passes the pistol's right hip; the thigh is in view and clear of the pistol.
12. **ME3's lens flares with each eye's own frustum** (T09; lane FXR, 2026-11-17, research/30 9.4 item 6). With a PS VR2's
    eyes replayed, every lamp's flare in ME3 sits off its lamp, at the same pixel of both eye images (which look different
    ways), so it can't fuse: the sprite is placed through the CPU's symmetric window while the image is the eye's own
    off-centre frustum (mode 2). The fake headset's symmetric eyes hid it; ME2's lamps showed none; ME1 has the same
    mechanism for the sun's sprite near the edges. Options: keep mode 2 and find where ME3 places the flare (Ghidra) to give
    it the eye's matrix; or run ME3 in mode 1 (the eyes' union window, flares right, about 15% fewer pixels per degree on a
    PS VR2) until then. Recommended: mode 1 on ME3 for its first headset test, the flare path as a lane. **Answered
    2026-11-17, the owner ("All recommendations"): mode 1 on ME3** (lane DEF: `MELE3VR.ini [VR] MetricEyeFrustum`, LE1's
    `sfr2MetricEyeFrustum`, 0 by default and not pinned by the baseline; ME1 and ME2 keep mode 2). The flare path stays a
    lane for later. **Found and fixed by lane FL (2026-11-17), off by default:** the way back to mode 2 is question 17,
    answered the same day: ME3 renders mode 2 with the fix by default since lane DEF2.
13. **ME3's heavy weapon and the holsters** (T25; lane FX2, 2026-11-17). A heavy weapon has no body slot, so the holsters
    see an empty hand: a right-grip squeeze inside any slot (the right shoulder's, close to where a heavy weapon is held)
    calls `SwitchWeapon`, and ME3's own `SwitchWeapon` puts the heavy weapon down with `SFXHeavyWeapon.PutDownWeapon`, which
    in single player throws it away (`bTossOnWeaponSwitch`) for good. ME1 has no heavy weapon, so there is no ME1 rule to
    follow. Today: the grab switches (and drops it). The options: (a) while a heavy weapon is out on ME3, a grab at a slot is
    refused with the refusal buzz and a log line, and only the game's weapon wheel switches away; (b) the same, but a second
    squeeze within a second switches (a deliberate double grab); (c) keep today's. ME2's heavy weapon stays in its inventory
    on a switch, so ME2 needs none of this. **Answered 2026-11-17, the owner ("All recommendations"): (a).** Built (lane
    CTL): while ME3's `SFXHeavyWeapon` is in hand a grab at a slot is refused with the refusal buzz and a log line
    (`body_slots::Action::SwitchRefused`; `mevr weapon` refuses too); ME2 keeps its draw (its `SwitchWeapon` keeps the
    heavy weapon: no loss). Unit-tested; the game check waits for an ME3 combat fixture (research/le3-combat-fixtures.md).
14. **The raw right stick click on the sequels** (lane FX2, 2026-11-17). The click still reaches the game as R3 (Halcyon's
    double-click recenter reads it; ME1's R3 does nothing in VR). ME2's R3 is `Console_ShowMap` (a tap opens the area map,
    a hold turns the objective radar on); ME3's R3 is `Console_NavAssistanceOrCoverTurn` (every press: a cover turn, a point
    of interest, else `ShowAreaMap`). So the power wheel's hold also brings up the radar on ME2 and, on ME3, may open the
    area map before the wheel. Keep the R3 (today), or keep R3 from the game on foot while the click's hold is the power
    wheel's (ME1's effect: R3 does nothing there), leaving the area map on the omni-tool's Map entry? **Answered 2026-11-17,
    the owner ("All recommendations"): keep it from the game.** Built (lane CTL): on the sequels a press of the click that
    begins while the mod owns it (`kFlagModPowerHold`: on foot and in the wheels) never reaches the game as R3
    (`gamepad::Options::keepPowerClick`, set by the sequels' bridge); other modes keep the click. ME1 unchanged: its R3 is
    `ChangeMagnification` (a zoomed scope's step, no screen) and Halcyon LE1's double-click recenter reads R3 from the
    game's pad (the sequels' recenter is a double Back). The map: the omni-tool's Map entry on both sequels. Seen on the
    fake headset: ME2 Save 4 (a tap and a hold call no `ShowAreaMap` or objective radar any more; the Map entry opens the
    `AreaMap` page) and ME3 Save 31 (no point of interest or area map; the Map entry opens the deck map).
15. **A refused holster draw on ME1** (lane FX2, 2026-11-17). On the sequels a grab where the game allows no combat (a hub,
    the Normandy) is refused with the refusal buzz. On ME1 the grab buzzes as a draw and nothing comes out
    (`SFXGameModeDefault.SwitchToWeapon` refuses by itself; a warning 3 s later). Give ME1 the sequels' refusal too (one
    rule), or keep ME1 as it is? **Answered 2026-11-17, the owner ("All recommendations"): one rule.** Built (lane CTL): the
    shared draw asks `BioPlayerInput.IsCombatEnabled` first on every game (what ME1's `SwitchToWeapon` asks), and a refusal
    is the refusal buzz and `the <gun> stays in its holster: the game allows no combat here`. A haptic and log change on
    ME1; checked at the same pose (identical but for the combat-off grab: research/le2-vrik.md).
16. **ME2's "leave" on B** (T34; lane MEL, 2026-11-17). With B kept from the game on foot, its other two uses there are gone:
    `TryExitCover` (nothing: no cover snapping in VR) and `SFXGameModeBase.LeaveWorld`, which outside combat fires the level's
    own `SFXSeqEvt_LeaveWorld` events (its script, read 2026-11-17: the levels that let the player leave by the button). ME1's B
    had no such use. Today: nothing reaches it on foot. Options: (a) an entry on our menu's top bar or the omni-tool's rail
    that calls `LeaveWorld` on the game thread (as Game menu and Holster do); (b) B tapped (not held, not double) reaches the
    game only where a level has a leave event; (c) leave it. Which levels carry the event is a scan away
    (`tools/conversation-scan --script grep:` over the levels). **Answered 2026-11-17, the owner: (a), a Leave entry on the top
    bar next to Game menu and Holster.** Built (lane LV, research/28 "Leave"): it closes our menu and the mod calls `LeaveWorld`
    on the active mode on the game thread two ticks after the menu's pause is lifted; it shows only where that would act (on
    foot, out of combat as the game's own `InCombat` says, an armed `SFXSeqEvt_LeaveWorld` loaded: enabled, not used up, not
    behind a closed gate), told to the host each tick by HostServices v44 `setGameMenuOffers`. ME3 binds the same leave on its B
    tap (`Console_Melee`: `OnTap 0.3 TryMelee | LeaveWorld | ...`), so ME3 has the entry by the same rule; never on ME1 (its
    only `LeaveWorld` is the Mako's X). The levels: ME2's assignments after their end (one shared prefab: "return to the
    Normandy?"), the post-credits cabin scenes, the shooting gallery; ME3's Citadel party, dance floors, casino race and combat
    simulator.
17. **ME3's sharpness: mode 2 with the flare fix, the eyes' window, a taller render** (T09; lane FL, 2026-11-17,
    research/le3-flares-and-window.md sections 3 and 4). All three are built and off by default; each eye's pixels per
    degree (across x down) with the PS VR2's eyes, same life-size stereo in all:
    - (a) **Mode 2 back on ME3 with the flare fix** (`MELE3VR.ini [VR] MetricEyeFrustum = 1`, `ASI\LE3MEVR.ini [Stereo]
      flareWindow = 1`): 14.6 x 7.3 today -> 19.2 x 11.3; every lamp's flare on its lamp in both eyes; no measurable cost.
      ME3 then runs ME1's and ME2's mode. Recommended.
    - (b) **The eyes' window on ME2 and ME3** (`[Stereo] windowEyes = 1`): the fill window today takes upstream's
      side-by-side half-eye aspect and is 56% taller than the eyes; the union gives mode 1 11.3 down instead of 7.3 at no
      pixel cost, and mode 2 a culling window no taller than the eyes. Recommended, after a look at the HUD panel in a
      scene with a HUD (Save 31 draws none).
    - (c) **A taller render for ME3** (`RenderW`/`RenderH`, multiples of 8): 3072x2216 gives 14.6 down in mode 1, 19.2 x 14.6
      in mode 2, for 28% more pixels (about +0.6 ms GPU on the 4090 at Save 31; the render thread, draw-bound, unchanged
      within the noise). The PS VR2 asks for about 17.7 x 18.9. Optional; the menu's sizes are all 16:9 today.
    - Not recommended: mode 1 widened to mode 2's sharpness (4056x1728): the same picture as (a) for 32% more pixels.
    **Answered 2026-11-17, the owner ("All recommendations"): (a) yes, (c) yes, (b) after a HUD check.** Built by lane DEF2:
    ME3's defaults are now `MELE3VR.ini [VR] MetricEyeFrustum = 1` (le3 `me2_menu.cpp` `LoadValues`' default) with `[Stereo]
    flareWindow = 1` (`plugin/FlareWindow`'s default when the key is missing) and a 3072x2216 render (the code default of
    `RenderW`/`RenderH` in le3 `d3d_capture.cpp` `ArmResolutionSpoof`; `src/halcyon/le3/runtime/MELE3VR.ini`, which
    `tools/deploy-asi.ps1 -Game LE3` and MEVR Setup install when there is none; `vr-game-settings.ps1 -Game LE3`'s ResX/ResY;
    the runner's preview). `[Stereo] windowEyes` stays 0 on both sequels until the HUD panel is seen in a scene with a HUD.
    Seen on the fake headset with the PS VR2's eyes replayed (Save 31, run `def2-lane\runs\LE3-20261117-144209`): mode 2, each
    eye 19.2 x 14.6 px/deg from a 3072x2216 target, the lamps' flares on their lamps in both eyes (a canary with `flareWindow
    = 0` put them off again), life-size as before (research/le3-flares-and-window.md section 5).
18. **ME2's render shape** (T17; lane AU4, 2026-11-18, research/le3-flares-and-window.md section 6). ME2 renders any size
    whose sides are multiples of 8, but draws no HUD on a near-square frame (0.85 of a PS VR2 eye is 2400x2448): at 4:3 and
    16:9 the HUD is whole. The first VR start now widens ME2's target to at least **16:9** (every ME2 screen has been seen at
    that shape; on a PS VR2 4352x2448, 13% more pixels than today's 4096x2304; a Quest 3 over Link 3344x1880). **4:3** would
    be 3264x2448 / 2504x1880, 25% fewer pixels than 16:9; only the HUD was checked at it. Recommendation: 4:3 after a look
    at ME2's menus, conversation wheel and room screens at that shape; 16:9 until then. **Answered 2026-11-18 ("whichever
    leads to the better user experience"), measured by lane AU9: 16:9** (4:3 broke the power wheel at the hand and saved
    about 5% of the GPU; research/le3-flares-and-window.md section 6.1).
19. **Backing up with the weapon away** (T21; lane M2W, 2026-11-18, research/29 "Backing up on the sequels"; the owner: "my
    avatar does a side strafe instead of walking backward"). Exploring, ME1 and ME2 turn Shepard to run where the stick points
    and have no backward run; the legs keep the run with the spine at its 60 degrees, so backing up the legs run 120 degrees
    off the motion. With a weapon drawn both backpedal, legs right. Options: (1) keep it; (2) `[Hands] legsTwistDeg` 90 (90
    degrees off, the spine wrung); (3) backpedal exploring with the explore run played backwards (needs the AnimTree read:
    `posture-walk.ps1 -Phases tree`); (4) backpedal with the combat branch's directional legs. Recommendation: (3) if the run's
    sync group takes a negative rate, else (4); (1) until the tree is read. Today: (1).
20. **The physical crouch's margin and a lean** (T31; lane M2W, 2026-11-18, research/23 section 8.1; the owner: "When my hand
    touches an NPC or companion, I go into a kind of crouch"). The calibration's threshold is half way down to the measured
    crouch, capped at 0.92 of standing: with the owner's shallow calibrated crouch (31 cm) a 15 cm drop crouches, and his reach
    (eyes 1.69 m of 1.86 m) did. Built, off: `[Hands] crouchMinDropM` (the crouch at least that far under standing, never below
    the calibrated crouch; 0.25: the crouch at 1.61 m) and `[Hands] crouchLeanRatio` (a drop that took the head that many times
    as far forward is a lean; 1.0). Recommendation: both (0.25 and 1.0) on every game. **Answered 2026-11-18, the owner: "Yes,
    please turn on those fixes so I can validate them in game": both on by default, every game.**

**Defaults waiting for a seen result and a yes** (not questions yet): `[SquadOrders] place` on the sequels (ME1's is on),
`[Traversal] cue`, ME3's heavy melee and traversal gestures (its stroke melee is on by default since 2026-11-17), the
galaxy projector replacing
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
| 14 | "M6 follows the first ME2 release", and that release has M3, which includes holsters (M3.4) and the reload (M3.5) (continuation plan M6, M7) | on 2026-11-15 the owner authorized M6's parity work in parallel and its packages A-G were built; holsters are still missing and the reload is linked into nothing (ledger G08, G41) | both stand: M6's code is on main, and T25 is still owed to ME2's first release; T26 is off since the owner's decision of 2026-11-16 (later, its gesture to be redesigned) |
| 15 | Squad placement is on by default (ME1) | opt-in on both sequels | settled 2026-11-17: on by default on ME2 once seen working on Save 4 (T46; ME3's code default was already on) |

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

<!-- SPDX-License-Identifier: MIT -->
# ME3 implementation plan for the unified MEVR mod

Date: 2026-11-15. Audit baseline: committed main `7a3a1158`. Status: **implementation underway; native ME3 acceptance
pending** (see the ledger below). Scope: Legendary Edition single-player ME3, including its included DLC. This extends
the same mod used in ME1 and ME2; it is not a separate VR product.

Read the [reuse audit](le3-port-reuse-audit.md), [ME3 melee audit](../research/le3-melee-audit.md), current
[ME2 continuation status](le2-continuation-plan.md) and repository instructions before taking a package.

## Implementation ledger (2026-11-15)

Implementation is now authorized. The audit baseline above remains historical; these are the first foundation changes:

- **L3-0, partial:** semantic game-mode mapping is shared. The full ME3 v59 save codec now reconstructs
  all six existing saves exactly, with independent Trilogy Save Editor full-field comparisons. Bounded private
  resource variants have offline receipts. Native loading and same-map reloading of the untouched Normandy
  manual save now pass with exact descriptor, world/pawn identity changes and stable gameplay. Native save
  creation, edited-variant persistence and combat fixture acceptance remain pending.
  See [save foundation](../research/le3-save-foundation.md).
- **L3-1, native analysis implemented:** the protected capture and Ghidra analysis establish ME3's object/function/
  name layouts and native allocators. Twelve address patterns match uniquely. The shared engine adapter and
  protected reflection probe are implemented. The initial live comparison passed 159/159; the extended save-descriptor
  metadata comparison passed 177/177, including native owned-string allocation/free, with zero mismatches.
  Loaded-player and owned descriptor getter execution pass across a same-map reload. Weapon scalar getter
  agreement and broader cached-object invalidation gates remain open.
  See [engine discovery](../research/le3-engine-access-discovery.md).
- **L3-2, candidate integration:** the pinned original ME3 renderer and thin bridge reuse controller, floor, pad, colour,
  capture and replay modules. Unsupported UI/weapon/voice services remain unavailable. First-person world conversations
  stay off. The game-thread queue cannot execute native setters until L3-1 is established and the host restarts; head
  hiding and camera/body behavior are not accepted. See [renderer integration](../research/le3-renderer-integration.md).
- **L3-3, candidate adapter:** shared hand aim, weapon pose and skeleton representation now have explicit ME3
  adapters. SMGs retain their native category with a rifle presentation fallback. Native pose and headset review
  remain pending. See [body pose](../research/le3-body-pose.md).
- **L3-4, candidate presentation:** shared main menu, calibration, rail, flat-panel pointer, stereo cinema and
  weapon/power/comfort layers are integrated. Native HUD clip geometry was observed read-only; its local bounds
  are not accepted screen crops. Wrist capture, native outcomes and separate-eye/headset acceptance remain open.
  See [presentation](../research/le3-shared-presentation.md).
- **L3-6, candidate native adapter:** shared power paging and live LE3 squad members/inventory/orders are implemented;
  the expanded native metadata comparison passed. Target policy, healing/revive, UI capacity and actual companion
  outcomes remain open. See [squad audit](../research/le3-squad.md).
- **L3-7/8, staged adapters:** native light/heavy melee diagnostics and ladder/climb/vault/evade lifecycle observations
  are wired through fresh ownership checks. Gestures remain opt-in pending native proof; heavy has no new physical
  binding yet. Shared comfort turning/hand-walk rejects native movement-ignore/custom-action states. Stable traversal
  camera handback and native/headset acceptance remain open. See [traversal](../research/le3-traversal-adapter.md).
- **L3-5, L3-9 and L3-10:** no completion claimed. Native reload, attachment-aware optics, galaxy gameplay and
  campaign/release gates still require their adapters and evidence below.

The desktop engine probes and normal descriptor loads passed and restored with zero differences. The existing chapter
candidate failed ordinary-combat admission (see below); a coherent combat fixture remains open. Next are the expanded
renderer/power metadata probe and a protected renderer preview on the accepted Normandy fixture. Each run needs the
shared game-run lock. Offline builds and tests never promote native/headset gates automatically.

Integration checks: all three development host/mod pairs and all three monolithic player ASIs compile. The 59-test
offline suite passed across the full run and focused reruns (PowerShell module resolution was fixed; the three new
context executables then built and passed). SPDX and whitespace checks passed. The shared pointer's internal `near`
member was renamed `inReach` to avoid the Windows SDK macro in monolithic builds; behavior is unchanged.

The first two protected launch attempts failed upstream of ME3 and were restored. On resumption, ME3 launched and
captured its runtime code; a second successful launch validated the corrected live-log reader and dump receipt.
Both restored with zero differences and released the run lock. Twenty runner safety tests pass. The launcher blocker
is cleared. The subsequent engine probe passed 159/159 checks and restored with zero differences; the runner now has
22 offline safety cases. The imported renderer has not been run or accepted in ME3.

The engine/body/menu integration batch passed development and monolithic builds for all three games and the 59-test
suite, with focused tests repeated after the native GUI mapping addition. Two retired clean, merged worktrees were
removed using `clean-worktrees.py`; active lanes and worktrees holding unique private evidence remain preserved.

The subsequent explicit engine-request session reused the protected runner and `ConsoleRequest` channel. It loaded
manual Save 31 from the title screen into `BioP_Nor`, then reloaded it into a distinct world/pawn instance. Both
receipts prove the requested descriptor, stable mode-0 gameplay, unchanged source save and unchanged runtime artifacts.
All tracked files restored with zero differences; the LE1 ini remained unchanged and the run lock was released.
Twenty-four runner tests and ten load-acceptance tests pass. This is desktop load acceptance, not VR, combat or
save-creation acceptance. Request processing is opt-in and runs only after the native reflection check passes.

The existing chapter candidate was then tested without save edits: it entered `Biop_End001` and progressed from
cinematic mode 8 to injured-Shepard mode 6 within the bounded 120-second observation, not ordinary mode-0 gameplay.
It therefore failed combat-fixture acceptance; no weapon scalar comparison was claimed. Its source/runtime hashes
were unchanged and full restoration again reported zero differences. Preserve this exception fixture separately
from the ordinary combat, melee and traversal requirements.

The next adapter probe passed **259/259** comparisons, zero mismatches/not-reflected rows, including squad and comfort
metadata. Normal Normandy loading passed again. The read-only HUD query resolved the PCHUD stage and all twelve
named clips, but their negative local coordinates require parent transforms before any crop is accepted. Melee and
traversal commands explicitly refused engine-probe mode. The protected run restored with zero differences and
released its lock. Native weapon scalar execution remains open because this fixture has no live weapon.

After the presentation/traversal integration, all three development host/module builds and the full 59-test suite
passed. After the squad/comfort/ownership increment, all three development pairs passed again, followed by six
focused shared/context/runner tests (including the newly registered load runner). All three monolithic builds then passed
at integration checkpoint `4c0e5daf`. Subsequent renderer native-access corrections and shared traversal/squad/voice/label
consumers are being integrated and require their own build and native validation; first VR acceptance remains pending.

The subsequent candidate adds a versioned traversal camera/input yaw contract with stale-state and unload cleanup,
shared squad panel/placement consumers, native self/ally/location power intent and healing admission, and the audited
Marksman gun gesture. The shared microphone/recognizer host is distinct from native spoken conversation handling:
LE3 must not inherit ME1's conversation reader or mute/skip behavior. Interaction labels use only explicitly supplied
valid crops until HUD parent transforms and a live target establish a measured default. Neither microphone availability
nor a visible menu proves a native companion action or accepted HUD crop.

## Outcome and boundaries

Deliver ME1/ME2's tracked body, controller aim, comfort, omni-tool/wrist menus, gun HUD, per-weapon sights, left-hand
powers, squad controls, voice and cinema experience through LE3-native adapters. Add the mechanics that ME3 requires:
deliberate light/heavy melee and class actions, ladder/evade handling, aim-mode/attachment/loadout-aware weapons,
and galaxy scanning with Reaper pursuit. Preserve native damage, resource accounting, cooldowns, AI and progression.

First-person world conversations remain deferred. Use the shared cinema presentation and native interrupt windows.
Authored seated or restrained scenes, including shuttle rides, prohibit player traversal and combat gestures while
preserving tracked head orientation and dialogue controls. Determine this from native scene/context state; ordinary
physically seated gameplay must retain movement and combat. An upstream first-person conversation feature is not
permission to enable it. Hammerhead/Mako work is outside this pass; ME3 Atlas and mounted-actor states still need an
explicit supported adapter or safe fallback so they cannot enter Shepard's normal input path accidentally.

## Architecture contract: implement each feature once

1. **Existing boundaries first.** Keep mathematical solvers, gesture/state machines and view descriptions in `core`;
   native queries/commands on the game thread in `game`/`engine`; rendering/input services in `plugin`; and small
   versioned host/mod messages. Refactor a specific LE1/LE2 conditional into an adapter only where the third game
   demonstrates the need. Do not rewrite the entire engine abstraction in advance.
2. **Semantic contexts.** Extend `engine::adapter::IsFlatScreenMode` and its existing game mapping into named context
   predicates used by UI, input, combat and capture. ME3 conversation/cinematic/GUI/movie/galaxy/orbit are **7/8/9/10/11/12**;
   ME2's corresponding values are **5/6/7/8/9/10**. Shared code must receive the semantic state, not a copied literal.
   Include player-controlled, authored-seated, traversal/action-owned, loading/dead and special-actor eligibility.
3. **Explicit capabilities.** Populate a service only when its renderer, native adapter, lifecycle and ownership paths
   exist. Distinguish unsupported, implemented-but-unverified and accepted in diagnostics/docs. Menu availability and
   theme come from the game/capability description; `not LE1` must not silently mean ME2, nor `not LE2` mean LE1.
4. **One owner.** One XR session, Present path, ProcessEvent/Tick hook chain, controller sample stream, hand-ownership
   decision and shared gesture dispatch per game. Import the LE3 renderer into the host and stand down legacy output.
   Script calls stay off Present. One physical action produces at most one admitted native request.
5. **Native authority.** Adapter results distinguish requested, rejected, started, completed and interrupted. A void
   native request is not success. Bind cached objects, pending orders and asynchronous speech to world/player/scene
   generations. Re-read weapon, target and cooldown state at dispatch. Reuse `PauseOwner`, close latches and the recent
   Thespian/menu/session fixes; reset state on load, death, tracking/session loss, recenter and actor replacement.
6. **One delivery and testing system.** Keep common menu assets, model/profile formats, recognizer and tool protocols.
   Three ASI outputs are normal. Installation manifests, game state and saves remain scoped to the selected game;
   never change another game's INI/runtime to make a test work. Extend existing runners and save tools rather than
   introducing another launcher, recorder, screenshot format or writable gameplay ledger.

## Work packages and acceptance

Each package has a mergeable implementation gate and separate native/headset gates. Source findings and scripts can
be prepared in parallel; native runs are serialized across **all three games**, not merely within each game.

| ID | Deliverable and existing code to use | Dependencies | Smallest acceptance gate |
| --- | --- | --- | --- |
| L3-0 | Coordinator: semantic context/capability contract, baseline inventory and protected fixture/runner adapter. Reuse `GameAdapter`, `tools/common.ps1`, run lock, QA receipts and existing save tools. | Starts now | Wrong-game/stale requests refused; read-only fixture inventory; no live game changes for offline tasks. |
| L3-1 | LE3 reflection, globals, allocator, frame/parameter ABI and development comparison. Reuse `ue3::Runtime`, handles/params, `Addresses`, `ReflectCheck`, SDK discovery and existing executable-analysis tools. | L3-0 contract | Locate live world/player/weapon; scalar getter and owned string output agree with native/SDK observations; loading retries and pointer invalidation work. No blanket feature activation. |
| L3-2 | Import pinned Halcyon ME3 and connect common host services via a thin renderer adapter. Reuse `Renderer`, LE2 host seam pattern, controllers, floor, colour, swapchains, captures and cinema. | Source import can run beside L3-1; native proof needs both | One session/hook owner, metric stereo in both eyes, head/controller poses, clean load/exit, responsive shared console/snapshot/recording channels. |
| L3-3 | Tracked body, gun aim and support grip. Reuse `Skeleton`, `BonePose`, `ArmIk`, `HandTargets`, fingers, guided/manual/live calibration and bounds protection. | L3-1/2 | Observe correct skeletal layout before writing it; pistol/rifle muzzle hits, two-hand release and raw-controller/IK alignment in standing and seated play. |
| L3-4 | Main menu, omni-tool projection, native screens, HUD/labels, cinema interrupts and spoken cards. Reuse the menu documents, rail, pointer, crops, `CineScreen`, `ScriptMatch`, Thespian ownership/lifecycle. | L3-1/2; wrist/body review needs L3-3 | Open/close native screens through existing input; no overlapping owners or held-click leak; actual native reply/plot selection and timed interrupts. Full spoken text requires a verified LE3 text source. |
| L3-5 | Native weapon/loadout/ammo adapter, shared holster model, production reload and per-model optics. Reuse weapon observation, support solver, sights/profiles, reload request prototype and reviewed Overlord geometry. | L3-1/3; coordinate hand ownership with L3-6/7 | Pistol/rifle firing and observed equip, empty/partial/denied/cancelled reload with conserved ammo, model optics and installed mod/aim-mode changes. Special weapons declare supported behavior. |
| L3-6 | Player powers, squad menu/orders, placement, healing/revive and radio commands. Reuse shared wheel/gestures, squad panel, parser, recognizer, scene generations and pause ownership. | L3-1/3; weapon requests need L3-5 | Correct ranked/evolved roster; self/ally/hostile/location targets; native cooldown and loadout effects; each squadmate visibly performs the requested action. Delayed speech cannot cross a scene/menu boundary. |
| L3-7 | Light/heavy/class melee and intentional contextual attacks. Extend existing swing/intent code; implement LE3 native action adapters. | L3-1/3 and ownership contract; movement presentation coordinates L3-8 | One deliberate stroke/one native request, observed admission/impact/recovery, all six legal class fixtures, no accidental heavy or cover action. See the dedicated melee audit. |
| L3-8 | Walking/turning/sprint, physical crouch/cover, evade, ladders/vaults/mantles and movement powers. Reuse comfort, traversal/cue and existing physical-cover findings. | L3-1/3; coordinate action lifecycle with L3-6/7 | Native destination/collision and action completion; no automatic cover snap; stable tracked orientation; safe restoration after interrupted actions; no traversal in authored seated scenes. |
| L3-9 | Galaxy navigation, scan pulses, alert/pursuit and orbital retrieval. Reuse table/pointer surfaces and observation/fixture principles. | L3-0/1/2/4 | Navigate and scan natively; one intended pulse/probe action; steering remains usable during pursuit; discoveries/resources follow native save semantics; alert/pursuit match native persistence and reset behavior across save/load, escape/re-entry and mission transitions. |
| L3-10 | Campaign exception coverage, install/repair/update/uninstall and release qualification. Reuse existing loader/installer/tests and evidence format. | Starts with inventory; release requires applicable packages | Selected-game files only, byte-for-byte uninstall restoration, SDK-free release requirement, campaign/DLC matrix, Quest 3/Virtual Desktop standing/seated and performance results. |

## Specific adapter work that must not disappear inside “reuse”

**Weapons and reload.** ME3 has variable loadouts and installed mods. Separate native weapon identity/category from
the four-slot LE1 body layout and four-tile squad view. A common weapon capability/profile record should carry the
native identity, grip/muzzle/support/clip/sight geometry, reload family, aim-mode/mod identity and supported actions.
Native game queries supply ammo, availability, weight-related cooldown and effects; profiles do not become a second
inventory. The physical reload request core is still production-unwired in ME2: this is shared unfinished work, not
a finished feature to copy. Inspect native admission, automatic reload, ammo transfer, animation completion and cancel
before player-paced insertion commits anything. Preserve an assisted native reload route and refusal for unsupported
weapons. Reuse model optics where useful; fall back to generic optics by profile. Cache invalidation includes installed
optic and aim mode, not just the mesh. The [optics research](../research/le2-le3-vr-optics.md) remains the starting point.

**Powers and squad.** LE3 powers are `SFXPowerCustomActionBase`; ME2's `SFXPower` field assumptions are not valid.
`BioPlayerInput.ActivatePower` retains LE2's target and original camera location/rotation parameters. Reuse the existing
LE2 VR target/camera construction, then verify LE3's reflected ABI and native admission. LE3 squad power and equip requests use input
method/selection/target parameters. Validate these with `Params` and reflection comparisons rather than passing an
ME2 argument buffer. Preserve grenade counts, shared cooldown, evolution, combo effects and loadout modifiers through
native rules. Voice calls the same validated dispatcher; do not give it a less restrictive path.

**Melee.** Follow [the source audit and six-step proof](../research/le3-melee-audit.md). Reuse swing detection but add
explicit light/heavy intent. Proposed heavy input is deliberate arming plus strike, not “swing faster.” Native class
actions own effects, damage and combos. Installed base-game package inspection now establishes all six class heavy
action mappings, their storm variants and Infiltrator's cloak variant (see the melee audit's provenance table).
Live class-array assignment, DLC overrides and action admission remain unproved. Both native melee entry points
can choose cover/storm actions first, so an ordinary swing must reject those contexts before dispatch.
Cover-synchronized attacks and Atlas melee are separate contexts, not automatic
extensions of ordinary swings. Do not directly call impact handlers to simulate success.

**Movement.** Current `Traversal::Kind` covers None/Vault/ClimbUp; ladders need a real lifecycle extension. Use native
ladder discovery/reach specs, mantle landing checks and evade admission. Separate Use, Traverse and Evade semantics
before mapping them to the shared action controls. Physical cover must preserve LE3's native protection and out-of-cover
modifiers without an invalid cover slot. Roll/lunge/Charge can move the actor, but cannot rotate the HMD with authored
camera roll. Acceptance covers the entire action and return to control, not just a successful request.

**Cinema and dialogue.** LE3 uses `BioConversationController`, separate conversation data, a conversation manager and
a different GUI handler. `GetConversationHandler` takes the player controller; native hide-wheel behavior is exposed
through `AS_HideWheel`. A `QueueReply` declaration does not make ME2's live-reply reader correct. Establish current
eligible native slots, full versus ambient dialogue, disabled persuasion/submenus and full spoken text/TLK lookup.
When full text is unavailable, label/paraphrase matching may be an explicitly limited first slice; do not fabricate
full-line cards. Keep native Shepard playback until a separately verified LE3 mute/skip path exists. Do not enable
the current non-LE2 Thespian branch: it contains LE1 assumptions.

**Galaxy.** Present the native image on the shared surface first. ME3 system scan, Reaper alert/pursuit and orbital
retrieval are distinct input states even when a coarse game-mode value is shared. Continuous steering must survive
scan/menu transitions; held triggers may not spam scans or launch on entry. Observe native fuel, discovery, retrieval
and alert changes, including save/load, escape/re-entry and mission transitions. Observe native persistence and reset
behavior rather than assuming alert must persist: the SDK exposes a transient live alert and explicit reset paths.
Do not interpret retained mineral/probe save fields
as proof of active ME2 mining semantics. Full CIC surroundings/stereo map depth are still unfinished in ME2 and need
their own presentation gate if included in ME3.

## Reuse the save/editor and test flow

The audit found **six `.pcsav` files in one existing ME3 career directory** at the standard Documents location. No
file was modified or loaded. The read-only v59 prefix now confirms Vanguard/VanguardNonCombat identities and levels
59/60. ChapterSave at `Biop_End001` is a combat-pawn candidate; the Normandy saves are noncombat and the ending
NewGamePlusSave reports injured state. Full plot/checkpoint legality still needs decoding and native confirmation.
Start with these files. There is no justification yet for making the
user replay the campaign or starting a new game to acquire the first fixture.

`tools/le2-save.py` is a strict independent LE2 v30 implementation based on Trilogy Save Editor's documented format;
it is not a generic editor that already accepts ME3. The local TSE source at `6451f23d1fba62358328dce4e87733970f36ebf1`
contains ME3 v59 data and serialization/CRC tests under `src/save_data/mass_effect_3/`. Extend the existing save-tool
approach with a versioned LE3 adapter and independent TSE roundtrip checks. Preserve its provenance/licensing boundary
([save adapter research](../research/le2-save-adapter.md)); do not copy another codec or substitute LE2's CRC/schema
without checking the ME3 format.

First inventory/decode privately, then prove byte-identical no-op roundtrip, unknown-byte preservation, invalid-format
rejection and a normal native load/save/reload. Only then produce narrow cloned variants for credits, fuel, grenades,
weapon mods/loadout, powers, war assets or galaxy state. Each edit needs a typed diff, original/output hash, native
observation and restoration receipt. Changing a story flag alone may not reconstruct actors, streaming/Kismet state,
quest prerequisites or legal class powers; prefer an existing coherent checkpoint and inspect its dependencies.

Reuse `QueueSaveGameCommand` and the existing descriptor-load design after verifying LE3's allocator and call contract.
SDK `FSFXSaveDescriptor` supplies Career/SaveName/Index/Type; Load is a distinct action from Save. Do not use a scripted
level jump as proof that the checkpoint can be reached and loaded normally. Never use `streamlevelin`.

The common deploy script accepts LE3. `tools/le2-halcyon-run` now supports a protected LE3 desktop dump-only session
with per-game process/backup identity and the shared run lock. Its LE2 renderer route remains the default;
`tools/qa-le2/session.py` explicitly refuses LE3 native fixtures until full save/runner contracts exist. Preserve
wrong-game rejection, manifests, normal load and `finally` restoration when extending this. Extend `ConsoleRequest` and `TestLoop` in the host instead
of building another file channel. Reuse `console.ps1`, `snapshot.ps1`, `record.ps1`, `replay.ps1 -Inputs` and existing
synthetic generators once that bridge exists. A tool filename or accepted `-Game LE3` flag is not native proof.

## Fixture and evidence matrix

| Fixture | What it proves | What it cannot replace |
| --- | --- | --- |
| Existing untouched ME3 save | Decode, ordinary load/save/reload, mode/player identity and restoration | Assumed combat, dialogue or range availability before inspection |
| Citadel shooting range, if acquired and confirmed | Weapon pose, sight alignment, mods, ammo/reload observation | Hostile damage/AI, melee admission, squad outcomes or story progression |
| Natural hostile checkpoint | Damage and shields/armor/barriers, targeted powers, squad behavior, combat interruption | All classes or all weapon families |
| Six legal class fixtures | Light/heavy/class melee, powers and class effects | A class-name-only edit that leaves incompatible powers/actions behind |
| Ladder/vault/evade and blocked comparison | Admission, destination, collision, completion and recovery | Comfortable HMD behavior without headset review |
| Dialogue/interrupt/seated scene | Native choices and plot effects, timed interrupts, no traversal during authored seating | First-person world-conversation acceptance, which is deferred |
| Galaxy/search/pursuit/retrieval | Scan cadence, steering, resources/discoveries/alert and save persistence | Full stereo hologram/CIC presentation |
| Special actor and DLC checkpoints | Atlas/mounted weapons, forced equipment, tutorials, exceptions and return to Shepard | Blanket campaign support from one successful base-game mission |

For every run record commit, loaded host/mod hashes, executable identity, save/variant hashes, settings, active
capabilities, commands/replay, both-eye screenshots, native before/after state and cleanup receipt. A function call or
drawn effect alone is insufficient. Quest 3 through Virtual Desktop should check raw controller overlay against IK,
hand rotation/reach, sight eye box, wrist projection, two-hand transitions, melee accidents and seated/standing comfort.
The null headset's roughly 31 fps behavior cannot establish physical-headset stutter or performance acceptance.

## Parallel execution and landing order

Initial implementation wave: one worker on L3-1 engine discovery, one on L3-2 pinned renderer import/host integration,
one on L3-0 save/runner contracts and fixture inventory. The coordinator owns `CMakeLists`, `ModApi`, service composition,
semantic mode mapping and the acceptance ledger. Give each worker an isolated worktree and explicit interface contract.

After the engine/host proof, fan out body/weapons, UI/dialogue and movement/melee/native-power research. Sequence writes
to shared hand ownership and gameplay dispatch through the coordinator; do not let reload, powers and melee each add
an independent controller poll. Galaxy and release work can continue offline while another owner has the game.

Land scoped code only after the required development, player and trilogy builds and relevant regression tests pass.
Exercise LE1 and LE2 behavior whenever shared policy changes. A native renderer restart is required to validate a new
host; mod hot reload cannot validate an old installed host. Keep unsupported features visibly unavailable and retain
working native behavior where possible. Preserve existing branches/edits; use `land-on-main.py` and the cleanup script
after reviewed integration. The user pushes.

Completion gates are **source/ABI understood → implemented/build-tested → native outcome observed → headset accepted
→ campaign/release qualified**. Do not turn these into a single percentage based on files compiled. The first bounded
proof is: correct LE3 process and one host, a normal save load, tracked body/pistol, firing, shared menu/cinema input,
and one observed light melee. Heavy/class melee, production reload, traversal, squad/powers and galaxy then earn their
own gates. No feature is accepted merely because the corresponding ME2 candidate compiled.

<!-- SPDX-License-Identifier: MIT -->
# ME3 unified-mod goal: complete status and resumption handoff

> **The work list is [docs/trilogy-plan.md](trilogy-plan.md)** (since 2026-11-16: one list for all three games, keyed by
> feature). This document stays as the record of ME3's checkpoint `0f02fd38`: the work history, the evidence behind each
> milestone and the order its author suggested for resuming. Pick work from the trilogy plan; its "Where the old plans'
> items went" maps the remaining work here to features there.

Status as of 2026-11-15, implementation checkpoint **`0f02fd38`**.
The user paused and then cleared the goal. This document records work and remaining requirements;
it does not resume implementation, authorize a game session, or claim release readiness.

## Overall goal and honest completion status

The goal is **complete ME3 parity from the implementation plan**, using the same
unified mod as ME1 and ME2. Shared VR behavior, input, menus, gestures and tooling
should remain one implementation; game-specific native contracts belong in thin
adapters. Scope is Legendary Edition single-player ME3 and its included DLC.

**Current state: substantial foundation and candidate implementation, with bounded
native rendering/body/navigation successes; not a complete playable-parity release.**
The engine can be inspected, normal saves can be loaded, Normandy gameplay and
separate-eye rendering have run, and several concrete rendering/body defects
have been corrected. Most combat-dependent features have implementation or
research but lack native outcome validation. We still have no accepted ordinary
ME3 combat fixture from this session. The physical galaxy-projector experience
is incomplete, and physical-headset qualification remains open.

No defensible overall completion percentage has been measured. The earlier
approximately 60% comment applied only to the galaxy-projector engineering task.
It must not be used as the completion percentage for the entire port. Likewise,
603 reflection checks or 71 passing tests do not count as 603 gameplay features
or establish campaign/headset acceptance.

Explicit scope decisions:

- Reuse ME1 behavior and ME2 adaptations after auditing their implementations.
- First-person conversations in the world are release scope since 2026-11-19
  (the owner changed priorities; T49, `game/SequelConversationWorld`, experimental
  and off by default). The cinema/native conversation route and head tracking stay
  the default; authored seated scenes
  must reject traversal. Do not implement free walking inside shuttle scenes.
- The Hammerhead is outside the requested current work.
- ME3 melee, its custom-action traversal, weight/cooldown behavior, weapon mods,
  grenades and galaxy pursuit require their own native semantics.
- User settings, saves and unrelated agents' work must be preserved.

## Where the authoritative work is

The latest implementation is on branch
`worktree-agent-a08810c1900a1136a`, at repository-relative worktree
`.claude/worktrees/agent-a08810c1900a1136a`, checkpoint `0f02fd38`.
Integration update: merge `ccc9325d` landed this checkpoint and the main handoffs on
local main on 2026-11-15. The original worktree is retained for its built artifacts
and private evidence. This does not mean it is installed or accepted for the player.

Main's [implementation plan](le3-port-implementation-plan.md) and
[reuse audit](le3-port-reuse-audit.md) establish the architecture/work packages,
and main now includes the newer implementation ledger. Read the ledger
at the checkpoint for the latest native evidence. This document provides the
cross-milestone summary; the reviewed integration is now on local main.
The [galaxy-specific handoff](galaxy-projector-handoff.md) contains its detailed
attempt history, native addresses, code inventory and resumption procedure.

All `research/...` and `src/...` references below refer to the implementation
checkpoint unless stated otherwise. These files are now integrated on main; use
`git show 0f02fd38:<path>` when reproducing the original evidence checkpoint.
Older introductory “untested” statements in research describe earlier stages:
use later dated evidence and the worktree ledger, while retaining their limits.
Do not silently interpret one later fixture success as validation of all cases.

## Milestone ledger

| Milestone | Work implemented/researched | Strongest recorded evidence | Still required |
|---|---|---|---|
| **L3-0: modes, saves, fixtures** | Shared semantic modes; full v59 save codec and independent TSE comparison; protected native load/reload tooling; private resource variants | Six existing saves reconstruct byte-for-byte; ordinary Normandy load and same-map reload; native CIC QuickSave creation and reload | Accepted combat/companion/traversal fixtures; edited-resource persistence; safe plot-rewind recipes and campaign validation |
| **L3-1: engine access** | Runtime code dump, Ghidra project, reflection/name/allocator contracts, native adapters and metadata checks | Successive protected probes, reaching 603/603 for the capture build; owned string/descriptor execution across reload | Feature-specific getters and mutations verified in relevant gameplay; broader invalidation/failure coverage |
| **L3-2: renderer and host** | Halcyon ME3 integration with shared host services, HDR policy, eye-copy handling, per-eye pose tags, transition guard | Visible separate-eye world captures, distinct 64 mm eye positions, menu/recenter/reload runs and exact restoration | Physical stereo fusion, wider scene/transition coverage, stale-copy negative cases, headset performance and full service acceptance |
| **L3-3: body/weapon pose** | ME3 skeleton/weapon/hand-aim adapters; controller-loss torso fallback; shared floor/menu continuity fixes | Normandy controller-loss/head-turn/reacquisition/third-person/reload tests; 1.7526 m floor/view-height evidence | Quest 3/Virtual Desktop physical alignment, gun and support grip under combat, all weapon/body variants |
| **L3-4: menus, HUD, cinema, dialogue** | Shared menu/calibration/rail/pointer/cinema/HUD layers; native GUI event adapter; pause ownership; dialogue reader and spoken-selection safeguards | Shared menu captures, native War Assets Use, mission dialogue pad selections, localized reply text on three wheels, menu height continuity | Native wrist crop/capture and Omni-tool alignment; complete GUI/pause/input lifecycle; spoken replies/interrupts; cinema and UI headset acceptance |
| **L3-5: weapons, reload, optics** | Native ammo observer and getter/identity audit; shared weapon presentation foundation | Offline/native-contract work, not accepted physical reload or optic gameplay | Combat fixture; real magazine/reserve/thermal-clip outcomes; reload mechanics; per-weapon/attachment optics and integrated ammo/element UI |
| **L3-6: powers and squad** | Shared left-hand wheel/gestures, live ME3 power adapter, dynamic squad/weapon/power menus, order/voice/placement reuse | Metadata, script/native dispatch audits, readiness/paging/parser/ownership tests | Actual casts, targets, cooldown/resource changes, grenade refusal, Unity healing/revive, companion orders/navigation, weapon switches and spoken outcomes |
| **L3-7: melee** | Native light/heavy routes and gated gesture candidates; heavy chamber-and-strike approach | Source/native-contract audit and offline staging | Correct action start, hit/damage/recovery, class/weapon variants, no accidental activation, physical feel |
| **L3-8: traversal/comfort** | Native ladder/climb/vault/evade request/observation adapters; shared comfort admission and input ownership | Source/metadata/offline tests; requests/gestures remain opt-in | Valid approach fixtures, custom-action lifecycle, stable horizon/yaw/camera handback, collision/interruption, seated denial and headset comfort |
| **L3-9: galaxy map** | Native observer/input candidates, flat-table placement, measured projector, bounded CIC residency, shared stereo capture layers, adapted post-effect target fix | Normal navigation/briefing entry; room residency and X exit; both raw map captures visible | Runtime proof of post-effect fix, CIC camera hold, exact UI/pointer projection, transitions, scanning/pursuit/rewards/persistence and headset review |
| **L3-10: release/campaign** | Existing unified build, installer and protected-test machinery retained | All three development host/module pairs and player builds compile; regression checks pass | Reviewed main integration, release/deployment validation, campaign/DLC matrix, performance, physical-headset and user acceptance |

No milestone above should be marked fully accepted merely because its candidate
code is present. Acceptance is feature- and fixture-specific.

## Work history and the problems it resolved

### 1. Audited reuse and established original ME3 engine access

The port uses existing engine/reflection abstractions, protected game-session
tools and the local original-game decompilation workflow. The installed executable
required a runtime code dump before meaningful native analysis. The resulting
local Ghidra project and dumps are reusable; do not repeat collection or seed a
new project unnecessarily. Closed third-party mod binaries were not the source
of implementation. Keep raw game code, saves and dumps private.

Initial probes progressed from 159 to 177 checks and then larger feature-specific
sets. These established object/function/name layouts, allocator ownership and
parameter contracts. The owned FString/descriptor audit matters because native
output ownership cannot be guessed from a pointer. Live checks gate gameplay
initialization. Most recently, a capture check correctly refused startup when
the generated SDK's compiled bitfield offset disagreed with native/reflection
evidence; the independent verified native contract replaced that comparison.

References: `research/le3-engine-access-discovery.md`, `src/engine/Addresses.hpp`,
`src/engine/ReflectCheck.cpp`, `tools/ghidra`, `tools/le2-halcyon-run`.

### 2. Built the save foundation and searched for coherent test fixtures

`tools/le3-save.py` reuses the existing reader/bounds/CRC/recipe/diff mechanisms.
It describes the complete known little-endian v59 format and CRC32/BZIP2; six
saves round-trip exactly. The independent local Trilogy Save Editor oracle
checks full decoded fields. Private resource variants exist, but neither a
valid checksum nor an oracle match proves a playable edited campaign state.

Normal Save 31 loading and same-map reload succeeded with the requested native
descriptor, changed world/pawn identity and unchanged source/runtime hashes.
Native door interaction and the War Room–Tech Labs–security–CIC route were used
to acquire a new ordinary QuickSave at the galaxy console, then reload it.
That shortcut removes repeated walking; it is **noncombat**.

The six-save corpus did not supply an accepted ordinary combat checkpoint.
The chapter candidate enters cinematic/injured states. Installed Citadel source
identifies the normal shooting-range entry sequence and disables saving inside;
an appropriate checkpoint should be outside the range. Save 31's Citadel access
is blocked by actual plot conditions, not merely a missing map-name edit. The
original AutoSave is at the CIC with the Cerberus assault available and was
used to reach briefing/pre-assault dialogue, but combat was not reached.

LegendSave's type-5 load did not satisfy the exact descriptor gate: the native
resume descriptor was unnamed Auto. It is not an accepted replacement fixture.
Two later changed native Normandy saves were preserved before restoration and
passed the independent oracle; neither has weapons or proves combat readiness.

ME1's before/after save-rewind workflow was audited and is available as a reuse
pattern. ME3's current adapter permits resource edits only. Arbitrary story
rewind is not already implemented or verified: map, plot, streaming, squad and
campaign dependencies require coherent recipes and native load/reload proof.

References: `research/le3-save-foundation.md`, `research/le3-combat-fixtures.md`,
the protected load receipts, existing ME1 save/dictionary tools. Do not build
another launcher/save-editor stack to solve a missing fixture.

### 3. Investigated why Halcyon's stereo flow was failing

An XR-ready message originally did not correspond to actual stereo output.
Earlier runs produced a mono fallback with no first-eye captures. A legacy
side-by-side request migrated to same-frame mode, so it was not valid SBS
evidence. The protected runner now rejects that obsolete configuration and
checks stable initialized settings and runtime artifacts.

The port had omitted ME2's HDR display-capability hook. Merely requesting SDR
still produced a float HDR backbuffer. Sharing and connecting the actual
display policy produced an 8-bit backbuffer and restored the capture boundary:
one bounded run recorded 304 first-eye captures for 304 presents. The shared
EyeCopy/Blit conversion tests separately cover HDR input, sRGB output, channel
brightness, SDR preservation and a negative raw-copy control. This is not
acceptance of HDR highlight tone mapping or the optional HDR headset path.

ME1's delayed per-eye pose-history correction was then reused in both sequels.
Five snapshots per game recorded distinct 64 mm eye positions while still and
turning, including menu/resume. Both world images were present. This is stronger
than different-looking PNGs, but physical-headset fusion remains untested.

The Back/recenter transition exposed another native rendering failure. A
blur-specialized native path bound a velocity buffer that settings had disabled.
ME1's shared motion-blur guard was adapted to seven independently verified ME3
proxy pairs. A protected run completed recenter, seven stereo captures and
same-save reload, recording 2,825 guarded conversions and zero unknown variants.
ME1's caller-plus-primary-player ownership rule also prevents auxiliary native
views from receiving the gameplay stereo edits.

References: `research/le3-renderer-integration.md`,
`research/le3-renderer-preview-evidence.md`, `research/le3-hdr-eye-copy-test.md`,
`research/sequel-eye-pose-tags.md`, `research/le3-recenter-transition-failure.md`.
Useful commits: `a6f82aa1`, `c483d55e`, `7fcb9846`, `d93d39de`, `eb4cf228`, `61681492`.

### 4. Separated desktop cadence from actual performance

The sampled configuration presented the desktop mirror once per eight frames.
At approximately 64–78 engine frames/second this implies only about 8–10 monitor
updates/second, explaining a poor-looking desktop mirror without proving the
headset had that refresh rate. Steady recorded windows were about 12.85–15.67 ms
per game frame; an instrumented capture/menu interval included a 175 ms maximum.
Those runs are not clean stutter benchmarks and GPU utilization alone did not
establish saturation. Do not attribute every slow run to the null driver.

The existing LE1 performance script needs producer seams on LE3 before its
results can be used there. Reuse it through an adapter if needed. Compare mirror
cadence 1 versus 8 on the same fixture with uncaptured steady intervals, then
measure a real headset. General performance acceptance remains open.

### 5. Corrected null-headset height, torso tracking loss and menu continuity

The shared fake headset now defaults to **5 ft 9 in / 1.7526 m** for ME1–3.
Native measurement corrected an initial floor-translation sign issue. A protected
ME3 run recorded matching stage/final eye height, active floor lock, controller
IK and an unobstructed sightline using chest-level hands. Recentered replay head
Y remains zero; adding standing height to the replay would double it.

A head-only torso solve now reuses the shared body follower when controller
grips disappear. A 14-capture Normandy run verified controller loss, held head
turn, one-/two-hand reacquisition, third-person rejection and reload; the collar
no longer obstructed the first-person view. A separate menu height drop was
traced to shared floor-lock admission and corrected. An 11-capture run retained
1.7526 m through menu opening, head turn and resume, while rejecting third person.

These are bounded view/body outcomes. The user's Quest 3/Virtual Desktop hand
position/twist alignment, physical gun grip and two-hand support still need
headset validation. Do not label synthetic controller coordinates as proof that
real hands match the IK model. ME1/ME2 compile the shared menu fix; their own
native menu continuity acceptance was not established by the ME3 run.

References: `research/null-headset-height.md`, `research/le3-body-pose.md`,
`research/le3-menu-body-presentation.md`. Commits: `3986c1f6`, `d621c843`,
`744ebaa4`, `43c18ebf`, `22a0e3c2`, `9be350ef`.

### 6. Adapted interaction, menus, native GUI and dialogue ownership

The gaze/Use failure was investigated using the native selection ABI rather
than forced actor activation. The shared scoped gaze rotation/swap restores
state on exceptional exits and has direct LE1/LE3 tests. A fixed-position
down-left look selected the War Terminal and A opened War Assets. This also
corrected an earlier mistaken identification of that table as the galaxy map.
Broader interaction prompts, aiming and UI-return cases remain open.

ME3 GUI event values and modes differ from ME1/ME2. The adapter translates the
existing shared queue, discovers live browser pages and preserves single input
ownership. The complete uncropped native image is the current source geometry;
LE1 wrist crops and browser-stacking workarounds are not valid ME3 evidence.
Native wrist capture, actual crop/aspect, UI-world figures, native art/cache
provenance and Omni-tool alignment remain unfinished. Shared menu pause uses
the native world's admission and existing pause ownership rather than opening
a second wheel; full native pause/restore lifecycle still needs acceptance.

Native Cronos docking/briefing and cabin replies advanced using a **held stick
direction plus A**. Separate direction and confirm pulses with the stick neutral
did not select the first wheel. The HUD image must be inspected separately:
the dialogue wheel is not part of the cinema source image.

The missing ME3 `mevr thespian` console route was connected to the existing
shared adapter. A subsequent run read paraphrases and full localized text on
three native wheels. A selection command correctly refused an unfocused XR
session; that is not failed text reading and must not be bypassed. Shared
identity/generation/late-transcript protections and a fresh host input-eligibility
lease guard spoken selection. Microphone, actual spoken dispatch, interrupt
windows, investigate/back and persuasion outcomes remain unaccepted.

Native conversations lock movement. The mod preserves their direction/confirm
inputs and cinema ownership without enabling first-person world conversations.
Seated no-traversal behavior still needs an explicit runtime attempt, not just
source/default-flag evidence. Shepard's native speech remains enabled.

References: `research/le3-interaction-gaze.md`, `research/le3-native-gui.md`,
`research/le3-menu-pause.md`, `research/le3-shared-presentation.md`,
`research/le3-dialogue-cinema.md`. Commits: `eed5039d`, `4f20aa07`, `d2872a55`.

### 7. Implemented combat-dependent candidates without inventing outcomes

The power port keeps the shared left-hand wheel, targeting and push/chest/gun/
raised-hand gestures. ME3's actual manager, custom-action powers, enabled state,
cooldowns and grenade accessor remain authoritative. Ammo buffs use gun contact,
buffs chest, Unity raised hand, and aimed powers the existing push. Native
ActivatePower acceptance is not proof of damage, resource consumption or action
start. No manual rank/cooldown/resource edits or LE1 synthetic projectile path
substitute for the native result.

Squad work reuses the same menu, paging, voice parser, placement and orders.
The adapter reads live companions and actual carried weapons, revalidates
membership/inventory at dispatch, and confirms an equipped weapon by observation.
Native player and companion power-dispatch paths were audited separately so a
companion order does not flush unrelated player orders. Generic artwork and
some language/target-policy cases remain. Actual squad obedience, interrupted
navigation, casts, healing/revive and microphone outcomes need combat fixtures.

Weapon work establishes a guarded ammo observer that distinguishes raw fields
from native getters and brackets object identity around reads. It is not a
physical reload implementation. Per-weapon/attachment-aware optics, native-model
optic reuse, ammo effects and integrated gun HUD require implementation and
weapon-specific validation. Existing ME1/ME2 systems remain the starting point.

Melee and traversal preserve native actions. Ladder discovery must find the
correct live approach; a true outer request may still fail native facing or
collision checks. Vaults retain native mantle data and landing verification.
Gestures are gated/opt-in, with heavy melee using a staged chamber-and-strike
candidate. Comfort rejects ignored movement/custom actions, but stable authored
yaw/horizon and camera handback during traversal are separate unfinished work.

References: `research/le3-powers.md`, `research/le3-squad.md`,
`research/le3-weapon-ammo.md`, `research/le3-melee-audit.md`,
`research/le3-traversal-adapter.md`, `research/le3-locomotion-audit.md`,
`research/le3-comfort.md`. None of these source audits replaces gameplay proof.

### 8. Started physical galaxy-projector parity

The user corrected the acceptance target: the map belongs at the physical
preview projector with the CIC present, as in ME1. A head-positioned flat table
is only an intermediate presentation. Native map entry normally unloads CIC art,
so the room requires bounded residency in addition to a primary-camera hold.

The measured projector and bounded streaming adapter now survive normal entry
and X exit. Two shared scene captures publish at its measured anchor. Normal
post-processed targets were black; turning post-processing off diagnostically
produced visible native map imagery in both eyes. Native analysis confirms the
same global-backbuffer binding bug fixed in ME1. The existing guarded target
helper was extracted and reused with checked ME3 hook addresses.

The new correction compiles but its validation launch never obtained a game
process within 240 seconds. Cleanup restored exactly and released the lock.
Main-view CIC hold, correct appearance, label/pointer projection and lifecycle
acceptance are still missing. Do not ship the raw-image diagnostic or duplicate
fullscreen and projector presentations as a completed feature.

ME2 also currently has only the opt-in flat-table route in the main/worktree
sources inspected. Its physical-projector/CIC/stereo-capture flow needs a native
adapter after auditing other pending branches. Do not copy ME3 streaming or
RVA assumptions into ME2. Its mining/probe semantics differ from ME3 pursuit.

See the [full galaxy handoff](galaxy-projector-handoff.md) for every attempt,
snapshot identifier, native contract, changed file and ordered implementation step.

## Validation and environment at pause

- Checkpoint `0f02fd38`: all three development host/module pairs and all three
  monolithic player ASIs build; separate LE1 host/module/test targets also build.
- All **71 tests pass across the full run and a justified focused rerun**.
  The sandbox initially failed three installer/temp-path/local-HTTP tests; those
  three passed outside it. No source fix was needed to make those fixtures pass.
- SPDX, whitespace and knowledge-link checks passed. This is offline integration
  evidence, not native acceptance of every feature in those binaries.
- Protected successful runs preserve receipts, logs and snapshots. Recent runs
  restored profile, game and VR files with zero differences. The last attempted
  run also cleaned up after its pre-launch timeout; game-run lock was free at
  the recorded pause. Recheck ownership/processes before any future action.
- No physical headset test was performed for this checkpoint. No production
  installation, release or push was performed. The later reviewed source merge
  `ccc9325d` is on local main, with 71 tests passing again across the full run and
  the focused rerun of three sandbox-restricted tests.
- Main contains other agents' changes. Preserve them. Keep the worktree and its
  ignored private evidence and built artifacts for the next bounded validation run.

## Remaining work and suggested order when the user resumes

1. **Reconcile source and integration state.** Read this document, the checkpoint
   ledger and relevant reuse research. Check main and other active worktrees for
   newer fixes. The checkpoint is integrated at `ccc9325d`; preserve concurrent
   cockpit/launcher edits and keep one shared implementation with explicit adapters.
2. **Finish the current galaxy rendering increment.** Runtime-validate the
   post-effect target fix, then implement primary-view CIC hold and matching
   native UI/pointer projection. Prove exit/recenter/cinematic/reload/mission
   boundaries before changing the default. The galaxy handoff details the seams.
3. **Acquire accepted fixtures.** Use protected normal loading and native travel
   to obtain coherent combat, companion, ladder/vault/melee and conversation
   checkpoints. Reuse save tooling and independent comparisons. If rewinding
   story state, build reviewed recipes with native consistency proof. A save
   editor cannot make contradictory plot/streaming state safe by itself.
4. **Validate and finish combat features.** Hand/gun pose, ammo and reload,
   elemental ammo HUD, per-weapon optics, powers, grenade counts, healing/revive,
   squad orders/voice and melee must show actual native effects and failures.
   Avoid counting request acceptance as a successful action.
5. **Finish UI and traversal presentation.** Actual wrist image/crop/art and
   pointer parity; menu/pause ownership; cinema UI, spoken reply eligibility and
   interrupts; traversal horizon/camera return and seated movement denial.
6. **Run lifecycle regressions.** Controller loss/reacquisition, session/focus
   loss, recenter, menu/calibration takeover, held buttons on transition, mod
   reload, same-world reload, travel and new pawn/world. Recheck HDR/per-eye/body
   fixes under the new capture and UI paths, including ME1/ME2 shared consumers.
7. **Qualify performance and headset behavior.** Distinguish mirror cadence,
   frame production and XR submission; use real Quest 3/Virtual Desktop for
   alignment, stereo fusion, comfort, readability and sustained pacing.
8. **Complete release gates.** Campaign/DLC scenario matrix, clean installation,
   upgrade/rollback, player-build deployment, coordinated user testing and
   explicit release acceptance. Preserve public-repository licensing and exclude
   raw game data. The user performs pushes.

Native sessions must use the existing protected request/replay/snapshot flow,
one game at a time. Read the repository's fake-headset skill. Never `streamlevelin`
and never return the game slot without asking. Null poses use the shared height
policy. Do not restart the game, Steam or VR merely because this document exists.

## Short prompt for resuming the overall goal

> Resume ME3 unified-mod parity work only after the user authorizes
> resumption. Read `docs/me3-overall-goal-handoff.md`,
> `docs/galaxy-projector-handoff.md`, the implementation ledger at checkpoint
> `0f02fd38`, and the current reuse audit. Audit main and other worktrees before
> changing shared code. Preserve completed renderer/HDR/per-eye/body fixes and
> distinguish code, offline tests, bounded native outcomes and headset acceptance.
> First finish the pending galaxy capture correction and CIC presentation; then
> obtain coherent combat fixtures and finish/validate powers, squad, weapons,
> optics, melee, traversal and UI. Reuse existing scripts and mod systems. Keep
> authored seated traversal blocked (first-person world conversations: release scope since 2026-11-19, T49).
> Do not call the port complete until the milestone acceptance gates and release
> matrix pass. Update the ledger with concrete evidence and remaining limitations.

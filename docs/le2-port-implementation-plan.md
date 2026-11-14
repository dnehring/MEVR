<!-- SPDX-License-Identifier: MIT -->
# Mass Effect 2 VR implementation plan

Date: 2026-11-13. Status: **P9 offline validation tooling implemented; native port proof remains pending**.

Use the [concise port goal](le2-port-goal.md) as the goal prompt's entry point; the documents below provide its detailed scope.

Deliver the existing MEVR interaction model in Legendary Edition ME2, beginning with a measurable combat proof. Design the shared boundaries to support LE3, while keeping LE1 functional. The [trilogy assessment](le2-le3-port-feasibility.md) covers the complete scope; the [reuse map](le2-port-reuse-map.md) identifies code and native mechanics to use in every work package.

This is the starting document for agents. A task is complete only when its stated gate is met. Compiling an ASI, viewing stereo, proving combat behavior and accepting it in a physical headset are different milestones. No sequel runtime proof is recorded by this planning commit.

The [validation tooling goal](le2-validation-goal.md) expands P9 into save editing, reproducible scenarios, story-state preparation and evidence requirements. Use it when assigning the testing infrastructure work.

P9's current [workflow](../tools/qa-le2/README.md) includes save preparation, protected staging/restoration, draft scenarios and evidence auditing. [Verification results](../research/le2-validation-evidence.md) distinguish tested offline tooling and independent editor reopening from pending native loading, story recipes and VR acceptance. [Fixture acquisition findings](../research/le2-fixture-acquisition.md) identify concrete reload/interrupt candidates and scanning dependencies.

## Product decisions

- Keep controller aim, full-body presentation, calibrated seated/standing play, physical holsters, two-hand support, powers, squad controls and usable in-headset menus.
- Implement Alyx-style physical thermal-clip handling with game-owned ammo. Preserve native damage, inventory, cooldowns, resource costs and plot progression unless a deliberate gameplay change is recorded.
- Use physical cover with equivalent native combat benefits. Retain mandatory native traversal; disabling cover must not disable vaults or mission triggers.
- First prove native galaxy/scanning and hacking interfaces on a stable VR surface. Enhanced holograms and a physical Hammerhead cockpit are separate presentation milestones.
- Prefer existing MEVR systems, then bounded adaptation of licensed external modules. CircuitLord Titanfall VR is a behavioral reference; its closed mod is not a code source. Overlord is the principal external weapon-handling candidate. See the reuse map for pinned sources and limits.
- Keep LE3 discovery findings, but prioritize completing the LE2 proof. A small LE3 foundation check can expose shared-interface mistakes without expanding every lane into two simultaneous ports.

## Agent work packages

P0-P8 remain planned; P9's offline tooling checkpoint is linked above. The lead records owner, branch, base commit, status and result link when dispatching. Source paths below refer to existing files; new adapter placement is agreed in P0 before parallel edits.

| ID and package | Inputs and dependencies | Owned scope and reusable systems | Completion evidence |
|---|---|---|---|
| P0 Integration contracts | Read `CLAUDE.md`, assessment and reuse map | Define state/action boundaries for engine access, renderer host, weapon observation, reload requests and hand ownership. Lead owns `ModApi.hpp`, shared enums, `Mod.cpp` and CMake integration | Written interface contract with game-thread ownership, units, object generations, failure behavior and one agreed integration order |
| P1 LE2 engine access | P0 contract; may research independently | `engine/GameAdapter.hpp`, `Addresses.hpp`, `ReflectCheck.cpp`, `ue3/` and sequel adapters. Reuse reflection and validation infrastructure | Runtime checks establish required globals/layouts, player and function parameters; legal test call; unload/load transition; failures disable affected features safely |
| P2 LE2 renderer host | P0 contract; minimal upstream stereo proof may precede P1 | Halcyon ME2 renderer, `plugin/HalcyonBridge.cpp`, XR/controller publication, menu services. Preserve game-specific draw fixes | One XR session; stable stereo and metric scale, head tracking, controller input/haptics, menu open/close and clean shutdown. Eye captures and frame timing; physical-headset check recorded separately |
| P3 Hands and weapon authority | P1 + P2; offline math/catalog work can start earlier | Bone pose, `ArmIk`, `Weapon`, `HandAim`, `SupportGrip`, `Sights`, `GunKick`, body calibration. Reuse existing IK and support behavior | Pistol/rifle poses align; both eyes agree; actual hits follow muzzle through one/two-hand transitions; head does not inherit recoil; tracking loss restores safely |
| P4 Physical reload and hand ownership | P0; engine-independent prototype can start alongside P1/P2; final integration needs P3 | Shared reload state machine, LE2 reload adapter, clip visuals, arbitration, weapon metadata. Reuse Overlord contact/profile/ownership concepts selectively | Ammo and native reload events agree with ejection/insertion; no duplicate commits; partial/empty/no-reserve reload, cancellation and tutorial checks pass; hardware handling evaluated |
| P5 Movement and physical cover | P1 + P2; native control/damage research can start independently | `Posture`, `Sprint`, `Comfort`, `VrCover`, `ViewLevel`, body follow; preserve LE2 use/traversal/damage paths | Use/sprint/vault remain reachable; duck/peek and seated stance agree with view/collision; correct damage front/flank/above at multiple difficulties; Charge transition captured |
| P6 Loadout and combat systems | P3 + P4 input ownership; can inventory native systems earlier | `BodySlots`, `Holsters`, powers, melee, throws, squad and voice adapters | Supported slots/classes/weapon families enumerated; no gesture steals reload; powers and squad calls retain native cost/cooldown/target validity |
| P7 UI and progression | P1 + P2 + agreed input ownership | HUD, `WorldUi`, cursor, conversation/cinema, interrupts, hacking, research/loadout; reuse panel/menu services | Dialogue choices and both interrupts work; bypass/hack and research/loadout complete; no input leaks or stuck pause after context exit |
| P8 Galaxy and Hammerhead | P1 + P2; consume P7 panel/input interfaces | LE2 navigation/orbital adapter, fuel/probe/discovery state; vehicle possession/control adapter; reuse panel/hologram and Mako primitives where appropriate | Travel, scan/probe and anomaly mission unlock survive save/load; fuel/resources remain native; Hammerhead movement/fire/retrieval/exit work. Physical cockpit is a later explicit gate |
| P9 Validation and delivery | Starts with test/save inventory; integrated acceptance needs P1–P8 | Repeatable proof cases, performance captures, setup/launch/package per game, source provenance and LE1 regression | SDK-free sequel release path under existing project policy; separate install/repair/update/uninstall state; campaign/DLC exception register; physical-headset results; release checklist |

P1/P2 own different files where possible. P4 proposes shared input changes to the lead; it does not independently rewrite `GamepadMapping` while P6/P7 edit it. Shared files and ABI changes have one integrator even when multiple agents supply patches.

## First dispatch wave

Dispatch at most three independent workers plus the lead, in separate worktrees:

1. **Engine worker, P1:** inventory LE2 globals/layouts and native weapon/reload/control entry points. Produce a small verified-access candidate and a discovery report. No gameplay-mechanic changes.
2. **Renderer worker, P2:** compare local Halcyon ME2 against MEVR host requirements; prepare isolated integration with existing bridge services and identify game-specific stereo/UI differences. Coordinate CMake and ABI edits with the lead.
3. **Interaction worker, P4 discovery:** inspect pinned Overlord candidates and current MEVR hand ownership; propose reload states, geometry and cancellation tests. Implement only game-independent pieces after P0 is agreed. Do not treat SDK names as a working native reload adapter.
4. **Lead, P0/P9:** settle interfaces, establish the proof checklist, coordinate shared-file changes and serialize native runs. Review worker results before merging.

P5/P7/P8 discovery follows as workers become available. P3 runtime work waits for P1/P2; no agent should claim a complete reload until native weapon authority is established.

## Worktree and native session rules

- Record the exact clean base commit. The main workspace may contain unrelated launcher/Mako work; do not absorb it into a port branch or reset it. If a task depends on an uncommitted change, the lead identifies and integrates that change explicitly.
- Use named branches such as `work/le2-engine`, `work/le2-renderer`, `work/le2-reload`; each has its own build directory and explicit file ownership. Do not commit directly over another agent's changes.
- Read the repository's current game-session/lease and QA instructions before a native run. One owner may launch, deploy or exercise the games at a time. A missing game process alone does not establish ownership. Offline tasks can proceed while the game is occupied.
- Back up and restore relevant save/profile/config state using the established QA workflow. Use dedicated proof saves; tutorial/progression tests may autosave. Do not change system-wide OpenXR runtime settings as part of ordinary port testing.
- Host/renderer changes require a controlled restart. A successful hot-reloaded mod test is not validation of a newly built host that was never deployed.
- Before merging implementation changes, run the appropriate focused tests, then the repository's development, monolithic and trilogy builds and CTest checks. Keep unit tests about observable invariants, not mirrored implementation details.
- Native and physical-headset gates remain separate from the merge/build result. Record the exact candidate hash, game build, loaded mod hash, scenario/save, settings, logs and captures. Do not silently replace missing evidence with an upstream claim.

## Minimal ME2 proof

Use a controlled early-combat/tutorial save plus one scene with usable cover and traversal. The test owner records exact save/map identifiers after inspection; this plan does not invent them.

| Proof | Required result |
|---|---|
| Startup and lifecycle | Correct LE2 executable, one XR session, inspectable reflection checks, loaded candidate hash, load/reload/exit without residual pause or input |
| Stereo and body | Both eyes show consistent scale and depth; hands/gun align in standing and seated play; view and collision remain coherent |
| Firing | Pistol and rifle shots hit along the observed muzzle; support release does not swing the gun toward the ammo supply |
| Reload | Empty and partial reload, no reserve, interrupted reload, weapon switch, ammo pickup and repeated insertion cannot duplicate ammo; native tutorial progression completes |
| Movement and cover | Walk/turn/sprint/use/vault; duck/peek changes relevant protection without unwanted cover camera; record exposed versus protected damage |
| Power and context | One targeted power and one forced movement/camera case; menu and conversation transitions leave no stale trigger/grip state |
| Performance | CPU/GPU timings and stereo captures for reproducible scenes; identify the limiting stage rather than claiming success from average FPS |

Passing this slice permits expanding weapon/classes and progression. It is not complete campaign support. The next gate adds conversations/interrupts, hacking, research/loadout, galaxy scanning/anomalies, squad behavior and Hammerhead. The campaign gate then covers all six classes, weapon families and included DLC, with explicit exceptions instead of implied coverage.

## Required agent handoff

Every task result contains:

- Task ID, owner, base/candidate commits, changed files, interface changes and dependencies.
- Existing system used and why; for copied/adapted external code, upstream URL/revision/path, scope of adaptation, license/notice changes and excluded engine-specific portions.
- Native function/state evidence: declaration-only, locally inspected implementation, or observed runtime behavior. Mark unresolved signatures/call order **[verify]**.
- Tests run with exact commands/results; native and headset evidence separately; outstanding failures and the next smallest experiment.
- Integration instructions, required restart/deploy, restoration steps and whether changes are ready for merge, runtime proof, headset review or release.

Use a task-specific tracked report under `research/` for original findings. Keep game dumps, extracted scripts, binaries, recordings and machine-specific data local according to `CLAUDE.md`. Update this plan's status only after evidence is linked. Do not edit historical observations into claims of current acceptance.

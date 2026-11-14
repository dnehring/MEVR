<!-- SPDX-License-Identifier: MIT -->
# Mass Effect 2 VR existing systems and code reuse map

Date: 2026-11-13. Companion to the [implementation plan](le2-port-implementation-plan.md) and [trilogy assessment](le2-le3-port-feasibility.md).

Use three distinct authorities: **the native game for rules and progression; MEVR for our established interaction behavior; external VR projects for bounded implementation reuse and design examples.** A convincing interaction in another game does not establish our engine call order, ammo accounting, animation timing or comfort.

## Titanfall VR source correction

CircuitLord's [current README](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/README.md) identifies the VR mods as closed source. Its [license](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/LICENSE) begins: “This license covers the installer source code only. Mod packages and release assets are excluded.” Checked 2026-11-13. The local `reference/` inventory contains Overlord, REFramework, vrframework and Halcyon's three Mass Effect repositories; it does not contain a CircuitLord weapon-handling source checkout. A downloaded mod package is not evidence of reusable source.

Use Titanfall's manual reloads, two-hand transitions and vehicle controls as behavioral comparisons through public documentation or an authorized play session. Do not extract/reimplement its private code from binaries. Its MIT installer can inform installation work, but cannot supply the weapon implementation. The existing [Titanfall survey](../research/02-titanfall2-vr.md) records the same distinction. For installer behavior, a previously inspected [installer source revision](https://github.com/CircuitLord/CircuitLordVRModInstaller/tree/3594ce8dabb13c7340a3bfe26af64341d53349f6) is available independently of local research files.

If a different Titanfall VR project or newly licensed source becomes available, record its exact identity, revision and license before evaluating it. Do not transfer CircuitLord's feature claims to another similarly named project. No permission request or source acquisition is needed to proceed with the ME2 plan using the sources below.

## Pinned external sources

These are inspected local revisions, not claims that a checkout is current upstream. `reference/` is gitignored. Agents can use the upstream permalinks without committing third-party reference trees; inspect the named revision before adapting code. `tools/fetch-references.ps1` fetches references but may advance them, so record any deliberate change from these pins.

| Source and local revision | Intended reuse | Boundary |
|---|---|---|
| [Halcyon ME2](https://github.com/jrush64/MassEffect2-VR/tree/4784e90), `reference/MassEffect2-VR`, `4784e90` | LE2 stereo/render hook, camera and UI integration starting point | GPL-3.0 project. Integrate into MEVR host; do not run two independent OpenXR owners |
| [Halcyon ME3](https://github.com/jrush64/MassEffect3-VR/tree/cfd4001), `reference/MassEffect3-VR`, `cfd4001` | Later LE3 renderer comparison and adapter work | Separate proof from LE2; its ABI and UI details are not interchangeable |
| [Overlord](https://github.com/Aeka0/Overlord/tree/11f3005034188894976cb8392122ad166d59f927), `reference/Overlord`, `11f3005034188894976cb8392122ad166d59f927` | Physical weapon interaction, contact geometry, hand ownership, profile structure and test scenarios | GPL-3.0 project, compatible with this project's established reuse policy. H2 engine hooks, native ammo writes and weapon assets are not portable modules |
| [REFramework](https://github.com/praydog/REFramework/tree/d1461375aee4ec3f313170f8eaad12064eb542d9), `reference/REFramework`, `d1461375aee4ec3f313170f8eaad12064eb542d9` | Targeted OpenXR/D3D implementation reference where MEVR lacks a solution | MIT; its engine stereo/gameplay hooks do not replace UE3 adapters |
| [vrframework](https://github.com/elliotttate/vrframework/tree/751863e2076c9c142553c69a73d159f483b2ab5e), `reference/vrframework`, `751863e2076c9c142553c69a73d159f483b2ab5e` | Porting guidance on timing, stereo, camera coordinates, UI, input and engine access | MIT; consult matching guide before implementing a milestone, preserve upstream attribution for copied code |

For adopted code, retain copyright/license notices and update `NOTICE.md`; use the repository's required SPDX identifier. GPL-derived implementation belongs in a clearly attributed file, with `GPL-3.0-only` under current project policy. Original MIT MEVR files that receive GPL-derived code must be relabeled accordingly. The implementing agent checks file-level notices and dependencies before adoption. This plan copies no external implementation.

## Overlord weapon module candidates

Paths below are relative to `reference/Overlord/`. Most gameplay headers live under `src/client/component/vr/gameplay/` (abbreviated `gameplay/`). Candidate means reviewed for relevance, not already extracted or validated in MEVR.

| Need | Specific source | What to reuse or learn | What must remain ME2-specific |
|---|---|---|---|
| Insertion geometry | [`physical_reload_geometry.hpp`](https://github.com/Aeka0/Overlord/blob/11f3005034188894976cb8392122ad166d59f927/src/client/component/vr/gameplay/physical_reload_geometry.hpp), `physical_reload_contact_sample.hpp` | Finite swept contact volumes and coherent gun/controller samples; small geometry routines are candidates for direct adaptation | Thermal-clip sockets, units conversion, tracked-state validity and acceptance tuning |
| Reload states and profiles | `gameplay/physical_reload_gesture.hpp`, `physical_reload_profile.hpp`, `weapon_profile.hpp`, `weapon_profiles.hpp` | Data-driven receiver/contact definitions, action transitions and gesture admission | Actual clip mechanism, game ammo events, reload speed/modifiers, supported weapon families |
| Two-hand transitions | `gameplay/support_grip.hpp`, `support_carry_rotation.hpp`; `docs/vr-foregrip-reload.md` | Release/regrip behavior; separate a valid rendered pose from permission to fire | MEVR's existing support solver and UE3 skeletal ownership; avoid wholesale replacement without evidence |
| Ownership and conflicts | `gameplay/hand_interaction/runtime.cpp`, `object_identity.hpp`, `interaction_schedule.hpp`; `docs/vr-gameplay-interaction-architecture.md` | Per-hand ownership, identity generations, one input batch and explicit settlement | Our power, squad, omni-tool and menu priorities; game-thread call dispatch |
| Reload items and lifecycle | `gameplay/reload_item.hpp`, `reload_item_runtime.cpp`, `weapon_runtime_lifecycle.hpp`, `falling_trajectory.hpp` | Cancellation, transient item identity, held/falling presentation and stale-generation checks | Native LE2 inventory remains authoritative; no copied H2 ammo ledger or native memory writes |
| Reach and holsters | `gameplay/weapon_holsters.hpp`, `body_reach_volume.hpp` | Acquisition/release hysteresis and body-relative reach geometry | Existing MEVR calibration, seated layout and expanded LE2 weapon categories |
| Regression scenarios | `tests/vr/manual_magazine_reload_tests.hpp`, `foregrip_reload_tests.hpp`, `hand_interaction_lifecycle_tests.hpp`, `physical_reload_tests.cpp` | Translate observable invariants: one commit, no stale-object insertion, support-hand continuity and cancellation | ME2 legal states, tutorial notifications and ammo/power interactions; upstream tests alone cannot validate our adapter |

`physical_reload_runtime.cpp` directly includes H2 entrypoints, scene-model, scheduler and native game modules. It is **not** a drop-in library. First isolate a small algorithm/state boundary, list its dependencies, compare with existing MEVR code, and preserve its useful invariants in focused tests. Do not import an entire second gameplay runtime merely because its license permits copying.

## Native mechanics and MEVR reuse by work package

The native leads below were found in local SDK declarations and existing research. **[verify]** means inspect the installed LE2 implementation and observe runtime behavior before wiring calls. SDK declarations establish names, not side effects, parameter safety or completion timing. Native rules take precedence over an external mod's behavior.

| Packages | Existing MEVR system | Native game system to retain | Required discovery and proof |
|---|---|---|---|
| P1 Engine | `src/ue3/`, `engine/ReflectCheck.cpp`, `GameAdapter.hpp`, `Addresses.hpp`; [reflection research](../research/34-reflection.md) | UObject reflection, native allocator and game-thread script execution | LE2 header/global layout and parameters **[verify]**; derive independently and cross-check; do not vendor SDK layouts into release code |
| P2 Renderer | `plugin/HalcyonBridge.cpp`, `mod/ModApi.hpp`, current XR input, main menu and diagnostic services | LE2 render/camera and Scaleform paths | Halcyon `src/calcview_hook.cpp`, `d3d_capture.cpp`, `me2_xr.cpp`, `convo_fp.cpp`; `docs/ME2_UI_STEREO_FIX.md`. Preserve its game-specific UI duplication/capture behavior while integrating our host |
| P3 Body and aiming | `game/ArmIk.cpp`, `Weapon.cpp`, `HandAim.cpp`, `SupportGrip.cpp`, `Sights.cpp`, `GunKick.cpp`, `BodyFollow.cpp`, calibration and floor lock | Held `SFXWeapon`, skeletal attachments, native firing/projectile/spread/damage routines | Weapon mesh/muzzle and final native shot direction **[verify]**. Do not accept a moving gun mesh while shots still follow the original camera |
| P4 Reload | Existing grip/trigger edges, haptics, `game/Vent.cpp` gesture groundwork; Overlord candidates above | `SFXGameModeDefault.TryReload`, `SFXWeapon.TryReload`, `CanReload`, `ReloadNearFinished`; native ammo and reload animation/custom-action notifications | Exact debit/credit/completion events **[verify]**; `ReloadAmmoPower` and `OnWeaponReload` declarations indicate interactions to trace. Preserve tutorial and ammo-power behavior |
| P5 Movement | `game/Comfort.cpp`, `Sprint.cpp`, `Posture.cpp`, `ViewLevel.cpp`, body follow and view anchor | Native use/action, `SFXGameModeDefault.StormOn/StormOff`, cover exit and climb/mantle actions | `TryExitCover` and `TryClimbOrMantleCover` declarations are discovery leads **[verify]**. Inspect caller/state before interception; do not block traversal by suppressing all cover-related code |
| P5 Cover | `core/vr/VrCover.hpp`, `game/VrCover.cpp`; [cover research](../research/23-cover.md#63-le2-and-le3) | LE2 `SFXGame.ModifyDamage`, cover-slot orientation and applicable cover bonuses | No native slot exists for free physical cover; bridge the damage calculation safely, including exceptions. Prove exposed/protected damage and crouch/collision behavior |
| P6 Loadout | `core/ik/BodySlots.hpp`, `game/Holsters.cpp`, `SquadWeapons.cpp` | Inventory ownership, native weapon switching and class/mission restrictions | Enumerate actual available weapon categories and active weapon transitions **[verify]**; do not equate our slot index to the native weapon enum |
| P6 Powers and melee | `PowerWheel.cpp`, `Throwables.cpp`, `SquadOrders.cpp`, `Voice.cpp` | Native power targeting/cooldowns/resources, melee, revive and squad orders | LE2 `TryMelee` and `UsePower`/`UsePowerOnTarget` variants are discovery leads **[verify]**. Identify owning class/parameters and classify self, targeted, thrown, sustained and movement powers |
| P7 Dialogue | `ConversationFlow.cpp`, `ConversationWorld.cpp`, cinema panels and input latches | Native dialogue wheel, `SFXGameModeConversation.InterruptParagon/InterruptRenegade` | Prompt timing and legal trigger window **[verify]**; these are not the generic API that cancels a conversation |
| P7 Screens | `WorldUi.cpp`, `GuiCursor.cpp`, `HudPieces.cpp`, `OmniTool.cpp`, `GameMenuRequest.cpp`, `MenuPause.cpp` | Original hacking/bypass puzzles, research costs, loadout/modification, squad and pause interfaces | Inventory screen classes and pointer/key/held-event behavior **[verify]**. Adapt the native interface before recreating its rules |
| P8 Galaxy | `GalaxyMap.cpp`, hologram rendering, world panels and cursor | `SFXGameModeGalaxy`, `SFXGameModeOrbital.StartScanning/StopScanning`, `LaunchProbe`, `ScanResources`; native fuel/probes/anomaly unlocks | Continuous versus edge input, context transition, resource deduction and mission discovery **[verify]**; avoid firing one probe per sampled frame |
| P8 Vehicle | Mako control geometry/panel/possession concepts | LE2 vehicle game mode, hover simulation, weapon and mining/retrieval events | Hammerhead control/possession, exit, targeting and retrieval progression **[verify]**; do not reuse Mako physics writes or weapon assumptions |
| P9 Delivery | `src/install/`, diagnostics, eye snapshots, build/tests and QA tools | Game executable identity, save/profile format and loading/progression | Per-game deployment/config/backup manifests, loaded hash and SDK-free release checks; imported/new saves and DLC progression |

Use the [official ME2 manual](https://eaassets-a.akamaihd.net/eahelp/manuals/mass-effect-2-manuals_PC.pdf) for player-facing behavior such as reloads, research, scanning and interrupts, not for Legendary Edition internal offsets. The [existing code survey](../research/10-vr-code-survey.md) supplies background on Overlord and REFramework; this document pins the inspected sources for implementation.

## Reuse acceptance record

Before a worker proposes copied/adapted code, its handoff records: source/revision/path; applicable license and notices; exact function/module scope; included dependencies; destination and license identifier; engine-specific code deliberately excluded; native ME2 contract used; tests and remaining runtime/headset gates. Direct reuse is encouraged where it reduces risk, but suitability must be demonstrated at the extracted boundary.

<!-- SPDX-License-Identifier: MIT -->
# Mass Effect 2 and 3 VR port feasibility and work map

Execution entry point: [ME2 implementation plan](le2-port-implementation-plan.md). Existing systems and source reuse:
[ME2 reuse map](le2-port-reuse-map.md). These turn the assessment into bounded agent tasks; implementation and runtime
proofs remain pending.

Assessment date: 2026-11-13. Scope: **Legendary Edition single-player LE2 and LE3**, including their included DLC. Original standalone releases are a separate port. Baseline: working tree at `557628cac6fb1b37aa06803326cf4ff0776406f7`, including existing uncommitted changes.

**Both ports are feasible.** We have reusable VR interaction code, trilogy build targets, and separate Halcyon renderer sources for both games. However, the current MEVR experience is substantially LE1-specific. The work includes engine integration, gameplay adapters, several new interactions, and campaign validation. Building an LE2 or LE3 ASI does not establish feature support.

Recommended sequence: establish a minimal renderer and engine-access proof in **both** sequels, develop the shared combat interaction model in **LE2 first**, then apply it to LE3 and handle its additional traversal, combat and mission modes. Keep LE1 working throughout. No executable, installation, runtime or save changes were made for this assessment; feasibility remains a source-based conclusion until those proofs run.

## What exists and what blocks reuse

| Finding in the current source | Implication |
|---|---|
| `CMakeLists.txt` has LE2/LE3 targets, disabled by default. Its Halcyon renderer and VR main-menu integration are selected only for LE1. | Build scaffolding exists; the current renderer, menu and controller bridge need sequel integration. |
| `engine/GameAdapter.hpp` returns no reflection layout, reflection globals or allocator definition for LE2/LE3. | Modules using our engine reflection layer cannot become functional merely by compiling them for another game. This is a foundational dependency. |
| `engine/Addresses.hpp` has some sequel entry points, but many skeletal, cover, conversation and engine-global addresses are zero; sequel entries lack the LE1 pattern checks. | Discover and validate each required hook per executable. Do not copy LE1 addresses or structure assumptions. |
| `game/Weapon.cpp` and `game/Holsters.cpp` contain LE1-only implementations using `BioWeaponRanged` and LE1 equipment. | The sequels' `SFXWeapon` model needs a different adapter. |
| `core/ik/BodySlots.hpp` itself defines four LE1 weapon categories and four slots. | Some assumptions extend into the shared core. SMGs, heavy weapons and flexible loadouts require an actual model change. |
| `Mod.cpp::ComfortLink` returns without enabling the current comfort integration for the legacy sequel renderer path. | Turning, walking direction, vignette and calibration must be integrated explicitly. |
| `game/GalaxyMap.cpp` uses LE1 map objects, console logic and a fixed galaxy-mode value. | Hologram drawing is reusable; map discovery, controls and game-state ownership need per-game implementations. |
| Setup detection, launching and packaging explicitly name ME1/LE1 files. | Trilogy installation and selection are separate release work. Loader recognition of all three games is only part of it. |

The older [trilogy design](trilogy-design.md) remains useful background, but is marked historical. Its button-cover proposal is superseded by physical cover, and its simple heat-sink reload proposal is not an implemented sequel reload system.

Halcyon's [ME2 renderer](https://github.com/jrush64/MassEffect2-VR) and [ME3 renderer](https://github.com/jrush64/MassEffect3-VR) provide concrete integration starting points. Local references inspected: ME2 `4784e90`, ME3 `cfd4001`, both dated 2026-09-30. Their documented stereo and head-tracking support is upstream evidence, not acceptance of our combined mod. Their source must be adapted into our host while preserving attribution and their game-specific rendering fixes.

## Complete work map

Effort labels are relative engineering assessments, not calendar estimates. **High** means substantial new behavior or uncertain engine integration; **Medium** means adapting an existing system with targeted verification. Campaign coverage is required even where the initial implementation is small.

| Area | Common work | LE2 work | LE3 work | Effort |
|---|---|---|---|---|
| Engine access and hooks | Reflection, names, allocator, object lifetime, game-thread calls, function arguments, hook ownership and executable checks | Validate LE2 layout and weapon/player classes | Validate LE3 layout and different script-frame layout | High |
| Renderer and OpenXR host | Connect controllers, haptics, host services, eye snapshots and menus to a single session; preserve metric stereo | Integrate its renderer and UI draw handling | Integrate its renderer and HUD handling separately | High |
| Stereo and visual correctness | Eye scale/frusta, clipping, UI depth, shadows, reflections, transparencies, effects, scopes, cinematics and upscaling | Validate combat effects and Normandy/mission transitions | Validate denser battles and additional effects/modes | High |
| Player body and hands | Bone-pose hook, arm IK, fingers, body follow, floor lock, head hiding, armor/casual outfits and seated calibration | Revalidate skeletons, attachments and animation ownership | Revalidate skeletons plus custom-action transitions | High |
| Weapon aiming and firing | Weapon pose, muzzle origin, actual hit direction, spread, recoil to gun/haptics, two-hand support and sights | New `SFXWeapon` adapter; ordinary and heavy weapon families | New adapter; weapon modifications, unusual firing modes and temporary weapons | High |
| Physical unloading and reloading | Ejection, supply grab, insertion, completion, ammo authority, interruption handling, visuals and haptics | Connect to reload state and ammo powers; separate heavy ammo | Connect to reload/custom-action state; inventory weapon exceptions | High |
| Holsters and loadout | Replace four-category assumptions; resolve occupied slots and reachable seated positions | SMGs, heavy weapons, class/mission availability | Variable carried loadout and weight; temporary weapons must not become permanent inventory | Medium–High |
| Input routing | Semantic actions and explicit context ownership; release latches, simultaneous actions and device capability handling | Separate reload/use, combat, wheels, orbital scan and hacking | Add evade/traversal, search mode, chase and special possession | High |
| Locomotion | Head/hand-relative walking, snap/smooth turn, sprint, collision, body follow and recenter after scripted relocation | Preserve interaction and vault access while changing cover behavior | Add directional evade, ladders, quick vault and cover transitions | High |
| Physical cover and crouch | Match visible pose, collision and game combat benefits; retain progression-critical cover actions | New damage hook and substitute for missing cover-slot orientation | New damage hook, unsafe-cover/damage-type rules and out-of-cover modifier | High |
| Powers | Target selection, gesture categories, cooldowns, valid-state checks, effects and camera comfort | Ammo powers, Charge, cloak, armor, drones and squad powers | Additional powers, combinations, movement powers and grenade resources | High |
| Melee and thrown attacks | Trigger legal game attacks once per gesture; preserve range, cooldown and targeting | Map normal melee and grenade-like powers individually | Class-specific heavy melee, combos and contextual attacks; not one universal omni-blade | Medium–High |
| Squad commands and voice | Enumerate current squad/powers/weapons, target and movement commands, acknowledgements | New roster, availability and revive semantics | New roster, power variants and weapon selection | Medium–High |
| HUD and interaction prompts | Remap captured HUD pieces and data sources; health/shields, ammo, power state, objectives, damage direction and subtitles | Replace heat display with loaded/reserve ammo; scan information | Health segments, grenade/power resources, loadout and chase information | Medium–High |
| Conversations and cinematics | Mode transitions, head-safe camera, subtitles, dialogue choice, actor visibility and restoration | Timed Paragon/Renegade interrupts | Timed interrupts plus ambient/scripted transitions | High for full parity |
| Menus and hacking | Panel capture, pointer/touch, held inputs, scrolling, back/confirm, pause ownership and localization | Research, loadout, upgrades, circuit bypass and scrolling-code hacking | Weapon bench/mods, loadout, war assets and mission interfaces | Medium–High |
| Galaxy navigation | Table/panel placement, correct context switching, ship steering, travel confirmation and resource authority | Separate orbital scanning, mineral display, probes, anomalies and fuel | System scan pulse, Reaper alert/chase, orbital search and recovered assets | High |
| Vehicles and mounted sequences | Explicit possession, input, camera, reticle and exit handling; reset after transitions | Hammerhead hover/boost/jump, aim/fire and retrieval; audit other scripted rides | Audit controllable mechs, mounted weapons and scripted vehicle sequences | High for physical cockpits |
| Audio and feedback | Listener orientation at the headset, spatial weapon/squad cues, reload and scan haptics | Remap events and actor ownership | Remap events and special-action ownership | Medium |
| Setup and release | Per-game selection, deployment, version checks, settings, backups, uninstall, logs and update manifests | Separate LE2 package and defaults | Separate LE3 package and defaults | Medium–High |
| Performance and compatibility | Measure CPU/GPU frame time and transitions per game; validate mods, resolutions and XR runtimes | Establish representative heavy scenes | Establish representative heavy scenes independently | High validation effort |
| Campaign and DLC coverage | Saves/import, tutorial triggers, death/reload, level streaming, forced equipment and unusual playable states | Recruitment/loyalty progression, final mission and included DLC | Class-specific traversal/combat, final mission and included DLC | High validation effort |

EA's original PC manuals establish the broad gameplay differences: [ME2](https://eaassets-a.akamaihd.net/eahelp/manuals/mass-effect-2-manuals_PC.pdf) describes thermal clips, scanning, research, hacking and interrupts; [ME3](https://eaassets-a.akamaihd.net/eahelp/manuals/mass-effect-3-manuals_PC.pdf) describes evade/vault, class-specific heavy melee, weapon weight and system scanning. They are not Legendary Edition hook specifications. The work map's engine conclusions come from current source; the SDK names below are discovery leads, not verified runtime contracts.

## Physical reload design

Use an **Alyx-style interaction sequence with Mass Effect's thermal clips and game-owned ammunition**. The existing LE1 vent is a quick cooling gesture; it neither transfers ammunition nor represents a complete physical reload.

Proposed flow:

1. **Eject or unload:** deliberate input at the held weapon releases its clip representation. Block firing while the mechanism is open. Preserve the game's partial-reload accounting unless a separate balance change is explicitly chosen.
2. **Acquire a replacement:** the support hand reaches a calibrated supply zone. Availability reflects the game's reserve ammo; the hand object is not a second inventory.
3. **Insert:** accept the clip at a weapon-specific socket with generous position/orientation tolerance and clear haptic confirmation. The reload owns that hand so it cannot also cast, issue a squad order or grab the foregrip.
4. **Complete:** synchronize the physical action with the game's legal reload and completion path. A rack/bolt step is optional per weapon, not assumed universal. Honor game state and applicable reload modifiers; do not add an unexplained second full animation wait after the physical action.
5. **Recover:** weapon changes, death, loading, stun, cinematics, menus and tracking loss must reconcile the held object and weapon state without duplicating ammo or leaving firing permanently blocked.

The engine research must determine **when ammo is debited and credited**, which animation events finish reloads, how auto-reload starts, and how tutorial/script notifications fire. Calling `TryReload` on insertion may be an initial experiment, but is not sufficient if it transfers ammo early, plays conflicting arm animation or allows firing before insertion. Retain a simple assisted reload option for limited reach/controllers and recovery.

Create a small weapon metadata catalog: category, grip/muzzle, support zone, clip socket, reload style and exception behavior. It should cover magazine-fed, single-shot, heavy, charged/continuous-fire and any regenerating/heat weapons discovered in the installed content. Do not impose the same clip operation on every gun.

Acceptance: empty and partial reloads, no reserves, repeated ejection, dropped replacement, cancelled reload, weapon switching, ammo pickup while reloading, ammo powers, stun/death/load, tutorial progression, both play positions and simultaneous locomotion. Observe game ammo before/after and actual shot availability; visual insertion alone is not enough.

## Movement and physical cover

The important conflict is the games' **context-sensitive action routing**. Use, sprint, cover and traversal share input paths. Reusing the LE1 A-button treatment can trigger an unwanted cover action or suppress a required vault.

Introduce explicit actions such as Use, Sprint, Traverse and Evade. A per-game adapter decides which game call is legal from the current state. Keep continuous HMD orientation independent of animated camera roll, shake or forced aim. During unavoidable root-motion travel, preserve the game's collision and script completion, then re-anchor the VR body. Use configurable vignette/fade or a stable screen for uncomfortable sequences. Free teleport locomotion and physical ladder climbing are additional designs, not prerequisites.

Physical crouching also needs a sequel-specific solution. Lowering the camera does not prove that Shepard's collision, eye height or damage handling changed. Standing and seated input must agree with the visible pose, and the view must not lower twice. Check whether native free crouch is usable before choosing an adapter; do not assume LE1's toggle exists unchanged.

The existing [cover investigation](../research/23-cover.md#63-le2-and-le3) identifies a concrete blocker: LE2 reads the cover slot's orientation when applying protection. A VR player who never snapped to a slot has no such slot. Simply answering `IsInCover=true` can therefore be invalid. LE3 has an additional out-of-cover damage modifier. Implement the physical visibility rule inside each game's relevant combat calculation, preserving difficulty, damage-type exceptions and NPC behavior. Verify cover-related accuracy, recoil and regeneration too, not only incoming damage.

Acceptance: approach/use a door next to cover; sprint past it; duck and peek; fire around a corner; vault a mandatory obstacle; evade in LE3 without rotating the headset view; perform Charge; take damage from front/flank/above; recover after a ladder or scripted relocation. Test at multiple difficulties and with seated controls.

## Galaxy travel and planet discovery

Treat map navigation, orbital scanning and chase as separate input contexts. Reuse our hologram/panel rendering and pointing infrastructure, while the game retains ownership of travel, resources, discoveries and mission unlocks.

| Context | Proposed initial VR treatment | Required verification |
|---|---|---|
| Navigation in either sequel | Stable tabletop or panel; left stick steers the Normandy; pointer selects destinations; explicit enter/back | Travel levels, relay transitions, fuel, dialogs and return to Shepard |
| LE2 orbital scan | Aim at the planet on the panel; one held action scans, another launches a probe; stick rotates/repositions as needed | Continuous hold/release, seams/poles, resource graph, scan speed, one probe per deliberate press, costs and anomaly discovery |
| LE3 system search | Dedicated scan-pulse action while steering; alert information remains visible | Each press produces one intended pulse; held input cannot accidentally spam scans |
| LE3 pursuit | Retain responsive stick steering and a stable view; avoid modal interaction that steals steering | Alert/chase transitions, escape, capture/retry and pause/resume behavior |
| LE3 orbital retrieval | Pointer/stick search with explicit probe confirmation | Correct destination, recovered item and mission-state update |

Start with the games' own map interfaces on a usable VR surface. A freely hand-rotated planet and fully reconstructed 3D map are subsequent presentation work. A successful visual hologram is not enough: discoveries, resource deductions and mission access must round-trip through the game's normal logic.

## Controls and progression edge cases

The existing [controller guide](controls.md) already uses the support hand for the foregrip, powers, squad commands, voice and the omni-tool. Adding reload grabs requires arbitration, not just another proximity check. Proposed priority: active menu or scripted context, active physical reload/object, weapon support, then optional gestures. Context entry should consume held controls until release where needed.

Conversation interrupts need their own prompt and input routing. The left/right cue must remain visible and usable on the cinema screen, and firing or reloading must not leak into the timed choice. Preserve the original response window. Check dialog-wheel control in every supported cinematic mode.

Treat power and melee families individually. Charge is a locomotion/camera event as well as an attack. A grenade-like power may use a cooldown rather than a consumable. Heavy melee varies by class. Squad healing/revival and ammo powers must call the sequel's rules rather than inheriting LE1 medi-gel or grenade assumptions.

Scripted states require a mission checklist: forced weapons, disabled holstering, injuries, stuns, deaths, possession, turret aim, chase cameras, puzzle interactions and tutorial completion. Include Hammerhead retrieval and progression in LE2; audit LE3's controllable mech/mounted-weapon sequences rather than interpreting the historical plan's “no vehicles” row as no work. Catalogue exceptions from installed base and DLC packages before claiming complete campaign support.

## Implementation boundaries and discovery leads

Keep shared math, calibration, input sampling, haptics, UI rendering and gesture state machines. Give game adapters narrow responsibilities: query current state, request a legal action, and report the result. Avoid scattering new game-name branches through gesture code.

| Boundary | Current files or candidate entry points | First investigation |
|---|---|---|
| Engine reflection | `src/engine/GameAdapter.hpp`, `Addresses.hpp`, `ReflectCheck.cpp`; `src/ue3/` | Derive sequel layouts/globals from each executable and cross-check names, fields, parameter sizes and object lifetime |
| Renderer host | `CMakeLists.txt`, `src/plugin/HalcyonBridge.cpp`, `src/mod/ModApi.hpp`; `reference/MassEffect2-VR/src/`, `reference/MassEffect3-VR/src/` | Import each renderer in isolation, then attach host services without competing XR sessions or hook owners |
| Body and weapon | `src/engine/BonePose.cpp`, `src/game/ArmIk.cpp`, `Weapon.cpp`, `HandAim.cpp`, `Sights.cpp` | Verify skeleton update timing, held mesh, muzzle and shot origin before enabling all gestures |
| Reload | SDK LE2 `SFXWeapon.TryReload`, `CanReload`, `ReloadNearFinished`, `SFXGameModeDefault.TryReload`; sequel ammo fields and custom actions | Trace entry, ammo transfer, animation completion and script notifications; names alone do not establish call order |
| Holsters and controls | `src/core/ik/BodySlots.hpp`, `src/game/Holsters.cpp`, `GamepadMapping.cpp` | Separate VR slots from game weapon enums and add context/hand ownership |
| Cover and movement | `Posture.cpp`, `Sprint.cpp`, `VrCover.cpp`, `Comfort.cpp`, `ViewLevel.cpp` | Map legal traversal and damage paths; do not globally suppress cover helpers needed for vaulting |
| Map | `GalaxyMap.cpp`; SDK LE2 `SFXGameModeOrbital`, `ScanResources`, probe state; LE3 `SFXGameModeGalaxy.ScanSystem` and orbital/search modes | Observe mode transitions and native input handlers; preserve resource and mission ownership |
| UI and dialogue | `WorldUi.cpp`, `HudPieces.cpp`, `GuiCursor.cpp`, `ConversationFlow.cpp`, `ConversationWorld.cpp`; sequel `InterruptParagon` / `InterruptRenegade` | Inventory screens and timed prompts; distinguish dialogue choices from cancel/interrupt-conversation APIs |
| Shipping | `src/install/Detect.cpp`, `Launch.cpp`, `Installer.cpp`, `tools/package-release.ps1` | Parameterize game layout and manifest; enforce separate backups, settings and uninstall state |

SDK declarations are local reference material, not new source to vendor. The existing project's release policy requires an SDK-free release path unless permission is obtained; LE2/LE3 still use the legacy SDK path. Extend our reflection layer as part of the port, following [research 34](../research/34-reflection.md), and extend package checks to the new outputs.

## Delivery order and acceptance

1. **Prove both foundations.** Independently validate sequel engine access and minimal stereo/head tracking, load a scene, survive a level transition and exit cleanly. Record eye captures and frame timing. This bounds the largest uncertainty before building interactions.
2. **Complete an LE2 combat slice.** Body/hand alignment, pistol and rifle aim, two-hand support, physical reload, holsters, movement, physical cover and one power. Verify damage and ammo behavior in game, then assess physical headset feel.
3. **Complete LE2 progression interfaces.** Dialogue interrupts, hacking, research/loadout, galaxy travel, mineral/anomaly scanning, squad actions and Hammerhead. A screen-based vehicle fallback can establish playability while a physical cockpit is developed separately.
4. **Apply shared work to LE3.** Repeat engine/body/weapon validation, then complete evade, ladders, heavy melee, grenade/power differences, loadout/mods, pursuit/search and special gameplay modes.
5. **Reach campaign and presentation parity.** Exercise all classes and weapon families, included DLC, unusual missions, imported/new saves, death/reload and transitions. Finish native map/cockpit presentation where promised. Retain an exception register with reproducible saves.
6. **Release each game independently.** Validate installer/update/uninstall, package checks, controller profiles, seated/standing use and performance. Run LE1 regression checks with each shared-core change. A complete LE2 release need not wait for LE3 campaign acceptance.

Build success, automated logic tests, in-game behavior, physical-headset comfort, campaign coverage and release readiness are separate gates. The first implementation estimate should follow the foundation proofs and the LE2 reload/cover slice; a reliable percentage or completion date cannot be inferred from shared UE3 ancestry.

## Highest uncertainty items

- Whether each upstream renderer can receive our host bridge and metric-stereo changes without substantial changes to its game-specific UI/capture path.
- The exact reload and custom-action points that preserve ammo, animation state, powers and script notifications while allowing player-paced physical handling.
- The safest way to preserve sequel cover benefits and mandatory traversal without entering the standard cover camera/state.
- The extent of mission and DLC exceptions, particularly changed player pawns, forced equipment and controllable mounted actors.
- Actual CPU/GPU headroom in each sequel once stereo, hand rendering, scopes and VR panels run together.

These are bounded discovery tasks. None of the inspected source shows a fundamental incompatibility with the proposed ports; none establishes that all these areas already work.

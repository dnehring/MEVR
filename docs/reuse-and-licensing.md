# Reuse and licensing

**This mod is GPL-3.0** ([LICENSE](../LICENSE), [NOTICE.md](../NOTICE.md)) since 2026-10-08. It
was MIT before; the user chose GPL so we can build directly on Halcyon's working MassEffect1/2/3-VR
instead of re-deriving it. Raw data: [research/06-repo-licenses.md](../research/06-repo-licenses.md).

## Rules

1. **Copy code from GPL-3.0 and permissive sources** (MIT/BSD/Apache-2.0/zlib), keeping their
   copyright and license notices, and list each in `NOTICE.md`. GPL-2.0-only code can't be
   combined with GPL-3.0.
2. **Halcyon's MassEffect1/2/3-VR and Overlord, h2-mod and Legendary Explorer (all GPL-3.0) are
   now code sources.** Halcyon's code is imported verbatim into `src/halcyon/` in its own commit
   and modified from there.
3. **All-rights-reserved code (UEVR, TF2VR) is never cloned or copied.** Learn from public docs,
   talks and play-testing only.
4. **Unlicensed ME3Tweaks code (LExASIs, LEBinkProxy)** is fine to read for patterns. Ask
   ME3Tweaks before copying anything.
5. **LExSDKv2** is consumed as a git submodule, never copied into `src/`. It's deliberately
   unlicensed (a modding gray area), and every LE native mod depends on it the same way.

## Before distributing binaries

- **Unlicensed parts in the ASI.** LExSDKv2 (deliberately no license) and LEBinkProxy's `SPI.h`
  (no license) are compiled into the ASI. GPL-3.0 section 5c needs the whole binary licensable
  under the GPL, so ask their authors (llamathings, ME3Tweaks) for permission or a compatible
  license before publishing builds. Sharing the source is fine.
- Ship `LICENSE`, `NOTICE.md` and the MinHook and Dear ImGui license files with any binary, plus
  the corresponding source (GPL-3.0 section 6).
- Modified Halcyon files carry a dated "Modified for ME VR Attempt" header (section 5a); keep it
  on any file we change there.

## What we take from each

| Project | License | Role for us |
|---------|---------|-------------|
| [MassFPS (LE1)](https://www.nexusmods.com/masseffectlegendaryedition/mods/1731) (Aphar) | "Do whatever you want … incorporating its modifications into your own mod", no credit needed | **Script-side code source.** First-person camera modes and offsets, head hiding, controller-relative movement. We credit Aphar anyway. Exception: its bundled LE1 Community Patch `CombatStorm.Initialize` variant. See [research/11](../research/11-massfps.md) |
| [vrframework](https://github.com/elliotttate/vrframework) (elliotttate) | MIT (REFramework-derived) | **Guides plus architecture.** 17-part guide on VR-ifying a closed engine (frame timing, stereo, camera math, TAA, HUD, input). Its core is a scaffold with stubs; borrow pieces freely. See [research/12](../research/12-vr-frameworks-survey.md) |
| [REFramework](https://github.com/praydog/REFramework) | MIT | **Code source.** Present hook, D3D11 eye textures and swapchain copy, OpenXR runtime abstraction (`src/D3D11Hook.*`, `src/mods/vr/D3D11Component.*`, `runtimes/`). Not its overlay, which is OpenVR-only. Fix the 7 bugs listed in [research/10](../research/10-vr-code-survey.md) when lifting |
| [MassEffect1-VR](https://github.com/jrush64/MassEffect1-VR) / [2](https://github.com/jrush64/MassEffect2-VR) / [3](https://github.com/jrush64/MassEffect3-VR) (Halcyon) | GPL-3.0 | **Our rendering base.** Working 6DOF stereo VR for all three LE games (double `FViewportClient::Draw` per present, first person, HUD, pacing; gamepad/head aim, no motion controllers). Imported into `src/halcyon/`. See [research/13](../research/13-existing-me-vr-mods.md) |
| ME2VR v0.54 / v0.5.6 download (author unknown) | None; binaries only | **Concepts only.** Motion-controlled ME2 with arm IK. Never disassemble or run it. See [research/13](../research/13-existing-me-vr-mods.md) |
| [Overlord](https://github.com/Aeka0/Overlord) | GPL-3.0 | **Code and design source.** Runtime/gameplay separation, isolated per-eye state, cylinder UI layers, controller grip calibration (Quest Touch over SteamVR and Virtual Desktop), data-driven weapon recipes, holsters, reload arbitration |
| TF2VR (CircuitLord) | Closed | **Feel reference.** Reload choreography, two-hand handling, comfort. Learned by playing |
| [UEVR](https://github.com/praydog/UEVR) | All rights reserved | Concepts from public docs only. Not cloned |
| [LExSDKv2](https://github.com/llamathings/LExSDKv2) | None (deliberate) | Submodule: UE3/ME class layouts, ProcessEvent |
| [LExASIs](https://github.com/ME3Tweaks/LExASIs) / [LEBinkProxy](https://github.com/ME3Tweaks/LEBinkProxy) | None | Patterns for hooks, build setup and loading. LEBinkProxy is the runtime loader |
| Khronos OpenXR SDK | Apache-2.0 | Dependency (loader and headers) |
| [Ghidra](https://github.com/NationalSecurityAgency/ghidra) 12.1.4 and [GhidraMCP](https://github.com/LaurieWired/GhidraMCP) 2.0 (LaurieWired) | Apache-2.0, both | **Local research tooling only, not in the repo and never shipped.** `tools/ghidra/` drives the installed copies (headless decompiling, and an MCP server on the same project). Our Ghidra scripts are our own code |
| Dear ImGui, MinHook, SafetyHook, kananlib, spdlog, glm, DirectXTK | MIT, BSD-2, BSL-1.0, BSL-1.0, MIT, MIT, MIT | Likely dependencies (all permissive) |

## Overlord ideas worth taking (code may now be copied, with its notice)

| Idea | Overlord file (for reading) | Our version |
|------|-----------------------------|-------------|
| One input frame per tick, published to every gameplay consumer | `interaction_coordinator.cpp`, `interaction_schedule.hpp` | `core/input/` |
| Controller grip calibration, rejecting uncertain data | `controller_pose_reference.hpp`, `touch_controller_reference.hpp` | `core/xr/controller_reference` |
| Menus and HUD on cylinder/quad layers with pointer input; blit on a private deferred context | `openxr_menu.cpp`, `texture_blit.cpp` | `core/ui/`, new code (REFramework has nothing for OpenXR layers) |
| Ammo HUD as a billboard at the weapon | `native_hud_capture.cpp`, `docs/vr-weapon-hud.md` | Diegetic HUD stage in `core/ui/` |
| Per-weapon recipes as data | `weapon_profile.hpp`, `weapon_registry.cpp` | `games/leN/weapons.json` |
| Two-hand aim (shortest-arc onto the controller vector, acquire/release hysteresis) | `weapon_profile.hpp`, `grip_presenter.hpp` | `core/interaction/` |
| Holster volumes on a yaw-deadzone body frame | `weapon_holsters.hpp`, `body_pose.hpp` | `core/interaction/` |
| Carry, grip and reload arbitration | `weapon_carry_grip.hpp`, `interaction_coordinator.cpp` | `core/interaction/` |
| Analytic two-bone arm IK and finger posing | `hands/pose_solver.hpp` | `core/ik/ArmPose.cpp`: arm orientation, elbow pole and subtree transfer adapted (GPL-3.0, NOTICE.md). UE3's `SkelControlLimb` (H3) has no right-arm control in Shepard's AnimTree |
| Second eye with isolated view state and history | `engine_stereo_*.cpp` | UE3 split-screen view states (H2 in [research/08](../research/08-ue3-hypotheses.md)) |

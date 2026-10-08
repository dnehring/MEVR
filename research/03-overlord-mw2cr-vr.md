# 03: Overlord (MW2 Campaign Remastered VR, Aeka0)

Repo: https://github.com/Aeka0/Overlord (the old URL `Aeka0/h2-mod-vr` redirects here).
Cloned to `reference/Overlord` at `11f3005` (see [06-repo-licenses.md](06-repo-licenses.md)).

> **Name collision:** ME2 has a DLC that's also called *Overlord* (the Hammerhead missions). In this
> project, "Overlord" means Aeka0's MW2CR VR mod unless stated otherwise.

## Status and licensing

- **GPL-3.0** for project code. Third-party code and resources keep their own licenses
  (`licenses/`, `THIRD_PARTY_NOTICES.md`). [Overlord README, GitHub API]
- **Beta.** Expect adaptation issues in some scenes and scripts. Mods made for the non-VR client
  aren't expected to work. [Overlord README]
- **Acknowledged upstreams:** H2-Mod (alicealys, GPL-3.0), IW6x, S1x, ZoneTool, gsc-tool,
  CoDLuaDecompiler, **REFramework**, plus related work from Valve and Khronos. [Overlord README]
- **Update 2026-10-08:** the project is now GPL-3.0, so Overlord's code may be copied (with its
  notice). The rest of this line predates that.
- **For us (MIT project):** design reference only. Copying code would force GPL-3.0 on us. If a
  specific engine-agnostic module would save a lot of work, ask Aeka0 to relicense or dual-license
  it under MIT. Some of their OpenXR code may derive from REFramework (MIT), and we can take that
  original directly.

## Features (from README)

- Native stereoscopic rendering, 6DOF head and hand tracking.
- **Weapons:** two-handed hold and aim with an off-hand grip; body holsters; picking weapons up
  from the world; physical reloading and chambering that varies by weapon design.
- **Equipment and world:** throwables, melee, night vision goggles, selected mission props.
- **Campaign:** VR controls and view changes for vehicles, mounted weapons, climbing and scripted
  camera sequences.
- **UI:** the native game interface is presented in VR. A desktop launcher in English and Chinese.
- **Body or arm IK:** not mentioned in the README. The runtime doc implies there's no arm or
  body model. **Corrected by the code survey ([10](10-vr-code-survey.md)):** `hands/pose_solver.hpp`
  is a two-bone arm IK on the viewmodel arms, and the ammo HUD is a billboard at the weapon. There
  is still no full body, legs or head hiding.

## Build and layout

- Top level: `.github/ assets/ data/ deps/ docs/ licenses/ premake/ src/ tests/ tools/`, plus
  `premake5.lua`, `generate.bat`, `version.json`, `CONTRIBUTING.md`, `SECURITY.md`.
- C++, Visual Studio 2022 (v143) or 2019 (v142), Windows SDK, Premake5 (in `tools/`). Git
  submodules under `deps/` are required.
- Build: `git clone --recurse-submodules`, run `generate.bat`, then
  `msbuild build\overlord.sln /t:client /m:2 /p:Configuration=RelWithDebInfo /p:Platform=x64`.
  Output: `build/bin/x64/<Config>/overlord.exe`.
- Runtime layout: `overlord.exe`, `openxr_loader.dll`, `overlord.vrmanifest`, `steamvr/`,
  `vr_input/`, `h2-mod/`. It ships as a **custom client exe** (H2-Mod lineage), not a proxy DLL.

## VR runtime and rendering architecture (from `docs/vr-runtime-rendering.md`)

- **Backends:** OpenXR by default, OpenVR as a manual fallback (chosen in the launcher). Both run
  on the actual D3D11 Present owner. Validated with Meta Quest controllers on SteamVR/OpenXR.
- **Stereo:** per-eye "owner transactions" run in sequence on the owner thread. The left eye
  executes the native record. The right eye is a **separate transaction with isolated camera
  state, models, dynamic uploads, targets and history**. The doc explicitly warns against making
  the second eye by "simply repeating `R_RenderScene`". Scene data is published early and read
  only after H2's native job waits finish. No alternate-eye or engine multi-view approach is
  described.
- **OpenXR feed:** the game's D3D11 device is bound to the XR session only after adapter-LUID and
  feature-level checks. A native ring converts eye output to linear RGBA16F, then a
  state-restoring blit copies it to the XR swapchain format on the same queue. Submission uses the
  original SDK poses and FOV, and runtime waits never hold the GPU queue. Projection bounds are
  stored per ring slot.
- **OpenVR:** non-shared textures on the same device; submitted pairs kept until a successful
  `WaitGetPoses`.
- **Menus and UI** (`openxr_menu.cpp`): native UI and movie textures shown as OpenXR **cylinder
  layers** (quad layer as fallback), with pointer input mapped onto the layer. Menu capture uses
  encoded premultiplied color, decoded before linear composition. Fullscreen video needs a native
  playback owner. Backdrop: `menu_backdrop.cpp`. In-world gameplay HUD placement isn't described.
- **Hands:** grip and aim poses come from predicted OpenXR action spaces (`openxr_input.cpp`).
  Grip drives the gameplay hand; aim drives firing and menu pointers. Wrist calibration: for
  SteamVR/OpenXR the static `openxr_grip` metadata is copied from the controller model
  (`steamvr_controller_reference.cpp`) and its inverse applied. For **Oculus Touch on
  VirtualDesktopXR** there's a canonical legacy frame (`touch_controller_reference.hpp`). Missing
  or ambiguous reference data rejects the grip rather than guessing.
- **Engine hooks (H2-specific):** `engine_stereo_renderer.cpp`, `engine_stereo_binding.cpp`,
  `engine_scene_completion.hpp`, `engine_stereo_owner_pass.cpp`, `engine_stereo_eye_resources.cpp`,
  `native_post_aa.cpp` (FXAA/SMAA per eye with separate histories), `native_render_session.cpp`.
  Desktop overlay observers validate H2 instruction bytes before installing.

### Portability map (as the doc describes it)

| Layer | Files | Portability |
|---|---|---|
| Backend selection, lifecycle, settings | `vr_component.cpp`, `runtime_backend.cpp`, `runtime.cpp` | Mostly agnostic |
| OpenXR / OpenVR adapters | `openxr_runtime.cpp`, `openvr_runtime.cpp`, `openxr_startup.hpp` | Agnostic |
| Input and pose reference | `openxr_input.cpp`, `controller_pose_reference.hpp`, `steamvr_controller_reference.cpp`, `touch_controller_reference.hpp` | Agnostic |
| UI layers and blit | `openxr_menu.cpp`, `texture_blit.cpp`, `menu_backdrop.cpp` | Agnostic (D3D11/OpenXR) |
| Native eye ring and AA | `native_render_session.cpp`, `native_post_aa.cpp`, `native_stereo_source.hpp` | Boundary agnostic, contents H2-specific |
| Engine integration | `engine_stereo_*.cpp`, `engine_scene_completion.hpp` | H2-specific |

## Gameplay architecture (from the gameplay README)

- **Carry and grip decisions:** `weapon_carry_grip.hpp`, working on copied inputs, validated
  contacts and the VR inventory. Host adapters `weapon_carry_runtime.cpp` and
  `carry_interaction.hpp` admit the server frame, coordinate native selection and inventory
  commits, and publish presentation snapshots.
- **Reload:** support and magazine handoffs stay in their domain runtimes and commit through carry
  after arbitration. Reload-item tokens are presentation metadata, not native ownership.
- **Hands:** `hands/` handles rig binding, solving and presentation dispatch.
  `hands/pose_solver.hpp` and `hands/pose_math.hpp` cover the bone hierarchy and finger posing.
  `spatial_math.hpp` has the vector, quaternion and transform primitives.
- **Weapons:** recipes in `weapon_registry.cpp`, scene data in `weapon_scene.hpp`, plus a
  configuration guide at `weapons/README.md`.
- **Coordination:** `interaction_coordinator.cpp` (domain dispatch), `hand_interaction/runtime`
  (arbitration and publication), `interaction_schedule.hpp` (phases). **One runtime input frame is
  published to all consumers. "Gameplay must not independently poll SteamVR."**
- Further docs: `docs/vr-gameplay-interaction-architecture.md`, `docs/development.md`.

## Takeaways for ME VR (design, not code)

1. Decouple: XR runtime → one input frame per tick → gameplay domains → native commits.
2. Second eye = isolated view state and history. In UE3 that suggests one `FSceneViewState` per
   eye (see [08-ue3-hypotheses.md](08-ue3-hypotheses.md)).
3. Menus and HUD on cylinder layers, with a quad fallback and pointer input mapped onto the layer.
4. Reject uncertain controller calibration rather than guess.
5. Per-weapon recipes in data (registry), not code.

# 10: VR code survey: REFramework (MIT) and Overlord (GPL-3.0), from cloned source

Read-only survey, 2026-10-07. REFramework at `d1461375` (2026-09-16), Overlord at `11f30050`
(2026-10-08). Shallow clones without submodules. Paths: `RF` = `reference/REFramework`,
`OL` = `reference/Overlord`.

## Corrections to earlier research (web-only)

| Earlier claim | From the code |
|---------------|---------------|
| "Overlord has no arm or body model" ([03](03-overlord-mw2cr-vr.md), from its runtime doc) | **Wrong.** `gameplay/hands/pose_solver.hpp` is an analytic **two-bone arm IK** on first-person viewmodel arms. There's still no full body, legs or head-hiding |
| HUD placement not described | Overlord renders the **ammo HUD as a billboard at the weapon** (`native_hud_capture.cpp`, `docs/vr-weapon-hud.md`). No wrist HUD |
| "Build HUD layers on REFramework's overlay code" | **Not possible.** REFramework's `OverlayComponent` is **OpenVR-only**. Its OpenXR path submits only a projection layer (no quad or cylinder layers). We write our own OpenXR UI layers |

## REFramework: how its D3D11 + OpenXR path works

### Present hook (`RF/src/D3D11Hook.cpp`)

- A **vtable pointer swap** (kananlib `PointerHook`) on Present (slot 8) and ResizeBuffers
  (slot 13). It's installed via a dummy NULL-driver device and swapchain, with threads suspended,
  and temporarily restores the original bytes of `D3D11CreateDeviceAndSwapChain` for ReShade
  compatibility.
- `present()` (124-210) runs `on_present`, then the original Present, then `on_post_present`.
  `ignore_next_present()` lets VR skip the desktop Present.
- The ImGui menu is drawn **after** the VR copy, so it isn't in the eye images
  (`REFramework.cpp:908-991`).

### Eye textures (`RF/src/mods/vr/D3D11Component.cpp`)

- **`setup()` (438-532):** copies the backbuffer into `m_backbuffer_copy_tex` and creates
  left/right eye textures as **R8G8B8A8_UNORM at backbuffer size**. Non-RGBA8 backbuffers are
  converted with DirectXTK `SpriteBatch` under a `DX11StateBackup` (16-91).
- **`on_frame()` (191-417):**
  - The eye is chosen by `m_render_frame_count % 2`. Even frame: `copy(0)`. Odd frame: `copy(1)`,
    then `end_frame()`.
  - The desktop mirror blits the left eye back.
  - `CopyResource` needs identical sizes, so REFramework **spoofs the engine's render size** to
    the HMD's recommended size (`VR.cpp:95-231`; engine-specific).

### OpenXR setup and frame submission

- **Graphics binding** (`OpenXR::initialize`, 534-580): calls `xrGetD3D11GraphicsRequirementsKHR`
  but **doesn't validate the adapter LUID**, then chains `XrGraphicsBindingD3D11KHR`.
- **Swapchains** (582-680): one per eye, **hard-coded `R8G8B8A8_UNORM_SRGB`**, never enumerated.
  Size is `recommendedImageRect × resolution_scale`.
- **`copy()` (709-773):** acquire, wait, `CopyResource`, release, on the immediate context in the
  Present thread.

### Frame loop (default EARLY sync)

| Step | Where | What happens |
|------|-------|--------------|
| Game thread, start of the left-eye frame | `VR::on_pre_begin_rendering`, `VR.cpp:2946-3039` | `xrPollEvent`, then `xrWaitFrame`, `xrBeginFrame`, `xrLocateViews`/`xrLocateSpace` (`OpenXR.cpp:16-134`), then the camera update |
| Present #1 | render thread | Left eye: `copy(0)` |
| Present #2 | render thread | Right eye: `copy(1)`, then `xrEndFrame` with one projection layer |
| Game thread, before the next frame | `WaitRendering` | Waits up to 333 ms for the right-eye Present to finish |

- **Reference space:** `stage_space` is actually `XR_REFERENCE_SPACE_TYPE_LOCAL`.

### Stereo: tied to the RE Engine, doesn't port

- **Synchronized (default):** re-executes the RE Engine application chain from WaitRendering to
  EndRendering a **second time in the same tick** (`VR.cpp:3063-3202`), with physics and movies
  stripped out.
- **AFR (`m_use_afr`):** alternates eyes across ticks.
- **Per-eye matrices:** injected by hooking the engine camera's projection and view getters
  (`VR.cpp:233-309`).
- **Takeaway:** only the Present-side copy and submit logic is engine-agnostic.

### Runtime abstraction and input

- **`VRRuntime.hpp`** (130 lines):
  - Virtual interface: `synchronize_frame`, `update_poses`, `update_matrices`, `consume_events`,
    `get_width/height`, `update_input`.
  - Shared state: per-eye `projections` and `eyes`, plus mutexes.
- **`OpenXR.cpp`** (1350 lines):
  - Actions are parsed from an OpenVR-style `actions.json` into one action set, using **grip pose
    only (no aim pose)**.
  - Default bindings are in `OpenXR.hpp:214-233`, with profiles for simple, Touch, Go, Index, WMR
    and Vive. Bindings are validated by incremental `xrSuggestInteractionProfileBindings`.
- **Projection:** Khronos `xr_linear.h` (**Apache-2.0**) `XrMatrix4x4f_CreateProjection(GRAPHICS_D3D, tan angles, near, far)`.
  Engine FOV and aspect are derived back from P (`VR.cpp:1513-1526`).
  - **Caveat:** `on_camera_get_view_matrix` uses the *opposite* eye's transform un-inverted and
    relies on mirror symmetry. **Use `inverse(eyeTransform)` instead.**

### Liftability

| File | Verdict |
|------|---------|
| `runtimes/VRRuntime.hpp` | As-is (swap `sdk/Math.hpp` for our glm typedefs) |
| `runtimes/OpenXR.hpp/.cpp` | Mostly as-is: remove RE-specific actions, replace `get_persistent_dir`, fix the bugs below, **add the aim pose** |
| `D3D11Component.*` | Logic liftable, glue rewritten (`DX11StateBackup`, `TextureContext`, init, swapchain creation, copy) |
| `D3D11Hook.*` | Liftable (needs kananlib, **BSL-1.0**) or reimplement the vtable swap |
| `OverlayComponent.*` | Not useful (OpenVR-only) |
| `VR.cpp` | Heavy rewrite. Extract: OpenXR init (883-1079), input init (1081-1102), `update_hmd_state` ordering (1358-1407), recenter math (1575-1599, 2129-2133), pose getters (3925-4073) |
| `Bindings.cpp` | Replace |

### Bugs to fix if we copy REFramework code

1. **Dangling pointer:** the `XrCompositionLayerProjection` in `end_frame` is used after its
   scope ends (`OpenXR.cpp:1307-1338`). This is undefined behavior; hoist it.
2. **Right-eye SpriteBatch draw** uses the position overload, which places it at (1,1) instead
   of filling a destination rect (`D3D11Component.cpp:320,409`). Inferred, not run.
3. **Bindings save and load paths differ** (save is relative to the working directory, load uses
   the persistent dir).
4. **Dead `ends_with("/value")` check** (`OpenXR.cpp:533`).
5. **No LUID or feature-level validation.**
6. `apiVersion = XR_CURRENT_API_VERSION` may be rejected by 1.0-only runtimes.
7. **Swapchain format isn't negotiated.**

### Licenses

- **REFramework:** MIT, "Copyright (c) 2019 praydog". VR files have no per-file headers.
- **Third-party notices** are in `RF/src/LicenseStrings.hpp`: OpenXR Apache-2.0, DirectXTK MIT,
  glm, imgui, minhook, spdlog, nlohmann json, and others.
- **Checked via the GitHub API** (2026-10-07):

  | Dependency | License |
  |---|---|
  | `cursey/safetyhook` | **BSL-1.0** |
  | `cursey/kananlib` | **BSL-1.0** |
  | DirectXTK | MIT |
  | OpenXR-SDK | Apache-2.0 |
  | imgui | MIT |
  | minhook | BSD-2-Clause (GitHub reports NOASSERTION) |
  | glm | MIT or Happy Bunny (GitHub reports NOASSERTION) |

  All are permissive and compatible with our license (MIT then, GPL-3.0 since 2026-10-08).

## Overlord: architecture (GPL-3.0; copyable since we went GPL-3.0 on 2026-10-08)

`OL/THIRD_PARTY_NOTICES.md:87-92` says its D3D11/OpenXR architecture was researched with
reference to REFramework.

### Runtime and UI (paths under `OL/src/client/component/vr/`)

- **`openxr_runtime.cpp`** (1958 lines): a **Present-anchored loop.**
  - **Before Present:** blit the eye pair into the swapchains, prepare menu layers, `xrEndFrame`.
  - **After Present:** `xrWaitFrame`, `xrBeginFrame`, locate views and spaces (LOCAL), and sample
    input at `predictedDisplayTime`, then publish per-eye offsets and tan-FOVs to the stereo
    renderer for the next frame.
  - **Session binding:** bound to the game's D3D11 device after LUID and feature-level checks.
  - **Swapchain format preference:** RGBA16F, then RGBA8_SRGB, BGRA8_SRGB, RGBA8.
  - **Rejects canted-eye HMDs.**
- **`openxr_input.cpp`:**
  - One `gameplay` action set: move, turn, sprint, jump, menu, recenter.
  - Per hand: trigger, squeeze, trigger_touch, primary, secondary, **grip pose, aim pose**, haptic.
  - Binding recipes for Touch, Index and Vive.
- **`openxr_menu.cpp`:**
  - Per-surface swapchains as **`XR_KHR_composition_layer_cylinder`, or `XrCompositionLayerQuad`**
    as fallback, in LOCAL space with source alpha.
  - Head-anchored once per menu session, with depth-ordered stacking, a dim backdrop, a cursor
    layer, a movie theater, and aim-ray pointer mapping.
- **`texture_blit.cpp`:** same-device fullscreen-triangle blit, recorded on a **private deferred
  context** and run with `ExecuteCommandList(list, TRUE)` so the game's immediate-context state
  is restored.
- **Controller references:**
  - Optional rigid transform onto the grip pose, rejected if not rigid.
  - Virtual Desktop + Touch canonical frame: ±7 mm X, −1.83 mm Y, 101.9 mm Z, 20.6° pitch.
  - The aim pose is never altered.

### Stereo (`engine_stereo_renderer.cpp`, 3445 lines)

**Synchronized stereo by deep backend instrumentation.**
- Patches call sites around the renderer's end-frame, scene-record generation and PostFX.
- Publishes both eye views **before** scene generation.
- Eye 1 runs a **cloned** scene record with isolated per-eye TAA/SSR history.
- Culls with the union of both frusta.

### Gameplay

- **Arm IK (`hands/pose_solver.hpp`, 389 lines):**
  - Analytic shoulder→elbow→wrist two-bone solve with a 1.25× stretch limit.
  - The elbow pole swings an outward/down rest bend toward the reach direction, avoiding
    singularities.
  - One bend plane prevents elbow roll flips.
  - Weapon bones follow the control wrist through an authored grip offset.
  - Writes are transactional.
- **Two-handed aim** (our own summary):
  - The rear hand gives position and roll.
  - The support hand engages near the authored foregrip, with hysteresis: acquire 0.10 m,
    release 0.22 m, hands at least 8 cm apart.
  - Orientation: rotate the authored rear→support vector by the rear-hand orientation, then apply
    the shortest-arc rotation onto the real rear→support controller vector. This keeps roll and
    avoids world-up singularities.
  - The support hand's visual pose blends in over 0.1 s. On release, the relative rotation is
    frozen during blend-out so reaching for a magazine doesn't steer the gun.
- **Weapon recipes** (`gameplay/weapon_profile.hpp:38-75`), about 47 weapons, registered at
  compile time and matched by viewmodel model name:
  - Identity: `id`, `receiver`, `variant`, `aiming` (rear_hand or two_hand).
  - Grip: `wrists[2]` anchors, `authored_rear`, `acquire_meters`, `release_meters`,
    `blend_seconds`, `fingers` joint rotations.
  - Visibility: `equip_rest`, viewmodel part visibility.
  - Optional mechanics: reload, cylinder, tube, break-open, launcher, melee, shield.
  - Extra grips: support and control grips, `fixed_support_position`.
- **Holsters** (`gameplay/weapon_holsters.hpp`):
  - Layout in meters from the head on an estimated body frame: waist width .23, waist down .60,
    back distance .22, back down .24, waist radius .16, back radius .20.
  - The body frame (`body_pose.hpp`) uses a **35° yaw deadzone** with damped follow and no
    pitch/roll.
  - Slots: left waist, right waist, back, overflow, abdominal. Waist guns stow muzzle-down with
    an 8° inward cant.
- **Interaction coordinator:** one batch per input frame, in order: report, sync, collect,
  resolve (arbiter), settle, carry grips, world use, publish.
- **HUD:** the ammo HUD is captured into a texture and shown as a camera-facing billboard at the
  weapon, drawn into both eyes at weapon depth. Prompts, directional UI, damage screen and menus
  each have their own surface.
- **Not present:** full-body or leg IK, head hiding (first person is viewmodel-only), wrist HUD.

### Design rules worth adopting (from `docs/vr-gameplay-interaction-architecture.md`, `docs/development.md`)

1. **One immutable `VrInputFrame` per frame.** No consumer polls XR directly; edges are generated
   exactly once; stale or non-finite data is rejected; after a focus or context change, input
   waits for a neutral release.
2. **Explicit contexts** (Frontend, Menu, Paused, Cinematic, Vehicle, Dead), each with its own
   action set.
3. **The game engine stays authoritative** for inventory, ammo, damage and scripts. VR never
   keeps a writable second copy. Mutations go intention → transaction → idempotent settlement.
4. **Hooks are fingerprinted and fail closed.**
5. **One global interaction arbiter** with per-hand leases.
6. **Combat:** shots follow the muzzle axis. ADS never moves the camera. Recoil goes to the gun,
   never the head.
7. **Test discipline:** offline PASS is never reported as hardware acceptance. Release builds
   treat warnings as errors.

## Open questions for ME LE

- **Backbuffer format?** It decides whether a straight `CopyResource` works or a conversion is
  needed. Check with RenderDoc.
- **Does UE3 in ME LE use reversed-Z?** `xr_linear`'s projection assumes standard [0,1] depth.
- **Which OpenXR API version** does REFramework's pinned SDK expose?

<!-- SPDX-License-Identifier: MIT -->
# LE2 renderer integration discovery

Offline P2 source audit, 2026-11-13. **The licensed ME2 renderer is a useful
foundation, but it is not an interchangeable LE1 renderer library yet.** Its
DXGI-proxy startup, render-thread game mutations, private OpenXR loader and
competing hooks must be adapted before a host candidate can be run. No source
was imported, build performed, binary deployed or game request issued for this
audit. ME1 remains occupied; elapsed scheduling time does not transfer ownership.

This report implements the discovery part of the
[port plan](../docs/le2-port-implementation-plan.md) and
[reuse map](../docs/le2-port-reuse-map.md). It supplies P2's implementation detail
for the [integration contract](../docs/le2-integration-contract.md); it does not
define a new shared ABI or establish that an engine layout is valid.

## Source identity and scope

Inspected upstream: **jrush64/MassEffect2-VR**, commit
`4784e90649a0398c6812b89fc09f4a0dbcce6441`, clean local checkout under
`reference/MassEffect2-VR`. [Pinned source tree](https://github.com/jrush64/MassEffect2-VR/tree/4784e90649a0398c6812b89fc09f4a0dbcce6441).
The local host comparison is commit
`b5811bf52378a5d7d7e019821d1b538b5734b41c`.

Upstream has **12 C++ translation units and 12 headers** in `src/`. Its Visual
Studio project builds a `dxgi.dll` proxy, not an ASI or host renderer library.
The following is the complete source import/adaptation inventory. Proposed
destination is `src/halcyon/le2/`; paths in the first column are upstream `src/`.

| Files | Import and adaptation responsibility |
|---|---|
| `d3d_capture.cpp`, `.h` | Keep LE2 capture, UI classification, factory/swapchain hooks and draw/clear sequencing. Adapt ownership of Present, display/device hooks, exit and capture seams. Remove engine/script work from Present. |
| `calcview_hook.cpp`, `.h` | Keep LE2 view construction, SFR replay and per-eye state handling. Gate executable addresses through P1, publish coherent view transforms, reconcile metric translation/IPD and disable competing game aim writes. |
| `me2_xr.cpp`, `.h` | Keep the game-D3D11 binding and projection/flat submission paths. Replace dynamic loader selection with the host loader; add action, view, capture and lifecycle bridge seams. |
| `me2_menu.cpp`, `.h` | Initially compile as its renderer settings dependency. Adapt XInput/raw mouse/window ownership, focus and config paths before enabling. A full MEVR menu port is a later UI package. |
| `engine_probe.cpp`, `.h` | Compile only with unsafe runtime operations unreachable. Split validated game-thread observation/mutation from rendering primitives; raw addresses and actor traversal require P1. |
| `convo_fp.cpp`, `.h` | Preserve camera/context findings. Disable Present-driven DOF/script/actor writes until game-thread equivalents exist. Header descriptions of pure reads are insufficient. |
| `pchud.cpp`, `.h` | Preserve LE2 HUD/power-wheel mapping, but compose its handler into the host ProcessEvent owner rather than installing a second hook. Validate object lifetime and game-thread execution. |
| `logger.cpp`, `.h` | Retain in provenance import; adapt output to an explicit per-game/per-run destination or host logger. No shared LE1 request/log naming. |
| `dxgi_proxy.cpp`, `.h` | Retain original for provenance. Adapt a non-exporting ASI factory bootstrap using the existing LE1 pattern; do not build or install another proxy DLL. |
| `calcview_finder.cpp`, `.h`; `allocviewstate_finder.cpp`, `.h` | Import for provenance and compile dependencies; discovery is not executable-address validation. Do not invoke diagnostic discovery or transient trampoline probes in the production bootstrap. |
| `dllmain.cpp` | Import for provenance only. **Exclude from host compilation.** Our ASI entry owns startup; no upstream startup thread or resolution-file rewrite under loader lock. |
| `xr_types.h` | Preserve upstream's namespaced XR declarations initially, isolated to renderer translation units. Bridge through the existing narrow scalar/opaque-handle pattern. Do not mix these with official global XR types by include order or unchecked structure casts. |

Also preserve upstream `README.md`, `BUILD.md`, `docs/ME2_UI_STEREO_FIX.md` and
`runtime/MELE2VR.ini` as attributed reference material. The INI is not an approved
deployment default. Preserve the project file as build provenance if useful;
do not make its proxy target the host build. Do not import upstream `builds/`,
`release/`, loader DLLs or game-extracted layouts.

Follow [existing import policy](../src/halcyon/README.md): a verbatim provenance
commit first, then explicitly marked MEVR adaptations in separate commits.
Record the full upstream pin and imported scope in that README and `NOTICE`.
This report is original MIT documentation, not a source import.

## Dependencies and CanCompile boundary

Upstream `src/MELE2VR.vcxproj` specifies Release/x64, VS v143, C++17, Unicode,
Windows SDK, and `Shell32`, `Ole32`, `d3d11`, `dxguid` libraries. It compiles
four Dear ImGui core units plus `backends/imgui_impl_dx11.cpp`, and four MinHook
C units (`buffer.c`, `hook.c`, `trampoline.c`, `hde/hde64.c`). The upstream
`ME2VR_M0_PASSIVE_DXGI_PROBE` define is historical naming, not a guarantee that
the current code is passive. Runtime shader compilation tries
`d3dcompiler_47.dll`, then `_43.dll`; availability must be recorded separately
from link success. Upstream dynamically loads `openxr_loader.dll`.

The repository already supplies MinHook, Dear ImGui and a statically built
OpenXR loader. ImGui versions in both trees are `1.92.9 WIP` / `19282`.
SHA-256 comparison found identical bytes for ImGui's four core `.cpp` files,
`imgui.h`, DX11 backend `.cpp/.h`, and MinHook's `hook.c`, `trampoline.c` and
`MinHook.h`. This checked subset supports dependency reuse; it is not a hash
claim about either entire directory. Link **one** copy of each dependency.

| Component | Attribution and build treatment |
|---|---|
| Halcyon ME2 renderer | Upstream declares GPL-3.0, copyright 2026 Halcyon. Preserve attribution and modification history under `src/halcyon`; update exact LE2 scope in NOTICE when importing. |
| Dear ImGui | MIT, copyright 2014–2026 Omar Cornut; retain repository `third_party/imgui/LICENSE.txt`. Upstream BUILD text mentions a license file absent from its inspected ImGui file inventory; do not discard our existing notice. |
| MinHook/HDE | Preserve BSD notices in existing dependency, including Tsuda Kageyu and HDE's Vyacheslav Patkov attribution. Avoid duplicate bundled implementations. |
| OpenXR | Use repository OpenXR SDK/loader and its notices. No downloaded upstream loader binary. The renderer's `xr_types.h` remains separately attributed upstream code until deliberately replaced. |
| LExSDKv2 | Needed by the LE2 host adapter, not proof of correct layouts. Keep the existing submodule boundary; do not vendor generated proprietary SDK/game data into this package. |

The repository uses MSVC and CMake 3.27+, with C++20 host code and a C++17
`halcyon-le1` library. `MEVR_BUILD_LE2` defaults off. An isolated renderer compile
target may use C++17 matching upstream; the bridge-facing adapter follows host
requirements. A complete host also needs the checked-out OpenXR and LExSDK
submodules. Optional RmlUi/FreeType menu dependencies need not be added to the
first renderer-only compile package.

**CanCompile is currently unproven.** There is no `src/halcyon/le2` import or
`halcyon-le2` target at the audited base. `CMakeLists.txt` only appends
`MEVR_HALCYON_HOST_SOURCES`, links `halcyon-le1`, and defines
`MEVR_WITH_HALCYON` for LE1. `Halcyon.cpp` and `HalcyonBridge.cpp` include LE1
renderer headers and substantial LE1 UI/voice machinery. Broadening that
conditional would compile the wrong adapter. The named blockers are source
import, explicit LE2 source list, dependency deduplication, loader/entry seam,
LE2 host glue, and a link check of the complete object set. Compilation alone
cannot validate executable addresses, hook timing or native behavior.

## Startup and hook ownership

Upstream `dllmain.cpp` synchronously arms resolution spoofing before its worker
installs display hooks and loads DXGI. The game can read and clamp resolution
before an ASI attaches. Our LE1 `Halcyon.cpp` already acknowledges this later
timing. A successful late factory hook does not prove equivalent early device
or resolution interception. Use protected prelaunch settings or an explicitly
next-launch change where necessary; never silently rewrite active settings to
approximate the proxy's timing.

| Resource | Existing conflict | Required single owner |
|---|---|---|
| Module startup | Upstream DllMain/worker versus host `src/plugin/Entry.cpp` ASI entry | Host entry, after game/build identity checks. No executable upstream DllMain. |
| DXGI factories/Present | Proxy exports versus host fallback `PresentHook` and LE1-style ASI bootstrap | LE2 renderer owns its selected Present path. Adapt LE1 `InstallRealFactoryHooks` behavior; disable host fallback for this renderer selection even if XR initialization fails. |
| MinHook lifetime | Multiple upstream initializers and host hooks | Shared implementation and explicit hook ledger. Already-initialized can be tolerated; already-hooked at the same target is not successful composition. |
| UObject::ProcessEvent | Host permanent hook and `pchud.cpp::Tick` independent hook | Host callback composition, with verified game thread and supported function/object checks. |
| UGameEngine::Tick | Host tick and bridge queue | Host/P1 game-thread publication and requests; renderer reads immutable samples. |
| ExitProcess | `d3d_capture.cpp` installs a terminating hook; host owns teardown | Host. Disable upstream exit-hook installation. No joining workers or invoking engine logic during process teardown. |
| XInputGetState/GetStateEx | Upstream menu hook versus host motion-controller merger | One pad merge, then deliberate focus/menu/context policy. No second locomotion rotation or second synthetic button stream. |
| Raw input/WndProc/hotkeys | Upstream mouse interception/menu/recenter versus host UI/focus | One routed policy, with focus loss and modal consumption observable. |
| D3D11 context draw/clear | Body hooks needed for cached draw pointers; not interchangeable with vtable-only hooks | Renderer owns a documented set. Preserve state restoration, source pass identity and installation rollback. |

Display/device interception includes runtime-specific D3D11 device flag handling;
validate it against the selected runtime and actual device creation timing.
It must not select a different system OpenXR runtime as a side effect. Bink
decode hooks and movie signals need their own entries in the hook ledger.

`InstallBreakpointProbes` at this pin immediately logs that probes are disabled
and returns. Unreachable code below still installs a vectored handler and patches
breakpoints. Keep it unreachable; do not accidentally resurrect it when cleaning
up the file. Finder code also contains diagnostic executable trampolines. Neither
is the P1 fail-closed address-validation mechanism.

## XR and controller seam contract

`me2_xr.cpp::Tick` attempts initialization once after a D3D device exists. It
creates its own instance/session/spaces/swapchains, handles READY and STOPPING,
and submits frames from Present. It has no motion-controller action creation,
attachment, synchronization or haptic route. Merely reusing this file does not
give the MEVR mod controller poses. Its one-shot initialization latch and lack
of complete loss/recreation cleanup are also not the host lifecycle contract.
Destroy function pointers exist, but instance/session destruction is not a
complete implemented shutdown path. The current frame loop does not test the
`beginFrame` return before continuing.

Use the existing [LE1 bridge declaration](../src/halcyon/le1/mevr_bridge.h) as a
behavioral reference, not a wholesale LE1 header dependency. P0 owns the final
interface shape. The first LE2 bridge must meet these minimum semantics:

1. **Loader and ownership:** obtain `xrGetInstanceProcAddr` from the host's
   static loader. The renderer creates the only instance/session; do not also
   start `core/xr/Session`. Renderer selection is explicit and persists through
   init failure. Missing poses must not trigger a fallback second XR owner.
2. **Session-created boundary:** publish instance/session handles and monotonic
   generations before session begin; create and attach one
   [`core/xr/Input`](../src/core/xr/Input.hpp) action set. Never attach twice to a
   session. Haptics use that same target, focus and lifetime.
3. **Frame boundary:** after a successful begin, sync actions once using the
   frame's predicted display time and chosen app space. Record space/session
   generation and frame sequence. Preserve wait/begin/end balance on every
   error path and `shouldRender` branch; do not submit stale eye textures.
4. **Controller publication:** reuse `vr::controllers::Publish`/Latest snapshot
   transport with calibration, grip/aim correction and focus handling. Publish
   inactive state on loss rather than keeping a held trigger or old pose alive.
   P3/P4 consume coherent immutable samples; this does not establish aim/reload.
5. **View publication:** publish the actual view used for the submitted frame
   through [`HostView`](../src/core/vr/HostView.hpp). Include valid world-view,
   head-look/head-aim state, quaternion order xyzw, position in metres, camera
   basis, offsets in game units, one verified units-per-metre scale, and relevant
   floor/centre data. Pair it with the same controller/space generation, not the
   latest unrelated sample. Reject missing or stale transforms.
6. **Native pad route:** merge after the real XInput call and before final
   menu/context filtering. Record source, consumption and resulting native
   state. Retain native input until its replacement has explicit ownership;
   a bridge callback invocation alone is not evidence the game consumed it.
7. **Recenter/loss/stop:** invalidate cached hand anchors, view, menu pointers,
   latches and pending haptics on space/session changes. Destroy children while
   parents remain valid; handle STOPPING, LOSS_PENDING, EXITING and partial init
   failure. Shutdown requests are nonblocking signals, serviced by the owning
   frame thread. Permit deliberate fresh generations after cleanup.
8. **Layers and capture:** provide bounded layer capacity, current app space,
   prediction, projection/flat-screen metadata and frame-local resource validity
   to overlays/capture. Wrap session functions once for performance/capture
   telemetry, following the existing bridge's `WrapSessionFunctions` pattern.

`ModApi.hpp` currently uses non-null host controller services as an ownership
signal that suppresses the mod's own VR/output/input setup. Preserve that
behavior deliberately and publish availability separately; do not advertise
working controller capability with fabricated poses. A new shared ABI is not
needed merely to prove compilation, and is outside this discovery package.

## Rendering, UI and thread constraints

The upstream UI fix note describes draw-body interception because cached draw
function pointers bypassed vtable patches. Panels and glyphs take different
indexed/nonindexed paths; instanced draws also matter. The full-width LDR
R8G8B8A8 plus alpha-blend predicate separates UI from opaque composites. Keep
those LE2-specific findings rather than replacing the capture path with LE1's.
The historical note's success claims are not acceptance evidence for our port.

Current source also has SFR replay and render-thread first-eye capture beyond
that earlier note. `DrawDetour` requests two game Draw passes; GPU copies stay
in render-thread hooks using pass markers/sequence freshness. Per-eye view state,
matrix updates, captured-eye identity and no-op replay counts all matter. Two
Draw calls do not prove two fresh rendered views. The LE2 enum is Mono=0,
Stereo=1, AER=2, DIBR=3, SFR=4; its menu's Stereo selection uses SFR. Do not carry
the LE1 `Stereo2` numeric value of 2 into this adapter.

Source head translation uses 50 game units per metre times `headPosScale`, with
an additional **3x forward emphasis**. The default half-eye separation is a
separate tuning value. These are not a demonstrated metric camera contract.
Reconcile eye spacing, head translation, controller world mapping and the actual
projection together. A comfort gain must be explicit; do not label the existing
forward emphasis 1:1 tracking. Reuse pure metric math only after establishing the
LE2 basis and camera reference.

Context classification currently distinguishes gameplay, conversation, cinematic,
GUI/pause, movies, galaxy and orbital views using engine mode plus other signals.
It clears the cinematic latch on gameplay return and uses Bink/scene-view
heartbeat evidence for movie versus in-engine presentation. Preserve these
transitions as test cases, not assumed verified engine enum values. The renderer
must not leave the next gameplay scene mono, suppress interrupt input, or turn
planet scanning into an ordinary weapon-action context.

There is a concrete safety split to make before running the adapted renderer:

- Present calls `EngineProbe::ApplyFirstPerson` and `ConvoFp::Tick`.
  `convo_fp.h` says pure read, but implementation writes DOF settings and calls
  `EngineProbe::ConvoFpDisableDof`, which invokes ProcessEvent. Other first-person
  logic changes actor visibility/camera state. Move validated gameplay writes
  and script calls to the game thread, with generation-checked identities.
- XR view update calls `EngineProbe::DriveHeadAim`; actor/control-rotation writes
  compete with MEVR hand aim and native aim updates. Disable upstream aim writing
  until the P1/P3 adapter owns its explicit game-thread route.
- `PcHud::Tick` installs an independent ProcessEvent hook when adjustments are
  active. Its actual HUD writes run within that callback, a useful pattern, but
  hooking the same target again conflicts with the host. Compose and validate
  the callback rather than retaining its independent installer.
- Direct scene-view modifications belong only at the engine's verified view
  hook boundary. Do not generalize that permission into arbitrary UObject reads
  or ProcessEvent calls from Present, XInput, workers or capture callbacks.
- Immediate-context copies, draw-state changes and XR frame/layer submission
  stay with their owning rendering threads. No game-thread wait for a GPU
  capture, and no background thread retaining engine pointers.

For evidence, the existing TestLoop/Perf bridge is currently attached to LE1.
See [runtime-observer audit](le2-runtime-observer.md): archive inspection is not
an LE2 runtime exporter, and old file names do not establish game identity.
Future captures must bind process start, executable and loaded-module hashes,
run ID, game, session/space/frame generations, source eye, prediction and
monotonic timestamp. Use per-run/per-game request paths, never shared LE1
request files. Native baseline observation must work without requiring a VR
session; that is a separate P9 change. Weapon reload evidence must also retain
phase/early-transfer information from the
[ammo source report](le2-ammo-fixture-source.md).

## First compile-only integration package

This is the next bounded package, **not work already performed here**:

1. Import the 24 upstream `.cpp/.h` files and reference documents at the exact
   pin, with license/NOTICE/README records. Preserve the pristine import before
   modifications; no release binaries, extracted game data or unrelated mods.
2. Add an explicit, isolated `halcyon-le2` source target. Initially compile the
   **10 renderer C++ units excluding `dllmain.cpp` and `dxgi_proxy.cpp`** for
   syntax coverage. Keep both excluded files in the provenance tree. Add the
   adapted ASI factory bootstrap as the eleventh linked unit when ready. Share
   one MinHook/ImGui implementation and the host OpenXR loader.
3. Implement a minimal LE2-specific bootstrap/bridge, not the entire LE1 host
   feature bundle. Resolve factory/export and entry ownership, remove dynamic
   loader selection, and supply explicit XR/input/view seams. Keep unsafe
   gameplay mutation paths disabled until P1 supplies validated routes.
4. Introduce fail-closed address/capability gates and the hook ledger. No
   diagnostic finder, hidden worker startup, upstream ExitProcess hook, duplicate
   ProcessEvent hook or automatic settings write may be reachable from the
   future attach path. Do not infer this from successful compilation.
5. Perform a complete object link check in an isolated build/output directory.
   A static archive alone can conceal unresolved references; use a nonexecuted
   link harness or equivalent whole-object link. Explicitly exclude upstream
   DllMain and exports. The harness must never be launched or copied into a game.
6. Save a build manifest with `importedSourcePin`, repository commit, source and
   binary hashes, compiler/toolset/SDK, dependency revisions, source list,
   compile definitions, link inputs, disabled capabilities and diagnostics.
   Review hook ownership, settings paths and thread gates before proposing a
   separately authorized native candidate.

Acceptance for this package: attributable source, reproducible isolated compile
and complete link, coherent declared seams, and reviewed startup exclusions.
Tests of pure transform/lifecycle logic may accompany implementation. This gate
does not claim that ASI timing captures the game device, that hooks install, or
that the game can render stereo. Do not build a main hot-reload target or write
to a deployed binary path while ME1 is in use.

## Native verification after explicit handoff

Use the protected LE2 session workflow and recheck shared ownership, profiles,
installation and candidate hashes immediately before the trial. The following
remain open, even if the compile package succeeds:

| Trial | Required observation |
|---|---|
| Normal native launch/load | Correct LE2 exe/version/PID/start time, intended prepared save and scene, original profile/config backup, loaded candidate hash. Preserve the normal launcher path. |
| ASI bootstrap | Actual factory/device/swapchain capture timing; supported display/device configuration; exactly one owner of each hooked target; unsupported build fails closed. |
| XR lifecycle | One instance/session/action stream; correct runtime/device; focus, tracking loss, recenter, partial-init failure, session stop/recreation and process exit without hangs or stale actions. |
| Stereo and scale | Fresh left/right source eyes with matching frame and pose metadata; verified IPD, head/controller scale, axes, FOV and metre-to-world conversion. Screenshot plus headset judgment, not counters alone. |
| Input/aim boundary | Real native and VR-controller paths distinguished; one pad merger; buttons released on focus/context loss; no head writer fighting hand/native aim. Weapon aiming remains P3 proof. |
| UI/context transitions | Main/pause/power wheel, subtitle/glyph/panel placement, conversation interrupts, cinematic/movie return, galaxy/orbital scanning and gameplay return; no doubled HUD or stuck mono screen. |
| Capture/performance | Correct game/run/module provenance; eye/frame freshness; no-op replay and dropped-frame visibility; bounded frame-time comparison under matched settings; native baseline independent of VR. |
| Restoration | Clean shutdown, original absent/present files and hashes restored, run-created saves preserved separately, no remaining request files or ownership ambiguity. |

Each gate needs its own evidence. Source findings, offline tests, successful
compilation, a normal native load and headset acceptance remain distinct claims.

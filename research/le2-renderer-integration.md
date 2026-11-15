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

## Imported and linked into the LE2 host (2026-11-14)

Done by the LE1 conversations session (docs/le2-from-le1.md section 3.1, LE1's recipe of 2026-10-08). **Built, and run
once on the fake headset** (below: stereo, our controllers on its session, the queued ProcessEvents, clean quits; first
person needs upstream's `MELE2VR.ini`): `build-trilogy`'s `LE2MEVR.asi` links it; `build-trilogy`, `build` and `build-mono` build with no
warnings and the 53 offline tests pass.

1. **Provenance.** `a21fc14e` imports upstream `4784e90649a0398c6812b89fc09f4a0dbcce6441` verbatim into
   `src/halcyon/le2`: the 12 units, 12 headers and `MELE2VR.vcxproj` of `src/`, `docs/ME2_UI_STEREO_FIX.md`,
   `runtime/MELE2VR.ini`, and `README.md`/`BUILD.md` as `UPSTREAM_*.md`. Its `third_party/` MinHook and Dear ImGui are
   byte-identical to ours (`diff -r`: only our `imgui/LICENSE.txt` extra) and were not imported again; `builds/` and
   `release/` were left out. `c1a6d183` adds only the GPL-3.0 SPDX lines and the rows in `src/halcyon/README.md` and
   `NOTICE.md`.
2. **Compile.** `halcyon-le2` (CMake; defined only in a build with `MEVR_BUILD_LE2` and `MEVR_HALCYON`, not the diagnostic
   host, so LE1-only builds don't compile it) builds 11 units unchanged at upstream's settings (C++17, Unicode, its
   `ME2VR_M0_PASSIVE_DXGI_PROBE`, `third_party/minhook/src/hde` for the finders' disassembler) plus `MEVR_ASI`:
   `dllmain.cpp` out, `dxgi_proxy.cpp` in with its exports compiled out. No warnings at `/W3`.
3. **Host.** `2b6900d3`: with `MEVR_HALCYON` on and not `MEVR_LE2_DIAGNOSTIC`, the LE2 host links it
   (`MEVR_WITH_HALCYON_LE2`). `plugin/HalcyonLe2::Attach` runs at the host's attach, first, what upstream's DllMain and
   startup thread did: `ArmResolutionSpoof` (its `[VR] RenderW/H` and `DisableDof` writes to GamerSettings.ini, so as for
   LE1 they may apply only from the next launch), `InstallDisplayQueryHooks`, `Dxgi::EnsureLoaded`, and a new `[MEVR]`
   `Dxgi::InstallRealFactoryHooks` (LE1 upstream's function, rewritten for ME2's names: MinHook on the system dxgi.dll's
   `CreateDXGIFactory`/`1`/`2`, each factory to `D3DCapture::TryInstallFactoryHooks` as the proxy exports did). Not
   `InstallBreakpointProbes` nor the finders' probes. The host's own Present hook is not installed for this renderer.
   `HostServices::controllers` is set on LE2 now, so the mod stands its own VR output, scene view and XInput hooks down.
4. **Seams** (`src/halcyon/le2/mevr_bridge.h`, implemented in `plugin/HalcyonLe2Bridge.cpp` and `plugin/HalcyonLe2.cpp`):
   `me2_xr.cpp` takes `xrGetInstanceProcAddr` from our static loader (no `openxr_loader.dll`); `OnSessionCreated` after its
   spaces and swapchains (our `core/xr/Input` action set on its session); `OnFrameBegun` after a successful `xrBeginFrame`
   (sync in `g_localSpace`, `vr::controllers::Publish`); `me2_menu.cpp`'s two XInput hooks call `MergeMotionControllers`
   right after the real read (`game/GamepadMapping`'s default mapping, Touch to Xbox pad 0); `d3d_capture.cpp`'s
   `SvrIsSteamVrActiveRuntime` asks `XrRuntimeIsSteamVr` (XR_RUNTIME_JSON first: the fake headset's SteamVR while the
   machine's active runtime is another's); `engine_probe.cpp`'s four ProcessEvent calls (`SetOwnerNoSee`, `Actor.SetHidden`,
   `PrimitiveComponent.SetHidden`, `DisableDOF`, all reached from Present) go through `MevrCallProcessEvent`: off the game
   thread each is queued with its parameter block for the next engine tick (`halcyon_le2::Tick`, at the start of the
   host's tick hook), run only if the object and the function are still in GObjects at their index and the object's
   class is unchanged. LE2's GObjects (`0x173CC48`, the address Halcyon's own code and LExSDKv2 use) is now a data row of
   `engine/Addresses.hpp`'s LE2 table. `[Debug] openxrRuntime` in `ASI\LE2MEVR.ini` sets XR_RUNTIME_JSON at attach.
5. **Left as upstream for the first session** (each a later `[MEVR]` change, after a native look): its own ProcessEvent hook
   in `pchud.cpp` (on top of ours, as LE1's Halcyon hooks it); its `[EXITKILL]` ExitProcess hook (our hook terminates too);
   `DriveHeadAim` (head aim: the owner's rule is no head aim, but LE2 has no hand aim yet); its 50 units per metre with
   the 3x forward emphasis (LE1's metric stereo, research/30, is the later fix); its two full renders a present.

### The first run on the fake headset (2026-11-14)

The owner's VR Shepard career, Save 4 at Freedom's Progress (`BioP_ProFre`), three launches, about 20 minutes; session
folder `%LOCALAPPDATA%\MEVR\sessions\20261114-154422-le2-halcyon-first` (logs, the mirror's picture, the backups).

**ME2's own runner is `tools/qa-le2`** (docs/le2-scripted-testing.md, the ME2 validation session's work): `session.py`
backs up and restores the protected roots, `launch.ps1` forwards `-game 2` through Steam and binds the process,
`control.py` loads a save through the game's own Load screen on the game thread (`advance-splash`, `open-load-menu`, one
guarded `LoadGame` on a staged fixture), observes resources and weapons, and submits normal saves, and
`prepare-variants.py` / `save-evidence.py` make and verify edited saves (Trilogy Save Editor as the oracle). **Use it for
the next Halcyon run.** It could not drive this one: its responder (`engine/Le2DiagnosticHost`, `Le2ControlHost`, the
bounded pad) is compiled only into the isolated, monolithic `MEVR_LE2_DIAGNOSTIC` build, and Halcyon's renderer only into
the other LE2 host (`CMakeLists.txt`: `MEVR_HALCYON AND NOT MEVR_LE2_DIAGNOSTIC`). So this run did by hand what
`session.py` does, and loaded the save with computer use, the handoff's fallback when no channel exists. **The next step
for scripted VR runs** is one LE2 host with both, agreed with the validation session: its pad output and Halcyon's XInput
merge (`MergeMotionControllers`) both touch XInput, and the diagnostic build is monolithic while the renderer's
development host hot-reloads.

**How this run was done** (by hand; for the record, not the route to repeat):
1. Check that no game, launcher or SteamVR runs and that `tools/game-slot.ps1 status` says free.
2. Back up `Documents\BioWare\Mass Effect Legendary Edition` (the saves), `Game\ME2\BioGame\Config` and
   `Game\ME2\Binaries\Win64` with robocopy, plus a hash manifest of each (`Get-FileHash` per file).
3. ME2 has the stock Bink DLL: rename `Game\ME2\Binaries\Win64\bink2w64.dll` to `bink2w64_original.dll` and copy ME1's
   LEBinkProxy (`Game\ME1\Binaries\Win64\bink2w64.dll`, ProductName "LEBinkProxy (RELEASE)") in its place. Our own
   loader starts only ME1 from the launcher.
4. `tools/deploy-asi.ps1 -Game LE2 -BuildDir build-trilogy` (the hot-reload host and `.modpath`), then write
   `[Debug] openxrRuntime = C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json` into
   `ASI\LE2MEVR.ini`.
5. `tools/fake-headset.ps1 -On`; 16 s after vrserver starts, `steam.exe -applaunch 1328670 -game 2 -autoterminate`
   (the launcher's LEBinkProxy starts ME2 at once, no launcher menu).
6. With computer use (grant `MassEffect2.exe`; `open_application "Masseffect2"` brings it forward): Enter on the title,
   click Load Game then Enter, click "Save 4 - Freedom's Progress" then Enter. The first screenshot after a focus change
   can show the desktop: take another.
7. Quit: Esc, Exit Game, Yes. Then copy the logs out, `tools/fake-headset.ps1 -Off`, and put the three roots back from
   the manifests (changed files copied back, added files moved aside). All three matched, hash for hash.

**What worked** (host log `Win64\Logs\LE2MEVR.log`, Halcyon's `%LOCALAPPDATA%\MELE2VR\MELE2VR_Log.txt`):
- `halcyon le2: renderer attached`, `[DXGIHOOK] real dxgi CreateDXGIFactory/1/2 hook installed [MEVR]`, `[SVRFIX2]
  this process's OpenXR runtime is SteamVR [MEVR]: yes`, `xrCreateInstance: 0`, `VR session ready` 17 s after start.
- `our controller actions are attached to Halcyon's session`, `first controller sync (session not focused)`.
- Menus on its flat panel at 11.2 ms a frame (90 Hz); in gameplay `[VRSTATE] gm 0 -> VR`, SFR stereo (pass 0 the left
  eye, the second eye a draw replay) at 12.4-14.3 ms a frame, the game thread at 78-81 fps.
- Every ProcessEvent from Halcyon's Present thread went through our queue: `game thread calls: 26 queued from other
  threads, 26 run, 0 skipped`; no `process event: off the game thread` line.
- First person (after K, below): `[ME2FP] state=Combat (weapon out) mode=SFXCameraMode_Combat eye=(-60,5,25)`, the
  head hidden, the heavy pistol in view on the mirror.
- Two quits through the Mission Computer: the process gone in 8 s, the launcher with it, no crash report.

**What it found:**
1. **First person is off without upstream's ini.** Halcyon reads `[FirstPerson] Enabled` from `MELE2VR.ini` (next to
   `MassEffect2.exe`) with a default of 0 (`me2_menu.cpp`), and upstream ships `runtime/MELE2VR.ini` with `Enabled=1`
   and `FpToggleKey=75` (K). Our deploy writes no such file, so the mirror showed the over-the-shoulder camera until K
   was pressed. The deploy installs upstream's file since (its `runtime/` and `release/` copies are identical), and the
   baseline pins first person ("Defaults from ME1" below, with the fault the file first brought).
2. **The eyes are the monitor's shape.** `Rendering 5120x1440 (2560x1440 per eye). Headset suggests 1912x1952`: the game
   renders at the desktop's 32:9 size, so each eye is 16:9 against a square-ish headset. Upstream's `ArmResolutionSpoof`
   takes `[VR] RenderW/RenderH` from the same missing ini; with them and a borderless window
   (`tools/vr-game-settings.ps1 -Game LE2` since then) it rendered 2048x2304 an eye.
3. **The second launch crashed in our mod, not in Halcyon** (fixed the same day in `engine/Legacy.cpp` and
   `engine/ReflectCheck.cpp`): LExSDKv2 finds the exe's base with `CreateToolhelp32Snapshot`, which fails now and then
   while another DLL loads (EA's overlay `IGO64.dll` was injecting on that launch), and its checks compile out in
   release, so every `BUILTIN_*_RVA` resolved to the bare offset and the first `SFXName` called `0x78640` (LE2's SFXName
   init RVA) from `TryInstallScriptHooks`. Our code now gives the initializer the exe's base from `exe::Base()` (log
   `LExSDKv2's module snapshot gave the exe's base as ...` when the snapshot was wrong).
4. **The mod sees no view from this host**: `host state sizes differ from the mod's (controllers 200 vs 200, view 0 vs
   136): ... controllers and the view ignored`. Expected: LE2's host publishes controllers but no view service yet, so
   the mod's hand aim and the rest stay off on LE2 (Wave B).
5. Halcyon writes `DepthOfField=False` into `GamerSettings.ini` and `BIOEngine.ini` at attach (`[DOFOFF]`); the backup
   put both back.
6. Harmless first-chance access violations: `PcHud::ReadFString` (its HUD discovery, inside `__try`) and the F3 dump's
   reads.

**Diagnostics:** `[VR] Diagnostics=1` in `MELE2VR.ini` (read at launch) lets the `[ME2FP]`, `[ME2DISC]` and other
diagnostic tags through its logger (`logger.cpp kDiagnosticTags`); F3 held about 0.3 s dumps the controller's and the
camera's object graph (`cam+0x580` is `SFXCameraMode_Combat` in combat). The LE1 test loop's snapshots still need a
`WrapSessionFunctions` seam in `me2_xr.cpp`, so the desktop mirror is the only picture.

### Defaults from ME1 (2026-11-14)

The owner asked to reuse what ME1 established rather than re-decide it for ME2. Three layers hold settings:

| Layer | ME1 | ME2 |
|---|---|---|
| The mod's own (`ASI\<GAME>MEVR.ini`) | code defaults, written on first start | **the same code**: the first LE2 start wrote all of them (most of their features are LE1-only for now) |
| The engine's (`game/SystemSettings`: shadows, motion blur, foliage after each load) | yes, at the world change | **already runs on LE2**, since this work 3 s into gameplay instead (the fault below) |
| The game's files (`tools/vr-game-settings.ps1`) | VSync, blur, grain, HDR, filtering, borderless, shadows, foliage | the first five already; **borderless, shadows and foliage added for LE2 now** |
| Halcyon's (`MELEVR.ini` / `MELE2VR.ini`) | pinned in code: `MevrBridge::ModernBaseline` | different program and keys: **`ApplyMevrBaseline` in `me2_menu.cpp`** (`[MEVR]`), run at the end of `LoadValues` |

ME1's baseline mapped to ME2's keys (ME2's mode numbers differ: 0 Mono, 2 AER, 3 DIBR, 4 Stereo):

| ME1 (`MELEVR.ini`) | ME2 (`MELE2VR.ini`) | in ME2's baseline |
|---|---|---|
| Stereo 2 (`vrMode = 2`) | `Mode=4` | pinned |
| `questFovMatch`, `vrFovFillEnabled` | `QuestFovMatch`, `VrFovFill` | pinned |
| `leanEnabled`, `lookSensitivity = 1` | `PosEnabled`, `LookSensitivity` | pinned |
| `menuFlat` | `MonoMenus` | pinned |
| `fpHideHead` | `[FirstPerson] S*_hideHead` | pinned |
| `fpEnabled` | `[FirstPerson] Enabled` (code default 0, upstream's comment says "defaults ON") | pinned |
| no profile hotkeys | `ProfileHotkeys` (keys 1-4: ME2's power keys) | off |
| keys only with the game in front (`MevrBridge::KeyDown`) | its ten hotkey polls | `MevrBridge::HotkeyDown` |
| recenter on R (ME1 has no reload key) | `RecenterKey=82`: R is ME2's reload | off R (`RecenterKeyEnabled` is saved but never read upstream, so the key is cleared); double-tap Back, the menu's Recenter view and opening the menu still recenter |
| life-size stereo (research/30) | none (`HalfEyeUU`, `Convergence`, `SwapEyes`) | a port, later |
| no head aim; conversations on the 3D screen; `decoupledPitch` | `HeadAim=1`, `CineVrConvo=1`, `CineVrCutscene=1`, `DecoupledPitch=0` | pinned to ME1's (the owner, 2026-11-14): head aim off (VRIK fast-tracked; the stick or mouse aims until then), conversations and cutscenes on Halcyon's flat cine panel until the 3D screen is ported, `DecoupledPitch` on |

**Checked on the fake headset** (runs `20261114-171033-le2-baseline` and `-171437-le2-baseline-borderless`): `[MEVR-BASELINE]
2 setting(s) in MELE2VR.ini set to the MEVR baseline: profile hotkeys (ProfileHotkeys), the recenter key R (RecenterKey)`;
R in gameplay logged no `Recentred view`. The eye size: with upstream's `RenderW/RenderH` the game rendered `4096x1460`
in a fullscreen window on the 1440p monitor and `4096x2304 (2048x2304 per eye)` borderless.

#### The first-gameplay-frame fault, found and fixed

**Symptom:** with upstream's `MELE2VR.ini` installed, the level's first gameplay frame after a load (15-25 ms after
`[VRSTATE] gm 0 -> VR`) faulted on the rendering thread: `first-chance access violation ... at MassEffect2.exe+0x98C25D
(reading 0x1C)`, a thread that isn't the game thread, its stack all `MassEffect2.exe` (`0x3D262C`, `0x3AA564`,
`0x34B296` ... from the thread entry `0x130F34`; the D3D11 code: `OMSetRenderTargets` is called from `+0x98B8C4`). The
thread dies behind the engine's handler, the game thread waits for good (no heartbeat, no CPU, the window still
"responding"), so the game hangs instead of closing.

**Cause:** `game/SystemSettings` (the mod) sends `scale set DynamicShadows / MotionBlur / FoliageDrawRadiusMultiplier /
LightEnvironmentShadows` when a new level becomes the world, which on a save's load is during the load, about a second
before the first gameplay frame; with the game's depth of field on, any such `scale set` there (even the value the
engine already had, directly or through its opposite) faults that frame. Upstream's file has `DisableDof=0` (depth of
field on); without the file Halcyon's code default `DisableDof=1` wrote `DepthOfField=False` at attach (`[DOFOFF]`),
which is why the first runs never faulted. **Fix:** on LE2 a new level's settings wait until it has been in gameplay for
3 s (`kSettleApplyAt[0]`, `#if defined(MEVR_GAME_LE2)` in `Poll`); the same settings there, opposite-first as on LE1, are
harmless (runs I, J). LE1 keeps applying at the world change (its motion blur fix needs it).

The A/B, one variable a run, the owner's Freedom's Progress Save 4 each time (session `20261114-175705-le2-firstframe`,
its `logs` keep every report):

| Run | Upstream's ini, except | Mod's settings at the load | Depth of field | Result |
|---|---|---|---|---|
| first runs | no ini at all | sent | off (`DisableDof` default 1) | fine |
| 3 runs | none / no body or weapon hides / 4096x1460 window | sent | on | fault |
| A | first person off | sent | on | fault |
| B | `RenderW/H` the desktop's 5120x1440 | sent | on | fault |
| C | B, and `[HUD] PerElement=0` | sent | on | fault |
| D | C, and `HudScaleX/Y` 1.0 | sent | on | fault |
| E | D, and `DisableDof=1` | sent | **off** | **fine** |
| F | D | **`[SystemSettings] apply = 0`** | on | **fine** |
| G | D | sent directly, not through the opposite | on | fault |
| H | D | `LightEnvironmentShadows = True`: no value changes | on | fault |
| I | D | **held until 3 s into gameplay** | on | **fine** (applied at 3 s and 15 s) |
| J | none (as upstream ships it, first person on) | held | on | **fine**: first person on from the load, 2048x2304 eyes |

**Also noted:** `LightEnvironmentShadows False` is LE1's rule for its replayed second eye (`[Performance] sharePass`); on
LE2 the key is missing, read as LE1's default 5, so ME2 gets it too. Halcyon's ME2 replays its second eye as well
(`replays=600`), so it may be right there, but nobody decided it: [verify] the characters' shadows in both eyes.

## M2: the host's services on ME2 (lane B, 2026-11-14)

The work list is [the continuation plan](../docs/le2-continuation-plan.md) M2. What each step changed, so the next one (or
a lane that builds on it) can follow without the conversation that did it.

### Step 1: the test loop on ME2

**What:** `plugin/TestLoop` (eye snapshots, pose recording, replay; CLAUDE.md "Test loop") runs in the LE2 host as in LE1's.
The same request files and tools (`tools/snapshot.ps1`, `tools/record.ps1`, `tools/replay.ps1`) work on ME2.

**How, file by file:**
1. `src/halcyon/le2/me2_xr.cpp`, `BringUp` right after `ResolveFunctions()`: a `[MEVR]` call
   `ME2VR::MevrBridge::WrapSessionFunctions(&g_fn.acquireSwapchainImage, &g_fn.waitSwapchainImage,
   &g_fn.releaseSwapchainImage, &g_fn.waitFrame, &g_fn.beginFrame, &g_fn.endFrame, &g_fn.locateViews)` (as `void**`). Every
   upstream call goes through `g_fn`, so the wrappers see each image release, each frame's layers and each located view.
   Declared in `src/halcyon/le2/mevr_bridge.h`.
2. `plugin/TestLoop.hpp`: a public `testloop::WrapSessionFunctions(acquire, release, endFrame, locateViews)` holds the
   wrapping that used to be the body of LE1's `MELEVR::MevrBridge::WrapSessionFunctions` in `TestLoop.cpp`. LE1's seam
   moved to `plugin/HalcyonBridge.cpp` (it calls the test loop's, then `perf::WrapFrameFunctions` as before, and logs the same
   line); ME2's is in `plugin/HalcyonLe2Bridge.cpp` (the test loop's only; log `test loop: wrapped Halcyon's ME2 ...`).
3. `plugin/HalcyonLe2Bridge.cpp`: `OnSessionCreated` calls `testloop::OnSessionCreated`; `OnFrameBegun` syncs the controllers
   (if their actions exist), then `testloop::OnFrameBegun(session, space, time, focused, state)` (records the live sample or
   puts a replay's in its place), then publishes, as LE1's bridge does. A replay now drives the controllers even when our
   actions failed.
4. `plugin/HalcyonLe2.cpp`: `halcyon_le2::AfterTick(deltaSeconds)` (after the engine tick, `plugin/Entry.cpp`) calls
   `testloop::Tick` (the End and Home keys, the request files once a second, `testloop.status`).
5. `plugin/TestLoop.cpp` builds into both hosts. What the LE2 host doesn't link yet is compiled only with
   `MEVR_WITH_HALCYON` (LE1's Halcyon host): the spectator, our swapchains' budget (`swapchains::AfterEndFrame`), each module's
   block of the snapshot JSON (`ModuleStatus()`: `null` on LE2), and `perf.request` / `eyediff.request` (taken and ignored on
   LE2 with a warning). Halcyon's own menu (`"halcyonMenuOpen"`) is asked through the renderer interface,
   `renderer::OwnMenuOpen()` (`plugin/Renderer.hpp`; LE1 `MELEVR::Menu::IsOpen`, LE2 `ME2VR::Menu::IsOpen`).
6. `CMakeLists.txt`: the LE2 host's sources gain `plugin/Renderer.hpp`, `plugin/RendererLe2.cpp`, `plugin/TestLoop.*` and what
   it needs (`core/vr/AimRay`, `HostView`, `ImageConvert`, `LayerComposite`, `PoseRecording`, `TestRequest`, `core/Hotkey`,
   `core/PngFile`), and links `windowscodecs` (WIC: the PNGs). LE1's Halcyon sources gain `plugin/Renderer.hpp`,
   `plugin/RendererLe1.cpp`.

**Checked on the fake headset** (session `%LOCALAPPDATA%\MEVR\sessions\20261114-191506-lane-b-testloop`, logs kept there):
the host logged `test loop: wrapped Halcyon's ME2 xrAcquireSwapchainImage, ...` right before `VR session ready`, and
`tools/snapshot.ps1` at the title wrote `<stamp>.json` and `<stamp>-quad0.png`: Halcyon's flat menu quad (4096x1152, 2.2 m
wide, 1.6 m ahead), the title picture in it. At the title there is no projection layer and ME2 locates no views in its app
space, so the JSON's `head` is invalid and no `-left`/`-right` composites are written (as on LE1's flat panel). The game
could not be taken into gameplay that run (the desktop was in use: computer use was interrupted), so both eyes in
gameplay are still to be seen: load Save 4 from the Load menu, then `tools/snapshot.ps1`.

**Using it on ME2:** as on LE1, with the stretch from `tools/le2-halcyon-run` (its build is the hot-reload one, so the request
files are read). `tools/snapshot.ps1 -Tag x`, `tools/record.ps1 -Seconds N`, `tools/replay.ps1 -Path f -Loop [-Inputs]`.
Quads ME2's renderer submits are named `quadN` (LE1's are named by `testloop::NameSwapchain` in our own modules; Halcyon's
menu quad is "menu" through `NoteMenuQuad`, which ME2 has no seam for yet).

### Step 2: the renderer interface

**The rule:** a host module asks the renderer only through `src/plugin/Renderer.hpp` (namespace `mevr::host::renderer`,
plus `mevr::host::SettingsPath()`), never through a renderer's own headers. `plugin/RendererLe1.cpp` (in
`MEVR_HALCYON_HOST_SOURCES`) forwards each function to what the modules called before; `plugin/RendererLe2.cpp` (the LE2
host's sources) answers from ME2's renderer or, where it has nothing yet, as LE1 does with that feature off. The other
direction (the renderer calling the host: `OnFrameBegun`, `WrapSessionFunctions`, the HUD panel's draw seams) stays in each
renderer's `mevr_bridge.h`.

| Function | LE1 (`RendererLe1.cpp`) | LE2 (`RendererLe2.cpp`) | Used by |
|---|---|---|---|
| `mevr::host::SettingsPath()` | `engine::SettingsFile()`, cached (LE1's `MevrBridge::SettingsPath` forwards here now) | the same | every module with settings |
| `OwnMenuOpen()` | `MELEVR::Menu::IsOpen` | `ME2VR::Menu::IsOpen` | TestLoop, Calibration, FloorLock, MainMenu |
| `SetOwnMenuOpen(bool)` | `MELEVR::Menu::SetOpen` | no-op (no setter in `me2_menu.cpp`: M4) | MainMenu |
| `LogLine(text)` | `MELEVR::Logger::LogLine` | `ME2VR::Log::Line` | Colour, HalcyonOverlays, MetricStereo, RecenterTilt |
| `GameMode()` | `MELEVR::HeadAim::ReadGameModeSEH` | `ME2VR::EngineProbe::ReadGameModeSEH` (ME2's numbers: [verify] each past 0) | GamePointer |
| `FirstPersonOwned()` | `Config::Get().fpEnabled && HeadAim::FirstPersonOwnsAny()` | false (M3) | FloorLock |
| `HeadArmSequence()` | `RenderHook::GetHeadArmSeq` | 0 (no arm counter) | FloorLock |
| `GameWindow()` | `MevrBridge::GameWindow` (its `SetGameWindow` seam) | the captured swap chain's `OutputWindow` | GamePointer |
| `Hud()` (`HudSettings`) | `Config::Get()`'s `hudPanel*`, `handReticle*` | ME1's defaults (the struct's member values) | HalcyonOverlays |
| `Panels()` (`PanelSettings`) | `worldLockedPanels`, `panelFollowDeg` | ME1's defaults | WorldPanels |
| `RecenterTiltDeg()` | `recenterTiltDeg` | 60 (ME1's default) | RecenterTilt |
| `SetScreenReticleHidden`, `ScreenReticleHidden` | `MELEVR::Pchud::SetCrosshairHidden`, `CrosshairHidden` | no-op, false (M3) | HalcyonOverlays, ModLoader (`hideScreenReticle`) |
| `SetAuthoredHudLayout(bool)` | `MELEVR::Pchud::SetAuthoredLayout` | no-op (step 4) | HalcyonOverlays |

`mevr::window::KeyDownInGame(vk)` (`core/Window`, game-independent) is what both bridges' `KeyDown` and plugin/Voice call.

**Moved onto it** (their `halcyon/le1/*.h` includes gone but `mevr_bridge.h`, whose plain types and seam declarations they
share): Calibration, CineScreen, Colour, FloorLock, GalaxyHologram, GamePointer, HalcyonOverlays, MainMenu, MetricStereo,
ModLoader, MotionBlurGuard, OmniPad, OmniToolRail, PowerWheel, QuestPacing, RecenterTilt, SquadPlaceDraw, Swapchains,
TestLoop, Upscale, Voice, VoiceModel, WorldPanels; PerfProbe and RenderLevers for the settings path only.

**Still LE1's on purpose** (Halcyon's LE1 renderer itself, not modules that use it): `HalcyonBridge.cpp` (LE1's seams),
`Halcyon.cpp` (its startup), `MevrMenuTab.cpp` and `MevrMenuPointer.cpp` (our tab in Halcyon's LE1 ImGui menu),
`MainMenuSettings.cpp` (edits Halcyon's LE1 `VrConfig` row by row: lane D maps those rows to ME2's keys in M4),
`PerfProbe.cpp` (`g_perfDraws`, `XrSession::GetResolutionStats`), `RenderLevers.cpp` and `DrawReplay.cpp` (LE1's render
thread), `MetricStereo.cpp`'s `StereoPlan` (LE1's stereo plan).

**What a module still takes from LE1's adapter files** (host functions, not the renderer's; they move to game-independent
files when the module first compiles into the LE2 host): `MevrBridge::MainMenuOpen` (HalcyonBridge.cpp; FloorLock,
GamePointer), `MevrBridge::TakeArmSample` (MevrMenuTab.cpp; Calibration), `MevrBridge::StartCameraAlign` (MevrMenuTab.cpp;
MainMenu), and `mevr::host::halcyon::*` (HalcyonBridge.cpp: haptics, the floor, the support grip, hand aim).

**Checked: LE1 is unchanged.** Two fake-headset runs of the owner's current LE1 career (it continues on Noveria, Anoleis's
office), each `tools/fake-headset-session.ps1 -Run <script>` with the same script: 20 s in the world, the replay
`make-walk-recording.py --plan stand:600 --hands view` looped, 8 s, the console's `pause`, two snapshots 1 s apart. Before:
the build of step 1 (sessions `2026-11-14_19-39-41-laneb-pose-before`); after: step 2's (`2026-11-14_19-47-47-laneb-pose-after`).
The same six layers (both eyes, `hud`, `label`, `wrist`, `omnitarget`) at the same places (the wrist quad 1 mm apart), the
JSON's `hostView` identical (offset -43.3/-4.1/-18.1 uu, 100 uu/m), the HUD panel's picture identical pixel for pixel,
and the eyes 0.5% of pixels apart by more than 32/255, all of them on things that move in a paused world between two
launches (Shepard's idle hands and the gun, Anoleis, the console's flicker, the window's light); two snapshots of
the same run differ by 0.26% over 8/255. The comparison is a script over the two snapshots' PNGs and JSON (PIL
`ImageChops.difference`).

### Step 3 (first part) and step 5 (first part): the view, our session on ME2's, one services builder

**What landed:**
1. **`plugin/XrHost`** (new, both hosts): our controller actions on the renderer's session (`core/xr/Input`), their
   vibration (`core/vr/Haptics`) and the STAGE floor (`core/xr/Floor`), moved unchanged out of `plugin/HalcyonBridge.cpp`.
   API: `xr_host::OnSessionCreated(instance, session)`, `OnFrameBegun(session, base, time, state&)` (sync, vibrations, the
   floor), `InputReady`, `InputFocused`, `InteractionProfile(hand)`, `NoteRecentered`, and for any thread `RequestHaptic`,
   `RequestContinuousHaptic`, `ForgetHaptics`, `Floor(out)`. LE1's bridge calls it in place of its own copy (its
   `mevr::host::halcyon::RequestHaptic`, `Floor`, `NoteRecentered` forward there); ME2's bridge calls the same, so ME2's copy
   of the input code is gone. Log lines unchanged (`halcyon bridge: first controller vibration`, `floor: first sample`).
2. **ME2's view seam:** `src/halcyon/le2/me2_xr.cpp`, `[MEVR] MevrPublishView` at every return of `DriveHeadLook` (a scope
   guard; DriveHeadLook runs each frame of the stereo, AER and SFR paths, not the flat menu panel), calling
   `ME2VR::MevrBridge::OnViewBuilt(worldView, headLook, headAim, lookYawUU, lookPitchUU, headQ, headP, offsetRightUU,
   offsetUpUU, offsetForwardUU, unitsPerMeter, floorWorld)`: the head DriveHeadLook drove (ME2 drives `SetHeadPos` with
   `views[0]`, the LEFT eye's position: upstream's, kept), the smoothed roll-free orientation, CalcViewHook's head look, and
   `ApplyHeadPosition`'s offset by its own formula (50 uu/m x `HeadPosScale`, the forward 3x, each axis clamped to 2 m, none
   while a VR cine locks tracking). `plugin/HalcyonLe2Bridge.cpp` turns it into `vr::host_view::State` and publishes it (no
   floor lock on LE2 yet: `floorWorld` unused). Yaw sign as LE1's: Halcyon's + is a left turn, ours + right, so it's negated.
3. **Recenter and stop seams:** `RecenterAppSpace` calls `MevrBridge::OnRecentered()` after a new app space (the floor's
   recenter count), `PumpEvents` calls `OnSessionStopped()` after `xrEndSession` (the published view reset).
4. **One builder** (`plugin/ModLoader.cpp`): `BuildServices()` = `BaseServices()` (version, log, the ProcessEvent callback,
   exit, keep loaded) + `WithCrashBreadcrumbs` + `WithRenderer` (either Halcyon host: controllers, view, setHandAim, both
   sizes, the aim ray, the snapshots' status, the three vibrations, the floor) + LE1's module list (`WithLe1Modules`: the
   pad mapping's grips, the squad menu and wheel panel, sights, calibration, our main menu, the vignette; then
   `WithGameMenuRequest` ... `WithVoice`) where `MEVR_WITH_HALCYON`. The LE1 values are field for field the old aggregate's.
   `renderer::SetModHandAim` / `ModHandAim` joined the interface (LE1: `halcyon::SetModHandAim`, `ControllerAimActive`; LE2:
   a flag, logged `halcyon le2: the mod aims weapons with the right controller`).

**Checked on the fake headset** (session `%LOCALAPPDATA%\MEVR\sessions\20261114-195511-lane-b-view`, its logs and snapshots
kept there): the mod's start logs no `no view to aim with` and no `host state sizes differ` (both were there before);
`halcyon le2 bridge: first world view: ... 100.0 units per meter`; in gameplay (the main menu's Resume: the career's most
recent save) the snapshot JSON has
`head.valid` and `hostView.valid` true; with a replayed head turned 30 degrees right
(`make-walk-recording.py --plan stand:600 --hands view --yaw 30`) `hostView.lookYawUU` is +5461 (+30 degrees, UE's right)
and the offset forward +4.8, right -2.77 uu: exactly the left eye's lean at 100 uu/m with the 3x forward. The STAGE floor on
the fake headset reads 0.00 m (its floor is at the head, as on LE1). The game was brought from the title into gameplay with
two posted Enters (`PostMessageW(WM_KEYDOWN, VK_RETURN)` to the game window: the title, then the main menu's Resume); the
owner's rule since is that runs are driven only by script (launch.ps1, the request files, replays), so the next gameplay
check waits for lane A's `tools/le2-halcyon-run/load.ps1`.

**LE1 after the move** (the same pose run as step 2's, session `2026-11-14_20-07-12-laneb-pose-xrhost`, against step 2's
"before"): the same six layers at the same places, `hostView` identical, the HUD panel identical, the eyes 0.2% of pixels
apart by more than 32/255 (what animates), and the floor still sampled (`floorLock.floorSequence` advancing,
`finalEyeM` 1.5989 both times).

**Found: in gameplay the eye images are the main menu's backdrop.** Every gameplay snapshot (19:57:57, 19:58:06, 20:00:52,
30 s to 3.5 min after `[VRSTATE] gm 0 -> VR`) shows, in both eyes, ME2's animated main-menu picture (the Normandy
wireframe, its counter changing between snapshots), while the desktop window showed gameplay (the HUD's squad icons, a
"Door Open" prompt). The projection layer's views are `g_aerSwap[eye]` (4096x2304, the whole backbuffer), filled by
`CopyTextureToEyeFullFrame` from `ME2VR::D3DCapture::GetGameSwapChain()->GetBuffer(0)` (the right eye) and the SFR pass-0
capture (the left). `GetGameSwapChain()` is the FIRST swap chain `CaptureSwapChainInfoOnce` (d3d_capture.cpp) saw present,
kept for the process. **[verify]** whether the game presents gameplay through another swap chain than the one captured
(the borderless window of `vr-game-settings.ps1 -Game LE2` may make it make a second one), which would freeze or misroute
the eyes in a real headset too: run with `prep.ps1 -Diagnostics` (its `[ME2DISC] game swapchain <ptr>` line) and log each
Present's swap chain against it; compare with a run without the borderless window. **Run again with lane A's scripted load**
(session `20261114-201931-lane-b-eyes`, `prep.ps1 -Diagnostics`, `launch.ps1`, `load.ps1`: Freedom's Progress, game mode 0,
`[VRSTATE] gm 0 -> VR`, `frame mode = SFR`, `[SFR] Draw replay #3000 ok=1 faults=0`): the eye snapshot is **black** in both
eyes (lane A saw the same), the JSON's `hostView` valid. So the AER eye images hold whatever was last copied before
gameplay (the menu's backdrop when the main menu came first, nothing when the load came straight from the title), and in
SFR gameplay either nothing reaches them or the test loop reads another image than the runtime gets. The Diagnostics log
has `[ME2DISC] hooked IDXGISwapChain::Present` and `first Present seen` (backbuffer 4096x2304, R8G8B8A8_UNORM; the eye
swapchains are format 29, R8G8B8A8_UNORM_SRGB) but no `game swapchain` line: `CaptureSwapChainInfoOnce` logs it untagged by
Diagnostics, or never ran. Next: read the compositor's own picture (SteamVR's VR View) once, to tell a renderer bug from a
test-loop one; then the `GetGameSwapChain`/`CopyTextureToEyeFullFrame` path (`idx < g_aerImages[eye].size()`, the copy's
format pair) in me2_xr.cpp and d3d_capture.cpp. The test loop copies the image the
renderer releases (the same texture the runtime composites), so the snapshot is what the headset gets unless that copy is
wrong, which LE1's identical path argues against. Until this is settled, an ME2 eye snapshot doesn't show the world.
(Settled below, "The black eyes": the "main menu's backdrop" is ME2's loading screen, and the eyes were stale because
the copy into them was dropped.)

### The black eyes (lane B, second agent, 2026-11-14)

**Symptom** (every wording it had): after `load.ps1` both ME2 eye snapshots are black; after the main menu's Resume both
eyes show "the main menu's animated backdrop" (it is ME2's loading screen, the mass relay wireframe with its counter); the
desktop mirror shows gameplay; SFR runs (`[SFR] Draw replay #3000 ok=1`).

**Cause: the game's swap chain is R16G16B16A16_FLOAT in gameplay, and D3D11 drops a CopyResource from it into the 8-bit
eye images without a word.** Halcyon's `CopyTextureToEyeFullFrame` (me2_xr.cpp) and the flat panel's copy both use
`CopyResource`, valid only within one typeless format family. The eye images kept whatever reached them while the swap
chain was still 8-bit (black from the session's start, or the loading screen's last frame). Why float: **Windows HDR is on
for this machine's monitor** (DisplayConfig's advanced colour: supported and enabled, 10 bits per channel), and **ME2 makes a
float swap chain in gameplay on an HDR display even with `[HDR] DesiredRange = DynamicRange_SDR`** (GamerSettings.ini;
`vr-game-settings.ps1 -Game LE2` writes it). At the title the swap chain is R8G8B8A8_UNORM (28), the same as LE1's on this
machine (`[HDRBLIT] backbuffer format 0 -> 28`), which keeps 8 bits with that setting. Upstream's ME2 renderer assumes an
8-bit swap chain throughout: besides the copies, its UI classifier (`IsUiDrawNow`: the bound target must be format 28) and
its SFR left-eye capture (the backbuffer snapshot at pass 1's opening clear: in the float frame every scene clear comes
before the one backbuffer draw, `[CLEARMAP] clears=5 bbDraws=1 ... e2060@0 e2060@0 ...`, `[SFRDIAG] totalCaps=0`) find
nothing, so in a float frame both eyes are the same picture and the HUD gets no treatment.

**How it was found** (each step one run of `prep.ps1`, `launch.ps1`, `load.ps1`, `tools/snapshot.ps1`; the stretch
`%LOCALAPPDATA%\MEVR\sessions\20261114-203401-lane-b2-eyes`):
1. The snapshot JSON's projection layers had images and no errors, and the PNGs were RGB 0 with alpha 255: the test loop
   read real images that were black, not a failed copy of its own.
2. A seam saving what the renderer copies **from** (`MevrBridge::OnEyeSource`, below): `<stamp>-source-left/right.png`
   showed Freedom's Progress while the eyes showed the loading screen. So the game's picture was there and the copy wasn't.
3. A seam saving the eye image right **after** the copy, by the renderer's own pointer (`OnEyeCopied`): `-copied-*.png` was
   as stale as the eye: the test loop reads the right image; the copy does nothing. Acquire and wait succeed (`eye copies in
   60 s: left 784 (indices 0x7; acquire failed 0 ...)`).
4. Its log line gave the formats: `eye copy from 4096x2304 format 10 ... bind 0xa8 into 4096x2304 format 27 ... bind 0x28
   misc 0x2` (format 10 R16G16B16A16_FLOAT; 27 R8G8B8A8_TYPELESS, what SteamVR backs a format-29 swapchain with).
5. With the game told the display is SDR (`[Colour] hdrDisplay = -1`, below): `the game's swap chain is 4096x2304 format 28
   (2 buffers, swap effect 4)` in gameplay, the left source the pass-0 capture (`bind 0x20`) and different from the right
   (mean difference 15/255: a real stereo pair), and Halcyon's UI treatment shrinks the HUD into the middle of the eye as
   upstream does. Answered with the display's own HDR10 (`hdrDisplay = 0`), the swap chain is format 10 in gameplay.

**Fixes** (all `[MEVR]`-marked in src/halcyon/le2, the rest ours):
1. **`plugin/EyeCopy`** (`eye_copy::Copy`, over `core/d3d11/Blit`): `MevrBridge::CopyIntoImage(ctx, image, source, shiftX)` is
   called in place of the `CopyResource` in `CopyTextureToEyeFullFrame` and the flat panel's copy (`RunFrame`, the
   `flatPresentation` branch). Where the two formats are one family it returns false and Halcyon's own copy runs, unchanged;
   otherwise it draws the source through an R8G8B8A8_UNORM_SRGB view of the image (the float source is linear scRGB; values
   over 1.0 clip), with Halcyon's convergence shift as a second draw moved `shiftX` pixels (`Blit::Draw`'s new `shiftX`,
   default 0). Log `eye copy: the game's picture (4096x2304, format 10) is drawn into the eye images ...` once. Checked: the
   float run's eyes show Freedom's Progress (mean RGB 80/86/96, the source's). Still one picture in both eyes there.
2. **The display's HDR answer**: `MevrBridge::OnFactory` (end of d3d_capture.cpp's `TryInstallFactoryHooks`) patches the
   first output's `IDXGIOutput6::GetDesc1` (vtable slot 27; the game's HDR question, research/27 7c), answered by
   `[Colour] hdrDisplay` in `ASI\LE2MEVR.ini` as LE1's key: 1 HDR10 for an SDR display, 0 the display's own answer (the
   default, ME1's), **-1 SDR for an HDR display** (new; on LE2 only). Log `hdr display: the display reports colour space 12
   (HDR10), 8 bits, 400 nits; answered SDR ([Colour] hdrDisplay = -1)`. **-1 is ME2's default, as an interim stop gap**
   (the owner, 2026-11-14: "If it's cheap to do sdr, then we can do that as a stop gap while you figure out HDR"): when
   ME2's renderer handles its native HDR frame (the HDR study, `research/le2-hdr.md`), the default goes back to 0,
   ME1's. `[Colour] hdrDisplay = 0` in `ASI\LE2MEVR.ini` passes the display's answer through now (the float frame,
   drawn by EyeCopy: one picture in both eyes).
   With -1 the frame is the game's own 8-bit SDR output, as ME1's: the swap chain format 28, copied raw into the sRGB eye
   (Halcyon's own copy, no blit), like LE1's raw copy with the neutral colour profile; darker than the float frame drawn
   by EyeCopy (mean 51/55/62 against 80/86/96 at the same spot), which is the SDR tonemap against the float frame's
   linear values. LE1's colour profiles and FSR are not on LE2 yet.

**Diagnostics kept** (cheap; for the next "the eyes look wrong"):
- `tools/snapshot.ps1` on ME2 also writes `<stamp>-source-left/right.png` (what the renderer copied into each eye) and
  `<stamp>-copied-left/right.png` (the eye image right after that copy, by the renderer's pointer): `testloop::OnEyeSource`,
  `OnEyeCopied`, fed by `MevrBridge::OnEyeSource` / `OnEyeCopied` in `CopyTextureToEyeFullFrame`. Source right but copied
  wrong: the copy; copied right but the eye wrong: the test loop's image lookup; source wrong: the game's picture or the
  swap chain.
- Host log: `halcyon le2 bridge: eye copy from ... into ...` (each new pair of formats, at most 8), `eye copies in 60 s:
  ...` (counts, image indices, refused acquires and waits), `an eye copy was refused (...)` once, `the game's swap chain is
  WxH format F (...)` at each change (checked every 90th Present), and `a Present on swap chain X that isn't the one the
  renderer copies its eyes from` (never seen: one swap chain).
- Note for log readers: `[ME2DISC]` lines written before Halcyon's menu reads `MELE2VR.ini` are dropped even with
  `Diagnostics=1` (the flag is set by that read), which is why `CaptureSwapChainInfoOnce`'s `game swapchain` line never
  shows.

### Step 3 (the rest begun): our layers at xrEndFrame on ME2

**What:** ME2's renderer now submits our quads after its own world and panels (and under its Insert menu), as LE1's
`OverlayLayers` does, and the LE2 host has the two host modules that need nothing of LE1's renderer: the swapchain budget
(`plugin/Swapchains`) and the comfort vignette (`plugin/Vignette`). This is the place every later host module on LE2 adds
its quad (the HUD panel, the reticle, the sights, our main menu, the calibration).

**How, file by file:**
1. `src/halcyon/le2/mevr_bridge.h`: `struct OverlayFrame` (LE1's field names, the ones LE2 can fill: session, `appSpace`
   = `g_localSpace`, `viewSpace`, the game's context, `worldFrame`, `menuOpen`, `gameMode`, the world layer's views as
   submitted) and `OverlayLayers(frame, layersOut, capacity)`.
2. `src/halcyon/le2/me2_xr.cpp`, `RunFrame` right before `xrEndFrame`: a `[MEVR]` block fills the frame and appends up to
   12 of our quads after the projection layer, the flat panel and the game-UI quad, before the Insert menu's quad (so the
   menu stays on top); the layer array grew from 4 to 16.
3. `src/plugin/HalcyonLe2Bridge.cpp`: `OverlayLayers` asks each LE2 host module for its quad: so far `vignette::Layer`
   (the same call LE1's HalcyonOverlays makes). `OnSessionCreated` tells `swapchains::OnSessionCreated`.
4. `src/plugin/TestLoop.cpp`: `swapchains::AfterEndFrame` (which of ours were shown; room for refused creates) and the
   snapshot's `"swapchains"` block run in both Halcyon hosts.
5. `src/plugin/ModLoader.cpp`: `WithVignette` (HostServices v17's `setVignette`) and `vignette::Forget` moved out of the
   LE1-only lists into the both-hosts part (`BuildServices`).
6. `CMakeLists.txt`: the LE2 host gains `plugin/Swapchains`, `core/vr/SwapchainBudget`, `plugin/Vignette`, `core/vr/Comfort`.

**Checked on the fake headset** (stretch `%LOCALAPPDATA%\MEVR\sessions\20261114-212412-lane-b2-layers`, Freedom's Progress Save 4 by
`load.ps1`, no `[Colour]` key in the ini): `hdr display: ... answered SDR ([Colour] hdrDisplay = -1)` (the default), `the
game's swap chain is 4096x2304 format 28`, the snapshot's `"swapchains"` block now live on LE2 (`ours 0, halcyons 3`),
the eye sources a stereo pair (mean difference 15/255), the eyes Freedom's Progress. No quad of ours yet (nothing asks
for the vignette on LE2).

**LE1 after steps 1 and 3 (this section): unchanged.** The same pose run as step 2's (`le1-pose.ps1`: 20 s in the world,
`make-walk-recording.py --plan stand:600 --hands view` looped, 8 s, the console's `pause`, two snapshots 1 s apart;
`tools/fake-headset-session.ps1 -Tag laneb2-pose`, build-mono, session `2026-11-14_21-26-07-laneb2-pose`) against the first
agent's 20:09:55 snapshot: the same six layers at the same places (the omni-tool target 0.4 mm apart), `hostView`
identical, the HUD panel identical pixel for pixel, the eyes 0.86% of pixels apart by more than 32/255, all on what
animates between launches (the edges of the idle hands and the gun, the console's flicker, the window's stars; the two
snapshots of one run differ by 0.3% over 8/255). The comparison script: PIL `ImageChops.difference` over the two
snapshots' PNGs and the JSON's layers and `hostView`.

**Not yet:** the floor lock (`plugin/FloorLock`: LE1's camera and first-person state through Halcyon's LE1 seams; ME2's
`OnViewBuilt` passes `floorWorld` but nothing uses it). The mod's comfort module (`game/Comfort`) is LE1 game code, so on
LE2 nobody asks for the vignette yet: the service is there for lane C/D's comfort work.

### Step 4: the HUD panel and the hand reticle on ME2 (lane B, third agent)

**What:** ME2's UI goes onto our body-locked HUD panel quad, ME1's way, instead of into each eye; the hand reticle (a ring
on the mod's aim ray) is drawn on ME2 too. Both are the same code as LE1's, moved into game-independent modules.

**How, file by file:**
1. `src/plugin/QuadSwapchain.{hpp,cpp}` (new, both hosts): `mevr::host::QuadSwapchain` (images, a render target view per
   image, the refused state) and `quad_swapchain::Create`, `Ensure` (also names it for snapshots), `Acquire`, `Release`, moved
   unchanged out of HalcyonOverlays (they go through `plugin/Swapchains`).
2. `src/plugin/HudPanel.{hpp,cpp}` (new, both hosts), namespace `mevr::host::hud_panel`: the panel texture and the UI draws'
   states (render thread: `Armed`, `Target`, `Blend`, `TintBlends`, `TintTarget`, `NoWrites`, `NoteDraw`, `NoteTint`,
   `IsFloat`), the blit pipeline and its shader (`Blit` of a window of the panel into any `QuadSwapchain`, with masks;
   `DrawCoverageGrid` for the wheel's measurement), the panel's own swapchain (`Swapchain`, `Factor`, `Render`), its
   placement (`Place`, `Placement`, `Recentered`, `RequestReplace`) and the end-of-frame clear (`EndFrame`, `NoteArmChange`,
   `TakeClears`). LE1's colour correction is handed in (`SetColour`: plugin/Colour's prelude, `BeginPanel`, `EndPanel`), so
   the module doesn't depend on plugin/Colour. Two entry points are for a renderer without LE1's own draw seam:
   `Redirect(context, draw, user, textBatch, afterLeftEye)` (classifies one UI draw: "over" or a Scaleform blend mode go into
   the panel, anything else is left alone) and `Layer(Frame)` (the plain body-locked panel for one frame: placement, render,
   clear, arm the next frame's redirect; exactly LE1's path for a frame without wheels, labels, HUD pieces, world screens or
   the cinema screen).
3. `src/plugin/HandReticle.{hpp,cpp}` (new, both hosts): `hand_reticle::Layer(Frame)`, LE1's reticle block moved unchanged
   (the mod's `aim_ray`, else the right controller's aim pose; on foot, or in the Mako on the mod's ray).
4. `src/plugin/HalcyonOverlays.cpp` (LE1): its HUD panel and reticle code replaced by calls to the three modules. Every LE1
   log line keeps its text. The `MevrBridge::HudPanel*` seams forward to `hud_panel`.
5. **ME2's seam:** `src/halcyon/le2/d3d_capture.cpp`, `[MEVR] MevrHudPanelDraw` called in `MhDrawIndexed`,
   `MhDrawIndexedInstanced` and `MhDraw` right before their `uiMode == 4` branch (SFR), inside Halcyon's own test (an
   `IsUiDrawNow` draw, VR on, not a menu or cinematic frame): `MevrBridge::HudPanelDraw(ctx, draw thunk, user, textBatch,
   SfrUiMirrorArmed())` -> `hud_panel::Redirect`. True: the draw went to the panel at the game's own viewport (the HUD's
   authored layout) and Halcyon's shrunk draw into the eyes is skipped. Declared in `src/halcyon/le2/mevr_bridge.h`.
6. `me2_xr.cpp`'s `[MEVR]` OverlayLayers block also fills `OverlayFrame::stereo` (`GetVrMode() == 4`) and the head
   (`headValid`, `headOrientation`, `headPosition`: the views located once more through `g_fn.locateViews` at the frame's
   display time in `g_localSpace`, so a replay's head is used in a replay; the left eye's orientation, the eyes' mean
   position, as LE1's frame).
7. `src/plugin/HalcyonLe2Bridge.cpp`: `OverlayLayers` calls `hud_panel::Layer` (after the vignette; gameplay = game modes
   0-4, `worldFrame && !menuOpen`, the settings `renderer::Hud()`: ME1's defaults on LE2, 1.30 m wide, 1.5 m ahead, 0.25 m
   down) and `hand_reticle::Layer` (handAim `renderer::ModHandAim()`, the last readable game mode); `OnRecentered` ->
   `hud_panel::Recentered`; `OnSessionStopped` disarms it.
8. **The HUD's own layout on the panel:** `renderer::SetAuthoredHudLayout(on)` on LE2 now calls
   `ME2VR::PcHud::SetAuthoredLayout(on)` (`[MEVR]` in pchud.cpp/.h): while on, PcHud's per-element moves and scales for a
   head-locked HUD (MELE2VR.ini `[HUD]`: upstream's ini moves the weapon block 232 px and scales it 1.62, the squad block
   1.75) stand down, as LE1's `Pchud::SetAuthoredLayout`. The bridge sets it each frame to "the panel is enabled, SFR, and
   usable". Elements moved before it came on would stay moved until the HUD movie is rebuilt; it comes on with the session,
   before gameplay's first HUD.
9. `CMakeLists.txt`: both hosts gain the three modules; the LE2 host also `core/vr/OverlayPlacement`.

**Found: ME2's SFR draws the UI twice a frame.** Each of the two `FViewportClient::Draw` calls (pass 0, the game's; pass 1,
Halcyon's replay) draws the whole HUD: 75 UI draws before the renderer's left-eye capture (`SfrUiMirrorArmed()` false) and 75
after it, all into the backbuffer on the immediate context (a trace of one present's UI draws, logged as `halcyon le2
bridge: this present's UI draws in order ...: E75 L75`). The first batch ends up in the right eye only (the backbuffer at
Present), never in the left (the pass-0 capture): with only the later batch on the panel, the right eye still had the HUD,
shrunk, and the left none. Upstream therefore draws the HUD twice into its right eye and once (the mirror) into its left.
So both batches go to the panel, and the first draw after the capture clears the panel first: the later batch replaces the
earlier one, and a frame whose capture never came (an anchor miss) still has its HUD. **Why the first batch doesn't reach
the left-eye capture is not understood** ([verify]: Halcyon's `[CLEARMAP]` puts the capture after 76 backbuffer draws, which
should include it); the panel doesn't depend on it.

**Checked on the fake headset** (stretches `%LOCALAPPDATA%\MEVR\sessions\20261114-2152..2206-lane-b3-hud*`, Freedom's
Progress by `load.ps1`, `[Colour] hdrDisplay` -1 by default): `[MEVR-HUD] HUD panel texture 4096x2304 fmt=28`, `swapchains:
HUD panel 1365x768 made`, `panel layer: 150 UI draws this frame, 1365x768 (1/3 of 4096x2304), 1.30 x 0.73 m at (0.00, -0.25,
-1.50) m`, every UI draw "over" blended (`UI draw to the HUD panel: blend=1 src=5 dst=6`, none left to the renderer); the
snapshot has a `hud` layer with the whole HUD at its authored layout (power bar top left, prompt, crosshair, weapon panel,
squad portraits) and neither eye shows the HUD (the HUD's orange in each eye: 639 pixels in the right eye's power-bar band
before, about 20 in the whole picture after, the lamps' own). PcHud logs `[MEVR] PcHud: the HUD is on the body-locked panel:
its own layout`.

**The colour correction on the panel** (after the picture lane put `plugin/Colour` in the LE2 host, fc24c20d):
`HalcyonLe2Bridge::OverlayLayers` hands `hud_panel::SetColour` the same prelude and `BeginPanel`/`EndPanel` LE1's
HalcyonOverlays does, before the panel's first blit, so the game's UI gets the eyes' correction (neutral: the plain shader).

**LE1 after step 4: unchanged.** The same pose run as the earlier steps' (the scratchpad's `le1-pose.ps1`: 20 s in the
world, `make-walk-recording.py --plan stand:600 --hands view` looped, 8 s, the console's `pause`, two snapshots 1 s apart;
`tools/fake-headset-session.ps1 -BuildDir build-mono`), before and after on one base (main 34d41fff): "before" built from
this worktree with `HalcyonOverlays.cpp` and `CMakeLists.txt` checked out from 34d41fff (session
`2026-11-14_23-03-25-laneb3-before`, snapshot `23-06-05-087`), "after" with this step (session
`2026-11-14_22-35-47-laneb3-pose`, snapshot `22-38-33-671`; both sessions keep their snapshots: the snapshots folder is
pruned). The same six layers (both eyes, `hud`, `label`, `wrist`, `omnitarget`) at the same places (the label 0.1 mm, the
omni-tool target 0.4 mm apart), `hostView` identical, the HUD panel identical pixel for pixel, the eyes 0.47% / 0.44% of
pixels apart by more than 32/255 (what animates between launches). The earlier steps' baseline snapshot (21:28) was gone,
hence the fresh "before".

**The first-chance access violations at XR start** (every LE2 run before, three lines `first-chance access violation
0xC0000005 ... at LE2MEVR.asi+0x...` reading addresses like 0x1000002B7 or 0xFFFFFFFFFFFFFFFF; the offset moved with each
build: +0xE45FB, +0xE930B, +0xE987B, +0xF2F1B, +0x10CF1B): `python tools/resolve-native-symbols.py <the session's
added\ME2Win64\ASI\LE2MEVR.asi> 0xE987B` (each `tools/le2-halcyon-run` session keeps the ASI and its PDB) named
`ME2VR::PcHud::ReadFString` (pchud.cpp): Halcyon's `CollectHandlers` reads the HUD handler's FString path offsets on every
Scaleform handler class, so most reads take FNames and flags for string pointers and fault inside its own `__try`. Fixed
with a `[MEVR]` check before the read (f4c5fe68): no pointer under 64 KB, above user space or not 8-byte aligned (an engine
allocation is), no count past the FString's capacity. No first-chance line since.

**Diagnostics kept:** `[MEVR-HUD] UI draw <where>: blend=... src= dst= ...` once per blend kind; the panel's line every 30 s
counts the draws before the left-eye capture and the draws left to the renderer; `halcyon le2 bridge: this present's UI
draws in order` (e/l before/after the capture, upper case into the backbuffer, `D` a deferred context's) for the first three
presents with UI and every 600th, twelve lines.

**Not yet on ME2:** the game's crosshair is still drawn in the panel's middle (LE1 hides it while the hand reticle stands
in for it: `renderer::SetScreenReticleHidden`, a no-op on LE2 until PcHud or the HUD movie gives a switch for it); the
wheels, interaction labels, HUD pieces, world screens and the cinema screen's UI layer stay LE1's (HalcyonOverlays); ME2's
UI during menus and conversations stays on Halcyon's flat panel (the panel is armed only for head-tracked world frames).

### What is left of M2 and the host side of M3, with the seams (for the next agent)

In the order the coordinator set (2026-11-14 22:00: lane D took the main menu, calibration and the omni-tool; lane C's mod
side of VRIK is on main and publishes `setAimRay` every tick):
1. **The game's crosshair off on ME2** (finishes the reticle): `renderer::SetScreenReticleHidden` is a no-op in
   `plugin/RendererLe2.cpp`. ME2's crosshair is in the HUD movie (the panel shows it in its middle); find its path (PcHud's
   `RunDiscovery`, F11 with `[VR] Diagnostics`, lists every path the HUD handler exposes, and its sweep tries `Crosshair`,
   `mcCrosshair`, `Reticle`) and hide it on the game thread as LE1's `Pchud::SetCrosshairHidden` does (a `[MEVR]` setter in
   pchud.cpp; the write through `WriteVar` in its ProcessEvent hook, never from the Present thread).
2. **Sights** (`setSight` + `sightSize`, HostServices v13/v39): `plugin/SightLayers` draws the dot, the holo and the scope's
   picture; it needs the eye poses and the world layer as submitted (`OverlayFrame::projection*`, already filled on LE2,
   plus `eyeOrientation`/`eyePosition`: add them in me2_xr.cpp's block from the views located there), and LE1's
   `MevrBridge::NoteDrawArm`/`pairArm` for the scope capture (none on LE2: leave the capture off). Move it into the LE2
   host's sources, call `sight_layers::Layers` from `HalcyonLe2Bridge::OverlayLayers` before the reticle (the ring hides while
   `sight.looking && sight.replaceRing`, `HandReticle::Frame::sightHidesRing`), and move `WithSights` (ModLoader.cpp) out of
   the LE1-only list. Lane C ports `game/Sights` once the service is there.
3. **The pad merge with the mod's flags** (support grip v7, `rightGripFree` v9, `gripHolds` v10, grips v11;
   `setSupportGrip`/`supportGripHeld`): LE1's `MergeMotionControllers` (plugin/HalcyonBridge.cpp) is the plain mapping plus
   `PadOptions()` (the mod's flags from `gSupportFlags`, set by the services `WithLe1Modules` adds), the close latch, and the
   trigger owners (main menu, calibration, omni-tool, game pointer, galaxy). Move the mapping core (`PadOptions`, the latches,
   the support-grip report, `WalkTowardHand`) into a game-independent `plugin/PadMerge` that both bridges call, with the
   LE1-only owners passed in as a callback; then move the grip services out of the LE1-only list.
4. **The floor lock** (`setFloorLockSample`, v36): `plugin/FloorLock` needs (a) a render seam on ME2 where the view's origin
   is moved, `calcview_hook.cpp ApplyHeadPosition` (LE1's is render_hook.cpp `ApplyViewOffsetSEH`: `FloorViewTranslation(
   replay, -pvt[2], VM[8], VM[9], VM[10], &r, &u, &f)` returns the vertical correction added to the world offset), with
   ME2's offsets in UE units (`w2m * (...)`, 50 uu/m x `HeadPosScale`, forward x3) and `t_replay` for the second pass;
   (b) `renderer::FirstPersonOwned()` on LE2 (ME2's first person owning the camera: `ME2VR::EngineProbe` first-person state,
   `[FirstPerson] Enabled`), else `vr::floor_lock::Applies` is never true; (c) FloorLock.cpp off LE1's headers (it asks
   `MELEVR::MevrBridge::MainMenuOpen`: put that behind the renderer interface); (d) `OnViewBuilt` on LE2 calling
   `floor_lock::Queue(view, floor, floorWorld)` instead of publishing the view itself (LE1's way); (e) the mod's sample
   (lane C: `game/BodyScale`'s floor-lock sample on LE2).
5. **HUD pieces** (v22, `setHudPieces`; lane D's omni-tool tap needs `hud_pieces::Pieces`' arm bones): the wrist and gun
   quads are cut from the HUD panel (HalcyonOverlays' masks, `ShowPiece`); with `hud_panel::Blit` and its `Masks` shared
   now, they can move into a module beside `hud_panel::Layer` the same way. ME2's HUD has its own crops (the squad block at
   the bottom middle, the weapon block bottom left: measure them on the panel's snapshot, `-hud.png`).
6. **`plugin/GameThreadCalls` for LE2** in place of `HalcyonLe2.cpp`'s own queue: GameThreadCalls checks a queued call's
   object through `engine/GameAdapter` and `ue3` (LE1's layout so far); with M1's LE2 layout it can serve both.

**What the mod on LE2 still says** (each line a module the LE2 host doesn't serve yet, all LE1 host modules that come over
with steps 4-5): holsters (v9: `rightGripFree`, keeping the squeeze from the game; v10 `gripHolds`), throwables, vent, squad
orders and squad (v11: the grips and the squad menu), calibration (v15), comfort (v17: the vignette and turn flags), main
menu requests (v19), interact (v21), hud pieces (v22), voice (v31), support grip and sights (v39 messages: `setSupportGrip`,
`setSight`). They come from the pad mapping with the mod's flags (`HalcyonBridge::MergeMotionControllers`: LE2's bridge has
the plain mapping), `plugin/Calibration`, `plugin/Vignette`, `plugin/HalcyonOverlays` (HUD panel, labels, HUD pieces, the
squad panel, reticle), `plugin/SightLayers`, `plugin/MainMenu` and `plugin/Voice`. Two of them repeat in every status block
(`squad: the host is older than v11`, `comfort: ... older than v17`).

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

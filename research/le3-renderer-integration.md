<!-- SPDX-License-Identifier: MIT -->
# ME3 renderer foundation

## Source and ownership

The L3-2 foundation imports Halcyon's GPL-3.0 MassEffect3-VR at `cfd40019450d3469b44a9b155f03e0263a61f787`, in a separate verbatim commit. `src/halcyon/le3/README.md` records provenance. Its ME2-named files are upstream's ME3 source; no ME2 native offsets were substituted.

The host ASI calls `halcyon_le3::Attach`, hooks the actual system DXGI factories and excludes upstream `dllmain.cpp`. `MEVR_WITH_HALCYON_LE3` keeps the legacy host Present hook off. Halcyon alone creates the OpenXR instance/session and submits frames; the bridge uses that same statically linked loader and attaches the shared controller actions before `xrBeginSession`. Session stop clears published controller/view state; recenter invalidates the previous controller space before new poses are published.

`HalcyonLe3Bridge` composes existing modules: `XrHost`, `Controllers`, `HostView`, `FloorLock`, `PadMerge`, `Swapchains`, `Colour`, `EyeCopy` and `TestLoop`. The thin `RendererLe3` facade provides real renderer settings/window/mode/first-person answers. Unimplemented crosshair/hud-layout methods explicitly do nothing. Service composition must not publish them as available. Main menu, omni-tool rail, calibration UI, weapon layers, labels, voice and dialogue cards remain subsequent packages, not duplicated here.

The renderer's view publication uses the same head/lean formula as its `ApplyHeadPosition`. Its floor hook reuses the shared correction on both SFR passes. The LE3 native pawn sample and camera-ownership proof are prerequisites for claiming a working floor lock; linking the module is not that proof. The imported forward lean emphasis remains upstream's 3x. Metric world scale, tracked gun alignment and comfort require native/headset measurement before acceptance.

## Native and thread boundaries

All three upstream `ProcessEvent` setter sites go through the host's validated game-thread queue. The queue depends on L3-1's verified reflection layout; until then calls fail safely rather than running script on Present. The imported renderer's other native camera access remains pinned upstream code and requires the planned native signature audit.

Controller state uses the shared live/replay publication and XInput pad mapping. Semantic conversation mode preserves native interrupt triggers; cinematic/conversation movement sticks are zeroed so authored seated scenes cannot traverse. No orbital mapping is claimed yet. The native game remains authoritative for actions and input locks. Real pad/keyboard camera ownership must also be observed in seated fixtures.

Native head aim is disabled: view tracking does not rotate weapon aim. Tracked gun/body support is L3-3; until then weapon aim is native stick/mouse. Upstream's first-person conversations (ConvoFirstPerson) and native VR cinematic options are hard-disabled even if an old ini enables them (ME1's own first-person conversations are release scope since 2026-11-19, through `game/SequelConversationWorld`: docs/trilogy-plan.md T49). Their developer-menu controls are unavailable. Native flat cinema and the original native reply/interrupt flow remain. The shared 3D cinema/Thespian integration is L3-4, with authored seated scenes keeping their traversal locks (first-person conversations: T49, release scope since 2026-11-19).

Process-specific `[Debug] openxrRuntime` follows the existing fake-headset mechanism without changing the machine runtime. Hotkeys respect foreground ownership. The development-only `[Debug] dumpOnly=1` returns immediately after the existing code dump attempt and before renderer/tick/mod hooks, logging `dump-only: code dump attempted; renderer and hooks disabled`. It does not claim a successful dump: the runner must verify the timestamp-named binary and metadata. Monolithic/player builds ignore this diagnostic option.

## Integration and acceptance

The coordinator owns CMake and HostServices composition. Build only the imported `.cpp` sources excluding `dllmain.cpp`, with `MEVR_ASI`, existing ImGui/MinHook and the shared host modules above. Keep unsupported host callbacks null. Do not also link `RendererLe2` or the other Halcyon archive into the LE3 host.

Required proof, in order:

1. Development and monolithic LE3 builds, LE1/LE2 regression builds, SPDX and whitespace checks. No native launch is implied by a build.
2. A protected desktop dump-only run validates native signatures before the renderer's first native run; runner backups and byte-for-byte cleanup remain mandatory.
3. One isolated LE3 session: a normal save loads, exactly one renderer/session owns submission, both eyes receive changing valid images, controllers and view share space, console and snapshot/record/replay requests respond. Compare source/copy/eye captures for HDR correctness.
4. Recenter, focus loss/recovery, pause/native menu, save/load and clean exit do not leave stale poses, held inputs or competing hooks. Seated authored conversation keeps native positioning and does not traverse; native reply/interrupt buttons still work.
5. Quest 3 with Virtual Desktop: stereo scale, eye parity, view/controller alignment, floor height, colour and comfort; performance on a real headset. Fake-headset frame pacing cannot certify stutter.

L3-2 is not accepted until these gates are recorded. This foundation deliberately leaves shared UI/cinema and tracked body/gun work to their planned adapters.

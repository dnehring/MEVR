<!-- SPDX-License-Identifier: MIT -->
# LE3 native renderer audit — 2026-11-15

This is an offline executable/source audit and candidate correction, not headset acceptance. The protected runtime capture is MassEffect3.exe timestamp `60AEF0C4`, `.text` RVA `0x1000`, size `0x1065FC1`, SHA-256 `50c79c1c3e64ceca9af90ce87af919f01361cc9f5adb7efa2de8166e58bcd95d`. Raw executable/script/decompiler output remains private.

## Confirmed renderer boundaries

| Boundary | Evidence and result |
| --- | --- |
| CalcSceneView `0x6CFFD0` | Native function boundary and six-argument shape; calls the player view/FOV events. Captured unique prologue checked before hooking. |
| Draw `0x6D1E30` | Native function boundary and three-argument shape; calls CalcSceneView. Captured unique prologue checked before hooking. |
| AllocateViewState `0x3440F0` | Native one-argument allocator, allocates `0xD60` then constructs the state. Captured unique prologue checked before direct calls. |
| FSceneView construction `0x33D7D0` | Constructor supports existing state `0x8`, rect `0x64`, view `0x90`, projection `0xD0`, translated view `0x190`, translated view-projection `0x1D0`, pre-view translation `0x250`, view-projection `0x260`, view origin `0x320` accesses. |
| LocalPlayer native view fields | Native view build agrees with actor `0x68`, state `0x46C`, origin `0x59C`, size `0x5A4/0x5A8`. |

These checks protect the imported entry contracts; they do not prove stereo image correctness or every imported experimental probe.

## FOV source correction

The import searched camera memory for floats equal to the current FOV, then wrote up to 24 matches. Unrelated constants could match; restoration also used the first previous value for every field. This is replaced by the exact installed `Engine.pcc` path:

1. `PlayerController.GetFOVAngle` reads `Engine.PlayerController.PlayerCamera` (`0x684`). The imported `0xE28` is the separate `SFXPlayerController.MyCamera` gameplay-mode member.
2. `Camera.GetFOVAngle` chooses `LockedFOV` (`0x4D4`) when `bLockedFOV` (`0x504`, mask 1) is set; otherwise `CameraCache.POV.FOV` (`0x448 + 0 + 0x18`). Installed source and LE3 SDK metadata agree. Independent live reflection rows are supplied by the extended renderer ABI audit.
3. The CalcSceneView scope changes just that getter field. It revalidates camera and active field before writing, records the exact previous value, and restores only the same live camera/address while it still contains the value we wrote. Camera replacement and newer native writes are preserved. Missing/dying cameras and invalid FOV values refuse the edit.

The director lens remains untouched for shared cinema. This removes speculative writes, but glass/refraction correctness must still be captured in actual stereo gameplay and ADS transitions.

## Known unsupported import path

The old `d3d_capture.cpp` UI-caller constant `0x4850A2` belongs to the native registration function beginning `0x484FB0`, not a verified UI compositor. Its `g_uiPerEye` path is already inactive and must not be enabled using this constant. Shared cinema UI instead uses the existing late SFR HUD capture seam. No native gameplay HUD crop is accepted from this audit; the read-only GFx probe reports local clip geometry and still needs parent transforms and a live combat fixture.

## Validation and remaining acceptance

The modified native translation units compile. A bounded controlled-memory fixture executes the production FOV helper block and passes locked/cache selection, exact restoration, native overwrite preservation, camera swap, lock-bit transition, missing camera, and NaN rejection. The fixture is private test tooling, not runtime evidence. Full integration builds and the independent reflection table are coordinator-owned.

Still required: native hook marker and live camera-field reflection, first stereo frame/capture, independent left/right cinema images, gameplay glass/ADS FOV transitions, menu/cinema ownership transitions, and physical-headset tracking/comfort. Imported cover/crouch/marker probes and optional experimental native paths require their own evidence before being advertised as verified.

## Authored traversal yaw and movement

The LE3 bridge accepts the v41 `Traversal::Presentation` contract only when its schema, timestamp, scene, and floor-space counters are valid. The game applies authored-camera yaw cancellation only after this publication succeeds. After the ordinary shared pad merge and menu filter, on-foot movement receives the inverse yaw correction through the existing `GamepadMapping::RotateStick`; renderer head look is unchanged. Native reply-wheel axes are not rotated.

An active presentation that expires, loses focus, or crosses a recenter/session blocks walking until a fresh view publication or explicit clear. It is not silently treated as an angle of zero. Recenter/session callbacks invalidate the retained basis, and a fresh inactive publication or null clear releases it. Shared menus retain input ownership. Focused tests cover inverse rotation, stale input, schema/range rejection, recenter/session changes, lifecycle invalidation, focus loss, and release. Native traversal, later camera-blend recovery, and physical comfort remain unaccepted.

## Scripted first-person preview control

Use the existing channel: `tools/snapshot.ps1 -FirstPerson on -Delay 2 -Tag le3-firstperson`, `-FirstPerson off`, or `-FirstPerson status`. It writes `firstperson=on|off|status` alongside the ordinary snapshot options to `snapshot.request`; development/request permission is the existing TestLoop gate. No keyboard injection, extra request channel, persistent ini setting or host ABI was added.

The game-thread request parser queues the option. The owning Present/frame thread applies LE2/LE3's existing `EngineProbe::SetFirstPerson`; the requested capture delay starts after application, and a snapshot cannot start while control remains queued. Invalid values never toggle. LE1 reports unsupported for mutation and preserves its renderer flag. The snapshot script fails a requested mutation when the host is old, reports unsupported, or returns a different actual flag.

Both snapshot JSON and `testloop.status` include `firstPerson.requested`, `result`, the actual enable flag, control support, fresh CalcSceneView hook installation state, raw and semantic game mode, the renderer's existing ownership report, and application time. The enable flag and ownership report are diagnostics, not proof that a particular native camera profile wrote the correct eye position. Visual acceptance still needs the captured first-person body/weapon view.

Focused parser tests cover ordinary snapshots, on/off/status, case handling and malformed requests. All three renderer/TestLoop translation units compile with warnings-as-errors; the native entry getters use the existing atomic hook state. No native preview was launched by this lane.

// SPDX-License-Identifier: MIT
#pragma once

// Planar floor reflections (SceneCaptureReflectComponent) re-render at a throttled rate (UE3's
// FrameRate). On the frames they skip, the reflection lags the view, which reads as judder on
// every head turn. This raises each one to "every frame" with the engine's own SetFrameRate.
// (Found by Halcyon's MassEffect1-VR; research/13.)
namespace mevr::game::reflections {

// Finds SetFrameRate. False until the script packages have loaded; retry later.
bool TryStartup();
// Game thread: rescans for new reflection components every few seconds.
void Tick(float deltaSeconds);
void LogStats();
void Shutdown();

} // namespace mevr::game::reflections

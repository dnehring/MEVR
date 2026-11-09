// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace mevr::hook { class Context; }

namespace mevr::host::motion_blur_guard {

// LE1 host lifetime. Forces the engine's combined post-process proxies to their matching
// no-blur specializations before rendering, including proxies cached across cinematics.
// [Performance] motionBlurGuard = 0 disables installation for diagnosis; restart required.
bool Install(hook::Context& context);
// Safe to skip the old menu-return reset only while installed and no foreign proxy has appeared.
// A foreign proxy latches this false for the session; the settings fallback then resumes.
bool ProtectsGameplay();
void Tick();  // game-thread diagnostics; no engine writes or settings resets
std::string StatsJson();

} // namespace mevr::host::motion_blur_guard

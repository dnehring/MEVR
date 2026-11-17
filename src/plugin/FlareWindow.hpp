// SPDX-License-Identifier: MIT
#pragma once

// Lens flares in life-size stereo's mode 2 (research/30 section 9.6, lane FL): the engine's reflection pass of a lens flare
// (engine/Addresses.hpp's row "FLensFlareDynamicData::RenderReflections") places its elements on the screen through the
// CPU's symmetric window and turns them back into world points through the view's cached inverse, which in mode 2 is the
// eye's off-centre frustum's: every element then lands at the window's screen coordinates in each eye's image, the same
// pixel of both eyes. For that call only, the view's InvTranslatedViewProjectionMatrix holds the inverse of the CPU's own
// window (core/vr/FlareWindow); put back as the call returns. Render thread; SEH-guarded reads and writes.
//
// `[Stereo] flareWindow = 1` in ASI\<GAME>MEVR.ini (0 by default: ME3 renders mode 1 by default, where nothing differs);
// a development build also reads %LOCALAPPDATA%\MEVR\stereo.measure's `[Stereo] flareWindow = 0 | 1`, which wins (for
// a run whose ini a runner keeps exact). Both read once a second. A game without the row stands down with one log line.

#include <string>

namespace mevr::hook { class Context; }

namespace mevr::host::flare_window {

bool Install(hook::Context& context);
void Tick();               // the setting, live; a counts line every 10 s while it acts
std::string StatsJson();   // {"installed", "enabled", "calls", "rewritten", "same", "faults"}

} // namespace mevr::host::flare_window

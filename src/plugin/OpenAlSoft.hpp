// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The game's audio through OpenAL Soft, for two ears (research/69 "Real 3D audio", 2026-11-10). LE1's audio engine (ISACT)
// plays through OpenAL: the exe imports 34 OpenAL functions from Creative's router (Binaries\Win64\OpenAL32.dll), whose
// software device (wrap_oal.dll) only pans a positioned sound between the two channels. OpenAL Soft renders the same
// sources with head-related filters: a sound to one side reaches the far ear later and duller (measured: 0.9 ms), which
// tells left from right, front from back and near from far.
//
// Nothing of the game's is replaced: the release installs OpenAL Soft as ASI\audio\soft_oal.dll (built from its source
// release by tools/fetch-openal-soft.ps1; LGPL-2.0-or-later, its source in the release's licenses\), and at attach, before
// the game opens its audio device, the host loads it and detours every function of the router's that OpenAL Soft also
// has to OpenAL Soft's (the game's audio engine reaches the router both through its imports and through GetProcAddress:
// with only the import slots changed it still opened Creative's device). Opening the device and making its context go
// through two small wrappers: the device the game asks for, else OpenAL Soft's default; the game's context attributes plus
// HRTF on (or off). Steam's "verify files" has nothing to put back, and without the DLL, or with [Audio] spatial = game,
// the game's own OpenAL plays as before.
//
//   [Audio] spatial = hrtf (default: headphones, every headset)  | soft (OpenAL Soft without HRTF: loudspeakers)
//                   | game (the game's own OpenAL)
// Read once at attach: a change applies at the next start. LE1 only.
namespace mevr::hook {
class Context;
}

namespace mevr::host::openal_soft {

// After the hook context exists (the router's functions are detoured through it), before the game opens its audio device.
void Attach(hook::Context& init);
// What the game's audio runs on, for the log's status lines and reports.
std::string Describe();

} // namespace mevr::host::openal_soft

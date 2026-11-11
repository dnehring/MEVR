// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace mevr::hook {
class Context;
}

// A conversation's voices come from where the speakers stand (research/69 "The voices, placed", 2026-11-10; the owner: the
// Council should sound a little like an intercom, other scenes spatial). LE1's audio is ISACT. A line's voice is played on
// an audio component the speaker's pawn owns, and whether it is placed is decided per sound cue: 82% of the dialogue cues
// have no attenuation node and are played with spatialization off, in the centre, wherever the speaker stands
// (FISACTSoundSource::Update, LE1 0x7BA90: once a frame per playing source it sends the volume, the pitch, a
// "relative to the listener" flag and a position to ISACT). That function is post-hooked: for a source whose audio
// component is owned by one of the actors named here, the flag is sent again as "in the world" and the position as the
// place given (ISACT's axes: X, Z, -Y, in metres), and the volume again times the gain given. Sent every update, so it
// follows a moving head and a walking player mid-line; a source of nobody named is left exactly as the game sent it.
//
// The hook runs on the thread that updates the audio device (the game thread, checked: a call from another thread places
// nothing and is counted). LE1 only; no address for LE2/LE3 (they are Wwise).
namespace mevr::engine::voice_place {

bool Install(hook::Context& init);
// False: still live (or a call still inside it after waiting), so this module must stay loaded.
bool Uninstall(hook::Context& init, bool waitForCalls);
bool Installed();

inline constexpr int kMaxVoices = 16;
struct Voice {
    void const* actor = nullptr;  // the pawn whose audio components' sources are placed
    float at[3] = {};             // where its voice comes from, world units
    float gain = 1.0f;            // times the game's own volume for the source
};
// Who is placed from now on (count 0: nobody; the game's own mix). Game thread.
void Set(Voice const* voices, int count);

struct Stats {
    unsigned updates = 0;     // source updates seen since the last Take
    unsigned placed = 0;      // ... of which a named actor's, placed
    unsigned offThread = 0;   // ... seen on another thread than the one that names the actors: left alone
    unsigned faults = 0;      // access violations inside our part (after three the hook places nothing more)
    int voices = 0;           // actors named now
};
Stats Take();

} // namespace mevr::engine::voice_place

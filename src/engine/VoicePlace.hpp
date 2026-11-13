// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <vector>

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
// Since 2026-11-10 (A1, A2) three lists name actors: the first-person conversation's speakers, people speaking near the
// player on foot (their unplaced sources only), and the player's gun.
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
    // Only the sources the game left unplaced (in the centre: a cue without an attenuation node, the 82% of dialogue);
    // the actor's own placed sounds (footsteps, barks with a falloff of their own) stay exactly as the game sent them.
    bool centredOnly = false;
};
// Who names voices (research/69 "What remains", A1 and A2): each its own list, replaced whole by its Set. A source whose
// owner more than one list names is placed by the first in this order.
enum class Publisher : std::uint8_t {
    Conversation = 0,  // game/ConversationWorld: everyone of a first-person conversation but the player
    Overheard = 1,     // game/SoundPlace: people speaking near the player on foot (centred sources only)
    Gun = 2,           // game/SoundPlace: the player's own weapon, at the gun model
    Thespian = 3,      // game/Thespian: the player's pawn with gain 0 while a spoken reply's recording would play
};
inline constexpr int kPublishers = 4;
inline constexpr int kMaxPerPublisher = 32;  // what one list may hold (Set clamps; the conversation's own stays kMaxVoices)
char const* Name(Publisher publisher);
// Who is placed from now on (count 0: nobody; the game's own mix). Game thread. The conversation's list (game/ConversationWorld).
void Set(Voice const* voices, int count);
// The same for any publisher's list.
void Set(Publisher publisher, Voice const* voices, int count);
// The actors of a publisher's list whose sources were placed since the last call (at most `max`, each once): who spoke.
// Game thread.
int TakeHeard(Publisher publisher, void const** out, int max);
// The owners of sources the game left in the centre that no list names, since the last call (each once, at most `max`): who may
// be speaking outside the lists (a crowd past a list's cap). The caller keeps only actors it knows. Game thread.
int TakeHeardUnnamed(void const** out, int max);

struct Stats {
    unsigned updates = 0;     // source updates seen since the last Take
    unsigned placed = 0;      // ... of which placed for the conversation's list
    unsigned placedBy[kPublishers] = {};  // ... placed, by whose list
    unsigned leftPlaced = 0;  // ... a named actor's (centred only) that the game had placed itself: left alone
    unsigned offThread = 0;   // ... seen on another thread than the one that names the actors: left alone
    unsigned faults = 0;      // access violations inside our part (after three the hook places nothing more)
    int voices = 0;           // actors the conversation's list names now
    int voicesBy[kPublishers] = {};
};
Stats Take();
// The same counts since the hook went in, never reset (for a second reader: Take is game/ConversationWorld's).
struct Totals {
    unsigned long long updates = 0;
    unsigned long long placedBy[kPublishers] = {};
    unsigned long long leftPlaced = 0;
};
Totals SinceInstall();

// A census of the sources the game updates (`mevr voices census [seconds]`; research/69 A2): for `seconds`, every distinct
// owner and audio component a source belongs to, with its cue, whether the game placed it itself, how often and by whose
// list we did. To tell in one look which actor owns a sound (the player's gun: the weapon actor or his pawn). Game thread.
void StartCensus(double seconds);
bool CensusRunning();
struct CensusEntry {
    void const* owner = nullptr;       // ActorComponent.Owner of the source's audio component
    void const* component = nullptr;   // the audio component
    void const* cue = nullptr;         // AudioComponent.SoundCue
    bool spatialized = false;          // the game placed it itself (the wave instance's spatialization after its update)
    int componentSpatialization = -1;  // AudioComponent.CurrentUseSpatialization (-1: not read)
    unsigned updates = 0;
    unsigned placed = 0;
    int placedBy = -1;                 // the publisher whose list placed it last (-1: none)
};
inline constexpr int kCensusEntries = 64;
// The census so far (at most kCensusEntries), and whether it filled up. Clears it.
std::vector<CensusEntry> TakeCensus(bool* full = nullptr);

} // namespace mevr::engine::voice_place

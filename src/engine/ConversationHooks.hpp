// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace mevr::hook {
class Context;
}

// Two of the game's conversation decisions the mod takes part in (research/69, read with Ghidra; LE1 only):
//
// - UBioConversation::SetPositionRotator (0xC862D0, the class's vtable slot 0x2C0; this, actor, FVector*, FRotator*) is the
//   one place a conversation puts a person on a stage mark: ABioStage::PlaceActors (0xC93B60, once, when a conversation that
//   is not ambient starts) calls it for the player, the owner, the squad and the other speakers, and a line's
//   BioEvtSysTrackSetFacing track (0xC95AF0) calls it for its actor, with the mark's place when the key names a stage node
//   (the "back on his mark at nearly every line" of research/59) and with a zero place for a facing alone. It stops the
//   pawn, moves it when the place is not zero, drops it to the floor, sets its rotation and its controller's focus. The
//   pre-hook asks the provider about a call with a place: kept, the original runs with a zero place (the game's own
//   facing-only path), so the person turns as the scene wants and does not move. A matinee's move track is another path.
//
// - ABioWorldInfo::InterruptConversation (0xB60460, the class's vtable slot 0x9F0; this, the conversation that wants to
//   start or null) ends the current conversation if it is ambient: called with the newcomer by
//   ABioWorldInfo::StartConversation (0xB65A50), which starts a conversation only when none is current or the current one
//   is ambient and was ended this way (a conversation that is not ambient refuses every newcomer), and with null when an
//   ambient conversation's player is beyond its interrupt range of the speaker or its owner is gone
//   (UBioConversation::UpdateConversation, 0xC87830). The pre-hook asks the provider: refused, the original is not called,
//   the current conversation goes on and StartConversation answers the newcomer as it would during a full conversation.
//
// - AActor::MoveWithInterpMoveTrack (0x4B75F0, the actor's vtable slot +0x558; this, the move track, its instance, the time,
//   the delta) is the one place a matinee's move track places its actor: AActor::physInterpolating calls it each tick for an
//   actor a track holds (PHYS_Interpolating), and so do SeqAct_Interp.SetPosition and the cinematic skip. The pre-hook asks
//   the provider: withheld, the original is not called and the hook answers "not moved". (An authored walk of Shepard: the
//   walk to the council; research/69 "Authored scenes".) It goes in with the other two; they work without it.
//
// The hooks run on the game thread, inside the engine's own code; the providers must be quick and never call script.
namespace mevr::engine::conversation_hooks {

// True: this actor of this conversation stays where it stands (the call's place is `location`, never zero here).
using KeepProvider = bool (*)(void const* conversation, void const* actor, float const* location);
// True: the current conversation is not ended for `newcomer` (null: the distance or owner test's interruption).
using InterruptProvider = bool (*)(void const* worldInfo, void const* newcomer);

// True: this actor is not moved by its move track this tick.
using MoveTrackProvider = bool (*)(void const* actor, void const* track, void const* instance, float time);

// The hooks go in once (lazily): false if they could not (another game, not a function start).
bool Install(hook::Context& init);
// False: still live (or a call still inside one after waiting), so this module must stay loaded.
bool Uninstall(hook::Context& init, bool waitForCalls);
bool Installed();

void SetKeepProvider(KeepProvider provider);
void SetInterruptProvider(InterruptProvider provider);
void SetMoveTrackProvider(MoveTrackProvider provider);
// Where a move track wants its actor at `time` (the track's own UInterpTrackMove::GetLocationAtTime, its vtable slot +0x390,
// as the hooked function calls it: track, instance, time, FVector* place, FRotator* rotation): for a provider that withholds
// the move and still has to know where the script has the actor. False if the call could not be made or said no.
bool MoveTrackPlace(void const* track, void const* instance, float time, float place[3]);

// Since the hooks went in (cumulative: game/ConversationWorld and game/ConversationFlow each keep what they last saw).
struct Stats {
    unsigned placements = 0;  // SetPositionRotator calls with a place
    unsigned kept = 0;        // ...of which the place was withheld
    unsigned facings = 0;     // calls with a zero place (a facing alone)
    unsigned interrupts = 0;  // InterruptConversation calls
    unsigned refused = 0;     // ...of which were refused
    unsigned trackMoves = 0;  // MoveWithInterpMoveTrack calls
    unsigned trackKept = 0;   // ...of which were withheld
};
Stats Totals();

} // namespace mevr::engine::conversation_hooks

// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/ConversationWorld.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <string_view>
#include <vector>

// Where sounds come from on foot (research/69 "What remains", A1 and A2; game/SoundPlace, engine/VoicePlace). The game plays
// 82% of its dialogue cues in the centre wherever the speaker stands, so people talking near the player (a flowed scene, an
// ambient conversation, a line overheard in passing) sound inside his head. The people near him are named with their head
// places under the conversation's own rule (conversation_world::PlaceVoice: within 8 m, kept to 10, our own loudness for the
// distance); the hook then places only the sources the game left in the centre, so a bark with a falloff of its own or a
// footstep keeps the game's mix. Game-independent: unit-tested in tests/SoundPlaceTests.cpp.
namespace mevr::vr::sound_place {

namespace cw = conversation_world;

// [Audio] overheard = 1 | 0 and gun = 1 | 0 (the mod's; the host's spatial and gainDb sit in the same section).
bool ParseOnOff(std::string_view text, bool fallback);

// A person near the player this tick.
struct Candidate {
    std::uint64_t id = 0;  // the pawn (an identity; never the player's own)
    cw::Vec3 head;         // where its voice comes from, world units
};
struct Pick {
    std::uint64_t id = 0;
    cw::Vec3 at;
    double gain = 1.0;
    double distanceUnits = 0.0;
    bool speaking = false;  // one of its sources was placed within `speakingHoldS`
};
struct Limits {
    cw::VoiceLimits voice;    // the conversation's rule (8 m, kept to 10; full within 2 m, 3 dB a doubling, at most 6)
    int max = 16;             // how many are named at once
    double speakingHoldS = 2.0;  // a person heard this recently is named before anyone silent (a crowd past `max`)
};

// The people named each tick: everyone within the rule's reach, the speaking first, then the nearest, at most `max`. Keeps
// each person's hysteresis (a step back mid-line changes nothing) and forgets those no longer near.
class Overheard {
public:
    std::vector<Pick> Step(cw::Vec3 const& listener, std::span<Candidate const> nearby, double nowS, Limits const& limits);
    // The hook placed one of this person's sources (they speak).
    void Heard(std::uint64_t id, double nowS);
    // Nothing near any more (off foot, the setting off): everyone forgotten.
    void Clear();
    std::size_t Known() const { return people_.size(); }

private:
    struct Person {
        cw::VoiceState state;
        double heardS = -1e9;
        bool seen = false;
    };
    std::map<std::uint64_t, Person> people_;
};

// Who is looked at each tick for a voice: pawns whose place is within this of the listener (the rule's keep radius plus a
// margin for the head above the feet), before their head bone is read.
double CandidateReachUnits(Limits const& limits);

} // namespace mevr::vr::sound_place

// SPDX-License-Identifier: MIT
#include "core/vr/SoundPlace.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace mevr::vr::sound_place {

bool ParseOnOff(std::string_view text, bool fallback) {
    std::string word;
    for (char const c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (word == "1" || word == "on" || word == "true" || word == "yes" || word == "spatial") {
        return true;
    }
    if (word == "0" || word == "off" || word == "false" || word == "no" || word == "game") {
        return false;
    }
    return fallback;
}

std::vector<Pick> Overheard::Step(cw::Vec3 const& listener, std::span<Candidate const> nearby, double nowS, Limits const& limits) {
    for (auto& [id, person] : people_) {
        person.seen = false;
    }
    std::vector<Pick> picks;
    for (Candidate const& c : nearby) {
        if (c.id == 0) {
            continue;
        }
        Person& person = people_[c.id];
        person.seen = true;
        cw::VoiceAt const at = cw::PlaceVoice(person.state, listener, c.head, limits.voice);
        if (!at.placed) {
            continue;
        }
        Pick pick;
        pick.id = c.id;
        pick.at = at.at;
        pick.gain = at.gain;
        pick.distanceUnits = at.distanceUnits;
        pick.speaking = nowS - person.heardS <= limits.speakingHoldS;
        picks.push_back(pick);
    }
    std::erase_if(people_, [](auto const& entry) { return !entry.second.seen; });
    std::stable_sort(picks.begin(), picks.end(), [](Pick const& a, Pick const& b) {
        if (a.speaking != b.speaking) {
            return a.speaking;
        }
        return a.distanceUnits < b.distanceUnits;
    });
    if (static_cast<int>(picks.size()) > std::max(0, limits.max)) {
        picks.resize(static_cast<std::size_t>(std::max(0, limits.max)));
    }
    return picks;
}

void Overheard::Heard(std::uint64_t id, double nowS) {
    auto const it = people_.find(id);
    if (it != people_.end()) {
        it->second.heardS = nowS;
    }
}

void Overheard::Clear() {
    people_.clear();
}

double CandidateReachUnits(Limits const& limits) {
    double const keep = std::max(limits.voice.withinUnits, limits.voice.keepUnits);
    return keep + 250.0;  // a head is at most about 2 m above the place the pawn stands at, plus a margin
}

} // namespace mevr::vr::sound_place

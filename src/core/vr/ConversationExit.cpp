// SPDX-License-Identifier: MIT
// People leaving a first-person conversation: the rule (core/vr/ConversationExit.hpp, research/conversation-exits.md).
#include "core/vr/ConversationExit.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace mevr::vr::conversation_exit {

namespace {

constexpr double kPi = 3.14159265358979323846;

double Flat(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

} // namespace

Mode ParseMode(char const* text, Mode fallback) {
    if (text == nullptr) {
        return fallback;
    }
    std::string word;
    for (char const* p = text; *p != '\0'; ++p) {
        if (!std::isspace(static_cast<unsigned char>(*p))) {
            word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
        }
    }
    if (word == "walk" || word == "on" || word == "2") {
        return Mode::Walk;
    }
    if (word == "hold" || word == "1") {
        return Mode::Hold;
    }
    if (word == "off" || word == "0" || word == "none") {
        return Mode::Off;
    }
    return fallback;
}

char const* Name(Mode mode) {
    switch (mode) {
    case Mode::Off: return "off";
    case Mode::Hold: return "hold";
    case Mode::Walk: return "walk";
    }
    return "?";
}

char const* Name(Kind kind) {
    switch (kind) {
    case Kind::Deactivate: return "deactivate";
    case Kind::Hide: return "hide";
    case Kind::Teleport: return "teleport";
    case Kind::Destroy: return "destroy";
    case Kind::StreamOut: return "stream out";
    }
    return "?";
}

char const* Name(Act act) {
    switch (act) {
    case Act::Keep: return "keep";
    case Act::Apply: return "apply";
    case Act::ApplyUnderDip: return "apply under a dip";
    }
    return "?";
}

Verdict Classify(Kind kind, Subject const& subject, Context const& context, Limits const& limits) {
    if (context.mode == Mode::Off) {
        return {false, "worldExit = off: the game's change at once"};
    }
    if (!context.worldMode) {
        return {false, "not the first-person conversation mode"};
    }
    if (kind == Kind::Destroy) {
        return {false, "a destroyed actor can't be given back later: at once"};
    }
    if (kind == Kind::StreamOut) {
        return {false, "a streaming change can't wait (the next area may wait on it): at once"};
    }
    if (context.underBlack) {
        return {false, "the game's own black screen covers it: at once"};
    }
    if (subject.player) {
        return {false, "the player's own pawn"};
    }
    if (subject.squad) {
        return {false, "a squadmate: the squad is the next scene's"};
    }
    if (subject.neededNext) {
        return {false, "the next scene needs them at once"};
    }
    if (!subject.person) {
        return {false, "not one of the scene's people"};
    }
    bool const recent = context.sinceSceneEndS >= 0.0 && context.sinceSceneEndS <= limits.windowS;
    if (!context.sceneShown && !recent) {
        return {false, "no scene shown in the world now or just before"};
    }
    if (!subject.seen) {
        return {false, "out of the player's sight: at once"};
    }
    return {true, "in the player's view: held, the person leaves first"};
}

std::vector<Vec3> CandidatePoints(Vec3 person, Vec3 eyes, Limits const& limits) {
    // Bearings measured from "straight away from the eyes" (flat), ordered 0, +30, -30, +60, -60 ... 180.
    double away = std::atan2(person.y - eyes.y, person.x - eyes.x);
    if (Flat(person, eyes) < 1.0) {
        away = 0.0;
    }
    double const distances[3] = {limits.minUnits, 0.5 * (limits.minUnits + limits.maxUnits), limits.maxUnits};
    std::vector<Vec3> out;
    out.reserve(36);
    constexpr int kSteps[12] = {0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6};  // in 30-degree steps off straight away
    for (int const step : kSteps) {
        double const bearing = away + step * (kPi / 6.0);
        for (double d : distances) {
            out.push_back({person.x + std::cos(bearing) * d, person.y + std::sin(bearing) * d, person.z});
        }
    }
    return out;
}

std::optional<Pick> PickDestination(std::span<Candidate const> candidates, Vec3 person, Vec3 eyes, Limits const& limits) {
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].scripted && candidates[i].path) {
            return Pick{i, candidates[i].hidden};  // where the script sends them, reachable in the same space
        }
    }
    double const personFromEyes = Flat(person, eyes);
    double ax = person.x - eyes.x, ay = person.y - eyes.y;
    double const an = std::hypot(ax, ay);
    if (an > 1e-6) {
        ax /= an;
        ay /= an;
    } else {
        ax = 1.0;
        ay = 0.0;
    }
    std::optional<Pick> best;
    double bestScore = -1e9;
    std::optional<Pick> away;
    double awayScore = -1e9;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        Candidate const& c = candidates[i];
        if (!c.path || c.scripted) {
            continue;
        }
        double const d = Flat(c.at, person);
        if (d < limits.minUnits - 1.0 || d > limits.maxUnits + 1.0) {
            continue;
        }
        if (Flat(c.at, eyes) < personFromEyes - 50.0) {
            continue;  // toward the player: never walk past them
        }
        double const awayness = ((c.at.x - person.x) * ax + (c.at.y - person.y) * ay) / d;  // -1..1
        if (c.hidden) {
            double const score = awayness - 0.5 * d / limits.maxUnits;
            if (score > bestScore + 1e-9) {
                bestScore = score;
                best = Pick{i, true};
            }
        } else if (awayness >= 0.5) {
            // Not hidden: within 60 degrees of straight away, the farthest (it leaves the view soonest).
            double const score = awayness + d / limits.maxUnits;
            if (score > awayScore + 1e-9) {
                awayScore = score;
                away = Pick{i, false};
            }
        }
    }
    return best ? best : away;
}

StepResult Step(Held& held, Facts const& facts, Limits const& limits) {
    if (facts.viewAway) {
        return {Act::Apply, "the player sees no world in first person now"};
    }
    if (facts.seen) {
        held.unseenSinceS.reset();
    } else {
        if (!held.unseenSinceS) {
            held.unseenSinceS = facts.nowS;
        }
        if (facts.nowS - *held.unseenSinceS >= limits.settleS) {
            return {Act::Apply, "out of the player's sight"};
        }
    }
    if (facts.nowS - held.heldS >= limits.holdMaxS) {
        return {Act::ApplyUnderDip, "held as long as anything is held"};
    }
    bool const walking = facts.walking && facts.nowS - held.heldS < limits.walkMaxS;
    if (walking) {
        held.waitSinceS.reset();
        return {Act::Keep, "walking away"};
    }
    if (!held.waitSinceS) {
        held.waitSinceS = facts.nowS;
    }
    if (facts.nowS - *held.waitSinceS >= limits.lookAwayS) {
        return {Act::ApplyUnderDip, "the player kept looking"};
    }
    return {Act::Keep, "waiting for the player to look away"};
}

bool WalkOverdue(Held const& held, double nowS, Limits const& limits) {
    return nowS - held.heldS >= limits.walkMaxS;
}

bool IsSwap(Vec3 heldAt, Vec3 shownAt, double sinceHoldS, Limits const& limits) {
    double const d = std::sqrt((heldAt.x - shownAt.x) * (heldAt.x - shownAt.x) + (heldAt.y - shownAt.y) * (heldAt.y - shownAt.y) +
                               (heldAt.z - shownAt.z) * (heldAt.z - shownAt.z));
    return sinceHoldS >= 0.0 && sinceHoldS <= limits.swapWithinS && d <= limits.swapUnits;
}

bool VanishedInView(bool seenBefore, bool presentBefore, bool presentNow, bool dipped) {
    return seenBefore && presentBefore && !presentNow && !dipped;
}

} // namespace mevr::vr::conversation_exit

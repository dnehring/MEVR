// SPDX-License-Identifier: MIT
#include "core/ik/Vent.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <initializer_list>

namespace mevr::ik::vent {
namespace {

bool Finite(OnGun const& p) {
    return std::isfinite(p.along) && std::isfinite(p.up) && std::isfinite(p.side);
}

float Scaled(double strength, double level) {
    if (!(strength > 0.0)) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp(strength * 2.0 * level, 0.0, 1.0));
}

} // namespace

bool InZone(OnGun const& p, Zone const& zone, double margin) {
    if (!Finite(p) || !std::isfinite(margin)) {
        return false;
    }
    return p.along >= -zone.backMeters - margin && p.along <= zone.frontMeters + margin && p.up <= zone.aboveMeters + margin &&
        p.up >= -zone.belowMeters - margin && std::abs(p.side) <= zone.sideMeters + margin;
}

char const* Name(Event event) {
    switch (event) {
    case Event::None: return "none";
    case Event::Grabbed: return "grabbed";
    case Event::Vented: return "vented";
    case Event::Refused: return "refused";
    case Event::Slow: return "slow";
    case Event::Lapsed: return "lapsed";
    case Event::Lost: return "lost";
    }
    return "?";
}

char const* Refusal(double heat, double cooldownLeft, Params const& params) {
    if (cooldownLeft > 0.0) {
        return "cooldown";
    }
    if (!(heat >= 0.0)) {
        return "the heat is unknown";
    }
    if (heat < params.minHeat) {
        return "the gun is cool";
    }
    return nullptr;
}

double Machine::HandSpeed(double now) const {
    auto const v = history_.FitVelocity(now - 0.04, now);
    return v ? Length(*v) : 0.0;
}

void Machine::Reset() {
    history_.Reset();
    inZone_ = false;
    holding_ = false;
    lapsed_ = false;
    grip_ = {};
}

void Machine::ResetAll() {
    Reset();
    readyAt_ = 0.0;
    vents_ = 0;
}

Step Machine::Update(Input const& input, Zone const& zone, Params const& params) {
    Step step;
    bool const valid = input.onGunValid && Finite(input.onGun) && std::isfinite(input.now);
    if (!valid) {
        if (holding_) {
            step.event = Event::Lost;
            step.why = "the gun's frame went away (the weapon away, or arm IK off)";
        }
        step.left = inZone_;
        Reset();
        return step;
    }
    if (history_.Add(input.now, AsVec(input.onGun)) && holding_) {
        // A jump in the hand's place on the gun: no stroke spans it; the squeeze goes on as a hold.
        start_ = input.onGun;
    }
    last_ = input.onGun;
    bool const was = inZone_;
    inZone_ = mevr::ik::vent::InZone(input.onGun, zone, was ? 0.02 : 0.0);  // the free function, not this member
    step.entered = inZone_ && !was;
    step.left = was && !inZone_;

    if (!holding_) {
        if (lapsed_) {
            lapsed_ = input.squeeze > params.grip.off;  // until the grip opens
            return step;
        }
        if (!input.newSqueeze || !inZone_) {
            return step;
        }
        if (!input.canVent) {
            step.event = Event::Refused;
            step.why = "not now";
            return step;
        }
        if (char const* const why = Refusal(input.heat, CooldownLeft(input.now), params)) {
            step.event = Event::Refused;
            step.why = why;
            return step;
        }
        holding_ = true;
        heldSince_ = input.now;
        start_ = input.onGun;
        grip_ = {true, std::max(input.squeeze, params.grip.on)};
        step.event = Event::Grabbed;
        return step;
    }

    bool const down = throwing::UpdateGrip(grip_, input.squeeze, HandSpeed(input.now), params.grip);
    if (down) {
        if (input.now - heldSince_ > params.maxHoldSeconds) {
            holding_ = false;
            lapsed_ = true;
            step.event = Event::Lapsed;
            step.why = "held too long: a grip, not a rack";
        }
        return step;
    }
    holding_ = false;
    // The rack is what the hand did since it took the receiver: the window never reaches back before the squeeze (a
    // reach to the receiver, or a tracking jump just before it, isn't the stroke).
    double const window = std::clamp(input.now - heldSince_, 0.03, 0.12);
    step.release = throwing::EstimateRelease(history_, input.now, window);
    step.back = step.release.valid ? -step.release.velocity.x : 0.0;
    step.travel = start_.along - input.onGun.along;
    if (!step.release.valid || step.release.speed < params.minSpeed) {
        step.event = Event::Slow;
        step.why = "too slow";
    } else if (step.back < params.backFraction * step.release.speed) {
        step.event = Event::Slow;
        step.why = "not back along the gun";
    } else if (step.travel < params.minTravel) {
        step.event = Event::Slow;
        step.why = "too short a pull";
    } else {
        step.event = Event::Vented;
        readyAt_ = input.now + params.cooldownSeconds;
        ++vents_;
    }
    return step;
}

throwing::Buzz EdgeTick(double strength) {
    return {Scaled(strength, 0.3), 220.0f, 0.015f};
}

throwing::Buzz VentPulse(double strength) {
    return {Scaled(strength, 0.9), 110.0f, 0.09f};
}

// ---- The visible rack ----------------------------------------------------------------------------------------------

double RackTarget(double travelMeters, double unitsPerMeter, double maxUnits) {
    if (!std::isfinite(travelMeters) || !std::isfinite(unitsPerMeter) || !std::isfinite(maxUnits) || !(unitsPerMeter > 0.0) ||
        !(maxUnits > 0.0)) {
        return 0.0;
    }
    return std::clamp(travelMeters * unitsPerMeter, 0.0, maxUnits);
}

void Rack::Reset() {
    x_ = v_ = 0.0;
    holding_ = returning_ = false;
    peak_ = returnSeconds_ = 0.0;
}

RackEvent Rack::Update(double dt, bool holding, double target, RackParams const& params) {
    double const maxUnits = std::isfinite(params.maxUnits) && params.maxUnits > 0.0 ? params.maxUnits : 0.0;
    double const step = std::isfinite(dt) ? std::clamp(dt, 0.0, 0.1) : 0.0;
    RackEvent event = RackEvent::None;
    if (holding) {
        if (!holding_) {
            holding_ = true;
            returning_ = false;
            peak_ = 0.0;
        }
        // The part goes where the hand has pulled it (clamped to its travel); its speed is kept for the log.
        double const x = std::isfinite(target) ? std::clamp(target, 0.0, maxUnits) : x_;
        v_ = step > 0.0 ? (x - x_) / step : 0.0;
        x_ = x;
        peak_ = std::max(peak_, x_);
        return event;
    }
    if (holding_) {
        holding_ = false;
        returning_ = x_ > 0.0;
        returnSeconds_ = 0.0;
        v_ = std::min(v_, 0.0);  // let go: it snaps forward at once (a hand still pulling back doesn't carry it further)
        event = RackEvent::Released;
        if (!returning_) {
            x_ = v_ = 0.0;
            return RackEvent::Released;
        }
    }
    if (!(x_ > 0.0)) {
        x_ = v_ = 0.0;
        return event;
    }
    // The spring (semi-implicit Euler in 2 ms steps: stable for any frame time): x'' = -w^2 x - 2 zeta w x'. It stops at
    // the rest place instead of swinging through it.
    double const omega = 2.0 * 3.14159265358979323846 * (std::isfinite(params.returnHz) ? std::clamp(params.returnHz, 0.5, 60.0) : 9.0);
    double const zeta = std::isfinite(params.damping) ? std::clamp(params.damping, 0.0, 4.0) : 0.5;
    double left = step;
    while (left > 0.0 && x_ > 0.0) {
        double const h = std::min(left, 0.002);
        left -= h;
        v_ += (-omega * omega * x_ - 2.0 * zeta * omega * v_) * h;
        x_ += v_ * h;
        if (returning_) {
            returnSeconds_ += h;
        }
        if (x_ > maxUnits) {
            x_ = maxUnits;
            v_ = std::min(v_, 0.0);
        }
    }
    if (x_ <= 0.0 || (std::abs(x_) < 1e-4 && std::abs(v_) < 1e-3)) {
        x_ = v_ = 0.0;
        if (returning_) {
            returning_ = false;
            event = RackEvent::Home;
        }
    }
    return event;
}

namespace {

std::string Lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool Contains(std::string const& lower, std::initializer_list<char const*> words) {
    for (char const* w : words) {
        if (lower.find(w) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> Tokens(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (c == ',' || c == ';' || c == ' ' || c == '\t' || c == '+') {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

// Why bone `b` can't be the part (nullptr: it can).
char const* Unsuitable(std::vector<int> const& parents, std::vector<int> const& protectedBones, int b) {
    int const count = static_cast<int>(parents.size());
    if (b < 0 || b >= count) {
        return "not in the model";
    }
    if (parents[static_cast<std::size_t>(b)] < 0) {
        return "the model's root (the whole gun would move)";
    }
    for (int p : protectedBones) {
        // A protected bone at or below `b`: moving `b` would move it (the muzzle, the tracer, the cool-down effect).
        for (int at = p, guard = 0; at >= 0 && at < count && guard <= count; at = parents[static_cast<std::size_t>(at)], ++guard) {
            if (at == b) {
                return "a socket hangs on it or below it (the muzzle, the tracer, the effects must stay)";
            }
        }
    }
    if (static_cast<int>(Subtree(parents, b).size()) > kMaxPartBones) {
        return "too big a part (more bones below it than the stage moves)";
    }
    return nullptr;
}

} // namespace

std::vector<int> Subtree(std::vector<int> const& parents, int bone) {
    std::vector<int> out;
    int const count = static_cast<int>(parents.size());
    if (bone < 0 || bone >= count) {
        return out;
    }
    out.push_back(bone);
    // Parents come first in a RefSkeleton: one pass in index order collects every descendant after its parent.
    for (int i = bone + 1; i < count; ++i) {
        int const parent = parents[static_cast<std::size_t>(i)];
        if (parent >= 0 && std::find(out.begin(), out.end(), parent) != out.end()) {
            out.push_back(i);
        }
    }
    return out;
}

char const* AutoPartList(int weaponType) {
    switch (weaponType) {
    // The pistol's upper barrel front (seen on a 2026-10-25 pistol, research/21 section 13 item 2): the top of the gun ahead
    // of the hand, which the draw's unfolding slides forward. A slide, if the model has one by that name.
    case 0: return "Barrel_Upper_front, Slide, Barrel_Upper";
    // No shotgun bone list yet (research/21 records none): the pistols' names, a slide or a pump if there are any.
    case 1: return "Barrel_Upper_front, Slide, Pump, Barrel_Upper";
    // The assault rifles' sight housing over the grip (ASLa's 12.8 units over the hand, ASLb's short one behind it;
    // research/21 section 16): the receiver's top, where the left hand takes hold. It moves along the bore, so the sight's
    // line is the same while it does.
    case 2: return "Scope";
    // The sniper rifles' only parts over the receiver are the scope's (SNPa Scope_Eye/Scope_Lens, SNPb Scope_Rear/Mid/
    // Forward), which the scope's picture sits on: none of those; a bone named for a moving part if the model has one.
    case 3: return "";
    default: return "";
    }
}

double PartTravel(std::string_view bone, int weaponType) {
    std::string const name = Lower(bone);
    if (name == "barrel_upper_front") {
        return 3.0;  // a pistol slide's travel, about 3 cm at 100 units per meter
    }
    if (name == "scope") {
        return weaponType == 2 ? 3.0 : 2.0;
    }
    switch (weaponType) {
    case 0: return 3.0;
    case 1: return 4.0;
    case 2: return 3.0;
    case 3: return 4.0;
    default: return 3.0;
    }
}

PartChoice ChoosePart(std::vector<std::string> const& names, std::vector<int> const& parents, std::vector<int> const& protectedBones,
    std::string_view setting, int weaponType) {
    PartChoice out;
    if (names.size() != parents.size() || names.empty()) {
        out.why = "no skeleton";
        return out;
    }
    std::string const lowered = Lower(setting);
    std::vector<std::string> const tokens = Tokens(setting);
    if (lowered == "none" || lowered == "0" || lowered == "off") {
        out.why = "the setting says none";
        return out;
    }
    bool const automatic = tokens.empty() || (tokens.size() == 1 && Lower(tokens[0]) == "auto");
    auto const take = [&](int b, char const* source) {
        out.bone = b;
        out.bones = Subtree(parents, b);
        out.maxUnits = PartTravel(names[static_cast<std::size_t>(b)], weaponType);
        out.source = source;
        out.why.clear();
    };
    auto const find = [&](std::string const& wanted) {
        std::string const w = Lower(wanted);
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (Lower(names[i]) == w) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    std::string refused;
    auto const tryList = [&](std::vector<std::string> const& list, char const* source) {
        for (std::string const& wanted : list) {
            int const b = find(wanted);
            if (b < 0) {
                continue;
            }
            if (char const* const why = Unsuitable(parents, protectedBones, b)) {
                refused += std::format("{}'{}': {}", refused.empty() ? "" : "; ", names[static_cast<std::size_t>(b)], why);
                continue;
            }
            take(b, source);
            return true;
        }
        return false;
    };
    if (!automatic) {
        if (!tryList(tokens, "the setting")) {
            out.why = refused.empty() ? std::format("none of '{}' is in the model", std::string(setting))
                                      : std::format("not usable: {}", refused);
        }
        return out;
    }
    if (tryList(Tokens(AutoPartList(weaponType)), "the type's list")) {
        return out;
    }
    // A bone named for a moving part, never one named for a sight (the sights ride on those) or a grip.
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::string const n = Lower(names[i]);
        if (!Contains(n, {"slide", "bolt", "cover", "heatsink", "heat_sink", "sink", "vent", "receiver", "charg", "breech", "hatch", "eject"}) ||
            Contains(n, {"scope", "sight", "optic", "lens", "eye", "reticle", "handle", "grip", "trigger", "guard", "stock"})) {
            continue;
        }
        if (char const* const why = Unsuitable(parents, protectedBones, static_cast<int>(i))) {
            refused += std::format("{}'{}': {}", refused.empty() ? "" : "; ", names[i], why);
            continue;
        }
        take(static_cast<int>(i), "its name");
        return out;
    }
    out.why = refused.empty() ? std::string("no bone of the model is a slide, heat sink or receiver cover (by its name)")
                              : std::format("its candidates aren't usable: {}", refused);
    return out;
}

} // namespace mevr::ik::vent

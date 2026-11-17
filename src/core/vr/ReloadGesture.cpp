// SPDX-License-Identifier: MIT
#include "core/vr/ReloadGesture.hpp"

#include <cmath>

namespace mevr::vr::reload_gesture {

char const* Name(Event event) {
    switch (event) {
    case Event::None: return "none";
    case Event::Grabbed: return "grabbed";
    case Event::Inserted: return "inserted";
    case Event::Dropped: return "dropped";
    case Event::Refused: return "refused";
    case Event::Lost: return "lost";
    }
    return "?";
}

void Machine::Reset() {
    unsigned const insertions = insertions_;
    *this = {};
    insertions_ = insertions;
}

Output Machine::Update(Input const& in, Settings const& s) {
    Output out;
    bool const wasDown = gripDown_;
    gripDown_ = in.tracked && std::isfinite(in.grip) && (gripDown_ ? in.grip > s.gripOff : in.grip >= s.gripOn);
    bool const newSqueeze = gripDown_ && !wasDown && !in.squeezeClaimed;

    // Where the hand is against the gun; a step past maxStepM, or the place lost and found again, is a jump.
    bool const placeValid = in.tracked && in.gunValid && std::isfinite(in.onGun.along) && std::isfinite(in.onGun.up) &&
        std::isfinite(in.onGun.side);
    bool jump = false;
    if (placeValid) {
        jump = havePlace_ && ik::Length(ik::vent::AsVec(in.onGun) - ik::vent::AsVec(last_)) > s.maxStepM;
        last_ = in.onGun;
    }
    jump = jump || (havePlace_ != placeValid);
    havePlace_ = placeValid;
    bool const inside = placeValid && ik::vent::InZone(in.onGun, s.receiver, inReceiver_ ? s.enterMarginM + 0.02 : s.enterMarginM);
    inReceiver_ = inside;

    auto finish = [&](Event event, char const* why) {
        out.event = event;
        out.why = why;
        out.holding = phase_ != Phase::Idle;
        out.inReceiver = inside;
        out.armed = armed_;
        return out;
    };

    switch (phase_) {
    case Phase::Idle: {
        bool const atPouch = in.pouchDistanceM >= 0.0 && in.pouchDistanceM <= s.pouchRadiusM;
        if (!newSqueeze || !atPouch) {
            return finish(Event::None, nullptr);
        }
        if (!in.gunValid) {
            return finish(Event::Refused, "no gun in the right hand");
        }
        if (!in.reloadable) {
            return finish(Event::Refused, "nothing to reload now (the clip is full, no spare clip, or the game won't reload)");
        }
        phase_ = Phase::Grabbed;
        grabbedS_ = in.nowS;
        armed_ = !inside;  // a clip taken inside the receiver must leave it first
        out.down.eject = true;
        return finish(Event::Grabbed, nullptr);
    }
    case Phase::Grabbed:
    case Phase::Holding: {
        if (!in.tracked) {
            phase_ = Phase::Idle;
            armed_ = false;
            return finish(Event::Lost, "the left hand isn't tracked");
        }
        if (!gripDown_) {
            phase_ = Phase::Idle;
            armed_ = false;
            return finish(Event::Dropped, "let go before the receiver");
        }
        if (in.nowS - grabbedS_ > s.holdTimeoutS) {
            phase_ = Phase::Idle;
            armed_ = false;
            return finish(Event::Dropped, "held too long");
        }
        bool const first = phase_ == Phase::Grabbed;
        phase_ = Phase::Holding;
        out.down.eject = true;
        out.down.acquire = true;  // its edge comes the tick after the eject's (the coordinator takes one edge a tick)
        if (first) {
            return finish(Event::None, nullptr);  // the acquire's tick: no insertion yet
        }
        if (jump || !in.gunValid) {
            armed_ = false;
        } else if (!inside) {
            armed_ = true;
        } else if (armed_) {
            out.down.insert = true;
            ++insertions_;
            phase_ = Phase::Idle;
            armed_ = false;
            Output inserted = finish(Event::Inserted, nullptr);
            inserted.holding = false;
            return inserted;
        }
        return finish(Event::None, nullptr);
    }
    }
    return finish(Event::None, nullptr);
}

} // namespace mevr::vr::reload_gesture

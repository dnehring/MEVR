// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Vent.hpp"
#include "core/vr/PhysicalReloadRequest.hpp"

// The reload by hand on the sequels (research/le2-vrik.md "Holsters and the reload on the sequels: the plan", docs/trilogy-plan.md
// T26): with a gun in the right hand, the LEFT hand squeezes its grip at the clip pouch (a body slot, the left hip by default)
// to take a clip, carries it to the gun and pushes it into the receiver (core/ik/Vent's zone in the gun's frame: over and
// behind the right hand). The game counts the ammo (rule 18): this machine only qualifies the gesture and hands
// vr::reload::Coordinator its signals; the adapter (game/Reload) asks the game's own reload once the coordinator says so.
//
// Contact rules taken from Overlord's magazine-well design as invariants (research/le2-overlord-reload-reuse.md; no code
// copied): an insertion is an entry into the receiver from outside it while the clip is held; a clip taken with the hand
// already in the receiver, a jump of the hand (tracking regained, a teleport, the gun's frame lost and found again) or a
// re-grab inside it needs the hand to leave the receiver and come back; a grip let go before the receiver drops the clip.
// Pure classification, testable offline (tests/ReloadGestureTests.cpp).
namespace mevr::vr::reload_gesture {

struct Settings {
    double pouchRadiusM = 0.13;     // the clip pouch's reach (a body slot's radius)
    ik::vent::Zone receiver;        // the gun's receiver, in its frame (core/ik/Vent's defaults)
    double enterMarginM = 0.0;      // widens the receiver for the entry test
    double maxStepM = 0.20;         // a hand step larger than this in one tick (the gun's frame) is a jump
    double holdTimeoutS = 8.0;      // a clip held this long without an insertion is dropped
    float gripOn = 0.6f;            // the grip latch (as the pad mapping's)
    float gripOff = 0.4f;
};

struct Input {
    double nowS = 0.0;
    bool tracked = false;           // the left hand is tracked
    double pouchDistanceM = -1.0;   // the left hand from the pouch's centre (negative: no pouch this tick)
    bool gunValid = false;          // the gun's frame is measured (a gun in the right hand, arm IK on)
    ik::vent::OnGun onGun;          // the left hand in the gun's frame (meters), meaningful with gunValid
    float grip = 0.0f;              // the left grip, analog
    bool squeezeClaimed = false;    // another feature took this squeeze (grenades, the support grip, the vent): not ours
    bool reloadable = false;        // the game would take a reload now (the adapter's native check)
};

enum class Event {
    None,
    Grabbed,   // a clip taken from the pouch
    Inserted,  // the clip pushed into the receiver from outside it: the coordinator's insert
    Dropped,   // the grip let go (or the hold lapsed) before the receiver (`why`)
    Refused,   // a squeeze at the pouch, but no reload now (`why`)
    Lost,      // tracking or the gun went away while a clip was held (`why`)
};
char const* Name(Event event);

struct Output {
    Event event = Event::None;
    char const* why = nullptr;
    vr::reload::Signals down;       // the coordinator's levels this tick (eject from the grab, acquire from the tick after,
                                    // insert on the insertion's tick); all false once idle again
    bool holding = false;
    bool inReceiver = false;
    bool armed = false;             // the hand has been outside the receiver since the grab (or the last jump)
};

class Machine {
public:
    Output Update(Input const& in, Settings const& settings = {});
    void Reset();
    bool Holding() const { return phase_ != Phase::Idle; }
    unsigned Insertions() const { return insertions_; }

private:
    enum class Phase { Idle, Grabbed, Holding };
    Phase phase_ = Phase::Idle;
    bool gripDown_ = false;
    bool armed_ = false;
    bool inReceiver_ = false;
    bool havePlace_ = false;
    ik::vent::OnGun last_;
    double grabbedS_ = 0.0;
    unsigned insertions_ = 0;
};

} // namespace mevr::vr::reload_gesture

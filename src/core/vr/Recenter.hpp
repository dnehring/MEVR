#pragma once

#include <openxr/openxr.h>

// A recenter that also corrects a badly tilted tracking space (notes/todo-vrik.md: SteamVR once started
// with the tracked head rolled 180 degrees, so the world was upside down). Halcyon's recenter
// (xr_session.cpp DoRecenter) re-origins its LOCAL app space at the head with a yaw-only pose read from a
// never-moved base space: right when the base is level, as OpenXR's LOCAL space should be, but it can
// never undo a base that is rolled or pitched. This decides, from the head's orientation in the base
// space, whether a recenter should take the head's full orientation (pitch and roll included) instead.
//
// OpenXR conventions: +X right, +Y up, -Z forward. Pure; unit-tested in tests/Main.cpp.
namespace mevr::vr::recenter {

// How the head sits in the base space.
struct Tilt {
    float upAngleDeg = 0.0f;  // between the head's up and the base's up (+Y): 0 level, 180 upside down
    float rollDeg = 0.0f;     // the head's up about its forward, from the level up; + = right ear down.
                              // 0 when looking straight up or down (no level up to measure from)
    float pitchDeg = 0.0f;    // the forward above (+) or below (-) the base's horizon
    bool rollKnown = true;    // false when looking (nearly) straight up or down
};

Tilt Measure(XrQuaternionf const& head);

struct Settings {
    // The head's up this far (degrees) from the base's up means a tilted base. 0 or less: never correct
    // (upstream's yaw-only recenter, always).
    float tiltDeg = 60.0f;
    // Beyond the threshold but rolled less than 90 degrees, it could be the player looking down at their
    // hands or feet: the head must have looked tilted for this long (seconds, TiltWatch) first.
    float persistSeconds = 1.5f;
};

// True when the head is beyond the threshold (the up angle past tiltDeg, or rolled past 90 degrees).
bool Tilted(Tilt const& tilt, Settings const& settings);
// Rolled past 90 degrees: no player recenters with their head like that, so it's the base space.
bool UpsideDown(Tilt const& tilt);

// How long the head has looked tilted in the base space, frame by frame: grows while tilted, shrinks
// three times as fast while not, so a glance away doesn't reset it and a glance down doesn't build it.
class TiltWatch {
public:
    void Note(bool tilted, float dt);
    float Seconds() const { return seconds_; }
    void Reset() { seconds_ = 0.0f; }

private:
    float seconds_ = 0.0f;
};

enum class Kind {
    YawOnly,     // upstream: the yaw only, the base's up kept
    Corrected,   // the head's full orientation: the base's tilt removed
};

struct Decision {
    Kind kind = Kind::YawOnly;
    Tilt tilt;
    XrQuaternionf orientation{0.0f, 0.0f, 0.0f, 1.0f};  // for the new app space, in the base space
    char const* why = "";                                // one phrase for the log
};

// The orientation to re-origin the app space with. `head`: the head's orientation in the base space;
// `yawOnly`: upstream's (returned unchanged, bit for bit, unless corrected). `automatic`: Halcyon's first
// recenter when tracking starts (no key pressed): the headset may be lying on a desk, so never corrected.
// `tiltedSeconds`: TiltWatch::Seconds() for this head. Corrected: the head's full orientation, which
// assumes the player looks straight ahead and level when recentering.
Decision Decide(XrQuaternionf const& head, XrQuaternionf const& yawOnly, Settings const& settings, bool automatic,
    float tiltedSeconds);

// `app` then `local`: the orientation in the base space of something oriented `local` in a space whose
// own orientation in the base space is `app` (Hamilton product app * local).
XrQuaternionf Compose(XrQuaternionf const& app, XrQuaternionf const& local);

} // namespace mevr::vr::recenter

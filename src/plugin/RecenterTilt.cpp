// A recenter that also corrects a badly tilted base space ([MEVR] seam in Halcyon's DoRecenter, rules in
// core/vr/Recenter). Present thread throughout: DoRecenter and OverlayLayers both run in Halcyon's frame.

#include "plugin/RecenterTilt.hpp"

#include "halcyon/le1/logger.h"
#include "halcyon/le1/mevr_bridge.h"
#include "halcyon/le1/vr_config.h"

#include "core/Log.hpp"
#include "core/vr/Recenter.hpp"

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace mevr::host::recenter_tilt {
namespace {

namespace rc = vr::recenter;

// The app space's orientation in the base space: identity until the first recenter, then what the last
// one used (committed once Halcyon confirms the new space was made).
XrQuaternionf gAppInBase{0.0f, 0.0f, 0.0f, 1.0f};
XrQuaternionf gPending{0.0f, 0.0f, 0.0f, 1.0f};
bool gHavePending = false;
rc::TiltWatch gWatch;        // the head tilted in the base space (what a recenter decides on)
rc::TiltWatch gUpsideInApp;  // the head upside down in the app space (what the player sees: the hint)
rc::Tilt gLastTilt;
bool gLastValid = false;
bool gToldUpsideDown = false;
std::uint64_t gLastNoteMs = 0;
int gCorrected = 0;
int gRecenters = 0;

void LogBoth(std::string const& line) {
    log::Info("{}", line);
    MELEVR::Logger::LogLine(line);
}

rc::Settings SettingsNow() {
    rc::Settings settings;
    settings.tiltDeg = MELEVR::Config::Get().recenterTiltDeg;
    return settings;
}

} // namespace

void NoteHead(float const headOrientation[4], bool headValid) {
    std::uint64_t const now = GetTickCount64();
    float const dt = gLastNoteMs == 0 ? 0.0f : static_cast<float>(now - gLastNoteMs) / 1000.0f;
    gLastNoteMs = now;
    if (!headValid) {
        return;
    }
    XrQuaternionf const inApp{headOrientation[0], headOrientation[1], headOrientation[2], headOrientation[3]};
    rc::Tilt const tilt = rc::Measure(rc::Compose(gAppInBase, inApp));
    rc::Settings const settings = SettingsNow();
    gWatch.Note(rc::Tilted(tilt, settings), dt);
    gLastTilt = tilt;
    gLastValid = true;
    // A hint once per stretch: the world looks upside down to the player (the head is upside down in the
    // app space), which is what the corrected recenter is for.
    rc::Tilt const seen = rc::Measure(inApp);
    gUpsideInApp.Note(rc::UpsideDown(seen), dt);
    if (gUpsideInApp.Seconds() >= 5.0f && settings.tiltDeg > 0.0f && !gToldUpsideDown) {
        gToldUpsideDown = true;
        char line[360];
        std::snprintf(line, sizeof line,
            "[MEVR-RECENTER] the world has looked upside down for 5 s (the head is rolled %.0f deg in the app space; up %.0f deg "
            "from the base space's up): the runtime's space is probably tilted. Look straight ahead and recenter (R, double-click "
            "the right stick, or the menu) to correct it",
            seen.rollDeg, tilt.upAngleDeg);
        LogBoth(line);
    } else if (gUpsideInApp.Seconds() == 0.0f) {
        gToldUpsideDown = false;
    }
}

void Committed() {
    if (gHavePending) {
        gAppInBase = gPending;
        gHavePending = false;
    }
}

std::string StatusJson() {
    char text[256];
    std::snprintf(text, sizeof text,
        "{\"recenters\": %d, \"corrected\": %d, \"tiltDeg\": %.1f, \"headUpAngleDeg\": %.1f, \"headRollDeg\": %.1f, "
        "\"headPitchDeg\": %.1f, \"tiltedSeconds\": %.2f}",
        gRecenters, gCorrected, static_cast<double>(SettingsNow().tiltDeg), gLastValid ? gLastTilt.upAngleDeg : 0.0,
        gLastValid ? gLastTilt.rollDeg : 0.0, gLastValid ? gLastTilt.pitchDeg : 0.0, static_cast<double>(gWatch.Seconds()));
    return text;
}

} // namespace mevr::host::recenter_tilt

namespace MELEVR::MevrBridge {

void RecenterOrientation(float const head[4], bool automatic, float orientation[4]) noexcept {
    using namespace mevr::host::recenter_tilt;
    namespace rc = mevr::vr::recenter;
    XrQuaternionf const h{head[0], head[1], head[2], head[3]};
    XrQuaternionf const yawOnly{orientation[0], orientation[1], orientation[2], orientation[3]};
    rc::Settings const settings = SettingsNow();
    rc::Decision const d = rc::Decide(h, yawOnly, settings, automatic, gWatch.Seconds());
    ++gRecenters;
    char line[512];
    if (d.kind == rc::Kind::Corrected) {
        ++gCorrected;
        orientation[0] = d.orientation.x;
        orientation[1] = d.orientation.y;
        orientation[2] = d.orientation.z;
        orientation[3] = d.orientation.w;
        std::snprintf(line, sizeof line,
            "[MEVR-RECENTER] tilt-corrected recenter: %s (up %.0f deg from the base space's up, roll %.0f deg, pitch %.0f deg, "
            "tilted for %.1f s; threshold %.0f deg): the app space takes the head's full orientation, so straight ahead and "
            "level is how the head is held now",
            d.why, d.tilt.upAngleDeg, d.tilt.rollKnown ? d.tilt.rollDeg : 0.0f, d.tilt.pitchDeg, gWatch.Seconds(), settings.tiltDeg);
    } else {
        std::snprintf(line, sizeof line,
            "[MEVR-RECENTER] yaw-only recenter (upstream)%s: %s (head up %.0f deg from the base space's up, roll %.0f deg, "
            "pitch %.0f deg)",
            automatic ? ", automatic" : "", d.why, d.tilt.upAngleDeg, d.tilt.rollKnown ? d.tilt.rollDeg : 0.0f, d.tilt.pitchDeg);
    }
    LogBoth(line);
    gPending = d.kind == rc::Kind::Corrected ? d.orientation : yawOnly;
    gHavePending = true;
}

} // namespace MELEVR::MevrBridge

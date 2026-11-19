// SPDX-License-Identifier: MIT
// Exercise the actual adapter. Only external samples/settings are stubbed;
// XrToUnreal.cpp and Rigid.cpp provide the production coordinate mathematics.
#include "game/HandTargets.cpp"
#include "core/vr/HostView.hpp"

#include <cmath>
#include <cstdio>

namespace {
namespace ht = mevr::game::hand_targets;
namespace engine = mevr::engine;
int failures = 0;
std::optional<engine::TrackingFrame> sampleAnchor;
mevr::vr::controllers::State sampleControllers;
mevr::vr::host_view::State sampleHost;
bool heldAnchor = false;
double trackingScale = 0.0;

void Check(bool pass, int line) {
    if (!pass) { ++failures; std::printf("HandTargetsTests:%d failed\n", line); }
}
#define CHECK(value) Check((value), __LINE__)
bool Near(double a, double b) { return std::abs(a - b) < 0.0001; }
bool Same(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
}
void NoHands(ht::Targets const& targets) {
    for (auto const& hand : targets.hands) {
        CHECK(!hand.valid && !hand.hasRotation && !hand.hasAim);
        CHECK(Same(hand.position, {}));
    }
}
void Reset() {
    ht::SetDesktopTest(false);
    ht::ResetTracking();
    sampleAnchor = engine::TrackingFrame{};
    sampleAnchor->head = {{0, 0, 0, 1}, {0, 1.75f, 0}};
    sampleAnchor->headWorld = {100, 200, 300};
    sampleAnchor->unitsPerMeter = 50;
    sampleControllers = {};
    sampleHost = {};
    sampleHost.valid = true;
    sampleHost.head = sampleAnchor->head;
    heldAnchor = false;
    trackingScale = 0;
}
engine::UnrealVector Animated(double separation = 0) {
    auto at = sampleAnchor->headWorld;
    at.x -= separation;
    return at;
}
void TrackedHands() {
    sampleControllers.active = true;
    sampleControllers.left.gripValid = sampleControllers.right.gripValid = true;
    sampleControllers.left.aimValid = sampleControllers.right.aimValid = true;
    sampleControllers.left.grip = {{0, 0, 0, 1}, {-0.2f, 1.4f, -0.4f}};
    sampleControllers.right.grip = {{0, 0, 0, 1}, {0.2f, 1.4f, -0.4f}};
    sampleControllers.left.aim = sampleControllers.left.grip;
    sampleControllers.right.aim = sampleControllers.right.grip;
    sampleControllers.left.trigger = sampleControllers.right.trigger = 1;
    sampleControllers.left.squeeze = sampleControllers.right.squeeze = 1;
}
void HeadOnly(ht::Targets const& targets) {
    CHECK(targets.source == ht::Source::HeadOnly);
    CHECK(targets.anchor == ht::Anchor::View);
    CHECK(Same(targets.headWorld, sampleAnchor->headWorld));
    CHECK(Same(engine::ToWorld(targets.body, targets.head.position), sampleAnchor->headWorld));
    NoHands(targets);
}
} // namespace

namespace mevr::game::view_anchor {
std::optional<engine::TrackingFrame> Current() { return sampleAnchor; }
bool Held() { return heldAnchor; }
}
namespace mevr::game::hand_calibration {
double TrackingScale() { return trackingScale; }
}
namespace mevr::vr::controllers {
State Latest(std::uint64_t) { return sampleControllers; }
}
namespace mevr::vr::host_view {
// Latest's production expiry contract is represented by valid=false below.
// These tests exercise the adapter, not a second implementation of the clock.
State Latest(std::uint64_t) { return sampleHost; }
}

int main() {
    Reset();
    CHECK(ht::Current(Animated()).source == ht::Source::None); // opt-in remains required
    HeadOnly(ht::Current(Animated(), false, true));

    // Controller expiry retains pose/validity/button fields in Latest(). None
    // of those retained hand targets may leak into the presentation-only source.
    TrackedHands();
    sampleControllers.active = false;
    auto targets = ht::Current(Animated(), false, true);
    HeadOnly(targets);
    CHECK(ht::Current(Animated(), false, false).source == ht::Source::None);

    // This is a fresh pose each tick, not a cached menu body. Both the headset's
    // tracking-space position/orientation and its mapped world centre move.
    sampleAnchor->head = {{0, 0.25881904f, 0, 0.9659258f}, {0.2f, 1.85f, -0.3f}};
    sampleAnchor->headWorld = {125, 210, 305};
    sampleAnchor->rotation = engine::ToBasis(engine::UnrealRotation{0, 16384, 0});
    sampleHost.head = sampleAnchor->head;
    targets = ht::Current(Animated(), false, true);
    HeadOnly(targets);
    CHECK(Near(targets.head.position.x, 0.2) && Near(targets.head.position.y, 1.85));
    CHECK(Near(targets.head.orientation.y, 0.25881904));
    CHECK(Near(targets.body.rotation.forward.y, 1));
    CHECK(Near(targets.body.unitsPerMeter, 50));
    trackingScale = 80;
    targets = ht::Current(Animated(), false, true);
    HeadOnly(targets);
    CHECK(Near(targets.body.unitsPerMeter, 80));

    Reset();
    sampleControllers.active = true; // focused input without any valid grip
    HeadOnly(ht::Current(Animated(), false, true));

    // Reacquisition uses the actual production wrist mapping, including the
    // seven-unit wrist setback. A single valid hand must remain usable.
    TrackedHands();
    targets = ht::Current(Animated(), false, true);
    CHECK(targets.source == ht::Source::Controllers && targets.anchor == ht::Anchor::View);
    CHECK(targets.hands[0].valid && targets.hands[1].valid);
    CHECK(targets.hands[0].hasRotation && targets.hands[1].hasAim);
    CHECK(Same(targets.hands[0].position, {113, 190, 282.5}));
    CHECK(Same(targets.hands[1].position, {113, 210, 282.5}));
    sampleControllers.left.gripValid = false;
    targets = ht::Current(Animated(), false, true);
    CHECK(targets.source == ht::Source::Controllers && !targets.hands[0].valid && targets.hands[1].valid);
    CHECK(!targets.hands[0].hasAim && !targets.hands[0].hasRotation);
    sampleControllers.left.gripValid = true;
    sampleControllers.right.gripValid = false;
    targets = ht::Current(Animated(), false, true);
    CHECK(targets.source == ht::Source::Controllers && targets.hands[0].valid && !targets.hands[1].valid);
    CHECK(!targets.hands[1].hasAim && !targets.hands[1].hasRotation);
    sampleControllers.left.gripValid = false;
    HeadOnly(ht::Current(Animated(), false, true));

    Reset();
    heldAnchor = true;
    CHECK(ht::Current(Animated(), false, true).source == ht::Source::None);
    heldAnchor = false;
    sampleHost.valid = false; // stale/lost head, even if a caller retains an anchor
    CHECK(ht::Current(Animated(), false, true).source == ht::Source::None);
    sampleHost.valid = true;
    sampleAnchor.reset();
    CHECK(ht::Current(engine::UnrealVector{}, false, true).source == ht::Source::None);

    Reset();
    CHECK(ht::Current(Animated(100), false, true).source == ht::Source::None);
    CHECK(ht::Current(std::nullopt, false, true).source == ht::Source::None);
    HeadOnly(ht::Current(Animated(40), false, true));
    HeadOnly(ht::Current(Animated(50), false, true)); // legitimate first-person hysteresis
    ht::ResetTracking();
    CHECK(ht::Current(Animated(50), false, true).source == ht::Source::None);
    HeadOnly(ht::Current(Animated(40), false, true));
    CHECK(ht::Current(Animated(80), false, true).source == ht::Source::None);

    // The game's crouch under a floor-locked view (research/29 "The crouch that went third person"): his animated head 68
    // units under the view and 35 ahead of it (76 in all). The whole distance leaves first person; with the view's height
    // the player's own only the 35 across counts, and a third-person camera 110 units away across still leaves.
    Reset();
    TrackedHands();
    auto crouched = Animated(35);
    crouched.z -= 68;
    CHECK(ht::Current(Animated(30)).anchor == ht::Anchor::View);
    targets = ht::Current(crouched);
    CHECK(targets.anchor == ht::Anchor::HeadBone && !targets.viewHeightIsPlayers && targets.anchorGap > 75);
    ht::ResetTracking();
    CHECK(ht::Current(Animated(30), false, false, true).anchor == ht::Anchor::View);
    targets = ht::Current(crouched, false, false, true);
    CHECK(targets.anchor == ht::Anchor::View && targets.viewHeightIsPlayers && Near(targets.anchorGap, 35));
    CHECK(Near(targets.viewToHeadBone, std::hypot(35.0, 68.0)));
    CHECK(ht::Current(Animated(110), false, false, true).anchor == ht::Anchor::HeadBone);

    // The explicit desktop-test source is unchanged and never borrows stale VR hands.
    Reset();
    sampleAnchor.reset();
    sampleHost.valid = false;
    ht::SetDesktopTest(true);
    targets = ht::Current(std::nullopt);
    CHECK(targets.source == ht::Source::DesktopTest);
    NoHands(targets);
    ht::SetDesktopTest(false);
    CHECK(ht::Current(std::nullopt).source == ht::Source::None);
    std::printf("HandTargets: %s (fresh head-only frames, stale input, reacquisition, gates, hysteresis)\n",
        failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

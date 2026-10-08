// Offline tests for the math that has to be right before it reaches a headset. No game, no
// runtime: `ctest -C Release --test-dir build`.

#include "core/vr/ViewSync.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/GamepadMapping.hpp"
#include "game/HeadSim.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

// Unreal units for a number of degrees (65536 = 360).
int Units(double degrees) {
    return static_cast<int>(std::lround(degrees * 65536.0 / 360.0));
}

bool Near(int actual, int expected, int tolerance = 2) {
    return std::abs(actual - expected) <= tolerance;
}

XrQuaternionf AxisAngle(double x, double y, double z, double degrees) {
    double const half = degrees * std::numbers::pi / 360.0;
    double const s = std::sin(half);
    return {static_cast<float>(x * s), static_cast<float>(y * s), static_cast<float>(z * s),
        static_cast<float>(std::cos(half))};
}

void TestXrToUnreal() {
    using mevr::engine::ToUnrealRotation;
    auto const identity = ToUnrealRotation({0, 0, 0, 1});
    CHECK(identity.pitch == 0 && identity.yaw == 0 && identity.roll == 0);

    // OpenXR yaw is about +Y with positive = turn left; UE3 yaw positive = turn right.
    auto const left = ToUnrealRotation(AxisAngle(0, 1, 0, 90));
    CHECK(Near(left.yaw, -Units(90)) && Near(left.pitch, 0) && Near(left.roll, 0));

    // Pitch is about +X with positive = look up, same sign as UE3.
    auto const up = ToUnrealRotation(AxisAngle(1, 0, 0, 30));
    CHECK(Near(up.pitch, Units(30)) && Near(up.yaw, 0) && Near(up.roll, 0));

    // Roll about the viewing axis (-Z): positive OpenXR angle about +Z tilts the head left
    // (right ear up); UE3 roll positive tilts the right ear down.
    auto const tilt = ToUnrealRotation(AxisAngle(0, 0, 1, 20));
    CHECK(Near(tilt.roll, -Units(20)) && Near(tilt.pitch, 0) && Near(tilt.yaw, 0));
}

void TestHeadSim() {
    using namespace mevr::game::head_sim;
    using mevr::engine::ToUnrealRotation;

    // Every pattern starts looking straight ahead, and stays a unit quaternion.
    for (Pattern pattern : {Pattern::Off, Pattern::Sweep, Pattern::Nod}) {
        auto const start = ToUnrealRotation(PoseAt(pattern, 0.0).orientation);
        CHECK(start.pitch == 0 && start.yaw == 0 && start.roll == 0);
        for (double t = 0.0; t < 10.0; t += 0.37) {
            XrQuaternionf const q = PoseAt(pattern, t).orientation;
            double const norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            CHECK(std::abs(norm - 1.0) < 1e-5);
        }
    }

    // Sweep at t = 2 s: yaw 25 degrees left (sin peak of the 8 s period), pitch back at 0.
    auto const sweep = ToUnrealRotation(PoseAt(Pattern::Sweep, 2.0).orientation);
    CHECK(Near(sweep.yaw, -Units(25)) && Near(sweep.pitch, 0) && Near(sweep.roll, 0));

    // Sweep at t = 1 s: yaw 25 sin(pi/4) left and pitch 12 up together, still no roll. This is
    // the yaw-then-pitch order the UE3 rotator uses, so the decomposition must recover both.
    auto const both = ToUnrealRotation(PoseAt(Pattern::Sweep, 1.0).orientation);
    CHECK(Near(both.yaw, -Units(25.0 * std::sin(std::numbers::pi / 4.0))));
    CHECK(Near(both.pitch, Units(12)));
    CHECK(Near(both.roll, 0));

    // Nod: pitch only, peaking at 20 degrees up at a quarter of its 3 s period.
    auto const nod = ToUnrealRotation(PoseAt(Pattern::Nod, 0.75).orientation);
    CHECK(Near(nod.pitch, Units(20)) && Near(nod.yaw, 0) && Near(nod.roll, 0));
}

bool NearD(double actual, double expected, double tolerance = 1e-3) {
    return std::abs(actual - expected) <= tolerance;
}

void TestOffsets() {
    using namespace mevr::engine;
    // 1 m forward in LOCAL (OpenXR -Z) is 100 units along the body's facing.
    auto const ahead = ToUnrealOffset({0.0f, 0.0f, -1.0f}, 0);
    CHECK(NearD(ahead.x, 100.0) && NearD(ahead.y, 0.0) && NearD(ahead.z, 0.0));
    auto const aheadTurned = ToUnrealOffset({0.0f, 0.0f, -1.0f}, Units(90));  // body faces +Y
    CHECK(NearD(aheadTurned.x, 0.0, 0.01) && NearD(aheadTurned.y, 100.0, 0.01));
    auto const right = ToUnrealOffset({1.0f, 0.0f, 0.0f}, 0);
    CHECK(NearD(right.x, 0.0) && NearD(right.y, 100.0));
    auto const up = ToUnrealOffset({0.0f, 1.0f, 0.0f}, Units(45));
    CHECK(NearD(up.z, 100.0) && NearD(up.x, 0.0) && NearD(up.y, 0.0));

    // The view's right axis: +Y facing +X; -X after turning right 90; down after rolling right.
    auto const r0 = RightAxis({0, 0, 0});
    CHECK(NearD(r0.x, 0.0) && NearD(r0.y, 1.0) && NearD(r0.z, 0.0));
    auto const r90 = RightAxis({0, Units(90), 0});
    CHECK(NearD(r90.x, -1.0, 1e-4) && NearD(r90.y, 0.0, 1e-4));
    auto const rolled = RightAxis({0, 0, Units(90)});
    CHECK(NearD(rolled.z, -1.0, 1e-4) && NearD(rolled.y, 0.0, 1e-4));

    // Eyes sit along the head's own right axis.
    XrPosef head{};
    head.orientation = {0, 0, 0, 1};
    head.position = {0.0f, 1.6f, 0.0f};
    auto const rightEye = OffsetRight(head, 0.032f);
    CHECK(NearD(rightEye.position.x, 0.032) && NearD(rightEye.position.y, 1.6) && NearD(rightEye.position.z, 0.0));
    head.orientation = AxisAngle(0, 1, 0, 90);  // turned left: the right side now faces -Z
    auto const turned = OffsetRight(head, 0.032f);
    CHECK(NearD(turned.position.x, 0.0, 1e-5) && NearD(turned.position.z, -0.032, 1e-5));
}

void TestGamepad() {
    using namespace mevr::game::gamepad;
    mevr::vr::controllers::State state;
    Latches latches;
    state.left.stickY = 1.0f;
    state.right.stickX = -1.0f;
    state.right.trigger = 1.0f;
    state.left.squeeze = 0.7f;
    auto pad = FromControllers(state, latches);
    CHECK(pad.sThumbLY == 32767 && pad.sThumbLX == 0);
    CHECK(pad.sThumbRX == -32767);
    CHECK(pad.bRightTrigger == 255 && pad.bLeftTrigger == 0);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);

    // Hysteresis: a half-squeezed grip stays pressed, a released one lets go.
    state.left.squeeze = 0.5f;
    CHECK((FromControllers(state, latches).wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
    state.left.squeeze = 0.3f;
    CHECK((FromControllers(state, latches).wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0);
    state.left.squeeze = 0.5f;  // not enough to press again
    CHECK((FromControllers(state, latches).wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0);

    // Buttons, and menu as Start, or Back with the left grip held.
    state = {};
    latches = {};
    state.right.primary = state.right.secondary = state.left.primary = state.left.secondary = true;
    state.left.menu = true;
    pad = FromControllers(state, latches);
    WORD const faces = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y;
    CHECK((pad.wButtons & faces) == faces);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_START) != 0 && (pad.wButtons & XINPUT_GAMEPAD_BACK) == 0);
    state.left.squeeze = 1.0f;
    pad = FromControllers(state, latches);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_BACK) != 0 && (pad.wButtons & XINPUT_GAMEPAD_START) == 0);

    // Merge: buttons combine; each stick and trigger comes from the pad pushed further.
    XINPUT_GAMEPAD a{};
    XINPUT_GAMEPAD b{};
    a.wButtons = XINPUT_GAMEPAD_A;
    b.wButtons = XINPUT_GAMEPAD_B;
    a.sThumbLX = 1000;
    b.sThumbLY = -20000;
    a.bLeftTrigger = 10;
    b.bLeftTrigger = 200;
    auto const merged = Merge(a, b);
    CHECK(merged.wButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B));
    CHECK(merged.sThumbLX == 0 && merged.sThumbLY == -20000);
    CHECK(merged.bLeftTrigger == 200);

    // Looking 90 degrees right, "forward" becomes the game camera's right.
    SHORT x = 0;
    SHORT y = 32767;
    RotateStick(x, y, Units(90));
    CHECK(Near(x, 32767) && Near(y, 0));
    x = 0;
    y = 32767;
    RotateStick(x, y, -Units(90));
    CHECK(Near(x, -32767) && Near(y, 0));
    x = 0;
    y = 0;
    RotateStick(x, y, Units(45));
    CHECK(x == 0 && y == 0);
    x = 0;
    y = 32767;
    RotateStick(x, y, Units(180));
    CHECK(Near(x, 0) && Near(y, -32767));
}

// A view whose FOV identifies it in the checks below.
mevr::vr::view_sync::RenderedView View(float id, int eye = -1) {
    XrPosef pose{};
    pose.orientation.w = 1.0f;
    return {pose, id, eye};
}

float PeekId(std::size_t& depth) {
    auto const view = mevr::vr::view_sync::PeekForPresent(depth);
    return view ? view->fovXDegrees : -1.0f;
}

// Image/view pairing between the game thread (push, wait) and the render thread (peek at
// Present, then consume). Each case starts from Reset.
void TestViewSync() {
    namespace vs = mevr::vr::view_sync;
    std::size_t depth = 0;

    // Steady lockstep: one view per frame, consumed by that frame's Present.
    vs::Reset();
    vs::PushRendered(View(1));
    CHECK(vs::HasPendingFrame());
    CHECK(PeekId(depth) == 1 && depth == 1);
    vs::NotifyPresented();
    CHECK(!vs::HasPendingFrame());
    CHECK(vs::WaitForPresent(0));

    // The game ran a frame ahead (after a timeout): each image still gets its own view.
    vs::Reset();
    vs::PushRendered(View(1));
    vs::PushRendered(View(2));
    CHECK(PeekId(depth) == 1 && depth == 2);
    vs::NotifyPresented();
    CHECK(vs::HasPendingFrame());
    CHECK(!vs::WaitForPresent(0));  // frame 2 not presented yet
    CHECK(PeekId(depth) == 2 && depth == 1);
    vs::NotifyPresented();
    CHECK(!vs::HasPendingFrame());

    // Never more than the engine's one-frame lead: older views are stale.
    vs::Reset();
    vs::PushRendered(View(1));
    vs::PushRendered(View(2));
    vs::PushRendered(View(3));
    CHECK(PeekId(depth) == 2 && depth == 2);

    // A frame that pushed but never presented: drift recovery drops its view.
    vs::Reset();
    vs::PushRendered(View(1));  // never presented
    vs::PushRendered(View(2));
    vs::DropOldest();
    CHECK(PeekId(depth) == 2 && depth == 1);

    // A second view in the same frame replaces the first; still one entry to consume.
    vs::Reset();
    vs::PushRendered(View(1, 0));
    vs::ReplaceNewest(View(5, 1));
    auto const replaced = vs::PeekForPresent(depth);
    CHECK(replaced && replaced->fovXDegrees == 5 && replaced->eye == 1 && depth == 1);
    vs::NotifyPresented();
    CHECK(!vs::HasPendingFrame());

    // Frames without a player view use the fallback; it's never waited on and a real view
    // replaces it.
    vs::Reset();
    vs::PublishFallback(View(9));
    CHECK(!vs::HasPendingFrame());
    CHECK(PeekId(depth) == -1 && depth == 0);
    CHECK(vs::Fallback() && vs::Fallback()->fovXDegrees == 9);
    vs::PushRendered(View(1));
    CHECK(!vs::Fallback());

    // Presents with nothing queued (VR off, loading) release any waiter and are counted.
    vs::Reset();
    auto const before = vs::PresentCount();
    vs::NotifyPresented();
    CHECK(vs::PresentCount() == before + 1);
    CHECK(!vs::HasPendingFrame());
    vs::Reset();
}

} // namespace

int main() {
    TestViewSync();
    TestXrToUnreal();
    TestHeadSim();
    TestOffsets();
    TestGamepad();
    if (gFailures == 0) {
        std::printf("all tests passed\n");
    }
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

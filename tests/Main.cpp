// Offline tests for the math that has to be right before it reaches a headset. No game, no
// runtime: `ctest -C Release --test-dir build`.

#include "core/ik/ArmLength.hpp"
#include "core/ik/ArmPose.hpp"
#include "core/ik/BoneNames.hpp"
#include "core/ik/HandFrame.hpp"
#include "core/ik/Ik.hpp"
#include "core/ik/Rigid.hpp"
#include "core/ik/TorsoPose.hpp"
#include "core/ik/TouchReference.hpp"
#include "core/vr/MenuPointer.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "core/vr/ViewSync.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/GamepadMapping.hpp"
#include "game/HeadSim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

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

    // A controller 30 cm right of and 40 cm below the head, for a body facing world +Y.
    auto const hand = ToWorld({0.3f, 1.2f, 0.0f}, {0.0f, 1.6f, 0.0f}, {1000.0, 2000.0, 300.0}, Units(90));
    CHECK(NearD(hand.x, 1000.0 - 30.0, 0.01) && NearD(hand.y, 2000.0, 0.01) && NearD(hand.z, 260.0, 0.01));

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

XrQuaternionf Multiply(XrQuaternionf const& a, XrQuaternionf const& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// Yaw (+ right) then pitch (+ up) in degrees, as an OpenXR orientation (yaw about +Y is + left).
XrQuaternionf YawPitch(double yawRight, double pitchUp) {
    return Multiply(AxisAngle(0, 1, 0, -yawRight), AxisAngle(1, 0, 0, pitchUp));
}

bool NearV(mevr::engine::UnrealVector const& a, mevr::engine::UnrealVector const& b, double tolerance = 1e-4) {
    return NearD(a.x, b.x, tolerance) && NearD(a.y, b.y, tolerance) && NearD(a.z, b.z, tolerance);
}

bool NearB(mevr::engine::Basis const& a, mevr::engine::Basis const& b, double tolerance = 1e-4) {
    return NearV(a.forward, b.forward, tolerance) && NearV(a.right, b.right, tolerance) && NearV(a.up, b.up, tolerance);
}

void TestTrackingFrame() {
    using namespace mevr::engine;
    double const s30 = 0.5, c30 = std::sqrt(3.0) / 2.0;

    // Rotator axes: yaw turns right (+Y), pitch looks up (+Z).
    CHECK(NearV(ToBasis(UnrealRotation{0, Units(90), 0}).forward, {0.0, 1.0, 0.0}));
    Basis const pitched = ToBasis(UnrealRotation{Units(30), 0, 0});
    CHECK(NearV(pitched.forward, {c30, 0.0, s30}) && NearV(pitched.up, {-s30, 0.0, c30}));

    // An OpenXR orientation's axes are the axes of the rotator it converts to, roll included.
    for (XrQuaternionf const q : {YawPitch(40, 15), YawPitch(-120, -50), Multiply(YawPitch(10, 20), AxisAngle(0, 0, 1, 25))}) {
        CHECK(NearB(ToBasis(q), ToBasis(ToUnrealRotation(q)), 2e-4));
    }

    // Inverse undoes, Compose chains, ToRotation reads a direction back.
    Basis const any = ToBasis(UnrealRotation{Units(-20), Units(130), Units(15)});
    CHECK(NearB(Compose(any, Inverse(any)), Basis{}));
    auto const back = ToRotation(ToBasis(UnrealRotation{Units(-20), Units(130), 0}).forward);
    CHECK(Near(back.pitch, Units(-20)) && Near(back.yaw, Units(130)) && back.roll == 0);
    CHECK(ToRotation({0.0, -2.0, 0.0}).yaw == -Units(90));

    // Unwind keeps a rotator's winding next to the game's own.
    CHECK(Unwind(100, 65536 * 3 + 50) == 65536 * 3 + 100);
    CHECK(Unwind(-32000, 32000) == 33536);
    CHECK(Unwind(Units(10), -65536 + Units(5)) == -65536 + Units(10));

    // Our renderer (body yaw): the same mapping as ToWorld / the old hand aim (yaw + aim yaw).
    XrPosef head{};
    head.orientation = {0, 0, 0, 1};
    head.position = {0.0f, 1.6f, 0.0f};
    TrackingFrame const body = FromBodyYaw({1000.0, 2000.0, 300.0}, Units(90), head);
    CHECK(NearV(ToWorld(body, {0.3f, 1.2f, 0.0f}), ToWorld({0.3f, 1.2f, 0.0f}, head.position, {1000.0, 2000.0, 300.0}, Units(90)), 0.01));
    auto const aimBody = ToRotation(OrientationToWorld(body, YawPitch(30, 10)).forward);
    CHECK(Near(aimBody.yaw, Units(120), 3) && Near(aimBody.pitch, Units(10), 3));

    // Halcyon's view: the game camera turned by head look. With look equal to the head's yaw and
    // pitch, tracking space is the camera itself, and a controller pointing where the head looks
    // points along the rendered view.
    UnrealRotation const camera{Units(-10), Units(70), 0};
    XrPosef gaze{};
    gaze.orientation = YawPitch(40, 15);
    gaze.position = {0.1f, 1.5f, -0.2f};
    UnrealRotation const look{Units(15), Units(40), 0};
    TrackingFrame const view = FromCameraView({100.0, 200.0, 300.0}, camera, look, {0.0, 0.0, 0.0}, gaze, 50.0);
    CHECK(NearB(view.rotation, ToBasis(camera), 2e-4));
    CHECK(NearV(OrientationToWorld(view, gaze.orientation).forward, Compose(ToBasis(camera), ToBasis(look)).forward, 2e-4));
    // The head itself sits at the camera; 0.5 m to its right in tracking space is along the camera's right.
    CHECK(NearV(ToWorld(view, gaze.position), {100.0, 200.0, 300.0}));
    UnrealVector const cameraRight = ToBasis(camera).right;
    CHECK(NearV(ToWorld(view, {0.6f, 1.5f, -0.2f}),
        {100.0 + 25.0 * cameraRight.x, 200.0 + 25.0 * cameraRight.y, 300.0 + 25.0 * cameraRight.z}, 0.01));

    // Head-locked view (no look): whatever the head does, the view's center is the camera's
    // forward, so a controller aimed with the head points along the camera.
    TrackingFrame const locked = FromCameraView({0.0, 0.0, 0.0}, camera, {}, {0.0, 0.0, 0.0}, gaze, 50.0);
    CHECK(NearV(OrientationToWorld(locked, gaze.orientation).forward, ToBasis(camera).forward, 2e-4));

    // The view offset moves the head along the camera's own axes (forward, right, up).
    TrackingFrame const moved = FromCameraView({100.0, 200.0, 300.0}, UnrealRotation{0, Units(90), 0}, {},
        {0.0, 10.0, 5.0}, head, 50.0);
    CHECK(NearV(moved.headWorld, {90.0, 200.0, 305.0}));
}

bool NearX(XrVector3f const& a, XrVector3f const& b, double tolerance = 1e-4) {
    return NearD(a.x, b.x, tolerance) && NearD(a.y, b.y, tolerance) && NearD(a.z, b.z, tolerance);
}

// The aim ray (core/vr/AimRay) goes from the world back into tracking space: ToTracking and
// DirectionToTracking undo ToWorld and OrientationToWorld, for every kind of frame.
void TestToTracking() {
    using namespace mevr::engine;
    XrPosef gaze{};
    gaze.orientation = YawPitch(40, 15);
    gaze.position = {0.1f, 1.5f, -0.2f};
    TrackingFrame const view = FromCameraView({100.0, 200.0, 300.0}, UnrealRotation{Units(-10), Units(70), Units(5)},
        UnrealRotation{Units(15), Units(40), 0}, {3.0, -2.0, 1.0}, gaze, 50.0);
    TrackingFrame const body = FromBodyYaw({1000.0, 2000.0, 300.0}, Units(-135), gaze);
    TrackingFrame const origin = AtOrigin(view, {-50.0, 20.0, 160.0}, kUnitsPerMeter);
    XrVector3f const points[] = {{0.0f, 0.0f, 0.0f}, {0.3f, 1.2f, -0.4f}, {-0.6f, 0.9f, 0.5f}, gaze.position};
    XrQuaternionf const orientations[] = {{0, 0, 0, 1}, YawPitch(-70, 30), Multiply(YawPitch(10, -20), AxisAngle(0, 0, 1, 35))};
    for (TrackingFrame const& frame : {view, body, origin}) {
        for (XrVector3f const& p : points) {
            CHECK(NearX(ToTracking(frame, ToWorld(frame, p)), p, 1e-4));
        }
        // A world point back and forth.
        UnrealVector const world{-321.0, 45.5, 99.0};
        CHECK(NearV(ToWorld(frame, ToTracking(frame, world)), world, 0.01));
        for (XrQuaternionf const& q : orientations) {
            // An aim pose's forward (-Z), right (+X) and up (+Y) come back as themselves.
            Basis const axes = OrientationToWorld(frame, q);
            mevr::ik::Quat const iq{q.x, q.y, q.z, q.w};
            auto const along = [&](mevr::ik::Vec3 v) {
                mevr::ik::Vec3 const r = mevr::ik::Rotate(iq, v);
                return XrVector3f{static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.z)};
            };
            CHECK(NearX(DirectionToTracking(frame, axes.forward), along({0.0, 0.0, -1.0})));
            CHECK(NearX(DirectionToTracking(frame, axes.right), along({1.0, 0.0, 0.0})));
            CHECK(NearX(DirectionToTracking(frame, axes.up), along({0.0, 1.0, 0.0})));
        }
    }
    // Directions come back unit length, whatever their length in the world; zero stays zero.
    XrVector3f const d = DirectionToTracking(view, {30.0, -40.0, 0.0});
    CHECK(NearD(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), 1.0, 1e-6));
    CHECK(NearX(DirectionToTracking(view, {0.0, 0.0, 0.0}), {0.0f, 0.0f, 0.0f}));
    // Concretely: our renderer facing world +Y, a point 50 units along +Y and 20 up from the head
    // is half a meter ahead (-Z) and 0.2 m up in tracking space.
    XrPosef level{};
    level.orientation = {0, 0, 0, 1};
    TrackingFrame const facingY = FromBodyYaw({0.0, 0.0, 0.0}, Units(90), level);
    CHECK(NearX(ToTracking(facingY, {0.0, 50.0, 20.0}), {0.0f, 0.2f, -0.5f}, 1e-4));
    // No scale: everything maps to the head.
    TrackingFrame flat = facingY;
    flat.unitsPerMeter = 0.0;
    CHECK(NearX(ToTracking(flat, {10.0, 20.0, 30.0}), level.position));
}

// Hand targets (game/HandTargets): controllers measured from tracking space's origin, placed at
// Shepard's head bone. Leaning must move the view, not the hands.
void TestHandTargetsOrigin() {
    using namespace mevr::engine;
    // Halcyon-style views of one camera (facing world +X), head-locked, 150 units per meter of
    // lean: upright at the origin, then leaning 0.2 m forward with the view moved 30 units along.
    UnrealRotation const camera{0, 0, 0};
    XrPosef upright{};
    upright.orientation = {0, 0, 0, 1};
    XrPosef leaning = upright;
    leaning.position = {0.0f, 0.0f, -0.2f};
    TrackingFrame const viewUpright = FromCameraView({0.0, 0.0, 0.0}, camera, {}, {0.0, 0.0, 0.0}, upright, 150.0);
    TrackingFrame const viewLeaning = FromCameraView({0.0, 0.0, 0.0}, camera, {}, {30.0, 0.0, 0.0}, leaning, 150.0);
    CHECK(NearV(viewLeaning.headWorld, {30.0, 0.0, 0.0}));
    // The view puts the origin in the same place either way.
    CHECK(NearV(OriginWorld(viewUpright), {0.0, 0.0, 0.0}));
    CHECK(NearV(OriginWorld(viewLeaning), {0.0, 0.0, 0.0}, 1e-3));

    UnrealVector const headBone{100.0, 20.0, 160.0};
    XrVector3f const grip{0.2f, -0.4f, -0.3f};  // still: right, below and ahead of the origin
    UnrealVector const handUpright = ToWorld(AtOrigin(viewUpright, headBone, kUnitsPerMeter), grip);
    UnrealVector const handLeaning = ToWorld(AtOrigin(viewLeaning, headBone, kUnitsPerMeter), grip);
    CHECK(NearV(handUpright, handLeaning, 1e-3));
    CHECK(NearV(handUpright, {100.0 + 30.0, 20.0 + 20.0, 160.0 - 40.0}, 1e-3));  // 1 m = 100 units around the bone
    // Measured from the live head instead (the old mapping), the still hand moved 20 units back.
    TrackingFrame fromHead = viewLeaning;
    fromHead.headWorld = headBone;
    fromHead.unitsPerMeter = kUnitsPerMeter;
    CHECK(NearD(ToWorld(fromHead, grip).x - handUpright.x, -20.0, 1e-3));

    // The turn into the world is the view's, untouched: a turned camera turns the hands about the bone.
    TrackingFrame const turned = FromCameraView({0.0, 0.0, 0.0}, UnrealRotation{0, Units(90), 0}, {}, {0.0, 0.0, 0.0},
        upright, 150.0);
    TrackingFrame const body = AtOrigin(turned, headBone, kUnitsPerMeter);
    CHECK(NearB(body.rotation, turned.rotation));
    CHECK(NearV(ToWorld(body, grip), {100.0 - 20.0, 20.0 + 30.0, 160.0 - 40.0}, 1e-3));
    XrQuaternionf const q = YawPitch(25, -10);
    CHECK(NearB(OrientationToWorld(body, q), OrientationToWorld(turned, q)));
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

    // A frame that pushed but never presented, in the real call order: every later image is
    // paired with the previous frame's view and every lockstep wait times out, until the game
    // thread's drift recovery (3 timeouts in a row, as in Mod.cpp) drops one view.
    vs::Reset();
    vs::PushRendered(View(1));  // its Present never comes
    unsigned timeouts = 0;
    bool recovered = false;
    for (int frame = 2; frame <= 6; ++frame) {
        vs::PushRendered(View(static_cast<float>(frame)));
        float const paired = PeekId(depth);  // what the render thread shows image `frame` with
        if (recovered) {
            CHECK(paired == static_cast<float>(frame) && depth == 1);  // aligned again
        } else {
            CHECK(paired == static_cast<float>(frame - 1));  // off by one while drifting
        }
        vs::NotifyPresented();
        if (vs::WaitForPresent(0)) {
            timeouts = 0;
        } else if (++timeouts == 3) {
            vs::DropOldest();
            timeouts = 0;
            recovered = true;
        }
    }
    CHECK(recovered);
    CHECK(!vs::HasPendingFrame());

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

void TestBoneNames() {
    using mevr::ik::HandSide;
    using mevr::ik::IsHeadBone;
    // Hands, whatever the convention.
    CHECK(HandSide("Hand_R") == 1 && HandSide("hand_l") == 0);
    CHECK(HandSide("R_Hand") == 1 && HandSide("L_Wrist") == 0);
    CHECK(HandSide("RightHand") == 1 && HandSide("leftHand") == 0);
    CHECK(HandSide("Bip01_R_Hand") == 1);
    // Not hands.
    CHECK(HandSide("Handle_R") == -1);
    CHECK(HandSide("Finger1_R") == -1 && HandSide("Hand_R_Thumb") == -1 && HandSide("Hand_R_End") == -1);
    CHECK(HandSide("Hand") == -1);  // no side
    CHECK(HandSide("Spine2") == -1);
    // The head.
    CHECK(IsHeadBone("Head") && IsHeadBone("head") && IsHeadBone("Bip01_Head"));
    CHECK(!IsHeadBone("HeadEnd") && !IsHeadBone("Head_Nub") && !IsHeadBone("Eye_Left") && !IsHeadBone("Forehead"));
    auto const tokens = mevr::ik::NameTokens("Bip01_R_Finger12");
    CHECK(tokens.size() == 5 && tokens[0] == "bip" && tokens[2] == "r" && tokens[3] == "finger" && tokens[4] == "12");
}

void TestIk() {
    using namespace mevr::ik;
    double const upper = 0.30;
    double const lower = 0.28;
    Vec3 const shoulder{0.2, 1.4, 0.0};
    Vec3 const pole{0.5, 0.9, 0.2};  // down, out to the right, back

    // Reachable: hand on target, bones keep their lengths, elbow on the pole's side.
    Vec3 const target{0.3, 1.2, -0.4};
    auto const arm = SolveTwoBone(shoulder, target, upper, lower, pole);
    CHECK(arm.reached);
    CHECK(Length(arm.end - target) < 1e-5);
    CHECK(std::abs(Length(arm.mid - shoulder) - upper) < 1e-9);
    CHECK(std::abs(Length(arm.end - arm.mid) - lower) < 1e-5);
    Vec3 const line = Normalize(target - shoulder);
    Vec3 const elbowOffset = (arm.mid - shoulder) - line * Dot(arm.mid - shoulder, line);
    Vec3 const poleOffset = (pole - shoulder) - line * Dot(pole - shoulder, line);
    CHECK(Dot(elbowOffset, poleOffset) > 0.0);

    // Out of reach: a straight arm pointing at the target.
    Vec3 const distant{0.2, 1.4, -2.0};
    auto const straight = SolveTwoBone(shoulder, distant, upper, lower, pole);
    CHECK(!straight.reached);
    CHECK(std::abs(Length(straight.end - shoulder) - (upper + lower)) < 1e-4);
    CHECK(Length(Normalize(straight.end - shoulder) - Normalize(distant - shoulder)) < 1e-6);

    // Pole on the line: still a valid bend (lengths kept).
    auto const degenerate = SolveTwoBone(shoulder, Vec3{0.2, 1.4, -0.4}, upper, lower, Vec3{0.2, 1.4, -1.0});
    CHECK(std::abs(Length(degenerate.mid - shoulder) - upper) < 1e-9);

    // Body yaw: holds inside the deadzone, is dragged along past it.
    BodyYaw body(0.6, 0.0);  // no recentering, to test the deadzone alone
    CHECK(NearD(body.Update(0.0, 0.1), 0.0));
    CHECK(NearD(body.Update(0.5, 0.1), 0.0));
    CHECK(NearD(body.Update(1.0, 0.1), 0.4));
    CHECK(NearD(body.Update(-1.0, 0.1), -0.4));
    BodyYaw easing(0.6, 1.0);  // recenters a fraction per second
    easing.Update(0.0, 0.1);
    double const eased = easing.Update(0.4, 0.5);
    CHECK(eased > 0.0 && eased < 0.4);

    // YawOf matches the OpenXR convention (positive = turned left about +Y).
    XrQuaternionf const left = AxisAngle(0, 1, 0, 90);
    CHECK(NearD(YawOf(left.x, left.y, left.z, left.w), std::numbers::pi / 2.0, 1e-5));
    XrQuaternionf const lookUp = AxisAngle(1, 0, 0, 30);
    CHECK(NearD(YawOf(lookUp.x, lookUp.y, lookUp.z, lookUp.w), 0.0, 1e-5));
    // Turned 30 degrees left, then looking straight down or up: still 30 degrees, from the up vector.
    for (double pitch : {-90.0, -89.0, 89.0, 90.0}) {
        double const yaw = std::numbers::pi / 6.0;
        XrQuaternionf const qy = AxisAngle(0, 1, 0, 30);
        XrQuaternionf const qp = AxisAngle(1, 0, 0, pitch);
        XrQuaternionf const q{  // qy * qp
            qy.w * qp.x + qy.x * qp.w + qy.y * qp.z - qy.z * qp.y,
            qy.w * qp.y - qy.x * qp.z + qy.y * qp.w + qy.z * qp.x,
            qy.w * qp.z + qy.x * qp.y - qy.y * qp.x + qy.z * qp.w,
            qy.w * qp.w - qy.x * qp.x - qy.y * qp.y - qy.z * qp.z,
        };
        CHECK(NearD(YawOf(q.x, q.y, q.z, q.w), yaw, 1e-4));
    }

    // Zero-length bones don't produce NaNs.
    auto const collapsed = SolveTwoBone(shoulder, target, 0.0, lower, pole);
    CHECK(std::isfinite(collapsed.mid.x) && std::isfinite(collapsed.end.x));

    // Upper body: shoulders level and symmetric about the neck, below and behind the head.
    auto const torso = EstimateUpperBody(Vec3{0.0, 1.7, 0.0}, 0.0);
    CHECK(NearD(torso.leftShoulder.y, torso.rightShoulder.y));
    CHECK(torso.rightShoulder.x > 0.0 && torso.leftShoulder.x < 0.0);
    CHECK(NearD(torso.rightShoulder.x, -torso.leftShoulder.x));
    CHECK(torso.neck.y < 1.7 && torso.neck.z > 0.0);  // forward is -Z, so behind is +Z
    auto const turned = EstimateUpperBody(Vec3{0.0, 1.7, 0.0}, std::numbers::pi / 2.0);  // facing left (-X)
    CHECK(NearD(turned.forward.x, -1.0, 1e-9) && NearD(turned.right.z, -1.0, 1e-9));
    Vec3 const elbowHint = ElbowPole(torso, +1);
    CHECK(elbowHint.y < torso.rightShoulder.y && elbowHint.x > torso.rightShoulder.x);
}

mevr::ik::Quat AxisAngleQ(double x, double y, double z, double degrees) {
    double const half = degrees * std::numbers::pi / 360.0;
    double const s = std::sin(half) / std::sqrt(x * x + y * y + z * z);
    return {x * s, y * s, z * s, std::cos(half)};
}

bool NearV(mevr::ik::Vec3 a, mevr::ik::Vec3 b, double tolerance = 1e-9) {
    return mevr::ik::Length(a - b) <= tolerance;
}

bool NearT(mevr::ik::Transform const& a, mevr::ik::Transform const& b, double tolerance = 1e-9) {
    return NearV(a.translation, b.translation, tolerance) && mevr::ik::AngleBetween(a.rotation, b.rotation) <= tolerance &&
        std::abs(a.scale - b.scale) <= tolerance;
}

void TestRigid() {
    using namespace mevr::ik;
    // Rotations act as expected and compose right to left.
    Quat const yaw90 = AxisAngleQ(0, 0, 1, 90);
    CHECK(NearV(Rotate(yaw90, {1, 0, 0}), {0, 1, 0}, 1e-12));
    Quat const pitch = AxisAngleQ(0, 1, 0, 30);
    Vec3 const v{0.3, -1.2, 2.5};
    CHECK(NearV(Rotate(yaw90 * pitch, v), Rotate(yaw90, Rotate(pitch, v)), 1e-12));
    CHECK(NearV(Rotate(yaw90 * Conjugate(yaw90), v), v, 1e-12));
    // FromTo, including opposite directions.
    for (auto const& [a, b] : {std::pair<Vec3, Vec3>{{1, 0, 0}, {0, 0, 1}}, {{1, 2, 3}, {-2, 0.5, 1}}, {{0, 1, 0}, {0, -1, 0}}}) {
        CHECK(NearV(Rotate(FromTo(a, b), Normalize(a)), Normalize(b), 1e-9));
    }
    // FromAxes recovers a rotation from its axes, on every branch of the matrix conversion.
    for (Quat const q : {AxisAngleQ(1, 2, 3, 40), AxisAngleQ(1, 0, 0, 179), AxisAngleQ(0, 1, 0, 179), AxisAngleQ(0, 0, 1, 179),
             AxisAngleQ(-1, 1, 0.2, 200), Quat{}}) {
        Quat const back = FromAxes(Rotate(q, {1, 0, 0}), Rotate(q, {0, 1, 0}), Rotate(q, {0, 0, 1}));
        CHECK(AngleBetween(back, q) < 1e-7);
    }
    // Bone atoms: compose, invert, relative.
    Transform const local{AxisAngleQ(1, 1, 0, 25), {3, 4, 5}, 1.0};
    Transform const parent{AxisAngleQ(0, 0, 1, -70), {-10, 2, 7}, 2.0};
    Vec3 const p{1, -2, 0.5};
    CHECK(NearV(Apply(Compose(local, parent), p), Apply(parent, Apply(local, p)), 1e-9));
    CHECK(NearV(Apply(Inverse(parent), Apply(parent, p)), p, 1e-9));
    CHECK(NearT(Compose(Relative(local, parent), parent), local, 1e-9));
    // UE3 FMatrix layout: rows are the images of the axes, row 3 the origin.
    Affine a;
    for (int r = 0; r < 3; ++r) {
        Vec3 const axis = Rotate(parent.rotation, Vec3{r == 0 ? 1.0 : 0.0, r == 1 ? 1.0 : 0.0, r == 2 ? 1.0 : 0.0}) * parent.scale;
        a.m[r][0] = axis.x;
        a.m[r][1] = axis.y;
        a.m[r][2] = axis.z;
    }
    a.m[3][0] = parent.translation.x;
    a.m[3][1] = parent.translation.y;
    a.m[3][2] = parent.translation.z;
    CHECK(NearV(TransformPoint(a, p), Apply(parent, p), 1e-9));
    auto const inverse = InverseAffine(a);
    CHECK(inverse.has_value() && NearV(TransformPoint(*inverse, TransformPoint(a, p)), p, 1e-9));
    auto const rotation = RotationOf(a);
    CHECK(rotation.has_value() && AngleBetween(*rotation, parent.rotation) < 1e-7);
    Affine mirrored = a;
    for (int c = 0; c < 3; ++c) {
        mirrored.m[0][c] = -mirrored.m[0][c];
    }
    CHECK(!RotationOf(mirrored).has_value());
    Affine stretched = a;
    for (int c = 0; c < 3; ++c) {
        stretched.m[1][c] *= 1.5;
    }
    CHECK(!RotationOf(stretched).has_value());
    Affine singular = a;
    for (int c = 0; c < 3; ++c) {
        singular.m[2][c] = 0.0;
    }
    CHECK(!InverseAffine(singular).has_value());
}

// OpenXR orientations as UE3 rotations: rotating then mapping equals mapping then rotating.
void TestOrientation() {
    using mevr::engine::ToUnrealOffset;
    using mevr::engine::ToUnrealOrientation;
    using mevr::ik::Vec3;
    XrQuaternionf const samples[] = {AxisAngle(0, 1, 0, 90), AxisAngle(1, 0, 0, 30), AxisAngle(0, 0, 1, 20), {0.2f, -0.5f, 0.4f, 0.74f}};
    XrVector3f const vectors[] = {{0, 0, -1}, {1, 0, 0}, {0.3f, -0.7f, 0.2f}};
    for (int yaw : {0, Units(90), Units(-37)}) {
        for (XrQuaternionf q : samples) {
            double const n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            q = {static_cast<float>(q.x / n), static_cast<float>(q.y / n), static_cast<float>(q.z / n), static_cast<float>(q.w / n)};
            for (XrVector3f const& v : vectors) {
                Vec3 const turned = mevr::ik::Rotate(mevr::ik::Quat{q.x, q.y, q.z, q.w}, {v.x, v.y, v.z});
                auto const expected = ToUnrealOffset({static_cast<float>(turned.x), static_cast<float>(turned.y), static_cast<float>(turned.z)}, yaw);
                auto const mapped = ToUnrealOffset(v, 0);
                Vec3 const actual = mevr::ik::Rotate(ToUnrealOrientation(q, yaw), {mapped.x, mapped.y, mapped.z});
                CHECK(NearV(actual, {expected.x, expected.y, expected.z}, 1e-3));
            }
        }
    }
    // Turning left in OpenXR (about +Y) turns UE3 forward (+X) to the left (-Y).
    Vec3 const forward = mevr::ik::Rotate(ToUnrealOrientation(AxisAngle(0, 1, 0, 90), 0), {1, 0, 0});
    CHECK(NearV(forward, {0, -1, 0}, 1e-6));
}

void TestArmChains() {
    using mevr::ik::FindArmChain;
    // ME-style names (the LeftWrist / -Swivel bones the AnimTree's limb controls name), with roll
    // and swivel helpers in and beside the chain.
    std::vector<std::string> const me{"Root", "Pelvis", "Spine", "Spine1", "Neck", "Head", "LeftCollar", "LeftShoulder",
        "LeftShoulder-Swivel", "LeftElbow", "LeftWrist", "LeftHandIndex1", "RightCollar", "RightShoulder", "RightShoulderRoll",
        "RightElbow", "RightForearmRoll", "RightWrist", "RightHandIndex1", "LHIK"};
    std::vector<int> const meParents{-1, 0, 1, 2, 3, 4, 3, 6, 6, 7, 9, 10, 3, 12, 13, 13, 15, 15, 17, 17};
    auto const right = FindArmChain(me, meParents, 1);
    CHECK(right && right->upper == 13 && right->fore == 15 && right->hand == 17);
    auto const left = FindArmChain(me, meParents, 0);
    CHECK(left && left->upper == 7 && left->fore == 9 && left->hand == 10);
    // Mixamo: "Shoulder" is the clavicle there; the nearest arm-named ancestor wins.
    std::vector<std::string> const mixamo{"Hips", "Spine", "RightShoulder", "RightArm", "RightForeArm", "RightHand"};
    std::vector<int> const chainParents{-1, 0, 1, 2, 3, 4};
    auto const mx = FindArmChain(mixamo, chainParents, 1);
    CHECK(mx && mx->upper == 3 && mx->fore == 4 && mx->hand == 5);
    // Biped.
    std::vector<std::string> const biped{"Bip01", "Bip01 Spine", "Bip01 L Clavicle", "Bip01 L UpperArm", "Bip01 L Forearm", "Bip01 L Hand"};
    auto const bp = FindArmChain(biped, chainParents, 0);
    CHECK(bp && bp->upper == 3 && bp->fore == 4 && bp->hand == 5);
    CHECK(!FindArmChain(biped, chainParents, 1));  // no right arm
    // Unfamiliar names: the hand's nearest joints.
    std::vector<std::string> const plain{"root", "j1", "j2", "j3", "R_Hand"};
    std::vector<int> const plainParents{-1, 0, 1, 2, 3};
    auto const fallback = FindArmChain(plain, plainParents, 1);
    CHECK(fallback && fallback->upper == 2 && fallback->fore == 3 && fallback->hand == 4);
    // An IK helper named like a hand, listed first, and a prop under the wrist don't win.
    std::vector<std::string> const helpers{"Root", "RightHand_IK", "Spine", "RightShoulder", "RightElbow", "RightWrist", "RightHandProp"};
    auto const real = FindArmChain(helpers, std::vector<int>{-1, 0, 0, 2, 3, 4, 5}, 1);
    CHECK(real && real->upper == 3 && real->fore == 4 && real->hand == 5);
    // A child listed before its parent is rejected (walks could loop).
    CHECK(!FindArmChain(plain, std::vector<int>{-1, 0, 4, 2, 3}, 1));
}

void TestArmPose() {
    using namespace mevr::ik;
    // A right arm in UE3-like component axes (X forward, Y right, Z up, centimetres), animated
    // with the elbow bent forward. Each bone's X axis points at its child, rolled arbitrarily.
    Vec3 const shoulder{0, 18, 145};
    Vec3 const elbow{5, 20, 117};
    Vec3 const wrist{25, 20, 105};
    Quat const upperRot = AxisAngleQ(elbow.x - shoulder.x, elbow.y - shoulder.y, elbow.z - shoulder.z, 33) * FromTo({1, 0, 0}, elbow - shoulder);
    Quat const foreRot = AxisAngleQ(wrist.x - elbow.x, wrist.y - elbow.y, wrist.z - elbow.z, -50) * FromTo({1, 0, 0}, wrist - elbow);
    Quat const handRot = AxisAngleQ(0.2, 1, 0.4, 70);
    std::vector<Transform> pose{
        {{}, {0, 0, 0}, 1.0},                                         // 0 root
        {{}, {0, 0, 140}, 1.0},                                       // 1 spine
        {upperRot, shoulder, 1.0},                                    // 2 upper arm
        {upperRot * AxisAngleQ(1, 0, 0, 15), (shoulder + elbow) * 0.5, 1.0},  // 3 upper-arm twist (child of 2)
        {foreRot, elbow, 1.0},                                        // 4 forearm
        {handRot, wrist, 1.0},                                        // 5 hand
        {handRot * AxisAngleQ(0, 0, 1, 20), wrist + Rotate(handRot, {8, 1, 0}), 1.0},  // 6 finger (child of 5)
        {{}, {0, -18, 145}, 1.0},                                     // 7 left shoulder (other branch)
    };
    std::vector<int> const parents{-1, 0, 1, 2, 2, 4, 5, 1};
    ArmChain const chain{2, 4, 5};
    BodyFrame const body;  // X forward, Y right, Z up
    std::vector<Transform> const before = pose;

    // In front of the chest: reachable.
    ArmGoal goal;
    goal.wrist = {35, 10, 130};
    ArmResult const result = PoseArm(pose, parents, chain, 1, body, goal);
    CHECK(result.ok && result.reached);
    CHECK(NearV(pose[5].translation, goal.wrist, 1e-6));
    CHECK(NearV(pose[2].translation, shoulder));  // the shoulder stays
    CHECK(std::abs(Length(pose[4].translation - shoulder) - Length(elbow - shoulder)) < 1e-6);
    CHECK(std::abs(Length(pose[5].translation - pose[4].translation) - Length(wrist - elbow)) < 1e-6);
    // Skinning stays rigid: each bone's offset in its parent's frame is unchanged...
    for (int i : {4, 5}) {
        Vec3 const oldOffset = Apply(Inverse(before[parents[i]]), before[i].translation);
        Vec3 const newOffset = Apply(Inverse(pose[parents[i]]), pose[i].translation);
        CHECK(NearV(oldOffset, newOffset, 1e-6));
    }
    // ...and bones hanging from a segment keep their whole transform relative to it.
    CHECK(NearT(Relative(pose[3], pose[2]), Relative(before[3], before[2]), 1e-9));
    CHECK(NearT(Relative(pose[6], pose[5]), Relative(before[6], before[5]), 1e-9));
    CHECK(NearT(pose[7], before[7], 0.0) && NearT(pose[1], before[1], 0.0) && NearT(pose[0], before[0], 0.0));
    // The elbow stays a hinge: it turns only about the animated bend axis, in the upper arm's frame.
    Quat const oldBend = Relative(before[4], before[2]).rotation;
    Quat const newBend = Relative(pose[4], pose[2]).rotation;
    Quat const change = Normalize(newBend * Conjugate(oldBend));
    Vec3 const changeAxis = Normalize(Vec3{change.x, change.y, change.z});
    Vec3 const hinge = Rotate(Conjugate(upperRot), Normalize(Cross(elbow - shoulder, wrist - elbow)));
    CHECK(Length(Cross(changeAxis, hinge)) < 1e-6);
    // The elbow bends toward the pole: out to the right and down.
    Vec3 const reach = Normalize(goal.wrist - shoulder);
    Vec3 const bend = (pose[4].translation - shoulder) - reach * Dot(pose[4].translation - shoulder, reach);
    CHECK(Dot(bend, ElbowPoleDirection(goal.wrist - shoulder, body, 1)) > 0.0);
    // Solving again from the result changes nothing.
    std::vector<Transform> const solved = pose;
    CHECK(PoseArm(pose, parents, chain, 1, body, goal).ok);
    for (std::size_t i = 0; i < pose.size(); ++i) {
        CHECK(NearT(pose[i], solved[i], 1e-7));
    }

    // A hand rotation goal: the hand takes it, the finger follows the hand.
    pose = before;
    goal.hasRotation = true;
    goal.rotation = AxisAngleQ(0, 1, 0, -40);
    CHECK(PoseArm(pose, parents, chain, 1, body, goal).ok);
    CHECK(AngleBetween(pose[5].rotation, goal.rotation) < 1e-9 && NearV(pose[5].translation, goal.wrist, 1e-6));
    CHECK(NearT(Relative(pose[6], pose[5]), Relative(before[6], before[5]), 1e-9));

    // Out of reach: a straight arm pointing at the target.
    pose = before;
    goal = {};
    goal.wrist = {200, 18, 145};
    ArmResult const outOfReach = PoseArm(pose, parents, chain, 1, body, goal);
    double const armLength = Length(elbow - shoulder) + Length(wrist - elbow);
    CHECK(outOfReach.ok && !outOfReach.reached);
    CHECK(std::abs(Length(pose[5].translation - shoulder) - armLength) < 1e-4);
    CHECK(NearV(Normalize(pose[5].translation - shoulder), Normalize(goal.wrist - shoulder), 1e-6));

    // A straight animated arm (no bend plane) still solves cleanly.
    pose = before;
    pose[5].translation = elbow + Normalize(elbow - shoulder) * Length(wrist - elbow);
    pose[6].translation = pose[5].translation + Vec3{1, 0, 0};
    goal.wrist = {30, 15, 125};
    ArmResult const fromStraight = PoseArm(pose, parents, chain, 1, body, goal);
    CHECK(fromStraight.ok && NearV(pose[5].translation, goal.wrist, 1e-6));
    for (Transform const& t : pose) {
        CHECK(std::isfinite(t.translation.x) && std::isfinite(t.rotation.w));
    }

    // Failures write nothing.
    pose = before;
    goal.wrist = {std::nan(""), 0, 0};
    CHECK(!PoseArm(pose, parents, chain, 1, body, goal).ok);
    goal.wrist = {35, 10, 130};
    CHECK(!PoseArm(pose, parents, ArmChain{4, 2, 5}, 1, body, goal).ok);  // out of order
    CHECK(!PoseArm(pose, parents, ArmChain{2, 4, 7}, 1, body, goal).ok);  // not one branch
    for (std::size_t i = 0; i < pose.size(); ++i) {
        CHECK(NearT(pose[i], before[i], 0.0));
    }

    // The pole: out and down for a forward reach, perpendicular to it, mirrored for the left arm.
    Vec3 const poleRight = ElbowPoleDirection({1, 0, 0}, body, 1);
    Vec3 const poleLeft = ElbowPoleDirection({1, 0, 0}, body, 0);
    CHECK(poleRight.y > 0.0 && poleRight.z < 0.0 && std::abs(poleRight.x) < 1e-9);
    CHECK(poleLeft.y < 0.0 && poleLeft.z < 0.0);
    Vec3 const overhead = ElbowPoleDirection({0, 0, 1}, body, 1);
    CHECK(std::abs(overhead.z) < 1e-9 && NearD(Length(overhead), 1.0));
}

// Out of reach, the arm stretches up to 1.25x (Overlord) so the hand lands on the target.
void TestArmStretch() {
    using namespace mevr::ik;
    Vec3 const shoulder{0, 18, 145};
    std::vector<Transform> const before{
        {{}, {0, 0, 140}, 1.0},
        {FromTo({1, 0, 0}, {0.2, 0, -1}), shoulder, 1.0},
        {FromTo({1, 0, 0}, {1, 0, -0.3}), {5, 18, 117}, 1.0},
        {{}, {25, 18, 111}, 1.0},
    };
    std::vector<int> const parents{-1, 0, 1, 2};
    ArmChain const chain{1, 2, 3};
    double const length = Length(before[2].translation - shoulder) + Length(before[3].translation - before[2].translation);
    ArmGoal goal;
    goal.maxStretch = kMaxArmStretch;
    // 1.2 arm lengths ahead: reached, both segments 1.2x.
    std::vector<Transform> pose = before;
    goal.wrist = shoulder + Vec3{1, 0, 0} * (1.2 * length);
    ArmResult const stretched = PoseArm(pose, parents, chain, 1, BodyFrame{}, goal);
    CHECK(stretched.ok && stretched.reached && NearD(stretched.stretch, 1.2, 1e-3));
    CHECK(NearV(pose[3].translation, goal.wrist, 1e-6));
    double const upper = Length(pose[2].translation - shoulder), lower = Length(pose[3].translation - pose[2].translation);
    CHECK(NearD(upper / Length(before[2].translation - shoulder), lower / Length(before[3].translation - before[2].translation), 1e-6));
    // 1.4 arm lengths: capped at 1.25x, a straight arm pointing at it.
    pose = before;
    goal.wrist = shoulder + Vec3{1, 0, 0} * (1.4 * length);
    ArmResult const capped = PoseArm(pose, parents, chain, 1, BodyFrame{}, goal);
    CHECK(capped.ok && !capped.reached && NearD(capped.stretch, kMaxArmStretch, 1e-9));
    CHECK(NearD(Length(pose[3].translation - shoulder), 1.25 * length, 1e-3));
    // No stretch asked (the default) or within reach: lengths unchanged.
    pose = before;
    goal.maxStretch = 1.0;
    goal.wrist = shoulder + Vec3{1, 0, 0} * (1.2 * length);
    CHECK(PoseArm(pose, parents, chain, 1, BodyFrame{}, goal).stretch == 1.0);
    CHECK(NearD(Length(pose[3].translation - shoulder), length, 1e-4));
    pose = before;
    goal.maxStretch = kMaxArmStretch;
    goal.wrist = shoulder + Vec3{0.5, 0, -0.5} * (0.6 * length);
    CHECK(PoseArm(pose, parents, chain, 1, BodyFrame{}, goal).stretch == 1.0);
}

// A UE3-like upper body (X forward, Y right, Z up, units ~cm): legs hang from the pelvis, the
// spine from it too; clavicles, neck and arms from the top of the spine.
struct TorsoRig {
    std::vector<std::string> names{"Root", "Pelvis", "LowerBack", "Chest", "Chest2", "LeftCollar", "RightCollar", "Neck",
        "Head", "LeftShoulder", "LeftElbow", "LeftWrist", "RightShoulder", "RightElbow", "RightWrist", "Eye_Left",
        "Eye_Right", "LeftHip"};
    std::vector<int> parents{-1, 0, 1, 2, 3, 4, 4, 4, 7, 5, 9, 10, 6, 12, 13, 8, 8, 1};
    std::vector<mevr::ik::Transform> pose;
    TorsoRig() {
        using mevr::ik::Vec3;
        std::vector<Vec3> const at{{0, 0, 0}, {0, 0, 100}, {0, 0, 105}, {0, 0, 120}, {0, 0, 135}, {0, -5, 145}, {0, 5, 145},
            {0, 0, 150}, {0, 0, 165}, {0, -18, 145}, {2, -18, 117}, {12, -16, 95}, {0, 18, 145}, {2, 18, 117}, {12, 16, 95},
            {8, -3, 170}, {8, 3, 170}, {0, -10, 90}};
        for (std::size_t i = 0; i < at.size(); ++i) {
            pose.push_back({AxisAngleQ(0.3, 1, 0.2, 10.0 * static_cast<double>(i)), at[i], 1.0});
        }
    }
};

void TestTorsoPose() {
    using namespace mevr::ik;
    TorsoRig rig;
    auto const left = FindArmChain(rig.names, rig.parents, 0);
    auto const right = FindArmChain(rig.names, rig.parents, 1);
    CHECK(left && right);
    auto const chain = FindTorsoChain(rig.names, rig.parents, *left, *right);
    CHECK(chain.has_value());
    CHECK(chain->spine == (std::vector<int>{2, 3, 4}));
    CHECK(chain->collars[0] == 5 && chain->collars[1] == 6);
    BodyFrame const body;
    std::vector<Transform> const before = rig.pose;
    auto const mid = [](std::vector<Transform> const& p) { return (p[9].translation + p[12].translation) * 0.5; };
    auto const eyes = [](std::vector<Transform> const& p) { return (p[15].translation + p[16].translation) * 0.5; };

    // Already there: nothing moves.
    TorsoGoal goal;
    goal.reference = {15, 16};
    goal.head = eyes(before);
    goal.forward = {1, 0, 0};
    std::vector<Transform> pose = before;
    TorsoResult r = PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal);
    CHECK(r.ok && !r.limited && r.bonesWritten > 0);
    for (std::size_t i = 0; i < pose.size(); ++i) {
        CHECK(NearT(pose[i], before[i], 1e-6));
    }

    // A pose the spine can reach: the upper body turned 20 degrees right and leaned 15 forward
    // about the lowest spine joint. The shoulders land there and face the new way; the legs and
    // the pelvis stay; everything above the spine moves rigidly.
    Vec3 const pivot = before[2].translation;
    Quat const turn = Normalize(AxisAngleQ(0, 1, 0, 15) * AxisAngleQ(0, 0, 1, 20));
    goal.head = pivot + Rotate(turn, eyes(before) - pivot);
    goal.forward = Rotate(AxisAngleQ(0, 0, 1, 20), {1, 0, 0});
    Vec3 const expected = pivot + Rotate(turn, mid(before) - pivot);
    pose = before;
    r = PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal);
    CHECK(r.ok && !r.limited);
    CHECK(Length(mid(pose) - expected) < 1.0);
    CHECK(Length(r.wantedShoulders - expected) < 1.0);
    CHECK(std::acos(std::clamp(Dot(r.facing, goal.forward), -1.0, 1.0)) * 180.0 / std::numbers::pi < 2.0);
    CHECK(NearD(r.twistDegrees, 20.0, 1e-6));
    for (int i : {0, 1, 2, 17}) {
        CHECK(NearV(pose[i].translation, before[i].translation, 1e-9));
    }
    CHECK(NearT(pose[0], before[0], 0.0) && NearT(pose[1], before[1], 0.0) && NearT(pose[17], before[17], 0.0));
    for (int i : {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}) {
        CHECK(NearT(Relative(pose[i], pose[4]), Relative(before[i], before[4]), 1e-6));
    }
    CHECK(NearD(Length(pose[13].translation - pose[12].translation), Length(before[13].translation - before[12].translation), 1e-9));

    // Too far: the bend stops at its limit, toward the goal.
    pose = before;
    goal.head = eyes(before) + Vec3{100, 0, -60};
    goal.forward = {1, 0, 0};
    r = PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal);
    CHECK(r.ok && r.limited && r.bendDegrees <= 35.0 + 1e-6 && r.bendDegrees > 30.0);
    CHECK(mid(pose).x > mid(before).x + 20.0);
    // Facing backwards: the twist stops at 60 degrees.
    pose = before;
    goal.head = eyes(before);
    goal.forward = {-1, 0.01, 0};
    r = PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal);
    CHECK(r.ok && r.limited && NearD(std::abs(r.twistDegrees), 60.0, 1e-6));

    // Failures write nothing.
    pose = before;
    goal.reference = {-1, -1};
    CHECK(!PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal).ok);
    goal.reference = {17, -1};  // a leg isn't above the spine
    CHECK(!PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal).ok);
    goal.reference = {8, -1};
    goal.head = {std::nan(""), 0, 0};
    CHECK(!PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal).ok);
    for (std::size_t i = 0; i < pose.size(); ++i) {
        CHECK(NearT(pose[i], before[i], 0.0));
    }

    // The clavicle shrugs toward a hand target far above, within its limit; a near one: nothing.
    pose = before;
    double const shrug = PoseCollar(pose, rig.parents, 6, *right, {0, 18, 250});
    CHECK(shrug > 1.0 && shrug <= 15.0 + 1e-9);
    CHECK(pose[12].translation.z > before[12].translation.z + 0.5);
    CHECK(NearT(pose[9], before[9], 0.0));
    CHECK(NearT(Relative(pose[14], pose[6]), Relative(before[14], before[6]), 1e-9));
    pose = before;
    CHECK(PoseCollar(pose, rig.parents, 6, *right, {20, 18, 120}) == 0.0);

    // A fraction of a turn: the same axis, that part of the angle.
    Quat const half = Fraction(AxisAngleQ(0, 0, 1, 90), 0.5);
    CHECK(AngleBetween(half, AxisAngleQ(0, 0, 1, 45)) < 1e-9);
    CHECK(AngleBetween(Fraction(Quat{0, 0, 0, -1}, 0.5), Quat{}) < 1e-9);
}

// The arm-length calibration's T pose (tracking space, meters).
void TestArmLength() {
    using namespace mevr::ik;
    ArmLengthSample sample{{0.02, 1.62, -0.05}, {-0.80, 1.45, 0.0}, {0.82, 1.45, 0.0}};
    auto const arms = ArmLengthFromTPose(sample);
    CHECK(arms.has_value());
    // The head projects onto the grip line at x = 0.02: shoulders at -0.16 and 0.20.
    CHECK(NearD(arms->left, 0.64, 1e-9) && NearD(arms->right, 0.62, 1e-9) && NearD(arms->mean, 0.63, 1e-9));
    // Not a T pose: hands together, or both hands to one side of the head.
    CHECK(!ArmLengthFromTPose({{0, 1.6, 0}, {-0.1, 1.2, -0.3}, {0.1, 1.2, -0.3}}).has_value());
    CHECK(!ArmLengthFromTPose({{0, 1.6, 0}, {0.1, 1.45, 0}, {1.6, 1.45, 0}}).has_value());
    CHECK(!ArmLengthFromTPose({{0, 1.6, 0}, {-3.0, 1.45, 0}, {3.0, 1.45, 0}}).has_value());  // 2.8 m arms
}

// The first aim ray's muzzle sat 2.12 m from the head in tracking space (2026-10-08): the hands
// were mapped into the world at 100 units per meter, the view (Halcyon's) at 50, so mapped back
// through the view every offset doubled. At the view's own scale, and in its frame, a point goes
// back exactly where the controller was.
void TestReachScale() {
    using namespace mevr::engine;
    XrPosef head{};
    head.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    head.position = {0.0f, 1.6f, 0.0f};
    TrackingFrame const view = FromCameraView({100.0, 200.0, 300.0}, UnrealRotation{0, Units(30), 0}, {}, {0.0, 0.0, 0.0}, head, 50.0);
    XrVector3f const grip{0.10f, 0.55f, -0.30f};  // a hand 1.09 m from the head, hanging low
    double const handFromHead = std::hypot(grip.x - head.position.x, grip.y - head.position.y, grip.z - head.position.z);
    // The old mapping: 100 units per meter about the same point.
    TrackingFrame old = view;
    old.unitsPerMeter = kUnitsPerMeter;
    XrVector3f const back = ToTracking(view, ToWorld(old, grip));
    double const oldDistance = std::hypot(back.x - head.position.x, back.y - head.position.y, back.z - head.position.z);
    CHECK(NearD(oldDistance, 2.0 * handFromHead, 1e-4));
    // First person now: the view's own frame at its own scale.
    TrackingFrame const firstPerson{view.head, view.headWorld, view.rotation, view.unitsPerMeter};
    CHECK(NearX(ToTracking(view, ToWorld(firstPerson, grip)), grip, 1e-5));

    // The torso's heading leans toward the hands the short way round.
    double const pi = std::numbers::pi;
    CHECK(NearD(mevr::ik::BlendYaw(0.0, 1.0, 0.5), 0.5));
    CHECK(NearD(std::abs(mevr::ik::BlendYaw(3.0, -3.0, 0.5)), pi, 1e-9));  // across the seam, not through 0
    CHECK(NearD(mevr::ik::BlendYaw(0.3, 2.0, 0.0), 0.3) && NearD(mevr::ik::BlendYaw(0.3, 2.0, 1.0), 2.0));
}


} // namespace

void TestHandFrame() {
    using namespace mevr::ik;
    auto const same = [](Vec3 a, Vec3 b) { return Length(a - b) < 1e-6; };
    // A hand bone whose axes are the anatomical ones: fingers +X, thumb side +Y.
    auto const plain = AnatomyInHand({9.0, 0.0, 0.0}, {8.0, 2.0, 0.5}, {8.0, -2.0, 0.5});
    CHECK(plain.has_value());
    CHECK(same(Rotate(*plain, {1.0, 0.0, 0.0}), {1.0, 0.0, 0.0}));
    CHECK(same(Rotate(*plain, {0.0, 1.0, 0.0}), {0.0, 1.0, 0.0}));
    // The fingers follow where the grip points, the thumb side its up, with no trim.
    Quat const grip = Normalize(Quat{0.1, -0.3, 0.2, 0.9});
    Quat const hand = HandFromGrip(grip, Quat{}, *plain);
    CHECK(same(Rotate(hand, {1.0, 0.0, 0.0}), Rotate(grip, {1.0, 0.0, 0.0})));
    CHECK(same(Rotate(hand, {0.0, 1.0, 0.0}), Rotate(grip, {0.0, 0.0, 1.0})));
    // A bone with other axes (fingers along -Z, thumb side +X): its fingers still follow the grip.
    auto const turned = AnatomyInHand({0.0, 0.0, -9.0}, {2.0, 0.3, -8.0}, {-2.0, 0.3, -8.0});
    CHECK(turned.has_value());
    Quat const hand2 = HandFromGrip(grip, Quat{}, *turned);
    CHECK(same(Rotate(hand2, {0.0, 0.0, -1.0}), Rotate(grip, {1.0, 0.0, 0.0})));
    CHECK(same(Rotate(hand2, {1.0, 0.0, 0.0}), Rotate(grip, {0.0, 0.0, 1.0})));
    // Degenerate fingers: no frame.
    CHECK(!AnatomyInHand({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}).has_value());
    CHECK(!AnatomyInHand({9.0, 0.0, 0.0}, {9.0, 0.0, 0.0}, {8.0, 0.0, 0.0}).has_value());
    // Trims: +pitch tilts the fingers down, +yaw turns them right (+Y), +roll the top to the right.
    CHECK(Rotate(GripTrim(30.0, 0.0, 0.0), {1.0, 0.0, 0.0}).z < -0.4);
    CHECK(Rotate(GripTrim(0.0, 30.0, 0.0), {1.0, 0.0, 0.0}).y > 0.4);
    CHECK(Rotate(GripTrim(0.0, 0.0, 30.0), {0.0, 0.0, 1.0}).y > 0.4);
    CHECK(same(Rotate(GripTrim(0.0, 0.0, 0.0), {1.0, 2.0, 3.0}), {1.0, 2.0, 3.0}));
}

// A held gun's barrel follows the controller's aim (core/ik/HandFrame HandForBarrel).
void TestHandForBarrel() {
    using namespace mevr::ik;
    auto const same = [](Vec3 a, Vec3 b, double tolerance = 1e-9) { return Length(a - b) < tolerance; };
    auto const about = [](Vec3 axis, double degrees) {
        Vec3 const a = Normalize(axis);
        double const half = degrees * std::numbers::pi / 360.0;
        return Quat{a.x * std::sin(half), a.y * std::sin(half), a.z * std::sin(half), std::cos(half)};
    };
    // How a gun might sit on a hand bone: barrel and up in the bone's axes (not perpendicular).
    Vec3 const barrel{0.3, -0.9, 0.2};
    Vec3 const gunUp{0.1, 0.25, 0.95};
    // Aims made by turning the hand: the solve finds that very turn, whatever the lengths.
    for (Quat const hand : {Quat{}, about({1, 2, 3}, 40), about({0, 1, 0}, 179), about({-1, 0.3, 0.5}, 250)}) {
        auto const solved = HandForBarrel(barrel, gunUp, Rotate(hand, barrel) * 3.0, Rotate(hand, gunUp) * 0.2);
        CHECK(solved.has_value() && AngleBetween(*solved, hand) < 1e-7);
    }
    // An up that isn't perpendicular to the aim: the barrel goes exactly along the aim, and the
    // gun's up stays in the plane of the aim and the wanted up, on its side.
    Vec3 const aim{0.0, 1.0, 0.0};
    Vec3 const up{0.0, 0.5, 1.0};
    auto const hand = HandForBarrel(barrel, gunUp, aim, up);
    CHECK(hand.has_value());
    CHECK(same(Rotate(*hand, Normalize(barrel)), aim));
    Vec3 const turnedUp = Rotate(*hand, gunUp);
    CHECK(std::abs(turnedUp.x) < 1e-9 && turnedUp.z > 0.0);
    // The gun's own up needn't be perpendicular to its barrel either: only its part across the
    // barrel counts, which ends up straight up here.
    Vec3 const across = Normalize(gunUp - Normalize(barrel) * Dot(gunUp, Normalize(barrel)));
    CHECK(same(Rotate(*hand, across), {0.0, 0.0, 1.0}));
    // No roll information or no direction: nothing.
    CHECK(!HandForBarrel(barrel, barrel * 2.0, aim, up).has_value());
    CHECK(!HandForBarrel(barrel, gunUp, aim, aim).has_value());
    CHECK(!HandForBarrel(barrel, gunUp, {0.0, 0.0, 0.0}, up).has_value());
    CHECK(!HandForBarrel({0.0, 0.0, 0.0}, gunUp, aim, up).has_value());
}

// A held gun: fixed on the controller, its roll from the hand (thumb side), never the model's axes.
void TestGunHold() {
    using namespace mevr::ik;
    double const pi = std::numbers::pi;
    auto const same = [](Vec3 a, Vec3 b, double tolerance = 1e-9) { return Length(a - b) < tolerance; };
    auto const degrees = [pi](Vec3 a, Vec3 b) {
        return std::acos(std::clamp(Dot(Normalize(a), Normalize(b)), -1.0, 1.0)) * 180.0 / pi;
    };
    auto const about = [pi](Vec3 axis, double angle) {
        Vec3 const a = Normalize(axis);
        double const half = angle * pi / 360.0;
        return Quat{a.x * std::sin(half), a.y * std::sin(half), a.z * std::sin(half), std::cos(half)};
    };

    // Overlord's Touch reference: forward pitched 20.6 degrees down from the grip's, the origin
    // about 10 cm ahead of the grip and 3.4 cm below, 7 mm to the side (mirrored per hand).
    double const s = std::sin(20.6 * pi / 180.0), c = std::cos(20.6 * pi / 180.0);
    Transform const right = TouchReferenceInGrip(1);
    Transform const left = TouchReferenceInGrip(0);
    CHECK(same(Rotate(right.rotation, {0.0, 0.0, -1.0}), {0.0, -s, -c}, 1e-12));
    CHECK(same(Rotate(right.rotation, {0.0, 1.0, 0.0}), {0.0, c, -s}, 1e-12));
    CHECK(same(right.translation, {0.007, -0.0342, -0.0960}, 5e-4));
    CHECK(NearD(left.translation.x, -right.translation.x, 1e-12) && NearD(left.translation.z, right.translation.z, 1e-12));
    CHECK(NearD(Length(right.translation), Length(Vec3{0.007, -0.00182941, 0.1019482}), 1e-9));
    // The same rotation in UE-style grip axes (X = -Z, Y = +X, Z = +Y): a 20.6 degree pitch down.
    auto const ue = [](Vec3 v) { return Vec3{-v.z, v.x, v.y}; };
    for (Vec3 const v : {Vec3{0.0, 0.0, -1.0}, Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}}) {
        CHECK(same(ue(Rotate(right.rotation, v)), Rotate(TouchReferenceRotationUe(), ue(v)), 1e-12));
    }

    // A hand bone whose axes are the anatomical ones: the gun's top is the thumb side (+Y).
    Quat const plain{};
    auto const up = GunUpInHand({1.0, 0.0, 0.0}, plain);
    CHECK(up.has_value() && same(*up, {0.0, 1.0, 0.0}));
    // A barrel tilted toward the thumb: the up stays perpendicular to it, on the thumb side.
    auto const tilted = GunUpInHand({1.0, 0.5, 0.0}, plain);
    CHECK(tilted.has_value() && std::abs(Dot(*tilted, Normalize(Vec3{1.0, 0.5, 0.0}))) < 1e-12 && tilted->y > 0.8);
    // The roll trim turns the top to the right (up x barrel), seen from behind.
    auto const rolled = GunUpInHand({1.0, 0.0, 0.0}, plain, 90.0);
    CHECK(rolled.has_value() && same(*rolled, Cross({0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}), 1e-12));
    // No roll information: a barrel along the thumb side, or none.
    CHECK(!GunUpInHand({0.0, 2.0, 0.0}, plain).has_value());
    CHECK(!GunUpInHand({0.0, 0.0, 0.0}, plain).has_value());

    // LE1's numbers (2026-10-08 log): Shepard's right hand frame from its finger bases, and the
    // pistol's and the rifle's grips. Same barrel; their model ups 90 degrees apart.
    auto const hand = AnatomyInHand({2.3, -8.6, 1.8}, {1.8, -8.1, 4.2}, {0.9, -8.1, -2.6});
    CHECK(hand.has_value());
    Vec3 const pistolBarrel{0.190, -0.965, -0.181}, pistolModelUp{-0.160, 0.151, -0.975};
    Vec3 const rifleBarrel{0.189, -0.965, -0.179}, rifleModelUp{0.968, 0.214, -0.130};
    auto const pistolUp = GunUpInHand(pistolBarrel, *hand);
    auto const rifleUp = GunUpInHand(rifleBarrel, *hand);
    CHECK(pistolUp.has_value() && rifleUp.has_value());
    CHECK(degrees(*pistolUp, *rifleUp) < 1.0);          // both guns held the same way
    CHECK(degrees(*pistolUp, pistolModelUp) > 170.0);   // the pistol's model up was upside down
    CHECK(std::abs(degrees(*rifleUp, rifleModelUp) - 90.0) < 15.0);  // the rifle's on its side

    // With HandForBarrel: the barrel goes along the aim, the thumb side to the controller's up,
    // and rolling the controller about its aim rolls the hand (and the gun) by the same angle.
    Vec3 const aim = Normalize(Vec3{0.8, 0.3, -0.2});
    Vec3 const aimUp = Normalize(Cross(Cross(aim, Vec3{0.0, 0.0, 1.0}), aim));  // up, across the aim
    auto const held = HandForBarrel(pistolBarrel, *pistolUp, aim, aimUp);
    CHECK(held.has_value() && same(Rotate(*held, Normalize(pistolBarrel)), aim, 1e-9));
    CHECK(same(Rotate(*held, *pistolUp), aimUp, 1e-9));
    Vec3 const thumb = Rotate(*held, Rotate(*hand, {0.0, 1.0, 0.0}));
    CHECK(Dot(thumb, aimUp) > 0.9);  // the thumb side is on top
    for (double roll : {30.0, -75.0, 180.0}) {
        Quat const twist = about(aim, roll);
        auto const turned = HandForBarrel(pistolBarrel, *pistolUp, aim, Rotate(twist, aimUp));
        CHECK(turned.has_value() && AngleBetween(*turned, Normalize(twist * *held)) < 1e-7);
    }
}

bool NearXr(XrVector3f a, XrVector3f b, double tolerance = 1e-4) {
    return NearD(a.x, b.x, tolerance) && NearD(a.y, b.y, tolerance) && NearD(a.z, b.z, tolerance);
}

void TestOverlayPlacement() {
    using namespace mevr::vr::overlay;
    double const pi = std::numbers::pi;

    // Headings: 0 facing -Z, + turned left (about +Y), from any quaternion with that forward.
    CHECK(NearD(HeadingOf({0.0f, 0.0f, 0.0f, 1.0f}), 0.0));
    CHECK(NearD(HeadingOf(AxisAngle(0, 1, 0, 90)), pi / 2.0));
    CHECK(NearD(HeadingOf(AxisAngle(0, 1, 0, -30)), -pi / 6.0));
    CHECK(NearD(HeadingOf(YawRotation(1.0f)), 1.0));
    // Looking straight down keeps the heading of the top of the head.
    CHECK(NearD(HeadingOf(AxisAngle(1, 0, 0, -90)), 0.0));
    CHECK(NearD(WrapAngle(static_cast<float>(1.5 * pi)), -0.5 * pi) && NearD(WrapAngle(static_cast<float>(-pi)), pi));

    // Facing: the quad's front (+Z) points along the normal, its top stays up.
    XrQuaternionf const identity = Facing({0.0f, 0.0f, 1.0f});
    CHECK(NearXr(Rotate(identity, {1.0f, 2.0f, 3.0f}), {1.0f, 2.0f, 3.0f}));
    XrVector3f const n{0.6f, 0.0f, -0.8f};
    XrQuaternionf const faced = Facing(n);
    CHECK(NearXr(Rotate(faced, {0.0f, 0.0f, 1.0f}), n) && Rotate(faced, {0.0f, 1.0f, 0.0f}).y > 0.99f);
    CHECK(NearXr(Rotate(Facing({0.0f, 1.0f, 0.0f}), {0.0f, 0.0f, 1.0f}), {0.0f, 1.0f, 0.0f}));  // up along normal

    // The panel: straight ahead, below eye height, tilted to face the eye.
    PanelSettings settings;
    settings.distance = 1.5f;
    settings.drop = 0.25f;
    settings.followAfter = static_cast<float>(50.0 * pi / 180.0);
    settings.followDelay = 1.0f;
    Panel panel;
    XrPosef pose = panel.Pose(settings);
    CHECK(NearXr(pose.position, {0.0f, -0.25f, -1.5f}));
    double const len = std::sqrt(0.25 * 0.25 + 1.5 * 1.5);
    CHECK(NearXr(Rotate(pose.orientation, {0.0f, 0.0f, 1.0f}),
        {0.0f, static_cast<float>(0.25 / len), static_cast<float>(1.5 / len)}));

    // A 30 degree glance never moves it; 90 degrees for half a second neither.
    XrQuaternionf const glance = AxisAngle(0, 1, 0, 30);
    for (int i = 0; i < 300; ++i) {
        panel.Update(glance, true, settings, 1.0f / 60.0f);
    }
    CHECK(NearD(panel.Yaw(), 0.0) && !panel.Following());
    XrQuaternionf const turned = AxisAngle(0, 1, 0, 90);
    for (int i = 0; i < 30; ++i) {
        panel.Update(turned, true, settings, 1.0f / 60.0f);
    }
    CHECK(NearD(panel.Yaw(), 0.0) && !panel.Following());
    // Held past the delay: it follows, then settles at the head's heading, to the left (-X).
    for (int i = 0; i < 40; ++i) {
        panel.Update(turned, true, settings, 1.0f / 60.0f);
    }
    CHECK(panel.Following());
    for (int i = 0; i < 240; ++i) {
        panel.Update(turned, true, settings, 1.0f / 60.0f);
    }
    CHECK(!panel.Following() && std::abs(panel.Yaw() - pi / 2.0) < 0.04);
    pose = panel.Pose(settings);
    CHECK(NearXr(pose.position, {-1.5f, -0.25f, 0.0f}, 0.07));
    // Losing tracking doesn't count toward following; following off never follows.
    Panel still;
    for (int i = 0; i < 600; ++i) {
        still.Update(turned, false, settings, 1.0f / 60.0f);
    }
    CHECK(!still.Following() && NearD(still.Yaw(), 0.0));
    PanelSettings never = settings;
    never.followAfter = 0.0f;
    for (int i = 0; i < 600; ++i) {
        still.Update(turned, true, never, 1.0f / 60.0f);
    }
    CHECK(!still.Following() && NearD(still.Yaw(), 0.0));
    // Recenter and re-place.
    panel.Reset();
    CHECK(NearD(panel.Yaw(), 0.0));
    panel.FaceHeading(AxisAngle(0, 1, 0, -45));
    CHECK(NearD(panel.Yaw(), -pi / 4.0));

    // The reticle: 10 m along the ray, facing the eye, constant angular size.
    Billboard reticle;
    CHECK(PlaceOnRay({0.2f, -0.3f, -0.4f}, {0.0f, 0.0f, -2.0f}, 10.0f, {0.0f, 0.0f, 0.0f},
        static_cast<float>(2.0 * pi / 180.0), reticle));
    CHECK(NearXr(reticle.pose.position, {0.2f, -0.3f, -10.4f}));
    XrVector3f const p = reticle.pose.position;
    double const range = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    CHECK(NearD(reticle.size, 2.0 * range * std::tan(pi / 180.0), 1e-4));
    CHECK(NearXr(Rotate(reticle.pose.orientation, {0.0f, 0.0f, 1.0f}),
        {static_cast<float>(-p.x / range), static_cast<float>(-p.y / range), static_cast<float>(-p.z / range)}));
    CHECK(!PlaceOnRay({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 10.0f, {0.0f, 0.0f, 0.0f}, 0.03f, reticle));
}

void TestMenuPointer() {
    using namespace mevr::vr::menu_pointer;
    Quad quad;
    quad.head.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    quad.head.position = {0.0f, 1.6f, 0.0f};
    quad.center = {0.0f, 0.0f, -1.35f};
    quad.width = 0.88f;
    quad.height = 0.70f;
    Hit hit;
    // Straight ahead from in front of the head: the middle.
    CHECK(Intersect(quad, {0.0f, 1.6f, -0.3f}, {0.0f, 0.0f, -1.0f}, hit));
    CHECK(NearD(hit.u, 0.5) && NearD(hit.v, 0.5) && NearD(hit.distance, 1.05));
    // Toward the upper-left quarter point: u and v count from the top-left corner.
    CHECK(Intersect(quad, {0.0f, 1.6f, 0.0f}, {-0.22f, 0.175f, -1.35f}, hit));
    CHECK(NearD(hit.u, 0.25) && NearD(hit.v, 0.25));
    // Outside the quad, pointing away, from behind it, or no direction: no hit.
    CHECK(!Intersect(quad, {0.0f, 1.6f, 0.0f}, {0.5f, 0.0f, -1.0f}, hit));
    CHECK(!Intersect(quad, {0.0f, 1.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, hit));
    CHECK(!Intersect(quad, {0.0f, 1.6f, -2.0f}, {0.0f, 0.0f, 1.0f}, hit));
    CHECK(!Intersect(quad, {0.0f, 1.6f, 0.0f}, {0.0f, 0.0f, 0.0f}, hit));
    // The quad follows the head: turned 90 degrees left, it sits along -X.
    quad.head.orientation = AxisAngle(0, 1, 0, 90);
    CHECK(Intersect(quad, {0.0f, 1.6f, 0.0f}, {-1.0f, 0.0f, 0.0f}, hit));
    CHECK(NearD(hit.u, 0.5) && NearD(hit.v, 0.5) && NearD(hit.distance, 1.35));
    // Angled toward the head's right (-Z in the tracking space here): right of the middle.
    CHECK(Intersect(quad, {0.0f, 1.6f, 0.0f}, {-1.35f, 0.0f, -0.2f}, hit) && hit.u > 0.6f);
    // An aim pose's forward is its -Z.
    XrPosef aim{};
    aim.orientation = AxisAngle(0, 1, 0, 90);
    XrVector3f const forward = AimDirection(aim);
    CHECK(NearD(forward.x, -1.0) && NearD(forward.z, 0.0));
}

int main() {
    TestBoneNames();
    TestIk();
    TestRigid();
    TestOrientation();
    TestArmChains();
    TestArmPose();
    TestArmStretch();
    TestTorsoPose();
    TestArmLength();
    TestReachScale();
    TestHandTargetsOrigin();
    TestViewSync();
    TestXrToUnreal();
    TestHeadSim();
    TestOffsets();
    TestTrackingFrame();
    TestToTracking();
    TestHandFrame();
    TestHandForBarrel();
    TestGunHold();
    TestGamepad();
    TestOverlayPlacement();
    TestMenuPointer();
    if (gFailures == 0) {
        std::printf("all tests passed\n");
    }
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

// Offline tests for the math that has to be right before it reaches a headset. No game, no
// runtime: `ctest -C Release --test-dir build`.

#include "core/PngFile.hpp"
#include "core/ik/ArmLength.hpp"
#include "core/ik/ArmPose.hpp"
#include "core/ik/BodySlots.hpp"
#include "core/ik/BoneNames.hpp"
#include "core/ik/CameraHook.hpp"
#include "core/ik/HandFrame.hpp"
#include "core/ik/Ik.hpp"
#include "core/ik/Rigid.hpp"
#include "core/ik/TorsoPose.hpp"
#include "core/ik/TouchReference.hpp"
#include "core/ik/TwoHand.hpp"
#include "core/vr/Haptics.hpp"
#include "core/vr/ImageConvert.hpp"
#include "core/vr/LayerComposite.hpp"
#include "core/vr/MenuPointer.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "core/vr/PoseRecording.hpp"
#include "core/vr/Recenter.hpp"
#include "core/vr/Sights.hpp"
#include "core/vr/SquadMenu.hpp"
#include "core/vr/TestRequest.hpp"
#include "core/vr/ViewSync.hpp"
#include "core/vr/WheelCrop.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/GamepadMapping.hpp"
#include "game/HeadSim.hpp"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
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

void TestSupportGrip() {
    using namespace mevr::game::gamepad;
    SupportInput onGrip{0.05f, 0.12f, 0.22f};
    SupportInput between{0.18f, 0.12f, 0.22f};
    SupportInput away{0.30f, 0.12f, 0.22f};
    SupportInput none{};  // no gun

    // A new squeeze near the foregrip holds; between acquire and release it keeps holding; past
    // release it lets go, and nothing (no LB) until the grip opens.
    SupportGrip grip;
    CHECK(UpdateSupportGrip(grip, false, onGrip) == LeftGrip::Open);
    CHECK(UpdateSupportGrip(grip, true, onGrip) == LeftGrip::Support);
    CHECK(UpdateSupportGrip(grip, true, between) == LeftGrip::Support);
    CHECK(UpdateSupportGrip(grip, true, away) == LeftGrip::Held);
    CHECK(UpdateSupportGrip(grip, true, onGrip) == LeftGrip::Held);  // back in range: no re-grab while squeezed
    CHECK(UpdateSupportGrip(grip, false, onGrip) == LeftGrip::Open);
    CHECK(UpdateSupportGrip(grip, true, onGrip) == LeftGrip::Support);  // a new squeeze

    // A squeeze between acquire and release (or away from the gun) is LB, and stays LB even when
    // the hand then moves onto the foregrip: only a new squeeze grabs.
    grip = {};
    CHECK(UpdateSupportGrip(grip, true, between) == LeftGrip::Shoulder);
    CHECK(UpdateSupportGrip(grip, true, onGrip) == LeftGrip::Shoulder);
    CHECK(UpdateSupportGrip(grip, false, onGrip) == LeftGrip::Open);
    grip = {};
    CHECK(UpdateSupportGrip(grip, true, none) == LeftGrip::Shoulder);

    // The gun goes away while held (holstered, arm IK off): let go, no LB.
    grip = {};
    CHECK(UpdateSupportGrip(grip, true, onGrip) == LeftGrip::Support);
    CHECK(UpdateSupportGrip(grip, true, none) == LeftGrip::Held);
    // Bad thresholds (release below acquire) mean no gun to hold.
    grip = {};
    CHECK(UpdateSupportGrip(grip, true, SupportInput{0.05f, 0.2f, 0.1f}) == LeftGrip::Shoulder);
    grip = {};
    CHECK(UpdateSupportGrip(grip, true, SupportInput{std::nanf(""), 0.12f, 0.22f}) == LeftGrip::Shoulder);

    // Through the pad: held = LT fully pulled and no LB; the left trigger setting.
    mevr::vr::controllers::State state;
    Latches latches;
    Options options;
    options.support = onGrip;
    state.left.squeeze = 1.0f;
    state.left.trigger = 0.4f;
    auto pad = FromControllers(state, latches, options);
    CHECK(latches.left == LeftGrip::Support);
    CHECK(pad.bLeftTrigger == 255 && (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0);
    options.supportAims = false;  // a sniper rifle: steadies only
    pad = FromControllers(state, latches, options);
    CHECK(pad.bLeftTrigger == 102 && (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0);
    options.leftTrigger = false;  // the left trigger freed
    pad = FromControllers(state, latches, options);
    CHECK(pad.bLeftTrigger == 0);
    options.supportAims = true;
    pad = FromControllers(state, latches, options);
    CHECK(pad.bLeftTrigger == 255);  // the grip still aims with the trigger freed
    // Grip opens, then a squeeze away from the gun: LB, and the trigger is still not LT.
    state.left.squeeze = 0.0f;
    FromControllers(state, latches, options);
    state.left.squeeze = 1.0f;
    options.support = away;
    pad = FromControllers(state, latches, options);
    CHECK(latches.left == LeftGrip::Shoulder && (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0 && pad.bLeftTrigger == 0);
    // Left grip + menu is still Back while the grip holds the gun.
    state = {};
    latches = {};
    options = {};
    options.support = onGrip;
    state.left.squeeze = 1.0f;
    state.left.menu = true;
    pad = FromControllers(state, latches, options);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_BACK) != 0);

    // The grip only takes the gun (gripShoulder off): squeezed away from it, no LB.
    state = {};
    latches = {};
    options = {};
    options.gripShoulder = false;
    options.support = away;
    state.left.squeeze = 1.0f;
    pad = FromControllers(state, latches, options);
    CHECK(latches.left == LeftGrip::Shoulder && (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0);
    // The left trigger as LB (the weapon wheel): pulled = LB, and no LT.
    state = {};
    latches = {};
    options = {};
    options.gripShoulder = false;
    options.leftTriggerShoulder = true;
    state.left.trigger = 1.0f;
    pad = FromControllers(state, latches, options);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0 && pad.bLeftTrigger == 0);
    state.left.trigger = 0.0f;
    pad = FromControllers(state, latches, options);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0);
    // ...and across the host boundary.
    {
        Options wheel;
        wheel.gripShoulder = false;
        wheel.leftTriggerShoulder = true;
        Options const back = FromFlags(0.0f, 0.12f, 0.22f, ToFlags(wheel));
        CHECK(!back.gripShoulder && back.leftTriggerShoulder);
    }

    // The flags across the host boundary.
    Options flagged;
    flagged.leftTrigger = false;
    flagged.supportAims = true;
    Options const back = FromFlags(0.1f, 0.12f, 0.22f, ToFlags(flagged));
    CHECK(!back.leftTrigger && back.supportAims && back.support.distance == 0.1f);
    CHECK(ToFlags(Options{}) == (kFlagLeftTrigger | kFlagSupportAims | kFlagGripShoulder));
}

void TestTwoHand() {
    using namespace mevr::ik;
    constexpr double kPi = std::numbers::pi;
    auto const closeTo = [](Vec3 a, Vec3 b, double tolerance = 1e-6) { return Length(a - b) < tolerance; };

    // Slerp: ends, middle, and the short way round.
    Quat const quarter = FromTo({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    CHECK(AngleBetween(Slerp({}, quarter, 0.0), {}) < 1e-9);
    CHECK(AngleBetween(Slerp({}, quarter, 1.0), quarter) < 1e-9);
    CHECK(std::abs(AngleBetween(Slerp({}, quarter, 0.5), {}) - kPi / 4.0) < 1e-9);
    Quat const flipped{-quarter.x, -quarter.y, -quarter.z, -quarter.w};
    CHECK(std::abs(AngleBetween(Slerp({}, flipped, 0.5), {}) - kPi / 4.0) < 1e-9);

    // A rifle in the right hand: barrel along hand X, foregrip 30 units ahead and 5 below.
    Vec3 const span{30.0, 0.0, -5.0};
    Quat const rear{};  // hand axes = component axes
    Vec3 const rearAt{0.0, 0.0, 0.0};
    // The left hand exactly where the one-hand pose has the foregrip: no swing.
    auto same = TwoHandRotation(rear, span, rearAt, span, 8.0);
    CHECK(same && AngleBetween(*same, rear) < 1e-9);
    // The left hand moved up and left: the span now points at it; the barrel turned with it.
    Vec3 const left{20.0, 15.0, 10.0};
    auto turned = TwoHandRotation(rear, span, rearAt, left, 8.0);
    CHECK(turned.has_value());
    if (turned) {
        Vec3 const spanNow = Rotate(*turned, span);
        CHECK(closeTo(Normalize(spanNow), Normalize(left)));
        // Roll stays the right hand's: a swing about an axis perpendicular to both directions,
        // so the hand's axis perpendicular to that plane (the swing axis) doesn't move.
        Vec3 const axis = Normalize(Cross(span, left));
        CHECK(closeTo(Rotate(*turned, axis), axis));
    }
    // The barrel follows the line between the hands: rotating the right hand rotates the span,
    // and the result still points the span at the left hand, keeping the new roll.
    Quat const rolled = FromTo({0.0, 1.0, 0.0}, {0.0, 0.0, 1.0});  // 90 degrees about X
    auto rolledTurn = TwoHandRotation(rolled, span, rearAt, left, 8.0);
    CHECK(rolledTurn && closeTo(Normalize(Rotate(*rolledTurn, span)), Normalize(left)));
    // Hands too close (or on top of each other): no axis, keep the right hand's aim.
    CHECK(!TwoHandRotation(rear, span, rearAt, {5.0, 0.0, 0.0}, 8.0));
    CHECK(!TwoHandRotation(rear, {0.0, 0.0, 0.0}, rearAt, left, 8.0));
    CHECK(!TwoHandRotation(rear, span, rearAt, {std::nan(""), 0.0, 0.0}, 8.0));
    // Opposite: the left hand behind the right turns the gun around (a well-defined half turn).
    auto behind = TwoHandRotation(rear, {30.0, 0.0, 0.0}, rearAt, {-30.0, 0.0, 0.0}, 8.0);
    CHECK(behind && closeTo(Normalize(Rotate(*behind, {1.0, 0.0, 0.0})), {-1.0, 0.0, 0.0}));

    // Foregrip candidates: on a rifle whose muzzle is 60 units ahead.
    Vec3 const barrel{1.0, 0.0, 0.0};
    Vec3 const muzzle{60.0, 0.0, 4.0};
    CHECK(OnGun({30.0, 0.0, -5.0}, barrel, muzzle));
    CHECK(!OnGun({1.0, 1.0, 1.0}, barrel, muzzle));           // on the right hand itself
    // The gun at the shoulder (OpenXR axes: +Y up, -Z forward).
    {
        Vec3 const head{0.0, 1.6, 0.0}, ahead{0.0, 0.0, -1.0}, up{0.0, 1.0, 0.0};
        CHECK(Shouldered(head, ahead, up, {0.12, 1.42, -0.15}, false));    // cheek weld: right of and under the eyes
        CHECK(!Shouldered(head, ahead, up, {0.25, 1.05, -0.30}, false));   // at the hip
        CHECK(!Shouldered(head, ahead, up, {0.15, 1.40, -0.60}, false));   // held out at arm's length
        CHECK(!Shouldered(head, ahead, up, {0.12, 1.62, -0.15}, false));   // at eye height, not under it
        CHECK(Shouldered(head, ahead, up, {0.12, 1.21, -0.15}, true));     // just past the bound, already in: kept
        CHECK(!Shouldered(head, ahead, up, {0.12, 1.21, -0.15}, false));   // ...but not entered
        CHECK(Shouldered(head, {0.0, -0.5, -0.8}, up, {0.12, 1.42, -0.15}, false));  // looking down the sights
        CHECK(!Shouldered(head, up, up, {0.12, 1.42, -0.15}, false));      // straight up: no facing
    }
    CHECK(!OnGun({30.0, 40.0, 0.0}, barrel, muzzle));         // far off the barrel (a hand at the side)
    CHECK(!OnGun({80.0, 0.0, 4.0}, barrel, muzzle));          // past the muzzle
    CHECK(OnGun({-10.0, 0.0, -10.0}, barrel, muzzle));        // a pistol cupped from just behind and below
    CHECK(!OnGun({-40.0, 0.0, 0.0}, barrel, muzzle));         // far behind the grip
    GunOffsets const o = OffsetsOnGun({30.0, 0.0, -5.0}, barrel, muzzle);
    CHECK(std::abs(o.along - 30.0) < 1e-9 && std::abs(o.off - 9.0) < 1e-9);
    // Under the grip: down against the thumb side, a little forward, whatever the lengths.
    Vec3 const under = UnderGrip({2.0, 0.0, 0.0}, {0.0, 0.0, 3.0});
    CHECK(closeTo(under, {2.0, 0.0, -8.0}));
    CHECK(OnGun(under, barrel, muzzle));

    // The native off-hand IK's state from its strength.
    CHECK(ClassifyNativeIk(0.0) == NativeIk::Off && ClassifyNativeIk(0.01) == NativeIk::Off);
    CHECK(ClassifyNativeIk(0.5) == NativeIk::Blending && ClassifyNativeIk(0.98) == NativeIk::Blending);
    CHECK(ClassifyNativeIk(0.99) == NativeIk::On && ClassifyNativeIk(1.0) == NativeIk::On);
    CHECK(ClassifyNativeIk(std::nan("")) == NativeIk::Unknown);
    CHECK(std::string(Name(NativeIk::On)) == "on" && std::string(Name(NativeIk::Unknown)) == "unknown");
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

// LE1's camera hook bones (research/18): found by name, outside what the torso and arms write, and
// GetCameraLocation's formula.
void TestCameraHook() {
    using namespace mevr::ik;
    std::vector<std::string> const names{"God", "Root", "Pelvis", "CAMERA", "Camera_End", "Godot", "camera"};
    CHECK(FindCameraHookBones(names) == (std::vector<int>{0, 3, 6}));
    CHECK(FindCameraHookBones(std::vector<std::string>{}).empty());

    // A torso rig with a root above it and a Camera bone beside Root, as on Shepard (God > Root,
    // God > Camera): the spine, the clavicles and the arms never reach it, and a full solve
    // leaves it where it was.
    TorsoRig rig;
    std::vector<std::string> names2{"God"};
    std::vector<int> parents{-1};
    std::vector<Transform> pose{{AxisAngleQ(0, 0, 1, 5), {0, 0, 0}, 1.0}};
    for (std::size_t i = 0; i < rig.names.size(); ++i) {
        names2.push_back(rig.names[i]);
        parents.push_back(rig.parents[i] + 1);
        pose.push_back(rig.pose[i]);
    }
    names2.push_back("Camera");
    parents.push_back(0);
    pose.push_back({AxisAngleQ(0, 1, 0, 30), {5, 0, 170}, 1.0});
    int const camera = static_cast<int>(names2.size()) - 1;
    auto const left = FindArmChain(names2, parents, 0);
    auto const right = FindArmChain(names2, parents, 1);
    CHECK(left && right);
    auto const chain = FindTorsoChain(names2, parents, *left, *right);
    CHECK(chain.has_value());
    std::vector<int> const hooks = FindCameraHookBones(names2);
    CHECK(hooks == (std::vector<int>{0, camera}));
    std::vector<int> const roots{chain->spine.front(), chain->collars[0], chain->collars[1], left->upper, right->upper};
    CHECK(InSubtrees(parents, roots, hooks).empty());
    CHECK(InSubtrees(parents, std::vector<int>{0}, hooks) == hooks);  // a whole-body move would reach both
    CHECK(InSubtrees(parents, std::vector<int>{1}, hooks).empty());   // Root's subtree: not them
    CHECK(InSubtrees(parents, roots, std::vector<int>{left->hand, -1, 999}) == (std::vector<int>{left->hand}));
    std::vector<Transform> const before = pose;
    TorsoGoal goal;
    goal.reference = {16, 17};  // the eyes, shifted by the God bone in front
    goal.head = (before[16].translation + before[17].translation) * 0.5 + Vec3{40, 10, -30};
    goal.forward = Rotate(AxisAngleQ(0, 0, 1, 40), {1, 0, 0});
    TorsoResult const r = PoseTorso(pose, parents, *chain, *left, *right, BodyFrame{}, goal);
    CHECK(r.ok && r.bonesWritten > 0);
    CHECK(NearT(pose[camera], before[camera], 0.0) && NearT(pose[0], before[0], 0.0));
    CHECK(Length(pose[left->hand].translation - before[left->hand].translation) > 1.0);

    // GetCameraLocation: the hook plus HookOffset turned by yaw only, minus Offset turned by
    // pitch and yaw. Level, facing +X: the combat camera's defaults.
    double const quarter = std::numbers::pi / 2.0;
    CHECK(NearV(CameraFromHook({0, 0, 0}, {-30, 20, 90}, {80, -44, 15}, 0.0, 0.0), {-110, 64, 75}, 1e-9));
    CHECK(NearV(CameraFromHook({1, 2, 3}, {-30, 20, 90}, {80, -44, 15}, 0.0, quarter), Vec3{1, 2, 3} + Vec3{-64, -110, 75}, 1e-9));
    // Looking straight up: Offset's forward goes up, HookOffset stays level.
    CHECK(NearV(CameraFromHook({0, 0, 0}, {10, 0, 0}, {10, 0, 0}, quarter, 0.0), {10, 0, -10}, 1e-9));
    // In the view's level axes: facing +Y, +Y is forward and -X is right.
    CHECK(NearV(InViewAxes({0, 5, 0}, quarter), {5, 0, 0}, 1e-9));
    CHECK(NearV(InViewAxes({-5, 0, 2}, quarter), {0, 5, 2}, 1e-9));
    CHECK(NearV(InViewAxes({3, 4, 0}, 0.0), {3, 4, 0}, 1e-12));

    // The log's change test.
    CameraModeKey const a{1, 7, {-30, 20, 90}, {80, -44, 15}};
    CameraModeKey b = a;
    CHECK(!CameraModeChanged(a, b));
    b.offset.x += 0.4;
    CHECK(!CameraModeChanged(a, b));
    b.offset.x += 0.2;
    CHECK(CameraModeChanged(a, b));
    b = a;
    b.hookOffset.z -= 45;  // crouch
    CHECK(CameraModeChanged(a, b));
    b = a;
    b.mode = 2;
    CHECK(CameraModeChanged(a, b));
    b = a;
    b.hookName = 8;
    CHECK(CameraModeChanged(a, b));
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
    XrPosef head{};
    head.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    head.position = {0.0f, 1.6f, 0.0f};
    Quad quad = HeadLocked(head, {0.0f, 0.0f, -1.35f}, 0.88f, 0.70f);
    CHECK(NearXr(quad.pose.position, {0.0f, 1.6f, -1.35f}));
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
    // Head-locked, the quad follows the head: turned 90 degrees left, it sits along -X.
    head.orientation = AxisAngle(0, 1, 0, 90);
    quad = HeadLocked(head, {0.0f, 0.0f, -1.35f}, 0.88f, 0.70f);
    CHECK(Intersect(quad, {0.0f, 1.6f, 0.0f}, {-1.0f, 0.0f, 0.0f}, hit));
    CHECK(NearD(hit.u, 0.5) && NearD(hit.v, 0.5) && NearD(hit.distance, 1.35));
    // Angled toward the head's right (-Z in the tracking space here): right of the middle.
    CHECK(Intersect(quad, {0.0f, 1.6f, 0.0f}, {-1.35f, 0.0f, -0.2f}, hit) && hit.u > 0.6f);

    // World-locked: a quad submitted in the tracking space itself (identity space pose). Placed
    // 1.5 m along +X from a player who faced +X (yawed -90 degrees), upright, facing back at them.
    XrPosef const identity{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrPosef placed{};
    placed.orientation = AxisAngle(0, 1, 0, -90);
    placed.position = {1.5f, 1.6f, 0.2f};
    Quad const world = InTracking(identity, placed, 1.0f, 0.5f);
    CHECK(NearXr(world.pose.position, {1.5f, 1.6f, 0.2f}));
    // A controller beside the head, pointing at the middle: u, v = 0.5, wherever the head looks.
    CHECK(Intersect(world, {0.3f, 1.6f, 0.2f}, {1.0f, 0.0f, 0.0f}, hit));
    CHECK(NearD(hit.u, 0.5) && NearD(hit.v, 0.5) && NearD(hit.distance, 1.2));
    // Its right edge is toward +Z (the player's right when facing +X), its top is up.
    CHECK(Intersect(world, {0.3f, 1.6f, 0.2f}, {1.2f, 0.2f, 0.4f}, hit));
    CHECK(NearD(hit.u, 0.9) && NearD(hit.v, 0.1));
    // From behind (past the quad) or beside it: no hit.
    CHECK(!Intersect(world, {2.0f, 1.6f, 0.2f}, {-1.0f, 0.0f, 0.0f}, hit));
    CHECK(!Intersect(world, {0.3f, 1.6f, 1.5f}, {1.0f, 0.0f, 0.0f}, hit));
    // A VIEW-space quad through InTracking is the same as HeadLocked.
    XrPosef local{};
    local.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    local.position = {0.1f, -0.2f, -1.35f};
    Quad const a = InTracking(head, local, 0.88f, 0.70f);
    Quad const b = HeadLocked(head, local.position, 0.88f, 0.70f);
    CHECK(NearXr(a.pose.position, b.pose.position) && NearD(a.pose.orientation.y, b.pose.orientation.y));
    // Composition: the head's rotation applies to the offset and to the quad's own orientation.
    CHECK(NearXr(a.pose.position, {-1.35f, 1.4f, -0.1f}));
    CHECK(NearXr(mevr::vr::overlay::Rotate(a.pose.orientation, {0.0f, 0.0f, 1.0f}), {1.0f, 0.0f, 0.0f}));
    // An aim pose's forward is its -Z.
    XrPosef aim{};
    aim.orientation = AxisAngle(0, 1, 0, 90);
    XrVector3f const forward = AimDirection(aim);
    CHECK(NearD(forward.x, -1.0) && NearD(forward.z, 0.0));
}

XrPosef HeadAt(double yawDeg, XrVector3f position, double pitchDeg = 0.0) {
    XrQuaternionf const yaw = AxisAngle(0, 1, 0, yawDeg);
    XrQuaternionf const pitch = AxisAngle(1, 0, 0, pitchDeg);
    XrPosef head{};
    head.orientation = {yaw.w * pitch.x + yaw.x * pitch.w + yaw.y * pitch.z - yaw.z * pitch.y,
        yaw.w * pitch.y - yaw.x * pitch.z + yaw.y * pitch.w + yaw.z * pitch.x,
        yaw.w * pitch.z + yaw.x * pitch.y - yaw.y * pitch.x + yaw.z * pitch.w,
        yaw.w * pitch.w - yaw.x * pitch.x - yaw.y * pitch.y - yaw.z * pitch.z};  // yaw * pitch
    head.position = position;
    return head;
}

void TestWorldPanel() {
    using namespace mevr::vr::overlay;
    double const pi = std::numbers::pi;
    float const step = 1.0f / 60.0f;
    WorldPanelSettings const settings;  // 70 degrees for a second, 5 frames' gap
    XrVector3f const local{0.0f, 0.0f, -1.4f};  // Halcyon's flat screen, head-locked

    // Appears: in front of the head where it is, its yaw only (looking down 40 degrees, turned 30 left).
    WorldPanel panel;
    std::uint64_t frame = 100;
    XrPosef head = HeadAt(30, {0.1f, 1.7f, -0.2f}, -40);
    CHECK(panel.Update(1, frame, head, true, settings, step) == Placement::Appeared);
    XrPosef const pose = panel.PoseOf(local);
    double const s30 = std::sin(pi / 6.0), c30 = std::cos(pi / 6.0);
    CHECK(NearXr(pose.position, {static_cast<float>(0.1 - 1.4 * s30), 1.7f, static_cast<float>(-0.2 - 1.4 * c30)}));
    CHECK(Rotate(pose.orientation, {0.0f, 1.0f, 0.0f}).y > 0.9999f);  // level, upright
    CHECK(NearXr(Rotate(pose.orientation, {0.0f, 0.0f, 1.0f}), {static_cast<float>(s30), 0.0f, static_cast<float>(c30)}));
    XrVector3f const placedAt = pose.position;

    // Looking around within 70 degrees and leaning (6DOF): it stays put, so leaning gives parallax.
    for (int i = 0; i < 300; ++i) {
        head = HeadAt(30 + 60 * std::sin(i * 0.05), {0.1f + 0.3f * static_cast<float>(std::sin(i * 0.1)), 1.7f, -0.2f});
        CHECK(panel.Update(1, ++frame, head, true, settings, step) == Placement::Kept);
    }
    CHECK(NearXr(panel.PoseOf(local).position, placedAt) && !panel.Following());

    // Turned past 70 degrees for a moment: still put. Held past a second: it eases round, eyes and all.
    head = HeadAt(30 + 100, {0.5f, 1.7f, 0.3f});
    for (int i = 0; i < 30; ++i) {
        CHECK(panel.Update(1, ++frame, head, true, settings, step) == Placement::Kept);
    }
    CHECK(NearXr(panel.PoseOf(local).position, placedAt));
    bool followed = false;
    for (int i = 0; i < 40; ++i) {
        followed = panel.Update(1, ++frame, head, true, settings, step) == Placement::Followed || followed;
    }
    CHECK(followed && panel.Following());
    for (int i = 0; i < 300; ++i) {
        panel.Update(1, ++frame, head, true, settings, step);
    }
    CHECK(!panel.Following() && std::abs(WrapAngle(panel.Yaw() - static_cast<float>(130.0 * pi / 180.0))) < 0.04f);
    CHECK(NearXr(panel.Anchor(), {0.5f, 1.7f, 0.3f}, 0.03));

    // Hidden for a while, then shown again: placed in front of the head anew.
    frame += 20;
    head = HeadAt(-10, {0.0f, 1.6f, 0.0f});
    CHECK(panel.Update(1, frame, head, true, settings, step) == Placement::Appeared);
    CHECK(NearXr(panel.Anchor(), {0.0f, 1.6f, 0.0f}) && NearD(panel.Yaw(), -10.0 * pi / 180.0));
    // A blink of a frame or two (within gapFrames) doesn't re-place it.
    head = HeadAt(20, {0.2f, 1.6f, 0.0f});
    frame += 3;
    CHECK(panel.Update(1, frame, head, true, settings, step) == Placement::Kept);
    CHECK(NearD(panel.Yaw(), -10.0 * pi / 180.0));
    // Another kind of panel takes the slot (a cutscene becomes a conversation): re-placed.
    CHECK(panel.Update(2, ++frame, head, true, settings, step) == Placement::KindChanged);
    CHECK(NearD(panel.Yaw(), 20.0 * pi / 180.0) && NearXr(panel.Anchor(), {0.2f, 1.6f, 0.0f}));
    // ... unless the head hasn't moved: then it stays exactly where it was.
    head = HeadAt(22, {0.21f, 1.6f, 0.0f});
    CHECK(panel.Update(1, ++frame, head, true, settings, step) == Placement::Kept);
    CHECK(NearD(panel.Yaw(), 20.0 * pi / 180.0));
    // Recenter: placed afresh next frame, even with the head where it was.
    panel.Recenter();
    CHECK(panel.Update(1, ++frame, head, true, settings, step) == Placement::Recentered);
    CHECK(NearD(panel.Yaw(), 22.0 * pi / 180.0));

    // No head when it appears: placed at the origin facing -Z, then in front of the head once found.
    WorldPanel blind;
    CHECK(blind.Update(0, 5, head, false, settings, step) == Placement::Appeared);
    CHECK(NearXr(blind.PoseOf(local).position, {0.0f, 0.0f, -1.4f}));
    CHECK(blind.Update(0, 6, head, false, settings, step) == Placement::Kept);
    CHECK(blind.Update(0, 7, head, true, settings, step) == Placement::HeadFound);
    CHECK(NearD(blind.Yaw(), 22.0 * pi / 180.0));
    // Following off: never follows.
    WorldPanelSettings never = settings;
    never.followAfter = 0.0f;
    head = HeadAt(150, {0.0f, 1.6f, 0.0f});
    for (std::uint64_t f = 8; f < 600; ++f) {
        CHECK(blind.Update(0, f, head, true, never, step) == Placement::Kept);
    }
    CHECK(NearD(blind.Yaw(), 22.0 * pi / 180.0));
    // Offsets in the panel's frame (Halcyon's menu offsets): right is the panel's right.
    WorldPanel facingX;
    facingX.Update(0, 1, HeadAt(-90, {0.0f, 1.6f, 0.0f}), true, settings, step);
    CHECK(NearXr(facingX.PoseOf({0.3f, -0.1f, -1.0f}).position, {1.0f, 1.5f, 0.3f}));
}

// ---- Test loop: recordings, snapshot images, request files ------------------------------------

mevr::vr::recording::Frame RecordedFrame(std::int64_t timeNs, float x) {
    mevr::vr::recording::Frame frame;
    frame.timeNs = timeNs;
    frame.headValid = true;
    frame.views[0].pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {x - 0.032f, 1.6f, 0.0f}};
    frame.views[1].pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {x + 0.032f, 1.6f, 0.0f}};
    frame.views[0].fov = {-0.9f, 0.8f, 0.85f, -0.95f};
    frame.views[1].fov = {-0.8f, 0.9f, 0.85f, -0.95f};
    frame.controllersActive = true;
    for (int h = 0; h < 2; ++h) {
        auto& hand = frame.hands[h];
        hand.gripValid = hand.aimValid = true;
        hand.grip = {AxisAngle(0, 1, 0, 10.0 * h), {x + (h == 0 ? -0.2f : 0.2f), 1.1f, -0.3f}};
        hand.aim = {AxisAngle(1, 0, 0, -20.0), {x, 1.2f, -0.35f}};
        hand.trigger = 0.25f * static_cast<float>(h + 1);
        hand.squeeze = 0.5f;
        hand.stickX = -0.5f;
        hand.stickY = 1.0f;
        hand.primary = h == 1;
        hand.stickClick = h == 0;
        hand.menu = h == 0;
    }
    return frame;
}

bool SamePose(XrPosef const& a, XrPosef const& b, double tolerance = 1e-5) {
    return NearD(a.orientation.x, b.orientation.x, tolerance) && NearD(a.orientation.y, b.orientation.y, tolerance) &&
        NearD(a.orientation.z, b.orientation.z, tolerance) && NearD(a.orientation.w, b.orientation.w, tolerance) &&
        NearD(a.position.x, b.position.x, tolerance) && NearD(a.position.y, b.position.y, tolerance) &&
        NearD(a.position.z, b.position.z, tolerance);
}

void TestPoseRecording() {
    using namespace mevr::vr::recording;
    std::vector<std::uint8_t> bytes;
    EncodeHeader(1234567890123, bytes);
    CHECK(bytes.size() == kHeaderBytes);
    Frame const a = RecordedFrame(0, 0.0f);
    Frame b = RecordedFrame(10'000'000, 1.0f);  // 10 ms later, 1 m to the right
    b.hands[1].aimValid = false;
    b.hands[0].primary = true;
    Frame c = RecordedFrame(20'000'000, 2.0f);
    c.headValid = false;
    EncodeFrame(a, bytes);
    CHECK(bytes.size() == kHeaderBytes + kFrameBytes);
    EncodeFrame(b, bytes);
    EncodeFrame(c, bytes);

    // Round trip: every field back as written.
    Recording rec;
    std::string error;
    bool truncated = true;
    CHECK(Decode(bytes.data(), bytes.size(), rec, &error, &truncated));
    CHECK(!truncated && rec.createdUnixMs == 1234567890123 && rec.frames.size() == 3);
    if (rec.frames.size() == 3) {
        Frame const& r = rec.frames[0];
        CHECK(r.timeNs == 0 && r.headValid && r.controllersActive);
        CHECK(SamePose(r.views[0].pose, a.views[0].pose) && SamePose(r.views[1].pose, a.views[1].pose));
        CHECK(r.views[1].fov.angleLeft == a.views[1].fov.angleLeft && r.views[0].fov.angleDown == a.views[0].fov.angleDown);
        for (int h = 0; h < 2; ++h) {
            CHECK(SamePose(r.hands[h].grip, a.hands[h].grip) && SamePose(r.hands[h].aim, a.hands[h].aim));
            CHECK(r.hands[h].trigger == a.hands[h].trigger && r.hands[h].squeeze == a.hands[h].squeeze);
            CHECK(r.hands[h].stickX == a.hands[h].stickX && r.hands[h].stickY == a.hands[h].stickY);
            CHECK(r.hands[h].primary == a.hands[h].primary && r.hands[h].stickClick == a.hands[h].stickClick &&
                r.hands[h].menu == a.hands[h].menu && r.hands[h].secondary == a.hands[h].secondary);
            CHECK(r.hands[h].gripValid && r.hands[h].aimValid);
        }
        CHECK(!rec.frames[1].hands[1].aimValid && rec.frames[1].hands[0].primary && !rec.frames[2].headValid);
    }
    CHECK(rec.DurationNs() == 20'000'000);

    // A partial last frame (the game closed mid-write) is dropped, not an error.
    std::vector<std::uint8_t> cut(bytes.begin(), bytes.end() - 100);
    CHECK(Decode(cut.data(), cut.size(), rec, &error, &truncated) && truncated && rec.frames.size() == 2);
    // Not a recording, or no frames.
    std::vector<std::uint8_t> junk(64, 0x41);
    CHECK(!Decode(junk.data(), junk.size(), rec, &error));
    CHECK(!Decode(bytes.data(), kHeaderBytes, rec, &error));
    CHECK(Decode(bytes.data(), bytes.size(), rec));

    // Sampling: halfway between a and b, positions interpolate, inputs hold the earlier frame.
    Frame s;
    CHECK(Sample(rec, 5'000'000, false, s));
    CHECK(s.headValid && NearD(s.views[0].pose.position.x, 0.5 - 0.032) && NearD(s.views[1].pose.position.x, 0.5 + 0.032));
    CHECK(NearD(s.hands[0].grip.position.x, 0.5 - 0.2) && !s.hands[0].primary);
    // The right aim pose is only valid in a: taken from a, not blended toward b's.
    CHECK(s.hands[1].aimValid && NearD(s.hands[1].aim.position.x, 0.0));
    // Orientations stay unit length.
    XrQuaternionf const& q = s.hands[1].grip.orientation;
    CHECK(NearD(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0));
    // Between b and c, the head is only valid in b: held from b.
    CHECK(Sample(rec, 15'000'000, false, s) && s.headValid && NearD(s.views[0].pose.position.x, 1.0 - 0.032));
    CHECK(s.hands[0].primary);
    // Past the end: the last frame, and false; with loop, wrapped to the start.
    CHECK(!Sample(rec, 30'000'000, false, s) && s.timeNs == 20'000'000);
    CHECK(Sample(rec, 25'000'000, true, s) && NearD(s.views[0].pose.position.x, 0.5 - 0.032));
    // Before the start: the first frame.
    CHECK(Sample(rec, -5, false, s) && NearD(s.views[0].pose.position.x, -0.032));
    Recording empty;
    CHECK(!Sample(empty, 0, true, s));

    // Through a real file: the Writer streams what Load reads back (left in %TEMP% for
    // tools/mevrrec.py to read).
    std::filesystem::path const file = std::filesystem::temp_directory_path() / "mevr-tests-roundtrip.mevrrec";
    {
        Writer writer;
        CHECK(writer.Open(file, 42));
        CHECK(writer.Append(a) && writer.Append(b) && writer.Append(c) && writer.Frames() == 3);
    }
    Recording loaded;
    CHECK(Load(file, loaded, &error) && loaded.createdUnixMs == 42 && loaded.frames.size() == 3);
    if (loaded.frames.size() == 3) {
        CHECK(SamePose(loaded.frames[1].hands[0].grip, b.hands[0].grip) && loaded.frames[2].timeNs == c.timeNs);
    }
    CHECK(!Load(file.parent_path() / "mevr-tests-missing.mevrrec", loaded, &error));
}

void TestImageConvert() {
    using namespace mevr::vr::image;
    // FP16: 1.0, 0.5, 0, -2, a subnormal, infinity.
    CHECK(HalfToFloat(0x3C00) == 1.0f && HalfToFloat(0x3800) == 0.5f && HalfToFloat(0x0000) == 0.0f);
    CHECK(HalfToFloat(0xC000) == -2.0f && NearD(HalfToFloat(0x0001), 5.96046e-8, 1e-12));
    CHECK(std::isinf(HalfToFloat(0x7C00)));
    // sRGB transfer: ends, mid grey (linear 0.2140 is sRGB 0.5), and back.
    CHECK(LinearToSrgb8(0.0f) == 0 && LinearToSrgb8(1.0f) == 255 && LinearToSrgb8(4.0f) == 255 && LinearToSrgb8(-1.0f) == 0);
    CHECK(LinearToSrgb8(0.2140f) == 128 || LinearToSrgb8(0.2140f) == 127);
    CHECK(NearD(SrgbToLinear(LinearToSrgb(0.3f)), 0.3, 1e-5));

    // 8-bit: BGRA is swizzled; opaque forces alpha.
    std::uint8_t const bgra[8] = {10, 20, 30, 40, 1, 2, 3, 4};
    std::uint32_t out[2] = {};
    Options opaque;
    opaque.opaque = true;
    CHECK(ConvertRow(format::kB8G8R8A8UnormSrgb, bgra, 2, opaque, out));
    CHECK(out[0] == Pack(30, 20, 10, 255) && out[1] == Pack(3, 2, 1, 255));
    CHECK(ConvertRow(format::kR8G8B8A8Unorm, bgra, 2, Options{}, out) && out[0] == Pack(10, 20, 30, 40));
    // Premultiplied half-covered white: straight white at half alpha (in linear light: sRGB 188 is
    // linear 0.5).
    std::uint8_t const premul[4] = {188, 188, 188, 128};
    Options pre;
    pre.premultiplied = true;
    CHECK(ConvertRow(format::kR8G8B8A8UnormSrgb, premul, 1, pre, out));
    CHECK((out[0] & 0xFF) >= 254 && (out[0] >> 24) == 128);
    // Fully transparent premultiplied: no colour.
    std::uint8_t const clear[4] = {0, 0, 0, 0};
    CHECK(ConvertRow(format::kR8G8B8A8UnormSrgb, clear, 1, pre, out) && out[0] == 0);
    // FP16 scRGB: linear 0.5 encodes; the white scale maps 2.0 to 1.0.
    std::uint16_t const half[8] = {0x3800, 0x3800, 0x3800, 0x3C00, 0x4000, 0x0000, 0x0000, 0x3C00};
    Options hdr;
    hdr.whiteScale = 0.5f;
    CHECK(ConvertRow(format::kR16G16B16A16Float, half, 2, hdr, out));
    CHECK((out[0] & 0xFF) == LinearToSrgb8(0.25f) && (out[0] >> 24) == 255);
    CHECK((out[1] & 0xFF) == 255 && ((out[1] >> 8) & 0xFF) == 0);
    // 10:10:10:2: max red, opaque.
    std::uint32_t const r10 = 0x3FFu | (3u << 30);
    CHECK(ConvertRow(format::kR10G10B10A2Unorm, &r10, 1, Options{}, out) && out[0] == Pack(255, 0, 0, 255));
    CHECK(!ConvertRow(2, bgra, 1, Options{}, out) && !Supported(2) && BytesPerPixel(10) == 8);

    // Over: opaque source replaces, transparent keeps, half white over black is linear 0.5 (sRGB 188).
    CHECK(Over(Pack(1, 2, 3, 255), Pack(9, 9, 9, 255)) == Pack(9, 9, 9, 255));
    CHECK(Over(Pack(1, 2, 3, 255), Pack(9, 9, 9, 0)) == Pack(1, 2, 3, 255));
    std::uint32_t const grey = Over(Pack(0, 0, 0, 255), Pack(255, 255, 255, 128));
    CHECK((grey >> 24) == 255 && (grey & 0xFF) >= 187 && (grey & 0xFF) <= 189);
}

void TestLayerComposite() {
    using namespace mevr::vr::composite;
    using mevr::vr::image::Pack;
    // A 90-degree square FOV.
    XrFovf const fov{-0.785398f, 0.785398f, 0.785398f, -0.785398f};
    XrVector3f const center = EyeRay(fov, 0.5f, 0.5f);
    CHECK(NearD(center.x, 0.0) && NearD(center.y, 0.0) && center.z == -1.0f);
    XrVector3f const corner = EyeRay(fov, 0.0f, 0.0f);  // top left
    CHECK(NearD(corner.x, -1.0) && NearD(corner.y, 1.0));

    // A 1 m quad 2 m ahead, facing the eye.
    Image texture{2, 2, {Pack(255, 0, 0, 255), Pack(0, 255, 0, 255), Pack(0, 0, 255, 255), Pack(255, 255, 255, 255)}};
    Quad quad;
    quad.pose.position = {0.0f, 0.0f, -2.0f};
    quad.width = 1.0f;
    quad.height = 1.0f;
    quad.image = &texture;
    float u = 0.0f, v = 0.0f;
    CHECK(HitQuad(quad, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, u, v) && NearD(u, 0.5) && NearD(v, 0.5));
    // Up-left of the center: the texture's top-left quarter (v counts down).
    CHECK(HitQuad(quad, {0.0f, 0.0f, 0.0f}, {-0.125f, 0.125f, -1.0f}, u, v) && NearD(u, 0.25) && NearD(v, 0.25));
    CHECK(!HitQuad(quad, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, -1.0f}, u, v));   // misses
    CHECK(!HitQuad(quad, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, u, v));    // points away
    CHECK(!HitQuad(quad, {0.0f, 0.0f, -3.0f}, {0.0f, 0.0f, 1.0f}, u, v));   // from behind

    // Into a black 40x40 eye image: the quad spans tan = 0.25 either side, i.e. the middle 10 pixels.
    Image eye{40, 40, std::vector<std::uint32_t>(1600, Pack(0, 0, 0, 255))};
    Eye view;
    view.fov = fov;
    std::size_t const covered = Draw(eye, view, quad);
    CHECK(covered == 100);
    CHECK(eye.At(16, 16) == Pack(255, 0, 0, 255));   // top left: red
    CHECK(eye.At(23, 16) == Pack(0, 255, 0, 255));   // top right: green
    CHECK(eye.At(16, 23) == Pack(0, 0, 255, 255));   // bottom left: blue
    CHECK(eye.At(10, 10) == Pack(0, 0, 0, 255));     // outside: untouched
    // The eye turned 90 degrees left sees nothing of it.
    Image eye2{40, 40, std::vector<std::uint32_t>(1600, Pack(0, 0, 0, 255))};
    view.pose.orientation = AxisAngle(0, 1, 0, 90);
    CHECK(Draw(eye2, view, quad) == 0);
    // Both turned: the quad at the origin facing +X (its +Z turned 90 degrees about +Y), the eye
    // 2 m along +X looking back along -X (its -Z turned the same way).
    quad.pose.orientation = AxisAngle(0, 1, 0, 90);
    quad.pose.position = {0.0f, 0.0f, 0.0f};
    view.pose.position = {2.0f, 0.0f, 0.0f};
    CHECK(Draw(eye2, view, quad) == 100);
    // Alpha: a half-transparent white quad shows grey over black.
    Image white{1, 1, {Pack(255, 255, 255, 128)}};
    quad.image = &white;
    Image eye3{40, 40, std::vector<std::uint32_t>(1600, Pack(0, 0, 0, 255))};
    CHECK(Draw(eye3, view, quad) == 100);
    CHECK((eye3.At(20, 20) & 0xFF) >= 187 && (eye3.At(20, 20) & 0xFF) <= 189);
    quad.blend = false;  // opaque: alpha ignored
    CHECK(Draw(eye3, view, quad) == 100 && eye3.At(20, 20) == Pack(255, 255, 255, 255));
}

void TestPng() {
    using mevr::vr::image::Pack;
    // COM for WIC, as the snapshot worker has it.
    HRESULT const co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::vector<std::uint32_t> pixels;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 5; ++x) {
            pixels.push_back(Pack(static_cast<std::uint8_t>(x * 50), static_cast<std::uint8_t>(y * 100), 7,
                static_cast<std::uint8_t>(255 - x * 40)));
        }
    }
    std::filesystem::path const file = std::filesystem::temp_directory_path() / "mevr-tests-snapshot.png";
    std::string error;
    CHECK(mevr::png::Write(file, 5, 3, pixels.data(), &error));
    int width = 0, height = 0;
    std::vector<std::uint32_t> back;
    CHECK(mevr::png::Read(file, width, height, back, &error));
    CHECK(width == 5 && height == 3 && back == pixels);  // lossless, RGBA order and alpha kept
    CHECK(!mevr::png::Write(file, 0, 3, pixels.data(), &error) && !error.empty());
    CHECK(!mevr::png::Read(file.parent_path() / "mevr-tests-missing.png", width, height, back, &error));
    if (SUCCEEDED(co)) {
        CoUninitialize();
    }
}

void TestTestRequest() {
    using namespace mevr::vr::test_request;
    Request r = Parse("C:\\Users\\me\\AppData\\Local\\MEVR\\recordings\\a b.mevrrec loop=1 Inputs=yes");
    CHECK(r.argument == "C:\\Users\\me\\AppData\\Local\\MEVR\\recordings\\a b.mevrrec");
    CHECK(r.Flag("loop", false) && r.Flag("inputs", false) && !r.Flag("missing", false) && r.Flag("missing", true));
    r = Parse("\xEF\xBB\xBF" "count=5\r\ninterval=0.5\ndelay=x\n\"C:\\a path\\x.mevrrec\"\n");
    CHECK(r.argument == "C:\\a path\\x.mevrrec");
    CHECK(NearD(r.Number("count", 1), 5) && NearD(r.Number("interval", 0), 0.5) && NearD(r.Number("delay", 2), 2));
    CHECK(r.Has("COUNT") && r.Get("tag", "none") == "none");
    r = Parse("stop");
    CHECK(r.argument == "stop" && r.options.empty());
    r = Parse("   ");
    CHECK(r.argument.empty() && r.options.empty());
    // A path with '=' in it stays the argument (a key is letters, digits and _ only).
    r = Parse("C:\\x=y\\z.mevrrec");
    CHECK(r.argument == "C:\\x=y\\z.mevrrec");

    CHECK(JsonString("a\"b\\c\nd") == "\"a\\\"b\\\\c\\nd\"");
    CHECK(JsonString(std::string_view("\x01", 1)) == "\"\\u0001\"");
    CHECK(JsonNumber(1.5) == "1.5" && JsonNumber(std::nan("")) == "null");
}

mevr::ik::Quat ToIk(XrQuaternionf const& q) {
    return {q.x, q.y, q.z, q.w};
}

bool NearIk(mevr::ik::Vec3 const& a, mevr::ik::Vec3 const& b, double tolerance = 1e-6) {
    return NearD(a.x, b.x, tolerance) && NearD(a.y, b.y, tolerance) && NearD(a.z, b.z, tolerance);
}

void TestBodySlotGeometry() {
    using namespace mevr::ik;
    using namespace mevr::ik::body_slots;
    // Axes of a body yaw: 0 faces -Z with +X to the right; 90 degrees left faces -X.
    CHECK(NearIk(YawForward(0.0), {0.0, 0.0, -1.0}));
    CHECK(NearIk(YawRight(0.0), {1.0, 0.0, 0.0}));
    double const left90 = std::numbers::pi / 2.0;
    CHECK(NearIk(YawForward(left90), {-1.0, 0.0, 0.0}));
    CHECK(NearIk(YawRight(left90), {0.0, 0.0, -1.0}));

    // The neutral eye: an upright head is its own anchor; pitching or rolling about the neck pivot
    // doesn't move it; turning doesn't either.
    Vec3 const head{0.3, 1.6, -0.2};
    CHECK(NearIk(NeutralEye(head, ToIk(YawPitch(0, 0))), head));
    CHECK(NearIk(NeutralEye(head, ToIk(YawPitch(35, 0))), head));
    // Looking 60 degrees down about the neck pivot: the eyes move down and forward; the anchor is
    // where they'd be upright again.
    Quat const down = ToIk(YawPitch(20, -60));
    Vec3 const pivot = head + Rotate(ToIk(YawPitch(20, 0)), Vec3{0.0, -0.08, 0.08});
    Vec3 const pitchedEye = pivot - Rotate(down, Vec3{0.0, -0.08, 0.08});
    CHECK(NearIk(NeutralEye(pitchedEye, down), head, 1e-6));

    // Slot centers: the default right hip, facing -Z, is to the right, below and a little behind;
    // facing 90 degrees left, "right" is -Z; offsets scale with the eye height.
    Layout layout = DefaultLayout();
    CHECK(layout.slots[0].weapon == Weapon::Pistol && layout.slots[1].weapon == Weapon::AssaultRifle &&
          layout.slots[2].weapon == Weapon::Shotgun && layout.slots[3].weapon == Weapon::Sniper);
    Frame frame{{0.0, 1.6, 0.0}, 0.0};
    Vec3 const hip = SlotCenter(frame, layout.slots[0], 1.0);
    CHECK(NearIk(hip, {0.20, 1.6 - 0.62, 0.05}));
    frame.yaw = left90;
    CHECK(NearIk(SlotCenter(frame, layout.slots[0], 1.0), {0.05, 1.6 - 0.62, -0.20}));
    layout.eyeHeight = kReferenceEyeHeight * 1.2;
    CHECK(NearD(Scale(layout), 1.2, 1e-9));
    layout.eyeHeight = 10.0;  // nonsense is clamped
    CHECK(Scale(layout) <= 1.5);
    // The shoulders are behind the head and don't contain it; the default slots don't overlap,
    // except the two shoulders behind the neck (as GoldenEye VR's do; the nearer one wins).
    frame.yaw = 0.0;
    for (int i : {1, 3}) {
        Vec3 const c = SlotCenter(frame, DefaultLayout().slots[i], 1.0);
        CHECK(c.z > 0.1);
        CHECK(Length(c - frame.anchor) > DefaultLayout().slots[i].radius);
    }
    for (int i = 0; i < kSlots; ++i) {
        for (int j = i + 1; j < kSlots; ++j) {
            if (i == 1 && j == 3) {
                continue;
            }
            Slot const& a = DefaultLayout().slots[i];
            Slot const& b = DefaultLayout().slots[j];
            CHECK(Length(SlotCenter(frame, a, 1.0) - SlotCenter(frame, b, 1.0)) > a.radius + b.radius);
        }
    }
    // The shouldered aim point (support grip's "at the shoulder") isn't near any slot.
    Vec3 const shouldered = frame.anchor + Vec3{0.15, -0.15, -0.10};
    for (Slot const& slot : DefaultLayout().slots) {
        CHECK(Length(shouldered - SlotCenter(frame, slot, 1.0)) > slot.radius + DefaultLayout().exitMargin);
    }

    // The anchor's height follows with a time constant; its horizontal position at once.
    Anchor anchor(0.08);
    CHECK(NearIk(anchor.Update({0.0, 1.6, 0.0}, 0.011), {0.0, 1.6, 0.0}));
    Vec3 const crouch = anchor.Update({0.1, 1.2, 0.0}, 0.08);
    CHECK(NearD(crouch.x, 0.1, 1e-9));
    CHECK(NearD(crouch.y, 1.6 - 0.4 * (1.0 - std::exp(-1.0)), 1e-6));
    for (int i = 0; i < 100; ++i) {
        anchor.Update({0.1, 1.2, 0.0}, 0.011);
    }
    CHECK(NearD(anchor.Update({0.1, 1.2, 0.0}, 0.011).y, 1.2, 1e-3));

    // Weapon names.
    CHECK(ParseWeapon("Pistol") == Weapon::Pistol);
    CHECK(ParseWeapon(" rifle ") == Weapon::AssaultRifle);
    CHECK(ParseWeapon("assault_rifle") == Weapon::AssaultRifle);
    CHECK(ParseWeapon("SNIPER") == Weapon::Sniper);
    CHECK(ParseWeapon("off") == Weapon::None);
    CHECK(!ParseWeapon("rocket"));
}

void TestBodySlotMachine() {
    using namespace mevr::ik::body_slots;
    // Two slots: 0 a pistol (r 0.14), 1 an assault rifle (r 0.20); 2 and 3 off.
    auto const input = [](double d0, double d1) {
        Input in;
        in.distance = {d0, d1, -1.0, -1.0};
        in.radius = {0.14, 0.20, 0.13, 0.20};
        in.weapon = {Weapon::Pistol, Weapon::AssaultRifle, Weapon::None, Weapon::None};
        in.available = {true, true, false, false};
        in.exitMargin = 0.05;
        return in;
    };
    Machine m;
    // Approaching: near (RB kept off) before in; in at the radius; out only past radius + margin.
    Step s = m.Update(input(0.30, 1.0));
    CHECK(!s.nearby && m.InSlot() == -1);
    s = m.Update(input(0.18, 1.0));
    CHECK(s.nearby && m.InSlot() == -1 && s.entered == -1);
    s = m.Update(input(0.13, 1.0));
    CHECK(s.entered == 0 && m.InSlot() == 0 && s.action == Action::None);
    s = m.Update(input(0.17, 1.0));
    CHECK(m.InSlot() == 0 && s.left == -1);  // within the hysteresis
    s = m.Update(input(0.20, 1.0));
    CHECK(s.left == 0 && m.InSlot() == -1);

    // A new squeeze in a slot draws its weapon, once; the grip held after that does nothing more.
    m.Reset();
    Input in = input(0.05, 1.0);
    in.held = Weapon::AssaultRifle;
    m.Update(in);
    in.gripDown = in.newSqueeze = true;
    s = m.Update(in);
    CHECK(s.action == Action::Draw && s.slot == 0 && s.weapon == Weapon::Pistol);
    CHECK(m.CurrentPhase() == Phase::Grab && m.Busy());
    in.newSqueeze = false;
    s = m.Update(in);
    CHECK(s.action == Action::None);
    in.gripDown = false;
    s = m.Update(in);
    CHECK(s.action == Action::None && m.CurrentPhase() == Phase::Idle);

    // A grip held while the hand moves into a slot never grabs (no new squeeze).
    m.Reset();
    in = input(0.5, 1.0);
    in.gripDown = true;
    m.Update(in);
    in.distance[0] = 0.05;
    s = m.Update(in);
    CHECK(s.entered == 0 && s.action == Action::None);

    // A slot whose weapon the player doesn't carry refuses.
    m.Reset();
    in = input(1.0, 0.1);
    in.available[1] = false;
    m.Update(in);
    in.gripDown = in.newSqueeze = true;
    s = m.Update(in);
    CHECK(s.action == Action::Unavailable && s.slot == 1);

    // A squeeze near a slot but in none: missed (nothing drawn).
    m.Reset();
    in = input(0.16, 1.0);
    in.gripDown = in.newSqueeze = true;
    s = m.Update(in);
    CHECK(s.nearby && s.action == Action::Missed);

    // Holster: squeeze and release at the held gun's own slot.
    m.Reset();
    in = input(0.05, 1.0);
    in.held = Weapon::Pistol;
    m.Update(in);
    in.gripDown = in.newSqueeze = true;
    s = m.Update(in);
    CHECK(s.action == Action::HolsterArmed && m.CurrentPhase() == Phase::HolsterArmed);
    in.newSqueeze = false;
    in.gripDown = false;
    s = m.Update(in);
    CHECK(s.action == Action::Holster && s.slot == 0 && s.weapon == Weapon::Pistol);
    // ... in one quick tap between steps too.
    m.Reset();
    in.gripDown = false;
    in.newSqueeze = true;
    m.Update(input(0.05, 1.0));
    s = m.Update(in);
    CHECK(s.action == Action::Holster);
    // ... cancelled by leaving the slot (past the hysteresis) before the release.
    m.Reset();
    in = input(0.05, 1.0);
    in.held = Weapon::Pistol;
    m.Update(in);
    in.gripDown = in.newSqueeze = true;
    m.Update(in);
    in.newSqueeze = false;
    in.distance[0] = 0.25;
    s = m.Update(in);
    CHECK(s.action == Action::HolsterCancelled);
    in.distance[0] = 0.05;  // back in the slot and released: still nothing
    m.Update(in);
    in.gripDown = false;
    s = m.Update(in);
    CHECK(s.action == Action::None);
    // ... cancelled by the trigger.
    m.Reset();
    in = input(0.05, 1.0);
    in.held = Weapon::Pistol;
    m.Update(in);
    in.gripDown = in.newSqueeze = true;
    m.Update(in);
    in.newSqueeze = false;
    in.trigger = true;
    s = m.Update(in);
    CHECK(s.action == Action::HolsterCancelled);
    in.gripDown = false;
    CHECK(m.Update(in).action == Action::None);
    // ... off: the held gun's slot does nothing.
    m.Reset();
    in = input(0.05, 1.0);
    in.held = Weapon::Pistol;
    in.holster = Holster::Off;
    in.gripDown = in.newSqueeze = true;
    s = m.Update(in);
    CHECK(s.action == Action::AlreadyHeld);
    in.newSqueeze = in.gripDown = false;
    CHECK(m.Update(in).action == Action::None);
    // ... an empty slot holsters only with OwnOrEmptySlot.
    m.Reset();
    in = input(1.0, 0.05);
    in.available[1] = false;
    in.held = Weapon::Pistol;
    in.gripDown = in.newSqueeze = true;
    CHECK(m.Update(in).action == Action::Unavailable);
    in.newSqueeze = in.gripDown = false;
    CHECK(m.Update(in).action == Action::None);
    m.Reset();
    in.holster = Holster::OwnOrEmptySlot;
    in.gripDown = in.newSqueeze = true;
    CHECK(m.Update(in).action == Action::HolsterArmed);
    in.newSqueeze = in.gripDown = false;
    CHECK(m.Update(in).action == Action::Holster);

    // Overlapping slots: the smaller distance / radius wins.
    m.Reset();
    s = m.Update(input(0.12, 0.15));  // 0.86 vs 0.75
    CHECK(m.InSlot() == 1);
    // Lost tracking: out of every slot, not near.
    in = input(0.05, 1.0);
    in.tracked = false;
    s = m.Update(in);
    CHECK(s.left == 1 && m.InSlot() == -1 && !s.nearby);
}

// The right grip holds the gun (ik::body_slots::Machine with gripHolds).
void TestBodySlotHolding() {
    using namespace mevr::ik::body_slots;
    // Two slots: 0 a pistol (r 0.14), 1 an assault rifle (r 0.20); 2 and 3 off.
    auto const base = [](double d0, double d1) {
        Input in;
        in.distance = {d0, d1, -1.0, -1.0};
        in.radius = {0.14, 0.20, 0.13, 0.20};
        in.weapon = {Weapon::Pistol, Weapon::AssaultRifle, Weapon::None, Weapon::None};
        in.available = {true, true, false, false};
        in.gripHolds = true;
        in.releaseGrace = 0.3;
        in.deltaSeconds = 0.011;
        return in;
    };
    Machine m;
    // Squeeze at the hip: draw the pistol; keep squeezing (carry it away): nothing more, and the long
    // hold doesn't hold the body yaw once the hand is out of the slot.
    Input in = base(0.05, 1.0);
    m.Update(in);
    in.gripDown = in.newSqueeze = true;
    Step s = m.Update(in);
    CHECK(s.action == Action::Draw && s.weapon == Weapon::Pistol && m.CurrentPhase() == Phase::Grab);
    in.newSqueeze = false;
    in.held = Weapon::Pistol;
    in.distance[0] = 0.6;
    for (int i = 0; i < 100; ++i) {
        s = m.Update(in);
        CHECK(s.action == Action::None);
    }
    CHECK(!m.Busy());
    // Let go away from the slot: the return is armed; squeezing again in time keeps the gun.
    in.gripDown = false;
    s = m.Update(in);
    CHECK(s.action == Action::ReturnArmed && s.weapon == Weapon::Pistol && m.CurrentPhase() == Phase::Returning);
    CHECK(m.ReturnLeft() > 0.28);
    in.gripDown = in.newSqueeze = true;
    s = m.Update(in);
    CHECK(s.action == Action::ReturnCancelled && m.CurrentPhase() == Phase::Grab);
    in.newSqueeze = false;
    // Let go again and wait: it goes back after the grace (0.3 s), once.
    in.gripDown = false;
    CHECK(m.Update(in).action == Action::ReturnArmed);
    in.deltaSeconds = 0.1;
    CHECK(m.Update(in).action == Action::None);
    CHECK(m.Update(in).action == Action::None);
    s = m.Update(in);  // 0.3 - 3 * 0.1: about zero, either side with floating point
    if (s.action == Action::None) {
        s = m.Update(in);
    }
    CHECK(s.action == Action::Return && s.weapon == Weapon::Pistol && m.CurrentPhase() == Phase::Idle);
    CHECK(m.Update(in).action == Action::None);
    // Never mid-burst: the trigger held stops the grace's clock.
    m.Reset();
    in = base(0.6, 1.0);
    in.held = Weapon::Pistol;
    in.gripDown = in.newSqueeze = true;
    m.Update(in);
    in.newSqueeze = false;
    in.gripDown = false;
    in.trigger = true;
    CHECK(m.Update(in).action == Action::ReturnArmed);
    in.deltaSeconds = 0.5;
    for (int i = 0; i < 5; ++i) {
        s = m.Update(in);
        CHECK(s.action == Action::None && s.returnWaitsForTrigger);
    }
    in.trigger = false;
    s = m.Update(in);
    CHECK(s.action == Action::Return);

    // Let go at the gun's own slot: holstered at once. At another slot: the grace, as anywhere.
    m.Reset();
    in = base(0.05, 1.0);
    in.held = Weapon::Pistol;
    in.deltaSeconds = 0.011;
    m.Update(in);
    in.gripDown = in.newSqueeze = true;
    s = m.Update(in);
    CHECK(s.action == Action::AlreadyHeld && s.weapon == Weapon::Pistol);  // taking the gun that's out
    in.newSqueeze = false;
    in.gripDown = false;
    s = m.Update(in);
    CHECK(s.action == Action::Holster && s.slot == 0 && s.weapon == Weapon::Pistol);
    m.Reset();
    in = base(0.6, 1.0);
    in.held = Weapon::Pistol;
    in.gripDown = in.newSqueeze = true;
    m.Update(in);
    in.newSqueeze = false;
    in.distance[1] = 0.05;  // carried to the rifle's slot, let go there
    m.Update(in);
    in.gripDown = false;
    s = m.Update(in);
    CHECK(s.action == Action::ReturnArmed && s.slot == 1 && s.weapon == Weapon::Pistol);
    // A squeeze at another gun's slot while holding one draws that one; let go there and it's
    // holstered (it's at its own slot by then).
    m.Reset();
    in = base(1.0, 0.05);
    in.held = Weapon::Pistol;
    in.gripDown = in.newSqueeze = true;
    CHECK(m.Update(in).action == Action::Draw);
    in.newSqueeze = false;
    in.gripDown = false;
    in.held = Weapon::AssaultRifle;
    s = m.Update(in);
    CHECK(s.action == Action::Holster && s.weapon == Weapon::AssaultRifle);

    // Let go at the own slot while firing: it goes back once the trigger is let go (no grace).
    m.Reset();
    in = base(0.05, 1.0);
    in.held = Weapon::Pistol;
    in.gripDown = in.newSqueeze = true;
    m.Update(in);
    in.newSqueeze = false;
    in.gripDown = false;
    in.trigger = true;
    CHECK(m.Update(in).action == Action::ReturnArmed);
    in.trigger = false;
    CHECK(m.Update(in).action == Action::Return);

    // Let go while the draw is still on its way (nothing out yet): it waits for the gun, then the grace.
    m.Reset();
    in = base(0.05, 1.0);
    in.gripDown = in.newSqueeze = true;
    CHECK(m.Update(in).action == Action::Draw);
    in.newSqueeze = false;
    in.gripDown = false;
    in.distance[0] = 0.6;
    CHECK(m.Update(in).action == Action::ReturnArmed);
    in.deltaSeconds = 0.2;
    CHECK(m.Update(in).action == Action::None);  // not out yet: waiting
    in.held = Weapon::Pistol;
    CHECK(m.Update(in).action == Action::None);
    s = m.Update(in);
    CHECK(s.action == Action::Return && s.weapon == Weapon::Pistol);
    // ...but not forever.
    m.Reset();
    in = base(0.05, 1.0);
    in.gripDown = in.newSqueeze = true;
    m.Update(in);
    in.newSqueeze = in.gripDown = false;
    in.distance[0] = 0.6;
    m.Update(in);
    in.deltaSeconds = 1.0;
    m.Update(in);
    m.Update(in);
    CHECK(m.Update(in).action == Action::ReturnCancelled && m.CurrentPhase() == Phase::Idle);
    // The gun put away (by the game) or swapped during the grace: the return is off.
    m.Reset();
    in = base(0.6, 1.0);
    in.held = Weapon::Pistol;
    in.deltaSeconds = 0.011;
    in.gripDown = in.newSqueeze = true;
    m.Update(in);
    in.newSqueeze = in.gripDown = false;
    m.Update(in);
    in.held = Weapon::None;
    CHECK(m.Update(in).action == Action::ReturnCancelled);
    m.Reset();
    in.held = Weapon::Pistol;
    in.gripDown = in.newSqueeze = true;
    m.Update(in);
    in.newSqueeze = in.gripDown = false;
    m.Update(in);
    in.held = Weapon::AssaultRifle;
    CHECK(m.Update(in).action == Action::ReturnCancelled);

    // releaseGrace < 0: let go away from a slot and the gun stays out; holster = 0: never by hand.
    m.Reset();
    in = base(0.6, 1.0);
    in.held = Weapon::Pistol;
    in.releaseGrace = -1.0;
    in.gripDown = in.newSqueeze = true;
    m.Update(in);
    in.newSqueeze = in.gripDown = false;
    CHECK(m.Update(in).action == Action::None && m.CurrentPhase() == Phase::Idle);
    m.Reset();
    in = base(0.05, 1.0);
    in.held = Weapon::Pistol;
    in.holster = Holster::Off;
    in.gripDown = in.newSqueeze = true;
    m.Update(in);
    in.newSqueeze = in.gripDown = false;
    CHECK(m.Update(in).action == Action::None && m.CurrentPhase() == Phase::Idle);

    // A squeeze and release between two steps at a slot: the draw now, the let-go next step.
    m.Reset();
    in = base(0.05, 1.0);
    in.newSqueeze = true;
    in.gripDown = false;
    CHECK(m.Update(in).action == Action::Draw);
    in.newSqueeze = false;
    CHECK(m.Update(in).action == Action::ReturnArmed);

    // A squeeze we didn't see (it began in a menu): taken as held once a gun is out (gripKept), so
    // letting go still puts it away; without gripKept it isn't ours.
    m.Reset();
    in = base(0.6, 1.0);
    in.held = Weapon::Pistol;
    in.gripDown = true;
    in.gripKept = true;
    m.Update(in);
    CHECK(m.CurrentPhase() == Phase::Grab);
    in.gripDown = false;
    CHECK(m.Update(in).action == Action::ReturnArmed);
    m.Reset();
    in.gripDown = true;
    in.gripKept = false;
    m.Update(in);
    in.gripDown = false;
    CHECK(m.Update(in).action == Action::None);

    // A squeeze away from the slots with nothing out: no action, no RB; near a slot: missed.
    m.Reset();
    in = base(0.6, 1.0);
    in.gripDown = in.newSqueeze = true;
    CHECK(m.Update(in).action == Action::None);
    in.newSqueeze = in.gripDown = false;
    CHECK(m.Update(in).action == Action::None);
    in = base(0.17, 1.0);
    in.gripDown = in.newSqueeze = true;
    CHECK(m.Update(in).action == Action::Missed);

    // Switching modes drops what was in progress.
    m.Reset();
    in = base(0.6, 1.0);
    in.held = Weapon::Pistol;
    in.gripDown = in.newSqueeze = true;
    m.Update(in);
    in.newSqueeze = in.gripDown = false;
    m.Update(in);
    CHECK(m.CurrentPhase() == Phase::Returning);
    in.gripHolds = false;
    CHECK(m.Update(in).action == Action::None && m.CurrentPhase() == Phase::Idle);
}

void TestContactHaptics() {
    using namespace mevr::ik::body_slots;
    CHECK(NearD(Depth(0.0, 0.14), 1.0, 1e-9));
    CHECK(NearD(Depth(0.07, 0.14), 0.5, 1e-9));
    CHECK(Depth(0.16, 0.14) == 0.0);  // the hysteresis band
    CHECK(Depth(-1.0, 0.14) == 0.0 && Depth(0.1, 0.0) == 0.0);
    // Deeper: stronger and higher; the edge is faint but felt.
    Buzz const edge = Contact(0.0, 0.5, false);
    Buzz const mid = Contact(0.5, 0.5, false);
    Buzz const center = Contact(1.0, 0.5, false);
    CHECK(edge.amplitude > 0.05f && edge.amplitude < mid.amplitude && mid.amplitude < center.amplitude);
    CHECK(edge.frequency < mid.frequency && mid.frequency < center.frequency);
    CHECK(center.amplitude <= 0.4f);  // a buzz, not a jolt
    CHECK(std::abs(Contact(1.0, 0.5, true).amplitude - center.amplitude * 0.5f) < 1e-6f);
    // Strength scales everything; 0 is silence; it never exceeds 1.
    CHECK(Contact(1.0, 0.0, false).amplitude == 0.0f && DrawClick(0.0).amplitude == 0.0f);
    CHECK(std::abs(Contact(0.5, 1.0, false).amplitude - 2.0f * mid.amplitude) < 1e-6f);
    CHECK(DrawClick(1.0).amplitude <= 1.0f);
    // The events: the edge above the contact; the click crisp and short; the holster click the draw's
    // mirror at lower amplitude and frequency; a refusal dull and longer.
    CHECK(EnterEdge(0.5).amplitude > center.amplitude && EnterEdge(0.5).seconds <= 0.05f);
    CHECK(DrawClick(0.5).seconds <= 0.03f && DrawClick(0.5).frequency > center.frequency);
    CHECK(HolsterClick(0.5).amplitude < DrawClick(0.5).amplitude && HolsterClick(0.5).frequency < DrawClick(0.5).frequency);
    CHECK(Refused(0.5).seconds > DrawClick(0.5).seconds && Refused(0.5).frequency < edge.frequency);
}

void TestStandingHeight() {
    using namespace mevr::ik::body_slots;
    StandingHeight h;
    CHECK(!h.Valid());
    h.Update(0.2, 0.011);  // no floor (the head at the origin): ignored
    h.Update(3.0, 0.011);
    CHECK(!h.Valid());
    h.Update(1.60, 0.011);
    CHECK(h.Valid() && NearD(h.Value(), 1.60, 1e-9));
    // Crouching never lowers it.
    for (int i = 0; i < 1000; ++i) {
        h.Update(1.0, 0.011);
    }
    CHECK(NearD(h.Value(), 1.60, 1e-9));
    // Standing taller raises it slowly: a 0.1 s tiptoe barely moves it; staying there gets there.
    h.Update(1.75, 0.1);
    CHECK(h.Value() > 1.60 && h.Value() < 1.61);
    for (int i = 0; i < 1000; ++i) {
        h.Update(1.70, 0.011);
    }
    CHECK(NearD(h.Value(), 1.70, 0.01));
    // A recenter takes the sample as the standing height, down too (another player, or the last
    // estimate was a jump); an implausible one is ignored.
    h.Recenter(1.55);
    CHECK(NearD(h.Value(), 1.55, 1e-9));
    h.Recenter(0.1);
    CHECK(NearD(h.Value(), 1.55, 1e-9));
    h.Reset();
    CHECK(!h.Valid());
}

void TestHapticChannel() {
    using namespace mevr::vr::haptics;
    using Kind = Command::Kind;
    Channel c;
    std::uint64_t t = 10000;
    CHECK(c.Step(t).kind == Kind::None);
    // A continuous buzz is sent in chunks: once, then again only when the chunk is nearly over or
    // the amplitude changes.
    c.Continuous(0.3f, 120.0f, 0.15f, t);
    Command cmd = c.Step(t);
    CHECK(cmd.kind == Kind::Play && std::abs(cmd.seconds - 0.1f) < 1e-6f && cmd.amplitude == 0.3f && cmd.frequency == 120.0f);
    CHECK(c.Step(t + 11).kind == Kind::None);
    c.Continuous(0.305f, 120.0f, 0.15f, t + 22);  // a tiny change waits
    CHECK(c.Step(t + 22).kind == Kind::None);
    c.Continuous(0.5f, 150.0f, 0.15f, t + 33);  // a real one doesn't
    cmd = c.Step(t + 33);
    CHECK(cmd.kind == Kind::Play && cmd.amplitude == 0.5f);
    c.Continuous(0.5f, 150.0f, 0.15f, t + 70);
    CHECK(c.Step(t + 70).kind == Kind::None);
    c.Continuous(0.5f, 150.0f, 0.15f, t + 100);
    CHECK(c.Step(t + 100).kind == Kind::Play);  // 33 ms left of the chunk sent at +33: re-sent
    // A pulse interrupts it; the buzz resumes when the pulse is over.
    c.Pulse(0.02f, 0.9f, 320.0f);
    cmd = c.Step(t + 110);
    CHECK(cmd.kind == Kind::Play && cmd.amplitude == 0.9f && cmd.frequency == 320.0f);
    c.Continuous(0.5f, 150.0f, 0.15f, t + 115);
    CHECK(c.Step(t + 115).kind == Kind::None);
    CHECK(c.Step(t + 131).kind == Kind::Play);
    // Not asked again: the last chunk ends with the request (chunks never outlast it) and nothing
    // more is sent; the vibration just ends.
    cmd = c.Step(t + 200);
    CHECK(cmd.kind == Kind::Play && cmd.seconds <= 0.066f);
    CHECK(c.Step(t + 240).kind == Kind::None);
    CHECK(c.Step(t + 270).kind == Kind::None && !c.ContinuousActive(t + 270));
    // Amplitude 0 stops it now; Clear (the mod unloaded) too.
    c.Continuous(0.3f, 0.0f, 0.15f, t + 400);
    CHECK(c.Step(t + 400).kind == Kind::Play);
    c.Continuous(0.0f, 0.0f, 0.0f, t + 411);
    CHECK(c.Step(t + 411).kind == Kind::Stop);
    c.Continuous(0.3f, 0.0f, 0.15f, t + 500);
    CHECK(c.Step(t + 500).kind == Kind::Play);
    c.Clear();
    CHECK(c.Step(t + 511).kind == Kind::Stop);
    CHECK(c.Step(t + 522).kind == Kind::None);
    // A request can't keep it going for long: at most kMaxContinuousSeconds.
    c.Continuous(0.3f, 0.0f, 10.0f, t + 600);
    CHECK(!c.ContinuousActive(t + 600 + 501) && c.ContinuousActive(t + 600 + 499));
}

void TestRightGripFree() {
    using namespace mevr::game::gamepad;
    mevr::vr::controllers::State state;
    Latches latches;
    Options options;
    // Without the flag the right grip is RB as always.
    state.right.squeeze = 1.0f;
    CHECK((FromControllers(state, latches, options).wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
    // The flag appearing while the grip is held changes nothing (the squeeze began elsewhere).
    options.rightGripFree = true;
    CHECK((FromControllers(state, latches, options).wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
    CHECK(RightGripReport(latches) == 0);
    // A new squeeze with the flag: no RB, counted, until the grip opens, wherever the hand goes.
    state.right.squeeze = 0.0f;
    FromControllers(state, latches, options);
    state.right.squeeze = 1.0f;
    CHECK((FromControllers(state, latches, options).wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) == 0);
    CHECK(RightGripReport(latches) == ((1u << 1) | 1u));
    options.rightGripFree = false;  // the hand left the holster, grip still held
    CHECK((FromControllers(state, latches, options).wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) == 0);
    state.right.squeeze = 0.0f;
    FromControllers(state, latches, options);
    CHECK(RightGripReport(latches) == (1u << 1));
    // The next squeeze away from a holster is RB again.
    state.right.squeeze = 1.0f;
    CHECK((FromControllers(state, latches, options).wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
    CHECK(RightGripReport(latches) == (1u << 1));
    // The flag crosses the host boundary.
    Options flagged;
    flagged.rightGripFree = true;
    CHECK((ToFlags(flagged) & kFlagRightGripFree) != 0);
    CHECK(FromFlags(-1.0f, 0.12f, 0.22f, ToFlags(flagged)).rightGripFree);
    CHECK(!FromFlags(-1.0f, 0.12f, 0.22f, kFlagLeftTrigger).rightGripFree);
}

// The right grip holds the gun (research/19): never RB, every squeeze counted; RB from rbSource.
void TestRightGripHolds() {
    using namespace mevr::game::gamepad;
    constexpr WORD kRb = XINPUT_GAMEPAD_RIGHT_SHOULDER;
    mevr::vr::controllers::State state;
    Latches latches;
    Options options;
    options.rightGripHolds = true;
    options.rbSource = RbSource::Y;
    // Every squeeze is kept from the game and counted, anywhere.
    state.right.squeeze = 1.0f;
    CHECK((FromControllers(state, latches, options).wButtons & kRb) == 0);
    CHECK(latches.rightHold && RightGripReport(latches) == ((1u << 1) | 1u));
    // The option dropping mid-squeeze (a menu, the Mako) doesn't make the held grip RB.
    Options legacy;
    CHECK((FromControllers(state, latches, legacy).wButtons & kRb) == 0);
    state.right.squeeze = 0.0f;
    FromControllers(state, latches, options);
    CHECK(!latches.rightHold && RightGripReport(latches) == (1u << 1));
    // A new squeeze without the option is RB, as always.
    state.right.squeeze = 1.0f;
    CHECK((FromControllers(state, latches, legacy).wButtons & kRb) != 0);
    state.right.squeeze = 0.0f;
    FromControllers(state, latches, legacy);
    // Halcyon's menu: still not RB, but not counted (it must never draw a weapon).
    Options menu = options;
    menu.countKept = false;
    state.right.squeeze = 1.0f;
    CHECK((FromControllers(state, latches, menu).wButtons & kRb) == 0);
    CHECK(RightGripReport(latches) == ((1u << 1) | 1u));
    state.right.squeeze = 0.0f;
    FromControllers(state, latches, options);

    // Y as the RB source: Y is RB, not Y; X and B keep their own buttons.
    state = {};
    latches = {};
    state.left.secondary = true;
    state.left.primary = true;
    state.right.secondary = true;
    auto pad = FromControllers(state, latches, options);
    CHECK((pad.wButtons & kRb) != 0 && (pad.wButtons & XINPUT_GAMEPAD_Y) == 0);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_X) != 0 && (pad.wButtons & XINPUT_GAMEPAD_B) != 0);
    // A press keeps its meaning: Y held stays RB when the option drops (the power wheel stays open).
    pad = FromControllers(state, latches, legacy);
    CHECK((pad.wButtons & kRb) != 0 && (pad.wButtons & XINPUT_GAMEPAD_Y) == 0);
    state.left.secondary = false;
    pad = FromControllers(state, latches, legacy);
    CHECK((pad.wButtons & kRb) == 0);
    // ...and a Y pressed without the option is Y.
    state.left.secondary = true;
    pad = FromControllers(state, latches, legacy);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_Y) != 0 && (pad.wButtons & kRb) == 0);
    pad = FromControllers(state, latches, options);  // held on: still Y
    CHECK((pad.wButtons & XINPUT_GAMEPAD_Y) != 0 && (pad.wButtons & kRb) == 0);

    // The flags cross the boundary; an unknown source reads as none.
    Options const back = FromFlags(-1.0f, 0.12f, 0.22f, ToFlags(options));
    CHECK(back.rightGripHolds && back.rbSource == RbSource::Y);
    CHECK(FromFlags(-1.0f, 0.12f, 0.22f, kFlagRightGripHolds | (0xFu << kRbSourceShift)).rbSource == RbSource::None);
    CHECK(ToFlags(Options{}) == (kFlagLeftTrigger | kFlagSupportAims | kFlagGripShoulder));
}

// The right stick click as RB (the default RB source): commit delay, tap after the double-click
// window, Halcyon's double-click recenter, the stick pushed over, the chords.
void TestRbFromStickClick() {
    using namespace mevr::game::gamepad;
    using Phase = RbClick::Phase;
    RbClick s;
    RbClickInput in;
    in.enabled = true;
    std::uint64_t t = 1000;
    auto const step = [&](bool click, std::uint64_t advance) {
        t += advance;
        in.click = click;
        in.nowMs = t;
        return RbFromStickClick(s, in);
    };
    // A hold: no RB for the first kRbCommitMs, then RB until the click opens.
    CHECK(!step(true, 0) && s.phase == Phase::Pending);
    CHECK(!step(true, 50));
    CHECK(step(true, 60) && s.phase == Phase::Held);
    CHECK(step(true, 500));
    CHECK(!step(false, 10) && s.phase == Phase::Idle);
    // A quick click: RB only after the double-click window, for kRbTapMs.
    t += 1000;
    CHECK(!step(true, 0));
    CHECK(!step(false, 40) && s.phase == Phase::TapWait);
    CHECK(!step(false, 300));
    CHECK(step(false, 100) && s.phase == Phase::Tap);  // 440 ms after the press
    CHECK(step(false, 50));
    CHECK(!step(false, 40) && s.phase == Phase::Idle);
    // A double-click (Halcyon's recenter): neither click is RB.
    t += 1000;
    CHECK(!step(true, 0));
    CHECK(!step(false, 60));
    CHECK(!step(true, 120) && s.phase == Phase::Ignored);  // second press 180 ms after the first
    CHECK(!step(true, 200));
    CHECK(!step(false, 10) && s.phase == Phase::Idle);
    // ...and the pair is used up (as in Halcyon): a third click 100 ms later is a fresh first click,
    // whose tap a fourth click inside the window cancels (another recenter).
    CHECK(!step(true, 90) && s.phase == Phase::Pending);
    CHECK(!step(false, 30) && s.phase == Phase::TapWait);
    CHECK(!step(true, 100) && s.phase == Phase::Ignored);
    CHECK(!step(false, 30));
    CHECK(!step(false, 600) && s.phase == Phase::Idle);
    t += 1000;
    step(false, 0);
    // The stick pushed over (turning) when clicked: not RB, even if held.
    in.stickX = 0.9f;
    CHECK(!step(true, 0) && s.phase == Phase::Ignored);
    in.stickX = 0.0f;
    CHECK(!step(true, 300));
    CHECK(!step(false, 10));
    // Halcyon's both-sticks chord: the left click joining within the commit delay cancels it.
    t += 1000;
    CHECK(!step(true, 0));
    in.otherClick = true;
    CHECK(!step(true, 30) && s.phase == Phase::Ignored);
    CHECK(!step(true, 500));
    in.otherClick = false;
    CHECK(!step(false, 10));
    // The test loop's chord reserves the clicks.
    t += 1000;
    in.reserved = true;
    CHECK(!step(true, 0) && s.phase == Phase::Ignored);
    CHECK(!step(true, 300));
    in.reserved = false;
    CHECK(!step(false, 10));
    // Not the RB source: a new press is ignored, but one already held carries on.
    t += 1000;
    CHECK(!step(true, 0));
    CHECK(step(true, 150));
    in.enabled = false;
    CHECK(step(true, 100));
    CHECK(!step(false, 10));
    CHECK(!step(true, 1000) && s.phase == Phase::Ignored);
    CHECK(!step(false, 10));
    in.enabled = true;
    // No clock: RB follows an accepted click.
    s = {};
    in.nowMs = 0;
    in.click = true;
    CHECK(RbFromStickClick(s, in));
    in.click = false;
    CHECK(!RbFromStickClick(s, in));

    // Through the pad: R3 stays R3 (Halcyon reads it) and is RB once committed.
    mevr::vr::controllers::State state;
    Latches latches;
    Options options;
    options.rightGripHolds = true;
    options.rbSource = RbSource::RightStickClick;
    state.timeMs = 5000;
    state.right.stickClick = true;
    auto pad = FromControllers(state, latches, options);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0 && (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) == 0);
    state.timeMs += 120;
    pad = FromControllers(state, latches, options);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0 && (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
    // Without gripHolds the stick click is only R3.
    latches = {};
    state.timeMs += 2000;
    Options legacy;
    pad = FromControllers(state, latches, legacy);
    state.timeMs += 300;
    pad = FromControllers(state, latches, legacy);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) == 0);
}

// ---- The squad weapon menu (core/vr/SquadMenu, game/GamepadMapping's chord, gate and squad flag) ----

void TestSquadPointing() {
    using namespace mevr::vr::squad_menu;
    float const deg = 3.14159265f / 180.0f;
    XrVector3f const origin{0.0f, 1.2f, 0.0f};
    XrVector3f const ahead{0.0f, 0.0f, -1.0f};
    // A target straight ahead is 0 degrees off; one 5 m ahead and 1 m right is about 11.3 degrees off.
    CHECK(AngleToRay(origin, ahead, {0.0f, 1.2f, -5.0f}) < 1e-3f);
    CHECK(std::abs(AngleToRay(origin, ahead, {1.0f, 1.2f, -5.0f}) / deg - 11.31f) < 0.05f);
    // Degenerate: a point at the origin or no direction is as far off as can be.
    CHECK(AngleToRay(origin, ahead, origin) > 3.1f);
    CHECK(AngleToRay(origin, {0.0f, 0.0f, 0.0f}, {0.0f, 1.2f, -5.0f}) > 3.1f);

    Target targets[2];
    targets[0] = {true, {-0.4f, 1.2f, -5.0f}};  // 4.6 degrees left
    targets[1] = {true, {0.6f, 1.2f, -5.0f}};   // 6.8 degrees right
    CHECK(PickPointed(origin, ahead, targets, 2, 10.0f * deg, 40.0f) == 0);
    // Pointing a little right picks the right one.
    XrVector3f const right = {std::sin(6.0f * deg), 0.0f, -std::cos(6.0f * deg)};
    CHECK(PickPointed(origin, right, targets, 2, 10.0f * deg, 40.0f) == 1);
    // A narrow cone misses both; a short reach misses both.
    CHECK(PickPointed(origin, ahead, targets, 2, 3.0f * deg, 40.0f) == -1);
    CHECK(PickPointed(origin, ahead, targets, 2, 10.0f * deg, 4.0f) == -1);
    // Pointing away: nobody within the cone, but the nearest to the ray is still known.
    XrVector3f const leftward{-1.0f, 0.0f, 0.0f};
    CHECK(PickPointed(origin, leftward, targets, 2, 10.0f * deg, 40.0f) == -1);
    CHECK(NearestToRay(origin, leftward, targets, 2) == 0);
    // Two squadmates in line (within a degree): the nearer one.
    targets[0] = {true, {0.0f, 1.2f, -12.0f}};
    targets[1] = {true, {0.05f, 1.2f, -4.0f}};
    CHECK(PickPointed(origin, ahead, targets, 2, 10.0f * deg, 40.0f) == 1);
    // An invalid target (no position) is never picked.
    targets[1].valid = false;
    CHECK(PickPointed(origin, ahead, targets, 2, 10.0f * deg, 40.0f) == 0);
    targets[0].valid = false;
    CHECK(PickPointed(origin, ahead, targets, 2, 10.0f * deg, 40.0f) == -1);
    CHECK(NearestToRay(origin, ahead, targets, 2) == -1);
}

void TestSquadRadial() {
    using namespace mevr::vr::squad_menu;
    RadialSettings const s;  // dead zone 0.55, release 0.35, 12 degrees of hysteresis
    // The fixed layout.
    CHECK(WeaponAt(Direction::Up) == 2 && WeaponAt(Direction::Left) == 1 && WeaponAt(Direction::Right) == 3 &&
          WeaponAt(Direction::Down) == 0 && WeaponAt(Direction::None) == -1);
    for (int w = 0; w < kWeapons; ++w) {
        CHECK(WeaponAt(DirectionOf(w)) == w);
    }
    // Inside the dead zone: nothing.
    CHECK(Radial(0.0f, 0.5f, Direction::None, s) == Direction::None);
    CHECK(Radial(0.3f, -0.3f, Direction::None, s) == Direction::None);
    // Past it: the nearest of the four.
    CHECK(Radial(0.0f, 0.9f, Direction::None, s) == Direction::Up);
    CHECK(Radial(0.0f, -0.9f, Direction::None, s) == Direction::Down);
    CHECK(Radial(-0.9f, 0.1f, Direction::None, s) == Direction::Left);
    CHECK(Radial(0.9f, -0.2f, Direction::None, s) == Direction::Right);
    // Just past the up/right diagonal (50 degrees): right, unless up is held (hysteresis to 57 degrees).
    float const a50 = 50.0f * 3.14159265f / 180.0f;
    CHECK(Radial(0.9f * std::sin(a50), 0.9f * std::cos(a50), Direction::None, s) == Direction::Right);
    CHECK(Radial(0.9f * std::sin(a50), 0.9f * std::cos(a50), Direction::Up, s) == Direction::Up);
    float const a60 = 60.0f * 3.14159265f / 180.0f;
    CHECK(Radial(0.9f * std::sin(a60), 0.9f * std::cos(a60), Direction::Up, s) == Direction::Right);
    // Held: it stays down to the release radius, not the dead zone.
    CHECK(Radial(0.0f, 0.45f, Direction::Up, s) == Direction::Up);
    CHECK(Radial(0.0f, 0.3f, Direction::Up, s) == Direction::None);
    // Garbage in: nothing.
    CHECK(Radial(std::nanf(""), 0.9f, Direction::None, s) == Direction::None);
}

void TestSquadMachine() {
    using namespace mevr::vr::squad_menu;
    Machine m;
    Input in;
    in.dt = 0.011f;
    in.usable = true;
    in.memberCount = 2;
    in.members[0] = {0x0F, 2};  // carries all four, holds the assault rifle
    in.members[1] = {0x05, 0};  // pistol and assault rifle only, holds the pistol
    auto const step = [&]() { return m.Update(in); };

    // Point at the right squadmate and squeeze: open at once with that card.
    in.pointed = 1;
    in.gripDown = true;
    in.newAwaySqueeze = true;
    Step s = step();
    CHECK(s.opened && s.openedPointing && m.IsOpen() && m.Selected() == 1 && m.Highlighted() == -1);
    in.newAwaySqueeze = false;
    in.pointed = -1;
    // Flick up: the assault rifle (carried) is highlighted; centring keeps it.
    in.rightY = 0.9f;
    s = step();
    CHECK(s.tileChanged && m.Highlighted() == 2);
    in.rightY = 0.0f;
    s = step();
    CHECK(!s.tileChanged && m.Highlighted() == 2);
    // Flick right: the sniper rifle, which this one doesn't carry: refused, the highlight stays.
    in.rightX = 0.9f;
    s = step();
    CHECK(s.tileRefused && !s.tileChanged && m.Highlighted() == 2);
    s = step();
    CHECK(!s.tileRefused);  // one refusal per flick
    in.rightX = 0.0f;
    step();
    // Let go: confirmed, that card, that weapon.
    in.gripDown = false;
    s = step();
    CHECK(s.closed == Close::Confirmed && s.member == 1 && s.weapon == 2 && !m.IsOpen());

    // Let go with nothing chosen: cancelled.
    in.gripDown = true;
    in.newAwaySqueeze = true;
    in.pointed = 0;
    s = step();
    CHECK(s.opened && m.Selected() == 0);
    in.newAwaySqueeze = false;
    in.gripDown = false;
    s = step();
    CHECK(s.closed == Close::Released && s.weapon == -1);

    // B cancels; A confirms only with something chosen; an A held when it opened doesn't count.
    in.gripDown = true;
    in.newAwaySqueeze = true;
    in.a = true;
    s = step();
    CHECK(s.opened);
    in.newAwaySqueeze = false;
    s = step();
    CHECK(s.closed == Close::None);
    in.a = false;
    step();
    in.a = true;
    s = step();
    CHECK(s.closed == Close::None && m.IsOpen());  // A with nothing chosen: nothing
    in.a = false;
    in.rightX = -0.9f;  // the shotgun
    s = step();
    CHECK(m.Highlighted() == 1);
    in.rightX = 0.0f;
    in.a = true;
    s = step();
    CHECK(s.closed == Close::Confirmed && s.member == 0 && s.weapon == 1);
    in.a = false;
    in.gripDown = false;
    step();
    in.gripDown = true;
    in.newAwaySqueeze = true;
    step();
    in.newAwaySqueeze = false;
    in.b = true;
    s = step();
    CHECK(s.closed == Close::CancelButton);
    in.b = false;
    in.gripDown = false;
    step();

    // The left stick picks the card; a weapon the new one lacks is no longer chosen.
    in.gripDown = true;
    in.newAwaySqueeze = true;
    in.pointed = 0;
    step();
    in.newAwaySqueeze = false;
    in.rightY = -0.9f;  // the pistol: both carry it
    step();
    in.rightY = 0.0f;
    step();
    CHECK(m.Highlighted() == 0);
    in.leftX = 0.8f;
    s = step();
    CHECK(s.cardChanged && m.Selected() == 1 && m.Highlighted() == 0 && !s.tileChanged);
    in.leftX = 0.0f;
    step();
    in.leftX = -0.8f;
    s = step();
    CHECK(s.cardChanged && m.Selected() == 0);
    in.leftX = 0.0f;
    in.rightX = -0.9f;  // the shotgun (card 0 carries it)
    step();
    in.rightX = 0.0f;
    step();
    CHECK(m.Highlighted() == 1);
    in.leftX = 0.8f;  // card 1 has no shotgun
    s = step();
    CHECK(s.cardChanged && s.tileChanged && m.Highlighted() == -1);
    in.leftX = 0.0f;
    in.gripDown = false;
    s = step();
    CHECK(s.closed == Close::Released);

    // Sticks already pushed when it opens (walking, turning) choose nothing until they move on.
    in.leftX = -0.9f;
    in.rightY = 0.9f;
    in.gripDown = true;
    in.newAwaySqueeze = true;
    in.pointed = 1;
    s = step();
    CHECK(s.opened && m.Selected() == 1 && m.Highlighted() == -1);
    in.newAwaySqueeze = false;
    s = step();
    CHECK(!s.cardChanged && !s.tileChanged && m.Selected() == 1 && m.Highlighted() == -1);
    in.leftX = 0.0f;
    in.rightY = 0.0f;
    step();
    in.rightY = 0.9f;
    s = step();
    CHECK(s.tileChanged && m.Highlighted() == 2);
    in.rightY = 0.0f;
    // It can't stay open where it can't be used.
    in.usable = false;
    s = step();
    CHECK(s.closed == Close::Unusable && !m.IsOpen());
    in.usable = true;
    in.gripDown = false;
    step();

    // Pointing at nobody: it arms, and opens after the hold with the preferred card.
    in.pointed = -1;
    in.preferred = 1;
    in.openHoldSeconds = 0.3f;
    in.gripDown = true;
    in.newAwaySqueeze = true;
    s = step();
    CHECK(s.armed && !s.opened && m.CurrentPhase() == Phase::Arming);
    in.newAwaySqueeze = false;
    in.dt = 0.1f;
    s = step();
    s = step();
    CHECK(!s.opened);
    s = step();
    CHECK(s.opened && !s.openedPointing && m.Selected() == 1);
    in.gripDown = false;
    s = step();
    CHECK(s.closed == Close::Released);
    // Let go before the hold: nothing.
    in.gripDown = true;
    in.newAwaySqueeze = true;
    step();
    in.newAwaySqueeze = false;
    step();
    in.gripDown = false;
    s = step();
    CHECK(!s.opened && s.closed == Close::None && m.CurrentPhase() == Phase::Idle);
    // Pointing at someone while arming opens at once with them.
    in.gripDown = true;
    in.newAwaySqueeze = true;
    step();
    in.newAwaySqueeze = false;
    in.pointed = 0;
    s = step();
    CHECK(s.opened && s.openedPointing && m.Selected() == 0);
    in.pointed = -1;
    in.gripDown = false;
    step();
    in.dt = 0.011f;
    // A squeeze at nobody right by the gun is a missed foregrip grab; with no squad, a refusal.
    in.nearGun = true;
    in.gripDown = true;
    in.newAwaySqueeze = true;
    s = step();
    CHECK(s.missedGrab && m.CurrentPhase() == Phase::Idle);
    in.nearGun = false;
    in.gripDown = false;
    in.newAwaySqueeze = false;
    step();
    in.memberCount = 0;
    in.gripDown = true;
    in.newAwaySqueeze = true;
    s = step();
    CHECK(s.refused && !m.IsOpen());
    in.memberCount = 2;
    in.gripDown = false;
    in.newAwaySqueeze = false;
    step();
    // Not usable: a squeeze does nothing.
    in.usable = false;
    in.gripDown = true;
    in.newAwaySqueeze = true;
    in.pointed = 0;
    s = step();
    CHECK(!s.opened && !s.armed && m.CurrentPhase() == Phase::Idle);
    in.usable = true;
    in.gripDown = false;
    in.newAwaySqueeze = false;
    step();

    // confirmOnRelease off: letting go keeps it open; A confirms; a new squeeze cancels; it times out.
    in.confirmOnRelease = false;
    in.idleCloseSeconds = 1.0f;
    in.gripDown = true;
    in.newAwaySqueeze = true;
    in.pointed = 0;
    step();
    in.newAwaySqueeze = false;
    in.gripDown = false;
    s = step();
    CHECK(s.closed == Close::None && m.IsOpen());
    in.rightY = 0.9f;
    step();
    in.rightY = 0.0f;
    in.a = true;
    s = step();
    CHECK(s.closed == Close::Confirmed && s.weapon == 2);
    in.a = false;
    in.gripDown = true;
    in.newAwaySqueeze = true;
    step();
    in.newAwaySqueeze = false;
    in.gripDown = false;
    step();
    in.gripDown = true;
    in.newAwaySqueeze = true;
    s = step();
    CHECK(s.closed == Close::Resqueezed);
    in.gripDown = false;
    in.newAwaySqueeze = false;
    step();
    in.gripDown = true;
    in.newAwaySqueeze = true;
    step();
    in.newAwaySqueeze = false;
    in.gripDown = false;
    in.dt = 0.4f;
    s = step();
    s = step();
    CHECK(s.closed == Close::None);
    s = step();
    CHECK(s.closed == Close::Idle && !m.IsOpen());
}

void TestSquadPanelPlacement() {
    using namespace mevr::vr::squad_menu;
    XrVector3f const head{0.0f, 1.6f, 0.0f};
    XrVector3f const hand{-0.2f, 1.1f, -0.4f};
    XrPosef const pose = PlaceAtHand(hand, head, 0.27f);
    XrVector3f const d{pose.position.x - hand.x, pose.position.y - hand.y, pose.position.z - hand.z};
    CHECK(NearD(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), 0.27, 0.005));
    CHECK(d.y > 0.2f);                // mostly above the hand
    CHECK(d.x < 0.0f && d.z < 0.0f);  // and a little further out, away from the head
    // Its front (+Z) faces the eyes, its +X is level (no roll).
    XrVector3f const z = mevr::vr::overlay::Rotate(pose.orientation, {0.0f, 0.0f, 1.0f});
    XrVector3f const x = mevr::vr::overlay::Rotate(pose.orientation, {1.0f, 0.0f, 0.0f});
    XrVector3f const toHead{head.x - pose.position.x, head.y - pose.position.y, head.z - pose.position.z};
    float const n = std::sqrt(toHead.x * toHead.x + toHead.y * toHead.y + toHead.z * toHead.z);
    CHECK(NearD((z.x * toHead.x + z.y * toHead.y + z.z * toHead.z) / n, 1.0, 1e-3));
    CHECK(NearD(x.y, 0.0, 1e-4));
    float const q = pose.orientation.x * pose.orientation.x + pose.orientation.y * pose.orientation.y +
        pose.orientation.z * pose.orientation.z + pose.orientation.w * pose.orientation.w;
    CHECK(NearD(q, 1.0, 1e-4));
    // A hand straight under the head still gives a usable pose.
    XrPosef const under = PlaceAtHand({0.0f, 1.0f, 0.0f}, head, 0.27f);
    CHECK(std::isfinite(under.position.x) && std::isfinite(under.orientation.w));
}

void TestStickChord() {
    using namespace mevr::game::gamepad;
    StickChord c;
    std::uint64_t t = 10000;
    auto const step = [&](bool l, bool r, std::uint64_t advance) {
        t += advance;
        return UpdateStickChord(c, l, r, t);
    };
    // A tap: both down within the join window, both up within 300 ms of the first press: a snapshot.
    CHECK(!step(true, false, 0).tap);
    CHECK(!step(true, true, 40).tap);
    CHECK(!step(true, true, 100).hold);
    CHECK(!step(false, true, 60).tap);
    StickChordStep s = step(false, false, 30);
    CHECK(s.tap && !s.hold);
    // A hold: Halcyon's menu once at 400 ms, and no snapshot when they're let go.
    t += 1000;
    step(true, true, 0);
    CHECK(!step(true, true, 390).hold);
    s = step(true, true, 20);
    CHECK(s.hold && !s.tap);
    CHECK(!step(true, true, 500).hold);  // once per hold
    s = step(false, false, 10);
    CHECK(!s.tap && !s.hold);
    // Too slow to be a tap (350 ms), too short to be a hold: nothing.
    t += 1000;
    step(true, true, 0);
    s = step(false, false, 350);
    CHECK(!s.tap && !s.hold);
    // The second click too late (150 ms after the first): no tap, even if quick after.
    t += 1000;
    step(false, true, 0);
    step(true, true, 150);
    s = step(false, false, 60);
    CHECK(!s.tap);
    // One stick alone is never a chord.
    t += 1000;
    step(true, false, 0);
    s = step(false, false, 50);
    CHECK(!s.tap && !s.hold);
}

void TestClickGate() {
    using namespace mevr::game::gamepad;
    using Phase = ClickGate::Phase;
    ClickGate g;
    std::uint64_t t = 5000;
    auto const step = [&](bool click, bool other, std::uint64_t advance) {
        t += advance;
        return GateClick(g, click, other, t);
    };
    // Held alone: L3 from 100 ms on, until let go.
    CHECK(!step(true, false, 0) && g.phase == Phase::Pending);
    CHECK(!step(true, false, 60));
    CHECK(step(true, false, 50) && g.phase == Phase::Pass);
    CHECK(step(true, false, 300));
    CHECK(!step(false, false, 10) && g.phase == Phase::Idle);
    // A quick click alone: a short L3 right after it's let go.
    CHECK(!step(true, false, 100));
    CHECK(step(false, false, 40) && g.phase == Phase::Pulse);
    CHECK(step(false, false, 50));
    CHECK(!step(false, false, 40) && g.phase == Phase::Idle);
    // The right click joining within 100 ms: never L3.
    CHECK(!step(true, false, 100));
    CHECK(!step(true, true, 40) && g.phase == Phase::Blocked);
    CHECK(!step(true, false, 200));  // the right click let go first: L3 doesn't come back
    CHECK(!step(false, false, 10));
    // The right click joining late: L3 lets go and stays off.
    CHECK(!step(true, false, 100));
    CHECK(step(true, false, 150));
    CHECK(!step(true, true, 50) && g.phase == Phase::Blocked);
    CHECK(!step(true, false, 50));
    CHECK(!step(false, false, 10));
    // Pressed while the right click is held: never L3.
    CHECK(!step(true, true, 100) && g.phase == Phase::Blocked);
    CHECK(!step(false, false, 10));
    // No clock: it passes as is.
    g = {};
    CHECK(GateClick(g, true, false, 0));
    CHECK(!GateClick(g, false, false, 0));

    // Through the pad: L3 shows up 100 ms after the press; in a both-sticks chord it never does.
    mevr::vr::controllers::State state;
    Latches latches;
    state.timeMs = 9000;
    state.left.stickClick = true;
    auto pad = FromControllers(state, latches);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) == 0);
    state.timeMs += 110;
    pad = FromControllers(state, latches);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0);
    state.left.stickClick = false;
    state.timeMs += 10;
    pad = FromControllers(state, latches);
    state.timeMs += 1000;
    state.left.stickClick = true;
    pad = FromControllers(state, latches);
    state.right.stickClick = true;
    state.timeMs += 30;
    pad = FromControllers(state, latches);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) == 0);
    state.timeMs += 300;
    pad = FromControllers(state, latches);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) == 0);
}

void TestSquadPad() {
    using namespace mevr::game::gamepad;
    // The flag crosses the boundary.
    Options options;
    options.squadMenu = true;
    CHECK((ToFlags(options) & kFlagSquadMenu) != 0);
    CHECK(FromFlags(-1.0f, 0.12f, 0.22f, ToFlags(options)).squadMenu);
    CHECK(!FromFlags(-1.0f, 0.12f, 0.22f, kFlagLeftTrigger).squadMenu);

    // While open: no sticks, no A, B or stick clicks; the triggers and X/Y still pass.
    mevr::vr::controllers::State state;
    state.active = true;
    state.timeMs = 20000;
    state.left.stickX = 0.8f;
    state.right.stickY = 0.9f;
    state.right.primary = true;
    state.right.secondary = true;
    state.left.secondary = true;
    state.right.trigger = 1.0f;
    state.left.stickClick = true;
    Latches latches;
    auto pad = FromControllers(state, latches, options);
    CHECK(pad.sThumbLX == 0 && pad.sThumbLY == 0 && pad.sThumbRX == 0 && pad.sThumbRY == 0);
    CHECK((pad.wButtons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB)) == 0);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_Y) != 0 && pad.bRightTrigger == 255);
    // Closed: what was in use stays the menu's until released or centred...
    options.squadMenu = false;
    state.timeMs += 20;
    pad = FromControllers(state, latches, options);
    CHECK(pad.sThumbLX == 0 && pad.sThumbRY == 0 && (pad.wButtons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B)) == 0);
    // ...each on its own: A let go and pressed again is A; the right stick centred and pushed again turns.
    state.right.primary = false;
    state.right.stickY = 0.1f;
    state.timeMs += 20;
    pad = FromControllers(state, latches, options);
    state.right.primary = true;
    state.right.stickY = 0.9f;
    state.timeMs += 20;
    pad = FromControllers(state, latches, options);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_A) != 0 && pad.sThumbRY > 0);
    CHECK(pad.sThumbLX == 0 && (pad.wButtons & XINPUT_GAMEPAD_B) == 0);  // still held from the menu
    state.left.stickX = 0.0f;
    state.right.secondary = false;
    state.timeMs += 20;
    FromControllers(state, latches, options);
    state.left.stickX = 0.8f;
    state.right.secondary = true;
    state.timeMs += 20;
    pad = FromControllers(state, latches, options);
    CHECK(pad.sThumbLX > 0 && (pad.wButtons & XINPUT_GAMEPAD_B) != 0);

    // The left grip's report: squeezes away from the gun count; one on the foregrip doesn't.
    Latches grip;
    mevr::vr::controllers::State hands;
    hands.active = true;
    Options away;
    away.support = {0.5f, 0.12f, 0.22f};  // the hand half a metre from the foregrip
    hands.left.squeeze = 1.0f;
    FromControllers(hands, grip, away);
    CHECK(LeftGripReport(grip) == ((1u << 1) | 1u));
    FromControllers(hands, grip, away);
    CHECK(LeftGripReport(grip) == ((1u << 1) | 1u));  // the same squeeze
    hands.left.squeeze = 0.0f;
    FromControllers(hands, grip, away);
    CHECK(LeftGripReport(grip) == (1u << 1));
    Options onGun;
    onGun.support = {0.05f, 0.12f, 0.22f};
    hands.left.squeeze = 1.0f;
    FromControllers(hands, grip, onGun);
    CHECK(LeftGripReport(grip) == (1u << 1) && grip.left == LeftGrip::Support);
    hands.left.squeeze = 0.0f;
    FromControllers(hands, grip, onGun);
    // Not counted while Halcyon's menu is open (countKept off).
    Options inMenu = away;
    inMenu.countKept = false;
    hands.left.squeeze = 1.0f;
    FromControllers(hands, grip, inMenu);
    CHECK((LeftGripReport(grip) >> 1) == 1u);
}

void TestNativeWheelGeometry() {
    namespace wheel = mevr::vr::squad_menu::wheel;
    using wheel::Entry;
    // Every entry's centre angle reads back as that entry (the HUD movie's own tables).
    int seen = 0;
    for (int member = 0; member < wheel::kMembers; ++member) {
        for (int slot = 1; slot <= wheel::kSlots; ++slot) {
            int const angle = wheel::StickAngleOf({member, slot});
            CHECK(angle >= 0 && angle < 360);
            CHECK((wheel::EntryAtStickAngle(angle) == Entry{member, slot}));
            ++seen;
        }
    }
    CHECK(seen == 12);
    // The movie's arrow angle is 361 - nAngle: its boundaries, checked from both sides.
    auto const at = [](int arrow) { return wheel::EntryAtStickAngle(361 - arrow); };
    CHECK((at(1) == Entry{2, 1}));    // member 2's slot 1 wraps through the arrow's 0
    CHECK((at(11) == Entry{2, 1}));
    CHECK((at(12) == Entry{0, 4}));
    CHECK((at(168) == Entry{0, 1}));
    CHECK((at(169) == Entry{1, 4}));
    CHECK((at(269) == Entry{1, 1}));
    CHECK((at(270) == Entry{2, 4}));
    CHECK((at(346) == Entry{2, 2}));
    CHECK((at(347) == Entry{2, 1}));
    CHECK((at(360) == Entry{2, 1}));
    CHECK(wheel::EntryAtStickAngle(0) == wheel::EntryAtStickAngle(1));  // clamped like the movie
    CHECK(!wheel::EntryAtStickAngle(-1).Valid());                      // no wheel
    CHECK(wheel::StickAngleOf({}) == -1 && wheel::StickAngleOf({3, 1}) == -1 && wheel::StickAngleOf({0, 5}) == -1);
    // Slots <-> weapon types, as BioSFHandler_HUD.HandleEvent 11 converts them.
    CHECK(wheel::WeaponOfSlot(1) == 3 && wheel::WeaponOfSlot(2) == 0 && wheel::WeaponOfSlot(3) == 2 && wheel::WeaponOfSlot(4) == 1);
    for (int weapon = 0; weapon < 4; ++weapon) {
        CHECK(wheel::WeaponOfSlot(wheel::SlotOfWeapon(weapon)) == weapon);
    }
    CHECK(wheel::WeaponOfSlot(0) == -1 && wheel::SlotOfWeapon(4) == 0);
}

void TestNativeWheelMachine() {
    namespace wheel = mevr::vr::squad_menu::wheel;
    using wheel::Phase;
    wheel::Machine m;
    wheel::Input in;
    in.dt = 0.02f;
    in.usable = true;
    in.onFoot = true;
    // Pointing: the enter goes out with the squeeze; the wheel opens when the game's mode says so.
    in.gripDown = true;
    in.newAwaySqueeze = true;
    in.pointed = true;
    wheel::Step s = m.Update(in);
    CHECK(s.enter && s.openedPointing && m.CurrentPhase() == Phase::Opening && m.Holding());
    in.newAwaySqueeze = false;
    s = m.Update(in);
    CHECK(!s.enter && !s.opened && m.CurrentPhase() == Phase::Opening);
    in.onFoot = false;
    in.wheelActive = true;
    s = m.Update(in);
    CHECK(s.opened && m.CurrentPhase() == Phase::Open);
    // Let go with a carried entry highlighted: A for a moment, then the exit once it's queued.
    in.highlightValid = true;
    in.highlightCarried = true;
    in.gripDown = false;
    s = m.Update(in);
    CHECK(s.holdA && s.confirmed && !s.exit && m.CurrentPhase() == Phase::Confirming);
    s = m.Update(in);
    CHECK(s.holdA);  // 0.02 s < 0.08 s
    in.dt = 0.1f;
    s = m.Update(in);
    CHECK(!s.holdA && !s.exit);  // A let go; the queue hasn't taken it yet
    in.choiceRecorded = true;
    s = m.Update(in);
    CHECK(s.exit && m.CurrentPhase() == Phase::Closing);
    in.wheelActive = false;
    in.onFoot = true;
    s = m.Update(in);
    CHECK(s.closed && m.CurrentPhase() == Phase::Idle && !m.Holding());

    // At nobody: held 0.35 s; near the foregrip: a missed grab.
    m.Reset();
    in = {};
    in.dt = 0.1f;
    in.usable = true;
    in.onFoot = true;
    in.gripDown = true;
    in.newAwaySqueeze = true;
    s = m.Update(in);
    CHECK(s.armed && m.CurrentPhase() == Phase::Arming);
    in.newAwaySqueeze = false;
    m.Update(in);
    m.Update(in);
    CHECK(m.CurrentPhase() == Phase::Arming);
    s = m.Update(in);
    s = m.Update(in);
    CHECK(s.enter && !s.openedPointing && m.CurrentPhase() == Phase::Opening);
    m.Reset();
    in.newAwaySqueeze = true;
    in.nearGun = true;
    s = m.Update(in);
    CHECK(s.missedGrab && m.CurrentPhase() == Phase::Idle);
    // Let go early while arming: nothing.
    in.nearGun = false;
    s = m.Update(in);
    CHECK(s.armed);
    in.newAwaySqueeze = false;
    in.gripDown = false;
    m.Update(in);
    CHECK(m.CurrentPhase() == Phase::Idle);
    // Not on foot (the Mako, a conversation): no wheel.
    in.gripDown = true;
    in.newAwaySqueeze = true;
    in.onFoot = false;
    s = m.Update(in);
    CHECK(!s.armed && !s.enter && m.CurrentPhase() == Phase::Idle);

    // The game never enters the wheel (combat disabled in a hub): given up after the timeout.
    m.Reset();
    in = {};
    in.dt = 0.1f;
    in.usable = true;
    in.onFoot = true;
    in.gripDown = true;
    in.newAwaySqueeze = true;
    in.pointed = true;
    m.Update(in);
    in.newAwaySqueeze = false;
    int updates = 0;
    bool failed = false;
    while (!failed && updates < 20) {
        failed = m.Update(in).openFailed;
        ++updates;
    }
    CHECK(failed && updates >= 6 && updates <= 7 && m.CurrentPhase() == Phase::Idle);

    // A quick squeeze-and-let-go that the game still opens: shut at once, nothing given.
    m.Reset();
    in.newAwaySqueeze = true;
    m.Update(in);
    in.newAwaySqueeze = false;
    in.gripDown = false;
    m.Update(in);
    in.wheelActive = true;
    in.highlightValid = true;
    in.highlightCarried = true;
    s = m.Update(in);
    CHECK(s.opened && s.exit && !s.holdA && m.CurrentPhase() == Phase::Closing);

    // B closes without an A; a B held from before the wheel opened doesn't.
    m.Reset();
    in = {};
    in.dt = 0.02f;
    in.usable = true;
    in.onFoot = true;
    in.gripDown = true;
    in.newAwaySqueeze = true;
    in.pointed = true;
    in.b = true;
    m.Update(in);
    in.newAwaySqueeze = false;
    in.wheelActive = true;
    m.Update(in);
    s = m.Update(in);
    CHECK(!s.exit && m.CurrentPhase() == Phase::Open);
    in.b = false;
    m.Update(in);
    in.b = true;
    in.highlightValid = true;
    in.highlightCarried = true;
    s = m.Update(in);
    CHECK(s.exit && s.cancelled && !s.holdA && m.CurrentPhase() == Phase::Closing);
    // The mode doesn't leave: one more exit, then given up.
    in.dt = 0.3f;
    s = m.Update(in);
    s = m.Update(in);
    CHECK(s.exit && !s.exitStuck);
    s = m.Update(in);
    s = m.Update(in);
    CHECK(s.exitStuck && m.CurrentPhase() == Phase::Idle);

    // confirmOnRelease off, or an entry they don't carry, or one already queued: no A.
    for (int variant = 0; variant < 3; ++variant) {
        m.Reset();
        in = {};
        in.dt = 0.02f;
        in.usable = true;
        in.onFoot = true;
        in.gripDown = true;
        in.newAwaySqueeze = true;
        in.pointed = true;
        m.Update(in);
        in.newAwaySqueeze = false;
        in.wheelActive = true;
        m.Update(in);
        in.highlightValid = true;
        in.highlightCarried = variant != 1;
        in.confirmOnRelease = variant != 0;
        in.choiceRecorded = variant == 2;
        in.gripDown = false;
        s = m.Update(in);
        CHECK(s.exit && !s.holdA && m.CurrentPhase() == Phase::Closing);
        CHECK(s.cancelled == (variant != 2));
    }
    // The game closes it by itself (LB let go, the pause menu): back to idle, nothing to call.
    m.Reset();
    in = {};
    in.usable = true;
    in.onFoot = true;
    in.gripDown = true;
    in.newAwaySqueeze = true;
    in.pointed = true;
    m.Update(in);
    in.newAwaySqueeze = false;
    in.wheelActive = true;
    m.Update(in);
    in.wheelActive = false;
    s = m.Update(in);
    CHECK(s.closedByGame && !s.exit && m.CurrentPhase() == Phase::Idle);
}

void TestNativeWheelPad() {
    using namespace mevr::game::gamepad;
    // The flags cross the boundary.
    Options options;
    options.nativeWheel = true;
    options.wheelStick = WheelStick::Either;
    options.wheelPressA = true;
    Options const back = FromFlags(-1.0f, 0.12f, 0.22f, ToFlags(options));
    CHECK(back.nativeWheel && back.wheelPressA && back.wheelStick == WheelStick::Either && !back.squadMenu);
    CHECK(FromFlags(-1.0f, 0.12f, 0.22f, kFlagLeftTrigger).wheelStick == WheelStick::Right);
    CHECK(FromFlags(-1.0f, 0.12f, 0.22f, 3u << kWheelStickShift).wheelStick == WheelStick::Right);  // unknown: right

    // A right stick pushed (turning) when the wheel opens steers nothing until it has been centred.
    options.wheelPressA = false;
    options.wheelStick = WheelStick::Right;
    mevr::vr::controllers::State state;
    state.active = true;
    state.timeMs = 30000;
    state.right.stickX = 0.9f;
    state.left.stickY = 0.8f;
    Latches latches;
    auto pad = FromControllers(state, latches, options);
    CHECK(pad.sThumbRX == 0 && pad.sThumbLY == 0);
    state.right.stickX = 0.1f;
    state.timeMs += 20;
    FromControllers(state, latches, options);
    state.right.stickX = -0.9f;
    state.timeMs += 20;
    pad = FromControllers(state, latches, options);
    CHECK(pad.sThumbRX < -20000 && pad.sThumbLY == 0);  // steers; the left stick never walks
    // A reaches the game (it queues the highlighted entry); B and the clicks don't; the mod's own A too.
    state.right.primary = true;
    state.right.secondary = true;
    state.right.stickClick = true;
    pad = FromControllers(state, latches, options);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_A) != 0);
    CHECK((pad.wButtons & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_RIGHT_THUMB)) == 0);
    state.right.primary = false;
    options.wheelPressA = true;
    pad = FromControllers(state, latches, options);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_A) != 0);
    options.wheelPressA = false;
    // Closed: the steering stick and B stay kept until centred and let go.
    options.nativeWheel = false;
    state.timeMs += 20;
    pad = FromControllers(state, latches, options);
    CHECK(pad.sThumbRX == 0 && (pad.wButtons & XINPUT_GAMEPAD_B) == 0);
    // The mod's A only with the wheel flag.
    options.wheelPressA = true;
    state.right.secondary = false;
    pad = FromControllers(state, latches, options);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_A) == 0);

    // The left stick steering: it reaches the game as the right stick; the right stick is kept.
    Latches left;
    Options leftOptions;
    leftOptions.nativeWheel = true;
    leftOptions.wheelStick = WheelStick::Left;
    mevr::vr::controllers::State hands;
    hands.active = true;
    hands.timeMs = 40000;
    FromControllers(hands, left, leftOptions);  // centred: armed
    hands.left.stickX = 0.7f;
    hands.left.stickY = 0.7f;
    hands.right.stickX = -0.9f;
    pad = FromControllers(hands, left, leftOptions);
    CHECK(pad.sThumbRX > 20000 && pad.sThumbRY > 20000 && pad.sThumbLX == 0 && pad.sThumbLY == 0);
    // Either: the one pushed further.
    leftOptions.wheelStick = WheelStick::Either;
    pad = FromControllers(hands, left, leftOptions);
    CHECK(pad.sThumbRX > 20000);  // the left (0.99) beats the right (0.9)
    hands.right.stickX = -1.0f;
    hands.left.stickX = 0.3f;
    hands.left.stickY = 0.3f;
    pad = FromControllers(hands, left, leftOptions);
    CHECK(pad.sThumbRX < -30000);
}

void TestWheelCrop() {
    namespace crop = mevr::vr::wheel_crop;
    // The default: the stage shown whole, top left. A near-square VR frame fits the stage's width.
    crop::Rect const vr = crop::DefaultCrop(3072, 3188);
    CHECK(NearD(vr.x, 282.0 * 2.4 / 3072.0, 1e-4) && NearD(vr.width, 716.0 * 2.4 / 3072.0, 1e-4));
    CHECK(NearD(vr.y, 61.0 * 2.4 / 3188.0, 1e-4) && NearD(vr.height, 588.0 * 2.4 / 3188.0, 1e-4));
    crop::Rect const flat = crop::DefaultCrop(1920, 1080);  // 16:9: the stage itself
    CHECK(NearD(flat.x, 282.0 / 1280.0, 1e-4) && NearD(flat.y, 61.0 / 720.0, 1e-4) && NearD(flat.height, 588.0 / 720.0, 1e-4));
    CHECK(!crop::DefaultCrop(0, 100).Valid());

    // Measuring: what appeared when the wheel opened.
    crop::Grid before;
    before.columns = 48;
    before.rows = 50;
    before.cells.assign(48 * 50, 0);
    auto const set = [](crop::Grid& g, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::uint8_t v) {
        for (std::uint32_t y = y0; y <= y1; ++y) {
            for (std::uint32_t x = x0; x <= x1; ++x) {
                g.cells[y * g.columns + x] = v;
            }
        }
    };
    set(before, 40, 44, 46, 49, 200);  // the radar, there all along
    set(before, 20, 30, 22, 31, 30);   // a faint HUD element
    crop::Grid wheel = before;
    set(wheel, 11, 4, 36, 22, 180);    // the wheel
    set(wheel, 2, 46, 3, 47, 255);     // a small HUD change elsewhere (4 cells)
    set(wheel, 40, 44, 46, 49, 90);    // the radar dimmed: not "appeared"
    auto const measured = crop::MeasureCrop(before, wheel);
    CHECK(measured.has_value());
    if (measured) {
        CHECK(measured->cells == 26u * 19u && measured->changed == 26u * 19u + 4u);
        CHECK(NearD(measured->rect.x, 10.0 / 48.0, 1e-5) && NearD(measured->rect.y, 3.0 / 50.0, 1e-5));
        CHECK(NearD(measured->rect.width, 28.0 / 48.0, 1e-5) && NearD(measured->rect.height, 21.0 / 50.0, 1e-5));
    }
    // At the frame's edge the padding is clipped.
    crop::Grid edge = before;
    set(edge, 0, 0, 5, 5, 255);
    auto const corner = crop::MeasureCrop(before, edge);
    CHECK(corner.has_value() && corner->rect.x == 0.0f && corner->rect.y == 0.0f && NearD(corner->rect.width, 7.0 / 48.0, 1e-5));
    // Nothing big enough, or grids that don't match: no measurement.
    crop::Grid speck = before;
    set(speck, 5, 5, 7, 7, 255);  // 9 cells
    CHECK(!crop::MeasureCrop(before, speck).has_value());
    crop::Grid other;
    other.columns = 10;
    other.rows = 10;
    other.cells.assign(100, 255);
    CHECK(!crop::MeasureCrop(before, other).has_value());

    // To pixels: rounded, inside the frame, at least 16 wide.
    crop::Pixels const p = crop::ToPixels({0.25f, 0.5f, 0.5f, 0.25f}, 1000, 800);
    CHECK(p.x == 250 && p.y == 400 && p.width == 500 && p.height == 200);
    crop::Pixels const tiny = crop::ToPixels({0.995f, 0.0f, 0.001f, 1.5f}, 1000, 800);
    CHECK(tiny.width == 16 && tiny.x + tiny.width <= 1000 && tiny.y == 0 && tiny.height == 800);
}

// ---- The recenter that corrects a tilted base space (core/vr/Recenter) ----------------------------------
bool SameQuat(XrQuaternionf a, XrQuaternionf b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
// The same rotation (q and -q alike).
bool NearQuat(XrQuaternionf a, XrQuaternionf b, double tolerance = 1e-4) {
    double const dot = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return dot >= 1.0 - tolerance;
}

void TestRecenterTilt() {
    namespace rc = mevr::vr::recenter;
    rc::Settings const settings;  // 60 degrees, 1.5 s
    XrQuaternionf const identity{0.0f, 0.0f, 0.0f, 1.0f};

    // Level, and level turned: nothing to correct; upstream's yaw-only pose comes back bit for bit.
    rc::Tilt const level = rc::Measure(identity);
    CHECK(NearD(level.upAngleDeg, 0.0, 0.05) && NearD(level.rollDeg, 0.0, 0.05) && NearD(level.pitchDeg, 0.0, 0.05));
    XrQuaternionf const yaw = AxisAngle(0, 1, 0, 40);
    rc::Decision d = rc::Decide(yaw, yaw, settings, false, 10.0f);
    CHECK(d.kind == rc::Kind::YawOnly && SameQuat(d.orientation, yaw));

    // Rolled (positive = right ear down: about the forward, -Z) and pitched (about +X).
    rc::Tilt const rolled = rc::Measure(AxisAngle(0, 0, -1, 30));
    CHECK(NearD(rolled.rollDeg, 30.0, 0.05) && NearD(rolled.upAngleDeg, 30.0, 0.05) && NearD(rolled.pitchDeg, 0.0, 0.05));
    rc::Tilt const down = rc::Measure(AxisAngle(1, 0, 0, -70));
    CHECK(NearD(down.pitchDeg, -70.0, 0.05) && NearD(down.upAngleDeg, 70.0, 0.05) && NearD(down.rollDeg, 0.0, 0.05));
    rc::Tilt const straightDown = rc::Measure(AxisAngle(1, 0, 0, -90));
    CHECK(!straightDown.rollKnown && NearD(straightDown.upAngleDeg, 90.0, 0.05));
    // Yaw doesn't change any of them.
    rc::Tilt const turnedRolled = rc::Measure(rc::Compose(AxisAngle(0, 1, 0, 120), AxisAngle(0, 0, -1, 30)));
    CHECK(NearD(turnedRolled.rollDeg, 30.0, 0.05) && NearD(turnedRolled.upAngleDeg, 30.0, 0.05));

    // Upside down (SteamVR's bad start: the head rolled 180 degrees in the base space): corrected at once
    // on a recenter the player asks for, never on the automatic first one, never with the setting off.
    XrQuaternionf const upsideDown = rc::Compose(AxisAngle(0, 1, 0, 25), AxisAngle(0, 0, 1, 180));
    rc::Tilt const flipped = rc::Measure(upsideDown);
    CHECK(rc::UpsideDown(flipped) && NearD(flipped.upAngleDeg, 180.0, 0.05) && NearD(std::abs(flipped.rollDeg), 180.0, 0.05));
    d = rc::Decide(upsideDown, yaw, settings, false, 0.0f);
    CHECK(d.kind == rc::Kind::Corrected && NearQuat(d.orientation, upsideDown));
    // After it, the head sits level and straight ahead in the new app space (the app's orientation
    // undone: conj(app) * head).
    XrQuaternionf const app = d.orientation;
    XrQuaternionf const inApp = rc::Compose({-app.x, -app.y, -app.z, app.w}, upsideDown);
    CHECK(NearQuat(inApp, identity));
    // ...and a later head motion keeps its sense: nodding down in the room is nodding down in the app space.
    XrQuaternionf const nodInApp = rc::Compose({-app.x, -app.y, -app.z, app.w}, rc::Compose(upsideDown, AxisAngle(1, 0, 0, -20)));
    CHECK(NearD(rc::Measure(nodInApp).pitchDeg, -20.0, 0.05) && NearD(rc::Measure(nodInApp).rollDeg, 0.0, 0.05));
    CHECK(rc::Decide(upsideDown, yaw, settings, true, 10.0f).kind == rc::Kind::YawOnly);
    rc::Settings off = settings;
    off.tiltDeg = 0.0f;
    d = rc::Decide(upsideDown, yaw, off, false, 10.0f);
    CHECK(d.kind == rc::Kind::YawOnly && SameQuat(d.orientation, yaw));
    // Rolled past 90 on its side: the same.
    CHECK(rc::Decide(AxisAngle(0, 0, -1, 100), yaw, settings, false, 0.0f).kind == rc::Kind::Corrected);

    // Looking down 70 degrees: tilted past 60, but it's a glance at the hands until it has lasted.
    XrQuaternionf const lookDown = AxisAngle(1, 0, 0, -70);
    CHECK(rc::Tilted(rc::Measure(lookDown), settings) && !rc::UpsideDown(rc::Measure(lookDown)));
    CHECK(rc::Decide(lookDown, yaw, settings, false, 0.3f).kind == rc::Kind::YawOnly);
    CHECK(rc::Decide(lookDown, yaw, settings, false, 2.0f).kind == rc::Kind::Corrected);
    // 50 degrees down: never.
    CHECK(rc::Decide(AxisAngle(1, 0, 0, -50), yaw, settings, false, 30.0f).kind == rc::Kind::YawOnly);
    // Straight down (no roll to speak of): only when it has lasted.
    CHECK(rc::Decide(AxisAngle(1, 0, 0, -90), yaw, settings, false, 0.0f).kind == rc::Kind::YawOnly);

    // The watch: grows while tilted, shrinks three times as fast, a hitch counts as a quarter second.
    rc::TiltWatch watch;
    for (int i = 0; i < 20; ++i) {
        watch.Note(true, 0.1f);
    }
    CHECK(NearD(watch.Seconds(), 2.0, 1e-4));
    for (int i = 0; i < 5; ++i) {
        watch.Note(false, 0.1f);
    }
    CHECK(NearD(watch.Seconds(), 0.5, 1e-4));
    watch.Note(true, 5.0f);
    CHECK(NearD(watch.Seconds(), 0.75, 1e-4));
    watch.Note(false, 1.0f);
    CHECK(watch.Seconds() < 1e-6f);
}

// ---- Sights and scopes (core/vr/Sights) ------------------------------------------------------------------
void TestSightSettings() {
    namespace s = mevr::vr::sights;
    s::Gun const none;
    auto gun = s::ParseGun("scope 6", none);
    CHECK(gun && gun->kind == s::Kind::Scope && gun->zoom == 6.0f);
    gun = s::ParseGun(" Holo ", none);
    CHECK(gun && gun->kind == s::Kind::Holo && gun->zoom == 1.0f);
    gun = s::ParseGun("scope", s::Gun{s::Kind::Scope, 3.0f});
    CHECK(gun && gun->kind == s::Kind::Scope && gun->zoom == 3.0f);
    gun = s::ParseGun("scope", none);
    CHECK(gun && gun->zoom == 4.0f);
    gun = s::ParseGun("scope 40", none);
    CHECK(gun && gun->zoom == 12.0f);
    CHECK(s::ParseGun("dot 4", none) == std::nullopt);
    CHECK(s::ParseGun("laser", none) == std::nullopt);
    CHECK(s::ParseGun("", none) == std::nullopt);
    gun = s::ParseGun("none", s::Gun{s::Kind::Dot});
    CHECK(gun && gun->kind == s::Kind::None);

    s::Gun place;
    CHECK(s::ParsePlace("30, 6", place) && !place.autoPlace && place.back == 30.0f && place.height == 6.0f);
    CHECK(s::ParsePlace("auto", place) && place.autoPlace);
    CHECK(!s::ParsePlace("30", place) && !s::ParsePlace("a, b", place));

    CHECK(s::ParseColor("#00ff00") == 0x00FF00u && s::ParseColor("FF3020") == 0xFF3020u && s::ParseColor("0x123456") == 0x123456u);
    CHECK(s::ParseColor("green") == 0x30FF40u && s::ParseColor("12345") == std::nullopt && s::ParseColor("zzzzzz") == std::nullopt);
    CHECK(s::ParseScopeReticle("mil-dot") == s::ScopeReticle::MilDot && s::ParseScopeReticle("x") == std::nullopt);

    // Auto placement: a share of the gun's muzzle-to-grip length, a typical one when it's unknown.
    s::Gun const placed = s::AutoPlace(0, s::Gun{s::Kind::Dot}, 27.2f);
    CHECK(NearD(placed.back, 0.75 * 27.2, 1e-4) && placed.height > 0.0f);
    CHECK(NearD(s::AutoPlace(3, s::Gun{s::Kind::Scope, 4.0f}, 0.0f).back, 1.05 * 55.0, 1e-4));
    s::Gun fixed{s::Kind::Holo};
    fixed.autoPlace = false;
    fixed.back = 12.0f;
    CHECK(s::AutoPlace(2, fixed, 39.0f).back == 12.0f);
}

void TestSightGeometry() {
    namespace s = mevr::vr::sights;
    using mevr::vr::overlay::Rotate;
    XrVector3f const muzzle{0.0f, 0.0f, -1.0f};
    XrVector3f const barrel{0.0f, 0.0f, -1.0f};
    XrVector3f const up{0.0f, 1.0f, 0.0f};
    // The sight 0.3 m behind the muzzle and 5 cm over the bore.
    XrVector3f const center = s::Place(muzzle, barrel, up, 0.3f, 0.05f);
    CHECK(NearXr(center, {0.0f, 0.05f, -0.7f}));
    // The gun's top: the controller's up made perpendicular to the barrel.
    auto const top = s::TopOf(barrel, {0.1f, 1.0f, 0.3f});
    CHECK(top && NearD(top->z, 0.0, 1e-5) && NearD(top->x * top->x + top->y * top->y, 1.0, 1e-5));
    CHECK(!s::TopOf(barrel, {0.0f, 0.0f, 2.0f}));
    // Zeroing: parallel without, down onto the shot's line at the zero.
    CHECK(NearXr(s::Zeroed(center, muzzle, barrel, 0.0f), barrel));
    XrVector3f const zeroed = s::Zeroed(center, muzzle, barrel, 10.0f);
    CHECK(zeroed.y < 0.0f && NearD(zeroed.y / -zeroed.z, -0.05 / 10.3, 1e-4));

    // An eye 10 cm behind the sight, 1 cm to the right of its line.
    s::Line const line = s::EyeLine({0.01f, 0.05f, -0.6f}, center, barrel, up);
    CHECK(NearD(line.along, 0.1, 1e-5) && NearD(line.lateral, 0.01, 1e-5) && NearD(line.x, 0.01, 1e-5) && NearD(line.y, 0.0, 1e-5));
    // In front of the sight: negative along.
    CHECK(s::EyeLine({0.0f, 0.05f, -0.9f}, center, barrel, up).along < 0.0f);

    // Looking through a 2 cm window: on inside it, kept to 2.5 cm, off beyond, back on only inside 2 cm.
    s::Looking looking;
    auto at = [](float lateral, float along) {
        s::Line l;
        l.lateral = lateral;
        l.along = along;
        return l;
    };
    CHECK(looking.Update(at(0.019f, 0.1f), 0.02f, 0.02f, 1.0f));
    CHECK(looking.Update(at(0.024f, 0.1f), 0.02f, 0.02f, 1.0f));
    CHECK(!looking.Update(at(0.026f, 0.1f), 0.02f, 0.02f, 1.0f));
    CHECK(!looking.Update(at(0.022f, 0.1f), 0.02f, 0.02f, 1.0f));
    CHECK(!looking.Update(at(0.01f, 0.01f), 0.02f, 0.02f, 1.0f));  // too close
    CHECK(!looking.Update(at(0.01f, 1.5f), 0.02f, 0.02f, 1.0f));   // too far
    CHECK(looking.Update(at(0.01f, 0.5f), 0.02f, 0.02f, 1.0f));

    // Which eye aims through a scope (box 4 cm): the nearer one; the current one kept unless clearly worse.
    s::Line lines[2] = {at(0.07f, 0.1f), at(0.01f, 0.1f)};
    CHECK(s::AimingEye(lines, 0.04f, 0.01f, 0.6f, -1) == 1);
    lines[1] = at(0.045f, 0.1f);
    CHECK(s::AimingEye(lines, 0.04f, 0.01f, 0.6f, 1) == 1);   // kept within 1.25 box
    CHECK(s::AimingEye(lines, 0.04f, 0.01f, 0.6f, -1) == -1); // but not taken there
    lines[0] = at(0.005f, 0.1f);
    CHECK(s::AimingEye(lines, 0.04f, 0.01f, 0.6f, 1) == 0);   // the other is clearly nearer
    lines[0] = at(0.035f, 0.1f);
    lines[1] = at(0.04f, 0.1f);
    CHECK(s::AimingEye(lines, 0.04f, 0.01f, 0.6f, 1) == 1);   // nearer, but not clearly

    // The collimated reticle: 100 m out along the line, facing back, 1 degree across.
    s::Quad const reticle = s::Reticle({0.0f, 0.0f, 0.0f}, barrel, up, 100.0f, static_cast<float>(std::numbers::pi / 180.0));
    CHECK(NearXr(reticle.pose.position, {0.0f, 0.0f, -100.0f}) && NearD(reticle.size, 200.0 * std::tan(std::numbers::pi / 360.0), 1e-3));
    CHECK(NearXr(Rotate(reticle.pose.orientation, {0.0f, 0.0f, 1.0f}), {0.0f, 0.0f, 1.0f}) &&
        NearXr(Rotate(reticle.pose.orientation, {0.0f, 1.0f, 0.0f}), up));
    s::Quad const disc = s::Disc(center, barrel, up, 0.02f);
    CHECK(NearXr(disc.pose.position, center) && NearD(disc.size, 0.04, 1e-6));
}

void TestScopeMapping() {
    namespace s = mevr::vr::sights;
    XrVector3f const axis{0.0f, 0.0f, -1.0f};
    XrVector3f const up{0.0f, 1.0f, 0.0f};
    auto const lens = s::MakeLens({0.0f, 0.0f, -0.1f}, axis, up, 0.02f);
    CHECK(lens && NearXr(lens->right, {1.0f, 0.0f, 0.0f}));
    if (!lens) {
        return;
    }
    // An eye 10 cm behind the eyepiece, on its line.
    auto view = s::View(*lens, {0.0f, 0.0f, 0.0f}, 0.1f);
    CHECK(view && NearD(view->along, 0.1, 1e-6) && NearD(view->qx, 0.0, 1e-6) && view->pupil == 1.0f);
    if (!view) {
        return;
    }
    // Zoom 1: a lens point shows what's behind it (the direction from the eye to it).
    CHECK(NearXr(s::LensDirection(*lens, *view, 1.0f, 0.0f, 1.0f), {0.02f, 0.0f, -0.1f}));
    CHECK(NearXr(s::LensDirection(*lens, *view, 0.0f, -1.0f, 1.0f), {0.0f, -0.02f, -0.1f}));
    // Zoom 4: the angle off the line (its tangent) is a quarter; the centre stays on the line.
    CHECK(NearXr(s::LensDirection(*lens, *view, 1.0f, 0.0f, 4.0f), {0.005f, 0.0f, -0.1f}));
    CHECK(NearXr(s::LensDirection(*lens, *view, 0.0f, 0.0f, 4.0f), {0.0f, 0.0f, -0.1f}));
    // An eye 1 cm off to the right: the line through it meets the lens half a radius right, and that
    // point shows straight along the sight line (collimated: the reticle there stays on target).
    auto const off = s::View(*lens, {0.01f, 0.0f, 0.0f}, 0.1f);
    CHECK(off && NearD(off->qx, 0.5, 1e-5) && NearD(off->qy, 0.0, 1e-5));
    if (off) {
        CHECK(NearXr(s::LensDirection(*lens, *off, 0.5f, 0.0f, 4.0f), {0.0f, 0.0f, -0.1f}));
    }
    // Eye relief: full up to 1.5 reliefs, then shrinking, none past 6.
    auto const back = s::View(*lens, {0.0f, 0.0f, 0.2f}, 0.1f);  // 0.3 m back
    CHECK(back && NearD(back->pupil, 0.5, 1e-5));
    CHECK(!s::View(*lens, {0.0f, 0.0f, 0.6f}, 0.1f));    // 0.7 m back
    CHECK(!s::View(*lens, {0.0f, 0.0f, -0.12f}, 0.1f));  // in front of the eyepiece

    // Where the magnified direction lands: 40 m out along the axis, then into an eye image.
    XrVector3f const point = s::FocusPoint({0.0f, 0.0f, 0.0f}, s::LensDirection(*lens, *view, 1.0f, 0.0f, 4.0f), axis, 40.0f);
    CHECK(NearXr(point, {2.0f, 0.0f, -40.0f}, 1e-3));
    XrPosef const eye{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrFovf const fov{-0.7853982f, 0.7853982f, 0.7853982f, -0.7853982f};
    float u = 0.0f;
    float v = 0.0f;
    CHECK(s::Project(eye, fov, {0.0f, 0.0f, -10.0f}, u, v) && NearD(u, 0.5, 1e-5) && NearD(v, 0.5, 1e-5));
    CHECK(s::Project(eye, fov, {10.0f, 0.0f, -10.0f}, u, v) && NearD(u, 1.0, 1e-4));
    CHECK(s::Project(eye, fov, {0.0f, 10.0f, -10.0f}, u, v) && NearD(v, 0.0, 1e-4));
    CHECK(!s::Project(eye, fov, {0.0f, 0.0f, 1.0f}, u, v));
    // Turned 90 degrees left, the image's centre looks along -X.
    XrPosef const turned{AxisAngle(0, 1, 0, 90), {0.0f, 0.0f, 0.0f}};
    CHECK(s::Project(turned, fov, {-10.0f, 0.0f, 0.0f}, u, v) && NearD(u, 0.5, 1e-4) && NearD(v, 0.5, 1e-4));
    // The other eye (6.4 cm left) as the source: something at the focus distance on the sight line lands
    // exactly where that eye sees it, so the lens centre shows it whichever eye's image is magnified.
    XrPosef const left{{0.0f, 0.0f, 0.0f, 1.0f}, {-0.064f, 0.0f, 0.0f}};
    XrVector3f const onLine = s::FocusPoint({0.0f, 0.0f, 0.0f}, s::LensDirection(*lens, *view, 0.0f, 0.0f, 4.0f), axis, 40.0f);
    CHECK(NearXr(onLine, {0.0f, 0.0f, -40.0f}, 1e-4));
    CHECK(s::Project(left, fov, onLine, u, v) && NearD(u, 0.5 + 0.5 * 0.064 / 40.0, 1e-5) && NearD(v, 0.5, 1e-5));
}

int main() {
    TestRecenterTilt();
    TestSightSettings();
    TestSightGeometry();
    TestScopeMapping();
    TestBoneNames();
    TestIk();
    TestRigid();
    TestOrientation();
    TestArmChains();
    TestArmPose();
    TestArmStretch();
    TestTorsoPose();
    TestCameraHook();
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
    TestSupportGrip();
    TestRightGripFree();
    TestRightGripHolds();
    TestRbFromStickClick();
    TestStickChord();
    TestClickGate();
    TestSquadPad();
    TestSquadPointing();
    TestSquadRadial();
    TestSquadMachine();
    TestSquadPanelPlacement();
    TestNativeWheelGeometry();
    TestNativeWheelMachine();
    TestNativeWheelPad();
    TestWheelCrop();
    TestBodySlotGeometry();
    TestBodySlotMachine();
    TestBodySlotHolding();
    TestContactHaptics();
    TestStandingHeight();
    TestHapticChannel();
    TestTwoHand();
    TestOverlayPlacement();
    TestMenuPointer();
    TestWorldPanel();
    TestPoseRecording();
    TestImageConvert();
    TestLayerComposite();
    TestTestRequest();
    TestPng();
    if (gFailures == 0) {
        std::printf("all tests passed\n");
    }
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

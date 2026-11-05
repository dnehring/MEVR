// SPDX-License-Identifier: MIT
// Offline tests for the math that has to be right before it reaches a headset. No game, no
// runtime: `ctest -C Release --test-dir build`.

#include "core/DevChannels.hpp"
#include "core/DiskBudget.hpp"
#include "core/Ini.hpp"
#include "core/PngFile.hpp"
#include "core/ik/ArmLength.hpp"
#include "core/ik/ArmPose.hpp"
#include "core/ik/BodyFollow.hpp"
#include "core/ik/BodySlots.hpp"
#include "core/ik/BoneNames.hpp"
#include "core/ik/CameraHook.hpp"
#include "core/ik/Fingers.hpp"
#include "core/ik/HandFrame.hpp"
#include "core/ik/Ik.hpp"
#include "core/ik/PoseGraft.hpp"
#include "core/ik/Posture.hpp"
#include "core/ik/Rigid.hpp"
#include "core/ik/Throwing.hpp"
#include "core/ik/TorsoPose.hpp"
#include "core/ik/TouchReference.hpp"
#include "core/ik/TwoHand.hpp"
#include "core/ik/Vent.hpp"
#include "core/vr/Calibration.hpp"
#include "core/vr/Colour.hpp"
#include "core/vr/ColourStrip.hpp"
#include "core/vr/Upscale.hpp"
#include "core/vr/CombatStance.hpp"
#include "core/vr/Comfort.hpp"
#include "core/vr/GameMenuRequest.hpp"
#include "core/vr/GamePointer.hpp"
#include "core/vr/Haptics.hpp"
#include "core/vr/HudPieces.hpp"
#include "core/vr/MakoControls.hpp"
#include "core/vr/ImageConvert.hpp"
#include "core/vr/InteractLabel.hpp"
#include "core/vr/LayerComposite.hpp"
#include "core/vr/MenuPointer.hpp"
#include "core/vr/MenuSettings.hpp"
#include "core/vr/MetricStereo.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "core/vr/PoseRecording.hpp"
#include "core/vr/Recenter.hpp"
#include "core/vr/SceneCaptureScope.hpp"
#include "core/vr/CineScreen.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include "core/vr/GalaxyMap.hpp"
#include "core/vr/WorldUi.hpp"
#include "core/vr/Spectator.hpp"
#include "core/vr/ScopeCapture.hpp"
#include "core/vr/Ads.hpp"
#include "core/vr/ScopeArt.hpp"
#include "core/vr/Sights.hpp"
#include "core/vr/SquadMenu.hpp"
#include "core/vr/FrameProbe.hpp"
#include "core/vr/TestRequest.hpp"
#include "core/vr/ViewLevel.hpp"
#include "core/vr/ViewSync.hpp"
#include "core/vr/WheelCrop.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/GamepadMapping.hpp"
#include "game/HeadSim.hpp"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <optional>
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

    // A rotator's axes and back, roll included (the cockpit riding in the Mako, research/47).
    using mevr::engine::UnrealRotation;
    for (UnrealRotation const r : {UnrealRotation{0, 0, 0}, UnrealRotation{Units(10), Units(-120), Units(25)}, UnrealRotation{Units(-30), Units(170), Units(-60)},
             UnrealRotation{Units(45), Units(30), Units(170)}}) {
        UnrealRotation const back = mevr::engine::BasisRotation(mevr::engine::ToBasis(r));
        CHECK(Near(back.pitch, r.pitch) && Near(mevr::engine::Unwind(back.yaw, r.yaw), r.yaw) && Near(mevr::engine::Unwind(back.roll, r.roll), r.roll));
    }
    // A part's yaw in a pitched and rolled vehicle: the composed frame turns as the vehicle, then the part.
    {
        using namespace mevr::engine;
        Basis const vehicle = ToBasis(UnrealRotation{Units(15), Units(90), Units(10)});
        UnrealRotation const composed = BasisRotation(Compose(vehicle, ToBasis(UnrealRotation{0, 0, 0})));
        CHECK(Near(composed.pitch, Units(15)) && Near(composed.yaw, Units(90)) && Near(composed.roll, Units(10)));
    }
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

    // Legs (the seated driver, research/47): LE1's names with the limb controls' swivel and IK bones beside them.
    using mevr::ik::FindLegChain;
    std::vector<std::string> const legs{"Root", "Pelvis", "LeftHip", "LeftHip-Swivel", "LeftKnee", "LeftAnkle", "LeftToe", "LFIK",
        "RightHip", "RightKnee", "RightAnkle", "RFIK"};
    std::vector<int> const legParents{-1, 0, 1, 1, 2, 4, 5, 0, 1, 8, 9, 0};
    auto const leftLeg = FindLegChain(legs, legParents, 0);
    CHECK(leftLeg && leftLeg->upper == 2 && leftLeg->fore == 4 && leftLeg->hand == 5);
    auto const rightLeg = FindLegChain(legs, legParents, 1);
    CHECK(rightLeg && rightLeg->upper == 8 && rightLeg->fore == 9 && rightLeg->hand == 10);
    std::vector<std::string> const mixLegs{"Hips", "LeftUpLeg", "LeftLeg", "LeftFoot"};
    CHECK(!FindLegChain(mixLegs, std::vector<int>{-1, 0, 1, 2}, 0));  // "LeftLeg" alone isn't a knee: no guessing
    std::vector<std::string> const bipLegs{"Bip01", "Bip01 L Thigh", "Bip01 L Calf", "Bip01 L Foot"};
    auto const bip = FindLegChain(bipLegs, std::vector<int>{-1, 0, 1, 2}, 0);
    CHECK(bip && bip->upper == 1 && bip->fore == 2 && bip->hand == 3);
    CHECK(!FindLegChain(bipLegs, std::vector<int>{-1, 0, 1, 2}, 1));
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

    // A given pole: the middle joint bends toward it (a seated leg's knee up and forward), the end still on the target.
    pose = before;
    goal = {};
    goal.wrist = {35, 10, 130};
    goal.hasPole = true;
    goal.pole = {0, 0, 1};
    CHECK(PoseArm(pose, parents, chain, 1, body, goal).ok);
    CHECK(NearV(pose[5].translation, goal.wrist, 1e-6));
    Vec3 const toward = Normalize(goal.wrist - shoulder);
    Vec3 const bent = (pose[4].translation - shoulder) - toward * Dot(pose[4].translation - shoulder, toward);
    Vec3 const upAcross = Normalize(goal.pole - toward * Dot(goal.pole, toward));
    CHECK(Dot(Normalize(bent), upAcross) > 0.999);
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

// The body under the player (research/29): an offline replay of the torso on Shepard's layout (God >
// Root > Pelvis > spine and legs; God > Camera) for the cases the user reported ("turning my head and
// walking ... as if my shoulders are in a different location"): the old solve (spine alone) against
// the new one (the whole body turns and steps under the view, core/ik/BodyFollow + TorsoGoal::body).
struct BodyRig {
    std::vector<std::string> names{"God"};
    std::vector<int> parents{-1};
    std::vector<mevr::ik::Transform> pose{{mevr::ik::Quat{}, {0, 0, 0}, 1.0}};
    int camera = -1;
    BodyRig() {
        TorsoRig rig;
        for (std::size_t i = 0; i < rig.names.size(); ++i) {
            names.push_back(rig.names[i]);
            parents.push_back(rig.parents[i] + 1);
            pose.push_back(rig.pose[i]);
        }
        names.push_back("Camera");
        parents.push_back(0);
        pose.push_back({AxisAngleQ(0, 1, 0, 30), {5, 0, 170}, 1.0});
        camera = static_cast<int>(names.size()) - 1;
    }
};

void TestBodyFollow() {
    using namespace mevr::ik;
    BodyRig rig;
    auto const left = FindArmChain(rig.names, rig.parents, 0);
    auto const right = FindArmChain(rig.names, rig.parents, 1);
    CHECK(left && right);
    auto const chain = FindTorsoChain(rig.names, rig.parents, *left, *right);
    CHECK(chain.has_value());
    int const pelvis = 2, hip = 18, head = 9;
    CHECK(chain->spine.front() == 3 && rig.parents[3] == pelvis);
    std::vector<int> const hooks = FindCameraHookBones(rig.names);
    CHECK(InSubtrees(rig.parents, std::vector<int>{pelvis}, hooks).empty());  // a whole-body move never reaches them
    std::vector<Transform> const before = rig.pose;
    BodyFrame const body;
    auto const yawOf = [](Vec3 v) { return std::atan2(v.y, v.x) * 180.0 / std::numbers::pi; };
    auto const facingError = [&](TorsoResult const& r, Vec3 wanted) { return std::abs(std::remainder(yawOf(r.facing) - yawOf(wanted), 360.0)); };
    auto const mid = [&](std::vector<Transform> const& p) { return (p[left->upper].translation + p[right->upper].translation) * 0.5; };

    // 1. The pawn faces away from the player. Explore mode turns Shepard to where the stick runs (with
    // moveFollowsHead: the head's yaw plus the stick's angle), so backing up turns him 180 degrees from the
    // player and strafing 90; standing, he keeps the last way he ran. The old spine twist stops at 60
    // degrees: the shoulders stay 120 / 30 degrees off the player's.
    for (double const away : {90.0, 180.0, -135.0}) {
        Vec3 const wanted = Rotate(AxisAngleQ(0, 0, 1, away - (away == 180.0 ? 0.5 : 0.0)), {1, 0, 0});
        TorsoGoal goal;
        goal.reference = {head, -1};
        goal.head = before[head].translation;
        goal.forward = wanted;
        std::vector<Transform> pose = before;
        TorsoResult const old = PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal);
        CHECK(old.ok && old.limited && facingError(old, wanted) > std::abs(away) - 61.0);
        goal.body = pelvis;
        goal.maxTwistDegrees = 30.0;
        pose = before;
        TorsoResult const now = PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal);
        CHECK(now.ok && facingError(now, wanted) < 2.0);
        CHECK(std::abs(now.twistDegrees) <= 30.0 + 1e-6 && std::abs(now.bodyTurnDegrees) > std::abs(away) - 32.0);
        CHECK(now.firstBone == pelvis);
        // The legs went with the hips (rigidly), Root, God and Camera didn't move at all.
        CHECK(NearT(Relative(pose[hip], pose[pelvis]), Relative(before[hip], before[pelvis]), 1e-6));
        CHECK(NearT(pose[0], before[0], 0.0) && NearT(pose[1], before[1], 0.0) && NearT(pose[rig.camera], before[rig.camera], 0.0));
    }

    // 2. The view isn't where the animation put Shepard's head: Halcyon's camera hangs off the animated
    // `Camera` bone plus per-state offsets, and wandered -21..+25 units forward of the head bone within
    // minutes (LE1MEVR.log, 2026-10-23 00:14-00:18). The hands are mapped from the view, so the old relative
    // goal (Shepard's eyes moved as far as the headset moved: here not at all) left his shoulders 25 units
    // behind the player's. Now the feet go under the view and his head to the head's pivot.
    {
        Vec3 const view = before[head].translation + Vec3{25, 4, 0};
        TorsoGoal goal;
        goal.reference = {head, -1};
        goal.relative = true;
        goal.head = {};  // the headset at the recenter point
        goal.forward = {1, 0, 0};
        std::vector<Transform> pose = before;
        TorsoResult const old = PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal);
        CHECK(old.ok && Length(old.newReference - view) > 24.0);
        goal.relative = false;
        goal.head = view;
        goal.body = pelvis;
        goal.bodyMoves = true;
        goal.bodyAt = before[pelvis].translation + Vec3{25, 4, 0};  // the feet under the head (at rest)
        goal.maxBodyShift = 75.0;
        goal.maxTwistDegrees = 30.0;
        pose = before;
        TorsoResult const now = PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal);
        CHECK(now.ok && !now.shiftLimited && Length(now.newReference - view) < 1.0);
        CHECK(NearD(Length(now.bodyShift), std::hypot(25.0, 4.0), 1e-9) && now.bendDegrees < 2.0);
        CHECK(Length(mid(pose) - (mid(before) + Vec3{25, 4, 0})) < 1.5);
        CHECK(NearT(pose[rig.camera], before[rig.camera], 0.0) && NearT(pose[0], before[0], 0.0) && NearT(pose[1], before[1], 0.0));
        // A slide past its limit stops there (logged as capped), along the way.
        goal.bodyAt = before[pelvis].translation + Vec3{100, 0, 30};
        goal.maxBodyShift = 50.0;
        pose = before;
        TorsoResult const capped = PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal);
        CHECK(capped.ok && capped.shiftLimited && NearD(Length(capped.bodyShift), 50.0, 1e-9) && capped.bodyShift.z == 0.0);
        CHECK(NearD(Length(capped.wantedBodyShift), 100.0, 1e-9));
        // The body bone must hang below the skeleton's root and above the spine.
        goal.body = 0;
        CHECK(!PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal).ok);
        goal.body = hip;
        CHECK(!PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal).ok);
    }

    // 3. A physical step: the head 0.6 m forward over a second (100 units per meter, life-size). The old
    // solve leans the spine (35 degrees at most) about LowerBack: Shepard bends over and his head stays
    // short of the view. Now the feet follow the head (core/ik/BodyFollow): the body steps, the spine
    // leans the last leanRadius, and the lean settles once the head stops.
    {
        double const unitsPerMeter = 100.0;
        BodyFollow follow;
        FollowState state;
        int const steps = 90;
        for (int i = 0; i <= steps; ++i) {
            FollowInput in;
            in.pivot = {0.0, 1.6, -0.6 * i / steps};  // -Z is forward in tracking space
            in.deltaSeconds = 1.0 / 90.0;
            in.haveRecenters = true;
            state = follow.Update(in);
            CHECK(state.anchored == (i == 0 ? FollowAnchor::Start : FollowAnchor::None));
        }
        CHECK(NearD(std::hypot(state.lean.x, state.lean.z), 0.20, 0.02));  // leaning at the radius while walking
        CHECK(NearD(state.feet.z, -0.4, 0.03));
        // Tracking -Z is the body's +X here: the view (Shepard's head at rest plus the step) and the feet.
        Vec3 const view = before[head].translation + Vec3{0.6 * unitsPerMeter, 0, 0};
        TorsoGoal goal;
        goal.reference = {head, -1};
        goal.relative = true;
        goal.head = {0.6 * unitsPerMeter, 0, 0};
        goal.forward = {1, 0, 0};
        std::vector<Transform> pose = before;
        TorsoResult const old = PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal);
        CHECK(old.ok && old.limited && Length(old.newReference - view) > 20.0);
        goal.relative = false;
        goal.head = view;
        goal.body = pelvis;
        goal.bodyMoves = true;
        goal.bodyAt = before[pelvis].translation + Vec3{-state.feet.z * unitsPerMeter, state.feet.x * unitsPerMeter, 0};
        goal.maxBodyShift = 0.75 * unitsPerMeter;
        goal.maxTwistDegrees = 30.0;
        pose = before;
        TorsoResult const now = PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal);
        // Horizontally the head lands within a few units (the old: 21 behind); the rest is the lean's drop and
        // the spine's curl (the solve aims the shoulders; the head sits above them).
        auto const flat = [](Vec3 v) { return std::hypot(v.x, v.y); };
        CHECK(flat(old.newReference - view) > 20.0);
        CHECK(now.ok && !now.limited && flat(now.newReference - view) < 6.0 && now.bendDegrees < 20.0);
        // Standing still for three seconds: the leftover lean settles under the head.
        for (int i = 0; i < 270; ++i) {
            FollowInput in;
            in.pivot = {0.0, 1.6, -0.6};
            in.deltaSeconds = 1.0 / 90.0;
            in.haveRecenters = true;
            state = follow.Update(in);
        }
        CHECK(std::hypot(state.lean.x, state.lean.z) < 0.09);
    }

    // 4. Turning the head in place swings the eyes around the neck (about 13 cm for 90 degrees): the old
    // relative goal moved Shepard's eyes, so his spine leaned sideways. The head's pivot doesn't move.
    {
        Vec3 const pivot{0.1, 1.6, -0.2};
        NeckModel const neck;
        for (double const turn : {0.0, 90.0, -120.0}) {
            Quat const look = Normalize(AxisAngleQ(0, 1, 0, turn) * AxisAngleQ(1, 0, 0, -20));  // turned, and looking down
            Vec3 const eyes = pivot + Rotate(look, Vec3{0.0, neck.down, -neck.back});
            CHECK(NearV(HeadPivot(eyes, look, neck), pivot, 1e-9));
            if (turn == 90.0) {
                Vec3 const eyesAhead = pivot + Rotate(AxisAngleQ(1, 0, 0, -20), Vec3{0.0, neck.down, -neck.back});
                CHECK(Length(eyes - eyesAhead) > 0.11);  // what the old goal followed
            }
        }
    }

    // 5. A recenter re-origins tracking space at the head: the head's yaw jumps from 90 degrees to 0. The old
    // body yaw (never reset while the torso ran) was left at the deadzone's edge, 34 degrees off, and eased
    // back over seconds; the new one re-anchors at once. A jump we aren't told about (no counter) too.
    {
        double const quarter = std::numbers::pi / 2.0;
        BodyYaw old;
        old.Reset(quarter);
        CHECK(NearD(old.Update(0.0, 1.0 / 90.0), 0.6, 1e-3));
        BodyFollow follow;
        FollowInput in;
        in.pivot = {0.3, 1.6, 0.2};
        in.headYaw = in.facingYaw = quarter;
        in.haveRecenters = true;
        in.recenters = 3;
        in.deltaSeconds = 1.0 / 90.0;
        CHECK(follow.Update(in).anchored == FollowAnchor::Start);
        CHECK(NearD(follow.Update(in).yaw, quarter, 1e-9));
        in.recenters = 4;
        in.headYaw = in.facingYaw = 0.0;
        in.pivot = {0.0, 1.6, 0.09};
        FollowState state = follow.Update(in);
        CHECK(state.anchored == FollowAnchor::Recenter && NearD(state.yaw, 0.0, 1e-12) && NearD(state.feet.x, 0.0, 1e-12));
        // No counter: the head's pivot jumping a meter re-anchors.
        in.haveRecenters = false;
        in.pivot = {1.2, 1.6, 0.09};
        state = follow.Update(in);
        CHECK(state.anchored == FollowAnchor::Jump && NearD(state.feet.x, 1.2, 1e-12));
        // Reset (the torso wasn't driven: a cutscene, a menu): the next update starts over.
        follow.Reset();
        CHECK(follow.Update(in).anchored == FollowAnchor::Start);
        // A small move follows without re-anchoring: within the radius the feet stay (nearly) put.
        in.pivot = {1.3, 1.6, 0.09};
        state = follow.Update(in);
        CHECK(state.anchored == FollowAnchor::None && NearD(state.feet.x, 1.2, 0.01));
    }
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
    // A hand closed on a handle, as on a pistol grip: its thumb side (Z) onto the handle's axis or the opposite, whichever is
    // nearer, turned the least way; the fingers (X) across it.
    Quat const level{};  // the fingers along +X, the thumb side up (+Z)
    Vec3 const leaning = Normalize(Vec3{0.4, 0.0, 1.0});  // a stick leaning forward
    Quat const onStick = AlignGripToAxis(level, leaning);
    CHECK(same(Rotate(onStick, {0.0, 0.0, 1.0}), leaning));
    CHECK(same(Rotate(onStick, {0.0, 1.0, 0.0}), {0.0, 1.0, 0.0}));  // still as far round: about the hand's own side axis
    CHECK(std::abs(Dot(Rotate(onStick, {1.0, 0.0, 0.0}), leaning)) < 1e-9);  // the fingers across the stick
    CHECK(same(Rotate(AlignGripToAxis(level, {0.1, 0.0, -1.0}), {0.0, 0.0, 1.0}), Normalize(Vec3{-0.1, 0.0, 1.0})));  // the nearer sign
    Quat const onBar = AlignGripToAxis(grip, {0.0, 1.0, 0.0});  // a bar across
    CHECK(same(Rotate(onBar, {0.0, 0.0, 1.0}), {0.0, Rotate(grip, {0.0, 0.0, 1.0}).y < 0.0 ? -1.0 : 1.0, 0.0}));
    CHECK(AngleBetween(AlignGripToAxis(onStick, leaning), onStick) < 1e-6);  // already on it: unchanged
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

// The game pointer (research/42): a controller's ray on the flat screen as the game's mouse.
void TestGamePointer() {
    using namespace mevr::vr::game_pointer;
    XrPosef const identity{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    // The flat screen as Halcyon places it head-locked for a level head at 1.6 m: 2.35 m wide, 16:9, 1.4 m ahead.
    XrPosef head{};
    head.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    head.position = {0.0f, 1.6f, 0.0f};
    XrPosef local{};
    local.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    local.position = {0.0f, 0.0f, -1.4f};
    float const w = 2.35f, h = 2.35f * 9.0f / 16.0f;
    Quad const quad = mevr::vr::menu_pointer::InTracking(head, local, w, h);

    // UV to a pixel: the top-left corner is (0, 0), the bottom-right the last pixel, the middle the middle.
    int x = -1, y = -1;
    ToPixel(0.0f, 0.0f, 2560, 1440, x, y);
    CHECK(x == 0 && y == 0);
    ToPixel(1.0f, 1.0f, 2560, 1440, x, y);
    CHECK(x == 2559 && y == 1439);
    ToPixel(0.5f, 0.5f, 2560, 1440, x, y);
    CHECK(x == 1280 && y == 720);
    ToPixel(-0.5f, 2.0f, 100, 50, x, y);  // out of range input is clamped
    CHECK(x == 0 && y == 49);
    ToPixel(0.5f, 0.5f, 0, 0, x, y);      // a degenerate area never divides by zero
    CHECK(x == 0 && y == 0);

    // The point on the quad: the middle is the centre; (0, 0) the top-left corner, in the tracking space.
    XrVector3f const centre = PointOnQuad(quad, 0.5f, 0.5f);
    CHECK(NearXr(centre, {0.0f, 1.6f, -1.4f}));
    XrVector3f const corner = PointOnQuad(quad, 0.0f, 0.0f);
    CHECK(NearXr(corner, {-w / 2.0f, 1.6f + h / 2.0f, -1.4f}));
    // A dot of 0.6 degrees at 1.4 m is about 1.5 cm.
    CHECK(NearD(DotSize({0.0f, 1.6f, 0.0f}, centre, 0.6f * 3.14159265f / 180.0f), 0.01466, 1e-4));

    // The pointer's state machine. The right hand beside the head, aimed at the quad's middle.
    Settings settings;
    settings.smoothing = 1.0f;  // no smoothing: exact pixels
    Pointer pointer(settings);
    Input in;
    in.active = true;
    in.width = 2560;
    in.height = 1440;
    in.dt = 1.0f / 90.0f;
    in.hands[1].aimValid = true;
    in.hands[1].aim.orientation = {0.0f, 0.0f, 0.0f, 1.0f};  // -Z: straight at the quad
    in.hands[1].aim.position = {0.0f, 1.6f, -0.2f};
    Output out = pointer.Step(quad, in);
    CHECK(out.hit && out.onQuad && out.hand == 1 && out.moved && !out.pressed && !out.pressEdge);
    CHECK(out.x == 1280 && out.y == 720 && NearD(out.distance, 1.2));
    CHECK(NearXr(out.point, centre));
    // The same aim again: within the dead band, no move.
    out = pointer.Step(quad, in);
    CHECK(out.hit && !out.moved && out.x == 1280 && out.y == 720);
    // Aimed a little to the right and down (more than a pixel): a move, and the pixel to the right and below.
    in.hands[1].aim.position = {0.02f, 1.59f, -0.2f};
    out = pointer.Step(quad, in);
    CHECK(out.hit && out.moved && out.x > 1280 && out.y > 720);
    int const heldX = out.x, heldY = out.y;
    // The trigger: below the on-threshold nothing, at it a press (one edge), held no edge, down to the off-threshold
    // still held, below it released (one edge).
    in.hands[1].trigger = 0.5f;
    out = pointer.Step(quad, in);
    CHECK(!out.pressed && !out.pressEdge);
    in.hands[1].trigger = 0.6f;
    out = pointer.Step(quad, in);
    CHECK(out.pressed && out.pressEdge && out.x == heldX && out.y == heldY);
    out = pointer.Step(quad, in);
    CHECK(out.pressed && !out.pressEdge);
    in.hands[1].trigger = 0.45f;
    out = pointer.Step(quad, in);
    CHECK(out.pressed && !out.pressEdge);
    in.hands[1].trigger = 0.3f;
    out = pointer.Step(quad, in);
    CHECK(!out.pressed && out.pressEdge);
    // A press that slides off the quad is held until the trigger lets go, and the button up comes at the last point.
    in.hands[1].trigger = 1.0f;
    out = pointer.Step(quad, in);
    CHECK(out.pressed && out.pressEdge);
    in.hands[1].aim.orientation = AxisAngle(0, 1, 0, 80);  // far off to the left
    out = pointer.Step(quad, in);
    CHECK(out.hit && !out.onQuad && out.pressed && !out.pressEdge && !out.moved);
    in.hands[1].trigger = 0.0f;
    out = pointer.Step(quad, in);
    CHECK(!out.hit && !out.pressed && out.pressEdge && out.left);
    out = pointer.Step(quad, in);
    CHECK(!out.hit && !out.pressEdge && !out.left);
    // A press can't start off the quad.
    in.hands[1].trigger = 1.0f;
    out = pointer.Step(quad, in);
    CHECK(!out.hit && !out.pressed && !out.pressEdge);
    in.hands[1].trigger = 0.0f;
    pointer.Step(quad, in);

    // The other hand takes over when it is on the quad and the pointing one isn't; the pointing hand keeps it while on.
    in.hands[0].aimValid = true;
    in.hands[0].aim.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    in.hands[0].aim.position = {-0.3f, 1.5f, -0.2f};
    out = pointer.Step(quad, in);
    CHECK(out.hit && out.hand == 0 && out.moved && out.x < 1280 && out.y > 720);
    in.hands[1].aim.orientation = {0.0f, 0.0f, 0.0f, 1.0f};  // the right hand is back on the quad too
    out = pointer.Step(quad, in);
    CHECK(out.hit && out.hand == 0);
    // ... unless it pulls its trigger on the quad while the left doesn't.
    in.hands[1].trigger = 1.0f;
    out = pointer.Step(quad, in);
    CHECK(out.hit && out.hand == 1 && out.pressed && out.pressEdge && out.x > 1280);
    // Through the press the hand stays, even with the left trigger pulled.
    in.hands[0].trigger = 1.0f;
    out = pointer.Step(quad, in);
    CHECK(out.hand == 1 && out.pressed);
    in.hands[0].trigger = 0.0f;
    in.hands[1].trigger = 0.0f;
    pointer.Step(quad, in);

    // The wheel: the right stick forward accumulates notches at wheelPerSecond (8 here, at 16 steps a second: an
    // exact half notch a step); six steps at full deflection give 3, in whole notches as they come; inside the dead
    // zone nothing, and letting go drops the fraction.
    in.hands[0].aimValid = false;
    Settings wheelSettings = settings;
    wheelSettings.wheelPerSecond = 8.0f;
    pointer = Pointer(wheelSettings);
    in.dt = 1.0f / 16.0f;
    in.rightStickY = 1.0f;
    int notches = 0;
    for (int i = 0; i < 6; ++i) {
        notches += pointer.Step(quad, in).wheelNotches;
    }
    CHECK(notches == 3);
    in.rightStickY = -1.0f;
    notches = 0;
    for (int i = 0; i < 2; ++i) {
        notches += pointer.Step(quad, in).wheelNotches;
    }
    CHECK(notches == -1);
    in.rightStickY = 0.15f;
    notches = 0;
    for (int i = 0; i < 90; ++i) {
        notches += pointer.Step(quad, in).wheelNotches;
    }
    CHECK(notches == 0);

    // No controllers, or no quad: everything released; an open press reports its button up once.
    in.rightStickY = 0.0f;
    in.hands[1].trigger = 1.0f;
    out = pointer.Step(quad, in);
    CHECK(out.pressed && out.pressEdge);
    in.active = false;
    out = pointer.Step(quad, in);
    CHECK(!out.hit && !out.pressed && out.pressEdge && out.left);
    in.active = true;
    out = pointer.Step(quad, in);  // the trigger is still pulled: a fresh latch closes it again, a new press
    CHECK(out.pressed && out.pressEdge);
    Quad none;
    out = pointer.Step(none, in);
    CHECK(!out.hit && out.pressEdge && !out.pressed);
    out = pointer.Release();
    CHECK(!out.pressEdge);

    // World-locked: the quad fixed in the room where it appeared (yawed 90 degrees left of the tracking space's
    // forward, 1.4 m along -X), the hand pointing at it from the head's place: the middle.
    XrPosef placed{};
    placed.orientation = AxisAngle(0, 1, 0, 90);
    placed.position = {-1.4f, 1.6f, 0.0f};
    Quad const world = mevr::vr::menu_pointer::InTracking(identity, placed, w, h);
    Pointer pointer2(settings);
    in.hands[1].trigger = 0.0f;
    in.hands[1].aim.position = {0.0f, 1.6f, 0.0f};
    in.hands[1].aim.orientation = AxisAngle(0, 1, 0, 90);  // -Z turned to -X
    out = pointer2.Step(world, in);
    CHECK(out.hit && out.x == 1280 && out.y == 720 && NearD(out.distance, 1.4));
    CHECK(NearXr(out.point, {-1.4f, 1.6f, 0.0f}));
    // Aimed toward the quad's right edge (the player's right when facing -X is -Z; a smaller yaw turns -Z that
    // way): u past the middle.
    in.hands[1].aim.orientation = AxisAngle(0, 1, 0, 70);
    out = pointer2.Step(world, in);
    CHECK(out.hit && out.x > 1280 && out.u > 0.5f);

    // Touch (research/53 section 6.2): a panel 0.4 x 0.3 m half a metre ahead of the eyes, facing them; the fingertip presses it.
    XrPosef panelPose{};
    panelPose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    panelPose.position = {0.0f, 1.6f, -0.5f};
    Quad const panel{panelPose, 0.4f, 0.3f};
    Input touch;
    touch.active = true;
    touch.width = 1000;
    touch.height = 750;
    touch.dt = 1.0f / 90.0f;
    touch.hands[1].tipValid = true;
    Pointer finger;
    auto tipAt = [&](float x, float y, float depth) {  // depth: in front of the panel, meters
        touch.hands[1].tip = {x, 1.6f + y, -0.5f + depth};
        return finger.Step(panel, touch);
    };
    out = tipAt(0.05f, 0.0f, 0.10f);  // out of reach
    CHECK(!out.hit && !out.touch);
    out = tipAt(0.05f, 0.0f, 0.03f);  // hovering: the pointer under the fingertip, nothing pressed
    CHECK(out.hit && out.touch && !out.pressed && NearD(out.u, 0.625) && NearD(out.v, 0.5) && NearD(out.touchDistance, 0.03));
    out = tipAt(0.05f, 0.0f, 0.002f);  // at the surface: pressed
    CHECK(out.pressed && out.pressEdge && out.touch);
    out = tipAt(0.05f, 0.0f, -0.01f);  // pushed through a little: still pressed, no new edge
    CHECK(out.pressed && !out.pressEdge);
    out = tipAt(0.05f, 0.0f, 0.01f);  // back out but within the release distance: held
    CHECK(out.pressed);
    out = tipAt(0.05f, 0.0f, 0.03f);  // pulled back: let go
    CHECK(!out.pressed && out.pressEdge && out.hit);
    // From behind: a hand coming through the panel never presses.
    Pointer behind;
    touch.hands[1].tip = {0.0f, 1.6f, -0.52f};
    out = behind.Step(panel, touch);
    CHECK(out.touch && !out.pressed);
    touch.hands[1].tip = {0.0f, 1.6f, -0.501f};
    out = behind.Step(panel, touch);
    CHECK(!out.pressed);
    // Only the touchable part (the visible crop) takes a touch; the margin around it still does.
    Pointer cropped;
    touch.touchU0 = 0.25f;
    touch.touchU1 = 0.75f;
    touch.hands[1].tip = {0.16f, 1.6f, -0.47f};  // u 0.9
    out = cropped.Step(panel, touch);
    CHECK(!out.touch && !out.hit);
    touch.hands[1].tip = {0.105f, 1.6f, -0.47f};  // u 0.7625: inside
    out = cropped.Step(panel, touch);
    CHECK(out.touch);
    touch.touchU0 = 0.0f;
    touch.touchU1 = 1.0f;
    // A fingertip near the panel takes over from the other hand's ray, trigger and all.
    Pointer both;
    touch.hands[0].aimValid = true;
    touch.hands[0].aim.position = {0.0f, 1.6f, 0.0f};
    touch.hands[0].aim.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    touch.hands[0].trigger = 1.0f;
    touch.hands[1].tip = {-0.1f, 1.6f, -0.48f};
    out = both.Step(panel, touch);
    CHECK(out.touch && out.hand == 1 && !out.pressed && NearD(out.u, 0.25));
    // With touch off the ray works as before.
    Settings noTouch;
    noTouch.touch = false;
    Pointer rays(noTouch);
    out = rays.Step(panel, touch);
    CHECK(!out.touch && out.hand == 0 && out.pressed);
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
        hand.triggerTouchKnown = hand.thumbTouchKnown = h == 1;  // the left one sensorless (an old recording's controllers)
        hand.triggerTouch = h == 1;
        hand.thumbTouch = false;
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
            CHECK(r.hands[h].triggerTouch == a.hands[h].triggerTouch && r.hands[h].thumbTouch == a.hands[h].thumbTouch &&
                r.hands[h].triggerTouchKnown == a.hands[h].triggerTouchKnown && r.hands[h].thumbTouchKnown == a.hands[h].thumbTouchKnown);
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

// The colour correction (core/vr/Colour, research/27): the CPU reference the shader mirrors (tools/colour-preview
// checks the shader against it on a GPU), the presets, the headset detection and the settings file.
void TestTonemap() {
    namespace cv = mevr::vr::colour;
    auto approx = [](float a, float b, float tolerance = 1e-4f) { return std::abs(a - b) <= tolerance; };

    // Names and the file's spellings.
    CHECK(cv::ParseTonemap("aces") == cv::Tonemap::Aces && cv::ParseTonemap(" Hable ") == cv::Tonemap::Hable);
    CHECK(cv::ParseTonemap("REINHARD") == cv::Tonemap::Reinhard && cv::ParseTonemap("off") == cv::Tonemap::Clip);
    CHECK(cv::ParseTonemap("clip") == cv::Tonemap::Clip && !cv::ParseTonemap("filmicx").has_value());
    CHECK(std::string(cv::Name(cv::Tonemap::Hable)) == "hable");
    CHECK(!cv::TonemapActive(cv::Hdr{}) && cv::TonemapActive(cv::Hdr{cv::Tonemap::Aces, 0.0f, 4.0f}));
    cv::Hdr wild{cv::Tonemap::Aces, 9.0f, 0.1f};
    CHECK(approx(cv::ClampedHdr(wild).exposure, 3.0f) && approx(cv::ClampedHdr(wild).white, 1.0f));

    // The constants carry the exposure as a multiplier and the mode in seed.y; the old overload is the clip.
    cv::Constants const plain = cv::MakeConstants(cv::Settings{}, 0.0f, true, 0);
    CHECK(plain.seed[1] == 0u && approx(plain.misc[2], 1.0f));
    cv::Constants const aces = cv::MakeConstants(cv::Settings{}, 0.0f, true, 0, cv::Hdr{cv::Tonemap::Aces, 1.0f, 4.0f});
    CHECK(aces.seed[1] == 1u && approx(aces.misc[2], 2.0f) && approx(aces.misc[3], 4.0f));

    // The clip: unchanged at or below 1, divided by the largest channel above it (hue kept).
    CHECK(approx(cv::Tone(plain, {0.25f, 0.5f, 1.0f})[1], 0.5f));
    cv::Rgb const clipped = cv::Tone(plain, {4.0f, 2.0f, 1.0f});
    CHECK(approx(clipped[0], 1.0f) && approx(clipped[1], 0.5f) && approx(clipped[2], 0.25f));
    // An encoded (8-bit) source gets the clip whatever the mode: its values never pass 1.
    cv::Constants const acesEncoded = cv::MakeConstants(cv::Settings{}, 0.0f, false, 0, cv::Hdr{cv::Tonemap::Aces, 0.0f, 4.0f});
    CHECK(approx(cv::Tone(acesEncoded, {0.5f, 0.5f, 0.5f})[0], 0.5f));

    // ACES: black stays black, monotonic, paper white lands near 0.80, highlights roll off below 1.
    cv::Constants const aces0 = cv::MakeConstants(cv::Settings{}, 0.0f, true, 0, cv::Hdr{cv::Tonemap::Aces, 0.0f, 4.0f});
    CHECK(approx(cv::Tone(aces0, {0.0f, 0.0f, 0.0f})[0], 0.0f));
    CHECK(approx(cv::Tone(aces0, {1.0f, 1.0f, 1.0f})[0], 0.8038f, 0.002f));
    float last = 0.0f;
    bool monotonic = true;
    for (float x = 0.0f; x <= 16.0f; x += 0.125f) {
        float const y = cv::Tone(aces0, {x, x, x})[0];
        monotonic = monotonic && y >= last - 1e-6f && y <= 1.0f;
        last = y;
    }
    CHECK(monotonic && cv::Tone(aces0, {8.0f, 8.0f, 8.0f})[0] > 0.98f);
    // Exposure: +1 stop doubles the input before the curve.
    CHECK(approx(cv::Tone(aces, {0.5f, 0.5f, 0.5f})[0], cv::Tone(aces0, {1.0f, 1.0f, 1.0f})[0]));

    // Hable and Reinhard: `white` reaches 1.0 exactly, below it the curve is below the identity's clip.
    cv::Constants const hable = cv::MakeConstants(cv::Settings{}, 0.0f, true, 0, cv::Hdr{cv::Tonemap::Hable, 0.0f, 4.0f});
    cv::Constants const reinhard = cv::MakeConstants(cv::Settings{}, 0.0f, true, 0, cv::Hdr{cv::Tonemap::Reinhard, 0.0f, 4.0f});
    CHECK(approx(cv::Tone(hable, {4.0f, 4.0f, 4.0f})[0], 1.0f, 1e-5f));
    CHECK(approx(cv::Tone(reinhard, {4.0f, 4.0f, 4.0f})[0], 1.0f, 1e-5f));
    CHECK(cv::Tone(hable, {1.0f, 1.0f, 1.0f})[0] < 1.0f && cv::Tone(hable, {1.0f, 1.0f, 1.0f})[0] > 0.5f);
    CHECK(cv::Tone(reinhard, {1.0f, 1.0f, 1.0f})[0] < 1.0f && cv::Tone(reinhard, {1.0f, 1.0f, 1.0f})[0] > 0.5f);
    CHECK(approx(cv::Tone(hable, {0.0f, 0.0f, 0.0f})[0], 0.0f, 1e-5f) && approx(cv::Tone(reinhard, {0.0f, 0.0f, 0.0f})[0], 0.0f));

    // Hue kept (research/27 section 7d): the curve on the largest channel and the pixel scaled by the ratio, so the
    // channels' ratios survive; hue 0 is the per-channel result; grey is the same either way.
    cv::Hdr const defaults{};
    CHECK(approx(defaults.hue, 1.0f) && approx(defaults.desat, 0.15f) && approx(defaults.sat, 0.7f));
    cv::Hdr const wildHue{cv::Tonemap::Aces, 0.0f, 4.0f, 2.0f, -1.0f, 3.0f};
    CHECK(approx(cv::ClampedHdr(wildHue).hue, 1.0f) && approx(cv::ClampedHdr(wildHue).desat, 0.0f) && approx(cv::ClampedHdr(wildHue).sat, 1.0f));
    cv::Constants const kept = cv::MakeConstants(cv::Settings{}, 0.0f, true, 0, cv::Hdr{cv::Tonemap::Aces, 0.0f, 4.0f, 1.0f, 0.0f, 1.0f});
    cv::Constants const perChannel = cv::MakeConstants(cv::Settings{}, 0.0f, true, 0, cv::Hdr{cv::Tonemap::Aces, 0.0f, 4.0f, 0.0f, 0.0f, 1.0f});
    CHECK(approx(kept.hdr[0], 1.0f) && approx(kept.hdr[1], 0.0f) && approx(kept.hdr[2], 1.0f) && approx(perChannel.hdr[0], 0.0f));
    cv::Rgb const k1 = cv::Tone(kept, {2.0f, 1.0f, 0.5f});
    CHECK(approx(k1[0], cv::Tone(aces0, {2.0f, 2.0f, 2.0f})[0]) && approx(k1[1], 0.5f * k1[0]) && approx(k1[2], 0.25f * k1[0]));
    cv::Rgb const p1 = cv::Tone(perChannel, {2.0f, 1.0f, 0.5f});
    CHECK(approx(p1[0], cv::Tone(aces0, {2.0f, 2.0f, 2.0f})[0]) && approx(p1[1], cv::Tone(aces0, {1.0f, 1.0f, 1.0f})[0]));
    CHECK(approx(p1[2], cv::Tone(aces0, {0.5f, 0.5f, 0.5f})[0]) && !approx(p1[2] / p1[0], 0.25f, 0.05f));
    CHECK(approx(cv::Tone(kept, {3.0f, 3.0f, 3.0f})[0], cv::Tone(perChannel, {3.0f, 3.0f, 3.0f})[0]));
    CHECK(approx(cv::Tone(kept, {0.0f, 0.0f, 0.0f})[2], 0.0f));
    // The path to white: a pixel far above its curve moves toward the curve's grey (its largest channel stays at
    // the curve); a pixel below the curve's value is untouched.
    cv::Constants const toWhite = cv::MakeConstants(cv::Settings{}, 0.0f, true, 0, cv::Hdr{cv::Tonemap::Aces, 0.0f, 4.0f, 1.0f, 0.15f, 1.0f});
    cv::Rgb const w1 = cv::Tone(toWhite, {8.0f, 4.0f, 2.0f});
    CHECK(approx(w1[0], cv::Tone(aces0, {8.0f, 8.0f, 8.0f})[0]) && w1[2] / w1[0] > 0.55f && w1[2] / w1[0] < 0.75f);
    cv::Rgb const w2 = cv::Tone(toWhite, {0.5f, 0.25f, 0.125f});
    CHECK(approx(w2[1], 0.5f * w2[0]) && approx(w2[2], 0.25f * w2[0]));
    // The chroma before the curve: half way toward Rec. 709 luminance.
    cv::Constants const chroma = cv::MakeConstants(cv::Settings{}, 0.0f, true, 0, cv::Hdr{cv::Tonemap::Aces, 0.0f, 4.0f, 1.0f, 0.0f, 0.5f});
    cv::Rgb const s1 = cv::Tone(chroma, {2.0f, 1.0f, 0.5f});
    CHECK(approx(s1[1] / s1[0], 0.6852f, 0.002f) && approx(s1[2] / s1[0], 0.5278f, 0.002f));
    cv::Resolved const r2 = cv::Resolve(cv::ParseSection("toneHue=0.5\r\ntoneDesat=0.3\r\ntoneSat=0.8\r\n"), cv::Section{}, cv::Detection{});
    CHECK(approx(r2.hdr.hue, 0.5f) && approx(r2.hdr.desat, 0.3f) && approx(r2.hdr.sat, 0.8f));

    // The file: tonemap, exposure and white from [Colour], the headset's section first.
    cv::Section common = cv::ParseSection("tonemap=aces\r\nexposure=0.5\r\nwhite=6\r\n");
    cv::Section headset = cv::ParseSection("tonemap=hable\r\n");
    cv::Resolved const r = cv::Resolve(common, headset, cv::Detection{});
    CHECK(r.hdr.tonemap == cv::Tonemap::Hable && approx(r.hdr.exposure, 0.5f) && approx(r.hdr.white, 6.0f));
    CHECK(cv::Resolve(cv::Section{}, cv::Section{}, cv::Detection{}).hdr.tonemap == cv::Tonemap::Clip);

    // The shader text names the hook and the modes.
    std::string const prelude = cv::ShaderPrelude();
    CHECK(prelude.find("#define MEVR_TONE(c) MevrTone(c)") != std::string::npos);
    CHECK(prelude.find("MevrAces") != std::string::npos && prelude.find("MevrHableCurve") != std::string::npos);
}

void TestColour() {
    namespace cv = mevr::vr::colour;
    auto grey = [](cv::Constants const& k, float v) { return cv::Grade(k, {v, v, v})[0]; };
    auto approx = [](float a, float b, float tolerance = 1e-5f) { return std::abs(a - b) <= tolerance; };

    // Neutral is the identity, and is detected as neutral (so the raw path runs).
    CHECK(cv::IsNeutral(cv::Settings{}));
    CHECK(cv::IsNeutral(cv::Preset(cv::Profile::Neutral)) && cv::IsNeutral(cv::Preset(cv::Profile::Auto)));
    CHECK(!cv::IsNeutral(cv::Preset(cv::Profile::Quest)) && !cv::IsNeutral(cv::Preset(cv::Profile::Vivid)));
    cv::Settings almost;
    almost.contrast = 1.0003f;  // within half a slider step
    CHECK(cv::IsNeutral(almost));
    almost.contrast = 1.01f;
    CHECK(!cv::IsNeutral(almost));
    {
        cv::Constants const k = cv::MakeConstants(cv::Settings{}, 0.0f, false, 0);
        bool identity = true;
        for (int i = 0; i <= 255; ++i) {
            float const v = static_cast<float>(i) / 255.0f;
            cv::Rgb const out = cv::Grade(k, {v, 1.0f - v, 0.5f * v});
            identity = identity && approx(out[0], v) && approx(out[1], 1.0f - v) && approx(out[2], 0.5f * v);
            cv::Rgb const stored = cv::Correct(k, {v, v, v}, 7, 9, false);
            identity = identity && cv::ToUnorm8(stored[0]) == i;
        }
        CHECK(identity);
    }

    // Each operation: its direction, and what it keeps.
    {
        cv::Settings s;
        s.contrast = 1.2f;
        cv::Constants const k = cv::MakeConstants(s, 0.0f, false, 0);
        CHECK(approx(grey(k, 0.0f), 0.0f) && approx(grey(k, 1.0f), 1.0f) && approx(grey(k, 0.5f), 0.5f));
        CHECK(grey(k, 0.25f) < 0.25f && grey(k, 0.75f) > 0.75f);
        float const slope = (grey(k, 0.501f) - grey(k, 0.499f)) / 0.002f;  // the slope at mid-grey is the setting
        CHECK(approx(slope, 1.2f, 0.01f));
        s.contrast = 0.8f;
        cv::Constants const flat = cv::MakeConstants(s, 0.0f, false, 0);
        CHECK(grey(flat, 0.25f) > 0.25f && grey(flat, 0.75f) < 0.75f && approx(grey(flat, 0.0f), 0.0f));
    }
    {
        cv::Settings s;
        s.gamma = 0.8f;  // Virtual Desktop's direction: below 1 darker
        cv::Constants const k = cv::MakeConstants(s, 0.0f, false, 0);
        CHECK(approx(grey(k, 0.5f), std::pow(0.5f, 1.25f)) && approx(grey(k, 0.0f), 0.0f) && approx(grey(k, 1.0f), 1.0f));
        s.gamma = 1.25f;
        CHECK(grey(cv::MakeConstants(s, 0.0f, false, 0), 0.5f) > 0.5f);
    }
    {
        cv::Settings s;
        s.black = -0.05f;  // deeper: 0.05 and below become black, white stays
        cv::Constants const k = cv::MakeConstants(s, 0.0f, false, 0);
        CHECK(approx(grey(k, 0.05f), 0.0f) && approx(grey(k, 0.03f), 0.0f) && approx(grey(k, 1.0f), 1.0f));
        CHECK(approx(grey(k, 0.525f), 0.5f));
        s.black = 0.05f;  // lifted
        cv::Constants const lifted = cv::MakeConstants(s, 0.0f, false, 0);
        CHECK(approx(grey(lifted, 0.0f), 0.05f / 1.05f) && approx(grey(lifted, 1.0f), 1.0f));
    }
    {
        cv::Settings s;
        s.brightness = 1.2f;
        s.gain = {1.0f, 0.5f, 1.0f};
        cv::Constants const k = cv::MakeConstants(s, 0.0f, false, 0);
        cv::Rgb const out = cv::Grade(k, {0.5f, 0.5f, 0.9f});
        CHECK(approx(out[0], 0.6f) && approx(out[1], 0.3f) && approx(out[2], 1.0f));  // the blue clips
    }
    {
        cv::Settings s;
        s.saturation = 0.0f;
        cv::Constants const k = cv::MakeConstants(s, 0.0f, false, 0);
        cv::Rgb const out = cv::Grade(k, {1.0f, 0.0f, 0.0f});
        CHECK(approx(out[0], 0.2126f) && approx(out[1], 0.2126f) && approx(out[2], 0.2126f));
        s.saturation = 1.5f;
        cv::Rgb const more = cv::Grade(cv::MakeConstants(s, 0.0f, false, 0), {0.6f, 0.4f, 0.4f});
        float const y = 0.2126f * 0.6f + 0.7152f * 0.4f + 0.0722f * 0.4f;
        CHECK(approx(more[0], y + 1.5f * (0.6f - y)) && approx(more[1], y + 1.5f * (0.4f - y)));
        cv::Rgb const greyStays = cv::Grade(cv::MakeConstants(s, 0.0f, false, 0), {0.3f, 0.3f, 0.3f});
        CHECK(approx(greyStays[0], 0.3f) && approx(greyStays[2], 0.3f));
    }

    // The presets: monotonic, black and white kept, darker mid-tones, more contrast.
    for (cv::Profile const p : {cv::Profile::Quest, cv::Profile::Vivid}) {
        cv::Constants const k = cv::MakeConstants(cv::Preset(p), 0.0f, false, 0);
        bool monotonic = true;
        float last = -1.0f;
        for (int i = 0; i <= 255; ++i) {
            float const v = grey(k, static_cast<float>(i) / 255.0f);
            monotonic = monotonic && v >= last;
            last = v;
        }
        CHECK(monotonic);
        CHECK(approx(grey(k, 0.0f), 0.0f) && approx(grey(k, 1.0f), 1.0f));
        CHECK(grey(k, 0.5f) < 0.5f);
        cv::Rgb const red = cv::Grade(k, {0.6f, 0.3f, 0.3f});
        CHECK(red[0] - red[1] > 0.3f * 1.05f);  // more colour
    }
    CHECK(cv::Preset(cv::Profile::Vivid).saturation > cv::Preset(cv::Profile::Quest).saturation);

    // Out-of-range values are clamped; NaN is the identity.
    {
        cv::Settings s;
        s.brightness = 9.0f;
        s.contrast = -1.0f;
        s.saturation = std::nanf("");
        s.gamma = 0.01f;
        s.black = 1.0f;
        s.gain = {0.0f, 2.0f, std::numeric_limits<float>::infinity()};
        cv::Settings const c = cv::Clamped(s);
        CHECK(c.brightness == 1.5f && c.contrast == 0.5f && c.saturation == 1.0f && c.gamma == 0.5f && c.black == 0.2f);
        CHECK(c.gain[0] == 0.5f && c.gain[1] == 1.5f && c.gain[2] == 1.0f);
    }

    // The constants as the shader reads them.
    {
        cv::Settings s;
        s.gamma = 0.8f;
        s.contrast = 1.1f;
        s.brightness = 1.2f;
        s.gain = {1.0f, 0.9f, 1.1f};
        cv::Constants const k = cv::MakeConstants(s, 0.5f, true, 3u);
        CHECK(approx(k.tone[1], 1.25f) && approx(k.tone[2], 0.2f) && approx(k.gain[1], 1.08f) && approx(k.gain[2], 1.32f));
        CHECK(approx(k.misc[0], 0.5f / 255.0f) && k.misc[1] == 1.0f && k.seed[0] == 3u);
        CHECK(cv::MakeConstants(s, 0.5f, false, 0).misc[1] == 0.0f);
    }

    // The linear path is the encoded path wrapped in a decode and an encode.
    {
        cv::Constants const k = cv::MakeConstants(cv::Preset(cv::Profile::Quest), 0.0f, true, 0);
        cv::Constants const e = cv::MakeConstants(cv::Preset(cv::Profile::Quest), 0.0f, false, 0);
        bool same = true;
        for (int i = 0; i <= 255; i += 5) {
            float const v = static_cast<float>(i) / 255.0f;
            cv::Rgb const lin = cv::Correct(k, {cv::Decode(v), cv::Decode(v), cv::Decode(v)}, 1, 1, true);
            cv::Rgb const enc = cv::Correct(e, {v, v, v}, 1, 1, false);
            same = same && approx(cv::Encode(lin[0]), enc[0], 2e-5f);
        }
        CHECK(same);
        CHECK(approx(cv::Encode(cv::Decode(0.5f)), 0.5f) && approx(cv::Decode(1.0f), 1.0f) && cv::Encode(0.0f) == 0.0f);
    }

    // The dither: triangular in (-1, 1), mean 0, the same at the same pixel, different per seed.
    {
        double sum = 0.0;
        int inner = 0;
        bool inRange = true;
        int const n = 256 * 256;
        for (std::uint32_t y = 0; y < 256; ++y) {
            for (std::uint32_t x = 0; x < 256; ++x) {
                float const d = cv::Dither(x, y, 0);
                inRange = inRange && d > -1.0f && d < 1.0f;
                sum += d;
                inner += std::abs(d) < 0.5f ? 1 : 0;
            }
        }
        CHECK(inRange);
        CHECK(std::abs(sum / n) < 0.01);
        CHECK(std::abs(static_cast<double>(inner) / n - 0.75) < 0.02);  // triangular: 3/4 within half its reach
        CHECK(cv::Dither(10, 20, 0) == cv::Dither(10, 20, 0) && cv::Dither(10, 20, 0) != cv::Dither(10, 20, 1));
    }
    // ... and what it is for: a level between two 8-bit steps averages closer to its value over an area.
    {
        cv::Constants const without = cv::MakeConstants(cv::Settings{}, 0.0f, false, 0);
        cv::Constants const with = cv::MakeConstants(cv::Settings{}, 0.5f, false, 0);
        float const v = (100.0f + 0.4f) / 255.0f;
        double plain = 0.0;
        double dithered = 0.0;
        int maxStep = 0;
        for (std::uint32_t y = 0; y < 128; ++y) {
            for (std::uint32_t x = 0; x < 128; ++x) {
                plain += cv::ToUnorm8(cv::Correct(without, {v, v, v}, x, y, false)[0]);
                int const q = cv::ToUnorm8(cv::Correct(with, {v, v, v}, x, y, false)[0]);
                dithered += q;
                maxStep = std::max(maxStep, std::abs(q - 100));
            }
        }
        double const target = 100.4;
        CHECK(std::abs(plain / (128.0 * 128.0) - target) > 0.35);
        CHECK(std::abs(dithered / (128.0 * 128.0) - target) < 0.15);
        CHECK(maxStep <= 1);  // never more than one step from the value
    }

    // Profiles by name.
    CHECK(cv::ParseProfile("Quest") == cv::Profile::Quest && cv::ParseProfile(" quest streamed ") == cv::Profile::Quest);
    CHECK(cv::ParseProfile("AUTO") == cv::Profile::Auto && cv::ParseProfile("custom") == cv::Profile::Custom);
    CHECK(!cv::ParseProfile("sepia").has_value());
    for (cv::Profile const p : {cv::Profile::Auto, cv::Profile::Neutral, cv::Profile::Quest, cv::Profile::Vivid, cv::Profile::Custom}) {
        CHECK(cv::ParseProfile(cv::Name(p)) == p);
    }

    // Headsets, as the runtimes name them (the user's PS VR2 on SteamVR in our logs; the others from research/27).
    {
        cv::Detection const psvr2 = cv::Detect("SteamVR/OpenXR", "SteamVR/OpenXR : playstation_vr2");
        CHECK(psvr2.display == cv::Display::Oled && psvr2.automatic == cv::Profile::Neutral);
        CHECK(cv::Detect("Oculus", "Meta Quest 3").automatic == cv::Profile::Quest);
        CHECK(cv::Detect("Oculus", "Oculus Quest2").automatic == cv::Profile::Quest);
        CHECK(cv::Detect("VirtualDesktopXR", "Meta Quest Pro").automatic == cv::Profile::Quest);
        CHECK(cv::Detect("VirtualDesktopXR", "").automatic == cv::Profile::Quest);
        CHECK(cv::Detect("SteamVR/OpenXR", "SteamVR/OpenXR : oculus").display == cv::Display::QuestStreamed);
        CHECK(cv::Detect("SteamVR/OpenXR", "SteamVR/OpenXR : vrlink").automatic == cv::Profile::Quest);
        CHECK(cv::Detect("SteamVR/OpenXR", "SteamVR/OpenXR : lighthouse").automatic == cv::Profile::Neutral);
        CHECK(cv::Detect("SteamVR/OpenXR", "SteamVR/OpenXR : pico").automatic == cv::Profile::Neutral);
        CHECK(cv::Detect("Oculus", "Oculus Rift S").display == cv::Display::Wired);
        CHECK(cv::Detect("Windows Mixed Reality Runtime", "Reverb G2").automatic == cv::Profile::Neutral);
        CHECK(cv::Detect("", "").automatic == cv::Profile::Neutral);
        CHECK(!psvr2.why.empty());
    }
    CHECK(cv::HeadsetKey("SteamVR/OpenXR", "SteamVR/OpenXR : playstation_vr2") == "steamvr_openxr_playstation_vr2");
    CHECK(cv::HeadsetKey("Oculus", "Meta Quest 3") == "oculus__meta_quest_3");
    CHECK(cv::HeadsetKey("VirtualDesktopXR", "Meta Quest 3") == "virtualdesktopxr__meta_quest_3");
    CHECK(cv::HeadsetKey("Oculus", "").empty());

    // The settings file: [Colour] for any headset, [Colour.<key>] for this one, key by key.
    {
        cv::Section const parsed = cv::ParseSection("Profile = Quest ; the preset\r\nContrast=1.2\n;gamma=0.5\n\n[next]\nblack = -0.02\n");
        CHECK(parsed.size() == 3 && parsed.at("profile") == "Quest" && parsed.at("contrast") == "1.2" && parsed.at("black") == "-0.02");
        cv::Detection const psvr2 = cv::Detect("SteamVR/OpenXR", "SteamVR/OpenXR : playstation_vr2");
        cv::Detection const quest = cv::Detect("Oculus", "Meta Quest 3");
        cv::Resolved const none = cv::Resolve({}, {}, psvr2);
        CHECK(none.chosen == cv::Profile::Auto && none.effective == cv::Profile::Neutral && cv::IsNeutral(none.settings));
        CHECK(!none.compare && none.dither == 0.5f);
        cv::Resolved const automatic = cv::Resolve({{"profile", "auto"}}, {}, quest);
        CHECK(automatic.effective == cv::Profile::Quest && automatic.settings.gamma == cv::Preset(cv::Profile::Quest).gamma);
        cv::Section const common{{"profile", "vivid"}, {"contrast", "1.3"}, {"compare", "1"}, {"dither", "7"}};
        cv::Section const own{{"profile", "custom"}, {"saturation", "1.4"}, {"contrast", "nonsense"}};
        cv::Resolved const custom = cv::Resolve(common, own, quest);
        CHECK(custom.chosen == cv::Profile::Custom && custom.effective == cv::Profile::Custom);
        CHECK(custom.settings.saturation == 1.4f && custom.settings.contrast == 1.3f);  // a bad value falls through to [Colour]
        CHECK(custom.compare && custom.dither == 2.0f);
        cv::Resolved const common_only = cv::Resolve(common, {{"profile", "sepia"}}, quest);
        CHECK(common_only.chosen == cv::Profile::Vivid && common_only.settings.contrast == cv::Preset(cv::Profile::Vivid).contrast);
        CHECK(common_only.custom.contrast == 1.3f);  // the custom values are kept for when Custom is chosen
    }
}

void TestColourStrip() {
    namespace cv = mevr::vr::colour;
    cv::Strip const s = cv::TestStrip(1024, 576);
    CHECK(s.width == 1024 && s.height == 576 && s.pixels.size() == 1024u * 576u);
    int const band = 576 / cv::kStripBands;
    auto at = [&](int x, int b) { return s.pixels[static_cast<std::size_t>(b * band + band / 2) * 1024 + x]; };
    auto grey = [](std::uint32_t p) { return static_cast<int>(p & 0xFFu); };
    CHECK(grey(at(0, 0)) == 0 && grey(at(1023, 0)) == 255 && grey(at(512, 0)) == 128);   // 17 steps
    CHECK(grey(at(0, 1)) == 0 && grey(at(1023, 1)) == 255);                              // the ramp
    CHECK(grey(at(0, 2)) == 0 && grey(at(1023, 2)) == 30 && grey(at(100, 2)) == 2);      // near black
    CHECK(grey(at(0, 3)) == 225 && grey(at(1023, 3)) == 255);                            // near white
    CHECK(at(0, 4) == (115u | (82u << 8) | (68u << 16) | 0xFF000000u));                  // dark skin
    CHECK(at(0, 5) == (255u | 0xFF000000u) && at(1023, 5) == (128u | (128u << 8) | 0xFF000000u));  // red .. half yellow
    bool opaque = true;
    for (std::uint32_t const p : s.pixels) {
        opaque = opaque && (p >> 24) == 0xFFu;
    }
    CHECK(opaque);
    CHECK(cv::TestStrip(10, 10).width == 96);
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

void TestFrameProbe() {
    using namespace mevr::vr::frame_probe;
    CHECK(std::string(PointName(kStart)) == "start" && std::string(PointName(kEnd)) == "end" &&
        std::string(PointName(kPointCount)) == "?");
    CHECK(GpuIndex(kStart) == 0 && GpuIndex(kPass0) == 1 && GpuIndex(kEnd) == 4 && GpuIndex(kWaitBegin) == -1);

    Clocks clocks;
    clocks.qpcFrequency = 1e7;
    clocks.cycleFrequency = 3e9;
    clocks.gpuFrequency = 1e9;
    clocks.originQpc = 1000;
    // One sync: its offset everywhere. Two: a straight line between them (drift).
    clocks.syncBegin = {1000, 5.0, true};
    CHECK(NearD(GpuOffsetAt(clocks, 999999), 5.0));
    clocks.syncEnd = {1000 + 100'000'000, 5.002, true};  // 10 s later, 2 ms of drift
    CHECK(NearD(GpuOffsetAt(clocks, 1000 + 50'000'000), 5.001, 1e-9));
    clocks.syncEnd.valid = false;

    Frame frame;
    frame.qpc[kStart] = 1000 + 100'000;    // 10 ms after the origin
    frame.qpc[kPresent] = 1000 + 180'000;  // 18 ms
    frame.cycles[kStart] = 3'000'000;
    frame.cycles[kPresent] = 6'000'000;    // 1 ms busy
    frame.draws[kStart] = 0xFFFFFFF0u;
    frame.draws[kPresent] = 0x10u;         // 32 draws across the counter's wrap
    double const originSeconds = 1000.0 / 1e7;
    frame.gpu[GpuIndex(kStart)] = static_cast<std::uint64_t>(std::llround((originSeconds + 0.0105 + 5.0) * 1e9));
    frame.gpuValid = true;
    frame.xrFrame = true;
    frame.displayPeriodNs = 11'111'111;
    frame.swapchainWaitQpc = 5'000;        // 0.5 ms
    frame.accessViolations = 3;
    frame.pass0Marks = 1;
    std::string const header = FrameCsvHeader();
    std::string const row = FrameCsvRow(7, frame, clocks);
    CHECK(header.starts_with("frame,t_start,busy_start,draws_start,t_pass0,") && header.ends_with("pass0_marks\n"));
    CHECK(std::count(header.begin(), header.end(), ',') == std::count(row.begin(), row.end(), ','));
    CHECK(row.starts_with("7,10.0000,0.0000,0,,,,18.0000,1.0000,32,"));
    CHECK(row.find(",10.5000,") != std::string::npos);  // the GPU start, on the CPU's clock
    CHECK(row.ends_with(",1,1,1,11.1111,0.5000,3,1\n"));
    frame.gpuValid = false;
    CHECK(FrameCsvRow(7, frame, clocks).find(",10.5000,") == std::string::npos);

    Tick tick;
    tick.beginQpc = 1000 + 10'000;       // 1 ms
    tick.engineEndQpc = 1000 + 90'000;   // the engine: 8 ms
    tick.endQpc = 1000 + 100'000;        // ours: 1 ms
    tick.beginCycles = 0;
    tick.engineEndCycles = 15'000'000;   // 5 ms busy
    tick.endCycles = 18'000'000;         // 1 ms busy
    CHECK(TickCsvHeader() == "tick,t_begin,engine_ms,engine_busy_ms,host_ms,host_busy_ms\n");
    CHECK(TickCsvRow(2, tick, clocks) == "2,1.0000,8.0000,5.0000,1.0000,1.0000\n");
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
    // The shotgun is a left-hip cross-draw, well clear of the pistol (the belly slot was 0.29 m away and in the right
    // hand's path to the hip), and a right hand resting in front of the belly is in no slot.
    {
        Slot const& pistol = DefaultLayout().slots[0];
        Slot const& shotgun = DefaultLayout().slots[2];
        CHECK(shotgun.weapon == Weapon::Shotgun && std::string_view(shotgun.name) == "hipLeft");
        double const apart = Length(SlotCenter(frame, pistol, 1.0) - SlotCenter(frame, shotgun, 1.0));
        CHECK(apart > pistol.radius + shotgun.radius + 2.0 * DefaultLayout().exitMargin);
        Vec3 const resting = frame.anchor + Vec3{0.08, -0.55, -0.15};  // in front of the belly, a little right
        for (Slot const& slot : DefaultLayout().slots) {
            CHECK(Length(resting - SlotCenter(frame, slot, 1.0)) > slot.radius);
        }
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

    // Let go at the own slot while the draw still animates (held already, the game mid-switch: TryHolster would be
    // ignored): it's kept until the switch is over, then holstered, with no grace.
    m.Reset();
    in = base(0.05, 1.0);
    in.deltaSeconds = 0.011;
    in.gripDown = in.newSqueeze = true;
    CHECK(m.Update(in).action == Action::Draw);
    in.newSqueeze = false;
    in.held = Weapon::Pistol;
    in.switching = true;
    in.gripDown = false;
    s = m.Update(in);
    CHECK(s.action == Action::ReturnArmed && s.slot == 0 && m.CurrentPhase() == Phase::Returning);
    for (int i = 0; i < 20; ++i) {  // 0.22 s of switching: nothing, and it says why
        s = m.Update(in);
        CHECK(s.action == Action::None && s.returnWaitsForSwitch && m.CurrentPhase() == Phase::Returning);
    }
    in.switching = false;
    s = m.Update(in);
    CHECK(s.action == Action::Return && s.weapon == Weapon::Pistol && m.CurrentPhase() == Phase::Idle);
    // ...and a squeeze again before the switch ends keeps the gun (the deferral is off).
    m.Reset();
    in = base(0.05, 1.0);
    in.gripDown = in.newSqueeze = true;
    m.Update(in);
    in.newSqueeze = false;
    in.held = Weapon::Pistol;
    in.switching = true;
    in.gripDown = false;
    CHECK(m.Update(in).action == Action::ReturnArmed);
    in.gripDown = in.newSqueeze = true;
    s = m.Update(in);
    CHECK(s.action != Action::Return && s.action != Action::Holster && m.CurrentPhase() == Phase::Grab);
    in.newSqueeze = false;
    in.switching = false;
    for (int i = 0; i < 10; ++i) {
        CHECK(m.Update(in).action == Action::None && m.CurrentPhase() == Phase::Grab);
    }
    // Away from the slots the grace's clock stops while the switch runs.
    m.Reset();
    in = base(0.6, 1.0);
    in.held = Weapon::Pistol;
    in.gripDown = in.newSqueeze = true;
    m.Update(in);
    in.newSqueeze = in.gripDown = false;
    in.switching = true;
    CHECK(m.Update(in).action == Action::ReturnArmed);
    in.deltaSeconds = 0.5;
    CHECK(m.Update(in).action == Action::None && m.ReturnLeft() > 0.28);
    in.switching = false;
    CHECK(m.Update(in).action == Action::Return);

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

void TestCrouchFraction() {
    using namespace mevr::ik::posture;
    CHECK(NearD(FractionFromFloor(1.2, 1.6).value_or(-1.0), 0.75, 1e-12));
    CHECK(!FractionFromFloor(1.2, 0.0));
    CHECK(!FractionFromFloor(std::nan(""), 1.6));
    // No floor: the head 0.4 m below where it stood (tracking space), over an assumed 1.65 m.
    CHECK(NearD(FractionFromRecenter(-0.4, 0.0, 1.65).value_or(-1.0), 1.25 / 1.65, 1e-12));
    // A recenter that didn't put the standing head at 0 (another space origin): only the drop counts.
    CHECK(NearD(FractionFromRecenter(0.3, 0.3, 1.65).value_or(-1.0), 1.0, 1e-12));
    CHECK(!FractionFromRecenter(0.0, std::nan(""), 1.65));
    // Settings are clamped: stand above at least 0.02 over crouch below, the dwell 0..2 s.
    CrouchParams p;
    p.crouchBelow = 0.9;
    p.standAbove = 0.5;
    p.dwellSeconds = -1.0;
    CrouchParams const s = Sanitize(p);
    CHECK(NearD(s.crouchBelow, 0.9, 1e-12) && NearD(s.standAbove, 0.92, 1e-12) && NearD(s.dwellSeconds, 0.0, 1e-12));
    p.crouchBelow = 0.1;
    p.standAbove = 2.0;
    p.dwellSeconds = 9.0;
    CrouchParams const t = Sanitize(p);
    CHECK(NearD(t.crouchBelow, 0.3, 1e-12) && NearD(t.standAbove, 0.99, 1e-12) && NearD(t.dwellSeconds, 2.0, 1e-12));
}

void TestCrouchDetector() {
    using namespace mevr::ik::posture;
    CrouchParams const p;  // 0.75 / 0.85 / 0.2 s
    double const dt = 1.0 / 90.0;
    CrouchDetector d;
    // Standing tall, then a quick dip below the threshold (a duck shorter than the dwell): nothing.
    CHECK(d.Update(1.0, dt, p) == CrouchEvent::None);
    for (int i = 0; i < 10; ++i) {  // 0.11 s
        CHECK(d.Update(0.7, dt, p) == CrouchEvent::None);
    }
    CHECK(d.Update(0.9, dt, p) == CrouchEvent::None && d.Dwell() == 0.0 && !d.Crouched());
    // Staying down: a crouch once the head has been below 0.75 for 0.2 s, exactly once.
    int crouches = 0;
    int ticks = 0;
    for (int i = 0; i < 60; ++i) {
        CrouchEvent const e = d.Update(0.7, dt, p);
        crouches += e == CrouchEvent::Crouch ? 1 : 0;
        ticks += crouches == 0 ? 1 : 0;
    }
    CHECK(crouches == 1 && d.Crouched());
    CHECK(ticks >= 17 && ticks <= 18);  // 18 ticks of 1/90 s reach 0.2 s
    // Hysteresis: rising to 0.8 (between the thresholds) keeps the crouch however long.
    for (int i = 0; i < 200; ++i) {
        CHECK(d.Update(0.8, dt, p) == CrouchEvent::None);
    }
    CHECK(d.Crouched());
    // A lost measurement holds the state and restarts the dwell.
    for (int i = 0; i < 10; ++i) {
        d.Update(0.95, dt, p);
    }
    CHECK(d.Dwell() > 0.0);
    CHECK(d.Update(std::nullopt, dt, p) == CrouchEvent::None && d.Dwell() == 0.0 && d.Crouched());
    // Standing up past 0.85 for the dwell: a stand.
    int stands = 0;
    for (int i = 0; i < 30; ++i) {
        stands += d.Update(0.95, dt, p) == CrouchEvent::Stand ? 1 : 0;
    }
    CHECK(stands == 1 && !d.Crouched());
    // Hovering around one threshold doesn't flap: only a full dwell past it counts.
    int events = 0;
    for (int i = 0; i < 300; ++i) {
        events += d.Update(i % 4 < 2 ? 0.74 : 0.76, dt, p) != CrouchEvent::None ? 1 : 0;
    }
    CHECK(events == 0 && !d.Crouched());
    // No dwell: at once. A reset starts crouched if asked.
    CrouchParams quick = p;
    quick.dwellSeconds = 0.0;
    CHECK(d.Update(0.5, dt, quick) == CrouchEvent::Crouch);
    d.Reset(true);
    CHECK(d.Crouched() && d.Update(1.0, dt, quick) == CrouchEvent::Stand);
}

void TestCrouchArbiter() {
    using namespace mevr::ik::posture;
    // Crouch while the game stands: ask; the game takes it; it's ours.
    CrouchArbiter a;
    ArbiterStep s = a.Update({CrouchEvent::None, false, true});
    CHECK(s.action == CrouchAction::None && !s.external);
    s = a.Update({CrouchEvent::Crouch, false, true});
    CHECK(s.action == CrouchAction::Crouch);
    a.Applied(true);
    CHECK(a.Owns() && !a.Pending());
    // Nothing more while nothing changes.
    s = a.Update({CrouchEvent::None, true, true});
    CHECK(s.action == CrouchAction::None && !s.external);
    // L3 stands Shepard up while the player stays crouched: seen, kept, no longer ours.
    s = a.Update({CrouchEvent::None, false, true});
    CHECK(s.external && s.action == CrouchAction::None && !a.Owns());
    for (int i = 0; i < 5; ++i) {
        CHECK(a.Update({CrouchEvent::None, false, true}).action == CrouchAction::None);
    }
    // The player stands: the game already stands: nothing to ask.
    s = a.Update({CrouchEvent::Stand, false, true});
    CHECK(s.action == CrouchAction::None && s.alreadyThere && !a.Owns());
    // L3 crouches him while the player stands: kept.
    s = a.Update({CrouchEvent::None, true, true});
    CHECK(s.external && s.action == CrouchAction::None);
    // The player crouches too: already there, and from now on it's the player's (a stand releases it).
    s = a.Update({CrouchEvent::Crouch, true, true});
    CHECK(s.alreadyThere && a.Owns());
    s = a.Update({CrouchEvent::Stand, true, true});
    CHECK(s.action == CrouchAction::Stand);
    a.Applied(false);
    CHECK(!a.Owns() && !a.Pending());
    // A crouch while sprinting waits, then goes once the game takes changes again.
    s = a.Update({CrouchEvent::Crouch, false, false});
    CHECK(s.deferred && s.action == CrouchAction::None && a.Pending() == std::optional<bool>(true));
    s = a.Update({CrouchEvent::None, false, false});
    CHECK(s.deferred);
    s = a.Update({CrouchEvent::None, false, true});
    CHECK(s.action == CrouchAction::Crouch);
    a.Applied(true);
    CHECK(a.Owns());
    // A stand that's overtaken by a crouch before it could go: only the latest counts.
    s = a.Update({CrouchEvent::Stand, true, false});
    CHECK(s.deferred && a.Pending() == std::optional<bool>(false));
    s = a.Update({CrouchEvent::Crouch, true, false});
    CHECK(s.alreadyThere && !a.Pending() && a.Owns());
    // A change the game doesn't take is asked kMaxTries times, then dropped.
    s = a.Update({CrouchEvent::Stand, true, true});
    int asks = 0;
    for (int i = 0; i < 10 && a.Pending(); ++i) {
        if (s.action == CrouchAction::Stand) {
            ++asks;
            a.Applied(true);  // bWantsToCrouch didn't change
        }
        s = a.Update({CrouchEvent::None, true, true});
    }
    CHECK(asks == CrouchArbiter::kMaxTries && s.gaveUp && !a.Pending());
    // While inactive (a conversation) a change by the game is seen, and nothing waits.
    CrouchArbiter b;
    b.Update({CrouchEvent::Crouch, false, true});
    b.Applied(true);
    CHECK(b.Owns());
    b.Observe(true);
    CHECK(b.Owns());
    b.Observe(false);  // the conversation stood him up
    CHECK(!b.Owns());
    // Releasing a crouch of ours outside a physical change (the setting off): nothing stays ours.
    CrouchArbiter c;
    c.Update({CrouchEvent::Crouch, false, true});
    c.Applied(true);
    c.Applied(false);
    CHECK(!c.Owns());
    // A new pawn: nothing seen, nothing ours.
    c.Update({CrouchEvent::Crouch, false, true});
    c.Applied(true);
    c.Forget();
    CHECK(!c.Owns() && !c.Pending());
    CHECK(!c.Update({CrouchEvent::None, true, true}).external);
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

    // Point at the right squadmate and pull the left trigger: open at once with that card.
    in.pointed = 1;
    in.triggerDown = true;
    in.newPull = true;
    Step s = step();
    CHECK(s.opened && s.openedPointing && m.IsOpen() && m.Selected() == 1 && m.Highlighted() == -1);
    in.newPull = false;
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
    in.triggerDown = false;
    s = step();
    CHECK(s.closed == Close::Confirmed && s.member == 1 && s.weapon == 2 && !m.IsOpen());

    // Let go with nothing chosen: cancelled.
    in.triggerDown = true;
    in.newPull = true;
    in.pointed = 0;
    s = step();
    CHECK(s.opened && m.Selected() == 0);
    in.newPull = false;
    in.triggerDown = false;
    s = step();
    CHECK(s.closed == Close::Released && s.weapon == -1);

    // B cancels; A confirms only with something chosen; an A held when it opened doesn't count.
    in.triggerDown = true;
    in.newPull = true;
    in.a = true;
    s = step();
    CHECK(s.opened);
    in.newPull = false;
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
    in.triggerDown = false;
    step();
    in.triggerDown = true;
    in.newPull = true;
    step();
    in.newPull = false;
    in.b = true;
    s = step();
    CHECK(s.closed == Close::CancelButton);
    in.b = false;
    in.triggerDown = false;
    step();

    // The left stick picks the card; a weapon the new one lacks is no longer chosen.
    in.triggerDown = true;
    in.newPull = true;
    in.pointed = 0;
    step();
    in.newPull = false;
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
    in.triggerDown = false;
    s = step();
    CHECK(s.closed == Close::Released);

    // Sticks already pushed when it opens (walking, turning) choose nothing until they move on.
    in.leftX = -0.9f;
    in.rightY = 0.9f;
    in.triggerDown = true;
    in.newPull = true;
    in.pointed = 1;
    s = step();
    CHECK(s.opened && m.Selected() == 1 && m.Highlighted() == -1);
    in.newPull = false;
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
    in.triggerDown = false;
    step();

    // Pointing at nobody: it opens at once (the trigger is deliberate) with the preferred card.
    in.pointed = -1;
    in.preferred = 1;
    in.triggerDown = true;
    in.newPull = true;
    s = step();
    CHECK(s.opened && !s.openedPointing && m.IsOpen() && m.Selected() == 1);
    in.newPull = false;
    in.triggerDown = false;
    s = step();
    CHECK(s.closed == Close::Released);
    // No preferred one: the first card.
    in.preferred = -1;
    in.triggerDown = true;
    in.newPull = true;
    s = step();
    CHECK(s.opened && m.Selected() == 0);
    in.newPull = false;
    in.triggerDown = false;
    step();
    // With no squad, a refusal.
    in.memberCount = 0;
    in.triggerDown = true;
    in.newPull = true;
    s = step();
    CHECK(s.refused && !m.IsOpen());
    in.memberCount = 2;
    in.triggerDown = false;
    in.newPull = false;
    step();
    // Not usable: a pull does nothing.
    in.usable = false;
    in.triggerDown = true;
    in.newPull = true;
    in.pointed = 0;
    s = step();
    CHECK(!s.opened && m.CurrentPhase() == Phase::Idle);
    in.usable = true;
    // A trigger held from before (no new pull) never opens it.
    in.newPull = false;
    s = step();
    CHECK(!s.opened && !m.IsOpen());
    in.triggerDown = false;
    step();

    // confirmOnRelease off: letting go keeps it open; A confirms; a new pull cancels; it times out.
    in.confirmOnRelease = false;
    in.idleCloseSeconds = 1.0f;
    in.triggerDown = true;
    in.newPull = true;
    in.pointed = 0;
    step();
    in.newPull = false;
    in.triggerDown = false;
    s = step();
    CHECK(s.closed == Close::None && m.IsOpen());
    in.rightY = 0.9f;
    step();
    in.rightY = 0.0f;
    in.a = true;
    s = step();
    CHECK(s.closed == Close::Confirmed && s.weapon == 2);
    in.a = false;
    in.triggerDown = true;
    in.newPull = true;
    step();
    in.newPull = false;
    in.triggerDown = false;
    step();
    in.triggerDown = true;
    in.newPull = true;
    s = step();
    CHECK(s.closed == Close::Repulled);
    in.triggerDown = false;
    in.newPull = false;
    step();
    in.triggerDown = true;
    in.newPull = true;
    step();
    in.newPull = false;
    in.triggerDown = false;
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

void TestGameMenuRequest() {
    using namespace mevr::vr::game_menu;
    // The values cross the host/mod boundary: unknown ones are None.
    CHECK(FromValue(0) == Action::None && FromValue(1) == Action::PauseMenu && FromValue(2) == Action::Holster);
    CHECK(FromValue(3) == Action::None && FromValue(0xFFFFFFFFu) == Action::None);
    // The slot: taken once; a newer request replaces one not yet taken; Polled once the mod has asked.
    Slot slot;
    CHECK(!slot.Polled());
    CHECK(slot.Put(Action::PauseMenu) == Action::None);
    CHECK(slot.Put(Action::Holster) == Action::PauseMenu);
    CHECK(slot.Take() == Action::Holster);
    CHECK(slot.Polled());
    CHECK(slot.Take() == Action::None);
    // Nothing taken: nothing runs.
    Schedule s;
    Step st = Advance(s, false, false);
    CHECK(st.run == Action::None && st.dropped == Action::None);
    Take(s, Action::None);
    CHECK(s.action == Action::None);
    // Taken while the game is still paused by the menu: waits for kSettleTicks ticks unpaused in a row.
    Take(s, Action::PauseMenu);
    st = Advance(s, false, true);
    CHECK(st.run == Action::None && st.dropped == Action::None);
    for (int i = 0; i < kSettleTicks - 1; ++i) {
        st = Advance(s, false, false);
        CHECK(st.run == Action::None);
    }
    st = Advance(s, false, false);
    CHECK(st.run == Action::PauseMenu && st.dropped == Action::None);
    CHECK(s.action == Action::None);
    CHECK(Advance(s, false, false).run == Action::None);  // once only
    // A pause in between starts the count again.
    Take(s, Action::Holster);
    st = Advance(s, false, false);
    CHECK(st.run == Action::None);  // kSettleTicks is 2
    st = Advance(s, false, true);
    CHECK(st.run == Action::None);
    int ticks = 0;
    do {
        st = Advance(s, false, false);
        ++ticks;
    } while (st.run == Action::None && ticks < 100);
    CHECK(st.run == Action::Holster && ticks == kSettleTicks);
    // Our menu opening again calls it off.
    Take(s, Action::PauseMenu);
    st = Advance(s, true, true);
    CHECK(st.run == Action::None && st.dropped == Action::PauseMenu && st.reopened);
    CHECK(s.action == Action::None);
    // Paused for good (another pause holds): dropped after kGiveUpTicks, not reopened.
    Take(s, Action::Holster);
    for (int i = 0; i < kGiveUpTicks - 1; ++i) {
        st = Advance(s, false, true);
        CHECK(st.run == Action::None && st.dropped == Action::None);
    }
    st = Advance(s, false, true);
    CHECK(st.dropped == Action::Holster && !st.reopened && st.run == Action::None);
    // A new request replaces a waiting one.
    Take(s, Action::Holster);
    Advance(s, false, true);
    Take(s, Action::PauseMenu);
    for (int i = 0; i < kSettleTicks - 1; ++i) {
        Advance(s, false, false);
    }
    CHECK(Advance(s, false, false).run == Action::PauseMenu);
}

void TestCloseLatch() {
    using namespace mevr::game::gamepad;
    CloseLatch latch;
    // Never held: nothing is kept.
    XINPUT_GAMEPAD pad{};
    pad.wButtons = XINPUT_GAMEPAD_A;
    pad.bRightTrigger = 255;
    CHECK(!ApplyCloseLatch(latch, pad) && pad.wButtons == XINPUT_GAMEPAD_A && pad.bRightTrigger == 255);
    // The menu had the pad; it closes with A and the right trigger down: both kept, B (not down then) isn't later.
    HoldCloseLatch(latch);
    pad = {};
    pad.wButtons = XINPUT_GAMEPAD_A;
    pad.bRightTrigger = 200;
    pad.sThumbLX = 1234;
    CHECK(ApplyCloseLatch(latch, pad) && pad.wButtons == 0 && pad.bRightTrigger == 0 && pad.sThumbLX == 1234);
    pad.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B;
    pad.bRightTrigger = 200;
    CHECK(ApplyCloseLatch(latch, pad) && pad.wButtons == XINPUT_GAMEPAD_B && pad.bRightTrigger == 0);
    // A let go: free; pressed again it reaches the game. The trigger still kept until it drops below the threshold.
    pad.wButtons = 0;
    pad.bRightTrigger = 40;
    CHECK(ApplyCloseLatch(latch, pad) && pad.bRightTrigger == 0);
    pad.bRightTrigger = 10;
    CHECK(!ApplyCloseLatch(latch, pad) && pad.bRightTrigger == 10);
    pad.wButtons = XINPUT_GAMEPAD_A;
    pad.bRightTrigger = 255;
    CHECK(!ApplyCloseLatch(latch, pad) && pad.wButtons == XINPUT_GAMEPAD_A && pad.bRightTrigger == 255);
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
    // Pointing: the enter goes out with the pull; the wheel opens when the game's mode says so.
    in.triggerDown = true;
    in.newPull = true;
    in.pointed = true;
    wheel::Step s = m.Update(in);
    CHECK(s.enter && s.openedPointing && m.CurrentPhase() == Phase::Opening && m.Holding());
    in.newPull = false;
    s = m.Update(in);
    CHECK(!s.enter && !s.opened && m.CurrentPhase() == Phase::Opening);
    in.onFoot = false;
    in.wheelActive = true;
    s = m.Update(in);
    CHECK(s.opened && m.CurrentPhase() == Phase::Open);
    // Let go with a carried entry highlighted: A for a moment, then the exit once it's queued.
    in.highlightValid = true;
    in.highlightCarried = true;
    in.triggerDown = false;
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

    // At nobody: it opens at once too (no hold), without a preselect.
    m.Reset();
    in = {};
    in.dt = 0.1f;
    in.usable = true;
    in.onFoot = true;
    in.triggerDown = true;
    in.newPull = true;
    s = m.Update(in);
    CHECK(s.enter && !s.openedPointing && m.CurrentPhase() == Phase::Opening);
    // A trigger held from before (no new pull): nothing.
    m.Reset();
    in.newPull = false;
    s = m.Update(in);
    CHECK(!s.enter && m.CurrentPhase() == Phase::Idle);
    // Not on foot (the Mako, a conversation): no wheel.
    in.newPull = true;
    in.onFoot = false;
    s = m.Update(in);
    CHECK(!s.enter && m.CurrentPhase() == Phase::Idle);
    // Not usable (both hands on the gun, no world view): no wheel.
    in.onFoot = true;
    in.usable = false;
    s = m.Update(in);
    CHECK(!s.enter && m.CurrentPhase() == Phase::Idle);

    // The game never enters the wheel (combat disabled in a hub): given up after the timeout.
    m.Reset();
    in = {};
    in.dt = 0.1f;
    in.usable = true;
    in.onFoot = true;
    in.triggerDown = true;
    in.newPull = true;
    in.pointed = true;
    m.Update(in);
    in.newPull = false;
    int updates = 0;
    bool failed = false;
    while (!failed && updates < 20) {
        failed = m.Update(in).openFailed;
        ++updates;
    }
    CHECK(failed && updates >= 6 && updates <= 7 && m.CurrentPhase() == Phase::Idle);
    // ...but if the wheel comes up late after all (the game paused in it), it's taken over: letting go closes it.
    in.onFoot = false;
    in.wheelActive = true;
    s = m.Update(in);
    CHECK(s.opened && s.adopted && !s.enter && !s.exit && m.CurrentPhase() == Phase::Open && m.Holding());
    in.triggerDown = false;
    s = m.Update(in);
    CHECK(s.exit && s.cancelled && !s.holdA && m.CurrentPhase() == Phase::Closing);
    in.wheelActive = false;
    in.onFoot = true;
    CHECK(m.Update(in).closed && m.CurrentPhase() == Phase::Idle);
    // Late with the trigger already up: shut at once, nothing given.
    m.Reset();
    in = {};
    in.dt = 0.1f;
    in.usable = true;
    in.onFoot = true;
    in.triggerDown = in.newPull = true;
    m.Update(in);
    in.newPull = false;
    in.triggerDown = false;
    for (int i = 0; i < 8; ++i) {
        m.Update(in);
    }
    CHECK(m.CurrentPhase() == Phase::Idle);
    in.onFoot = false;
    in.wheelActive = true;
    in.highlightValid = in.highlightCarried = true;
    s = m.Update(in);
    CHECK(s.opened && s.adopted && s.exit && !s.holdA && !s.confirmed && m.CurrentPhase() == Phase::Closing);
    // Too late (past kLateOpenSeconds): left alone, until a new pull takes it over.
    m.Reset();
    in = {};
    in.dt = 0.1f;
    in.usable = true;
    in.onFoot = true;
    in.triggerDown = in.newPull = true;
    m.Update(in);
    in.newPull = false;
    for (int i = 0; i < 8; ++i) {
        m.Update(in);
    }
    in.dt = 1.0f;
    for (int i = 0; i < 11; ++i) {
        m.Update(in);
    }
    in.triggerDown = false;
    in.onFoot = false;
    in.wheelActive = true;
    s = m.Update(in);
    CHECK(!s.opened && !s.adopted && m.CurrentPhase() == Phase::Idle);
    in.triggerDown = in.newPull = true;
    s = m.Update(in);
    CHECK(s.opened && s.adopted && !s.enter && m.CurrentPhase() == Phase::Open);
    // A wheel up that someone else opened (a pad's LB) isn't touched without a new pull.
    m.Reset();
    in = {};
    in.dt = 0.1f;
    in.usable = true;
    in.wheelActive = true;
    for (int i = 0; i < 5; ++i) {
        CHECK(!m.Update(in).opened && m.CurrentPhase() == Phase::Idle);
    }
    in.wheelActive = false;
    in.onFoot = true;
    in.triggerDown = true;  // as the failed open above left it, for the next case
    in.pointed = true;

    // A quick pull-and-let-go that the game still opens: shut at once, nothing given.
    m.Reset();
    in.newPull = true;
    m.Update(in);
    in.newPull = false;
    in.triggerDown = false;
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
    in.triggerDown = true;
    in.newPull = true;
    in.pointed = true;
    in.b = true;
    m.Update(in);
    in.newPull = false;
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
        in.triggerDown = true;
        in.newPull = true;
        in.pointed = true;
        m.Update(in);
        in.newPull = false;
        in.wheelActive = true;
        m.Update(in);
        in.highlightValid = true;
        in.highlightCarried = variant != 1;
        in.confirmOnRelease = variant != 0;
        in.choiceRecorded = variant == 2;
        in.triggerDown = false;
        s = m.Update(in);
        CHECK(s.exit && !s.holdA && m.CurrentPhase() == Phase::Closing);
        CHECK(s.cancelled == (variant != 2));
    }
    // The game closes it by itself (LB let go, the pause menu): back to idle, nothing to call.
    m.Reset();
    in = {};
    in.usable = true;
    in.onFoot = true;
    in.triggerDown = true;
    in.newPull = true;
    in.pointed = true;
    m.Update(in);
    in.newPull = false;
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

// ---- Physical grenades and melee (core/ik/Throwing, research/20) ----------------------------------

// A hand moving at constant velocity, sampled at 90 Hz: the release estimate is that velocity.
void TestThrowVelocity() {
    namespace thr = mevr::ik::throwing;
    using mevr::ik::Vec3;
    thr::MotionHistory history;
    Vec3 const v{1.0, 2.0, -6.0};
    double const dt = 1.0 / 90.0;
    for (int i = 0; i < 30; ++i) {
        history.Add(i * dt, v * (i * dt));
    }
    double const now = 29 * dt;
    auto const fit = history.FitVelocity(now - 0.05, now);
    CHECK(fit && std::abs(fit->x - 1.0) < 1e-6 && std::abs(fit->y - 2.0) < 1e-6 && std::abs(fit->z + 6.0) < 1e-6);
    thr::Release const r = thr::EstimateRelease(history, now);
    CHECK(r.valid && std::abs(r.speed - mevr::ik::Length(v)) < 1e-6 && r.samples >= 10);
    // A sample at the same time (or earlier) is ignored; a jump breaks the history.
    CHECK(!history.Add(now, {}));
    CHECK(history.Count() == 30);
    CHECK(history.Add(now + dt, {5.0, 5.0, 5.0}));
    CHECK(history.Count() == 1);
    // A gap breaks it too.
    CHECK(history.Add(now + dt + 0.2, {5.0, 5.0, 5.0}));

    // A throw: the hand speeds up to 8 m/s forward, then slows to 3 m/s before the grip opens 67 ms
    // later (the release comes late): the estimate is the peak, not the slow tail or an average.
    thr::MotionHistory throwing;
    double t = 0.0;
    Vec3 p{0.0, 0.0, 0.0};
    double peakTime = 0.0;
    for (int i = 0; i < 31; ++i) {
        double speed = 0.0;
        if (i < 25) {
            speed = 8.0 * i / 24.0;  // into the throw
            peakTime = t;
        } else {
            speed = std::max(3.0, 8.0 - (i - 24) * 1.0);  // the follow-through, slowing
        }
        p = p + Vec3{0.0, 0.0, -speed * dt};
        throwing.Add(t, p);
        t += dt;
    }
    double const release = t - dt;
    thr::Release const late = thr::EstimateRelease(throwing, release);
    CHECK(late.valid && late.speed > 7.0 && late.speed < 8.5);
    CHECK(late.velocity.z < -7.0 && std::abs(late.velocity.x) < 1e-6);
    CHECK(std::abs((release - late.peakAgo) - peakTime) < 0.03);
    // A plain average over the window would have been well under the peak.
    auto const average = throwing.FitVelocity(release - 0.12, release);
    CHECK(average && mevr::ik::Length(*average) < late.speed - 0.5);

    // Times from sequences: evenly spaced at the learned period, whatever the observation jitter; the
    // same sequence twice is no new sample; a gap starts over.
    thr::SampleClock clock;
    auto const first = clock.Update(100, 10.0);
    CHECK(first && std::abs(*first - 10.0) < 1e-9);
    CHECK(!clock.Update(100, 10.004).has_value());
    double previous = *first;
    double observed = 10.0;
    for (int i = 1; i <= 200; ++i) {
        observed += (i % 2 == 0) ? 0.009 : 0.0132;  // seen unevenly
        auto const at = clock.Update(100 + static_cast<std::uint64_t>(i), observed);
        CHECK(at.has_value());
        if (at && i > 100) {
            CHECK(std::abs((*at - previous) - 0.0111) < 0.0012);
        }
        previous = at ? *at : previous;
    }
    CHECK(std::abs(clock.Period() - 0.0111) < 0.0005);
    auto const restarted = clock.Update(400, observed + 1.0);
    CHECK(restarted && std::abs(*restarted - (observed + 1.0)) < 1e-9);
}

void TestThrowGrip() {
    namespace thr = mevr::ik::throwing;
    thr::GripLatch latch;
    CHECK(!thr::UpdateGrip(latch, 0.5f, 0.0));
    CHECK(thr::UpdateGrip(latch, 0.65f, 0.0));
    CHECK(thr::UpdateGrip(latch, 1.0f, 0.0));
    // Opening a little while slow keeps it; the same while fast lets go early (0.3 below the peak).
    CHECK(thr::UpdateGrip(latch, 0.75f, 0.5));
    CHECK(!thr::UpdateGrip(latch, 0.69f, 3.0));
    // At the latch's own "off" it lets go at any speed; early release off (0) waits for it.
    thr::GripLatch slow;
    thr::GripParams params;
    params.earlyDrop = 0.0f;
    thr::UpdateGrip(slow, 1.0f, 0.0, params);
    CHECK(thr::UpdateGrip(slow, 0.5f, 5.0, params));
    CHECK(!thr::UpdateGrip(slow, 0.4f, 0.0, params));
}

// The physical vent (core/ik/Vent, research/45): the left hand on the gun's receiver, a fast pull back, let go.
void TestVent() {
    namespace vnt = mevr::ik::vent;
    vnt::Zone const zone;
    vnt::Params params;
    // The zone: a box around the right hand in the gun's frame, with a shallow bottom (a pistol's cupped support hand is below).
    CHECK(vnt::InZone({0.05, 0.05, 0.02}, zone));
    CHECK(vnt::InZone({-0.05, 0.0, -0.02}, zone));
    CHECK(!vnt::InZone({0.20, 0.05, 0.0}, zone));    // the foregrip, ahead
    CHECK(!vnt::InZone({0.0, -0.08, 0.0}, zone));    // under the grip
    CHECK(!vnt::InZone({0.05, 0.05, 0.12}, zone));   // too far beside
    CHECK(!vnt::InZone({0.15, 0.0, 0.0}, zone) && vnt::InZone({0.15, 0.0, 0.0}, zone, 0.02));  // hysteresis
    CHECK(!vnt::InZone({std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, zone));
    // Refusals.
    CHECK(std::string(vnt::Refusal(0.1, 0.0, params)) == "the gun is cool");
    CHECK(std::string(vnt::Refusal(0.8, 1.0, params)) == "cooldown");
    CHECK(std::string(vnt::Refusal(-1.0, 0.0, params)) == "the heat is unknown");
    CHECK(vnt::Refusal(0.3, 0.0, params) == nullptr);

    double const dt = 1.0 / 90.0;
    double t = 0.0;
    vnt::Machine machine;
    auto step = [&](vnt::OnGun p, float squeeze, bool newSqueeze, double heat = 0.8, bool valid = true) {
        vnt::Input in;
        in.now = t;
        in.onGunValid = valid;
        in.onGun = p;
        in.newSqueeze = newSqueeze;
        in.squeeze = squeeze;
        in.heat = heat;
        vnt::Step const s = machine.Update(in, zone, params);
        t += dt;
        return s;
    };
    // The hand rests at `p` with the grip open (the test's hand teleports between places; a rest keeps that jump out of the
    // release window, and the machine clips the window to the hold anyway).
    auto settle = [&](vnt::OnGun p) {
        for (int i = 0; i < 14; ++i) {
            step(p, 0.0f, false);
        }
    };
    // The hand comes to the receiver (entered), a new squeeze there takes it, nothing else does.
    settle({0.30, 0.0, 0.0});
    CHECK(!machine.InZone());
    CHECK(step({0.30, 0.0, 0.0}, 1.0f, true).event == vnt::Event::None);  // a squeeze at the foregrip: not ours
    vnt::Step s = step({0.06, 0.05, 0.02}, 0.0f, false);
    CHECK(s.entered && machine.InZone() && !machine.Holding());
    settle({0.06, 0.05, 0.02});
    CHECK(step({0.06, 0.05, 0.02}, 1.0f, false).event == vnt::Event::None);  // an old squeeze moving in takes nothing
    s = step({0.06, 0.05, 0.02}, 1.0f, true);
    CHECK(s.event == vnt::Event::Grabbed && machine.Holding());
    // A rack: back along the gun at ~1.5 m/s for 80 ms (0.12 m), letting go. Vented, and the cooldown starts.
    vnt::OnGun p{0.06, 0.05, 0.02};
    for (int i = 0; i < 7; ++i) {
        p.along -= 1.5 * dt;
        p.up += 0.3 * dt;
        CHECK(step(p, 1.0f, false).event == vnt::Event::None);
    }
    p.along -= 1.5 * dt;
    s = step(p, 0.0f, false);
    CHECK(s.event == vnt::Event::Vented);
    CHECK(s.release.valid && s.release.speed > 1.2 && s.release.speed < 1.9);
    CHECK(s.back > 1.2 && s.travel > 0.10);
    CHECK(!machine.Holding() && machine.Vents() == 1 && machine.CooldownLeft(t) > 3.5);
    // Within the cooldown a new squeeze is refused.
    s = step({0.06, 0.05, 0.02}, 1.0f, true);
    CHECK(s.event == vnt::Event::Refused && std::string(s.why) == "cooldown");
    t += 5.0;
    step({0.06, 0.05, 0.02}, 0.0f, false);  // the long gap restarts the history
    // A cool gun: refused.
    s = step({0.06, 0.05, 0.02}, 1.0f, true, 0.1);
    CHECK(s.event == vnt::Event::Refused && std::string(s.why) == "the gun is cool");
    // A slow release: nothing.
    settle({0.06, 0.05, 0.02});
    CHECK(step({0.06, 0.05, 0.02}, 1.0f, true).event == vnt::Event::Grabbed);
    p = {0.06, 0.05, 0.02};
    for (int i = 0; i < 9; ++i) {
        p.along -= 0.2 * dt;
        step(p, 1.0f, false);
    }
    s = step(p, 0.0f, false);
    CHECK(s.event == vnt::Event::Slow && std::string(s.why) == "too slow" && machine.Vents() == 1);
    // A fast stroke FORWARD along the gun (toward the muzzle) is no rack.
    settle({0.0, 0.05, 0.02});
    CHECK(step({0.0, 0.05, 0.02}, 1.0f, true).event == vnt::Event::Grabbed);
    p = {0.0, 0.05, 0.02};
    for (int i = 0; i < 7; ++i) {
        p.along += 1.5 * dt;
        step(p, 1.0f, false);
    }
    s = step(p, 0.0f, false);
    CHECK(s.event == vnt::Event::Slow && std::string(s.why) == "not back along the gun");
    // Pulled straight away from the gun (up), no backward component: no rack either.
    settle({0.04, 0.05, 0.02});
    CHECK(step({0.04, 0.05, 0.02}, 1.0f, true).event == vnt::Event::Grabbed);
    p = {0.04, 0.05, 0.02};
    for (int i = 0; i < 7; ++i) {
        p.up += 1.5 * dt;
        step(p, 1.0f, false);
    }
    s = step(p, 0.0f, false);
    CHECK(s.event == vnt::Event::Slow && std::string(s.why) == "not back along the gun");
    // Held too long: lapsed, and the release afterwards does nothing; the next squeeze works again.
    for (int i = 0; i < 5; ++i) {
        step({0.06, 0.05, 0.02}, 0.0f, false);
    }
    CHECK(step({0.06, 0.05, 0.02}, 1.0f, true).event == vnt::Event::Grabbed);
    bool lapsed = false;
    for (int i = 0; i < 160 && !lapsed; ++i) {
        lapsed = step({0.06, 0.05, 0.02}, 1.0f, false).event == vnt::Event::Lapsed;
    }
    CHECK(lapsed && !machine.Holding());
    p = {0.06, 0.05, 0.02};
    for (int i = 0; i < 7; ++i) {
        p.along -= 1.5 * dt;
        step(p, 1.0f, false);
    }
    CHECK(step(p, 0.0f, false).event == vnt::Event::None && machine.Vents() == 1);
    // The gun's frame goes away while held: lost.
    for (int i = 0; i < 5; ++i) {
        step({0.06, 0.05, 0.02}, 0.0f, false);
    }
    CHECK(step({0.06, 0.05, 0.02}, 1.0f, true).event == vnt::Event::Grabbed);
    s = step({0.06, 0.05, 0.02}, 1.0f, false, 0.8, false);
    CHECK(s.event == vnt::Event::Lost && !machine.Holding() && !machine.InZone());
    // Haptics scale with the strength and clamp.
    CHECK(vnt::VentPulse(0.5).amplitude > 0.8f && vnt::VentPulse(0.0).amplitude == 0.0f && vnt::VentPulse(2.0).amplitude <= 1.0f);
    CHECK(vnt::EdgeTick(0.5).amplitude < vnt::VentPulse(0.5).amplitude);
}

// The visible rack (research/45 section 6): the hand's pull as the part's clamped offset, the spring forward, the part's choice.
void TestVentRack() {
    namespace vnt = mevr::ik::vent;
    double const dt = 1.0 / 90.0;
    // The machine's travel: how far the hand came back along the gun since it took the receiver.
    {
        vnt::Zone const zone;
        vnt::Params const params;
        vnt::Machine machine;
        double t = 0.0;
        auto step = [&](vnt::OnGun p, float squeeze, bool newSqueeze) {
            vnt::Input in;
            in.now = t;
            in.onGunValid = true;
            in.onGun = p;
            in.newSqueeze = newSqueeze;
            in.squeeze = squeeze;
            in.heat = 0.8;
            vnt::Step const s = machine.Update(in, zone, params);
            t += dt;
            return s;
        };
        for (int i = 0; i < 14; ++i) {
            step({0.06, 0.05, 0.0}, 0.0f, false);
        }
        CHECK(machine.Travel() == 0.0);
        CHECK(step({0.06, 0.05, 0.0}, 1.0f, true).event == vnt::Event::Grabbed);
        step({0.04, 0.05, 0.0}, 1.0f, false);
        CHECK(std::abs(machine.Travel() - 0.02) < 1e-9);
        step({0.08, 0.05, 0.0}, 1.0f, false);  // forward of the grab: negative (the part clamps it to its rest place)
        CHECK(machine.Travel() < 0.0);
    }
    // The pull as the part's offset: model units per meter, clamped to 0..its travel; nonsense is 0.
    CHECK(std::abs(vnt::RackTarget(0.02, 100.0, 3.0) - 2.0) < 1e-12);
    CHECK(vnt::RackTarget(0.10, 100.0, 3.0) == 3.0);
    CHECK(vnt::RackTarget(-0.05, 100.0, 3.0) == 0.0);
    CHECK(vnt::RackTarget(std::numeric_limits<double>::quiet_NaN(), 100.0, 3.0) == 0.0);
    CHECK(vnt::RackTarget(0.02, 0.0, 3.0) == 0.0 && vnt::RackTarget(0.02, 100.0, 0.0) == 0.0);

    // The part follows while held, never past its travel; let go, it snaps home fast and stops there (no swing past it).
    vnt::RackParams const p;  // 3 units, 9 Hz, damping 0.5
    vnt::Rack rack;
    CHECK(rack.AtRest() && rack.Update(dt, false, 0.0, p) == vnt::RackEvent::None);
    for (int i = 0; i <= 8; ++i) {
        CHECK(rack.Update(dt, true, 0.5 * i, p) == vnt::RackEvent::None);
    }
    CHECK(rack.Holding() && rack.Offset() == 3.0 && rack.Peak() == 3.0);
    rack.Update(dt, true, 1.5, p);  // the hand eases forward: so does the part
    CHECK(rack.Offset() == 1.5 && rack.Peak() == 3.0);
    rack.Update(dt, true, 3.0, p);
    CHECK(rack.Update(dt, false, 0.0, p) == vnt::RackEvent::Released);
    CHECK(!rack.Holding() && rack.Offset() < 3.0 && rack.Offset() > 0.0);
    double lowest = rack.Offset();
    bool home = false;
    int frames = 1;
    for (; frames < 90 && !home; ++frames) {
        vnt::RackEvent const e = rack.Update(dt, false, 0.0, p);
        CHECK(rack.Offset() >= 0.0 && rack.Offset() <= lowest + 1e-9);  // only forward: no bounce back, never past the rest place
        lowest = rack.Offset();
        home = e == vnt::RackEvent::Home;
    }
    CHECK(home && rack.AtRest() && rack.Offset() == 0.0 && rack.Velocity() == 0.0);
    CHECK(frames <= 8);  // a snap: home in a few frames at 90 Hz
    CHECK(rack.ReturnSeconds() > 0.01 && rack.ReturnSeconds() < 0.09);
    CHECK(rack.Update(dt, false, 0.0, p) == vnt::RackEvent::None);
    // Any frame time gives the same snap (the spring runs in 2 ms steps); a long frame is clamped.
    {
        vnt::Rack a;
        vnt::Rack b;
        a.Update(dt, true, 3.0, p);
        b.Update(dt, true, 3.0, p);
        a.Update(dt, false, 0.0, p);
        b.Update(dt, false, 0.0, p);
        a.Update(1.0 / 45.0, false, 0.0, p);
        b.Update(1.0 / 90.0, false, 0.0, p);
        b.Update(1.0 / 90.0, false, 0.0, p);
        CHECK(a.Offset() > 0.0 && std::abs(a.Offset() - b.Offset()) < 0.02);  // mid-flight, the same place
        vnt::Rack c;
        c.Update(dt, true, 2.0, p);
        c.Update(dt, false, 0.0, p);
        CHECK(c.Update(5.0, false, 0.0, p) == vnt::RackEvent::Home && c.AtRest());  // 5 s is 0.1 s: home well within it
    }
    // Let go at the rest place: released, nothing to return. A hold that never pulled.
    {
        vnt::Rack r;
        r.Update(dt, true, 0.0, p);
        CHECK(r.Update(dt, false, 0.0, p) == vnt::RackEvent::Released && r.AtRest() && r.Peak() == 0.0);
    }
    // A stiffer spring returns sooner; the travel setting clamps the follow.
    {
        vnt::RackParams soft = p;
        soft.returnHz = 4.0;
        vnt::RackParams tight = p;
        tight.maxUnits = 1.0;
        vnt::Rack r;
        r.Update(dt, true, 3.0, tight);
        CHECK(r.Offset() == 1.0);
        vnt::Rack s1;
        vnt::Rack s2;
        s1.Update(dt, true, 3.0, p);
        s2.Update(dt, true, 3.0, soft);
        s1.Update(dt, false, 0.0, p);
        s2.Update(dt, false, 0.0, soft);
        for (int i = 0; i < 30; ++i) {
            s1.Update(dt, false, 0.0, p);
            s2.Update(dt, false, 0.0, soft);
        }
        CHECK(s1.ReturnSeconds() < s2.ReturnSeconds());
    }

    // The part's choice. A pistol like the 2026-10-25 one (research/21 section 13): Root with the sockets, the upper barrel's
    // front with a child, the guard and the grip.
    std::vector<std::string> const pistol{"Root", "Barrel_Upper_front", "Barrel_Upper_tip", "Barrel_Lower_front", "Guard", "handle"};
    std::vector<int> const pistolParents{-1, 0, 1, 0, 0, 0};
    std::vector<int> const sockets{0, 0, 0};  // Flash_1, Tracer, Cooldown: all on Root
    CHECK(vnt::Subtree(pistolParents, 1) == std::vector<int>({1, 2}));
    CHECK(vnt::Subtree(pistolParents, 0).size() == pistol.size());
    vnt::PartChoice c = vnt::ChoosePart(pistol, pistolParents, sockets, "auto", 0);
    CHECK(c.bone == 1 && c.bones == std::vector<int>({1, 2}) && std::string(c.source) == "the type's list" && c.maxUnits == 3.0);
    // The setting names one (case-insensitive; the first the model has wins); none; a name the model lacks; the root refused.
    c = vnt::ChoosePart(pistol, pistolParents, sockets, "Slide, barrel_lower_front", 0);
    CHECK(c.bone == 3 && std::string(c.source) == "the setting");
    CHECK(vnt::ChoosePart(pistol, pistolParents, sockets, "none", 0).bone == -1);
    c = vnt::ChoosePart(pistol, pistolParents, sockets, "Slide", 0);
    CHECK(c.bone == -1 && c.why.find("none of") != std::string::npos);
    c = vnt::ChoosePart(pistol, pistolParents, sockets, "Root", 0);
    CHECK(c.bone == -1 && c.why.find("root") != std::string::npos);
    // A socket on the part (or below it) protects it: the muzzle must not move.
    std::vector<int> const muzzleOnTip{0, 2};
    c = vnt::ChoosePart(pistol, pistolParents, muzzleOnTip, "auto", 0);
    CHECK(c.bone == -1 && c.why.find("socket") != std::string::npos);
    // ASLa (research/21 section 16): its sight housing over the grip racks; the sniper's scope parts never do, and a model
    // without a part by name stands down with why.
    std::vector<std::string> const rifle{"Root", "handle", "Trigger_Guard", "squash_left", "squash_right", "Scope"};
    std::vector<int> const rifleParents{-1, 0, 0, 0, 0, 0};
    c = vnt::ChoosePart(rifle, rifleParents, sockets, "auto", 2);
    CHECK(c.bone == 5 && c.maxUnits == 3.0);
    std::vector<std::string> const sniper{"Root", "handle", "Scope_Eye", "Scope_Lens"};
    std::vector<int> const sniperParents{-1, 0, 0, 0};
    c = vnt::ChoosePart(sniper, sniperParents, sockets, "auto", 3);
    CHECK(c.bone == -1 && !c.why.empty());
    // ...unless a bone is named for a moving part (and not for a sight or a grip): auto takes it.
    std::vector<std::string> const bolted{"Root", "handle", "Scope_Eye", "Bolt_Handle", "HeatSink_Cover"};
    std::vector<int> const boltedParents{-1, 0, 0, 0, 0};
    c = vnt::ChoosePart(bolted, boltedParents, sockets, "auto", 3);
    CHECK(c.bone == 4 && std::string(c.source) == "its name" && c.maxUnits == 4.0);
    // The setting may still name a sight's bone on purpose.
    CHECK(vnt::ChoosePart(sniper, sniperParents, sockets, "Scope_Eye", 3).bone == 2);
    // Too many bones below one: refused.
    std::vector<std::string> many{"Root", "Part"};
    std::vector<int> manyParents{-1, 0};
    for (int i = 0; i < vnt::kMaxPartBones; ++i) {
        many.push_back("Child" + std::to_string(i));
        manyParents.push_back(1);
    }
    CHECK(vnt::ChoosePart(many, manyParents, sockets, "Part", 0).bone == -1);
    CHECK(vnt::ChoosePart({}, {}, {}, "auto", 0).bone == -1);
}

void TestGrenadeMachine() {
    namespace thr = mevr::ik::throwing;
    using mevr::ik::Vec3;
    double const dt = 1.0 / 90.0;
    thr::GrenadeMachine machine;
    thr::Input in;
    in.radius = 0.10;
    double t = 0.0;
    auto step = [&](Vec3 left, double leftDistance, float leftSqueeze, bool newLeft) {
        in.now = t;
        in.hands[0].tracked = true;
        in.hands[0].position = left;
        in.hands[0].distance = leftDistance;
        in.hands[0].squeeze = leftSqueeze;
        in.hands[0].newSqueeze = newLeft;
        in.hands[1].tracked = true;
        in.hands[1].position = {0.3, -0.3, -0.3};
        in.hands[1].distance = 0.5;
        thr::Step const s = machine.Update(in);
        t += dt;
        return s;
    };
    // A squeeze outside the slot takes nothing; entering is reported; a new squeeze inside takes one.
    CHECK(step({}, 0.20, 1.0f, true).event == thr::Event::None);
    thr::Step s = step({}, 0.05, 1.0f, false);
    CHECK(s.entered[0] == 1 && s.event == thr::Event::None && machine.InSlot(0));
    // A grip already held while the hand moves in never grabs (only a new squeeze does).
    CHECK(step({}, 0.05, 1.0f, false).event == thr::Event::None && machine.Holder() < 0);
    in.canGrab = false;
    s = step({}, 0.05, 1.0f, true);
    CHECK(s.event == thr::Event::Refused && s.hand == 0 && machine.Holder() < 0);
    in.canGrab = true;
    s = step({}, 0.05, 1.0f, true);
    CHECK(s.event == thr::Event::Grabbed && s.hand == 0 && machine.Holder() == 0);
    // The other hand can't take a second one meanwhile.
    in.now = t;
    in.hands[1].newSqueeze = true;
    in.hands[1].distance = 0.01;
    CHECK(machine.Update(in).event == thr::Event::None);
    in.hands[1].newSqueeze = false;
    t += dt;
    // Holding it out of the slot (exit hysteresis: 0.14 is still inside, 0.16 is out).
    CHECK(step({}, 0.14, 1.0f, false).left[0] == 0);
    CHECK(step({}, 0.16, 1.0f, false).left[0] == 1);
    // A fast throw forward and up, released: thrown, with the hand's velocity.
    Vec3 p{};
    for (int i = 0; i < 12; ++i) {
        p = p + Vec3{0.0, 0.03, -0.06};  // 2.7 m/s up, 5.4 m/s forward
        CHECK(step(p, 0.5, 1.0f, false).event == thr::Event::None);
    }
    p = p + Vec3{0.0, 0.03, -0.06};
    s = step(p, 0.5, 0.2f, false);
    CHECK(s.event == thr::Event::Thrown && s.hand == 0 && machine.Holder() < 0);
    CHECK(s.release.valid && std::abs(s.release.speed - std::sqrt(2.7 * 2.7 + 5.4 * 5.4)) < 0.1);
    CHECK(s.release.velocity.z < 0.0 && s.release.velocity.y > 0.0);

    // A slow release puts it back.
    step(p, 0.05, 1.0f, false);
    s = step(p, 0.05, 1.0f, true);
    CHECK(s.event == thr::Event::Grabbed);
    for (int i = 0; i < 10; ++i) {
        p = p + Vec3{0.0, 0.0, -0.005};  // 0.45 m/s
        step(p, 0.05, 1.0f, false);
    }
    s = step(p, 0.05, 0.1f, false);
    CHECK(s.event == thr::Event::Cancelled && s.release.speed < 1.0);

    // Losing tracking while holding puts it back (no throw).
    s = step(p, 0.05, 1.0f, true);
    CHECK(s.event == thr::Event::Grabbed);
    in.now = t;
    in.hands[0].tracked = false;
    s = machine.Update(in);
    CHECK(s.event == thr::Event::Dropped && machine.Holder() < 0);
    t += dt;
    // A hand not allowed to take grenades doesn't.
    in.hands[0].allowed = false;
    step(p, 0.05, 1.0f, false);
    s = step(p, 0.05, 1.0f, true);
    CHECK(s.event == thr::Event::None && machine.Holder() < 0);
}

void TestSwing() {
    namespace thr = mevr::ik::throwing;
    using mevr::ik::Vec3;
    double const dt = 1.0 / 90.0;
    thr::SwingParams params;  // 3 m/s, 0.12 m
    thr::SwingDetector swing;
    thr::SwingInput in;
    in.tracked = true;
    double t = 0.0;
    Vec3 p{0.2, -0.3, -0.2};
    auto run = [&](Vec3 velocity, int frames) {
        int strikes = 0;
        char const* refused = nullptr;
        for (int i = 0; i < frames; ++i) {
            p = p + velocity * dt;
            in.now = t;
            in.position = p;
            thr::SwingStep const s = swing.Update(in, params);
            strikes += s.strike ? 1 : 0;
            refused = s.refused != nullptr ? s.refused : refused;
            t += dt;
        }
        return std::make_pair(strikes, refused);
    };
    // Holding still, then aiming around slowly (1.5 m/s): nothing.
    CHECK(run({}, 20).first == 0);
    CHECK(run({1.5, 0.0, 0.0}, 20).first == 0);
    CHECK(run({}, 10).first == 0);
    // A punch forward at 4 m/s for 0.1 s (0.4 m): one strike, however long it lasts.
    auto const punch = run({0.0, 0.0, -4.0}, 9);
    CHECK(punch.first == 1);
    CHECK(run({0.0, 0.0, -4.0}, 5).first == 0);
    // Pulling back fast isn't a strike (backward); then the cooldown blocks the next punch.
    CHECK(run({}, 5).first == 0);
    auto const pull = run({0.0, 0.0, 4.0}, 9);
    CHECK(pull.first == 0 && pull.second != nullptr);
    CHECK(run({}, 5).first == 0);
    auto const early = run({0.0, 0.0, -4.0}, 9);
    CHECK(early.first == 0 && early.second != nullptr && std::string(early.second) == "cooldown");
    // After the cooldown a sideways swipe (a pistol whip across the body) strikes.
    CHECK(run({}, 72).first == 0);
    CHECK(run({-4.5, -1.0, -0.5}, 9).first == 1);
    // Fast jitter (back and forth every other frame) never travels far enough.
    CHECK(run({}, 80).first == 0);
    int jitter = 0;
    for (int i = 0; i < 40; ++i) {
        jitter += run({0.0, 0.0, (i % 2 == 0) ? -5.0 : 5.0}, 2).first;
    }
    CHECK(jitter == 0);
    // Suppressed (a grenade in the hand, firing...): refused with that reason.
    CHECK(run({}, 80).first == 0);
    in.suppressed = true;
    auto const held = run({0.0, 0.0, -4.0}, 9);
    CHECK(held.first == 0 && held.second != nullptr);
    in.suppressed = false;
    // A tracking glitch (40 m/s) is no stroke.
    CHECK(run({}, 10).first == 0);
    CHECK(run({0.0, 0.0, -40.0}, 3).first == 0);
}

void TestArcPoints() {
    namespace thr = mevr::ik::throwing;
    // Unreal axes: +Z up. 1000 units/s level, gravity -981: after 1 s, 1000 ahead and 490.5 down.
    auto const arc = thr::ArcPoints({0.0, 0.0, 100.0}, {1000.0, 0.0, 0.0}, -981.0, 0.05, 1.0, 1e9);
    CHECK(arc.size() == 21);
    CHECK(std::abs(arc.back().x - 1000.0) < 1e-6 && std::abs(arc.back().z - (100.0 - 490.5)) < 1e-6);
    // It stops once it has fallen maxDrop.
    auto const dropped = thr::ArcPoints({}, {0.0, 0.0, 0.0}, -1000.0, 0.1, 10.0, 500.0);
    CHECK(dropped.size() < 15 && dropped.back().z < -500.0);
    CHECK(thr::ArcPoints({}, {}, -1000.0, 0.0, 1.0, 1.0).empty());
}

// B and X kept from the game when the physical melee and throw replace them; an RB press stays RB.
void TestFaceButtonsOff() {
    using namespace mevr::game::gamepad;
    mevr::vr::controllers::State state;
    Latches latches;
    Options options;
    options.meleeButton = false;
    options.grenadeButton = false;
    state.right.secondary = true;  // B
    state.left.primary = true;     // X
    state.left.secondary = true;   // Y
    auto pad = FromControllers(state, latches, options);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_B) == 0 && (pad.wButtons & XINPUT_GAMEPAD_X) == 0);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_Y) != 0);
    // The flags round-trip, and 0 (an older mod) keeps both buttons.
    Options const back = FromFlags(-1.0f, 0.12f, 0.22f, ToFlags(options));
    CHECK(!back.meleeButton && !back.grenadeButton);
    Options const none = FromFlags(-1.0f, 0.12f, 0.22f, 0u);
    CHECK(none.meleeButton && none.grenadeButton);
    // B as the power wheel's RB source still sends RB.
    state = {};
    latches = {};
    options.rightGripHolds = true;
    options.rbSource = RbSource::B;
    state.right.secondary = true;
    pad = FromControllers(state, latches, options);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0 && (pad.wButtons & XINPUT_GAMEPAD_B) == 0);
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
    CHECK(gun && gun->zoom == 0.0f);  // the game's own magnification (research/21 section 16)
    gun = s::ParseGun("Scope Auto", s::Gun{s::Kind::Scope, 3.0f});
    CHECK(gun && gun->kind == s::Kind::Scope && gun->zoom == 0.0f);
    gun = s::ParseGun("scope", s::Gun{s::Kind::Scope, 0.0f});
    CHECK(gun && gun->zoom == 0.0f);
    CHECK(s::ParseGun("dot auto", none) == std::nullopt && s::ParseGun("scope x", none) == std::nullopt);
    gun = s::ParseGun("scope 40", none);
    CHECK(gun && gun->zoom == 12.0f);
    CHECK(s::ParseGun("dot 4", none) == std::nullopt);
    CHECK(s::ParseGun("laser", none) == std::nullopt);
    CHECK(s::ParseGun("", none) == std::nullopt);
    gun = s::ParseGun("none", s::Gun{s::Kind::Dot});
    CHECK(gun && gun->kind == s::Kind::None);
    // The assault rifle's low-power scope (research/21 section 14): decimals read. Its default is a red dot again (section
    // 15) that keeps the model's eyepiece radius for `scope 1.5`; the sniper's hides its glowing lens.
    gun = s::ParseGun("scope 1.5", s::Gun{s::Kind::Dot});
    CHECK(gun && gun->kind == s::Kind::Scope && gun->zoom == 1.5f);
    s::Settings const defaults;
    CHECK(defaults.guns[2].kind == s::Kind::Dot && defaults.guns[2].lens == 0.0f && defaults.guns[2].hide.empty());
    gun = s::ParseGun("scope 1.5", defaults.guns[2]);
    CHECK(gun && gun->kind == s::Kind::Scope && gun->zoom == 1.5f && gun->lens == 0.0f);
    CHECK(defaults.ads && defaults.adsReachM == 0.40f && defaults.scopeAdsPullMaxM == 0.35f && defaults.scopePullMaxM == 0.15f);
    // The one-time move back (section 15): exactly section 14's `scope 1.5` becomes dot; anything else is the player's.
    CHECK(s::MigrateAssaultRifle("scope 1.5") == std::optional<std::string>("dot"));
    CHECK(s::MigrateAssaultRifle(" Scope  1.50 ") == std::optional<std::string>("dot"));
    CHECK(!s::MigrateAssaultRifle("scope 2") && !s::MigrateAssaultRifle("scope") && !s::MigrateAssaultRifle("dot") &&
          !s::MigrateAssaultRifle("holo") && !s::MigrateAssaultRifle("none") && !s::MigrateAssaultRifle("scope 1.5 x") &&
          !s::MigrateAssaultRifle(""));
    // Since section 16 the sniper rifle's zoom, eyepieces and hidden part are the game's model's own ("auto"), its reticle the
    // game's; a model we don't know falls back to lensUnits and, on a sniper rifle, Scope_Lens.
    CHECK(defaults.guns[3].kind == s::Kind::Scope && defaults.guns[3].zoom == 0.0f && defaults.guns[3].hide == "auto");
    CHECK(defaults.scopeReticle == s::ScopeReticle::Game && defaults.zoomStick);
    CHECK(s::LensUnits(defaults, 2) == defaults.lensUnits && s::LensUnits(defaults, 3) == defaults.lensUnits &&
          s::LensUnits(defaults, 0) == defaults.lensUnits);
    gun = s::ParseGun("dot", defaults.guns[3]);  // a parsed kind keeps the gun's lens and hide list
    CHECK(gun && gun->kind == s::Kind::Dot && gun->hide == "auto" && gun->lens == 0.0f);
    CHECK(s::ParseScopeReticle("Game") == s::ScopeReticle::Game && s::ParseScopeReticle("duplex") == s::ScopeReticle::Duplex);
    CHECK(s::ParseBoneList(" Scope_Lens, Scope_Glow;scope_lens ") == std::vector<std::string>({"Scope_Lens", "Scope_Glow"}));
    CHECK(s::ParseBoneList("none").empty() && s::ParseBoneList("").empty());
    CHECK(s::IsEyepieceName("Scope_Eye") && s::IsEyepieceName("rear_sight") && !s::IsEyepieceName("Scope") &&
          !s::IsEyepieceName("Scope_Lens") && !s::IsEyepieceName("socket Scope_Eye") && !s::IsEyepieceName("Eye_L"));
    CHECK(s::HousingToEyepiece(2) > 0.0f && s::HousingToEyepiece(-1) == 0.0f);
    // The parts to hide: found by name (case aside), never the root, the sight's bone or what it hangs on.
    std::vector<std::string> const bones{"Root", "Body", "Scope_Lens", "Scope_Eye", "Scope", "socket Flash_1"};
    std::vector<int> const parents{-1, 0, 1, 1, 1, -1};
    std::vector<std::string> const wanted{"scope_lens", "Body", "Root", "Missing", "Scope_Eye", "socket Flash_1"};
    s::HideChoice const choice = s::PartsToHide(bones, parents, std::size_t{3}, wanted);
    CHECK(choice.bones == std::vector<std::size_t>({2}));
    CHECK(choice.skipped.size() == 5 && choice.skipped[0] == "Body (the sight hangs on it)" && choice.skipped[1] == "Root (the model's root)" &&
          choice.skipped[2] == "Missing (not on this model)" && choice.skipped[3] == "Scope_Eye (the sight's own bone)" &&
          choice.skipped[4] == "socket Flash_1 (not on this model)");
    CHECK(s::PartsToHide(bones, parents, std::nullopt, std::vector<std::string>{"Body", "Scope"}).bones == std::vector<std::size_t>({1, 4}));

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

// The game's own optics (research/21 section 16): the rifles' measured eyepieces, the hidden parts, the magnifications from
// the weapons' AimModes, the stick's flick between them.
void TestSightGameOptics() {
    namespace s = mevr::vr::sights;
    auto const snpa = s::OpticFor("wpn_snpa_mdl");  // the mesh's name, case aside
    CHECK(snpa && std::string(snpa->bone) == "Scope_Eye" && snpa->back > 13.0f && snpa->back < 14.0f && snpa->up < 0.0f &&
          snpa->lens > 2.0f && snpa->lens < 2.2f);
    auto const snpb = s::OpticFor("WPN_SNPb_MDL_1a");
    CHECK(snpb && std::string(snpb->bone) == "Scope_Rear" && snpb->lens == 1.7f && std::string(snpb->hide).empty());
    CHECK(s::OpticFor("WPN_ASLa_MDL") && s::OpticFor("WPN_ASLb_MDL") && !s::OpticFor("WPN_PSTa_MDL") && !s::OpticFor("") &&
          !s::OpticFor("WPN_SNPa_MDL_x"));
    // The section-14 eyepiece on SNPa: 59.2 units behind the muzzle at the bone; the glass 72.7, 6 behind the grip (66.6).
    CHECK(NearD(59.2 + snpa->back, 72.7, 0.1));
    // The parts to hide: the model's own for "auto", Scope_Lens on a sniper we don't know, the list as written otherwise.
    CHECK(s::HideList("auto", snpa, 3) == std::vector<std::string>({"Scope_Lens"}));
    CHECK(s::HideList("Auto", snpb, 3).empty());
    CHECK(s::HideList("auto", std::nullopt, 3) == std::vector<std::string>({"Scope_Lens"}) && s::HideList("auto", std::nullopt, 2).empty());
    CHECK(s::HideList("Scope_Mid, Scope_Forward", snpb, 3) == std::vector<std::string>({"Scope_Mid", "Scope_Forward"}));
    CHECK(s::HideList("none", snpa, 3).empty());

    // The magnifications: tan(35) / tan(ZoomFOV / 2) against the 70-degree combat camera.
    CHECK(NearD(s::ZoomFromFov(22.6f), 3.504, 0.005) && NearD(s::ZoomFromFov(11.4f), 7.015, 0.01));
    CHECK(NearD(s::ZoomFromFov(23.9f), 3.308, 0.005) && NearD(s::ZoomFromFov(12.1f), 6.606, 0.01));
    CHECK(NearD(s::ZoomFromFov(39.6f), 1.945, 0.005) && s::ZoomFromFov(70.0f) == 1.0f);
    CHECK(s::ZoomFromFov(0.0f) == 0.0f && s::ZoomFromFov(90.0f) == 0.0f && s::ZoomFromFov(std::nanf("")) == 0.0f &&
          s::ZoomFromFov(0.5f) == 0.0f);
    s::GameZoom sniper;
    s::AddAimMode(sniper, 22.6f, true);
    s::AddAimMode(sniper, 11.4f, true);
    CHECK(sniper.count == 2 && sniper.unscoped == 0 && NearD(s::ZoomAt(sniper, 0), 3.504, 0.005) && NearD(s::ZoomAt(sniper, 1), 7.015, 0.01));
    CHECK(s::ZoomAt(sniper, 5) == s::ZoomAt(sniper, 1) && s::ZoomAt(sniper, -1) == s::ZoomAt(sniper, 0));
    s::GameZoom rifle;
    s::AddAimMode(rifle, 39.6f, false);
    s::AddAimMode(rifle, 0.0f, true);  // implausible: skipped
    CHECK(rifle.count == 0 && rifle.unscoped == 1 && s::ZoomAt(rifle, 0) == 0.0f);
    s::GameZoom many;
    for (int i = 0; i < 6; ++i) {
        s::AddAimMode(many, 20.0f, true);
    }
    CHECK(many.count == 4);

    // The flick: one step per push past 0.7, nothing again until the stick is back inside 0.3.
    s::ZoomFlick flick;
    CHECK(flick.Update(0.5f) == 0 && flick.Update(0.8f) == 1 && flick.Update(0.95f) == 0 && flick.Update(0.5f) == 0);
    CHECK(flick.Update(0.2f) == 0 && flick.Update(-0.9f) == -1 && flick.Update(-1.0f) == 0 && flick.Update(0.0f) == 0);
    CHECK(flick.Update(0.75f) == 1 && flick.Update(std::nanf("")) == 0);
    flick.Reset();
    CHECK(flick.Update(-0.8f) == -1);
}

// The game's scope view (research/21 section 16, vr::scope_art): the layout's maps, the layers' framing.
void TestScopeArt() {
    namespace art = mevr::vr::scope_art;
    CHECK(art::Pieces().size() == 11);
    int reticle = 0;
    int tube = 0;
    for (art::Piece const& p : art::Pieces()) {
        (p.layer == art::Layer::Reticle ? reticle : tube) += 1;
        CHECK((p.layer == art::Layer::Reticle) == (p.texture == art::Texture::I4E));
        // Each piece's map round trip: a texel inside its clip -> the movie's pixels -> the layer's uv -> the texture's uv.
        art::Map m{};
        CHECK(art::LayerToTexture(p, m));
        art::TextureInfo const& info = art::Info(p.texture);
        float const tu = 0.5f * (p.clip[0] + p.clip[2]);
        float const tv = 0.5f * (p.clip[1] + p.clip[3]);
        float const x = p.map[0] * tu + p.map[2] * tv + p.map[4];
        float const y = p.map[1] * tu + p.map[3] * tv + p.map[5];
        art::Point const o = art::Origin(p.layer);
        float const U = 0.5f + (x - o.x) / art::kLayerSpan;
        float const V = 0.5f + (y - o.y) / art::kLayerSpan;
        CHECK(NearD(m.u[0] * U + m.u[1] * V + m.u[2], tu / static_cast<float>(info.width), 1e-4));
        CHECK(NearD(m.v[0] * U + m.v[1] * V + m.v[2], tv / static_cast<float>(info.height), 1e-4));
        CHECK(m.clip[0] >= 0.0f && m.clip[2] <= 1.0f && m.clip[0] < m.clip[2] && m.clip[1] < m.clip[3]);
    }
    CHECK(reticle == 9 && tube == 2 && reticle <= art::kMaxPiecesPerLayer && tube <= art::kMaxPiecesPerLayer);
    // The mirrored stems: one map is the other reflected about the reticle's axis (x = 0 in the movie).
    auto const pieces = art::Pieces();
    CHECK(pieces[3].map[0] == -pieces[4].map[0] && pieces[3].map[4] == -pieces[4].map[4]);
    // A picture point: the origin in the middle of the layer, its edge (r = 1) on the texture's edge, y up.
    art::Point const c = art::PictureToLayer(0.0f, 0.0f);
    art::Point const edge = art::PictureToLayer(1.0f, 1.0f);
    CHECK(NearD(c.x, 0.5, 1e-6) && NearD(c.y, 0.5, 1e-6) && NearD(edge.x, 1.0, 1e-5) && NearD(edge.y, 0.0, 1e-5));
    // The field fills 0.89 of the picture: the game's clear disc (223 movie pixels) inside the 251-pixel picture radius.
    CHECK(NearD(art::kFieldRadius / art::kPictureRadius, art::kFieldShare, 1e-5) && art::kPictureRadius > 240.0f);
    CHECK(std::string(art::Info(art::Texture::I41).name) == "Reticule_I41" && art::Info(9).width == art::Info(0).width);
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

// Shots that converge on the sight line, the red-dot defaults, the sight on the model
// (core/vr/Sights, research/21 sections 10-11).
void TestSightShots() {
    namespace s = mevr::vr::sights;
    constexpr double kDeg = std::numbers::pi / 180.0;
    // The defaults: a red dot on the pistol, the shotgun and the assault rifle (again since section 15), a scope on the
    // sniper rifle, an eye box twice the window.
    s::Settings const defaults;
    CHECK(defaults.guns[0].kind == s::Kind::Dot && defaults.guns[1].kind == s::Kind::Dot && defaults.guns[2].kind == s::Kind::Dot &&
          defaults.guns[3].kind == s::Kind::Scope);
    CHECK(defaults.eyeBoxUnits > defaults.windowUnits && defaults.converge);

    // Shots converge on what the sight line hits: the sight 30 units behind the muzzle and 6 above, its line along
    // the barrel; a wall 1000 units past the sight: the shot climbs 6 units over 970.
    XrVector3f const sight{0.0f, 6.0f, 30.0f};
    XrVector3f const axis{0.0f, 0.0f, -1.0f};
    XrVector3f const shot = s::ConvergedShot(sight, axis, 1000.0f);
    CHECK(NearD(shot.y / -shot.z, 6.0 / 970.0, 1e-5) && NearD(shot.x, 0.0, 1e-6));
    CHECK(NearD(s::AngleDeg(shot, axis), std::atan(6.0 / 970.0) / kDeg, 1e-3));
    // It reaches the sight line's point there.
    double const t = 970.0 / -shot.z;
    CHECK(NearD(shot.y * t, 6.0, 1e-3));
    // Nothing hit (the trace's end, 300 m): nearly parallel. No distance: along the line.
    CHECK(s::AngleDeg(s::ConvergedShot(sight, axis, 30000.0f), axis) < 0.012f);
    CHECK(NearXr(s::ConvergedShot(sight, axis, 0.0f), axis) && NearXr(s::ConvergedShot(sight, {0.0f, 0.0f, -3.0f}, -1.0f), axis));
    // A wall right at the sight (20 units past it, 10 behind the muzzle), or closer than 20 units past the muzzle:
    // parallel, not bent backward or steeply.
    CHECK(NearXr(s::ConvergedShot(sight, axis, 20.0f, 20.0f), axis) && NearXr(s::ConvergedShot(sight, axis, 45.0f, 20.0f), axis));
    CHECK(!NearXr(s::ConvergedShot(sight, axis, 60.0f, 20.0f), axis));
    CHECK(NearD(s::AngleDeg({1.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}), 90.0, 1e-4) && s::AngleDeg({}, axis) == 180.0f);

    // The sight on the model: a scope's eyepiece a lens radius under the model's top, a dot's window just above it.
    CHECK(s::HeightOnModel(s::Kind::Scope, 9.0f, 2.0f) == 7.0f);
    CHECK(s::HeightOnModel(s::Kind::Dot, 4.0f, 3.0f) == 5.0f);
    CHECK(s::HeightOnModel(s::Kind::Scope, 1.5f, 2.0f) == 1.0f);  // never below the bore
    CHECK(!s::HeightOnModel(s::Kind::Dot, 0.2f, 3.0f) && !s::HeightOnModel(s::Kind::Dot, 45.0f, 3.0f) &&
        !s::HeightOnModel(s::Kind::None, 5.0f, 3.0f));

    // research/21 section 13: the sniper rifle's bones (log 2026-10-25 18:27:56): the scope's picture goes on the eyepiece
    // bone, not the front lens; a dot takes the first sight-named bone; sockets never; nothing named: none.
    std::vector<std::string> const sniper{"Root", "Top_Gun", "handle", "Scope_Lens", "Scope_Eye", "Butt", "socket Flash_1"};
    CHECK(s::SightBone(s::Kind::Scope, sniper) == std::optional<std::size_t>(4));
    CHECK(s::SightBone(s::Kind::Dot, sniper) == std::optional<std::size_t>(3));
    std::vector<std::string> const rifle{"Root", "Butt", "handle", "Scope", "Upper_Rear"};
    CHECK(s::SightBone(s::Kind::Dot, rifle) == std::optional<std::size_t>(3) && s::SightBone(s::Kind::Scope, rifle) == std::optional<std::size_t>(3));
    std::vector<std::string> const pistol{"Root", "Guard", "Barrel_updown", "socket Sight_Flash"};
    CHECK(!s::SightBone(s::Kind::Dot, pistol));
    // The model's top from its bones (the pistol's, 2026-10-25: the bounds said 15.1): squash bones far to the side and
    // points behind the gun's back end don't count.
    std::vector<s::ModelPoint> const pistolBones{{27.2f, 0.9f, 0.5f}, {17.7f, -5.4f, -3.2f}, {18.3f, 2.7f, 1.6f}, {34.3f, 2.2f, 1.3f},
        {27.1f, 9.5f, -17.8f}, {200.0f, 12.0f, 0.0f}};
    CHECK(NearD(s::ModelTop(pistolBones, 27.2f).value_or(0.0f), 2.7, 1e-6));
    CHECK(!s::ModelTop(std::vector<s::ModelPoint>{{10.0f, 0.2f, 0.0f}, {5.0f, -3.0f, 0.0f}}, 30.0f));
    // A place with a side, and a set place with three numbers.
    XrVector3f const placed = s::Place({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 0.4f, 0.05f, 0.02f);
    CHECK(NearXr(placed, {0.02f, 0.05f, 0.4f}));
    s::Gun place3;
    CHECK(s::ParsePlace("59, 7, 1.5", place3) && !place3.autoPlace && place3.side == 1.5f && place3.height == 7.0f);
    CHECK(s::ParsePlace("30, 6", place3) && place3.side == 0.0f);
}

// The scope's virtual eyepiece (research/21 section 13): the picture keeps its apparent size from 25 to 60 cm behind the
// eyepiece, the magnification stays true across it, the shadow comes in with the eye off the line, and it fades past
// its range.
void TestSightVirtualEyepiece() {
    namespace s = mevr::vr::sights;
    auto const lens = s::MakeLens({0.0f, 0.0f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 0.02f);
    CHECK(lens.has_value());
    if (!lens) {
        return;
    }
    s::Eyepiece const ep{22.0f, 0.05f, 0.70f};
    for (float along : {0.25f, 0.45f, 0.60f}) {
        s::Lens picture{};
        auto const view = s::VirtualView(*lens, {0.0f, 0.0f, -0.5f + along}, ep, picture);
        CHECK(view.has_value());
        if (!view) {
            continue;
        }
        CHECK(NearD(view->along, along, 1e-5));
        // 22 degrees across from the eye.
        CHECK(NearD(2.0 * std::atan(picture.radius / view->along) * 180.0 / 3.14159265, 22.0, 1e-3));
        CHECK(view->fade == 1.0f && view->px == 0.0f && view->py == 0.0f && view->pupil == 1.0f);
        // The picture's edge shows the direction 11 / zoom degrees off the axis (true across the picture, x 4).
        XrVector3f const edge = s::LensDirection(picture, *view, 1.0f, 0.0f, 4.0f);
        double const shown = std::atan2(edge.x, -edge.z) * 180.0 / 3.14159265;
        CHECK(NearD(shown, std::atan(std::tan(11.0 * 3.14159265 / 180.0) / 4.0) * 180.0 / 3.14159265, 1e-3));
    }
    // The physical lens 45 cm back: the old 2 cm disc (5 degrees), shrunk to a quarter by the pupil.
    auto const physical = s::View(*lens, {0.0f, 0.0f, -0.05f}, 0.12f);
    CHECK(physical && NearD(physical->pupil, 0.25, 1e-5) && physical->px == physical->qx);
    // Never smaller than the lens itself (an eye right at it).
    CHECK(s::PictureRadius(0.02f, 0.03f, 22.0f) == 0.02f && s::PictureRadius(0.02f, 0.5f, 0.0f) == 0.02f);
    // The eye 2.5 cm right of the line: the pupil's centre half a picture radius right (a crescent of shadow); the
    // reticle (q) where the eye's line meets the lens.
    s::Lens picture{};
    auto const off = s::VirtualView(*lens, {0.025f, 0.0f, -0.05f}, ep, picture);
    CHECK(off && NearD(off->px, 0.5, 1e-4) && NearD(off->qx, 0.025 / picture.radius, 1e-4));
    // Two eye boxes off, or too far back: no picture. Past the range it fades.
    CHECK(!s::VirtualView(*lens, {0.11f, 0.0f, -0.05f}, ep, picture));
    CHECK(!s::VirtualView(*lens, {0.0f, 0.0f, -0.5f + 0.9f}, ep, picture));
    auto const distant = s::VirtualView(*lens, {0.0f, 0.0f, -0.5f + 0.7875f}, ep, picture);
    CHECK(distant && distant->fade > 0.4f && distant->fade < 0.6f);
    CHECK(!s::VirtualView(*lens, {0.0f, 0.0f, -0.48f}, ep, picture));  // closer than 3 cm

    // Pulled to the eye (research/21 section 14): the field is what the 2 cm eyepiece subtends from 0.12 m (18.9 degrees),
    // so with the eye at the relief the picture is the eyepiece itself; further back (the pull at its limit) it keeps that
    // angle, never smaller; nearer, the eyepiece.
    float const pulledDeg = s::PulledFieldDeg(0.02f, 0.12f);
    CHECK(NearD(pulledDeg, 2.0 * std::atan(0.02 / 0.12) * 180.0 / 3.14159265, 1e-3) && s::PulledFieldDeg(0.0f, 0.12f) == 0.0f);
    s::Eyepiece const pulled{pulledDeg, 0.05f, 0.70f};
    auto const atRelief = s::VirtualView(*lens, {0.0f, 0.0f, -0.5f + 0.12f}, pulled, picture);
    CHECK(atRelief && NearD(picture.radius, 0.02, 1e-5));
    auto const fartherBack = s::VirtualView(*lens, {0.0f, 0.0f, -0.5f + 0.24f}, pulled, picture);
    CHECK(fartherBack &&NearD(picture.radius, 0.04, 1e-5) && NearD(2.0 * std::atan(picture.radius / 0.24) * 180.0 / 3.14159265, pulledDeg, 1e-3));
    auto const nearer = s::VirtualView(*lens, {0.0f, 0.0f, -0.5f + 0.08f}, pulled, picture);
    CHECK(nearer && NearD(picture.radius, 0.02, 1e-6));
}

// The scope's real zoomed render (research/39): the capture's field, its camera, the pose history, and the picture
// sampling it through the same projection the eye copy uses.
// The spectator camera (core/vr/Spectator, research/49): its field, the head camera's damped follow, the publish history.
// The developer request files' switch and the diagnostics' disk budget (docs/release-checklist.md section 1).
void TestDevChannelsAndDiskBudget() {
    using mevr::dev_channels::Resolve;
    CHECK(Resolve(-1, true) && !Resolve(-1, false));   // no key: the build flavour decides
    CHECK(Resolve(1, false) && !Resolve(0, true));     // the key wins either way
    CHECK(Resolve(2, false));                          // any non-zero is on

    using mevr::disk_budget::Entry;
    using mevr::disk_budget::PlanPrune;
    std::vector<Entry> const files{{400, 5000}, {300, 9000}, {200, 100}, {100, 7000}};  // bytes, age in seconds
    CHECK(PlanPrune(files, 0, 600).empty());      // no limit
    CHECK(PlanPrune(files, 1000, 600).empty());   // fits exactly
    {
        auto const out = PlanPrune(files, 600, 600);  // 1000 bytes: the oldest go first (9000 s, then 7000 s)
        CHECK(out.size() == 2 && out[0] == 1 && out[1] == 3);
    }
    {
        auto const out = PlanPrune(files, 150, 600);  // the 100-s-old file is never removed, even over budget
        CHECK(out.size() == 3 && out[0] == 1 && out[1] == 3 && out[2] == 0);
    }
    CHECK(PlanPrune(files, 150, 100000).empty());  // nothing old enough
}

void TestCineScreen() {
    namespace cs = mevr::vr::cine_screen;
    // The crop: the two shown parts are c apart and (1 - c) wide, so infinity's disparity is depth of the shown width.
    float const c = cs::CropFraction(0.015f);
    CHECK(NearD(c / (1.0f - c), 0.015, 1e-6) && cs::CropFraction(0.0f) == 0.0f && cs::CropFraction(-1.0f) == 0.0f);
    // The rects: the left eye drops its left edge, the right eye its right edge, both the same size.
    auto const r = cs::Rects(2344, 2392, c);
    int const drop = static_cast<int>(std::lround(c * 2344.0f));
    CHECK(r.left.x == drop && r.left.width == 2344 - drop && r.right.x == 0 && r.right.width == r.left.width && r.left.height == 2392 &&
          r.right.height == 2392 && r.left.y == 0);
    CHECK(cs::Rects(0, 100, c).left.width == 0 && cs::Rects(100, 100, 2.0f).left.x == 25);   // the crop is limited to a quarter
    CHECK(NearD(cs::HeightM(r, 3.0f), 3.0 * 2392.0 / (2344.0 - drop), 1e-5));
    // The geometry, end to end: two parallel cameras b apart, a render half angle h; a point straight ahead at distance Z
    // projects at +-(b/2) f / Z of the image's width from the centre (f = 1 / (2 tan h)); after the crop each eye's shown part
    // maps onto the same screen. The convergence distance lands on the screen plane, infinity `depth` behind it.
    float const halfH = 0.6f;
    float const convergeUU = 250.0f;
    float const b = cs::BaselineUU(c, convergeUU, halfH);
    double const f = 1.0 / (2.0 * std::tan(static_cast<double>(halfH)));
    auto screenDisparity = [&](double z) {   // right eye's screen position minus the left eye's, a fraction of the screen
        double const xl = 0.5 + (b / 2.0) * f / z;   // the left camera sits left: the point is right of its centre
        double const xr = 0.5 - (b / 2.0) * f / z;
        double const ul = (xl - c) / (1.0 - c);       // the left eye shows [c, 1]
        double const ur = xr / (1.0 - c);             // the right eye shows [0, 1 - c]
        return ur - ul;
    };
    CHECK(NearD(screenDisparity(convergeUU), 0.0, 1e-6));
    CHECK(NearD(screenDisparity(1e12), 0.015, 1e-5));                                // infinity: depth, uncrossed (behind)
    CHECK(screenDisparity(convergeUU / 2.0) < 0.0 && screenDisparity(convergeUU * 2.0) > 0.0);   // nearer: in front
    // A close-up's narrower lens gets a narrower baseline for the same depth budget.
    CHECK(cs::BaselineUU(c, convergeUU, 0.2f) < b && cs::BaselineUU(0.0f, convergeUU, halfH) == 0.0f &&
          cs::BaselineUU(c, convergeUU, 1.6f) == 0.0f && cs::BaselineUU(c, -1.0f, halfH) == 0.0f);
    // The divergence cap: infinity's disparity on a 3 m screen stays under 0.8 of a 64 mm eye distance.
    CHECK(NearD(cs::MaxDepth(3.0f, 0.064f), 0.8 * 0.064 / 3.0, 1e-6) && NearD(cs::MaxDepth(3.0f, 0.0f), 0.8 * 0.064 / 3.0, 1e-6) &&
          cs::MaxDepth(0.5f, 0.064f) == cs::kMaxDepth && cs::MaxDepth(0.0f, 0.064f) == 0.0f);
    cs::Settings wide;
    wide.widthM = 8.0f;
    wide.depth = 0.03f;
    auto const capped = cs::Plan(wide, 0.064f, 100.0f, halfH);
    CHECK(capped.depthCapped && NearD(capped.depth, 0.8 * 0.064 / 8.0, 1e-6) && NearD(capped.depth * 8.0, 0.8 * 0.064, 1e-6));
    auto const plain = cs::Plan(cs::Settings{}, 0.064f, 100.0f, halfH);
    CHECK(!plain.depthCapped && NearD(plain.depth, 0.015, 1e-6) &&
          NearD(plain.baselineUU, cs::BaselineUU(plain.crop, cs::Settings{}.convergeM * cs::kSceneUnitsPerMeter, halfH), 1e-4));
    CHECK(cs::Settings{}.conversations && cs::Settings{}.cutscenes);   // on by default since the in-game check (research/52)
    CHECK(cs::Plan(cs::Settings{}, 0.064f, 0.0f, halfH).baselineUU == 0.0f);   // no scale yet: no separation
    // Clamping.
    cs::Settings odd;
    odd.distanceM = 100.0f;
    odd.widthM = std::numeric_limits<float>::quiet_NaN();
    odd.depth = -1.0f;
    odd.convergeM = 0.0f;
    auto const k = cs::Clamped(odd);
    CHECK(k.distanceM == cs::kMaxDistanceM && k.widthM == cs::Settings{}.widthM && k.depth == 0.0f && k.convergeM == cs::kMinConvergeM);
}

void TestWorldUi() {
    namespace wu = mevr::vr::world_ui;
    // The tags (the game's GUI library), either case.
    CHECK(wu::FromTag("shop") == wu::Screen::Shop && wu::FromTag("SHOP") == wu::Screen::Shop);
    CHECK(wu::FromTag("Inventory") == wu::Screen::Inventory && wu::FromTag("MainMenu") == wu::Screen::None);
    CHECK(std::string(wu::Name(wu::Screen::Loot)) == "Loot" && std::string(wu::Name(wu::Screen::None)) == "None");
    // The stage in the shop's window (2344 x 2392, measured: rows 537..1855): letterboxed 16:9.
    wu::Rect const stage = wu::Stage(2344.0f, 2392.0f);
    CHECK(NearD(stage.u0, 0.0, 1e-6) && NearD(stage.u1, 1.0, 1e-6));
    CHECK(NearD(stage.v0 * 2392.0, 537.0, 1.0) && NearD(stage.v1 * 2392.0, 1855.0, 1.0));
    // A wide window: pillarboxed.
    wu::Rect const wide = wu::Stage(2560.0f, 1080.0f);
    CHECK(NearD(wide.v0, 0.0, 1e-6) && NearD((wide.u1 - wide.u0) * 2560.0, 1920.0, 0.5));
    wu::Rect const part = wu::InWindow(stage, {0.25f, 0.5f, 0.75f, 1.0f});
    CHECK(NearD(part.u0, 0.25, 1e-6) && NearD(part.u1, 0.75, 1e-6) && NearD(part.v1, stage.v1, 1e-6) &&
          NearD(part.v0, stage.v0 + 0.5 * (stage.v1 - stage.v0), 1e-6));
    // Placed toward the anchor, short of it, below the eyes, facing them, level.
    XrVector3f const eyes{0.2f, 1.7f, 0.0f};
    auto const toward = wu::Place(eyes, {0.0f, 0.0f, -1.0f}, XrVector3f{2.2f, 1.2f, 0.0f}, 1.2f, 1.0f, 0.12f, 0.75f);
    CHECK(toward.has_value());
    if (toward) {
        CHECK(NearXr(toward->pose.position, {1.2f, 1.58f, 0.0f}, 1e-4));
        CHECK(NearD(toward->widthM, 1.2, 1e-6) && NearD(toward->heightM, 0.9, 1e-6));
        XrVector3f const front = mevr::vr::overlay::Rotate(toward->pose.orientation, {0.0f, 0.0f, 1.0f});
        XrVector3f const toEyes{eyes.x - toward->pose.position.x, eyes.y - toward->pose.position.y, eyes.z - toward->pose.position.z};
        float const len = std::sqrt(toEyes.x * toEyes.x + toEyes.y * toEyes.y + toEyes.z * toEyes.z);
        CHECK(NearXr(front, {toEyes.x / len, toEyes.y / len, toEyes.z / len}, 1e-4));
        XrVector3f const across = mevr::vr::overlay::Rotate(toward->pose.orientation, {1.0f, 0.0f, 0.0f});
        CHECK(NearD(across.y, 0.0, 1e-5));
    }
    // An anchor close by: never nearer than kNearestM; no anchor: straight ahead.
    auto const close = wu::Place(eyes, {0.0f, 0.0f, -1.0f}, XrVector3f{0.2f, 1.2f, -0.6f}, 1.2f, 1.0f, 0.12f, 0.75f);
    CHECK(close && NearD(close->pose.position.z, -wu::kNearestM, 1e-4));
    auto const ahead = wu::Place(eyes, {0.6f, -0.8f, 0.0f}, std::nullopt, 1.2f, 1.0f, 0.0f, 0.75f);
    CHECK(ahead && NearXr(ahead->pose.position, {1.2f, 1.7f, 0.0f}, 1e-4));
    CHECK(!wu::Place(eyes, {0.0f, 0.0f, -1.0f}, std::nullopt, 0.0f, 1.0f, 0.0f, 0.75f));
    // The whole window around a cropped panel: the crop's corners land on the panel's corners.
    if (toward) {
        wu::Rect const crop{0.14f, 0.2245f, 0.86f, 0.7755f};
        wu::Panel const whole = wu::WholeWindow(*toward, crop);
        auto const corner = [](wu::Panel const& p, float u, float v) {
            XrVector3f const local{(u - 0.5f) * p.widthM, (0.5f - v) * p.heightM, 0.0f};
            XrVector3f const r = mevr::vr::overlay::Rotate(p.pose.orientation, local);
            return XrVector3f{p.pose.position.x + r.x, p.pose.position.y + r.y, p.pose.position.z + r.z};
        };
        CHECK(NearXr(corner(whole, crop.u0, crop.v0), corner(*toward, 0.0f, 0.0f), 1e-4));
        CHECK(NearXr(corner(whole, crop.u1, crop.v1), corner(*toward, 1.0f, 1.0f), 1e-4));
    }
    // Projected from the wrist: facing the eyes, level; its bottom edge aboveM over the wrist across the line of sight, so the eyes
    // see the wrist below it (not through it).
    XrVector3f const wrist{0.2f, 1.25f, -0.40f};
    auto const held = wu::WristPlace(eyes, wrist, 0.30f, 0.06f, 0.0f, 0.75f);
    CHECK(held.has_value());
    auto const bottomOf = [](wu::Panel const& p) {
        XrVector3f const up = mevr::vr::overlay::Rotate(p.pose.orientation, {0.0f, 1.0f, 0.0f});
        return XrVector3f{p.pose.position.x - up.x * 0.5f * p.heightM, p.pose.position.y - up.y * 0.5f * p.heightM,
            p.pose.position.z - up.z * 0.5f * p.heightM};
    };
    auto const below = [&](XrVector3f const& p) {  // how far below the eyes' level it looks (radians)
        float const dx = p.x - eyes.x, dz = p.z - eyes.z;
        return std::atan2(eyes.y - p.y, std::sqrt(dx * dx + dz * dz));
    };
    if (held) {
        CHECK(NearD(held->heightM, 0.225, 1e-6));
        XrVector3f const edge = bottomOf(*held);
        float const gx = edge.x - wrist.x, gy = edge.y - wrist.y, gz = edge.z - wrist.z;
        CHECK(NearD(std::sqrt(gx * gx + gy * gy + gz * gz), 0.06, 0.01));  // the gap
        CHECK(below(edge) < below(wrist) - 0.05f);                          // seen above the wrist, the gap between
        CHECK(held->pose.position.y > wrist.y + 0.1f);
        XrVector3f const front = mevr::vr::overlay::Rotate(held->pose.orientation, {0.0f, 0.0f, 1.0f});
        XrVector3f const toEyes{eyes.x - held->pose.position.x, eyes.y - held->pose.position.y, eyes.z - held->pose.position.z};
        float const len = std::sqrt(toEyes.x * toEyes.x + toEyes.y * toEyes.y + toEyes.z * toEyes.z);
        CHECK(NearXr(front, {toEyes.x / len, toEyes.y / len, toEyes.z / len}, 1e-4));
        CHECK(NearD(mevr::vr::overlay::Rotate(held->pose.orientation, {1.0f, 0.0f, 0.0f}).y, 0.0, 1e-5));
        // Unfolding: nothing at first, its bottom edge where it stays; whole once kUnfoldS has passed.
        auto const bottom = [](wu::Panel const& p) {
            XrVector3f const up = mevr::vr::overlay::Rotate(p.pose.orientation, {0.0f, 1.0f, 0.0f});
            return XrVector3f{p.pose.position.x - up.x * 0.5f * p.heightM, p.pose.position.y - up.y * 0.5f * p.heightM,
                p.pose.position.z - up.z * 0.5f * p.heightM};
        };
        wu::Panel const first = wu::Unfold(*held, 0.0f);
        CHECK(first.heightM < 0.01f && NearD(first.widthM, 0.30 * wu::kUnfoldWidth, 1e-5));
        CHECK(NearXr(bottom(first), bottom(*held), 1e-4));
        wu::Panel const half = wu::Unfold(*held, 0.5f * wu::kUnfoldS);
        CHECK(half.heightM > 0.5f * held->heightM && half.heightM < held->heightM && NearXr(bottom(half), bottom(*held), 1e-4));
        wu::Panel const done = wu::Unfold(*held, wu::kUnfoldS);
        CHECK(NearD(done.heightM, held->heightM, 1e-6) && NearXr(done.pose.position, held->pose.position, 1e-6));
        // The beam: from the wrist to the panel's bottom edge, in their plane, as wide as the panel.
        auto const beam = wu::BeamTo(wrist, *held);
        CHECK(beam.has_value());
        if (beam) {
            XrVector3f const b = bottom(*held);
            CHECK(NearXr(beam->pose.position, {0.5f * (wrist.x + b.x), 0.5f * (wrist.y + b.y), 0.5f * (wrist.z + b.z)}, 1e-4));
            float const dx = b.x - wrist.x, dy = b.y - wrist.y, dz = b.z - wrist.z;
            CHECK(NearD(beam->heightM, std::sqrt(dx * dx + dy * dy + dz * dz), 1e-4) && NearD(beam->widthM, 0.30, 1e-6));
            XrVector3f const top = mevr::vr::overlay::Rotate(beam->pose.orientation, {0.0f, 0.5f * beam->heightM, 0.0f});
            CHECK(NearXr({beam->pose.position.x + top.x, beam->pose.position.y + top.y, beam->pose.position.z + top.z}, b, 1e-4));
            XrVector3f const bx = mevr::vr::overlay::Rotate(beam->pose.orientation, {1.0f, 0.0f, 0.0f});
            XrVector3f const px = mevr::vr::overlay::Rotate(held->pose.orientation, {1.0f, 0.0f, 0.0f});
            CHECK(bx.x * px.x + bx.y * px.y + bx.z * px.z > 0.99f);
        }
        CHECK(!wu::BeamTo(bottom(*held), *held));  // the emitter on the edge: no fan
    }
    // Never in the face: a wrist at the eyes puts it kWristNearestM away.
    auto const close2 = wu::WristPlace(eyes, {0.2f, 1.55f, -0.05f}, 0.30f, 0.06f, 0.05f, 0.75f);
    CHECK(close2.has_value());
    if (close2) {
        float const dx = close2->pose.position.x - eyes.x, dy = close2->pose.position.y - eyes.y, dz = close2->pose.position.z - eyes.z;
        CHECK(NearD(std::sqrt(dx * dx + dy * dy + dz * dz), wu::kWristNearestM, 1e-4));
    }
    CHECK(!wu::WristPlace(eyes, wrist, 0.0f, 0.06f, 0.05f, 0.75f));
    // On the wrist the minigame shows its frame only; in the room its whole stage.
    wu::Rect const skill = wu::WristCrop(wu::Screen::SkillGame);
    CHECK(skill.u0 < 0.226f && skill.u1 > 0.775f && skill.v0 < 0.12f && skill.v1 > 0.848f && skill.u0 > 0.15f);
    wu::Rect const skillRoom = wu::DefaultCrop(wu::Screen::SkillGame);
    CHECK(skillRoom.u0 == 0.0f && skillRoom.u1 == 1.0f);
    wu::Rect const shopWrist = wu::WristCrop(wu::Screen::Shop);
    CHECK(shopWrist.u0 == wu::DefaultCrop(wu::Screen::Shop).u0);
    // The loot shows its frame (measured: stage x 0.317..0.683, y 0.064..0.922) at the whole stage's scale.
    wu::Rect const loot = wu::DefaultCrop(wu::Screen::Loot);
    CHECK(loot.u0 < 0.317f && loot.u1 > 0.683f && loot.v0 < 0.064f && loot.v1 > 0.922f && loot.u0 > 0.2f && loot.u1 < 0.8f);
    CHECK(std::fabs(wu::RoomWidth(wu::Screen::Loot, 1.2f) - 1.2f * (loot.u1 - loot.u0)) < 1e-5f);
    CHECK(wu::RoomWidth(wu::Screen::Shop, 1.2f) == 1.2f);
    // The fan's picture: wide at the panel's edge, a point at the emitter, brighter there, premultiplied.
    std::vector<std::uint32_t> const fan = wu::BeamPixels(64, 0.6f, 0.8f, 1.0f);
    CHECK(fan.size() == 64u * 64u);
    auto const alpha = [&](int row, int column) { return static_cast<int>(fan[static_cast<std::size_t>(row) * 64 + column] >> 24); };
    CHECK(alpha(0, 8) > 0 && alpha(63, 8) == 0 && alpha(63, 32) > alpha(0, 32));
    CHECK(alpha(0, 0) == 0 && alpha(0, 32) > 0);
    // Premultiplied in linear light: each colour, decoded from sRGB, is at most alpha; blue (1.0) decodes to alpha itself.
    std::uint32_t const middle = fan[static_cast<std::size_t>(63) * 64 + 32];
    auto const linear = [](std::uint32_t b) {
        double const c = static_cast<double>(b & 0xFFu) / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    double const a63 = static_cast<double>(middle >> 24) / 255.0;
    CHECK(linear(middle) <= a63 + 0.01 && linear(middle >> 8) <= a63 + 0.01 && NearD(linear(middle >> 16), a63, 0.01));
}

void TestGalaxyHologram() {
    namespace gh = mevr::vr::galaxy_holo;
    // The table: ahead of the head's heading and below the eyes, facing the player, whatever the head's pitch.
    float const pitch = -0.5f;
    XrPosef const head{{std::sin(pitch * 0.5f), 0.0f, 0.0f, std::cos(pitch * 0.5f)}, {0.1f, 1.6f, 0.2f}};
    gh::TableSettings settings;
    gh::Table const table = gh::PlaceTable(head, settings);
    CHECK(NearD(table.pose.position.x, 0.1, 1e-5) && NearD(table.pose.position.y, 1.6 - settings.belowM, 1e-5) &&
          NearD(table.pose.position.z, 0.2 - settings.distanceM, 1e-5));
    CHECK(NearD(table.pose.orientation.w, 1.0, 1e-5));

    // A galaxy: the centre on the table's centre, the farthest object inside the rim, 'forward' away from the player.
    gh::Scene s;
    s.level = static_cast<std::uint8_t>(gh::Level::Galaxy);
    s.centre[0] = 1000.0f;
    auto add = [&](float x, float y, float z, std::uint8_t flags) {
        gh::Object& o = s.objects[s.objectCount++];
        o.position[0] = x; o.position[1] = y; o.position[2] = z;
        o.kind = static_cast<std::uint8_t>(gh::Kind::Cluster);
        o.flags = flags;
        std::snprintf(o.label, gh::kLabelBytes, "c%d", s.objectCount);
    };
    add(1000.0f, 0.0f, 0.0f, gh::kSelectable);        // at the centre
    add(3000.0f, 0.0f, 0.0f, gh::kSelectable);        // 2000 ahead (game X = the scene's forward)
    add(1000.0f, 2000.0f, 0.0f, gh::kSelectable);     // 2000 to the right
    add(1000.0f, 0.0f, 500.0f, 0);                    // above the plane, not selectable
    float const scale = gh::FitScale(s, table.radiusM);
    CHECK(NearD(scale, table.radiusM / (2000.0f * 1.15f), 1e-7));
    auto const placed = gh::PlaceObjects(s, table, settings);
    CHECK(placed.size() == 4);
    CHECK(NearD(gh::Length(gh::Sub(placed[0].position, table.pose.position)), 0.0, 1e-5));
    XrVector3f const ahead = gh::Sub(placed[1].position, table.pose.position);
    CHECK(ahead.z < -0.3f && NearD(ahead.x, 0.0, 1e-5) && NearD(ahead.y, 0.0, 1e-5));   // away from the player
    XrVector3f const side = gh::Sub(placed[2].position, table.pose.position);
    CHECK(side.x > 0.3f && NearD(side.z, 0.0, 1e-5));                                      // game right = room right
    CHECK(gh::Sub(placed[3].position, table.pose.position).y > 0.05f);                    // game up = room up

    // Picking: a ray from the hand through an object picks it; a ray a little off still does; the unselectable one never;
    // behind the hand, nothing.
    XrVector3f const hand{0.3f, 1.2f, 0.0f};
    for (int i : {0, 1, 2}) {
        XrVector3f const dir = gh::Sub(placed[static_cast<size_t>(i)].position, hand);
        CHECK(gh::Pick(hand, dir, placed, s) == i);
        CHECK(gh::Pick(hand, gh::Add(gh::Normalize(dir), XrVector3f{0.01f, 0.0f, 0.0f}), placed, s) == i);
    }
    CHECK(gh::Pick(hand, gh::Sub(placed[3].position, hand), placed, s) != 3);
    CHECK(gh::Pick(hand, gh::Mul(gh::Sub(placed[1].position, hand), -1.0f), placed, s) == -1);
    CHECK(gh::Pick(hand, {0.0f, 1.0f, 0.0f}, placed, s) == -1);  // straight up: nothing near

    // The pointer selects once per trigger press, on what's hovered.
    gh::Pointer pointer;
    CHECK(!pointer.Step(2, 0.0f, true) && pointer.Hovered() == 2);
    auto const pressed = pointer.Step(2, 0.7f, true);
    CHECK(pressed && *pressed == 2);
    CHECK(!pointer.Step(2, 0.5f, true));   // held (above the 0.4 release)
    CHECK(!pointer.Step(2, 0.3f, true));   // released
    CHECK(!pointer.Step(-1, 0.9f, true));  // pressed on nothing: no select ...
    CHECK(!pointer.Step(1, 0.9f, true));   // ... and moving onto something while held doesn't select either
    CHECK(!pointer.Step(1, 0.0f, false) && pointer.Hovered() == -1);

    // The game's screen: straight ahead is the centre; right is right, up is up; behind is nothing.
    gh::GameCamera cam;
    cam.valid = 1;
    cam.fovXDeg = 90.0f;
    cam.aspect = 2.0f;
    float const front[3] = {100.0f, 0.0f, 0.0f};
    float const rightUp[3] = {100.0f, 50.0f, 25.0f};
    float const behind[3] = {-100.0f, 0.0f, 0.0f};
    auto const c = gh::ToGameScreen(cam, front);
    auto const ru = gh::ToGameScreen(cam, rightUp);
    CHECK(c && NearD((*c)[0], 0.5, 1e-5) && NearD((*c)[1], 0.5, 1e-5));
    CHECK(ru && NearD((*ru)[0], 0.75, 1e-5) && NearD((*ru)[1], 0.25, 1e-5));  // tan 45 = 1: x 50/100 -> 0.75; y 25/(100/2) -> 0.25
    CHECK(!gh::ToGameScreen(cam, behind));
    // Turned 90 degrees right (yaw), a point on the game's +Y is straight ahead.
    cam.yaw = 1.5707963f;
    float const onY[3] = {0.0f, 100.0f, 0.0f};
    auto const y = gh::ToGameScreen(cam, onY);
    CHECK(y && NearD((*y)[0], 0.5, 1e-4) && NearD((*y)[1], 0.5, 1e-4));
    // Pitched down 30 degrees: a point below and ahead on that line is the centre.
    cam.yaw = 0.0f;
    cam.pitch = -0.5235988f;
    float const down[3] = {100.0f * std::cos(0.5235988f), 0.0f, -100.0f * std::sin(0.5235988f)};
    auto const d = gh::ToGameScreen(cam, down);
    CHECK(d && NearD((*d)[0], 0.5, 1e-4) && NearD((*d)[1], 0.5, 1e-4));

    // The picture: a label per labelled object, a halo on the hovered one, the galaxy's disc at the galaxy level.
    auto const list = gh::Build(s, table, settings, placed, 1, 0.0f);
    CHECK(list.labels.size() == 4);
    int halos = 0;
    for (auto const& sp : list.sprites) {
        halos += sp.style == gh::SpriteStyle::Halo ? 1 : 0;
    }
    CHECK(halos == 1);
    bool galaxyDisc = false;
    for (auto const& disc : list.discs) {
        galaxyDisc = galaxyDisc || disc.style == gh::DiscStyle::Galaxy;
    }
    CHECK(galaxyDisc);
    CHECK(!gh::Describe(s).empty());

    // The window on the projector's hologram (measured 2026-11-04: its normal 29 degrees off vertical, toward the player): the
    // quad's front faces the head, its +X is level and the head's right, its +Y climbs the disc away from the head. A normal
    // given from below turns to the head's side; a flat disc takes the head's right.
    float const tilt = 29.3f * 0.01745329f;
    XrVector3f const discCentre{0.0f, 1.0f, -3.7f};
    XrVector3f const discNormal{0.0f, std::cos(tilt), std::sin(tilt)};
    XrVector3f const eye{0.0f, 1.7f, 0.0f};
    for (XrVector3f const n : {discNormal, gh::Mul(discNormal, -1.0f)}) {
        XrPosef const pose = gh::DiscPose(discCentre, n, eye);
        XrVector3f const qx = mevr::vr::overlay::Rotate(pose.orientation, {1.0f, 0.0f, 0.0f});
        XrVector3f const qy = mevr::vr::overlay::Rotate(pose.orientation, {0.0f, 1.0f, 0.0f});
        XrVector3f const qz = mevr::vr::overlay::Rotate(pose.orientation, {0.0f, 0.0f, 1.0f});
        CHECK(NearD(gh::Dot(qz, discNormal), 1.0, 1e-5));
        CHECK(NearD(qx.x, 1.0, 1e-5) && NearD(qx.y, 0.0, 1e-5));
        CHECK(qy.y > 0.4f && qy.z < -0.8f);
        CHECK(NearD(pose.position.z, -3.7, 1e-6));
    }
    XrPosef const flat = gh::DiscPose(discCentre, {0.0f, 1.0f, 0.0f}, eye);
    XrVector3f const flatX = mevr::vr::overlay::Rotate(flat.orientation, {1.0f, 0.0f, 0.0f});
    XrVector3f const flatY = mevr::vr::overlay::Rotate(flat.orientation, {0.0f, 1.0f, 0.0f});
    CHECK(NearD(flatX.x, 1.0, 1e-5) && flatY.z < -0.99f);
}

void TestGalaxyMap() {
    namespace gm = mevr::vr::galaxy_map;
    // The scale: a map centre 50,000 units ahead of its camera shown 0.9 m ahead.
    CHECK(gm::UnitsPerMeter(50000.0f, 0.9f).has_value() && NearD(*gm::UnitsPerMeter(50000.0f, 0.9f), 50000.0 / 0.9, 1e-1));
    CHECK(!gm::UnitsPerMeter(0.0f, 0.9f) && !gm::UnitsPerMeter(-5.0f, 0.9f) && !gm::UnitsPerMeter(100.0f, 0.0f));
    CHECK(*gm::UnitsPerMeter(1e12f, 0.5f) == gm::kMaxUnitsPerMeter && *gm::UnitsPerMeter(0.1f, 2.0f) == gm::kMinUnitsPerMeter);
    // The virtual screen: through the point 0.9 m ahead, 0.3 m below the eyes, as wide as the window is at that distance.
    float const tx = 0.8f;
    float const ty = 0.45f;
    auto const screen = gm::VirtualScreen(0.9f, 0.3f, tx, ty);
    CHECK(screen.has_value());
    CHECK(NearD(screen->pose.position.z, -0.9, 1e-6) && NearD(screen->pose.position.y, -0.3, 1e-6) && NearD(screen->width, 2.0 * 0.9 * tx, 1e-6) &&
          NearD(screen->height, 2.0 * 0.9 * ty, 1e-6));
    CHECK(!gm::VirtualScreen(0.0f, 0.3f, tx, ty) && !gm::VirtualScreen(0.9f, 0.3f, 0.0f, ty));
    // A point on the screen projects to the window where the quad's own UV puts it (the game pointer's pixel).
    for (float const qx : {-0.5f, -0.1f, 0.0f, 0.3f, 0.49f}) {
        for (float const qy : {-0.45f, 0.0f, 0.2f}) {
            XrVector3f const p{qx * screen->width, -0.3f + qy * screen->height, -0.9f};
            auto const uv = gm::ProjectToWindow(p, 0.3f, tx, ty);
            CHECK(uv.has_value() && NearD(uv->x, 0.5 + qx, 1e-5) && NearD(uv->y, 0.5 - qy, 1e-5));
        }
    }
    // Further along the same line from the camera: the same pixel (what the game's projection does at any depth).
    XrVector3f const closer{0.2f, -0.3f + 0.1f, -0.9f};
    XrVector3f const further{0.4f, -0.3f + 0.2f, -1.8f};
    auto const a = gm::ProjectToWindow(closer, 0.3f, tx, ty);
    auto const b = gm::ProjectToWindow(further, 0.3f, tx, ty);
    CHECK(a && b && NearD(a->x, b->x, 1e-5) && NearD(a->y, b->y, 1e-5));
    CHECK(!gm::ProjectToWindow({0.0f, 0.0f, 0.5f}, 0.3f, tx, ty));  // behind the camera
}

void TestSpectator() {
    namespace sp = mevr::vr::spectator;
    constexpr double kDeg = 180.0 / 3.14159265358979;
    // 85 degrees across a 16:9 picture: the vertical field from the aspect.
    CHECK(NearD(sp::TanHalfX(85.0f), std::tan(42.5 / kDeg), 1e-5));
    CHECK(NearD(sp::TanHalfY(85.0f, 1920, 1080), std::tan(42.5 / kDeg) * 9.0 / 16.0, 1e-5));
    CHECK(NearD(sp::TanHalfY(85.0f, 3840, 2160), sp::TanHalfY(85.0f, 1920, 1080), 1e-6));
    {
        XrFovf const f = sp::Fov(90.0f, 1600, 900);
        CHECK(NearD(f.angleLeft, -0.785398, 1e-4) && NearD(f.angleRight, 0.785398, 1e-4));
        CHECK(NearD(std::tan(f.angleUp), 900.0 / 1600.0, 1e-5) && NearD(f.angleDown, -f.angleUp, 1e-6));
    }
    // Rotation vectors round-trip; the angle between two orientations.
    {
        XrQuaternionf const q = AxisAngle(0.3 / 0.9695, 0.9 / 0.9695, -0.2 / 0.9695, 70);
        XrQuaternionf const back = sp::Exp(sp::Log(q));
        CHECK(NearD(back.x, q.x, 1e-4) && NearD(back.y, q.y, 1e-4) && NearD(back.z, q.z, 1e-4) && NearD(back.w, q.w, 1e-4));
        CHECK(NearD(sp::AngleBetween(AxisAngle(0, 1, 0, 10), AxisAngle(0, 1, 0, 50)) * kDeg, 40.0, 1e-3));
        XrQuaternionf const minus{-q.x, -q.y, -q.z, -q.w};  // the same orientation: the short way, 0 apart
        CHECK(NearD(sp::AngleBetween(q, minus), 0.0, 1e-3));
    }
    // The first step snaps; then a step toward a jump moves part of the way, never past it, and settles.
    {
        sp::Follow f;
        XrPosef const a{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.6f, 0.0f}};
        sp::Step(f, a, 0.011f, 0.25f);
        CHECK(f.valid && f.pose.position.y == 1.6f);
        XrPosef b = a;
        b.position.x = 1.0f;
        b.orientation = AxisAngle(0, 1, 0, 90);
        float lastX = 0.0f;
        double lastAngle = 90.0;
        bool monotonic = true;
        float atSmooth = 0.0f;
        for (int i = 1; i <= 200; ++i) {  // 2.2 s at 90 Hz
            sp::Step(f, b, 1.0f / 90.0f, 0.25f);
            double const angle = sp::AngleBetween(f.pose.orientation, b.orientation) * kDeg;
            monotonic = monotonic && f.pose.position.x >= lastX - 1e-6f && f.pose.position.x <= 1.0f + 1e-6f && angle <= lastAngle + 1e-3;
            lastX = f.pose.position.x;
            lastAngle = angle;
            if (i == 22) {
                atSmooth = f.pose.position.x;  // about smoothS in
            }
        }
        CHECK(monotonic);
        CHECK(atSmooth > 0.3f && atSmooth < 0.9f);
        CHECK(NearD(f.pose.position.x, 1.0, 1e-3) && lastAngle < 0.1);
        // A shaking target (5 mm at 15 Hz): the camera moves far less.
        sp::Snap(f, a);
        float most = 0.0f;
        for (int i = 0; i < 180; ++i) {
            XrPosef shake = a;
            shake.position.x = 0.005f * static_cast<float>(std::sin(2.0 * 3.14159265 * 15.0 * i / 90.0));
            sp::Step(f, shake, 1.0f / 90.0f, 0.25f);
            most = std::max(most, std::abs(f.pose.position.x));
        }
        CHECK(most < 0.001f);
        // smoothS 0: the head exactly.
        sp::Step(f, b, 1.0f / 90.0f, 0.0f);
        CHECK(f.pose.position.x == 1.0f);
        // A zero step changes nothing; a huge one is clamped and never blows up.
        sp::Snap(f, a);
        sp::Step(f, b, 0.0f, 0.25f);
        CHECK(f.pose.position.x == 0.0f);
        sp::Step(f, b, 100.0f, 0.25f);
        CHECK(f.pose.position.x > 0.0f && f.pose.position.x <= 1.0f && std::isfinite(f.pose.orientation.w));
    }
    // The publish history and the Draw's arm, as the scope capture's.
    {
        sp::Reset();
        CHECK(sp::Latest().state == sp::kOff);
        sp::Frame frame{};
        frame.state = sp::kCapturing;
        frame.pose.position = {1.0f, 0.0f, 0.0f};
        sp::Publish(frame);
        std::uint64_t const s1 = sp::LatestSequence();
        sp::NoteDrawArm(5);
        frame.pose.position = {2.0f, 0.0f, 0.0f};
        sp::Publish(frame);
        auto const forArm = sp::SequenceForArm(5);
        CHECK(forArm && *forArm == s1);
        auto const rendered = forArm ? sp::BySequence(*forArm) : std::nullopt;
        CHECK(rendered && rendered->pose.position.x == 1.0f);
        CHECK(sp::Latest().state == sp::kCapturing && sp::Latest().pose.position.x == 2.0f);
        std::uint64_t const resets = sp::Resets();
        sp::Reset();
        CHECK(sp::Resets() == resets + 1 && !sp::SequenceForArm(5) && sp::Latest().state == sp::kOff);
    }
}

void TestSceneCaptureScope() {
    namespace sc = mevr::vr::scene_capture;
    // A capture's passes run with no eye: index -1, no skipped passes, and the eye's one-Draw finish isn't theirs.
    sc::EyeRenderState const eye{0, true, true, true};
    sc::EyeRenderState const during = sc::DuringCapture(eye);
    CHECK(during.renderIndex == -1);
    CHECK(!during.skipPasses);
    CHECK(!during.oneDrawFinish && !during.oneDrawCheck);
    // The final pass: an eye's goes to the backbuffer; a capture's to its own target, once that is known and has a size.
    CHECK(sc::ChooseFinalTarget(0, true, 512, 288) == sc::FinalTarget::Backbuffer);
    CHECK(sc::ChooseFinalTarget(1, true, 512, 288) == sc::FinalTarget::CaptureTarget);
    CHECK(sc::ChooseFinalTarget(1, false, 512, 288) == sc::FinalTarget::Backbuffer);
    CHECK(sc::ChooseFinalTarget(1, true, 0, 288) == sc::FinalTarget::Backbuffer);
    // The recorder's expected draws leave out the captures' (the counter wraps safely, and never goes below zero).
    CHECK(sc::EyeDrawsIssued(5100, 100, 1000) == 4000);
    CHECK(sc::EyeDrawsIssued(100, 0xFFFFFF00u, 0) == 356);
    CHECK(sc::EyeDrawsIssued(100, 50, 60) == 0);
}

void TestScopeCapture() {
    namespace c = mevr::vr::scope_capture;
    namespace s = mevr::vr::sights;
    constexpr double kDeg = 180.0 / 3.14159265358979;
    // 22 degrees at 4x hold 5.5 degrees of the world (true magnification: tan(11) / 4); the margin widens the tangent.
    double const world = 2.0 * std::atan(std::tan(11.0 / kDeg) / 4.0) * kDeg;
    CHECK(NearD(c::FovDeg(22.0f, 4.0f, 0.0f), world, 1e-3));
    CHECK(NearD(std::tan(c::FovDeg(22.0f, 4.0f, 0.25f) / 2.0 / kDeg), 1.25 * std::tan(world / 2.0 / kDeg), 1e-5));
    CHECK(NearD(c::FovDeg(22.0f, 1.5f, 0.0f), 2.0 * std::atan(std::tan(11.0 / kDeg) / 1.5) * kDeg, 1e-3));
    CHECK(c::FovDeg(22.0f, 0.5f, 0.0f) == c::FovDeg(22.0f, 1.0f, 0.0f));  // never wider than the unmagnified picture
    CHECK(c::FovDeg(std::numeric_limits<float>::quiet_NaN(), 4.0f, 0.0f) == 10.0f);
    CHECK(NearD(c::TanHalf(90.0f), 1.0, 1e-6));
    CHECK(c::PictureDeg(22.0f, 0.02f) == 22.0f);
    CHECK(NearD(c::PictureDeg(0.0f, 0.02f), 2.0 * std::atan(0.4) * kDeg, 1e-3));  // the physical lens seen from 5 cm
    CHECK(c::NearPlaneUnits(59.0f, 20.0f) == 79.0f && c::NearPlaneUnits(-5.0f, 0.0f) == 10.0f);
    // The camera looks along `forward` with its up toward `up`.
    {
        XrVector3f const forward{0.3f, -0.1f, -0.9f};
        auto const q = c::CameraOrientation(forward, {0.0f, 1.0f, 0.0f});
        CHECK(q.has_value());
        if (q) {
            XrVector3f const look = mevr::vr::overlay::Rotate(*q, {0.0f, 0.0f, -1.0f});
            XrVector3f const up = mevr::vr::overlay::Rotate(*q, {0.0f, 1.0f, 0.0f});
            float const n = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
            CHECK(NearD(look.x, forward.x / n, 1e-5) && NearD(look.y, forward.y / n, 1e-5) && NearD(look.z, forward.z / n, 1e-5));
            CHECK(up.y > 0.9f && NearD(up.x * look.x + up.y * look.y + up.z * look.z, 0.0, 1e-5));
        }
        CHECK(!c::CameraOrientation({0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}));
        CHECK(!c::CameraOrientation({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}));
    }
    // The publish history and which pose a Draw rendered.
    {
        c::Reset();
        c::Capture first{};
        first.state = c::kCapturing;
        first.pose.position = {1.0f, 0.0f, 0.0f};
        c::Publish(first);
        std::uint64_t const s1 = c::LatestSequence();
        c::NoteDrawArm(77);   // the Draw that latched arm 77 renders publish s1
        c::NoteDrawArm(77);   // another view of the same Draw
        c::Capture second = first;
        second.pose.position = {2.0f, 0.0f, 0.0f};
        c::Publish(second);
        c::NoteDrawArm(78);
        auto const forArm = c::SequenceForArm(77);
        CHECK(forArm && *forArm == s1);
        auto const rendered = forArm ? c::BySequence(*forArm) : std::nullopt;
        CHECK(rendered && rendered->pose.position.x == 1.0f);
        CHECK(c::SequenceForArm(78) && *c::SequenceForArm(78) == s1 + 1);
        CHECK(!c::SequenceForArm(0) && !c::SequenceForArm(99));
        CHECK(c::Latest().state == c::kCapturing && c::Latest().pose.position.x == 2.0f);
        for (int i = 0; i < c::kHistory; ++i) {
            c::Publish(second);
        }
        CHECK(!c::BySequence(s1));  // fell out of the history
        std::uint64_t const resets = c::Resets();
        c::Reset();
        CHECK(c::Latest().state == c::kOff && !c::SequenceForArm(77));
        CHECK(c::Resets() == resets + 1);  // the host drops its texture view: a reloaded mod's generations start again
    }
    // The D3D texture behind the resource: a fake object graph (resource -> RHI object -> texture).
    {
        std::uint64_t memory[64] = {};
        auto const addr = [&](int i) { return reinterpret_cast<std::uint64_t>(&memory[i]); };
        std::uint64_t const texture = addr(40);
        memory[0] = 0x140001000ull;  // the resource's vtable (an image address: not a texture)
        memory[3] = addr(20);        // resource +0x18: the RHI reference
        memory[20] = 0x140002000ull;  // its vtable
        memory[22] = texture;         // RHI object +0x10: the D3D texture
        auto const read = [&](std::uint64_t a) -> std::optional<std::uint64_t> {
            if (a < addr(0) || a >= addr(64)) {
                return std::nullopt;
            }
            return *reinterpret_cast<std::uint64_t const*>(a);
        };
        auto const isTexture = [&](std::uint64_t p) { return p == texture; };
        c::TexturePath const path = c::FindTexture(addr(0), read, isTexture);
        CHECK(path.Valid() && path.first == 0x18 && path.second == 0x10 && path.texture == texture);
        CHECK(c::FindTexture(addr(0), read, isTexture, path).texture == texture);  // through the hint
        CHECK(!c::FindTexture(addr(0), read, [](std::uint64_t) { return false; }).Valid());
        CHECK(!c::FindTexture(0, read, isTexture).Valid());
        memory[5] = texture;  // a direct reference at +0x28: the first in address order (+0x18's) still wins
        CHECK(c::FindTexture(addr(0), read, isTexture).first == 0x18);
    }
    // The picture samples the capture through the same projection as the eye copy: the sight line lands at the capture's
    // centre, and the picture's edge (11 degrees seen, 4x) inside its frustum with the margin's room.
    {
        auto const lens = s::MakeLens({0.0f, 0.0f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 0.02f);
        CHECK(lens.has_value());
        if (!lens) {
            return;
        }
        s::Eyepiece const ep{22.0f, 0.05f, 0.70f};
        XrVector3f const eye{0.0f, 0.0f, -0.2f};
        s::Lens picture{};
        auto const view = s::VirtualView(*lens, eye, ep, picture);
        CHECK(view.has_value());
        auto const orientation = c::CameraOrientation({0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
        CHECK(orientation.has_value());
        if (!view || !orientation) {
            return;
        }
        float const fov = c::FovDeg(22.0f, 4.0f, 0.25f);
        float const t = c::TanHalf(fov);
        float const half = std::atan(t);
        XrPosef const capture{*orientation, {0.0f, 0.0f, -0.5f}};
        XrFovf const frustum{-half, half, half, -half};
        float u = 0.0f;
        float v = 0.0f;
        XrVector3f const centre = s::FocusPoint(eye, s::LensDirection(picture, *view, 0.0f, 0.0f, 4.0f), lens->axis, 40.0f);
        CHECK(s::Project(capture, frustum, centre, u, v) && NearD(u, 0.5, 1e-4) && NearD(v, 0.5, 1e-4));
        XrVector3f const edge = s::FocusPoint(eye, s::LensDirection(picture, *view, 1.0f, 0.0f, 4.0f), lens->axis, 40.0f);
        // (0.3 m of parallax between the eye and the capture at 40 m: under 1%.)
        CHECK(s::Project(capture, frustum, edge, u, v) && NearD(u, 0.5 + 0.5 / 1.25, 5e-3) && NearD(v, 0.5, 1e-4));
        XrVector3f const top = s::FocusPoint(eye, s::LensDirection(picture, *view, 0.0f, 1.0f, 4.0f), lens->axis, 40.0f);
        CHECK(s::Project(capture, frustum, top, u, v) && v < 0.5f);  // the picture's top is the texture's top
    }
}

// The level camera (core/vr/ViewLevel, research/24): rotator angles, the gate, the camera modes, the
// last-minute window, the change log and the recoil pulse.
void TestViewLevel() {
    namespace vl = mevr::vr::view_level;
    // Rotator components wrap: a slightly-down pitch is stored as a large positive number.
    CHECK(vl::Signed(0) == 0 && vl::Signed(100) == 100 && vl::Signed(65436) == -100 && vl::Signed(-100) == -100);
    CHECK(vl::Signed(32767) == 32767 && vl::Signed(32768) == -32768 && vl::Signed(65536 + 5) == 5);
    CHECK(vl::Units(90.0) == 16384 && vl::Units(-0.5) == -91 && std::abs(vl::Degrees(16384) - 90.0) < 1e-9);
    CHECK(vl::Correction(5461) == -5461 && vl::Correction(65536 - 200) == 200 && vl::Correction(0) == 0);

    // The gate: holds only on foot, with a head-tracked view, the hand aiming, the host's head aim off,
    // out of cover and off ladders, in an allowed camera mode; the first reason that applies is given.
    vl::Inputs in;
    in.headTrackedView = in.handAim = in.onFoot = in.pawn = in.cameraModeAllowed = true;
    CHECK(vl::Decide(in) == vl::Why::Holding);
    vl::Inputs t = in;
    t.enabled = false;
    t.headTrackedView = false;
    CHECK(vl::Decide(t) == vl::Why::Off);
    t = in;
    t.headTrackedView = false;
    CHECK(vl::Decide(t) == vl::Why::NoView);
    t = in;
    t.headAim = true;
    CHECK(vl::Decide(t) == vl::Why::HeadAim);
    t = in;
    t.handAim = false;
    CHECK(vl::Decide(t) == vl::Why::NoHandAim);
    t = in;
    t.onFoot = false;
    CHECK(vl::Decide(t) == vl::Why::NotOnFoot);
    t = in;
    t.pawn = false;
    CHECK(vl::Decide(t) == vl::Why::NoPawn);
    t = in;
    t.cover = true;
    CHECK(vl::Decide(t) == vl::Why::Cover);
    t = in;
    t.ladder = true;
    CHECK(vl::Decide(t) == vl::Why::Ladder);
    t = in;
    t.cameraModeAllowed = false;
    CHECK(vl::Decide(t) == vl::Why::CameraMode);
    for (vl::Why why : {vl::Why::Holding, vl::Why::Off, vl::Why::CameraMode}) {
        CHECK(std::string(vl::Name(why)) != "?");
    }

    // Camera modes: on-foot explore, combat, aimed fire and sprint hold; the scope, cover, the Mako,
    // photo and cinematic modes keep the game's pitch (exact names: VehicleTightAim isn't TightAim).
    for (char const* mode : {"SFXCameraMode_Explore", "SFXCameraMode_ExploreStorm", "SFXCameraMode_Combat",
             "SFXCameraMode_CombatStorm", "SFXCameraMode_TightAim"}) {
        CHECK(vl::CameraModeAllows(mode));
    }
    for (char const* mode : {"SFXCameraMode_SniperAim", "SFXCameraMode_HipAimCover", "SFXCameraMode_EnterCover",
             "SFXCameraMode_Vehicle", "SFXCameraMode_VehicleTightAim", "SFXCameraMode_VehicleTightAimEx", "SFXCameraMode_Photo",
             "SFXCameraMode_PhotoFree", "SFXCameraMode_Cinematic", "SFXCameraMode_Interpolate", "none", ""}) {
        CHECK(!vl::CameraModeAllows(mode));
    }

    // The window: a minute of one-second buckets. 30 seconds of rifle fire, a 0.1-degree kick per shot
    // (18 units) put back each tick: the game moved the pitch +540 units, the hold put back -540.
    vl::Window window;
    double now = 1000.25;
    int left = 0;
    for (int i = 0; i < 30; ++i) {
        window.Shot(now, 18);
        int const pitch = left + 18;
        int const delta = vl::Signed(pitch - left);
        int const correction = vl::Correction(pitch);
        window.Frame(now, pitch, 0, delta, correction, true);
        left = pitch + correction;
        now += 1.0;
    }
    vl::Summary s = window.Last(now - 1.0);
    CHECK(s.any && s.shots == 30 && s.recoil == 540 && s.gameDrift == 540 && s.corrected == -540);
    CHECK(s.minPitch == 18 && s.maxPitch == 18 && s.frames == 30 && s.heldFrames == 30 && std::abs(s.seconds - 30.0) < 1e-9);
    // The last 10 seconds only.
    s = window.Last(now - 1.0, 10.0);
    CHECK(s.shots == 10 && s.recoil == 180);
    // Without the hold the pitch climbs (as in the 2026-10-22 session): min and max as the game left it.
    window.Clear();
    CHECK(!window.Last(now).any);
    left = 0;
    for (int i = 0; i < 5; ++i) {
        int const pitch = left + 100;
        window.Frame(now, pitch, 0, vl::Signed(pitch - left), 0, false);
        left = pitch;
    }
    s = window.Last(now);
    CHECK(s.gameDrift == 500 && s.corrected == 0 && s.minPitch == 100 && s.maxPitch == 500 && s.heldFrames == 0);
    // A down pitch (stored wrapped) reads as negative.
    window.Frame(now, 65536 - 300, 65536 - 10, 0, 0, false);
    s = window.Last(now);
    CHECK(s.minPitch == -300 && s.minRoll == -10);
    // Old seconds drop out of the minute; a bucket reused for a later second starts empty.
    window.Shot(now, 50);
    s = window.Last(now + 61.0);
    CHECK(!s.any && s.shots == 0);
    window.Shot(now + 64.0, 7);  // the same bucket index as `now`
    s = window.Last(now + 64.0);
    CHECK(s.shots == 1 && s.recoil == 7);
    window.ZoomSnap(now + 64.0);
    CHECK(window.Last(now + 64.0).zoomSnaps == 1);

    // The change log: a line once the game's pitch moved 0.5 degrees (91 units) since the last one, at
    // most every second; shots and their recoil counted in between.
    vl::ChangeLog changes(91, 1.0);
    CHECK(!changes.Feed(0.0, 0, 0));
    changes.Shot(18);
    CHECK(!changes.Feed(0.1, 18, 0));
    for (int i = 0; i < 4; ++i) {
        changes.Shot(18);
        CHECK(!changes.Feed(0.2 + 0.1 * i, 18, 0));  // 90 units: not yet
    }
    changes.Shot(18);
    CHECK(!changes.Feed(0.7, 18, 0));  // 108 units, but under a second since the start
    CHECK(changes.Feed(1.2, 0, 0));
    vl::ChangeLog::Line line = changes.Take(1.2);
    CHECK(line.pitch == 108 && line.shots == 6 && line.recoil == 108 && std::abs(line.seconds - 1.2) < 1e-9);
    CHECK(!changes.Feed(3.0, 0, 0));  // nothing moved since
    // Put back and kicked again: the sum counts what the game did, both ways.
    CHECK(!changes.Feed(3.1, -60, 0));
    CHECK(changes.Feed(3.2, -40, 0));
    line = changes.Take(3.2);
    CHECK(line.pitch == -100 && line.shots == 0);
    // A roll is reported from and to.
    CHECK(changes.Feed(5.0, 0, 65536 - 200));
    line = changes.Take(5.0);
    CHECK(line.rollFrom == 0 && line.rollTo == -200 && line.pitch == 0);

    // The recoil pulse: a rifle's kick is a short tap, a shotgun's a longer thump, scaled; 0 = none.
    vl::Pulse const rifle = vl::RecoilPulse(0.1, 0.5);
    vl::Pulse const shotgun = vl::RecoilPulse(3.0, 0.5);
    CHECK(rifle.amplitude > 0.25f && rifle.amplitude < 0.35f && rifle.seconds < 0.04f);
    CHECK(std::abs(shotgun.amplitude - 0.5f) < 1e-6f && std::abs(shotgun.seconds - 0.07f) < 1e-6f);
    CHECK(vl::RecoilPulse(1.0, 0.0).amplitude == 0.0f && vl::RecoilPulse(1.0, -1.0).amplitude == 0.0f);
    CHECK(vl::RecoilPulse(0.0, 1.0).amplitude > 0.0f);  // a shot whose kick wasn't seen still taps
    CHECK(vl::RecoilPulse(10.0, 2.0).amplitude <= 1.0f);
}

// Aim assist's friction and adhesion removed under hand aim (core/vr/ViewLevel, research/24 section 7):
// the gate, the camera modes, and the window's counts of what they would have done.
void TestAimAssist() {
    namespace vl = mevr::vr::view_level;
    vl::AssistInputs in;
    in.headTrackedView = in.handAim = in.onFoot = in.pawn = in.cameraModeRemoves = true;
    CHECK(vl::DecideAssist(in) == vl::AssistWhy::Removed);
    vl::AssistInputs t = in;
    t.keep = true;  // [Hands] aimAssist = 1 wins over everything
    t.headTrackedView = false;
    CHECK(vl::DecideAssist(t) == vl::AssistWhy::Kept);
    t = in;
    t.headTrackedView = false;  // the flat game, VR off
    CHECK(vl::DecideAssist(t) == vl::AssistWhy::NoView);
    t = in;
    t.headAim = true;
    CHECK(vl::DecideAssist(t) == vl::AssistWhy::HeadAim);
    t = in;
    t.handAim = false;
    CHECK(vl::DecideAssist(t) == vl::AssistWhy::NoHandAim);
    t = in;
    t.onFoot = false;  // the Mako
    CHECK(vl::DecideAssist(t) == vl::AssistWhy::NotOnFoot);
    t = in;
    t.pawn = false;
    CHECK(vl::DecideAssist(t) == vl::AssistWhy::NoPawn);
    t = in;
    t.cameraModeRemoves = false;  // the sniper scope
    CHECK(vl::DecideAssist(t) == vl::AssistWhy::CameraMode);
    for (vl::AssistWhy why : {vl::AssistWhy::Removed, vl::AssistWhy::Kept, vl::AssistWhy::CameraMode}) {
        CHECK(std::string(vl::Name(why)) != "?");
    }

    // Camera modes: wherever the hand aims on foot (cover too); the sniper scope, the Mako, photo and
    // cinematic modes keep the game's (exact names).
    for (char const* mode : {"SFXCameraMode_Explore", "SFXCameraMode_ExploreStorm", "SFXCameraMode_Combat",
             "SFXCameraMode_CombatStorm", "SFXCameraMode_TightAim", "SFXCameraMode_EnterCover", "SFXCameraMode_HipAimCover"}) {
        CHECK(vl::CameraModeRemovesAssist(mode));
    }
    for (char const* mode : {"SFXCameraMode_SniperAim", "SFXCameraMode_Vehicle", "SFXCameraMode_VehicleTightAim",
             "SFXCameraMode_VehicleTightAimEx", "SFXCameraMode_Photo", "SFXCameraMode_PhotoFree", "SFXCameraMode_PhotoOrbit",
             "SFXCameraMode_Cinematic", "SFXCameraMode_Interpolate", "none", ""}) {
        CHECK(!vl::CameraModeRemovesAssist(mode));
    }

    // The window: friction counts only with a target in the soft margin (factor > 0), adhesion only a
    // non-zero turn (wrapped components read signed).
    vl::Window window;
    double const now = 500.5;
    window.Friction(now, 0.0, true, true);  // no target: nothing would have happened
    window.Friction(now, std::nan(""), true, true);
    window.Adhesion(now, 0, 65536, true);  // a whole turn is no turn
    vl::Summary s = window.Last(now);
    CHECK(s.frictionTicks == 0 && s.adhesionTicks == 0);
    window.Friction(now, 0.2, true, true);
    window.Friction(now, 0.6, false, true);
    window.Friction(now + 1.0, 0.4, true, false);
    window.Adhesion(now, 65536 - 30, 100, true);  // 30 units down, 100 right
    window.Adhesion(now + 1.0, 10, -50, false);
    s = window.Last(now + 1.0);
    CHECK(s.frictionTicks == 3 && s.frictionTurning == 2 && s.frictionRemoved == 2);
    CHECK(std::abs(s.frictionSum - 1.2) < 1e-9 && std::abs(s.frictionMax - 0.6) < 1e-9);
    CHECK(s.adhesionTicks == 2 && s.adhesionRemoved == 1 && s.adhesionYaw == 150 && s.adhesionPitch == 40);
    // They age out of the minute like the rest.
    s = window.Last(now + 70.0);
    CHECK(s.frictionTicks == 0 && s.adhesionTicks == 0 && s.frictionMax == 0.0);
}

// The left trigger belongs to the squad wheel per pull: decided at the press, kept until let go.
void TestTriggerOwner() {
    using namespace mevr::vr::squad_menu;
    TriggerOwner owner;
    CHECK(OwnTrigger(owner, false, true));     // up, on foot: the next pull is the squad's
    CHECK(OwnTrigger(owner, true, true));      // the press
    CHECK(OwnTrigger(owner, true, false));     // into the Mako mid-pull: still the squad's (no LB edge)
    CHECK(!OwnTrigger(owner, false, false));   // let go there: the game's LB from now on
    CHECK(!OwnTrigger(owner, true, false));    // a pull in the Mako: LB (boost)
    CHECK(!OwnTrigger(owner, true, true));     // out of the Mako mid-pull: still LB until let go
    CHECK(OwnTrigger(owner, false, true));     // let go on foot: the squad's again

    // What the pad shows while the squad has it (squad_weapons::AdjustPad clears leftTriggerShoulder and
    // leftTrigger): a pulled trigger is neither LB nor LT, with the wheel open or not; the support grip's
    // LT (aimed fire) still comes through.
    using namespace mevr::game::gamepad;
    mevr::vr::controllers::State state;
    state.active = true;
    state.left.trigger = 1.0f;
    Options owned;
    owned.gripShoulder = false;
    owned.leftTriggerShoulder = false;
    owned.leftTrigger = false;
    Latches latches;
    auto pad = FromControllers(state, latches, owned);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0 && pad.bLeftTrigger == 0);
    owned.nativeWheel = true;
    pad = FromControllers(state, latches, owned);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0 && pad.bLeftTrigger == 0);
    Options const across = FromFlags(0.05f, 0.18f, 0.26f, ToFlags(owned));
    CHECK(!across.leftTriggerShoulder && !across.leftTrigger && across.nativeWheel);
    // The same trigger in the Mako (not owned: [Controls] leftTrigger = lb): LB.
    Options mako;
    mako.gripShoulder = false;
    mako.leftTriggerShoulder = true;
    mako.leftTrigger = false;
    Latches makoLatches;
    pad = FromControllers(state, makoLatches, mako);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0 && pad.bLeftTrigger == 0);
    // A left grip squeezed away from the gun: nothing at all (no LB), whoever has the trigger.
    mevr::vr::controllers::State grip;
    grip.active = true;
    grip.left.squeeze = 1.0f;
    Options away = owned;
    away.nativeWheel = false;
    away.support = {0.5f, 0.18f, 0.26f};
    Latches gripLatches;
    pad = FromControllers(grip, gripLatches, away);
    CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0 && pad.bLeftTrigger == 0);
}

// The support grip's grab zone: a capsule along the gun, the captured span (2026-10-22).
void TestSupportZone() {
    using namespace mevr::ik;
    // The assault rifle as the 2026-10-22 log measured it, in the right hand's axes (units).
    Vec3 const barrel{0.190, -0.965, -0.178};
    Vec3 const muzzle{7.2, -48.6, 3.3};
    Vec3 const lhik{-3.8, -19.0, 3.4};
    Quat const rear{};
    Vec3 const at{0.0, 0.0, 0.0};
    auto const zone = MakeSupportZone(at, rear, barrel, muzzle, lhik, 1.0, 10.0, 10.0);
    CHECK(zone.has_value());
    if (zone) {
        Vec3 const b = Normalize(barrel);
        double const muzzleAlong = Dot(muzzle, b);
        CHECK(NearD(Dot(zone->start, b), 10.0, 1e-9));                        // 10 units ahead of the hand
        CHECK(Length(zone->end - (muzzle + b * 10.0)) < 1e-9);                // 10 units past the muzzle
        CHECK(Length(zone->foregrip - lhik) < 1e-9);
        // Both ends on the bore line (through the muzzle along the barrel).
        Vec3 const off = (zone->start - muzzle) - b * Dot(zone->start - muzzle, b);
        CHECK(Length(off) < 1e-9);
        // The foregrip itself, and a hand on the barrel near the muzzle: in reach of a 0.18 m radius at
        // 100 units per meter; under the old rule (0.12 m from the foregrip) the muzzle hand missed.
        CHECK(DistanceToZone(lhik, *zone) < 1e-9);
        Vec3 up = Vec3{0.0, 0.0, 1.0} - b * Dot(Vec3{0.0, 0.0, 1.0}, b);
        up = Normalize(up);
        Vec3 const underMuzzle = muzzle - b * 5.0 - up * 12.0;
        CHECK(DistanceToZone(underMuzzle, *zone) < 18.0);
        CHECK(Length(underMuzzle - lhik) > 22.0);
        // Halfway along, 15 units under the bore: in; 30 units to the side (away from the foregrip): out.
        Vec3 const side = Normalize(Cross(b, up));
        Vec3 const middle = muzzle - b * (0.5 * (muzzleAlong - 10.0));
        CHECK(DistanceToZone(middle - up * 15.0, *zone) < 15.5);
        CHECK(DistanceToZone(middle - side * 30.0, *zone) > 29.0);
        // Far past the muzzle (40 units): out; far behind the hand: out.
        CHECK(DistanceToZone(muzzle + b * 40.0, *zone) > 29.0);
        CHECK(DistanceToZone(b * -30.0, *zone) > 30.0);
    }
    // A scaled hand bone scales the gun; a rotated one turns it; the front never passes the muzzle.
    auto const scaled = MakeSupportZone(at, rear, barrel, muzzle, lhik, 2.0, 10.0, 10.0);
    CHECK(scaled && Length(scaled->foregrip - lhik * 2.0) < 1e-9);
    Quat const turned = FromTo({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    auto const rotated = MakeSupportZone({5.0, 6.0, 7.0}, turned, barrel, muzzle, lhik, 1.0, 10.0, 10.0);
    CHECK(rotated && Length(rotated->foregrip - (Vec3{5.0, 6.0, 7.0} + Rotate(turned, lhik))) < 1e-9);
    auto const shortGun = MakeSupportZone(at, rear, {1.0, 0.0, 0.0}, {8.0, 0.0, 0.0}, {4.0, 0.0, -6.0}, 1.0, 30.0, 5.0);
    CHECK(shortGun && Length(shortGun->start - Vec3{8.0, 0.0, 0.0}) < 1e-9 && Length(shortGun->end - Vec3{13.0, 0.0, 0.0}) < 1e-9);
    CHECK(!MakeSupportZone(at, rear, {0.0, 0.0, 0.0}, muzzle, lhik, 1.0, 10.0, 10.0));
    CHECK(!MakeSupportZone(at, rear, barrel, {std::nan(""), 0.0, 0.0}, lhik, 1.0, 10.0, 10.0));
    // Segment distances: inside, beyond either end, a degenerate segment.
    CHECK(NearD(DistanceToSegment({5.0, 3.0, 0.0}, {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}), 3.0, 1e-12));
    CHECK(NearD(DistanceToSegment({-4.0, 3.0, 0.0}, {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}), 5.0, 1e-12));
    CHECK(NearD(DistanceToSegment({13.0, 4.0, 0.0}, {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}), 5.0, 1e-12));
    CHECK(NearD(DistanceToSegment({3.0, 4.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}), 5.0, 1e-12));

    // The captured span: taking hold anywhere doesn't swing the gun; afterwards it turns with the hands.
    Quat const hand = FromTo({1.0, 0.0, 0.0}, {0.6, 0.8, 0.0});
    Vec3 const rearAt{1.0, 2.0, 3.0};
    Vec3 const grabbed = rearAt + Vec3{30.0, 10.0, -12.0};  // the left hand took hold here
    auto const captured = CapturedSpan(hand, rearAt, grabbed, Length(lhik), 8.0);
    CHECK(captured.has_value());
    if (captured) {
        CHECK(NearD(Length(*captured), Length(lhik), 1e-9));
        CHECK(Length(Normalize(Rotate(hand, *captured)) - Normalize(grabbed - rearAt)) < 1e-9);
        auto const same = TwoHandRotation(hand, *captured, rearAt, grabbed, 8.0);
        CHECK(same && AngleBetween(*same, hand) < 1e-6);   // no swing when taking hold
        auto const raised = TwoHandRotation(hand, *captured, rearAt, grabbed + Vec3{0.0, 0.0, 10.0}, 8.0);
        CHECK(raised && AngleBetween(*raised, hand) > 0.1);  // the left hand moves: the gun turns
        // The foregrip's own span would have swung it at once.
        auto const fromForegrip = TwoHandRotation(hand, lhik, rearAt, grabbed, 8.0);
        CHECK(fromForegrip && AngleBetween(*fromForegrip, hand) > 0.2);
    }
    CHECK(!CapturedSpan(hand, rearAt, rearAt + Vec3{3.0, 0.0, 0.0}, 17.0, 8.0));  // hands together: no line
    CHECK(!CapturedSpan(hand, rearAt, {std::nan(""), 0.0, 0.0}, 17.0, 8.0));

    // Through the pad mapping (host side, unchanged): the zone's distance against its radii.
    using namespace mevr::game::gamepad;
    SupportGrip grip;
    CHECK(UpdateSupportGrip(grip, true, SupportInput{0.15f, 0.18f, 0.26f}) == LeftGrip::Support);  // the muzzle hand
    CHECK(UpdateSupportGrip(grip, true, SupportInput{0.24f, 0.18f, 0.26f}) == LeftGrip::Support);  // kept within release
    CHECK(UpdateSupportGrip(grip, true, SupportInput{0.30f, 0.18f, 0.26f}) == LeftGrip::Held);     // let go past it
    grip = {};
    CHECK(UpdateSupportGrip(grip, true, SupportInput{0.20f, 0.18f, 0.26f}) == LeftGrip::Shoulder); // a new squeeze outside
}

// Reach scaling fades out near the eyes (a gun at the cheek sits on its controller).
void TestReachWeight() {
    using namespace mevr::ik;
    CHECK(NearD(ReachWeight(0.20, 0.30), 0.0, 1e-12));
    CHECK(NearD(ReachWeight(0.30, 0.30), 0.0, 1e-12));
    CHECK(NearD(ReachWeight(0.375, 0.30), 0.5, 1e-12));
    CHECK(NearD(ReachWeight(0.45, 0.30), 1.0, 1e-12));
    CHECK(NearD(ReachWeight(0.90, 0.30), 1.0, 1e-12));
    CHECK(ReachWeight(0.34, 0.30) < ReachWeight(0.40, 0.30));
    CHECK(NearD(ReachWeight(0.10, 0.0), 1.0, 1e-12));          // off
    CHECK(NearD(ReachWeight(std::nan(""), 0.30), 1.0, 1e-12));
    CHECK(NearD(ScaledReach(0.76, 0.0), 1.0, 1e-12));
    CHECK(NearD(ScaledReach(0.76, 1.0), 0.76, 1e-12));
    CHECK(NearD(ScaledReach(0.76, 0.5), 0.88, 1e-12));
    CHECK(NearD(ScaledReach(1.3, 2.0), 1.3, 1e-12));            // weight clamped
}

// The sight alignment assist: where the gun goes, and how it gets there.
void TestSightAssist() {
    namespace s = mevr::vr::sights;
    CHECK(s::Smooth(-1.0f) == 0.0f && s::Smooth(0.0f) == 0.0f && s::Smooth(1.0f) == 1.0f && s::Smooth(2.0f) == 1.0f);
    CHECK(std::abs(s::Smooth(0.5f) - 0.5f) < 1e-6f && s::Smooth(0.2f) < s::Smooth(0.3f));
    s::Assist settings;  // full strength, 0.10 m at most (a scope), 0.03 m for a dot
    CHECK(settings.maxShiftM == 0.10f && settings.dotMaxShiftM == 0.03f);
    XrVector3f const eyes[2] = {{-0.032f, 0.0f, 0.0f}, {0.032f, 0.0f, 0.0f}};
    XrVector3f const ahead{0.0f, 0.0f, -1.0f};
    XrVector3f const up{0.0f, 1.0f, 0.0f};
    // The sight 0.3 m ahead, its line 1 cm right of and 2 cm below the right eye, the gun fully raised (weight 1): the move
    // puts the line through that eye.
    XrVector3f const center{0.042f, -0.02f, -0.3f};
    s::AssistGoal goal = s::AssistTarget(settings, center, ahead, up, eyes, 1.0f, -1);
    CHECK(goal.eye == 1 && std::abs(goal.weight - 1.0f) < 1e-5f);
    CHECK(NearX(goal.shift, {-0.01f, 0.02f, 0.0f}, 1e-5));
    CHECK(std::abs(goal.along - 0.3f) < 1e-5f && std::abs(goal.x + 0.01f) < 1e-5f && std::abs(goal.y - 0.02f) < 1e-5f);
    XrVector3f const moved{center.x + goal.shift.x, center.y + goal.shift.y, center.z + goal.shift.z};
    s::Line const through = s::EyeLine(eyes[1], moved, ahead, up);
    CHECK(through.lateral < 1e-5f);
    // The weight is continuous (no cone to toggle around): half raised, half the way; strength scales it; the cap limits it.
    CHECK(NearX(s::AssistTarget(settings, center, ahead, up, eyes, 0.5f, -1).shift, {-0.005f, 0.01f, 0.0f}, 1e-5));
    float lastLength = 0.0f;
    bool increasing = true;
    for (int i = 0; i <= 20; ++i) {
        s::AssistGoal const g = s::AssistTarget(settings, center, ahead, up, eyes, static_cast<float>(i) / 20.0f, -1);
        float const length = std::sqrt(g.shift.x * g.shift.x + g.shift.y * g.shift.y);
        increasing = increasing && length >= lastLength - 1e-7f && length - lastLength < 0.0012f;  // no jumps
        lastLength = length;
    }
    CHECK(increasing);
    s::Assist half = settings;
    half.strength = 0.5f;
    CHECK(NearX(s::AssistTarget(half, center, ahead, up, eyes, 1.0f, -1).shift, {-0.005f, 0.01f, 0.0f}, 1e-5));
    s::Assist capped = settings;
    capped.maxShiftM = 0.01f;
    goal = s::AssistTarget(capped, center, ahead, up, eyes, 1.0f, -1);
    CHECK(std::abs(std::sqrt(goal.shift.x * goal.shift.x + goal.shift.y * goal.shift.y + goal.shift.z * goal.shift.z) - 0.01f) < 1e-5f);
    // Not raised: no move, the eye still reported; bad weights are none.
    goal = s::AssistTarget(settings, center, ahead, up, eyes, 0.0f, -1);
    CHECK(goal.eye == 1 && goal.weight == 0.0f && NearX(goal.shift, {0.0f, 0.0f, 0.0f}, 1e-9));
    CHECK(s::AssistTarget(settings, center, ahead, up, eyes, std::nanf(""), -1).weight == 0.0f);
    CHECK(std::abs(s::AssistTarget(settings, center, ahead, up, eyes, 3.0f, -1).weight - 1.0f) < 1e-6f);
    // The sight behind the eye, or far out at arm's length and beyond: no aiming eye.
    CHECK(s::AssistTarget(settings, {0.032f, 0.0f, 0.2f}, ahead, up, eyes, 1.0f, -1).eye == -1);
    CHECK(s::AssistTarget(settings, {0.032f, 0.0f, -1.8f}, ahead, up, eyes, 1.0f, -1).eye == -1);
    // Which eye: the nearer the line; the current one kept unless the other is clearly (1.5 degrees) nearer.
    XrVector3f const between{0.002f, 0.0f, -0.3f};
    CHECK(s::AssistTarget(settings, between, ahead, up, eyes, 1.0f, -1).eye == 1);
    CHECK(s::AssistTarget(settings, between, ahead, up, eyes, 1.0f, 0).eye == 0);
    CHECK(s::AssistTarget(settings, {0.032f, 0.0f, -0.3f}, ahead, up, eyes, 1.0f, 0).eye == 1);
    // Off: nothing.
    s::Assist off = settings;
    off.enabled = false;
    goal = s::AssistTarget(off, center, ahead, up, eyes, 1.0f, -1);
    CHECK(goal.eye == -1 && goal.weight == 0.0f);

    // The pull (research/21 sections 14-15): the eyepiece 0.235 m ahead of the right eye, on its line: the gun slides
    // 0.115 m back along the line, so the eyepiece ends 0.12 m in front of the eye; nothing sideways.
    s::Pull const pull{0.12f, 0.15f};
    goal = s::AssistTarget(settings, {0.032f, 0.0f, -0.235f}, ahead, up, eyes, 1.0f, -1, pull);
    CHECK(goal.eye == 1 && std::abs(goal.weight - 1.0f) < 1e-5f && std::abs(goal.pull - 0.115f) < 1e-5f);
    CHECK(NearX(goal.shift, {0.0f, 0.0f, 0.115f}, 1e-5));
    // With the sideways offset too: both, the pull along the line, the sideways move capped on its own.
    goal = s::AssistTarget(settings, {0.042f, -0.02f, -0.3f}, ahead, up, eyes, 1.0f, -1, pull);
    CHECK(NearX(goal.shift, {-0.01f, 0.02f, 0.15f}, 1e-5) && std::abs(goal.pull - 0.15f) < 1e-5f);  // 0.18 wanted: capped at 0.15
    // Never away from the eye: an eyepiece already nearer than the relief stays.
    goal = s::AssistTarget(settings, {0.032f, 0.0f, -0.08f}, ahead, up, eyes, 1.0f, -1, pull);
    CHECK(goal.pull == 0.0f && NearX(goal.shift, {0.0f, 0.0f, 0.0f}, 1e-6));
    // Weighted like the sideways move: half raised, half the pull.
    goal = s::AssistTarget(settings, {0.032f, 0.0f, -0.3f}, ahead, up, eyes, 0.5f, -1, pull);
    CHECK(std::abs(goal.pull - 0.5f * 0.15f) < 1e-5f);
    // No pull asked (a red dot): none.
    CHECK(s::AssistTarget(settings, {0.032f, 0.0f, -0.235f}, ahead, up, eyes, 1.0f, -1).pull == 0.0f);
    // The relief: at least the near plane (10 units at 100 per meter: 0.10 m) plus 2 cm; at 50 units per meter 0.22 m.
    CHECK(std::abs(s::ReliefFor(0.12f, 10.0f, 100.0f) - 0.12f) < 1e-6f && std::abs(s::ReliefFor(0.08f, 10.0f, 100.0f) - 0.12f) < 1e-6f);
    CHECK(std::abs(s::ReliefFor(0.12f, 10.0f, 50.0f) - 0.22f) < 1e-6f && s::ReliefFor(0.12f, 0.0f, 100.0f) == 0.12f);

    // The spring: from rest, never faster than 0.25 m/s, no overshoot, settles; back to zero likewise.
    s::AssistSpring spring;
    XrVector3f const target{0.05f, 0.0f, 0.0f};
    XrVector3f value = spring.Update(target, 0.011f);
    CHECK(value.x > 0.0f && value.x <= 0.25f * 0.011f + 1e-6f);
    float last = value.x;
    bool monotonic = true;
    bool overshoot = false;
    for (int i = 0; i < 300; ++i) {
        value = spring.Update(target, 0.011f);
        monotonic = monotonic && value.x >= last - 1e-7f;
        overshoot = overshoot || value.x > target.x + 1e-6f;
        last = value.x;
    }
    CHECK(monotonic && !overshoot && std::abs(value.x - 0.05f) < 1e-4f);
    for (int i = 0; i < 300; ++i) {
        value = spring.Update({0.0f, 0.0f, 0.0f}, 0.011f);
    }
    CHECK(std::abs(value.x) < 1e-4f);
    // A long stall (dt clamped to 0.15 s) still moves at most 0.25 m/s; a bad goal resets.
    spring.Reset();
    value = spring.Update({1.0f, 0.0f, 0.0f}, 5.0f);
    CHECK(value.x <= 0.25f * 0.15f + 1e-6f);
    value = spring.Update({std::nanf(""), 0.0f, 0.0f}, 0.011f);
    CHECK(value.x == 0.0f && spring.Value().x == 0.0f);
    // The ADS profile (Overlord's thermal transition: 24/s, at most 0.9 m/s): 0.35 m in well under a second, never faster.
    spring.Reset();
    bool tooFast = false;
    float previous = 0.0f;
    int ticks = 0;
    for (; ticks < 200 && std::abs(spring.Value().x - 0.35f) > 0.002f; ++ticks) {
        value = spring.Update({0.35f, 0.0f, 0.0f}, 0.011f, mevr::vr::ads::kFastSpring.frequency, mevr::vr::ads::kFastSpring.maxSpeed);
        tooFast = tooFast || value.x - previous > 0.9f * 0.011f + 1e-6f || value.x > 0.35f + 1e-6f;
        previous = value.x;
    }
    CHECK(!tooFast && ticks * 0.011f < 0.75f);
}

// The pull reaching the relief (research/21 section 15): the session's hold, the eyepiece 0.47 m ahead of the aiming eye
// on its line, with the assist measured on the gun without its own move each tick (as game/sights does). With section 14's
// 15 cm the eye stayed 0.32 m behind the eyepiece; with ADS's 35 cm and the fast spring it reaches the 0.12 m relief.
void TestSightPullReachesRelief() {
    namespace s = mevr::vr::sights;
    namespace ads = mevr::vr::ads;
    XrVector3f const eyes[2] = {{-0.032f, 0.0f, 0.0f}, {0.032f, 0.0f, 0.0f}};
    XrVector3f const ahead{0.0f, 0.0f, -1.0f};
    XrVector3f const up{0.0f, 1.0f, 0.0f};
    XrVector3f const unassisted{0.032f, 0.0f, -0.47f};
    auto const run = [&](float maxM, ads::SpringProfile profile, float seconds) {
        s::AssistSpring spring;
        s::Pull const pull{0.12f, maxM};
        for (float t = 0.0f; t < seconds; t += 0.011f) {
            s::AssistGoal const goal = s::AssistTarget(s::Assist{}, unassisted, ahead, up, eyes, 1.0f, 1, pull);
            spring.Update(goal.shift, 0.011f, profile.frequency, profile.maxSpeed);
        }
        XrVector3f const shift = spring.Value();
        XrVector3f const drawn{unassisted.x + shift.x, unassisted.y + shift.y, unassisted.z + shift.z};
        return s::EyeLine(eyes[1], drawn, ahead, up).along;
    };
    CHECK(std::abs(run(0.15f, ads::kNormalSpring, 3.0f) - 0.32f) < 0.002f);
    CHECK(std::abs(run(0.35f, ads::kFastSpring, 0.6f) - 0.12f) < 0.003f);
    // Further back than the cap reaches: the cap holds (0.60 m: 0.25 m from the eye).
    s::AssistSpring spring;
    for (int i = 0; i < 200; ++i) {
        spring.Update(s::AssistTarget(s::Assist{}, {0.032f, 0.0f, -0.60f}, ahead, up, eyes, 1.0f, 1, s::Pull{0.12f, 0.35f}).shift, 0.011f,
            ads::kFastSpring.frequency, ads::kFastSpring.maxSpeed);
    }
    CHECK(std::abs(spring.Value().z - 0.35f) < 0.002f);
    // The picture at the relief is the model's eyepiece itself (section 14): 2 cm radius at 0.12 m, 18.9 degrees.
    CHECK(std::abs(s::PulledFieldDeg(0.02f, 0.12f) - 18.92f) < 0.05f);
    CHECK(std::abs(s::PictureRadius(0.02f, 0.12f, s::PulledFieldDeg(0.02f, 0.12f)) - 0.02f) < 1e-5f);
}

// Aiming down the sight (core/vr/Ads, Overlord's corridor, dwell and hysteresis; research/21 section 15).
// The interaction prompt at the object (core/vr/InteractLabel, research/40).
void TestInteractLabel() {
    namespace il = mevr::vr::interact;
    auto close = [](double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; };
    // The band in stage pixels (384..896, 30..100), the stage across the frame's width: the measured 2344x2392 HUD frame.
    il::Rect const crop = il::DefaultCrop(2344, 2392);
    CHECK(close(crop.x, 0.3, 1e-6) && close(crop.width, 0.4, 1e-6));
    CHECK(close(crop.y, 30.0 * 2344.0 / 1280.0 / 2392.0, 1e-6) && close(crop.height, 70.0 * 2344.0 / 1280.0 / 2392.0, 1e-6));
    // The text measured on that frame (y 0.034..0.063 of its height) lies inside the band.
    CHECK(crop.y < 0.034f && crop.y + crop.height > 0.063f);
    CHECK(!il::DefaultCrop(0, 100).Valid());
    il::Pixels const px = il::ToPixels(crop, 2344, 2392);
    CHECK(px.x == 703 && px.width >= 937 && px.width <= 939 && px.y == 54 && px.height >= 128 && px.height <= 130);
    // A band past the frame is clipped, never empty.
    il::Pixels const edge = il::ToPixels({0.98f, 0.98f, 0.1f, 0.1f}, 100, 100);
    CHECK(edge.x + edge.width <= 100 && edge.y + edge.height <= 100 && edge.width >= 8 && edge.height >= 8);
    CHECK(il::ToPixels(crop, 4, 4).width == 0);

    // The quad keeps its angular height at any distance and faces the head.
    for (float distance : {0.5f, 2.0f, 8.0f}) {
        il::Quad quad;
        CHECK(il::Place({0.0f, 1.6f, -distance}, {0.0f, 1.6f, 0.0f}, 6.0f, 4.0f, quad));
        double const angle = 2.0 * std::atan(quad.height / 2.0 / distance) * 180.0 / 3.14159265358979;
        CHECK(close(angle, 6.0, 1e-3) && close(quad.width, quad.height * 4.0f, 1e-5));
        XrVector3f const front = mevr::vr::overlay::Rotate(quad.pose.orientation, {0.0f, 0.0f, 1.0f});
        CHECK(close(front.z, 1.0, 1e-5));  // +Z toward the head
        XrVector3f const up = mevr::vr::overlay::Rotate(quad.pose.orientation, {0.0f, 1.0f, 0.0f});
        CHECK(close(up.y, 1.0, 1e-5));  // no roll
    }
    il::Quad degenerate;
    CHECK(!il::Place({0.0f, 1.6f, 0.0f}, {0.0f, 1.6f, 0.0f}, 6.0f, 4.0f, degenerate));
    CHECK(!il::Place({0.0f, 1.6f, -1.0f}, {0.0f, 1.6f, 0.0f}, 6.0f, 0.0f, degenerate));

    // Above the bounds' top, the top capped over the middle, never far over the eyes.
    il::Point const crate = il::LabelPoint({10.0, 20.0, 30.0}, 20.0, 150.0, 8.0, 50.0, 10.0);
    CHECK(crate.x == 10.0 && crate.y == 20.0 && close(crate.z, 58.0, 1e-9));
    il::Point const door = il::LabelPoint({0.0, 0.0, 100.0}, 200.0, 150.0, 8.0, 50.0, 10.0);
    CHECK(close(door.z, 158.0, 1e-9));  // 100 + 50 + 8, under the eyes' 150 + 10 cap
    il::Point const tall = il::LabelPoint({0.0, 0.0, 140.0}, 45.0, 150.0, 8.0, 50.0, 10.0);
    CHECK(close(tall.z, 160.0, 1e-9));  // capped at the eyes + 10
    il::Point const high = il::LabelPoint({0.0, 0.0, 400.0}, 45.0, 150.0, 8.0, 50.0, 10.0);
    CHECK(close(high.z, 400.0, 1e-9));  // above the eyes already: its middle, not pulled down below it

    // Published labels age out.
    il::Reset();
    CHECK(!il::Latest().visible && il::Latest().size == 0);
    il::Label label;
    label.visible = true;
    label.serial = 7;
    il::Publish(label);
    il::Label const latest = il::Latest();
    CHECK(latest.visible && latest.serial == 7 && latest.size == sizeof(il::Label) && latest.sequence != 0);
    il::Reset();
}

// HUD pieces on the body (core/vr/HudPieces, research/41).
void TestMakoControls() {
    namespace mc = mevr::vr::mako_controls;
    namespace gp = mevr::game::gamepad;
    auto const close = [](double a, double b, double tolerance = 1e-3) { return std::abs(a - b) <= tolerance; };
    auto const about = [](XrVector3f const& axis, float degrees) {
        float const half = degrees * 3.14159265f / 360.0f;
        return XrQuaternionf{axis.x * std::sin(half), axis.y * std::sin(half), axis.z * std::sin(half), std::cos(half)};
    };
    auto const times = [](XrQuaternionf const& a, XrQuaternionf const& b) {
        return XrQuaternionf{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    };
    XrQuaternionf const identity{0.0f, 0.0f, 0.0f, 1.0f};
    // A grip held like a joystick: its -Z (the tube the fingers close around) straight up.
    XrQuaternionf const upright = about({1.0f, 0.0f, 0.0f}, 90.0f);
    {
        XrVector3f const axis = mc::StickAxis(upright);
        CHECK(close(axis.y, 1.0) && close(axis.x, 0.0) && close(axis.z, 0.0));
        // Swing-twist: a turn about the axis counts; a swing about a perpendicular axis doesn't; both at once: the twist.
        XrVector3f const up{0.0f, 1.0f, 0.0f};
        CHECK(close(mc::TwistDegrees(upright, times(about(up, 30.0f), upright), up), 30.0, 0.01));
        CHECK(close(mc::TwistDegrees(upright, times(about({1.0f, 0.0f, 0.0f}, 50.0f), upright), up), 0.0, 0.01));
        CHECK(close(mc::TwistDegrees(upright, times(about({0.0f, 0.0f, 1.0f}, 170.0f), upright), up), 0.0, 0.01));
        CHECK(close(mc::TwistDegrees(identity, about(up, -20.0f), up), -20.0, 0.01));
        // Both signs of one rotation's quaternion are the same twist.
        XrQuaternionf q = about(up, 25.0f);
        q = {-q.x, -q.y, -q.z, -q.w};
        CHECK(close(mc::TwistDegrees(identity, q, up), 25.0, 0.01));
    }
    mc::Settings settings;
    // The cases below are worked out for these (the first defaults); the defaults are the cockpit's grips now.
    CHECK(close(settings.throttleTravelM, 0.055) && close(settings.leverOffset.x, -0.362) && close(settings.stickOffset.z, 0.314));
    settings.throttleTravelM = 0.15f;
    settings.throttleDetent = 0.08f;  // the first default (0.15 since 2026-11-03)
    settings.leverReturns = false;    // the lever stayed where it was left until 2026-11-03
    settings.stickHeightM = 0.10f;    // the tilt cases below are worked out for the first height
    settings.leverOffset = {-0.20f, -0.55f, 0.25f};
    settings.stickOffset = {0.20f, -0.55f, 0.25f};
    CHECK(!settings.stickTwist);  // tilting is the default since 2026-11-03; the session below is the twist's
    mc::Settings tilting = settings;
    settings.stickTwist = true;
    mc::Settings inverted = settings;
    inverted.stickInvert = true;
    CHECK(close(mc::SteerFromTwist(0.0f, settings), 0.0));
    CHECK(close(mc::SteerFromTwist(3.9f, settings), 0.0));   // inside the dead zone
    CHECK(close(mc::SteerFromTwist(-42.0f, settings), 1.0));  // clockwise from above: right
    CHECK(close(mc::SteerFromTwist(42.0f, settings), -1.0));
    CHECK(close(mc::SteerFromTwist(120.0f, settings), -1.0));  // clamped
    CHECK(close(mc::SteerFromTwist(-23.0f, settings), 0.5));   // halfway between the dead zone and the lock
    CHECK(close(mc::SteerFromTwist(-42.0f, inverted), -1.0));
    CHECK(close(mc::ThrottleFromLever(0.05f, 0.08f), 0.0));
    CHECK(close(mc::ThrottleFromLever(-0.05f, 0.08f), 0.0));
    CHECK(close(mc::ThrottleFromLever(1.0f, 0.08f), 1.0));
    CHECK(close(mc::ThrottleFromLever(-1.0f, 0.08f), -1.0));
    CHECK(close(mc::ThrottleFromLever(0.54f, 0.08f), 0.5));
    // The tilt: degrees to -1..1 past the dead zone, full at stickTiltDeg; the hand's travel over the grip's height as a tilt.
    CHECK(close(mc::FromTilt(3.0f, tilting), 0.0));
    CHECK(close(mc::FromTilt(20.0f, tilting), 1.0) && close(mc::FromTilt(-35.0f, tilting), -1.0));
    CHECK(close(mc::FromTilt(12.0f, tilting), 0.5));
    {
        float rightDeg = 0.0f;
        float forwardDeg = 0.0f;
        mc::TiltFromTravel({0.10f, 0.0f, -0.10f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 0.10f, rightDeg, forwardDeg);
        CHECK(close(rightDeg, 45.0, 0.01) && close(forwardDeg, 45.0, 0.01));
        mc::TiltFromTravel({-0.0364f, 0.3f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 0.10f, rightDeg, forwardDeg);
        CHECK(close(rightDeg, -20.0, 0.05) && close(forwardDeg, 0.0));  // up and down tilt nothing
    }

    // Placement: from the head's position and flat yaw.
    XrPosef head{identity, {0.3f, 1.2f, -0.5f}};
    {
        mc::Seat seat;
        CHECK(mc::PlaceSeat(head, settings, seat));
        CHECK(close(seat.forward.z, -1.0) && close(seat.right.x, 1.0));
        CHECK(close(seat.leverBase.x, 0.1) && close(seat.leverBase.y, 0.65) && close(seat.leverBase.z, -0.75));
        CHECK(close(seat.stickBase.x, 0.5) && close(seat.stickBase.y, 0.65) && close(seat.stickBase.z, -0.75));
        // Looking down at an angle and turned 90 degrees left: the yaw alone counts.
        XrPosef turned = head;
        turned.orientation = times(about({0.0f, 1.0f, 0.0f}, 90.0f), about({1.0f, 0.0f, 0.0f}, -40.0f));
        CHECK(mc::PlaceSeat(turned, settings, seat));
        CHECK(close(seat.forward.x, -1.0) && close(seat.forward.y, 0.0) && close(seat.right.z, -1.0));
        // Straight down: no yaw to go by.
        XrPosef down = head;
        down.orientation = about({1.0f, 0.0f, 0.0f}, -90.0f);
        mc::Seat const untouched = seat;
        CHECK(!mc::PlaceSeat(down, settings, seat));
        CHECK(seat.forward.x == untouched.forward.x);
        // The picture's quad: its top edge along forward, its front up.
        XrPosef const flat = mc::FlatPose({1.0f, 2.0f, 3.0f}, {-1.0f, 0.0f, 0.0f});
        XrQuaternionf const q = flat.orientation;
        auto const rotate = [&](XrVector3f v) {
            XrQuaternionf const r = times(times(q, {v.x, v.y, v.z, 0.0f}), {-q.x, -q.y, -q.z, q.w});
            return XrVector3f{r.x, r.y, r.z};
        };
        XrVector3f const top = rotate({0.0f, 1.0f, 0.0f});
        XrVector3f const front = rotate({0.0f, 0.0f, 1.0f});
        CHECK(close(top.x, -1.0) && close(top.y, 0.0) && close(top.z, 0.0));
        CHECK(close(front.y, 1.0));
        CHECK(flat.position.y == 2.0f);
    }

    // A session: hands at the controls.
    mc::Seat seat;
    CHECK(mc::PlaceSeat(head, settings, seat));
    auto const input = [&](float leftForward, float leftSqueeze, XrQuaternionf const& rightTurn, float rightSqueeze, XrVector3f rightMove = {0.0f, 0.0f, 0.0f}) {
        mc::Input in;
        in.deltaSeconds = 0.05;
        in.headValid = true;
        in.head = head;
        in.left.tracked = true;
        in.left.grip.orientation = identity;
        in.left.grip.position = {seat.leverBase.x + 0.03f, seat.leverBase.y + 0.02f, seat.leverBase.z - leftForward};  // off by a little
        in.left.squeeze = leftSqueeze;
        in.right.tracked = true;
        in.right.grip.orientation = times(rightTurn, upright);
        in.right.grip.position = {seat.stickBase.x + 0.05f + rightMove.x, seat.stickBase.y - 0.02f + rightMove.y, seat.stickBase.z + rightMove.z};
        in.right.squeeze = rightSqueeze;
        return in;
    };
    XrVector3f const up{0.0f, 1.0f, 0.0f};
    {
        // The cockpit's models are the seat (Input::modelSeat): grabbed where they are drawn, not where the head would place them.
        mc::Controls c;
        mc::Seat model = seat;
        model.stickBase = {seat.stickBase.x + 0.3f, seat.stickBase.y, seat.stickBase.z};  // 30 cm right of the head's placement
        mc::Input in = input(0.0f, 0.0f, identity, 1.0f);  // the hand at the head's placement: out of the model's reach
        in.haveModelSeat = true;
        in.modelSeat = model;
        mc::Output out = c.Step(in, tilting);
        CHECK(out.placed && out.replaced && !out.stickHeld);
        CHECK(close(c.CurrentSeat().stickBase.x, model.stickBase.x));
        in.right.squeeze = 0.0f;
        c.Step(in, tilting);
        in.right.grip.position.x += 0.3f;  // to the model's stick
        in.right.squeeze = 1.0f;
        out = c.Step(in, tilting);
        CHECK(out.stickHeld && !out.replaced);
        model.stickBase.z -= 0.01f;  // the cockpit moves a little (a frame's lag): followed, no new placement
        in.modelSeat = model;
        out = c.Step(in, tilting);
        CHECK(out.stickHeld && !out.replaced && close(c.CurrentSeat().stickBase.z, model.stickBase.z));
    }
    {
        // The tilting stick: the hand's travel from the grab (the seat faces -Z, its right is +X), not its twist.
        mc::Controls c;
        mc::Output out = c.Step(input(0.0f, 0.0f, identity, 1.0f), tilting);
        CHECK(out.stickGrabbed && out.stickHeld && close(out.steer, 0.0) && close(out.pitch, 0.0));
        // The squeeze settling: for kGrabSettleS the stick's zero follows the hand, so a hand that shifts as it closes tilts nothing.
        out = c.Step(input(0.0f, 0.0f, identity, 1.0f, {0.0f, 0.0f, -0.03f}), tilting);
        CHECK(close(out.pitch, 0.0) && close(out.tiltForwardDeg, 0.0));
        for (int i = 0; i < 3; ++i) {
            out = c.Step(input(0.0f, 0.0f, identity, 1.0f), tilting);  // back where it grabbed: the zero follows it here
        }
        // A twist steers too, on top of the tilt (2026-11-03): counter-clockwise from above, past full lock, is full left.
        for (int i = 0; i < 4; ++i) {
            out = c.Step(input(0.0f, 0.0f, about(up, 60.0f), 1.0f), tilting);
        }
        CHECK(close(out.steer, -1.0) && close(out.tiltRightDeg, 0.0));
        for (int i = 0; i < 4; ++i) {
            out = c.Step(input(0.0f, 0.0f, identity, 1.0f), tilting);  // turned back
        }
        CHECK(close(out.steer, 0.0));
        for (int i = 0; i < 10; ++i) {
            out = c.Step(input(0.0f, 0.0f, identity, 1.0f, {0.05f, 0.0f, 0.0f}), tilting);  // 27 degrees right: past full lock
        }
        // Full tilt steers tiltSteerMax (half lock); the twist adds to it, and alone it is the spin.
        CHECK(close(out.tiltRightDeg, 20.0) && close(out.steer, 0.5) && close(out.pitch, 0.0) && close(out.spin, 0.0));
        for (int i = 0; i < 5; ++i) {
            out = c.Step(input(0.0f, 0.0f, about(up, -42.0f), 1.0f, {0.05f, 0.0f, 0.0f}), tilting);  // and twisted right to full
        }
        CHECK(close(out.steer, 1.0) && close(out.spin, 1.0));
        for (int i = 0; i < 5; ++i) {
            out = c.Step(input(0.0f, 0.0f, about(up, 23.0f), 1.0f, {0.05f, 0.0f, 0.0f}), tilting);  // twisted half left: against the tilt
        }
        CHECK(close(out.steer, 0.0) && close(out.spin, -0.5));
        for (int i = 0; i < 5; ++i) {
            out = c.Step(input(0.0f, 0.0f, identity, 1.0f, {-0.0182f, 0.0f, 0.0f}), tilting);  // about 10 degrees left, no twist
        }
        CHECK(out.steer < -0.15f && out.steer > -0.25f && close(out.spin, 0.0));
        // Pushed away from the player (the seat's forward, -Z): the pitch is + (the nose down), steering back to centre.
        for (int i = 0; i < 10; ++i) {
            out = c.Step(input(0.0f, 0.0f, identity, 1.0f, {0.0f, 0.0f, -0.05f}), tilting);
        }
        CHECK(close(out.pitch, 1.0) && close(out.tiltForwardDeg, 20.0) && close(out.steer, 0.0));
        out = c.Step(input(0.0f, 0.0f, identity, 1.0f, {0.0f, 0.0f, 0.05f}), tilting);
        CHECK(close(out.pitch, -1.0));
        // Let go: it springs back to centre.
        for (int i = 0; i < 20; ++i) {
            out = c.Step(input(0.0f, 0.0f, identity, 0.0f, {0.0f, 0.0f, 0.05f}), tilting);
        }
        CHECK(!out.stickHeld && close(out.pitch, 0.0) && close(out.tiltForwardDeg, 0.0) && close(out.steer, 0.0));
        // A small forward tilt (a hand drifting while it steers) pitches nothing: inside stickPitchDeadZoneDeg.
        for (int i = 0; i < 5; ++i) {
            out = c.Step(input(0.0f, 0.0f, identity, 1.0f), tilting);  // grabbed again, and settled
        }
        out = c.Step(input(0.0f, 0.0f, identity, 1.0f, {0.0f, 0.0f, -0.012f}), tilting);  // about 7 degrees
        CHECK(close(out.pitch, 0.0) && out.tiltForwardDeg > 6.0f);
        // Tilting with the lever just past its notch steers as the tank does (the throttle stays); twisting there turns on the spot:
        // the throttle sent is exactly 0.
        out = c.Step(input(0.0f, 1.0f, identity, 1.0f, {0.05f, 0.0f, 0.0f}), tilting);  // the lever grabbed at neutral
        for (int i = 0; i < 5; ++i) {
            out = c.Step(input(0.03f, 1.0f, identity, 1.0f, {0.05f, 0.0f, 0.0f}), tilting);  // then 0.2 of its travel
        }
        CHECK(out.leverHeld && out.lever > 0.15f && close(out.steer, 0.5) && out.throttle > 0.0f);
        for (int i = 0; i < 5; ++i) {
            out = c.Step(input(0.03f, 1.0f, about(up, -42.0f), 1.0f, {0.05f, 0.0f, 0.0f}), tilting);
        }
        CHECK(close(out.spin, 1.0) && out.throttle == 0.0f);
    }
    {
        mc::Controls c;
        // Near, no grip: not driving, nothing goes to the game's physics.
        mc::Output out = c.Step(input(0.0f, 0.0f, identity, 0.0f), settings);
        CHECK(out.placed && out.replaced && out.leverNear && out.stickNear && !out.driving && !out.leverHeld && !out.stickHeld);
        CHECK(close(out.throttle, 0.0) && close(out.steer, 0.0));
        // A grab never jerks: off the spot and with the hand turned any way.
        XrQuaternionf const turned = about(up, 37.0f);
        out = c.Step(input(0.0f, 1.0f, turned, 1.0f), settings);
        CHECK(out.driving && out.leverGrabbed && out.stickGrabbed && out.leverHeld && out.stickHeld);
        CHECK(close(out.throttle, 0.0) && close(out.steer, 0.0) && close(out.twistDeg, 0.0));
        CHECK(out.hapticCount == 2);
        // The arm swings and moves with no twist: nothing steers. (The grab pose was `turned`.)
        out = c.Step(input(0.0f, 1.0f, times(about({1.0f, 0.0f, 0.0f}, 35.0f), turned), 1.0f, {0.2f, 0.1f, -0.2f}), settings);
        CHECK(close(out.steer, 0.0) && std::abs(out.twistDeg) < 4.0f);
        out = c.Step(input(0.0f, 1.0f, turned, 1.0f), settings);
        CHECK(close(out.twistDeg, 0.0, 0.1));
        // Twist clockwise from above to the limit: full right, a buzz at the lock; more clamps.
        for (int i = 0; i < 10; ++i) {
            out = c.Step(input(0.0f, 1.0f, times(about(up, -42.0f), turned), 1.0f), settings);
        }
        CHECK(close(out.steer, 1.0));
        out = c.Step(input(0.0f, 1.0f, times(about(up, -80.0f), turned), 1.0f), settings);
        CHECK(close(out.steer, 1.0));
        // A wrap past half a turn doesn't flip the sign.
        for (float twist : {-120.0f, -170.0f, -190.0f, -230.0f}) {
            out = c.Step(input(0.0f, 1.0f, times(about(up, twist), turned), 1.0f), settings);
            CHECK(out.steer > 0.99f);
            CHECK(out.twistDeg < -100.0f);
        }
        // Back through the wrap to the other lock.
        for (float twist : {-170.0f, -120.0f, -60.0f, 0.0f, 60.0f}) {
            out = c.Step(input(0.0f, 1.0f, times(about(up, twist), turned), 1.0f), settings);
        }
        for (int i = 0; i < 10; ++i) {
            out = c.Step(input(0.0f, 1.0f, times(about(up, 60.0f), turned), 1.0f), settings);
        }
        CHECK(close(out.steer, -1.0));

        // The lever: pushed forward half its travel past the notch, it follows the hand along the seat's forward only.
        out = c.Step(input(0.08f, 1.0f, turned, 1.0f, {0.0f, 0.0f, 0.0f}), settings);
        CHECK(close(out.lever, 0.08f / 0.15f));
        CHECK(close(out.throttle, (0.08 / 0.15 - 0.08) / 0.92));
        // Independence: the lever moved, the steering didn't, and vice versa.
        CHECK(out.steer < 0.0f);
        // Letting go: the stick springs back, the throttle stays.
        float const held = out.throttle;
        out = c.Step(input(0.08f, 0.0f, turned, 1.0f), settings);
        CHECK(!out.leverHeld && out.stickHeld && close(out.throttle, held));
        for (int i = 0; i < 10; ++i) {
            out = c.Step(input(0.0f, 0.0f, turned, 0.0f), settings);  // the hands wander off; it stays
        }
        CHECK(!out.stickHeld && close(out.steer, 0.0) && close(out.throttle, held) && out.driving);
        // A squeeze far from a control grabs nothing; moving in with it held doesn't either.
        mc::Input away = input(0.08f, 0.0f, identity, 0.0f);
        away.left.grip.position.x += 1.0f;
        away.left.squeeze = 1.0f;
        away.right.grip.position.x += 1.0f;
        away.right.squeeze = 1.0f;
        out = c.Step(away, settings);
        CHECK(!out.leverHeld && !out.stickHeld && !out.leverNear && !out.stickNear);
        out = c.Step(input(0.08f, 1.0f, identity, 1.0f), settings);
        CHECK(!out.leverHeld && !out.stickHeld && out.leverNear && out.stickNear);
        out = c.Step(input(0.08f, 0.0f, identity, 0.0f), settings);
        // The grab point moves with the lever: re-grabbing where it was left doesn't move it, pulling back through
        // the notch gives a tick.
        out = c.Step(input(0.08f, 1.0f, identity, 0.0f), settings);
        CHECK(out.leverHeld && close(out.throttle, held));
        out = c.Step(input(0.0f, 1.0f, identity, 0.0f), settings);
        CHECK(close(out.lever, 0.0) && close(out.throttle, 0.0));
        bool ticked = false;
        for (int i = 0; i < out.hapticCount; ++i) {
            ticked = ticked || (out.haptics[i].hand == 0 && out.haptics[i].seconds < 0.05f);
        }
        CHECK(ticked);
        // Full reverse, clamped: no wind-up past the end.
        out = c.Step(input(-0.5f, 1.0f, identity, 0.0f), settings);
        CHECK(close(out.throttle, -1.0));
        out = c.Step(input(-0.5f + 0.15f, 1.0f, identity, 0.0f), settings);
        CHECK(close(out.lever, 0.0));
    }
    // Every safety stop: throttle and steering to zero, the holds gone, the drive over until the next grab.
    for (mc::Stop const stop : {mc::Stop::LeftMako, mc::Stop::Menu, mc::Stop::Calibration, mc::Stop::Tracking, mc::Stop::Level, mc::Stop::Unload}) {
        mc::Controls c;
        c.Step(input(0.0f, 0.0f, identity, 0.0f), settings);
        c.Step(input(0.0f, 1.0f, identity, 1.0f), settings);
        mc::Output out{};
        for (int i = 0; i < 5; ++i) {
            out = c.Step(input(0.1f, 1.0f, about(up, -30.0f), 1.0f), settings);
        }
        CHECK(out.driving && out.throttle > 0.3f && out.steer > 0.3f);
        mc::Input in = input(0.1f, 1.0f, about(up, -30.0f), 1.0f);
        in.stop = stop;
        out = c.Step(in, settings);
        CHECK(out.stopped == stop);
        CHECK(!out.driving && out.throttle == 0.0f && out.steer == 0.0f && out.lever == 0.0f && !out.leverHeld && !out.stickHeld);
        // Once: the next stop step reports nothing new.
        out = c.Step(in, settings);
        CHECK(out.stopped == mc::Stop::None && !out.driving);
        // Afterwards the grip still held isn't a new squeeze: nothing moves until it's let go and squeezed again.
        out = c.Step(input(0.1f, 1.0f, about(up, -30.0f), 1.0f), settings);
        CHECK(!out.driving && !out.leverHeld && !out.stickHeld && out.throttle == 0.0f);
        out = c.Step(input(0.0f, 0.0f, identity, 0.0f), settings);
        out = c.Step(input(0.0f, 1.0f, identity, 1.0f), settings);
        CHECK(out.driving && out.leverHeld && out.stickHeld && close(out.throttle, 0.0) && close(out.steer, 0.0));
    }
    // Tracking: a moment of it lost keeps the drive; over the grace it stops.
    {
        mc::Controls c;
        c.Step(input(0.0f, 0.0f, identity, 0.0f), settings);
        c.Step(input(0.0f, 1.0f, identity, 1.0f), settings);
        mc::Output out = c.Step(input(0.1f, 1.0f, identity, 1.0f), settings);
        float const before = out.throttle;
        CHECK(before > 0.3f);
        mc::Input lost = input(0.5f, 1.0f, identity, 1.0f);
        lost.left.tracked = false;
        for (int i = 0; i < 8; ++i) {  // 0.4 s
            out = c.Step(lost, settings);
            CHECK(out.driving && close(out.throttle, before));  // an untracked hand moves nothing
        }
        for (int i = 0; i < 4; ++i) {  // past 0.5 s
            out = c.Step(lost, settings);
        }
        CHECK(!out.driving && out.throttle == 0.0f);
    }
    // A recenter: placed again, the holds let go, the lever where it was.
    {
        mc::Controls c;
        mc::Input in = input(0.0f, 0.0f, identity, 0.0f);
        in.haveRecenters = true;
        in.recenters = 3;
        c.Step(in, settings);
        in = input(0.0f, 1.0f, identity, 1.0f);
        in.haveRecenters = true;
        in.recenters = 3;
        c.Step(in, settings);
        in = input(0.1f, 1.0f, identity, 1.0f);
        in.haveRecenters = true;
        in.recenters = 3;
        mc::Output out = c.Step(in, settings);
        float const before = out.throttle;
        CHECK(!out.replaced && before > 0.3f);
        in.recenters = 4;
        in.head.position.x += 1.0f;
        out = c.Step(in, settings);
        CHECK(out.replaced && !out.leverHeld && !out.stickHeld && out.driving && close(out.throttle, before));
        CHECK(close(c.CurrentSeat().leverBase.x, seat.leverBase.x + 1.0f));
        in.recenters = 4;
        out = c.Step(in, settings);
        CHECK(!out.replaced);
    }
    // The thumbstick's turret aim, while the stick is held; seeded at the grab.
    {
        float yaw = 170.0f;
        float pitch = 0.0f;
        mc::StepAim(yaw, pitch, 1.0f, 0.0f, 90.0f, 0.25);  // 22.5 degrees: wraps
        CHECK(close(yaw, -167.5, 0.01) && close(pitch, 0.0));
        mc::StepAim(yaw, pitch, 0.1f, 0.1f, 90.0f, 0.25);  // inside the dead zone
        CHECK(close(yaw, -167.5, 0.01) && close(pitch, 0.0));
        for (int i = 0; i < 20; ++i) {
            mc::StepAim(yaw, pitch, 0.0f, 1.0f, 90.0f, 0.25);
        }
        CHECK(close(pitch, mc::kAimPitchMaxDeg));
        mc::Controls c;
        c.Step(input(0.0f, 0.0f, identity, 0.0f), settings);
        c.SeedAim(20.0f, -5.0f);
        mc::Input in = input(0.0f, 0.0f, identity, 1.0f);
        in.stickX = 1.0f;
        mc::Output out = c.Step(in, settings);
        CHECK(out.stickGrabbed && close(out.aimYawDeg, 24.5, 0.01) && close(out.aimPitchDeg, -5.0));
        in.right.squeeze = 0.0f;
        out = c.Step(in, settings);  // let go: it no longer moves
        CHECK(close(out.aimYawDeg, 24.5, 0.01));
    }
    // The rumble: only in a holding hand, rising with the engine's speed.
    {
        mc::Controls c;
        c.Step(input(0.0f, 0.0f, identity, 0.0f), settings);
        mc::Input in = input(0.0f, 1.0f, identity, 0.0f);
        in.engineSpeed = 0.5f;
        mc::Output out = c.Step(in, settings);
        CHECK(close(out.rumbleLeft, 0.06) && out.rumbleRight == 0.0f);
    }
    // The picture changes only when it should.
    CHECK(mc::LeverKey(0.5f, 0.08f, false, false) == mc::LeverKey(0.5001f, 0.08f, false, false));
    CHECK(mc::LeverKey(0.5f, 0.08f, false, false) != mc::LeverKey(0.6f, 0.08f, false, false));
    CHECK(mc::LeverKey(0.5f, 0.08f, false, false) != mc::LeverKey(0.5f, 0.08f, true, false));
    CHECK(mc::StickKey(-0.5f, false, false) != mc::StickKey(0.5f, false, false));
    CHECK(mc::LeverPixels(0.0f, 0.15f, 0.08f, false, false).size() == size_t{mc::kLeverPixelsWide} * mc::kLeverPixelsLong);
    CHECK(mc::StickPixels(0.0f, false, true).size() == size_t{mc::kStickPixels} * mc::kStickPixels);

    // The pad mapping's side.
    {
        CHECK(gp::FromFlags(-1.0f, 0.1f, 0.2f, gp::kFlagMakoDrive).makoDrive);
        gp::Options options;
        options.makoLever = true;
        options.makoStick = true;
        gp::Options const back = gp::FromFlags(-1.0f, 0.1f, 0.2f, gp::ToFlags(options));
        CHECK(back.makoLever && back.makoStick && !back.makoDrive && !back.cockpitLook);
        options.cockpitLook = true;
        CHECK(gp::FromFlags(-1.0f, 0.1f, 0.2f, gp::ToFlags(options)).cockpitLook);
        CHECK((gp::kFlagCockpitLook & (gp::kRbSourceMask | gp::kWheelStickMask | gp::kFlagMakoStick)) == 0);

        mevr::vr::controllers::State state;
        state.active = true;
        state.timeMs = 1000;
        gp::Latches latches;
        // Without the flags: as always.
        state.left.stickY = 0.8f;
        state.left.primary = true;
        state.left.secondary = true;
        state.right.primary = true;
        state.right.stickX = 0.6f;
        gp::Options plain;
        XINPUT_GAMEPAD pad = gp::FromControllers(state, latches, plain);
        CHECK(pad.sThumbLY > 0 && pad.sThumbRX > 0);
        CHECK((pad.wButtons & XINPUT_GAMEPAD_X) != 0 && (pad.wButtons & XINPUT_GAMEPAD_Y) != 0 && (pad.wButtons & XINPUT_GAMEPAD_A) != 0);
        CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) == 0);
        // A press keeps its meaning: those already down don't change when the controls are taken.
        gp::Options driving;
        driving.makoDrive = driving.makoLever = driving.makoStick = true;
        pad = gp::FromControllers(state, latches, driving);
        CHECK(pad.sThumbLX == 0 && pad.sThumbLY == 0 && pad.sThumbRX == 0 && pad.sThumbRY == 0);
        // ...except that while the mod drives, X and Y reach the game only as the lever's, and the right A only as the stick's
        // cannon: never the game's jump (both since 2026-11-03).
        CHECK((pad.wButtons & (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_A)) == 0);
        {
            // The left trigger too: driving with the lever let go, it reaches the game as nothing; on the lever, as itself.
            gp::Latches other;
            mevr::vr::controllers::State held = state;
            held.left.trigger = 1.0f;
            gp::Options off;
            off.leftTrigger = true;
            off.makoDrive = true;
            CHECK(gp::FromControllers(held, other, off).bLeftTrigger == 0);
            off.makoLever = true;
            CHECK(gp::FromControllers(held, other, off).bLeftTrigger > 0);
            // With leftTrigger = lb (the default) the trigger is LB, the Mako's boost: the same rule (2026-11-04).
            mevr::vr::controllers::State trigger = state;
            trigger.left.primary = trigger.left.secondary = trigger.right.primary = false;
            trigger.left.trigger = 1.0f;
            gp::Latches lbLatches;
            gp::Options lb;
            lb.leftTrigger = lb.leftTriggerShoulder = true;
            lb.makoDrive = true;
            CHECK((gp::FromControllers(trigger, lbLatches, lb).wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0);
            lb.makoLever = true;
            CHECK((gp::FromControllers(trigger, lbLatches, lb).wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
            // Not driving (on foot, or the Mako's own controls): the trigger is LB as always.
            gp::Latches footLatches;
            gp::Options foot;
            foot.leftTrigger = foot.leftTriggerShoulder = true;
            CHECK((gp::FromControllers(trigger, footLatches, foot).wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
        }
        // New presses while held: X jumps, Y boosts, A fires the cannon; the trigger stays the gun.
        state.left.primary = state.left.secondary = state.right.primary = false;
        pad = gp::FromControllers(state, latches, driving);
        CHECK((pad.wButtons & (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_A)) == 0);
        state.left.primary = true;
        state.right.trigger = 1.0f;
        pad = gp::FromControllers(state, latches, driving);
        CHECK((pad.wButtons & XINPUT_GAMEPAD_A) != 0 && (pad.wButtons & XINPUT_GAMEPAD_X) == 0 && pad.bRightTrigger == 255);
        state.left.primary = false;
        state.left.secondary = true;
        pad = gp::FromControllers(state, latches, driving);
        CHECK((pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0 && (pad.wButtons & XINPUT_GAMEPAD_Y) == 0);
        state.left.secondary = false;
        state.right.primary = true;
        pad = gp::FromControllers(state, latches, driving);
        CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0 && (pad.wButtons & XINPUT_GAMEPAD_A) == 0);
        // Letting go of the control mid-press: still the cannon until released; then A again.
        pad = gp::FromControllers(state, latches, plain);
        CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0 && (pad.wButtons & XINPUT_GAMEPAD_A) == 0);
        state.right.primary = false;
        pad = gp::FromControllers(state, latches, plain);
        state.right.primary = true;
        pad = gp::FromControllers(state, latches, plain);
        CHECK((pad.wButtons & XINPUT_GAMEPAD_A) != 0 && (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) == 0);
        // The thumbstick pushed at the moment the hand lets go stays the turret's until centred.
        state.right.primary = false;
        state.right.stickX = 0.9f;
        pad = gp::FromControllers(state, latches, driving);
        CHECK(pad.sThumbRX == 0);
        pad = gp::FromControllers(state, latches, plain);
        CHECK(pad.sThumbRX == 0);
        state.right.stickX = 0.0f;
        pad = gp::FromControllers(state, latches, plain);
        state.right.stickX = 0.9f;
        pad = gp::FromControllers(state, latches, plain);
        CHECK(pad.sThumbRX > 0);
        // A squeeze at a control isn't RB, and stays off it while kept (the holsters' seam).
        gp::Latches grip;
        gp::Options atControl;
        atControl.rightGripFree = true;
        state.right.squeeze = 1.0f;
        pad = gp::FromControllers(state, grip, atControl);
        CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) == 0);
        pad = gp::FromControllers(state, grip, plain);
        CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) == 0);
        state.right.squeeze = 0.0f;
        gp::FromControllers(state, grip, plain);
        state.right.squeeze = 1.0f;
        pad = gp::FromControllers(state, grip, plain);
        CHECK((pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);  // away from it: the cannon, as today
    }
}

void TestHudPieces() {
    namespace hp = mevr::vr::hud_pieces;
    auto close = [](double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; };
    // The bands are anchored to the frame's bottom: the same stage offsets on the 2344x2392 frame (1306 stage px tall)
    // and the 1536x1594 one (1328 tall), where the pieces sit 22 stage px lower.
    for (auto const [w, h] : {std::pair{2344u, 2392u}, std::pair{1536u, 1594u}, std::pair{1059u, 1080u}}) {
        double const scale = w / 1280.0;  // frame px per stage px
        double const stageH = h / scale;
        hp::Rect const wrist = hp::DefaultWristCrop(w, h);
        CHECK(close(wrist.x, 440.0 / 1280.0, 1e-6) && close(wrist.width, 400.0 / 1280.0, 1e-6));
        CHECK(close(wrist.y * stageH, stageH - 165.0, 0.01) && close((wrist.y + wrist.height) * stageH, stageH - 25.0, 0.01));
        hp::Rect const gun = hp::DefaultGunCrop(w, h);
        CHECK(close(gun.x, 56.0 / 1280.0, 1e-6) && close(gun.width, 168.0 / 1280.0, 1e-6));
        CHECK(close(gun.y * stageH, stageH - 145.0, 0.01) && close((gun.y + gun.height) * stageH, stageH - 10.0, 0.01));
        // The measured content (alpha > 40: the arc x 459..817, the bottom 152..34; the weapon panel x 69..200, the bottom
        // 133..40) lies inside each band, and the bands stay clear of each other and of the radar (x 1100..1212).
        CHECK(wrist.x * 1280.0 < 459.0 && (wrist.x + wrist.width) * 1280.0 > 817.0);
        CHECK(gun.x * 1280.0 < 69.0 && (gun.x + gun.width) * 1280.0 > 200.0 && (gun.x + gun.width) * 1280.0 < wrist.x * 1280.0);
        CHECK((wrist.x + wrist.width) * 1280.0 < 1100.0);
        hp::Pixels const px = hp::ToPixels(wrist, w, h);
        CHECK(px.width >= 8 && px.height >= 8 && px.x + px.width <= w && px.y + px.height <= h);
    }
    CHECK(!hp::DefaultWristCrop(0, 100).Valid() && !hp::DefaultGunCrop(100, 0).Valid());
    // On the 2344x2392 frame the status band is pixels (806, 2090) 732 x 257 or so.
    hp::Pixels const status = hp::ToPixels(hp::DefaultWristCrop(2344, 2392), 2344, 2392);
    CHECK(status.x == 805 || status.x == 806);
    CHECK(status.y >= 2089 && status.y <= 2091 && status.y + status.height >= 2345 && status.y + status.height <= 2347);

    // A hand-set crop wins over the default.
    hp::Settings byHand;
    byHand.gunCrop = {0.1f, 0.8f, 0.2f, 0.1f};
    CHECK(hp::GunCrop(byHand, 2344, 2392).x == 0.1f && close(hp::WristCrop(byHand, 2344, 2392).x, 440.0 / 1280.0, 1e-6));

    // The quad keeps its metric width, takes its height from the band's aspect, faces the head and stands upright.
    hp::Quad quad;
    CHECK(hp::Place({0.3f, 1.2f, -0.4f}, {0.0f, 1.6f, 0.0f}, 0.08f, 2.5f, quad));
    CHECK(close(quad.width, 0.08, 1e-6) && close(quad.height, 0.032, 1e-6));
    XrVector3f const front = mevr::vr::overlay::Rotate(quad.pose.orientation, {0.0f, 0.0f, 1.0f});
    XrVector3f const toHead{-0.3f, 0.4f, 0.4f};
    float const len = std::sqrt(toHead.x * toHead.x + toHead.y * toHead.y + toHead.z * toHead.z);
    CHECK(close(front.x, toHead.x / len, 1e-5) && close(front.y, toHead.y / len, 1e-5) && close(front.z, toHead.z / len, 1e-5));
    XrVector3f const right = mevr::vr::overlay::Rotate(quad.pose.orientation, {1.0f, 0.0f, 0.0f});
    CHECK(close(right.y, 0.0, 1e-5));  // no roll: the quad's X stays level
    // Leaning with the gun's top: a gun rolled 90 degrees (its top to the right) rolls the quad with it.
    hp::Quad rolled;
    CHECK(hp::Place({0.0f, 1.2f, -0.5f}, {0.0f, 1.2f, 0.0f}, 0.06f, 1.2f, rolled, {1.0f, 0.0f, 0.0f}));
    XrVector3f const up = mevr::vr::overlay::Rotate(rolled.pose.orientation, {0.0f, 1.0f, 0.0f});
    CHECK(close(up.x, 1.0, 1e-5));
    // An up along the facing falls back to the world's.
    hp::Quad along;
    CHECK(hp::Place({0.0f, 1.2f, -0.5f}, {0.0f, 1.2f, 0.0f}, 0.06f, 1.2f, along, {0.0f, 0.0f, 1.0f}));
    CHECK(close(mevr::vr::overlay::Rotate(along.pose.orientation, {0.0f, 1.0f, 0.0f}).y, 1.0, 1e-5));
    hp::Quad degenerate;
    CHECK(!hp::Place({0.0f, 1.6f, 0.0f}, {0.0f, 1.6f, 0.0f}, 0.08f, 2.5f, degenerate));
    CHECK(!hp::Place({0.0f, 1.6f, -1.0f}, {0.0f, 1.6f, 0.0f}, 0.0f, 2.5f, degenerate));

    // The wrist point: the offset in the grip's axes. An identity grip adds it as it is; a grip turned 90 degrees about
    // Y (its -Z now pointing -X) carries the offset round with it.
    XrPosef grip{{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, -0.5f}};
    XrVector3f const plain = hp::WristPoint(grip, {-0.03f, 0.06f, 0.05f});
    CHECK(close(plain.x, 0.97, 1e-6) && close(plain.y, 1.06, 1e-6) && close(plain.z, -0.45, 1e-6));
    float const s = std::sqrt(0.5f);
    grip.orientation = {0.0f, s, 0.0f, s};  // +90 degrees about +Y: +X -> -Z, +Z -> +X
    XrVector3f const turned = hp::WristPoint(grip, {-0.03f, 0.06f, 0.05f});
    CHECK(close(turned.x, 1.05, 1e-5) && close(turned.y, 1.06, 1e-6) && close(turned.z, -0.47, 1e-5));

    // The gun point: behind the muzzle along the barrel, up toward the gun's top.
    XrVector3f const anchor = hp::GunPoint({0.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 0.15f, 0.04f);
    CHECK(close(anchor.x, 0.0, 1e-6) && close(anchor.y, 1.04, 1e-6) && close(anchor.z, -0.85, 1e-6));

    // Settings: defaults, values, and bad values kept out with a word each.
    hp::Settings const defaults = hp::ReadSettings(mevr::ini::File{});
    CHECK(defaults.wristStatus && defaults.gunStatus && close(defaults.wristWidthM, 0.08, 1e-6) && close(defaults.gunWidthM, 0.06, 1e-6));
    CHECK(close(defaults.wristOffset.x, -0.03, 1e-6) && close(defaults.wristOffset.y, 0.06, 1e-6) && close(defaults.wristOffset.z, 0.05, 1e-6));
    CHECK(close(defaults.gunBackM, 0.15, 1e-6) && close(defaults.gunUpM, 0.04, 1e-6) && !defaults.wristCrop.Valid() && !defaults.gunCrop.Valid());
    mevr::ini::File const file = mevr::ini::Parse("[Hud]\nwristStatus = 0\ngunWidthM = 0.1\nwristOffset = 0.01, -0.02, 0.03\n"
                                                  "gunCrop = 0.05 0.85 0.15 0.12\nwristCrop = 0.9 0.9 0.5 0.5\ngunBackM = abc\n");
    std::string problems;
    hp::Settings const read = hp::ReadSettings(file, &problems);
    CHECK(!read.wristStatus && read.gunStatus && close(read.gunWidthM, 0.1, 1e-6) && close(read.wristWidthM, 0.08, 1e-6));
    CHECK(close(read.wristOffset.x, 0.01, 1e-6) && close(read.wristOffset.y, -0.02, 1e-6) && close(read.wristOffset.z, 0.03, 1e-6));
    CHECK(read.gunCrop.Valid() && close(read.gunCrop.y, 0.85, 1e-6) && close(read.gunCrop.height, 0.12, 1e-6));
    CHECK(!read.wristCrop.Valid() && close(read.gunBackM, 0.15, 1e-6));  // past the frame's edge; not a number
    CHECK(problems.find("wristCrop") != std::string::npos && problems.find("gunBackM") != std::string::npos);
    float three[3] = {};
    CHECK(hp::ParseNumbers("1 2 3", 3, three) && three[2] == 3.0f);
    CHECK(!hp::ParseNumbers("1 2", 3, three) && !hp::ParseNumbers("1 2 3 4", 3, three) && !hp::ParseNumbers("1 x 3", 3, three));
    CHECK(hp::Describe(read).find("on the HUD only") != std::string::npos);

    // Published pieces age out.
    hp::Reset();
    CHECK(!hp::Latest().gunVisible && hp::Latest().size == 0);
    hp::Pieces pieces;
    pieces.gunVisible = true;
    pieces.serial = 3;
    hp::Publish(pieces);
    hp::Pieces const latest = hp::Latest();
    CHECK(latest.gunVisible && latest.serial == 3 && latest.size == sizeof(hp::Pieces) && latest.sequence != 0);
    hp::Reset();
}

void TestAds() {
    namespace ads = mevr::vr::ads;
    XrVector3f const head{0.0f, 0.0f, 0.0f};
    XrVector3f const forward{0.0f, 0.0f, -1.0f};
    XrVector3f const barrel{0.0f, 0.0f, -1.0f};
    XrVector3f const top{0.0f, 1.0f, 0.0f};
    // A sniper rifle up to the eye: the muzzle 0.9 m ahead, 2 cm right of and 6 cm below the centre eye; the eyepiece 0.30 m
    // ahead (0.6 m behind the muzzle).
    XrVector3f const muzzle{0.02f, -0.06f, -0.9f};
    XrVector3f const sight{0.02f, -0.01f, -0.3f};
    ads::Sample sample = ads::Measure(head, forward, muzzle, sight, barrel, top);
    CHECK(sample.valid && std::abs(sample.depth - 0.9f) < 1e-5f && std::abs(sample.lateral - 0.02f) < 1e-5f &&
          std::abs(sample.height - 0.06f) < 1e-5f && sample.facingDeg < 0.05f && std::abs(sample.sightToMuzzle - 0.6f) < 1e-5f &&
          std::abs(sample.sightDepth - 0.3f) < 1e-5f && std::abs(sample.eyeToSight - std::sqrt(0.02f * 0.02f + 0.01f * 0.01f + 0.09f)) < 1e-5f);
    CHECK(ads::Inside(sample, false) && ads::Inside(sample, true) && std::abs(ads::Approach(sample) - 1.0f) < 1e-6f);
    // Either side of the bore counts; a bad pose isn't a sample.
    CHECK(std::abs(ads::Measure(head, forward, {-0.02f, -0.06f, -0.9f}, sight, barrel, top).lateral - 0.02f) < 1e-5f);
    CHECK(!ads::Measure(head, forward, muzzle, sight, barrel, barrel).valid);
    CHECK(!ads::Measure({std::nanf(""), 0.0f, 0.0f}, forward, muzzle, sight, barrel, top).valid);

    // Each limit: outside the entry corridor, inside the exit one (the hysteresis), and outside both.
    auto with = [&](auto change) {
        ads::Sample v = sample;
        change(v);
        return v;
    };
    ads::Reach const none{0.0f, 0.0f};
    auto const between = [&](ads::Sample const& v) { return !ads::Inside(v, false, none) && ads::Inside(v, true, none); };
    auto const outside = [&](ads::Sample const& v) { return !ads::Inside(v, false, none) && !ads::Inside(v, true, none); };
    CHECK(between(with([](ads::Sample& v) { v.facingDeg = 35.0f; })) && outside(with([](ads::Sample& v) { v.facingDeg = 41.0f; })));
    CHECK(between(with([](ads::Sample& v) { v.lateral = 0.11f; })) && outside(with([](ads::Sample& v) { v.lateral = 0.14f; })));
    CHECK(between(with([](ads::Sample& v) { v.height = -0.04f; })) && outside(with([](ads::Sample& v) { v.height = -0.07f; })));
    CHECK(between(with([](ads::Sample& v) { v.height = 0.19f; })) && outside(with([](ads::Sample& v) { v.height = 0.23f; })));
    CHECK(between(with([](ads::Sample& v) { v.depth = 0.12f; })) && outside(with([](ads::Sample& v) { v.depth = 0.09f; })));
    // The far depth: 1.5 m (1.7 to leave), extended by however much the sight sits behind the muzzle past 0.9 m.
    CHECK(ads::FarExtension(0.6f) == 0.0f && std::abs(ads::FarExtension(1.2f) - 0.3f) < 1e-6f);
    CHECK(between(with([](ads::Sample& v) { v.depth = 1.6f; })) && outside(with([](ads::Sample& v) { v.depth = 1.75f; })));
    CHECK(ads::Inside(with([](ads::Sample& v) { v.depth = 1.75f; v.sightToMuzzle = 1.2f; }), false, none));
    // Our reach: the eyes within 0.40 m of the sight to come on, 0.50 m to stay (the corridor alone allowed arm's length).
    auto const reachBetween = [&](float m) {
        ads::Sample const v = with([&](ads::Sample& x) { x.eyeToSight = m; });
        return !ads::Inside(v, false) && ads::Inside(v, true);
    };
    CHECK(ads::Inside(with([](ads::Sample& v) { v.eyeToSight = 0.39f; }), false) && reachBetween(0.45f) &&
          !ads::Inside(with([](ads::Sample& v) { v.eyeToSight = 0.55f; }), true));
    CHECK(!ads::Inside(ads::Sample{}, false) && !ads::Inside(ads::Sample{}, true));

    // The approach weight: continuous. The angle full within 5 degrees, half at 25, none at 45; each corridor edge half
    // way between the entry and the exit limit; the eye's distance (Overlord's 0.30 / 0.60 m, ADS's 0.50 / 0.70 m).
    CHECK(std::abs(ads::Approach(with([](ads::Sample& v) { v.facingDeg = 5.0f; })) - 1.0f) < 1e-6f);
    CHECK(std::abs(ads::Approach(with([](ads::Sample& v) { v.facingDeg = 25.0f; })) - 0.5f) < 1e-5f);
    CHECK(ads::Approach(with([](ads::Sample& v) { v.facingDeg = 45.0f; })) == 0.0f);
    CHECK(std::abs(ads::Approach(with([](ads::Sample& v) { v.lateral = 0.11f; })) - 0.5f) < 1e-4f);
    CHECK(std::abs(ads::Approach(with([](ads::Sample& v) { v.height = 0.19f; })) - 0.5f) < 1e-4f);
    CHECK(ads::Approach(with([](ads::Sample& v) { v.height = -0.07f; })) == 0.0f);  // a gun held at the hip, parallel
    float previous = 1.0f;
    bool smooth = true;
    for (int step = 0; step <= 100; ++step) {
        float const deg = 0.5f * static_cast<float>(step);
        float const w = ads::Approach(with([&](ads::Sample& v) { v.facingDeg = deg; }));
        smooth = smooth && w <= previous + 1e-7f && previous - w < 0.03f;  // the session's 8.5 <-> 9.1 degrees: no jump
        previous = w;
    }
    CHECK(smooth);
    CHECK(ads::DistanceWeight(0.30f, false) == 1.0f && std::abs(ads::DistanceWeight(0.45f, false) - 0.5f) < 1e-5f &&
          ads::DistanceWeight(0.60f, false) == 0.0f);
    CHECK(ads::DistanceWeight(0.50f, true) == 1.0f && std::abs(ads::DistanceWeight(0.60f, true) - 0.5f) < 1e-5f &&
          ads::DistanceWeight(0.70f, true) == 0.0f && ads::DistanceWeight(-1.0f, false) == 0.0f);
    // Overlord's per-optic pull and the clearance to the relief.
    CHECK(ads::PullDistance(ads::Optic::Unchanged) == 0.0f && ads::PullDistance(ads::Optic::Holographic) == 0.05f &&
          ads::PullDistance(ads::Optic::LowPower) == 0.10f && ads::PullDistance(ads::Optic::Scope) == 0.15f);
    CHECK(std::abs(ads::Clearance(0.47f, 0.12f, 0.35f) - 0.35f) < 1e-6f && std::abs(ads::Clearance(0.30f, 0.12f, 0.35f) - 0.18f) < 1e-6f &&
          ads::Clearance(0.10f, 0.12f, 0.35f) == 0.0f && ads::Clearance(0.3f, 0.12f, 0.0f) == 0.0f);

    // The latch: 100 ms inside turns it on, 100 ms outside off; a change of mind within the dwell restarts it.
    ads::Policy policy;
    for (int i = 0; i < 9; ++i) {
        CHECK(!policy.Update(true, 0.011f));
    }
    CHECK(policy.Entering() && !policy.Active());
    policy.Update(false, 0.011f);
    CHECK(!policy.Entering() && !policy.Active());
    int on = -1;
    for (int i = 0; i < 20 && on < 0; ++i) {
        if (policy.Update(true, 0.011f)) {
            on = i;
        }
    }
    CHECK(on == 10);  // the first inside tick starts the clock; ten more of 11 ms reach 100 ms
    // Out for a moment (under the dwell): it stays on.
    for (int i = 0; i < 5; ++i) {
        CHECK(policy.Update(false, 0.011f));
    }
    CHECK(policy.Leaving());
    CHECK(policy.Update(true, 0.011f) && !policy.Leaving());
    // Flicking every tick (the session's toggling around its threshold): never turns off once on, never on from off.
    for (int i = 0; i < 100; ++i) {
        CHECK(policy.Update(i % 2 == 0, 0.011f));
    }
    for (int i = 0; i < 11; ++i) {
        policy.Update(false, 0.011f);
    }
    CHECK(!policy.Active());
    for (int i = 0; i < 100; ++i) {
        CHECK(!policy.Update(i % 2 == 0, 0.011f));
    }
    policy.Reset();
    CHECK(!policy.Active() && !policy.Entering() && !policy.Leaving());

    // The whole gate on the session's numbers (LE1MEVR.log 14:52-14:57): the eye 0.45-0.57 m behind the eyepiece at a
    // casual hold never shows the picture; brought up to 0.33 m it comes on after 100 ms, and the pull's own move (0.33 m
    // -> 0.12 m on the ASSISTED gun) can't turn it off because the gate is measured on the unassisted gun; lowered past
    // 0.50 m it goes 100 ms later.
    policy.Reset();
    ads::Reach const reach{};
    auto const tick = [&](float eyeToSight) {
        ads::Sample const v = with([&](ads::Sample& x) { x.eyeToSight = eyeToSight; });
        return policy.Update(ads::Inside(v, policy.Active(), reach), 0.011f);
    };
    bool casual = false;
    for (int i = 0; i < 200; ++i) {
        casual = casual || tick(0.45f + 0.12f * static_cast<float>(i % 13) / 12.0f);
    }
    CHECK(!casual);
    for (int i = 0; i < 12; ++i) {
        tick(0.33f);
    }
    CHECK(policy.Active());
    for (int i = 0; i < 100; ++i) {
        CHECK(tick(0.33f + 0.1f * static_cast<float>(i % 2)));  // 0.33 / 0.43: inside the exit reach
    }
    for (int i = 0; i < 11; ++i) {
        tick(0.55f);
    }
    CHECK(!policy.Active());
}

// ---- The guided calibration (core/vr/Calibration, research/25) ---------------------------------------------

namespace cal = mevr::vr::calibration;

// A small deterministic noise source (no <random> state shared between tests).
struct Noise {
    std::uint32_t state = 12345u;
    double Next() {  // -1..1
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
    }
};

XrVector3f V3(double x, double y, double z) {
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}
XrVector3f Add3(XrVector3f a, XrVector3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
XrVector3f Sub3v(XrVector3f a, XrVector3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XrVector3f Scale3(XrVector3f a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Len3(XrVector3f a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
XrVector3f Unit3(XrVector3f a) { return Scale3(a, 1.0f / Len3(a)); }

// Synthetic aims: the player stands at `head` facing `yawDeg`, aims with eye `eye` at the three targets, the gun
// held at `trim` in the grip with its sight line `lineGun` (gun frame) from the grip, `relief` ahead of the eye.
std::vector<cal::AimSample> SyntheticAims(XrVector3f head, double yawDeg, int eye, XrQuaternionf trim, XrVector3f lineGun, float relief,
    double angleNoiseDeg, double positionNoise, bool rifle, XrVector3f foregripGun, XrQuaternionf leftTrim, std::uint32_t seed) {
    Noise noise{seed};
    cal::Anchor anchor{head, static_cast<float>(yawDeg * std::numbers::pi / 180.0)};
    XrVector3f const right = cal::AnchorRight(anchor);
    std::vector<cal::AimSample> samples;
    for (int i = 0; i < cal::kTargets; ++i) {
        cal::AimSample s;
        s.target = cal::TargetPosition(anchor, i);
        s.head.position = head;
        s.head.orientation = *cal::LookRotation(Sub3v(s.target, head));
        s.eyes[0] = Sub3v(head, Scale3(right, 0.032f));
        s.eyes[1] = Add3(head, Scale3(right, 0.032f));
        XrVector3f const e = s.eyes[eye];
        XrVector3f const d = Unit3(Sub3v(s.target, e));
        XrQuaternionf const gun = *cal::LookRotation(d);
        XrQuaternionf const jitter = AxisAngle(noise.Next(), noise.Next(), noise.Next() + 0.01, angleNoiseDeg);
        s.right.orientation = cal::Normalize(cal::Multiply(jitter, cal::Multiply(gun, cal::Conjugate(trim))));
        s.right.position = Add3(Sub3v(Add3(e, Scale3(d, relief)), cal::Rotate(gun, lineGun)),
            V3(noise.Next() * positionNoise, noise.Next() * positionNoise, noise.Next() * positionNoise));
        if (rifle) {
            s.hasLeft = true;
            s.left.position = Add3(s.right.position, cal::Rotate(gun, foregripGun));
            s.left.orientation = cal::Normalize(cal::Multiply(gun, leftTrim));
        }
        samples.push_back(s);
    }
    return samples;
}

void TestCalibration() {
    // ---- The pistol: the trim, the line and the eye come back from noisy aims ----
    XrQuaternionf const trim = cal::Normalize(cal::Multiply(AxisAngle(1, 0, 0, -35.0),
        cal::Multiply(AxisAngle(0, 1, 0, 4.0), AxisAngle(0, 0, 1, 3.0))));
    XrVector3f const lineGun{-0.012f, 0.065f, 0.0f};  // the sight line 6.5 cm above the grip, 1.2 cm to its left
    XrVector3f const head{0.1f, 1.62f, -0.2f};
    auto const pistol = SyntheticAims(head, 10.0, 1, trim, lineGun, 0.55f, 0.3, 0.002, false, {}, {0, 0, 0, 1}, 7u);
    cal::EyeChoice const choice = cal::ChooseEye(pistol);
    CHECK(choice.eye == 1);
    CHECK(choice.lateral[1] < 0.02f && choice.lateral[0] > 0.04f);
    cal::Stance const s = cal::Solve(pistol);
    CHECK(s.valid && s.samples == 3 && s.eye == 1 && s.eyeMeasured);
    CHECK(cal::AngleDeg(s.trim, trim) < 0.6f);
    CHECK(s.spreadDeg < 1.5f);
    CHECK(Len3(Sub3v(s.line, cal::Rotate(trim, lineGun))) < 0.006f);
    CHECK(std::abs(s.eyeRelief - 0.55f) < 0.01f);
    CHECK(!s.hasLeft);
    // The calibrated line from a grip of the samples runs through the aiming eye toward its target.
    for (cal::AimSample const& a : pistol) {
        cal::Line const line = cal::SightLine(a.right, s);
        XrVector3f const d = Unit3(Sub3v(a.target, a.eyes[1]));
        CHECK(cal::DirectionAngleDeg(line.direction, d) < 0.8f);
        XrVector3f const toEye = Sub3v(a.eyes[1], line.point);
        float const along = toEye.x * line.direction.x + toEye.y * line.direction.y + toEye.z * line.direction.z;
        CHECK(Len3(Sub3v(toEye, Scale3(line.direction, along))) < 0.012f);
        XrQuaternionf const gun = cal::GunOrientation(a.right.orientation, s);
        CHECK(cal::DirectionAngleDeg(cal::Rotate(gun, {0, 0, -1}), d) < 0.8f);
    }
    // The eye forced: the other one, flagged.
    cal::Stance const forced = cal::Solve(pistol, 0);
    CHECK(forced.valid && forced.eye == 0 && !forced.eyeMeasured);
    // A left-eyed player.
    auto const leftEyed = SyntheticAims(head, -20.0, 0, trim, lineGun, 0.5f, 0.2, 0.002, false, {}, {0, 0, 0, 1}, 11u);
    CHECK(cal::ChooseEye(leftEyed).eye == 0);
    cal::Stance const sl = cal::Solve(leftEyed);
    CHECK(sl.valid && sl.eye == 0 && cal::AngleDeg(sl.trim, trim) < 0.6f);
    // One aim 6 degrees off: the spread says so.
    auto off = pistol;
    off[2].right.orientation = cal::Normalize(cal::Multiply(AxisAngle(0, 1, 0, 6.0), off[2].right.orientation));
    CHECK(cal::Solve(off).spreadDeg > cal::kMaxSpreadDeg);
    // Nothing to solve.
    CHECK(!cal::Solve(std::span<cal::AimSample const>()).valid);

    // ---- The rifle: the foregrip, the left hand's trim, the hands' line and the stock ----
    XrQuaternionf const rifleTrim = cal::Normalize(cal::Multiply(AxisAngle(1, 0, 0, -12.0), AxisAngle(0, 0, 1, -6.0)));
    XrVector3f const foregrip{0.015f, -0.07f, -0.38f};
    XrQuaternionf const leftTrim = cal::Normalize(cal::Multiply(AxisAngle(0, 0, 1, 75.0), AxisAngle(1, 0, 0, -20.0)));
    auto const rifle = SyntheticAims(head, 0.0, 1, rifleTrim, {0.0f, 0.045f, 0.0f}, 0.13f, 0.25, 0.0015, true, foregrip, leftTrim, 23u);
    cal::Stance const r = cal::Solve(rifle);
    CHECK(r.valid && r.hasLeft && r.eye == 1);
    CHECK(cal::AngleDeg(r.trim, rifleTrim) < 0.6f);
    CHECK(Len3(Sub3v(r.foregrip, foregrip)) < 0.008f);
    CHECK(cal::AngleDeg(r.leftTrim, leftTrim) < 0.8f);
    CHECK(cal::DirectionAngleDeg(r.handsLine, Unit3(foregrip)) < 1.2f);
    CHECK(r.handsSpreadDeg < 1.5f);
    CHECK(std::abs(r.eyeRelief - 0.13f) < 0.01f);
    // The stock: the right grip below the eyes, a little to the right, just ahead (the head's level frame).
    XrVector3f stock{};
    for (cal::AimSample const& a : rifle) {
        XrQuaternionf const level = cal::YawRotation(cal::HeadingOf(a.head.orientation));
        stock = Add3(stock, Scale3(cal::Rotate(cal::Conjugate(level), Sub3v(a.right.position, a.head.position)), 1.0f / 3.0f));
    }
    CHECK(Len3(Sub3v(r.stock, stock)) < 1e-4f);
    CHECK(r.stock.y < -0.02f && r.stock.z < 0.0f);

    // ---- The rifle as it is really held (research/25 section 2): the right hand at the cheek, just behind the eye, the
    // left hand on a foregrip 8 cm below the sight line and 4 cm inboard, 0.26 m out. Captured; and each reason for
    // not capturing ----
    XrVector3f const foregripLow{-0.04f, -0.08f + 0.045f, -0.26f};  // gun frame from the right grip (the line 4.5 cm above it)
    auto const shouldered = SyntheticAims(head, 0.0, 1, rifleTrim, {0.0f, 0.045f, 0.0f}, -0.03f, 0.6, 0.004, true, foregripLow, leftTrim, 31u);
    for (cal::AimSample const& a : shouldered) {
        cal::AimCheck const c = cal::CheckRifleAim(a.head, a.right.position, a.left.position, a.target);
        CHECK(c.problem == cal::AimProblem::None);
        CHECK(c.stock < 0.2f && c.span > 0.25f && c.pointDeg < 15.0f);
        // The old test (the LEFT grip within 25 cm of the line of sight and 25 cm ahead of the eyes) refused it: a left
        // hand 0.26 m from a right hand 3 cm behind the eye is 23 cm ahead along the line.
        CHECK(!cal::OnTarget(a.head, a.left.position, a.target, 20.0f, 0.25f, 0.25f));
    }
    {
        cal::AimSample const& a = shouldered[1];
        XrPosef away = a.head;
        away.orientation = *cal::LookRotation(Sub3v(shouldered[2].target, a.head.position));  // looking at another target
        CHECK(cal::CheckRifleAim(away, a.right.position, a.left.position, a.target).problem == cal::AimProblem::LookAtTarget);
        XrVector3f const lowReady = Add3(a.right.position, {0.0f, -0.25f, -0.35f});  // carried, not shouldered
        CHECK(cal::CheckRifleAim(a.head, lowReady, Add3(lowReady, Sub3v(a.left.position, a.right.position)), a.target).problem ==
              cal::AimProblem::NotShouldered);
        CHECK(cal::CheckRifleAim(a.head, a.right.position, Add3(a.right.position, {0.03f, 0.0f, -0.05f}), a.target).problem ==
              cal::AimProblem::HandsTooClose);
        XrVector3f const sideways = Add3(a.right.position, Scale3(cal::AnchorRight(cal::Anchor{head, 0.0f}), -0.3f));
        cal::AimCheck const c = cal::CheckRifleAim(a.head, a.right.position, sideways, a.target);
        CHECK(c.problem == cal::AimProblem::NotPointing && c.pointDeg > 45.0f);
        CHECK(std::string(cal::ProblemName(c.problem)) == "not-pointing-at-target");
    }
    // The pistol's reasons.
    {
        cal::AimSample const& a = pistol[0];
        CHECK(cal::CheckPistolAim(a.head, a.right.position, a.target).problem == cal::AimProblem::None);
        CHECK(cal::CheckPistolAim(a.head, Add3(a.right.position, {0.0f, -0.3f, 0.0f}), a.target).problem == cal::AimProblem::PistolOffLine);
        CHECK(cal::CheckPistolAim(a.head, Add3(a.head.position, {0.0f, -0.05f, 0.0f}), a.target).problem == cal::AimProblem::PistolTooClose);
    }
    // The rifle's sway: 3 cm and 5 degrees of drift hold (the pistol's 2.5 cm / 4 degrees wouldn't).
    {
        cal::Stillness hold;
        XrPosef p[cal::Stillness::kMaxPoses]{};
        for (XrPosef& q : p) {
            q.orientation = {0, 0, 0, 1};
        }
        bool const all[cal::Stillness::kMaxPoses] = {true, true, true};
        float h = 0.0f;
        for (int frame = 0; frame <= 120; ++frame) {
            float const t = std::min(static_cast<float>(frame) / 90.0f, 1.0f);
            p[2].position = {0.0f, -0.03f * t, 0.0f};
            p[2].orientation = AxisAngle(1, 0, 0, 5.0 * t);
            h = hold.Update(p, all, 1.0f / 90.0f, cal::kRifleStill);
        }
        CHECK(h >= cal::kRifleStill.seconds);
        cal::Stillness strict;
        for (int frame = 0; frame <= 90; ++frame) {
            float const t = static_cast<float>(frame) / 90.0f;
            p[2].position = {0.0f, -0.03f * t, 0.0f};
            p[2].orientation = AxisAngle(1, 0, 0, 5.0 * t);
            strict.Update(p, all, 1.0f / 90.0f, cal::StillSettings{});
        }
        CHECK(strict.Held() < 1.0f && strict.Moved() == 2);  // the left hand broke it
    }

    // ---- The three aims' judgement (cal::AimSet) ----
    auto const fill = [](cal::AimSet& set, std::vector<cal::AimSample> const& samples) {
        for (int i = 0; i < cal::kTargets; ++i) {
            if (!set.Has(i)) {
                set.Add(i, samples[static_cast<std::size_t>(i)]);
            }
        }
    };
    auto const twisted = [](cal::AimSample s, double deg) {
        s.right.orientation = cal::Normalize(cal::Multiply(AxisAngle(0, 1, 0, deg), s.right.orientation));
        return s;
    };
    {
        // Agreeing aims: accepted.
        cal::AimSet set;
        fill(set, shouldered);
        CHECK(set.Count() == 3 && set.Next() == -1);
        cal::Review const ok = set.Finish(-1, cal::kMaxRifleSpreadDeg);
        CHECK(ok.result == cal::Review::Result::Accepted && ok.stance.valid && ok.stance.samples == 3);
        CHECK(cal::AngleDeg(ok.stance.trim, rifleTrim) < 1.0f);
        CHECK(Len3(Sub3v(ok.stance.foregrip, foregripLow)) < 0.012f);
    }
    {
        // One aim 8 degrees off: only its target is aimed again; aimed well, accepted.
        cal::AimSet set;
        std::vector<cal::AimSample> bad = shouldered;
        bad[1] = twisted(bad[1], 8.0);
        fill(set, bad);
        cal::Review const r1 = set.Finish(-1, cal::kMaxRifleSpreadDeg);
        CHECK(r1.result == cal::Review::Result::RedoOne && r1.outlier == 1 && r1.failures == 1);
        CHECK(r1.spreadDeg > cal::kMaxRifleSpreadDeg && r1.outlierDeg > 6.5f && r1.outlierDeg < 9.5f && r1.pairSpreadDeg < 1.5f);
        CHECK(set.Count() == 2 && set.Has(0) && !set.Has(1) && set.Has(2) && set.Next() == 1);
        set.Add(1, shouldered[1]);
        cal::Review const r2 = set.Finish(-1, cal::kMaxRifleSpreadDeg);
        CHECK(r2.result == cal::Review::Result::Accepted && r2.stance.samples == 3);
    }
    {
        // The same aim off again: after two failed judgements the agreeing pair is kept, the outlier left out.
        cal::AimSet set;
        std::vector<cal::AimSample> bad = shouldered;
        bad[2] = twisted(bad[2], -9.0);
        fill(set, bad);
        CHECK(set.Finish(-1, cal::kMaxRifleSpreadDeg).result == cal::Review::Result::RedoOne);
        CHECK(set.Next() == 2);
        set.Add(2, twisted(shouldered[2], 9.0));
        cal::Review const rv = set.Finish(-1, cal::kMaxRifleSpreadDeg);
        CHECK(rv.result == cal::Review::Result::AcceptedPair && rv.outlier == 2 && rv.failures == 2);
        CHECK(rv.stance.valid && rv.stance.samples == 2 && rv.stance.spreadDeg <= cal::kMaxRifleSpreadDeg);
        CHECK(cal::AngleDeg(rv.stance.trim, rifleTrim) < 1.0f);
        CHECK(std::string(cal::ResultName(rv.result)) == "accepted-pair");
    }
    {
        // All three far apart: no pair agrees, so all three again (the failures still count toward the fallback).
        cal::AimSet set;
        std::vector<cal::AimSample> bad = shouldered;
        bad[0] = twisted(bad[0], -12.0);
        bad[2] = twisted(bad[2], 12.0);
        fill(set, bad);
        cal::Review const rv = set.Finish(-1, cal::kMaxRifleSpreadDeg);
        CHECK(rv.result == cal::Review::Result::RedoAll && set.Count() == 0 && set.Next() == 0 && set.Failures() == 1);
        // The pistol keeps 3 degrees: the same 8-degree outlier is one target again.
        cal::AimSet pistolSet;
        std::vector<cal::AimSample> p = pistol;
        p[0] = twisted(p[0], 8.0);
        fill(pistolSet, p);
        cal::Review const rp = pistolSet.Finish(-1, cal::kMaxSpreadDeg);
        CHECK(rp.result == cal::Review::Result::RedoOne && rp.outlier == 0);
    }
    {
        // B takes back the aim taken last, whatever its target.
        cal::AimSet set;
        set.Add(0, shouldered[0]);
        set.Add(2, shouldered[2]);
        CHECK(set.Back() == 2 && set.Count() == 1 && set.Next() == 1);
        CHECK(set.Back() == 0 && set.Back() == -1);
    }

    // ---- The rifle's orbs: a foregrip's distance apart, within the left arm's reach ----
    {
        cal::Anchor const at{head, 0.0f};
        float const unmeasured = cal::ForegripDistance({});
        CHECK(std::abs(unmeasured - 0.29f) < 1e-6f);
        XrVector3f const rear = cal::OrbRest(at, cal::Orb::RifleRear);
        XrVector3f const front = cal::OrbRest(at, cal::Orb::RifleFront);
        XrVector3f const apart = Sub3v(front, rear);
        CHECK(std::abs(-apart.z - 0.29f) < 1e-4f && apart.y < -0.02f && apart.x < -0.02f);  // ahead, lower, inboard
        CHECK(Len3(apart) < 0.31f);  // was 0.40 m
        // The user's measures (arms 0.73 m, shoulders 0.29 m): 0.28 m, and the left shoulder reaches it bent.
        float const user = cal::ForegripDistance({0.73f, 0.29f});
        CHECK(user > 0.26f && user < 0.30f);
        XrVector3f const userFront = cal::OrbRest(at, cal::Orb::RifleFront, {0.73f, 0.29f});
        XrVector3f const leftShoulder = cal::AnchorPoint(at, -0.145f, -0.22f, -0.02f);
        CHECK(Len3(Sub3v(userFront, leftShoulder)) <= 0.85f * 0.73f + 1e-3f);
        // Short arms and broad shoulders: shorter, never below 0.2 m.
        float const tight = cal::ForegripDistance({0.55f, 0.45f});
        CHECK(tight >= 0.2f && tight < 0.24f);
        CHECK(cal::ForegripDistance({std::numeric_limits<float>::quiet_NaN(), std::nullopt}) == unmeasured);
    }
    // The grab point: the grip's origin moved in its frame, mirrored for the left hand.
    {
        XrPosef grip;
        grip.position = {0.1f, 1.2f, -0.4f};
        grip.orientation = AxisAngle(0, 1, 0, 90.0);  // -Z (ahead) turned to -X
        XrVector3f const gr = cal::GrabPoint(grip, {0.01f, 0.0f, -0.03f}, false);
        CHECK(std::abs(gr.x - (0.1f - 0.03f)) < 1e-5f && std::abs(gr.z - (-0.4f - 0.01f)) < 1e-5f);
        XrVector3f const l = cal::GrabPoint(grip, {0.01f, 0.0f, -0.03f}, true);
        CHECK(std::abs(l.z - (-0.4f + 0.01f)) < 1e-5f);
        CHECK(Len3(Sub3v(cal::GrabPoint(grip, {}, true), grip.position)) < 1e-6f);
    }

    // ---- Rotation helpers ----
    XrQuaternionf const look = *cal::LookRotation({1.0f, 0.0f, -1.0f});
    CHECK(cal::DirectionAngleDeg(cal::Rotate(look, {0, 0, -1}), {1, 0, -1}) < 0.01f);
    CHECK(cal::Rotate(look, {0, 1, 0}).y > 0.999f);  // level: no cant
    CHECK(!cal::LookRotation({0.0f, 1.0f, 0.0f}).has_value());
    CHECK(std::abs(cal::HeadingOf(cal::YawRotation(0.5f)) - 0.5f) < 1e-4f);
    XrQuaternionf const qs[] = {AxisAngle(0, 1, 0, 10), AxisAngle(0, 1, 0, 12)};
    XrQuaternionf const flipped[] = {qs[0], {-qs[1].x, -qs[1].y, -qs[1].z, -qs[1].w}};
    CHECK(cal::AngleDeg(*cal::Mean(flipped), AxisAngle(0, 1, 0, 11)) < 0.05f);  // sign-aligned

    // ---- Applying: onto the line, and where it meets the board ----
    cal::Line line;
    line.point = {0.0f, 0.0f, 0.0f};
    line.direction = {0.0f, 0.0f, -1.0f};
    XrVector3f const shift = cal::ShiftOntoLine({0.03f, -0.04f, -2.0f}, line, 1.0f);
    CHECK(std::abs(shift.x + 0.03f) < 1e-5f && std::abs(shift.y - 0.04f) < 1e-5f && std::abs(shift.z) < 1e-5f);
    CHECK(std::abs(Len3(cal::ShiftOntoLine({0.03f, -0.04f, -2.0f}, line, 0.025f)) - 0.025f) < 1e-5f);
    line.point = {0.0f, 1.6f, 0.0f};
    XrVector3f const hit = cal::OnPlane(line, {0.0f, 1.6f, -5.0f}, {0.0f, 0.0f, 1.0f}, 0.5f, 3.0f);
    CHECK(std::abs(hit.z + 5.0f) < 1e-4f && std::abs(hit.y - 1.6f) < 1e-4f);
    XrVector3f const away = cal::OnPlane(line, {0.0f, 1.6f, 5.0f}, {0.0f, 0.0f, 1.0f}, 0.5f, 3.0f);  // behind: the fallback
    CHECK(std::abs(away.z + 3.0f) < 1e-4f);

    // ---- Body ----
    CHECK(std::abs(cal::ShoulderWidth({-0.2f, 1.4f, -0.6f}, {0.19f, 1.42f, -0.62f}, {0.0f, 0.0f, -1.0f}) - 0.39f) < 1e-4f);
    CHECK(std::abs(cal::ArmLength(1.71f, 0.39f).value_or(0.0f) - 0.66f) < 1e-4f);
    CHECK(!cal::ArmLength(0.5f, 0.39f).has_value());
    auto const crouch = cal::CrouchFromHeights(1.65, 1.05);
    CHECK(crouch && std::abs(crouch->crouchBelow - (1.0 - 0.5 * 0.6 / 1.65)) < 1e-9 &&
          std::abs(crouch->standAbove - (1.0 - 0.35 * 0.6 / 1.65)) < 1e-9);
    CHECK(!cal::CrouchFromHeights(1.65, 1.55).has_value());  // too shallow to tell apart
    auto const deep = cal::CrouchFromHeights(1.65, 0.5);
    CHECK(deep && deep->crouchBelow >= 0.3 && deep->standAbove >= deep->crouchBelow + 0.02);

    // ---- Holding still ----
    cal::Stillness still;
    cal::StillSettings const settings;
    Noise jitter{99u};
    XrPosef poses[cal::Stillness::kMaxPoses]{};
    bool const used[cal::Stillness::kMaxPoses] = {true, true, false};
    for (XrPosef& p : poses) {
        p.orientation = {0, 0, 0, 1};
    }
    float held = 0.0f;
    for (int frame = 0; frame < 100; ++frame) {
        XrPosef now[cal::Stillness::kMaxPoses];
        for (int i = 0; i < cal::Stillness::kMaxPoses; ++i) {
            now[i] = poses[i];
            now[i].position = Add3(poses[i].position, V3(jitter.Next() * 0.002, jitter.Next() * 0.002, 0.0));
        }
        held = still.Update(now, used, 1.0f / 90.0f, settings);
    }
    CHECK(held >= settings.seconds);
    CHECK(Len3(still.Average(0).position) < 0.001f);
    XrPosef moved[cal::Stillness::kMaxPoses];
    for (int i = 0; i < cal::Stillness::kMaxPoses; ++i) {
        moved[i] = poses[i];
    }
    moved[1].position = {0.05f, 0.0f, 0.0f};  // the right hand moved 5 cm: the hold starts over
    CHECK(still.Update(moved, used, 1.0f / 90.0f, settings) == 0.0f);
    moved[0].orientation = AxisAngle(0, 1, 0, 8.0);  // the head turned 8 degrees
    still.Update(moved, used, 1.0f / 90.0f, settings);
    CHECK(still.Held() == 0.0f);

    // ---- Step criteria ----
    CHECK(cal::LookingAhead(AxisAngle(1, 0, 0, 10.0)) && !cal::LookingAhead(AxisAngle(1, 0, 0, 30.0)));
    XrVector3f const h{0.0f, 1.6f, 0.0f};
    CHECK(cal::TPose(h, {-0.85f, 1.45f, 0.0f}, {0.85f, 1.45f, 0.0f}));
    CHECK(!cal::TPose(h, {-0.3f, 1.45f, 0.0f}, {0.3f, 1.45f, 0.0f}));     // arms down or in
    CHECK(!cal::TPose(h, {-0.85f, 0.9f, 0.0f}, {0.85f, 0.9f, 0.0f}));     // hands at the hips
    CHECK(cal::ArmsForward(h, {-0.2f, 1.4f, -0.65f}, {0.2f, 1.4f, -0.65f}, {0, 0, -1}));
    CHECK(!cal::ArmsForward(h, {-0.2f, 1.4f, -0.2f}, {0.2f, 1.4f, -0.65f}, {0, 0, -1}));
    CHECK(cal::Crouched(1.65f, 1.3f) && !cal::Crouched(1.65f, 1.5f));
    cal::AimSample const& a0 = pistol[0];
    CHECK(cal::OnTarget(a0.head, a0.right.position, a0.target));
    CHECK(!cal::OnTarget(a0.head, Add3(a0.right.position, {0.3f, 0.0f, 0.0f}), a0.target));  // the hand off the line of sight
    CHECK(!cal::OnTarget(a0.head, a0.right.position, pistol[1].target));                      // looking at another target

    // ---- Layout: the targets about 17 degrees across and 15 up/down; the panel clear of them ----
    cal::Anchor const anchor{h, 0.0f};
    XrVector3f const upperLeft = Sub3v(cal::TargetPosition(anchor, 1), h);
    CHECK(upperLeft.x < 0.0f && upperLeft.y > 0.0f);
    double const yawUL = std::atan2(-upperLeft.x, -upperLeft.z) * 180.0 / std::numbers::pi;
    CHECK(yawUL > 15.0 && yawUL < 19.0);
    XrPosef const panel = cal::PanelPose(anchor, true);
    for (int i = 0; i < cal::kTargets; ++i) {
        CHECK(cal::DirectionAngleDeg(Sub3v(panel.position, h), Sub3v(cal::TargetPosition(anchor, i), h)) > 22.0f);
    }
    // Quads face the eyes: their +Z points back at them.
    CHECK(cal::DirectionAngleDeg(cal::Rotate(panel.orientation, {0, 0, 1}), Sub3v(h, panel.position)) < 0.1f);
    XrPosef const board = cal::BoardPose(anchor);
    CHECK(cal::Rotate(board.orientation, {0, 0, 1}).z > 0.999f && std::abs(board.position.z + cal::kBoardDistance) < 1e-4f);
    cal::Anchor const turned{h, static_cast<float>(std::numbers::pi / 2.0)};  // facing -X (turned left)
    CHECK(cal::AnchorForward(turned).x < -0.999f && cal::AnchorRight(turned).z < -0.999f);

    // ---- Text ----
    XrVector3f const v{0.0123f, -0.0456f, 1.5f};
    auto const parsed = cal::ParseVector(cal::FormatVector(v));
    CHECK(parsed && Len3(Sub3v(*parsed, v)) < 1e-4f);
    auto const rotation = cal::ParseRotation(cal::FormatRotation(trim));
    CHECK(rotation && cal::AngleDeg(*rotation, trim) < 0.01f);
    CHECK(!cal::ParseVector("1, 2").has_value() && !cal::ParseVector("1, 2, x").has_value() && !cal::ParseRotation("0, 0, 0, 0"));
    CHECK(cal::ProfileKey("/interaction_profiles/oculus/touch_controller", "Meta Quest 3") == "oculus_touch_controller__meta_quest_3");
    CHECK(cal::ProfileKey("/interaction_profiles/valve/index_controller", "SteamVR/OpenXR : lighthouse") ==
          "valve_index_controller__steamvr_openxr_lighthouse");
    CHECK(cal::ProfileKey("", "") == "unknown");
    CHECK(cal::ProfileLabel("/interaction_profiles/oculus/touch_controller", "Meta Quest 3") == "Touch Controller (Meta Quest 3)");
    CHECK(cal::ListHas("a, b,c", "b") && cal::ListHas("a, b,c", "c") && !cal::ListHas("a,bb", "b") && !cal::ListHas("", "a"));

    // ---- "At the shoulder" around a measured stock point (core/ik/TwoHand) ----
    using mevr::ik::Vec3;
    Vec3 const stockPoint{0.06, -0.12, 0.05};  // 6 cm right, 12 cm below the eyes, 5 cm behind them
    mevr::ik::ShoulderZone const zone = mevr::ik::ShoulderZoneAround(stockPoint);
    CHECK(std::abs(zone.maxDrop - 0.24) < 1e-9 && zone.minDrop == 0.0 && std::abs(zone.maxSide - 0.20) < 1e-9);
    CHECK(std::abs(zone.minForward + 0.17) < 1e-9 && std::abs(zone.maxForward - 0.12) < 1e-9);
    Vec3 const eyes{0.0, 1.6, 0.0};
    Vec3 const ahead{0.0, 0.0, -1.0};
    Vec3 const up{0.0, 1.0, 0.0};
    CHECK(mevr::ik::Shouldered(eyes, ahead, up, eyes + stockPoint, false, zone));
    CHECK(!mevr::ik::Shouldered(eyes, ahead, up, eyes + Vec3{0.35, -0.12, 0.05}, false, zone));  // off to the side
    CHECK(!mevr::ik::Shouldered(eyes, ahead, up, eyes + Vec3{0.06, -0.12, -0.45}, false, zone));  // held out ahead
    CHECK(mevr::ik::Shouldered(eyes, ahead, up, eyes + Vec3{0.06, -0.12, -0.15}, true, zone));    // inside with the margin
}

// The main menu's settings (core/vr/MenuSettings, research/28): the schema, what a slider reads, the conversions
// between rows and settings, and its edits of an ini file (key by key, the rest of the file untouched).
void TestMenuSettings() {
    namespace ms = mevr::vr::menu_settings;
    // The schema: unique, well-formed keys; numbers' defaults inside their ranges.
    std::vector<std::string> keys;
    for (ms::Field const& f : ms::Schema()) {
        std::string const key = f.key;
        CHECK(!key.empty());
        CHECK(std::all_of(key.begin(), key.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }));
        CHECK(std::find(keys.begin(), keys.end(), key) == keys.end());
        keys.push_back(key);
        if (f.type == ms::Type::Number) {
            CHECK(f.min < f.max);
            CHECK(f.number >= f.min && f.number <= f.max);
            CHECK(ms::Find(key + "_text") == nullptr);  // the companion's name is free
        }
    }
    CHECK(ms::Find("crouch") != nullptr && ms::Find("crouch")->type == ms::Type::Flag);
    CHECK(ms::Find("section") != nullptr && ms::Find("section")->local);
    CHECK(ms::Find("nonsense") == nullptr);
    ms::Field const* trim = ms::Find("trim_pitch");
    CHECK(trim != nullptr && ms::Clamp(*trim, 45.0f) == 30.0f && ms::Clamp(*trim, -45.0f) == -30.0f);
    CHECK(trim != nullptr && ms::Clamp(*trim, std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(ms::Clamp(*ms::Find("crouch"), 0.4f) == 1.0f);
    // About's build line (the version from CMake, the commit from tools/cmake/BuildInfo.cmake).
    CHECK(ms::Find("about_build") != nullptr && ms::Find("about_build")->type == ms::Type::Text);
    CHECK(ms::AboutBuild("0.1.0", "1a0622c1f2", false) == "MEVR 0.1.0 \xC2\xB7 commit 1a0622c1f2");
    CHECK(ms::AboutBuild("0.2.0", "1a0622c1f2-dirty", true) == "MEVR 0.2.0 \xC2\xB7 commit 1a0622c1f2-dirty \xC2\xB7 development build");
    CHECK(ms::AboutBuild("0.1.0", "unknown", false) == "MEVR 0.1.0");  // built without git

    // What a slider reads.
    CHECK(ms::FormatValue(ms::Format::SignedPercent, 8.0f) == "+8%");
    CHECK(ms::FormatValue(ms::Format::SignedPercent, -2.0f) == "-2%");
    CHECK(ms::FormatValue(ms::Format::SignedPercent, 0.2f) == "0%");  // no "+0" or "-0"
    CHECK(ms::FormatValue(ms::Format::SignedPercent, -0.2f) == "0%");
    CHECK(ms::FormatValue(ms::Format::Percent, 60.0f) == "60%");
    CHECK(ms::FormatValue(ms::Format::SignedDegrees, 4.0f) == "+4\xC2\xB0");
    CHECK(ms::FormatValue(ms::Format::DegreesOrNever, 0.0f) == "Never");
    CHECK(ms::FormatValue(ms::Format::DegreesOrNever, 35.0f) == "35\xC2\xB0");
    CHECK(ms::FormatValue(ms::Format::DegreesPerSec, 120.0f) == "120\xC2\xB0/s");
    CHECK(ms::FormatValue(ms::Format::Fixed2, 0.92f) == "0.92");
    CHECK(ms::FormatValue(ms::Format::SignedFixed3, -0.01f) == "-0.010");
    CHECK(ms::FormatValue(ms::Format::Fixed3, 0.012f) == "0.012");
    CHECK(ms::FormatValue(ms::Format::Times2, 1.0f) == "1.00\xC3\x97");
    CHECK(ms::FormatValue(ms::Format::Meters1, 1.6f) == "1.6 m");
    CHECK(ms::FormatValue(ms::Format::SignedMeters2, 0.125f) == "+0.13 m");
    CHECK(ms::FormatValue(ms::Format::Fixed1, std::numeric_limits<float>::infinity()) == "?");

    // The render resolution ladder (MELE-VR.bat's tiers, Halcyon's near-square height).
    CHECK(ms::QualityWidth("performance") == 3072);
    CHECK(ms::QualityWidth("max") == 6144);
    CHECK(ms::QualityWidth("huge") == 0);
    CHECK(std::string(ms::QualityFor(3072)) == "performance");
    CHECK(std::string(ms::QualityFor(4000)) == "balanced");
    CHECK(std::string(ms::QualityFor(100)) == "low");
    CHECK(ms::EyeHeight(3072) == 3188);  // the deploy's Performance target, 3072 x 3188
    CHECK(ms::EyeHeight(4096) == 4250);  // vr_config.h's sfr2ResX/Y defaults
    CHECK(ms::QualityText(3072) == "3072 \xC3\x97 3188");

    // The headset-sized targets (research/32): the PS VR2's recommended 2824 x 2880 exactly, 0.85 and 0.75 of it even.
    CHECK(ms::RoundEven(2400.4f) == 2400 && ms::RoundEven(2401.2f) == 2402 && ms::RoundEven(2118.0f) == 2118);
    CHECK(ms::RoundEven(2119.0f) == 2120 && ms::RoundEven(0.5f) == 2);
    CHECK((ms::HeadsetSize("headset", 2824, 2880) == ms::Size{2824, 2880}));
    CHECK((ms::HeadsetSize("headset85", 2824, 2880) == ms::Size{2400, 2448}));  // 2400.4 x 2448
    CHECK((ms::HeadsetSize("headset75", 2824, 2880) == ms::Size{2118, 2160}));
    CHECK((ms::HeadsetSize("headset", 2017, 2161) == ms::Size{2017, 2161}));    // the runtime's own size kept as it is
    CHECK((ms::HeadsetSize("headset85", 2017, 2161) == ms::Size{1714, 1836}));  // 1714.45 x 1836.85, each even
    CHECK((ms::HeadsetSize("headset85", 0, 0) == ms::Size{}));                  // before VR has started
    CHECK((ms::HeadsetSize("balanced", 2824, 2880) == ms::Size{}));
    // Selected: a headset chip when the target is exactly its size; a ladder tier by its width; else none.
    CHECK(ms::ResolutionChoice(2824, 2880, 2824, 2880) == "headset");
    CHECK(ms::ResolutionChoice(2400, 2448, 2824, 2880) == "headset85");
    CHECK(ms::ResolutionChoice(2118, 2160, 2824, 2880) == "headset75");
    CHECK(ms::ResolutionChoice(2824, 2930, 2824, 2880).empty());      // the headset's width, Halcyon's height: neither
    CHECK(ms::ResolutionChoice(2824, 2880, 0, 0).empty());            // not known before VR starts
    CHECK(ms::ResolutionChoice(3072, 3188, 2824, 2880) == "performance");
    CHECK(ms::ResolutionChoice(3072, 3188, 0, 0) == "performance");
    CHECK(ms::ResolutionChoice(4096, 4250, 2824, 2880) == "balanced");
    CHECK(ms::ResolutionChoice(3072, 3072, 3072, 3072) == "headset");  // a headset chip wins a tie with the ladder
    CHECK(ms::ResolutionChoice(3000, 3112, 2824, 2880).empty());
    CHECK(ms::SizeText(2824, 2880) == "2824 \xC3\x97 2880");
    CHECK(ms::HeadsetSizesText(2824, 2880) ==
          "Headset 2824 \xC3\x97 2880 \xC2\xB7 \xC3\x97 0.85: 2400 \xC3\x97 2448 \xC2\xB7 \xC3\x97 0.75: 2118 \xC3\x97 2160");
    CHECK(ms::HeadsetSizesText(0, 2880).find("once VR starts") != std::string::npos);
    // The upscale chip ([Upscale] method, read as core/vr/Upscale reads it).
    CHECK(ms::UpscaleChoice("fsr") == "fsr" && ms::UpscaleChoice(" FSR1 ") == "fsr" && ms::UpscaleChoice("on") == "fsr");
    CHECK(ms::UpscaleChoice("off") == "off" && ms::UpscaleChoice("bilinear") == "off" && ms::UpscaleChoice("") == "off");

    // Gun sights.
    CHECK(ms::SightKind("scope 4", "dot") == "scope");
    CHECK(ms::SightKind(" Holo ", "dot") == "holo");
    CHECK(ms::SightKind("laser", "dot") == "dot");
    CHECK(ms::SightValue("dot", "scope 6") == "dot");
    CHECK(ms::SightValue("scope", "scope 6") == "scope 6");
    CHECK(ms::SightValue("scope", "scope 2.5") == "scope 2.5");
    CHECK(ms::SightValue("scope", "holo") == "scope 4");
    CHECK(ms::SightValue("scope", "scope 99") == "scope 4");
    CHECK(ms::SightValue("scope", "dot", 1.5f) == "scope 1.5" && ms::SightValue("scope", "scope 3", 1.5f) == "scope 3");
    // The sniper rifle's chip (research/21 section 16): the game's own magnification unless the player set one.
    CHECK(ms::SightValue("scope", "dot", 0.0f) == "scope auto" && ms::SightValue("scope", "scope auto", 0.0f) == "scope auto");
    CHECK(ms::SightValue("scope", "Scope Auto", 1.5f) == "scope auto" && ms::SightValue("scope", "scope 5", 0.0f) == "scope 5");
    CHECK(ms::SightKind("scope 1.5", "dot") == "scope");

    // The desktop mirror, the colour sliders, the chips.
    CHECK(ms::MirrorChoice(1) == "every" && ms::MirrorChoice(2) == "smooth" && ms::MirrorChoice(8) == "normal");
    CHECK(ms::MirrorPresentEvery("every") == 1 && ms::MirrorPresentEvery("smooth") == 2 && ms::MirrorPresentEvery("normal") == 8);
    CHECK(ms::ColourPercent(1.08f) == 8.0f && ms::ColourPercent(0.98f) == -2.0f);
    CHECK(std::fabs(ms::ColourFromPercent(12.0f) - 1.12f) < 1e-6f);
    CHECK(ms::HeadsetLabel("SteamVR/OpenXR", "SteamVR/OpenXR : playstation_vr2") == "PlayStation VR2 \xC2\xB7 SteamVR");
    CHECK(ms::HeadsetLabel("Oculus", "Meta Quest 3") == "Meta Quest 3 \xC2\xB7 Link");
    CHECK(ms::HeadsetLabel("SteamVR/OpenXR", "SteamVR/OpenXR : some_new_thing") == "Some New Thing \xC2\xB7 SteamVR");
    CHECK(ms::HeadsetLabel("", "") .empty());
    CHECK(ms::ControllersLabel("Sense Controller (SteamVR/OpenXR : playstation_vr2)") == "Sense Controller");
    CHECK(ms::ControllersLabel("Touch Plus Controller") == "Touch Plus Controller");

    // The Game UI nudges and the original stereo's eye distance (2026-11-04).
    CHECK(ms::FormatValue(ms::Format::Signed0, 40.0f) == "+40" && ms::FormatValue(ms::Format::Signed0, -120.0f) == "-120");
    CHECK(ms::FormatValue(ms::Format::Signed0, -0.4f) == "0");
    CHECK(ms::FormatValue(ms::Format::Centimeters1, 6.8f) == "6.8 cm");

    // The controller guide: what each control does with the shipped settings, and with the settings that change it.
    {
        auto text = [](ms::GuideInput const& in, std::string const& key) {
            for (auto const& [k, t] : ms::GuideTexts(in)) {
                if (k == key) {
                    return t;
                }
            }
            return std::string("(missing)");
        };
        ms::GuideInput const shipped;
        for (char const* id : {"lt", "lgrip", "lstick", "x", "y", "menu", "rt", "rgrip", "rstick", "a", "b"}) {
            std::string const label = text(shipped, std::string("guide_") + id);
            CHECK(!label.empty() && label != "(missing)" && label.size() <= 22);  // fits the drawing's label in two lines
            CHECK(text(shipped, std::string("guide_") + id + "_how").size() > 10);
        }
        for (auto const& [k, t] : ms::GuideTexts(shipped)) {
            CHECK(ms::Find(k) != nullptr && ms::Find(k)->type == ms::Type::Text);  // the page binds every one
        }
        CHECK(text(shipped, "guide_lt") == "Weapon wheel" && text(shipped, "guide_rt") == "Fire");
        CHECK(text(shipped, "guide_rstick") == "Turn, click: powers");
        CHECK(text(shipped, "guide_rstick_how").find("recenter") != std::string::npos);
        CHECK(text(shipped, "guide_rgrip") == "Draw and holster");
        CHECK(text(shipped, "guide_x") == "Grenade" && text(shipped, "guide_y") == "Medi-gel" && text(shipped, "guide_b") == "Melee");
        CHECK(text(shipped, "guide_lgrip_how").find("vent") != std::string::npos);
        CHECK(text(shipped, "guide_grenade").rfind("Right hand", 0) == 0);  // [Throwables] hands = right
        CHECK(text(shipped, "guide_lstick_how").find("where you look") != std::string::npos);
        // The power wheel moved to Y ([Holsters] powerWheel): Y is the powers, the stick click only recenters...
        ms::GuideInput y = shipped;
        y.powerWheel = " Y ";
        CHECK(text(y, "guide_y") == "Powers" && text(y, "guide_rstick") == "Turn");
        CHECK(text(y, "guide_rstick_how").find("recenter") != std::string::npos);
        // ...but only while the right grip holds the gun: without the holsters the grip is the game's RB again.
        ms::GuideInput noHolsters = y;
        noHolsters.holsters = false;
        CHECK(text(noHolsters, "guide_rgrip") == "Powers" && text(noHolsters, "guide_y") == "Medi-gel");
        ms::GuideInput noHold = shipped;
        noHold.gripHolds = false;
        CHECK(text(noHold, "guide_rgrip") == "Draw, powers" && text(noHold, "guide_rstick") == "Turn");
        ms::GuideInput x = shipped;
        x.powerWheel = "x";
        CHECK(text(x, "guide_x") == "Powers");
        ms::GuideInput b = shipped;
        b.powerWheel = "b";
        CHECK(text(b, "guide_b") == "Powers");
        // The left trigger's choices; the wheel without the squad is the game's own (hold, tap); the custom squad menu.
        ms::GuideInput lt = shipped;
        lt.leftTrigger = "lt";
        CHECK(text(lt, "guide_lt") == "Aim");
        ms::GuideInput none = shipped;
        none.leftTrigger = "none";
        CHECK(text(none, "guide_lt") == "Nothing");
        ms::GuideInput noSquad = shipped;
        noSquad.squad = false;
        CHECK(text(noSquad, "guide_lt") == "Weapon wheel" && text(noSquad, "guide_lt_how").find("tap") != std::string::npos);
        ms::GuideInput custom = shipped;
        custom.squadStyle = "custom";
        CHECK(text(custom, "guide_lt") == "Squad weapons");
        // X and B kept from the game, no vent, grenades in the left hand, walking toward the hand, snap turns.
        ms::GuideInput kept = shipped;
        kept.grenadeButton = false;
        kept.meleeButton = false;
        kept.vent = false;
        kept.grenadeHands = "left";
        kept.walk = "hand";
        kept.turn = "snap";
        CHECK(text(kept, "guide_x") == "Nothing" && text(kept, "guide_b") == "Nothing");
        CHECK(text(kept, "guide_lgrip") == "Two-handed grip" && text(kept, "guide_lgrip_how").find("vent") == std::string::npos);
        CHECK(text(kept, "guide_lgrip_how").find("grenade") != std::string::npos);
        CHECK(text(kept, "guide_grenade").rfind("Left hand", 0) == 0);
        CHECK(text(kept, "guide_lstick_how").find("left hand points") != std::string::npos);
        CHECK(text(kept, "guide_rstick_how").find("steps") != std::string::npos);
        ms::GuideInput both = shipped;
        both.grenadeHands = "both";
        CHECK(text(both, "guide_grenade").rfind("Either hand", 0) == 0);
        ms::GuideInput off = shipped;
        off.grenades = false;
        off.melee = false;
        CHECK(text(off, "guide_grenade").rfind("Off", 0) == 0 && text(off, "guide_melee").rfind("Off", 0) == 0);
        CHECK(text(off, "guide_lgrip_how").find("grenade") == std::string::npos);
    }

    // A section as GetPrivateProfileSection gives it: lower-cased keys, trimmed values, comments skipped.
    auto const section = ms::ParseSection("; a comment\nphysicalCrouch = 1\n  HandPitch=-4.5 \nbroken line\n");
    CHECK(section.size() == 2 && section.at("physicalcrouch") == "1" && section.at("handpitch") == "-4.5");

    // The queue of edits: the latest value wins, a slider's waits until it rests, a re-read sees it meanwhile.
    ms::PendingWrites pending;
    pending.Put("Hands", "handPitch", "1.0", 1300);
    pending.Put("Hands", "handPitch", "2.0", 1600);  // the slider moved again: one edit, later
    pending.Put("Hands", "physicalCrouch", "0", 1000);
    CHECK(pending.Size() == 2);
    CHECK(pending.Overlay("Hands", "handPitch") == std::optional<std::string>("2.0"));
    CHECK(!pending.Overlay("Hands", "handYaw"));
    std::vector<ms::PendingWrites::Edit> due = pending.TakeDue(1100);
    CHECK(due.size() == 1 && due[0].key == "physicalCrouch");
    CHECK(pending.TakeDue(1500).empty());
    due = pending.TakeDue(0, true);  // the menu closed: everything
    CHECK(due.size() == 1 && due[0].value == "2.0" && pending.Empty());

    // Written key by key into a real file: the rest of it (other keys, comments, other sections) stays as it was.
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    std::filesystem::path const file = std::filesystem::path(temp) / L"mevr-menu-settings-test.ini";
    std::ofstream(file) << "; ME VR Attempt settings\n[Hands]\n; how the hands sit\nhandPitch = 0\nphysicalCrouch = 1\n\n[Sights]\npistol = dot\n";
    pending.Put("Hands", "physicalCrouch", "0", 0);
    pending.Put("Sights", "sniperRifle", ms::SightValue("scope", "scope 6"), 0);
    for (ms::PendingWrites::Edit const& e : pending.TakeDue(0)) {
        std::wstring const section16(e.section.begin(), e.section.end());
        std::wstring const key16(e.key.begin(), e.key.end());
        std::wstring const value16(e.value.begin(), e.value.end());
        CHECK(WritePrivateProfileStringW(section16.c_str(), key16.c_str(), value16.c_str(), file.c_str()) != 0);
    }
    std::string text;
    {
        std::ifstream in(file);
        for (std::string line; std::getline(in, line);) {
            text += line + "\n";
        }
    }
    CHECK(text.find("; ME VR Attempt settings") != std::string::npos);
    CHECK(text.find("; how the hands sit") != std::string::npos);
    CHECK(text.find("handPitch = 0") != std::string::npos);
    CHECK(text.find("pistol = dot") != std::string::npos);
    CHECK(text.find("physicalCrouch") == text.rfind("physicalCrouch"));  // replaced in place, not added again
    wchar_t value[64]{};
    GetPrivateProfileStringW(L"Hands", L"physicalCrouch", L"", value, 64, file.c_str());
    CHECK(std::wstring(value) == L"0");
    GetPrivateProfileStringW(L"Sights", L"sniperRifle", L"", value, 64, file.c_str());
    CHECK(std::wstring(value) == L"scope 6");
    std::filesystem::remove(file);
}

// Life-size stereo (core/vr/MetricStereo, research/30): off-centre projections from the runtime's FOVs, the eyes'
// offsets in game units, and how a pair reads in the headset: the metric pair exactly, Halcyon's Stereo 2 as measured.
void TestMetricStereo() {
    namespace mst = mevr::vr::metric_stereo;
    auto approx = [](double a, double b, double tol) { return std::fabs(a - b) <= tol; };
    // The PS VR2's left eye as SteamVR reports it (log 2026-10-23); the right eye mirrors it.
    mst::Fov const left{-1.0734f, 0.7583f, 0.9257f, -0.9257f};
    mst::Fov const right{-0.7583f, 1.0734f, 0.9257f, -0.9257f};

    // Off-centre terms: the frustum's edges land on NDC +-1, straight ahead on p8 (0 for a symmetric frustum).
    mst::Tangents const t = mst::TangentsOf(left);
    mst::OffCentre const p = mst::Projection(t);
    float x = 0.0f, y = 0.0f;
    mst::Project(p, {t.left * 2.0f, t.up * 2.0f, 2.0f}, &x, &y);
    CHECK(approx(x, -1.0, 1e-5) && approx(y, 1.0, 1e-5));
    mst::Project(p, {t.right * 7.0f, t.down * 7.0f, 7.0f}, &x, &y);
    CHECK(approx(x, 1.0, 1e-5) && approx(y, -1.0, 1e-5));
    mst::Project(p, {0.0f, 0.0f, 3.0f}, &x, &y);
    CHECK(approx(x, p.p8, 1e-6) && approx(p.p8, 0.3208, 1e-3) && approx(y, 0.0, 1e-6));   // the left eye looks right of centre
    mst::OffCentre const sym = mst::Projection(mst::TangentsOf(mst::Fov{-0.9f, 0.9f, 0.8f, -0.8f}));
    CHECK(approx(sym.p8, 0.0, 1e-6) && approx(sym.p9, 0.0, 1e-6) && approx(sym.p0, 1.0 / std::tan(0.9), 1e-5));
    // The same NDC is a different direction in each eye's own frustum: 24 degrees apart either way at the centre,
    // which is why screen-space UI in the eye images keeps the symmetric window.
    mst::OffCentre const pr = mst::Projection(mst::TangentsOf(right));
    double const centreL = std::atan(-p.p8 / p.p0) * 57.29578, centreR = std::atan(-pr.p8 / pr.p0) * 57.29578;
    CHECK(approx(centreL, -24.1, 0.2) && approx(centreR, 24.1, 0.2));

    // Halcyon's symmetric window holds both eyes' frusta, and the per-eye frusta are never wider than it.
    mst::Fov const window = mst::UnionWindow(left, right, 1.0f, 1.0f);
    CHECK(approx(window.right, 1.0734, 1e-6) && approx(window.left, -1.0734, 1e-6) && approx(window.up, 0.9257, 1e-6));
    for (float fill : {0.8f, 1.0f, 1.2f}) {
        mst::Fov const w = mst::UnionWindow(left, right, fill, fill);
        for (mst::Fov const& eye : {left, right}) {
            mst::Fov const f = mst::Filled(eye, fill, fill);
            CHECK(f.left >= w.left - 1e-6f && f.right <= w.right + 1e-6f && f.up <= w.up + 1e-6f && f.down >= w.down - 1e-6f);
        }
    }
    CHECK(mst::Filled(left, 2.0f, 2.0f).left == -mst::kMaxAngle);

    // The eyes' offsets: the runtime's eye distance in game units, along the camera's axes.
    mst::EyeInputs in;
    in.orientation[0] = in.orientation[1] = {0.0579f, -0.2229f, -0.0184f, 0.9730f};
    in.position[0] = {-0.1689f, -0.3938f, -0.8865f};
    in.position[1] = {-0.1122f, -0.3977f, -0.8593f};
    in.unitsPerMeter = 100.0f;
    in.viewOrientation = in.orientation[0];
    in.viewFollowsHead = false;   // head-relative: +-half the eye distance along the head's right
    mst::EyeOffsets o = mst::BodyOffsets(in);
    CHECK(approx(o.ipdM, 0.0630, 0.0005) && approx(o.ipdUU, 6.30, 0.05) && approx(o.cantDeg, 0.0, 0.01));
    CHECK(approx(o.body[0].x, -3.15, 0.03) && approx(o.body[1].x, 3.15, 0.03) && approx(o.body[1].y, 0.0, 0.03) &&
          approx(o.body[1].z, 0.0, 0.03));
    in.viewFollowsHead = true;    // app-space deltas: the head turned ~26 degrees, so the eyes sit fore and aft too
    o = mst::BodyOffsets(in);
    CHECK(approx(std::sqrt(o.body[1].x * o.body[1].x + o.body[1].y * o.body[1].y + o.body[1].z * o.body[1].z), 3.15, 0.03));
    CHECK(approx(o.body[1].x, 2.835, 0.02) && approx(o.body[1].z, -1.36, 0.02) && approx(o.body[0].x, -o.body[1].x, 1e-4));
    in.depth = 1.5f;
    CHECK(approx(mst::BodyOffsets(in).ipdUU, 9.45, 0.08));

    // How a point straight ahead reads. The metric pair: exactly where it is.
    double const ipd = 0.0621;   // the user's PS VR2 in the sights job's snapshot (research/21 section 12)
    mst::RenderedPair const metric = mst::MetricPair(static_cast<float>(ipd));
    for (double d : {0.2, 0.3, 1.0, 10.0}) {
        CHECK(approx(mst::PerceivedDepth(metric, d, ipd), d, d * 1e-4));
        CHECK(approx(mst::DisparityErrorArcmin(metric, d, ipd), 0.0, 0.01));
    }
    // Halcyon's, with the user's profile (sfr2HalfEyeUU 3.4 at 100 units per metre, convergence 0.012 over a
    // 61.5-degree half window, swap on): the shift is divergent. 0.2 / 0.3 / 0.5 m read as 0.21 / 0.34 / 0.68 m, 1 m
    // as 2.6 m, and anything past 1.5 m needs the eyes to diverge (2.5 degrees for the far scenery).
    mst::RenderedPair const user = mst::HalcyonPair(3.4f, 100.0f, 0.012f, 1.0734f, true, 0.0f);
    CHECK(approx(mst::PerceivedDepth(user, 0.2, ipd), 0.210, 0.005));
    CHECK(approx(mst::PerceivedDepth(user, 0.3, ipd), 0.340, 0.005));
    CHECK(approx(mst::PerceivedDepth(user, 0.5, ipd), 0.677, 0.005));
    CHECK(approx(mst::PerceivedDepth(user, 1.0, ipd), 2.61, 0.03));
    CHECK(mst::PerceivedDepth(user, 10.0, ipd) < 0.0);   // beyond infinity
    CHECK(approx(mst::DisparityErrorArcmin(user, 1e6, ipd), -151.9, 0.5));
    // Swap off: the same shift converges instead; the far scenery reads 1.4 m away.
    mst::RenderedPair const unswapped = mst::HalcyonPair(3.4f, 100.0f, 0.012f, 1.0734f, false, 0.0f);
    CHECK(approx(mst::PerceivedDepth(unswapped, 1e6, ipd), 1.405, 0.01));
    CHECK(approx(mst::PerceivedDepth(unswapped, 0.3, ipd), 0.229, 0.005));
    // Without the shift, the separation alone (6.8 uu against 6.2 cm): 9% near; with the head turned 45 degrees from
    // the body, upstream's eyes stay on the body's right axis and the world reads 29% further.
    mst::RenderedPair const noShift = mst::HalcyonPair(3.4f, 100.0f, 0.0f, 1.0734f, true, 0.0f);
    CHECK(approx(mst::PerceivedDepth(noShift, 1.0, ipd), 0.913, 0.005));
    mst::RenderedPair const turned = mst::HalcyonPair(3.4f, 100.0f, 0.0f, 1.0734f, true, 0.7854f);
    CHECK(approx(mst::PerceivedDepth(turned, 1.0, ipd), 1.291, 0.01));

    std::printf("metric stereo: a point straight ahead (m) reads at - metric / Halcyon (user: swap on) / Halcyon (swap off)"
                " / Halcyon, head 45 deg, no shift\n");
    for (double d : {0.2, 0.3, 0.5, 1.0, 2.0, 10.0}) {
        std::printf("  %5.1f  %7.3f  %8.3f  %7.3f  %7.3f   (disparity error %+.1f arcmin)\n", d,
            mst::PerceivedDepth(metric, d, ipd), mst::PerceivedDepth(user, d, ipd), mst::PerceivedDepth(unswapped, d, ipd),
            mst::PerceivedDepth(turned, d, ipd), mst::DisparityErrorArcmin(user, d, ipd));
    }
}

// Comfort (research/31): the snap's state machine, the smooth turn, yaw units, the camera guessed through a turn (and
// body follow across a snap), the vignette's fade and picture, walking toward the left hand, the pad flag, the menu.
void TestComfort() {
    namespace cf = mevr::vr::comfort;
    namespace ms = mevr::vr::menu_settings;
    cf::Settings settings;  // the defaults: smooth 120 deg/s, snap 45 deg past 0.7, re-arm inside 0.3, 0.25 s apart

    // ---- Snap: one turn per flick, re-armed near the centre, a cooldown between ----
    {
        cf::SnapTurn snap;
        double t = 0.0;
        // Taking over with the stick already pushed: nothing until it has been let go.
        CHECK(cf::StepSnap(snap, 1.0f, t, settings) == 0.0f);
        CHECK(cf::StepSnap(snap, 0.9f, t += 0.011, settings) == 0.0f);
        CHECK(cf::StepSnap(snap, 0.1f, t += 0.011, settings) == 0.0f);  // centred: armed
        CHECK(cf::StepSnap(snap, 0.5f, t += 0.011, settings) == 0.0f);  // not far enough
        CHECK(cf::StepSnap(snap, 0.75f, t += 0.011, settings) == 45.0f);  // a flick right
        for (int i = 0; i < 60; ++i) {
            CHECK(cf::StepSnap(snap, 1.0f, t += 0.011, settings) == 0.0f);  // held: once only
        }
        CHECK(cf::StepSnap(snap, 0.4f, t += 0.011, settings) == 0.0f);  // not back near the centre yet
        CHECK(cf::StepSnap(snap, 0.2f, t += 0.011, settings) == 0.0f);  // re-armed
        CHECK(cf::StepSnap(snap, -0.9f, t += 0.011, settings) == -45.0f);  // a flick left
        // Flicked again at once: re-armed but cooling down, then it goes.
        double const after = t;
        CHECK(cf::StepSnap(snap, 0.0f, t += 0.011, settings) == 0.0f);
        CHECK(cf::StepSnap(snap, -1.0f, t += 0.011, settings) == 0.0f);  // 22 ms after: too soon
        float turned = 0.0f;
        while (t - after < 0.3 && turned == 0.0f) {
            turned = cf::StepSnap(snap, -1.0f, t += 0.011, settings);
        }
        CHECK(turned == -45.0f && t - after >= settings.snapCooldown - 1e-9);
        // The angle follows the setting; Reset disarms.
        cf::Settings sixty = settings;
        sixty.snapDegrees = 60.0f;
        snap.Reset();
        CHECK(cf::StepSnap(snap, 0.0f, t += 1.0, sixty) == 0.0f);
        CHECK(cf::StepSnap(snap, 1.0f, t += 0.011, sixty) == 60.0f);
        // Not a number from the controllers: no turn.
        snap.Reset();
        CHECK(cf::StepSnap(snap, std::numeric_limits<float>::quiet_NaN(), t += 1.0, settings) == 0.0f);
    }

    // ---- Smooth: linear past the dead zone, the full speed at full deflection ----
    {
        CHECK(cf::SmoothTurnDegrees(0.1f, 1.0 / 90.0, settings) == 0.0);
        CHECK(!cf::StickTurns(0.1f, settings) && cf::StickTurns(-0.3f, settings));
        CHECK(NearD(cf::SmoothTurnDegrees(1.0f, 1.0 / 90.0, settings), 120.0 / 90.0, 1e-9));
        CHECK(NearD(cf::SmoothTurnDegrees(-1.0f, 1.0 / 90.0, settings), -120.0 / 90.0, 1e-9));
        double const half = cf::SmoothTurnDegrees(0.575f, 0.01, settings);  // halfway past the 0.15 dead zone
        CHECK(NearD(half, 0.5 * 120.0 * 0.01, 1e-6));
        CHECK(NearD(cf::SmoothTurnDegrees(1.0f, 2.0, settings), 12.0, 1e-9));  // a hitch: 0.1 s at most
        // A second at full deflection at 90 ticks a second, in whole units with the remainder kept: 120 degrees.
        cf::YawUnits units;
        int total = 0;
        for (int i = 0; i < 90; ++i) {
            total += units.Take(cf::SmoothTurnDegrees(1.0f, 1.0 / 90.0, settings));
        }
        CHECK(Near(total, Units(120.0), 1));
        cf::Settings slow = settings;
        slow.turnSpeed = 10.0f;
        units.Reset();
        total = 0;
        for (int i = 0; i < 900; ++i) {  // 0.011 degrees a tick: 2 units, with a remainder each time
            total += units.Take(cf::SmoothTurnDegrees(1.0f, 1.0 / 90.0, slow));
        }
        CHECK(Near(total, Units(100.0), 1));
        units.Reset();
        CHECK(units.Take(-45.0) == -Units(45.0) && units.Take(0.0) == 0);
    }

    // ---- The camera through a turn: what arm IK maps the view with before the camera updates ----
    {
        // Explore: the pawn keeps its facing; only the camera's yaw turns.
        cf::CameraGuess const still = cf::TurnCamera(1000.0, 500.0, Units(30), Units(45), false, 0.0, 0.0);
        CHECK(still.x == 1000.0 && still.y == 500.0 && still.yaw == Units(75));
        // Combat: the pawn turned too, so the first-person camera (on Shepard's mesh) went round his location.
        cf::CameraGuess const round = cf::TurnCamera(110.0, 0.0, 0, Units(90), true, 100.0, 0.0);
        CHECK(NearD(round.x, 100.0, 1e-6) && NearD(round.y, 10.0, 1e-6) && round.yaw == Units(90));  // +yaw turns X toward Y

        // The view mapped from the guessed camera is the view the turned camera gives: a controller lands in the same
        // place in the world either way, and 45 degrees round the head from where it was before the snap.
        using namespace mevr::engine;
        XrPosef head{};
        head.orientation = AxisAngle(0, 1, 0, 20);  // the player looks 20 degrees left
        head.position = {0.0f, 1.6f, 0.0f};
        UnrealRotation const look{0, -Units(20), 0};  // Halcyon's head look (+ right)
        UnrealVector const offset{0, 0, 0};
        UnrealVector const camera{2000.0, -300.0, 160.0};
        UnrealRotation const before{0, Units(10), 0};
        cf::CameraGuess const guess = cf::TurnCamera(camera.x, camera.y, before.yaw, Units(45), false, 0.0, 0.0);
        TrackingFrame const guessed = FromCameraView({guess.x, guess.y, camera.z}, {0, guess.yaw, 0}, look, offset, head, 100.0);
        TrackingFrame const turned = FromCameraView(camera, {0, before.yaw + Units(45), 0}, look, offset, head, 100.0);
        TrackingFrame const old = FromCameraView(camera, before, look, offset, head, 100.0);
        XrVector3f const hand{0.25f, 1.2f, -0.45f};
        UnrealVector const a = ToWorld(guessed, hand);
        UnrealVector const b = ToWorld(turned, hand);
        UnrealVector const c = ToWorld(old, hand);
        CHECK(NearD(a.x, b.x, 1e-6) && NearD(a.y, b.y, 1e-6) && NearD(a.z, b.z, 1e-6));
        double const aroundHead = std::atan2(b.y - turned.headWorld.y, b.x - turned.headWorld.x) -
            std::atan2(c.y - old.headWorld.y, c.x - old.headWorld.x);
        CHECK(NearD(std::remainder(aroundHead * 180.0 / std::numbers::pi, 360.0), 45.0, 1e-6));

        // Body follow through a snap: its state is in tracking space, which a turn doesn't move, so it neither
        // re-anchors nor eases (no lag, no shoulder swing); the body's facing in the world turns with the view at once.
        using namespace mevr::ik;
        BodyFollow follow;
        FollowInput in;
        in.pivot = {0.05, 1.5, -0.02};
        in.headYaw = 0.35;  // the head turned left of the body
        in.facingYaw = 0.35;
        in.haveRecenters = true;
        in.deltaSeconds = 1.0 / 90.0;
        CHECK(follow.Update(in).anchored == FollowAnchor::Start);
        FollowState settled{};
        for (int i = 0; i < 90; ++i) {
            settled = follow.Update(in);
        }
        FollowState const snapped = follow.Update(in);  // the tick after a snap: the same tracking-space inputs
        CHECK(snapped.anchored == FollowAnchor::None && NearD(snapped.yaw, settled.yaw, 1e-9) &&
              NearD(snapped.feet.x, settled.feet.x, 1e-9) && NearD(snapped.feet.z, settled.feet.z, 1e-9));
        auto const worldYaw = [&](TrackingFrame const& frame) {
            XrVector3f const at{0.0f, 1.5f, 0.0f};
            XrVector3f const ahead{static_cast<float>(-std::sin(snapped.yaw)), 1.5f, static_cast<float>(-std::cos(snapped.yaw))};
            UnrealVector const p = ToWorld(frame, at);
            UnrealVector const q = ToWorld(frame, ahead);
            return std::atan2(q.y - p.y, q.x - p.x) * 180.0 / std::numbers::pi;
        };
        CHECK(NearD(std::remainder(worldYaw(guessed) - worldYaw(old), 360.0), 45.0, 1e-4));
    }

    // ---- The vignette: the fade, what the mod asks for, the picture ----
    {
        cf::VignetteFade fade;
        float const dt = 1.0f / 90.0f;
        int frames = 0;
        while (fade.value < 1.0f && frames < 100) {
            fade.Step(1.0f, dt);
            ++frames;
        }
        CHECK(frames >= 13 && frames <= 14);  // 0.15 s at 90 Hz
        frames = 0;
        while (fade.value > 0.0f && frames < 100) {
            fade.Step(0.0f, dt);
            ++frames;
        }
        CHECK(frames >= 13 && frames <= 14 && fade.value == 0.0f);
        CHECK(fade.Step(0.6f, 0.05f) > 0.33f && fade.value < 0.34f);  // 0.05 s of 0.15: a third of the way
        CHECK(fade.Step(0.6f, 1.0f) == 0.6f);                         // a long frame counts 0.1 s at most: there
        CHECK(fade.Step(0.3f, 0.01f) < 0.6f && fade.value > 0.3f);    // down toward a lower target
        CHECK(fade.Step(std::numeric_limits<float>::quiet_NaN(), 0.2f) < 0.3f);

        cf::Settings on = settings;
        on.vignette = true;
        on.vignetteStrength = 0.7f;
        cf::VignetteDrive drive;
        CHECK(drive.Target(false, false, false, 0.0, on) == 0.0f);
        CHECK(drive.Target(true, false, false, 0.1, on) == 0.7f);
        CHECK(drive.Target(false, true, false, 0.2, on) == 0.0f);  // moving: only with "turning and moving"
        on.vignetteWhen = cf::VignetteWhen::TurningAndMoving;
        CHECK(drive.Target(false, true, false, 0.3, on) == 0.7f);
        CHECK(drive.Target(false, false, true, 1.0, on) == 0.7f);  // a snap: a pulse...
        CHECK(drive.Target(false, false, false, 1.15, on) == 0.7f);
        CHECK(drive.Target(false, false, false, 1.25, on) == 0.0f);  // ...for 0.2 s
        cf::Settings off = on;
        off.vignette = false;
        CHECK(drive.Target(true, true, true, 2.0, off) == 0.0f);

        float const fov[2][4] = {{-0.9f, 0.75f, 0.8f, -0.85f}, {-0.75f, 0.9f, 0.8f, -0.85f}};
        cf::HalfExtents const extents = cf::CoverFov(fov);
        CHECK(extents.width > std::tan(0.9f) && extents.height > std::tan(0.85f));
        float const wide[2][4] = {{-1.5f, 1.5f, 1.5f, -1.5f}, {-1.5f, 1.5f, 1.5f, -1.5f}};
        CHECK(cf::CoverFov(wide).width < 4.0f);  // 75 degrees at most
        int const size = 64;
        std::vector<std::uint8_t> const mask = cf::VignetteMask(size, size, extents.width, extents.height);
        CHECK(mask.size() == static_cast<std::size_t>(size * size));
        CHECK(mask[(size / 2) * size + size / 2] == 0);  // a clear centre
        CHECK(mask[0] == 255 && mask[size - 1] == 255);  // black corners
        bool rising = true;
        for (int x = size / 2 + 1; x < size; ++x) {
            rising = rising && mask[(size / 2) * size + x] >= mask[(size / 2) * size + x - 1];
        }
        CHECK(rising);
        // 30 degrees from the centre is still clear; 58 and past fully dark.
        int const clear = size / 2 + static_cast<int>(std::tan(25.0 * std::numbers::pi / 180.0) / extents.width * size / 2);
        CHECK(mask[(size / 2) * size + clear] == 0);
        std::vector<std::uint32_t> pixels;
        cf::VignettePixels(mask, 0.5f, pixels);
        CHECK(pixels.size() == mask.size() && pixels[0] == (127u << 24) && pixels[(size / 2) * size + size / 2] == 0u);
        cf::VignettePixels(mask, 1.0f, pixels);
        CHECK(pixels[0] == (255u << 24) && (pixels[0] & 0x00FFFFFFu) == 0u);  // premultiplied black

        // The host's CPU cost (plugin/Vignette): the 128 x 128 mask once a session, a refill per level while fading.
        auto const t0 = std::chrono::steady_clock::now();
        std::vector<std::uint8_t> const host = cf::VignetteMask(128, 128, extents.width, extents.height);
        auto const t1 = std::chrono::steady_clock::now();
        for (int level = 1; level <= 64; ++level) {
            cf::VignettePixels(host, level / 64.0f, pixels);
        }
        auto const t2 = std::chrono::steady_clock::now();
        std::printf("comfort vignette (CPU): the 128x128 mask %.3f ms once; a refill %.4f ms (64 levels, a full fade is ~14)\n",
            std::chrono::duration<double, std::milli>(t1 - t0).count(), std::chrono::duration<double, std::milli>(t2 - t1).count() / 64.0);
    }

    // ---- Walking toward the left hand ----
    {
        // The left controller pointing 90 degrees left, laid flat; straight down has no flat direction.
        float x = 0.0f, y = 0.0f, z = 0.0f;
        XrQuaternionf const left = AxisAngle(0, 1, 0, 90);
        cf::Forward(left.x, left.y, left.z, left.w, x, y, z);
        CHECK(NearD(x, -1.0, 1e-5) && NearD(z, 0.0, 1e-5));
        auto const yaw = cf::FlatYawLeft(x, y, z);
        CHECK(yaw && NearD(*yaw, std::numbers::pi / 2.0, 1e-5));
        XrQuaternionf const down = AxisAngle(1, 0, 0, -88);
        cf::Forward(down.x, down.y, down.z, down.w, x, y, z);
        CHECK(!cf::FlatYawLeft(x, y, z));
        XrQuaternionf const tilted = AxisAngle(1, 0, 0, -60);  // 60 degrees down: still forward
        cf::Forward(tilted.x, tilted.y, tilted.z, tilted.w, x, y, z);
        CHECK(cf::FlatYawLeft(x, y, z) && NearD(*cf::FlatYawLeft(x, y, z), 0.0, 1e-5));

        // Our turn, then Halcyon's (RotateMoveStickByHeadLook: left by its head look yaw): forward is the hand's yaw.
        auto const halcyon = [](SHORT& sx, SHORT& sy, int yawUnits) {
            float const a = static_cast<float>(yawUnits) * (6.2831853f / 65536.0f);
            float const c = std::cos(a), s = std::sin(a);
            float const lx = sx, ly = sy;
            sx = static_cast<SHORT>(std::clamp(lx * c - ly * s, -32768.0f, 32767.0f));
            sy = static_cast<SHORT>(std::clamp(lx * s + ly * c, -32768.0f, 32767.0f));
        };
        for (double const hand : {0.0, 40.0, 90.0, -70.0, 170.0}) {
            for (double const look : {0.0, 25.0, -60.0, 120.0}) {
                SHORT sx = 0, sy = 30000;  // forward
                int const head = Units(look);
                mevr::game::gamepad::RotateStick(sx, sy, cf::HandWalkUnits(hand * std::numbers::pi / 180.0, head));
                halcyon(sx, sy, head);
                double const went = std::atan2(-static_cast<double>(sx), static_cast<double>(sy)) * 180.0 / std::numbers::pi;  // + left
                CHECK(NearD(std::remainder(went - hand, 360.0), 0.0, 0.2));
                CHECK(std::hypot(static_cast<double>(sx), static_cast<double>(sy)) > 29900.0);  // the magnitude kept
            }
        }
        // moveFollowsHead off (Halcyon doesn't turn it): our turn alone is the hand's.
        SHORT sx = 0, sy = 30000;
        mevr::game::gamepad::RotateStick(sx, sy, cf::HandWalkUnits(std::numbers::pi / 2.0, 0));
        CHECK(sx < -29900 && std::abs(sy) < 100);  // forward became left
    }

    // ---- The pad: the game sees no right-stick X while the mod turns (not while the squad menu has the sticks) ----
    {
        using namespace mevr::game::gamepad;
        mevr::vr::controllers::State state;
        state.active = true;
        state.right.stickX = 0.9f;
        state.right.stickY = 0.4f;
        state.left.stickY = 1.0f;
        Latches latches;
        Options options;
        CHECK(FromControllers(state, latches, options).sThumbRX > 25000);
        options.modTurns = true;
        XINPUT_GAMEPAD const pad = FromControllers(state, latches, options);
        CHECK(pad.sThumbRX == 0 && pad.sThumbRY > 10000 && pad.sThumbLY > 30000);
        Options const back = FromFlags(-1.0f, 0.1f, 0.2f, ToFlags(options));
        CHECK(back.modTurns && !back.handWalk);
        options.handWalk = true;
        options.modTurns = false;
        CHECK(FromFlags(-1.0f, 0.1f, 0.2f, ToFlags(options)).handWalk);
        CHECK(!FromFlags(-1.0f, 0.1f, 0.2f, kFlagLeftTrigger).modTurns);  // an older mod's flags: the game turns
        CHECK((kFlagModTurns & (kRbSourceMask | kWheelStickMask | kFlagNoMeleeButton | kFlagNoGrenadeButton)) == 0);
        CHECK((kFlagHandWalk & (kRbSourceMask | kWheelStickMask | kFlagModTurns)) == 0);
    }

    // ---- The Mako's motion (research/44): moving and turning with hysteresis, the yaw wrap, the smoothing ----
    {
        cf::VehicleThresholds const t;  // 150 units/s, 20 deg/s, off at 60%
        cf::VehicleMotion motion;
        double const dt = 1.0 / 90.0;
        // Standing still, then a straight drive: moving, not turning.
        cf::VehicleMotionResult r = motion.Step(0.0f, 0.0f, 0.0f, 1000, dt, t);
        CHECK(!r.moving && !r.turning && r.speed == 0.0 && r.yawRate == 0.0);
        r = motion.Step(300.0f, 0.0f, 0.0f, 1000, dt, t);
        CHECK(r.moving && !r.turning && NearD(r.speed, 300.0, 1e-9));
        // Slowing: still moving at 100 (above 90, the 60% stop), off at 80.
        r = motion.Step(100.0f, 0.0f, 0.0f, 1000, dt, t);
        CHECK(r.moving);
        r = motion.Step(80.0f, 0.0f, 0.0f, 1000, dt, t);
        CHECK(!r.moving);
        // ...and not on again at 100 (below 150).
        r = motion.Step(100.0f, 0.0f, 0.0f, 1000, dt, t);
        CHECK(!r.moving);
        // Speed is the vector's length, whichever way it points (a jump counts).
        r = motion.Step(0.0f, 0.0f, -200.0f, 1000, dt, t);
        CHECK(r.moving);

        // Turning: 30 degrees a second of yaw for a while. The rate is smoothed (0.1 s), so one frame isn't enough.
        motion.Reset();
        int yaw = 0;
        int const step = static_cast<int>(std::lround(30.0 * dt * 65536.0 / 360.0));  // 30 deg/s
        r = motion.Step(0.0f, 0.0f, 0.0f, yaw, dt, t);
        CHECK(!r.turning);  // the first step has no rate
        yaw += step;
        r = motion.Step(0.0f, 0.0f, 0.0f, yaw, dt, t);
        CHECK(!r.turning && r.yawRate > 0.0 && r.yawRate < 30.0);  // one frame: part of the way
        for (int i = 0; i < 60; ++i) {
            yaw += step;
            r = motion.Step(0.0f, 0.0f, 0.0f, yaw, dt, t);
        }
        CHECK(r.turning && NearD(r.yawRate, 30.0, 0.5));
        // The yaw's wrap at 65536 is no jump: keep turning across it.
        motion.Reset();
        yaw = 65536 - 3 * step;
        motion.Step(0.0f, 0.0f, 0.0f, yaw, dt, t);
        for (int i = 0; i < 60; ++i) {
            yaw = (yaw + step) & 0xFFFF;
            r = motion.Step(0.0f, 0.0f, 0.0f, yaw, dt, t);
        }
        CHECK(r.turning && NearD(r.yawRate, 30.0, 0.5));
        // A signed yaw (the engine's rotators go negative too) wraps the same way, turning left.
        motion.Reset();
        yaw = 100;
        motion.Step(0.0f, 0.0f, 0.0f, yaw, dt, t);
        for (int i = 0; i < 60; ++i) {
            yaw -= step;
            r = motion.Step(0.0f, 0.0f, 0.0f, yaw, dt, t);
        }
        CHECK(r.turning && NearD(r.yawRate, -30.0, 0.5));
        // Stopping the turn: off once the smoothed rate falls under 12 (60% of 20), after a few frames.
        int frames = 0;
        while (r.turning && frames < 100) {
            r = motion.Step(0.0f, 0.0f, 0.0f, yaw, dt, t);
            ++frames;
        }
        CHECK(!r.turning && frames > 1 && frames < 30);
        // A single frame's jump (a landing) at a huge rate counts only as far as the smoothing lets it.
        motion.Reset();
        motion.Step(0.0f, 0.0f, 0.0f, 0, dt, t);
        r = motion.Step(0.0f, 0.0f, 0.0f, 1820, dt, t);  // 10 degrees in one frame: 900 deg/s raw
        CHECK(r.yawRate < 150.0);
        // A long or broken frame time is clamped (no division by zero, no infinite rate).
        motion.Reset();
        motion.Step(0.0f, 0.0f, 0.0f, 0, 0.0, t);
        r = motion.Step(0.0f, 0.0f, 0.0f, 182, 0.0, t);
        CHECK(std::isfinite(r.yawRate));
        r = motion.Step(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 182, std::numeric_limits<double>::infinity(), t);
        CHECK(std::isfinite(r.speed) && std::isfinite(r.yawRate) && !r.moving);
        // Sanitizing wild thresholds.
        cf::VehicleThresholds wild;
        wild.moveSpeed = -5.0f;
        wild.turnRate = std::numeric_limits<float>::quiet_NaN();
        wild.stop = 4.0f;
        cf::VehicleThresholds const sane = cf::SanitizeVehicle(wild);
        CHECK(sane.moveSpeed == 1.0f && sane.turnRate == 20.0f && sane.stop == 1.0f);
    }

    // ---- Settings and the menu's rows ----
    {
        CHECK(cf::ParseTurn(" Snap ") == cf::TurnStyle::Snap && cf::ParseTurn("game") == cf::TurnStyle::Game && !cf::ParseTurn("fast"));
        CHECK(cf::ParseVignetteWhen("move") == cf::VignetteWhen::TurningAndMoving && cf::ParseWalk("hand") == cf::WalkToward::LeftHand);
        cf::Settings wild;
        wild.snapDegrees = 500.0f;
        wild.turnSpeed = std::numeric_limits<float>::infinity();
        wild.vignetteStrength = 3.0f;
        wild.snapRearm = 0.9f;
        cf::Settings const sane = cf::Sanitize(wild);
        CHECK(sane.snapDegrees == 180.0f && sane.turnSpeed == 120.0f && sane.vignetteStrength == 1.0f &&
              sane.snapRearm < sane.snapThreshold);
        CHECK(ms::SnapChoice(30.0f) == "30" && ms::SnapChoice(45.0f) == "45" && ms::SnapChoice(50.0f) == "45" &&
              ms::SnapChoice(90.0f) == "60" && ms::SnapChoice(std::numeric_limits<float>::quiet_NaN()) == "45");
        CHECK(ms::TurnChoice("snap") == "snap" && ms::TurnChoice("game") == "smooth" && ms::TurnChoice("smooth") == "smooth");
        for (char const* key : {"turn", "snap", "vignette", "vignette_strength", "vignette_when", "walk"}) {
            CHECK(ms::Find(key) != nullptr);
        }
        CHECK(ms::Find("turn_speed") == nullptr);  // the game's turn rate can't be set: no slider
        CHECK(cf::Settings{}.turn == cf::TurnStyle::Game);
        CHECK(ms::FormatValue(ms::Find("vignette_strength")->format, 60.0f) == "60%");
    }
}

// The eye upscale's settings and constants (core/vr/Upscale, research/32): FSR 1's EASU constants for the
// headset's sizes, the affine map for Halcyon's fill rectangles, RCAS's sharpness.
void TestUpscale() {
    namespace vu = mevr::vr::upscale;
    CHECK(vu::Parse("fsr", 0.9).method == vu::Method::Fsr);
    CHECK(vu::Parse(" FSR1 ", 0.5).method == vu::Method::Fsr);
    CHECK(vu::Parse("off", 0.9).method == vu::Method::Off);
    CHECK(vu::Parse("", 0.9).method == vu::Method::Off);
    CHECK(vu::Parse("bilinear", 0.9).method == vu::Method::Off);
    CHECK(vu::Parse("fsr", 7.0).sharpness == 1.0f);
    CHECK(vu::Parse("fsr", -1.0).sharpness == 0.0f);
    CHECK(vu::Parse("fsr", std::nan("")).sharpness == 0.9f);
    CHECK(std::fabs(vu::RcasStops(1.0f)) < 1e-6f);
    CHECK(std::fabs(vu::RcasStops(0.0f) - 2.0f) < 1e-6f);
    CHECK(std::fabs(vu::RcasStops(0.9f) - 0.2f) < 1e-5f);
    auto const f = [](std::uint32_t u) {
        float v;
        std::memcpy(&v, &u, sizeof v);
        return v;
    };
    // The whole image: FsrEasuCon's own con0 (scale, and half a scaled pixel less half a texel).
    float const whole[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    vu::Constants const k = vu::MakeConstants(2118, 2160, 2824, 2880, whole, whole, 0.9f);
    CHECK(std::fabs(f(k.con0[0]) - 2118.0f / 2824.0f) < 1e-6f);
    CHECK(std::fabs(f(k.con0[1]) - 0.75f) < 1e-6f);
    CHECK(std::fabs(f(k.con0[2]) - (0.5f * 2118.0f / 2824.0f - 0.5f)) < 1e-6f);
    CHECK(std::fabs(f(k.con0[3]) - (0.5f * 0.75f - 0.5f)) < 1e-6f);
    CHECK(std::fabs(f(k.con1[0]) - 1.0f / 2118.0f) < 1e-9f);
    CHECK(std::fabs(f(k.con1[3]) + 1.0f / 2160.0f) < 1e-9f);
    CHECK(std::fabs(f(k.con2[1]) - 2.0f / 2160.0f) < 1e-9f);
    CHECK(std::fabs(f(k.con3[1]) - 4.0f / 2160.0f) < 1e-9f);
    CHECK(k.con3[2] == 0 && k.con3[3] == 0);
    CHECK(std::fabs(f(k.rcas[0]) - std::exp2(-0.2f)) < 1e-6f);
    CHECK((k.rcas[1] & 0xFFFFu) == (k.rcas[1] >> 16));
    CHECK((k.rcas[1] & 0xFFFFu) == 0x3AF7u);  // half(0.87055)
    CHECK(k.sizes[0] == 2118.0f && k.sizes[1] == 2160.0f && k.sizes[2] == 2824.0f && k.sizes[3] == 2880.0f);
    // A rectangle: the source's right half shown in the middle half of the target. Target pixel 200 (the image's
    // first column) reads the source at 199.75 (its column 200 less half a texel, plus half a target pixel's worth).
    float const fill[4] = {0.5f, 0.0f, 0.5f, 1.0f};
    float const rect[4] = {0.25f, 0.0f, 0.5f, 1.0f};
    vu::Constants const r = vu::MakeConstants(400, 400, 800, 800, fill, rect, 0.9f);
    CHECK(std::fabs(f(r.con0[0]) - 0.5f) < 1e-6f);
    CHECK(std::fabs(200.0f * f(r.con0[0]) + f(r.con0[2]) - 199.75f) < 1e-4f);
    CHECK(std::fabs(f(r.con0[1]) - 0.5f) < 1e-6f);
    CHECK(std::fabs(f(r.con0[3]) - (0.5f * 0.5f - 0.5f)) < 1e-6f);
    std::string const src = vu::ShaderSource("//A\n", "//F\n");
    CHECK(src.find("#define A_GPU 1") != std::string::npos && src.find("//A") != std::string::npos);
    CHECK(src.find("#define FSR_EASU_F 1") != std::string::npos && src.find("//F") < src.find("PSEasu"));
    CHECK(src.find("PSRcas") != std::string::npos && src.find("MEVR_EYE_OUT") != std::string::npos);
}

// The weapon drawn, like exploring (core/vr/CombatStance, research/38).
void TestCombatStance() {
    namespace cs = mevr::vr::combat_stance;
    // A: the decision, in priority order.
    cs::Inputs in;
    in.headTrackedView = in.onFoot = in.pawn = in.firstPerson = in.drawn = in.node = in.hook = in.orderKnown = true;
    in.handBones = in.handPose = true;
    CHECK(cs::Decide(in) == cs::Why::Active);
    // The arms keep the combat pose: without their bones, or before it's learned for this weapon, the game's pose.
    cs::Inputs learning = in;
    learning.handPose = false;
    CHECK(cs::Decide(learning) == cs::Why::LearningHands);
    learning.handBones = false;
    CHECK(cs::Decide(learning) == cs::Why::NoHandBones);
    learning.orderKnown = false;
    CHECK(cs::Decide(learning) == cs::Why::OrderUnknown);  // the order is checked first
    learning = in;
    learning.handPose = false;
    learning.crouched = true;
    CHECK(cs::Decide(learning) == cs::Why::Crouched);  // nothing is learned crouched: only the standing combat pose
    cs::Inputs off = in;
    off.enabled = false;
    CHECK(cs::Decide(off) == cs::Why::Off);
    cs::Inputs crouched = in;
    crouched.crouched = true;
    CHECK(cs::Decide(crouched) == cs::Why::Crouched);
    crouched.drawn = false;
    CHECK(cs::Decide(crouched) == cs::Why::Holstered);  // holstered says so first
    cs::Inputs cover = in;
    cover.cover = true;
    CHECK(cs::Decide(cover) == cs::Why::Cover);
    cs::Inputs scoped = in;
    scoped.scoped = true;
    CHECK(cs::Decide(scoped) == cs::Why::Scoped);
    cs::Inputs mako = in;
    mako.pawn = false;
    CHECK(cs::Decide(mako) == cs::Why::NoPawn);
    mako.onFoot = false;
    CHECK(cs::Decide(mako) == cs::Why::NotOnFoot);
    cs::Inputs third = in;
    third.firstPerson = false;
    CHECK(cs::Decide(third) == cs::Why::NotFirstPerson);
    cs::Inputs vrOff = in;
    vrOff.headTrackedView = false;
    CHECK(cs::Decide(vrOff) == cs::Why::NoView);
    cs::Inputs storm = in;
    storm.storming = true;
    CHECK(cs::Decide(storm) == cs::Why::Active);
    storm.stanceWhileStorming = false;
    CHECK(cs::Decide(storm) == cs::Why::Storming);
    cs::Inputs order = in;
    order.orderKnown = false;
    CHECK(cs::Decide(order) == cs::Why::OrderUnknown);
    order.orderKnown = true;
    order.orderInside = true;
    CHECK(cs::Decide(order) == cs::Why::OrderInside);
    cs::Inputs noNode = in;
    noNode.node = false;
    CHECK(cs::Decide(noNode) == cs::Why::NoNode);
    cs::Inputs noHook = in;
    noHook.hook = false;
    CHECK(cs::Decide(noHook) == cs::Why::NoHook);

    // A: the tick order. Before: the tag moves between calls, never during one.
    cs::OrderCheck before;
    for (int i = 0; i < cs::OrderCheck::kNeeded + 1; ++i) {
        before.Feed(0.011f, i, i);
        CHECK(i < cs::OrderCheck::kNeeded ? before.Result() == cs::OrderCheck::Order::Unknown
                                          : before.Result() == cs::OrderCheck::Order::Before);
    }
    // Calls with DeltaTime 0 and repeated calls without a tick in between decide nothing.
    cs::OrderCheck idle;
    for (int i = 0; i < 100; ++i) {
        idle.Feed(0.0f, i, i + 1);
        idle.Feed(0.011f, 7, 7);
    }
    CHECK(idle.Result() == cs::OrderCheck::Order::Unknown && idle.InsideCalls() == 0);
    // Inside: the tag moves during the call (twice to decide), and it sticks.
    cs::OrderCheck inside;
    inside.Feed(0.011f, 1, 2);
    CHECK(inside.Result() == cs::OrderCheck::Order::Unknown);
    inside.Feed(0.011f, 2, 3);
    CHECK(inside.Result() == cs::OrderCheck::Order::Inside);
    for (int i = 10; i < 100; ++i) {
        inside.Feed(0.011f, i, i);
    }
    CHECK(inside.Result() == cs::OrderCheck::Order::Inside);
    // One stray tick during a call keeps Before from being concluded.
    cs::OrderCheck stray;
    stray.Feed(0.011f, 0, 1);
    for (int i = 2; i < 100; ++i) {
        stray.Feed(0.011f, i, i);
    }
    CHECK(stray.Result() == cs::OrderCheck::Order::Unknown);

    // B: research/38's table. The explore eye from (0,0,120) and fpExplore (-50.1, 0, 25.1).
    cs::Vec const eye = cs::ExploreEye({0, 0, 120}, {-50.1, 0, 25.1});
    CHECK(std::fabs(eye.x - 50.1) < 1e-9 && eye.y == 0.0 && std::fabs(eye.z - 94.9) < 1e-9);
    cs::Vec const combat = cs::ConstantEyeOffset({-30, 20, 90}, eye);
    CHECK(std::fabs(combat.x + 80.1) < 1e-9 && std::fabs(combat.y - 20.0) < 1e-9 && std::fabs(combat.z + 4.9) < 1e-9);
    cs::Vec const sprint = cs::ConstantEyeOffset({-10, 40, 90}, eye);
    CHECK(std::fabs(sprint.x + 60.1) < 1e-9 && std::fabs(sprint.y - 40.0) < 1e-9 && std::fabs(sprint.z + 4.9) < 1e-9);
    cs::Vec const aim = cs::ConstantEyeOffset({-30, 20, 80}, eye);
    CHECK(std::fabs(aim.x + 80.1) < 1e-9 && std::fabs(aim.z + 14.9) < 1e-9);
    cs::Vec const explore = cs::ConstantEyeOffset({0, 0, 120}, eye);  // explore itself: fpExplore again
    CHECK(std::fabs(explore.x + 50.1) < 1e-9 && std::fabs(explore.z - 25.1) < 1e-9);
    CHECK(cs::ConstantEyeApplies("SFXCameraMode_Combat", "CombatCam0"));
    CHECK(cs::ConstantEyeApplies("SFXCameraMode_CombatStorm", "Default__SFXCameraMode_CombatStorm"));
    CHECK(cs::ConstantEyeApplies("SFXCameraMode_TightAim", "CombatTightAim0"));
    CHECK(cs::ConstantEyeApplies("SFXCameraMode_Explore", "ExploreCam0"));
    CHECK(!cs::ConstantEyeApplies("SFXCameraMode_Combat", "CombatCrouchCam0"));  // crouch keeps today's
    CHECK(!cs::ConstantEyeApplies("SFXCameraMode_TightAim", "CombatCrouchTightAim0"));
    CHECK(!cs::ConstantEyeApplies("SFXCameraMode_SniperAim", "SniperCam0"));
    CHECK(!cs::ConstantEyeApplies("SFXCameraMode_Vehicle", "MakoCam"));
    CHECK(!cs::ConstantEyeApplies("SFXCameraMode_HipAimCover", "x"));
    // A physical crouch: the crouch modes take the constant eye too (only the real head lowers the view); never cover,
    // the scope or the Mako.
    CHECK(cs::ConstantEyeApplies("SFXCameraMode_Combat", "CombatCrouchCam0", true));
    CHECK(cs::ConstantEyeApplies("SFXCameraMode_TightAim", "CombatCrouchTightAim0", true));
    CHECK(cs::ConstantEyeApplies("SFXCameraMode_Combat", "CombatCam0", true));
    CHECK(!cs::ConstantEyeApplies("SFXCameraMode_SniperAim", "SniperCrouchCam0", true));
    CHECK(!cs::ConstantEyeApplies("SFXCameraMode_HipAimCover", "CoverCrouch", true));
    {
        mevr::game::gamepad::Options o;
        o.physicalCrouch = true;
        CHECK(mevr::game::gamepad::FromFlags(0, 0, 0, mevr::game::gamepad::ToFlags(o)).physicalCrouch);
        CHECK(!mevr::game::gamepad::FromFlags(0, 0, 0, mevr::game::gamepad::ToFlags({})).physicalCrouch);
    }
    CHECK(cs::IsExploreMode("SFXCameraMode_Explore", "ExploreCam0"));
    CHECK(!cs::IsExploreMode("SFXCameraMode_ExploreStorm", "x"));
    CHECK(!cs::IsExploreMode("SFXCameraMode_Combat", "CombatCam0"));
    CHECK(cs::PlausibleHook({-30, 20, 90}));
    CHECK(!cs::PlausibleHook({std::numeric_limits<double>::quiet_NaN(), 0, 0}));
    CHECK(!cs::PlausibleHook({0, 0, 5000}));

    // B, the blend's stored offset: the rotator's axes (65536 a turn).
    auto closeTo = [](cs::Vec a, cs::Vec b) { return std::fabs(a.x - b.x) < 1e-6 && std::fabs(a.y - b.y) < 1e-6 && std::fabs(a.z - b.z) < 1e-6; };
    CHECK(closeTo(cs::RotateToWorld(0, 0, {1, 2, 3}), {1, 2, 3}));
    CHECK(closeTo(cs::RotateToWorld(0, 16384, {1, 0, 0}), {0, 1, 0}));    // yaw 90: forward is +Y
    CHECK(closeTo(cs::RotateToWorld(0, 16384, {0, 1, 0}), {-1, 0, 0}));   // and right is -X
    CHECK(closeTo(cs::RotateToWorld(16384, 0, {1, 0, 0}), {0, 0, 1}));    // pitch 90: forward is up
    CHECK(closeTo(cs::RotateToWorld(16384, 0, {0, 0, 1}), {-1, 0, 0}));   // and up is back
    CHECK(closeTo(cs::RotateToWorld(0, -16384, {1, 0, 0}), cs::RotateToWorld(0, 49152, {1, 0, 0})));
    {
        // The sniper's first draw (2026-10-25 18:27:55): the destination's vanilla Offset (90, -22, 25), written to the
        // constant eye's (-80.1, 20, -4.9) after the blend began. Model GetCameraLocation = hook - R * Offset.
        auto camera = [](cs::Vec hook, std::int32_t pitch, std::int32_t yaw, cs::Vec offset) {
            cs::Vec const r = cs::RotateToWorld(pitch, yaw, offset);
            return cs::Vec{hook.x - r.x, hook.y - r.y, hook.z - r.z};
        };
        std::int32_t const pitch = -1200;
        std::int32_t const yaw = -28136;
        cs::Vec const fromHook{-39316.0, 154792.0, -690.0};
        cs::Vec const toHook{-39346.0, 154780.0, -720.0};
        cs::Vec const from = camera(fromHook, pitch, yaw, {-50.1, 0, 25.1});
        cs::Vec const vanilla{90, -22, 25};
        cs::Vec const written{-80.1, 20, -4.9};
        cs::Vec const toThen = camera(toHook, pitch, yaw, vanilla);
        cs::Vec const stored{toThen.x - from.x, toThen.y - from.y, toThen.z - from.z};
        cs::Vec const toNow = camera(toHook, pitch, yaw, written);
        cs::Vec const fixed = cs::BlendLocationOffset(stored, pitch, yaw, vanilla, written);
        // At T = 0 the view (To - LocationOffset) is From's camera again, and with the game's stored offset it isn't.
        CHECK(closeTo({toNow.x - fixed.x, toNow.y - fixed.y, toNow.z - fixed.z}, from));
        double const jump = std::hypot(toNow.x - stored.x - from.x, toNow.y - stored.y - from.y, toNow.z - stored.z - from.z);
        CHECK(jump > 170.0 && jump < 185.0);  // the 1.8 m the log measured
        // What InitializeTransition would have stored with the written Offset.
        CHECK(closeTo(fixed, {toNow.x - from.x, toNow.y - from.y, toNow.z - from.z}));
        // An unchanged Offset leaves it alone.
        CHECK(closeTo(cs::BlendLocationOffset(stored, pitch, yaw, written, written), stored));
    }
    CHECK(cs::OffsetMoved({90, -22, 25}, {-80.1, 20, -4.9}));
    CHECK(!cs::OffsetMoved({-80.1, 20, -4.9}, {-80.1, 20.005, -4.9}));
    CHECK(!cs::OffsetMoved({0, 0, 0}, {std::numeric_limits<double>::quiet_NaN(), 0, 0}));

    // C: the ramp.
    CHECK(cs::RampNeedsWrite(3.0f, 2.0f, 1.5f, 1.5f));
    CHECK(!cs::RampNeedsWrite(1.5f, 1.5f, 1.5f, 1.5f));
    CHECK(!cs::RampNeedsWrite(3.0f, 2.0f, 0.0f, 1.5f));  // explore values not set yet: nothing written
    CHECK(!cs::RampNeedsWrite(3.0f, 2.0f, std::numeric_limits<float>::infinity(), 1.5f));
    CHECK(cs::UsableRate(1.5f) && !cs::UsableRate(-1.0f) && !cs::UsableRate(1000.0f));

    // D: the draw rate.
    CHECK(cs::ClampDrawRate(2.0) == 2.0);
    CHECK(cs::ClampDrawRate(0.5) == 1.0);
    CHECK(cs::ClampDrawRate(9.0) == 9.0);
    CHECK(cs::ClampDrawRate(25.0) == cs::kMaxDrawRate);
    CHECK(cs::ClampDrawRate(std::numeric_limits<double>::quiet_NaN()) == 1.0);
    CHECK(cs::RateNeedsWrite(1.0f, 2.0));
    CHECK(!cs::RateNeedsWrite(2.0f, 2.0));
    CHECK(cs::RateNeedsWrite(std::numeric_limits<float>::quiet_NaN(), 2.0));
    CHECK(std::string(cs::Name(cs::Why::Active)).find("explore") != std::string::npos);
}

// The arms keep the combat pose under the explore stance (core/ik/PoseGraft, research/38 A).
void TestPoseGraft() {
    using namespace mevr::ik;
    // root(0) > spine(1) > clavicle(2) > upper(3) > fore(4) > hand(5) > prop(6) > lhik(7); finger(8) under the hand;
    // camera(9) under the hand (a bone the graft must leave alone, with its child 10); leg(11) under the root.
    std::vector<int> const parents{-1, 0, 1, 2, 3, 4, 5, 6, 5, 5, 9, 0};
    std::vector<int> const camera{9};
    std::vector<int> const arm = Subtree(parents, 3, camera);
    CHECK((arm == std::vector<int>{3, 4, 5, 6, 7, 8}));
    CHECK((Subtree(parents, 5) == std::vector<int>{5, 6, 7, 8, 9, 10}));
    CHECK(Subtree(parents, 9, camera).empty());
    CHECK(Subtree(parents, 42).empty());
    std::vector<int> const bad{-1, 2, 0};  // bone 1's parent comes after it: never included
    CHECK((Subtree(bad, 0) == std::vector<int>{0, 2}));

    // A "combat" pose: every bone its own place and turn.
    auto const turn = [](double degrees, Vec3 axis) {
        double const h = degrees * std::numbers::pi / 360.0;
        Vec3 const a = Normalize(axis);
        return Quat{a.x * std::sin(h), a.y * std::sin(h), a.z * std::sin(h), std::cos(h)};
    };
    std::vector<Transform> combat(parents.size());
    for (std::size_t i = 0; i < combat.size(); ++i) {
        double const k = static_cast<double>(i);
        combat[i].translation = {10.0 * k, 3.0 * k - 5.0, 2.0 * k * k};
        combat[i].rotation = turn(17.0 * k, {1.0, 0.5 * k, -0.25});
    }
    std::vector<int> const armParents = ParentsOf(parents, arm);
    CHECK((armParents == std::vector<int>{2, 3, 4, 5, 6, 5}));
    std::vector<Transform> const relative = CaptureRelative(combat, arm, armParents);
    CHECK(relative.size() == arm.size());
    // An "explore" pose: the spine and clavicle elsewhere, the arm hanging differently, the prop and LHIK moved.
    std::vector<Transform> explore = combat;
    for (int bone : {1, 2, 3, 4, 5, 6, 7, 8, 11}) {
        explore[static_cast<std::size_t>(bone)].translation =
            explore[static_cast<std::size_t>(bone)].translation + Vec3{4.0, -7.0, 1.5 * bone};
        explore[static_cast<std::size_t>(bone)].rotation =
            Normalize(turn(9.0 + bone, {0.2, 1.0, 0.3}) * explore[static_cast<std::size_t>(bone)].rotation);
    }
    std::vector<Transform> const before = explore;
    CHECK(ApplyRelative(explore, arm, armParents, relative));
    // Every grafted bone sits in its parent's frame as in combat; the rest is the explore pose, bit for bit.
    for (std::size_t i = 0; i < arm.size(); ++i) {
        int const bone = arm[i];
        int const parent = parents[static_cast<std::size_t>(bone)];
        CHECK(NearT(Relative(explore[static_cast<std::size_t>(bone)], explore[static_cast<std::size_t>(parent)]),
            Relative(combat[static_cast<std::size_t>(bone)], combat[static_cast<std::size_t>(parent)]), 1e-6));
    }
    for (int bone : {0, 1, 2, 9, 10, 11}) {
        CHECK(NearT(explore[static_cast<std::size_t>(bone)], before[static_cast<std::size_t>(bone)], 0.0));
    }
    // The upper arm hangs from the explore clavicle: the hand isn't where combat had it in component space...
    CHECK(Length(explore[5].translation - combat[5].translation) > 1.0);
    // ...but the prop and LHIK are where combat had them relative to the hand (the gun in the hand, the foregrip on it).
    CHECK(NearT(Relative(explore[7], explore[5]), Relative(combat[7], combat[5]), 1e-6));
    CHECK(NearT(Relative(explore[6], explore[5]), Relative(combat[6], combat[5]), 1e-6));
    // Bad input writes nothing.
    std::vector<Transform> untouched = before;
    CHECK(!ApplyRelative(untouched, arm, armParents, std::span<Transform const>(relative).first(2)));
    std::vector<int> const rootBone{0};
    std::vector<int> const rootParent = ParentsOf(parents, rootBone);
    CHECK(CaptureRelative(combat, rootBone, rootParent).empty());  // the root has no parent
    CHECK(!ApplyRelative(untouched, rootBone, rootParent, std::span<Transform const>(relative).first(1)));
    CHECK(CaptureRelative(combat, arm, std::span<int const>(armParents).first(3)).empty());
    for (std::size_t i = 0; i < untouched.size(); ++i) {
        CHECK(NearT(untouched[i], before[i], 0.0));
    }

    // Difference: the largest move and turn of any element.
    std::vector<Transform> moved = relative;
    moved[2].translation = moved[2].translation + Vec3{0.0, 0.0, 0.5};
    moved[4].rotation = Normalize(turn(2.0, {0.0, 0.0, 1.0}) * moved[4].rotation);
    PoseDifference const d = Difference(relative, moved);
    CHECK(NearD(d.units, 0.5, 1e-9) && NearD(d.degrees, 2.0, 1e-6));
    CHECK(std::isinf(Difference(relative, std::span<Transform const>(relative).first(3)).units));

    // StillPose: ten samples in a row within the limits of the one before.
    StillPose still;
    CHECK(still.Feed(relative, 0.011) == StillPose::State::Learning);
    for (int i = 0; i < 9; ++i) {
        CHECK(still.Feed(relative, 0.011) == StillPose::State::Learning);
    }
    CHECK(still.Feed(relative, 0.011) == StillPose::State::Still);
    CHECK(still.StillFrames() == 10 && NearT(still.Latest()[3], relative[3], 0.0));
    CHECK(still.Feed(moved, 0.011) == StillPose::State::Still);  // settled: further samples change nothing
    CHECK(NearT(still.Latest()[2], relative[2], 0.0));
    // A pose that keeps moving restarts the count; after maxSeconds the latest sample is taken anyway.
    StillPose moving({0.3, 1.5, 10, 0.1});
    for (int i = 0; i < 8; ++i) {
        CHECK(moving.Feed(i % 2 == 0 ? relative : moved, 0.011) == StillPose::State::Learning);
    }
    CHECK(moving.StillFrames() == 0 && NearD(moving.LastDifference().units, 0.5, 1e-9));
    StillPose::State state = StillPose::State::Learning;
    for (int i = 0; i < 4 && state == StillPose::State::Learning; ++i) {
        state = moving.Feed(i % 2 == 0 ? relative : moved, 0.011);
    }
    CHECK(state == StillPose::State::TimedOut && moving.Seconds() >= 0.1);
    moving.Reset();
    CHECK(moving.Current() == StillPose::State::Learning && moving.Latest().empty() && moving.StillFrames() == 0);
}

// Fingers that follow the controllers (core/ik/Fingers, research/43): a synthetic right hand, turned in component
// space, with the LE1 rig's bone names.
void TestFingers() {
    using namespace mevr::ik;
    namespace fg = mevr::ik::fingers;
    auto const turn = [](double degrees, Vec3 axis) {
        double const h = degrees * std::numbers::pi / 360.0;
        Vec3 const a = Normalize(axis);
        return Quat{a.x * std::sin(h), a.y * std::sin(h), a.z * std::sin(h), std::cos(h)};
    };
    // root(0) > wrist(1); thumb 2-4, index 5-7, middle 8-10, ring 11-13, pinky 14-16 (knuckle, joint, tip); Prop01(17) under the
    // wrist; an end bone (18) under the index tip (carried, never a joint); Camera(19) off the root.
    std::vector<std::string> const names{"Root", "RightWrist", "RightThumbFinger", "RightThumbFinger1", "RightThumbFinger2",
        "RightIndexFinger", "RightIndexFinger1", "RightIndexFinger2", "RightMiddleFinger", "RightMiddleFinger1", "RightMiddleFinger2",
        "RightRingFinger", "RightRingFinger1", "RightRingFinger2", "RightPinkFinger", "RightPinkFinger1", "RightPinkFinger2", "Prop01",
        "RightIndexFingerEnd", "Camera"};
    std::vector<int> const parents{-1, 0, 1, 2, 3, 1, 5, 6, 1, 8, 9, 1, 11, 12, 1, 14, 15, 1, 7, 0};
    fg::HandBones const bones = fg::FindHand(names, parents, 1);
    CHECK(bones.hand == 1 && bones.Found() == 5);
    CHECK((bones.fingers[fg::kThumb].joints == std::array<int, 3>{2, 3, 4}) && bones.fingers[fg::kThumb].count == 3);
    CHECK((bones.fingers[fg::kIndex].joints == std::array<int, 3>{5, 6, 7}) && bones.fingers[fg::kIndex].count == 3);
    CHECK((bones.fingers[fg::kMiddle].joints == std::array<int, 3>{8, 9, 10}));
    CHECK((bones.fingers[fg::kRing].joints == std::array<int, 3>{11, 12, 13}));
    CHECK((bones.fingers[fg::kPinky].joints == std::array<int, 3>{14, 15, 16}));
    std::array<int, fg::kMaxBones> all{};
    CHECK(bones.All(all) == 16 && all[0] == 1 && all[1] == 2 && all[4] == 5 && all[15] == 16);
    CHECK(fg::FindHand(names, parents, 17).Found() == 0);  // nothing under Prop01
    CHECK(fg::FindHand(names, parents, 42).hand == -1);
    // A joint whose parent isn't the one before ends its chain.
    std::vector<int> gap = parents;
    gap[13] = 1;  // RightRingFinger2 hangs off the wrist
    CHECK(fg::FindHand(names, gap, 1).fingers[fg::kRing].count == 2 && fg::FindHand(names, gap, 1).fingers[fg::kRing].joints[2] == -1);

    // The hand's own layout: fingers along -Z, the knuckles from the pinky (x 0) to the index (x 6), the thumb's base ahead of them
    // and toward +Y, each finger's rest pose curling slightly toward +Y: the palm faces +Y. Prop01 sits on the back of the hand.
    std::vector<Vec3> local(names.size());
    auto const finger = [&](int knuckle, double x, double restCurl) {
        local[static_cast<std::size_t>(knuckle)] = {x, 0.0, -8.0};
        local[static_cast<std::size_t>(knuckle) + 1] = {x, restCurl, -12.0};
        local[static_cast<std::size_t>(knuckle) + 2] = {x, 3.0 * restCurl, -14.5};
    };
    finger(5, 6.0, 0.3);
    finger(8, 4.0, 0.3);
    finger(11, 2.0, 0.3);
    finger(14, 0.0, 0.3);
    local[2] = {7.0, 2.5, -2.0};
    local[3] = {8.5, 3.5, -4.5};
    local[4] = {9.0, 4.0, -6.5};
    local[17] = {2.0, -1.0, -3.0};
    local[18] = {6.0, 1.2, -16.0};
    Transform const hand{turn(37.0, {0.3, 1.0, -0.2}), {120.0, -30.0, 90.0}, 1.0};
    auto const build = [&](std::vector<Vec3> const& layout, Transform const& wrist) {
        std::vector<Transform> pose(names.size());
        pose[1] = wrist;
        for (std::size_t i = 2; i < pose.size(); ++i) {
            if (i == 19) {
                pose[i] = {Quat{}, {0.0, 0.0, 50.0}, 1.0};  // the Camera bone: off the root, rigid to it
                continue;
            }
            // Bone rotations are whatever the rig had; only the positions carry the measurement.
            pose[i].rotation = Normalize(turn(5.0 * static_cast<double>(i), {1.0, 0.2, 0.1}) * wrist.rotation);
            pose[i].translation = Apply(wrist, layout[i]);
        }
        return pose;
    };
    std::vector<Transform> const pose = build(local, hand);
    auto const axes = fg::MeasureAxes(pose, bones);
    CHECK(axes.has_value());
    if (!axes) {
        return;
    }
    // The palm from the thumb's base, agreed by every finger's rest curl; the index curls about the knuckle line (+X).
    CHECK(axes->palmFromThumb && axes->palm.y > 0.99 && axes->restCurlAgree == 4 && axes->restCurlDisagree == 0);
    for (int f = 0; f < fg::kFingerCount; ++f) {
        CHECK(axes->valid[static_cast<std::size_t>(f)] && NearD(Length(axes->axis[static_cast<std::size_t>(f)]), 1.0, 1e-9));
    }
    CHECK(axes->axis[fg::kIndex].x > 0.99 && std::abs(Dot(axes->axis[fg::kIndex], axes->palm)) < 1e-6);
    CHECK(std::abs(Dot(axes->axis[fg::kThumb], Normalize(local[3] - local[2]))) < 1e-6);

    // Curl 0: nothing written, the pose untouched.
    std::vector<Transform> zero = pose;
    CHECK(fg::Apply(zero, parents, bones, *axes, fg::Curl{}, 70.0) == 0);
    for (std::size_t i = 0; i < zero.size(); ++i) {
        CHECK(NearT(zero[i], pose[i], 0.0));
    }
    // Curl 1: every joint turned 70 degrees about the finger's axis through its own origin, the thumb's 70 x kThumbScale, the
    // end bone carried with the index tip, the wrist, Prop01, Camera and the root untouched.
    fg::Curl full;
    full.finger = {1.0, 1.0, 1.0, 1.0, 1.0};
    std::vector<Transform> fist = pose;
    CHECK(fg::Apply(fist, parents, bones, *axes, full, 70.0) == 16);
    for (int bone : {0, 1, 17, 19}) {
        CHECK(NearT(fist[static_cast<std::size_t>(bone)], pose[static_cast<std::size_t>(bone)], 0.0));
    }
    for (int f = 0; f < fg::kFingerCount; ++f) {
        fg::Chain const& chain = bones.fingers[static_cast<std::size_t>(f)];
        Vec3 const axis = Rotate(hand.rotation, axes->axis[static_cast<std::size_t>(f)]);
        double const degrees = 70.0 * (f == fg::kThumb ? fg::kThumbScale : 1.0);
        int const knuckle = chain.joints[0];
        CHECK(NearV(fist[static_cast<std::size_t>(knuckle)].translation, pose[static_cast<std::size_t>(knuckle)].translation, 1e-9));
        for (int k = 0; k < chain.count; ++k) {
            int const joint = chain.joints[static_cast<std::size_t>(k)];
            Quat const expected = Normalize(fg::AboutAxis(axis, degrees * (k + 1)) * pose[static_cast<std::size_t>(joint)].rotation);
            CHECK(AngleBetween(fist[static_cast<std::size_t>(joint)].rotation, expected) < 1e-9);
        }
        // The first joint swings about the knuckle by the knuckle's turn alone.
        int const joint1 = chain.joints[1];
        Vec3 const arm = pose[static_cast<std::size_t>(joint1)].translation - pose[static_cast<std::size_t>(knuckle)].translation;
        CHECK(NearV(fist[static_cast<std::size_t>(joint1)].translation,
            pose[static_cast<std::size_t>(knuckle)].translation + Rotate(fg::AboutAxis(axis, degrees), arm), 1e-9));
    }
    CHECK(NearT(Relative(fist[18], fist[7]), Relative(pose[18], pose[7]), 1e-9));
    // Toward the palm, monotonically: the index's first joint rises over the palm plane with the curl (the tip arcs back toward
    // the knuckle plane past 3 x 70 degrees, as a real fist's does), and the tip's direction from the knuckle turns further from
    // the rest direction; a negative curl (the index lifted off a trigger) goes the other way.
    auto const tipOverPalm = [&](std::vector<Transform> const& p) {
        Vec3 const inHand = Rotate(Conjugate(hand.rotation), p[6].translation - p[5].translation);
        return Dot(inHand, axes->palm);
    };
    auto const tipAngle = [&](std::vector<Transform> const& p) {
        Vec3 const rest = pose[7].translation - pose[5].translation;
        Vec3 const now = p[7].translation - p[5].translation;
        return std::acos(std::clamp(Dot(Normalize(rest), Normalize(now)), -1.0, 1.0));
    };
    double lastOver = tipOverPalm(pose);
    double lastAngle = tipAngle(pose);
    for (double c : {0.25, 0.5, 0.75, 1.0}) {
        fg::Curl curl;
        curl.finger[fg::kIndex] = c;
        std::vector<Transform> p = pose;
        CHECK(fg::Apply(p, parents, bones, *axes, curl, 70.0) == 4);  // the index chain and its end bone
        CHECK(tipOverPalm(p) > lastOver && tipAngle(p) > lastAngle);
        lastOver = tipOverPalm(p);
        lastAngle = tipAngle(p);
        // Only the index moved.
        for (int bone : {2, 3, 4, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17}) {
            CHECK(NearT(p[static_cast<std::size_t>(bone)], pose[static_cast<std::size_t>(bone)], 0.0));
        }
    }
    fg::Curl lifted;
    lifted.finger[fg::kIndex] = -0.5;
    std::vector<Transform> off = pose;
    CHECK(fg::Apply(off, parents, bones, *axes, lifted, 70.0) == 4);
    CHECK(tipOverPalm(off) < tipOverPalm(pose));
    CHECK(AngleBetween(off[5].rotation, Normalize(fg::AboutAxis(Rotate(hand.rotation, axes->axis[fg::kIndex]), -35.0) * pose[5].rotation)) < 1e-9);
    // The thumb closes toward the middle finger's first joint; a curl past 1 is clamped.
    CHECK(Length(fist[4].translation - fist[9].translation) < Length(pose[4].translation - pose[9].translation));
    fg::Curl over;
    over.finger = {2.0, 2.0, 2.0, 2.0, 2.0};
    std::vector<Transform> clamped = pose;
    CHECK(fg::Apply(clamped, parents, bones, *axes, over, 70.0) == 16);
    for (std::size_t i = 0; i < clamped.size(); ++i) {
        CHECK(NearT(clamped[i], fist[i], 1e-9));
    }
    // Bad input writes nothing.
    std::vector<Transform> untouched = pose;
    CHECK(fg::Apply(untouched, std::span<int const>(parents).first(5), bones, *axes, full, 70.0) == 0);
    fg::HandBones noHand = bones;
    noHand.hand = -1;
    CHECK(fg::Apply(untouched, parents, noHand, *axes, full, 70.0) == 0);
    CHECK(fg::Apply(untouched, parents, bones, *axes, full, std::numeric_limits<double>::quiet_NaN()) == 0);
    for (std::size_t i = 0; i < untouched.size(); ++i) {
        CHECK(NearT(untouched[i], pose[i], 0.0));
    }

    // Without a thumb the rest pose's own curl picks the palm side; with straight fingers too, nothing does.
    fg::HandBones thumbless = bones;
    thumbless.fingers[fg::kThumb] = {};
    auto const fromCurl = fg::MeasureAxes(pose, thumbless);
    CHECK(fromCurl && !fromCurl->palmFromThumb && fromCurl->palm.y > 0.99 && !fromCurl->valid[fg::kThumb] && fromCurl->valid[fg::kIndex]);
    std::vector<Vec3> straight = local;
    for (int knuckle : {5, 8, 11, 14}) {
        straight[static_cast<std::size_t>(knuckle) + 1].y = 0.0;
        straight[static_cast<std::size_t>(knuckle) + 2].y = 0.0;
    }
    CHECK(!fg::MeasureAxes(build(straight, hand), thumbless));
    CHECK(fg::MeasureAxes(build(straight, hand), bones).has_value());  // the thumb still tells
    // A left hand, mirrored in Y: the thumb sits toward -Y and the rest curl bends that way, so the palm faces -Y and a positive
    // curl still closes the fingers toward it.
    std::vector<std::string> leftNames = names;
    for (std::string& name : leftNames) {
        if (name.rfind("Right", 0) == 0) {
            name = "Left" + name.substr(5);
        }
    }
    std::vector<Vec3> leftLocal = local;
    for (Vec3& p : leftLocal) {
        p.y = -p.y;
    }
    fg::HandBones const leftBones = fg::FindHand(leftNames, parents, 1);
    CHECK(leftBones.Found() == 5);
    std::vector<Transform> const leftPose = build(leftLocal, hand);
    auto const leftAxes = fg::MeasureAxes(leftPose, leftBones);
    CHECK(leftAxes && leftAxes->palmFromThumb && leftAxes->palm.y < -0.99 && leftAxes->axis[fg::kIndex].x < -0.99);
    if (leftAxes) {
        std::vector<Transform> leftFist = leftPose;
        CHECK(fg::Apply(leftFist, parents, leftBones, *leftAxes, full, 70.0) == 16);
        Vec3 const before = Rotate(Conjugate(hand.rotation), leftPose[6].translation - leftPose[5].translation);
        Vec3 const after = Rotate(Conjugate(hand.rotation), leftFist[6].translation - leftFist[5].translation);
        CHECK(Dot(after, leftAxes->palm) > Dot(before, leftAxes->palm) + 3.0);  // 4 units x sin 70 from a 0.3 rest curl
    }
    // AboutAxis is Rotate's right-handed turn: +90 about Z takes X to Y.
    CHECK(NearV(Rotate(fg::AboutAxis({0.0, 0.0, 1.0}, 90.0), {1.0, 0.0, 0.0}), {0.0, 1.0, 0.0}, 1e-9));

    // The curls from a controller (research/43 "Touch sensors").
    auto const curlOf = [](fg::Rule rule, double trigger, double squeeze, double triggerTouch, double thumbTouch) {
        fg::Controller c;
        c.trigger = trigger;
        c.squeeze = squeeze;
        c.triggerTouch = triggerTouch;
        c.thumbTouch = thumbTouch;
        return fg::FromController(rule, c);
    };
    auto const sameCurl = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    // No sensors: the first rules exactly (index = trigger, the rest and the thumb = grip; on the gun -0.5 x (1 - trigger)).
    {
        fg::Curl const c = curlOf(fg::Rule::Free, 0.3, 0.8, -1.0, -1.0);
        CHECK(sameCurl(c.finger[fg::kIndex], 0.3) && sameCurl(c.finger[fg::kMiddle], 0.8) && sameCurl(c.finger[fg::kRing], 0.8) &&
              sameCurl(c.finger[fg::kPinky], 0.8) && sameCurl(c.finger[fg::kThumb], 0.8));
        CHECK(sameCurl(curlOf(fg::Rule::Gun, 0.0, 1.0, -1.0, -1.0).finger[fg::kIndex], -0.5));
        CHECK(sameCurl(curlOf(fg::Rule::Gun, 0.5, 1.0, -1.0, -1.0).finger[fg::kIndex], -0.25));
        CHECK(sameCurl(curlOf(fg::Rule::Gun, 1.0, 1.0, -1.0, -1.0).finger[fg::kIndex], 0.0));
        fg::Curl const gun = curlOf(fg::Rule::Gun, 0.2, 1.0, -1.0, -1.0);
        CHECK(gun.finger[fg::kThumb] == 0.0 && gun.finger[fg::kMiddle] == 0.0 && gun.finger[fg::kRing] == 0.0 && gun.finger[fg::kPinky] == 0.0);
    }
    // The trigger's sensor: lifted points, resting curls a little, the pull closes it from there; a pull counts as a touch.
    CHECK(sameCurl(curlOf(fg::Rule::Free, 0.0, 0.0, 0.0, -1.0).finger[fg::kIndex], fg::kIndexPoint));
    CHECK(sameCurl(curlOf(fg::Rule::Free, 0.0, 0.0, 1.0, -1.0).finger[fg::kIndex], fg::kIndexOnTrigger));
    CHECK(sameCurl(curlOf(fg::Rule::Free, 1.0, 0.0, 1.0, -1.0).finger[fg::kIndex], 1.0));
    CHECK(sameCurl(curlOf(fg::Rule::Free, 0.5, 0.0, 1.0, -1.0).finger[fg::kIndex], fg::kIndexOnTrigger + (1.0 - fg::kIndexOnTrigger) * 0.5));
    CHECK(sameCurl(curlOf(fg::Rule::Free, 0.5, 0.0, 0.0, -1.0).finger[fg::kIndex], fg::kIndexOnTrigger + (1.0 - fg::kIndexOnTrigger) * 0.5));
    CHECK(sameCurl(curlOf(fg::Rule::Free, 0.0, 0.0, 0.5, -1.0).finger[fg::kIndex], (fg::kIndexPoint + fg::kIndexOnTrigger) / 2.0));
    // Pointing: the grip closed, the finger off the trigger (the index straight, the other three closed).
    {
        fg::Curl const point = curlOf(fg::Rule::Free, 0.0, 1.0, 0.0, 1.0);
        CHECK(point.finger[fg::kIndex] < 0.0 && sameCurl(point.finger[fg::kMiddle], 1.0) && sameCurl(point.finger[fg::kThumb], 1.0));
    }
    // The thumb's sensor: lifted is up (a thumbs-up in a fist, open in an open hand), resting at least kThumbOnRest.
    CHECK(sameCurl(curlOf(fg::Rule::Free, 1.0, 1.0, 1.0, 0.0).finger[fg::kThumb], fg::kThumbUp));
    CHECK(sameCurl(curlOf(fg::Rule::Free, 0.0, 0.0, 0.0, 0.0).finger[fg::kThumb], fg::kThumbUp));
    CHECK(sameCurl(curlOf(fg::Rule::Free, 0.0, 0.0, 0.0, 1.0).finger[fg::kThumb], fg::kThumbOnRest));
    CHECK(sameCurl(curlOf(fg::Rule::Free, 0.0, 1.0, 0.0, 1.0).finger[fg::kThumb], 1.0));
    {
        fg::Curl const thumbsUp = curlOf(fg::Rule::Free, 1.0, 1.0, 1.0, 0.0);
        CHECK(sameCurl(thumbsUp.finger[fg::kIndex], 1.0) && sameCurl(thumbsUp.finger[fg::kPinky], 1.0) && thumbsUp.finger[fg::kThumb] < 0.0);
    }
    // On the gun with a sensor: along the frame until touched, then on the trigger as animated, whatever the pull.
    CHECK(sameCurl(curlOf(fg::Rule::Gun, 0.0, 1.0, 0.0, 0.0).finger[fg::kIndex], -fg::kIndexOffTrigger));
    CHECK(sameCurl(curlOf(fg::Rule::Gun, 0.0, 1.0, 1.0, 0.0).finger[fg::kIndex], 0.0));
    CHECK(sameCurl(curlOf(fg::Rule::Gun, 0.0, 1.0, 0.5, 0.0).finger[fg::kIndex], -fg::kIndexOffTrigger / 2.0));
    CHECK(sameCurl(curlOf(fg::Rule::Gun, 0.6, 1.0, 0.0, 0.0).finger[fg::kIndex], 0.0));  // pulled, the sensor late: on it
    CHECK(curlOf(fg::Rule::Gun, 0.0, 1.0, 0.0, 0.0).finger[fg::kThumb] == 0.0);    // the thumb keeps the animated grip
    // Out-of-range inputs are clamped.
    CHECK(sameCurl(curlOf(fg::Rule::Free, 2.0, -1.0, 3.0, 5.0).finger[fg::kIndex], 1.0));
    CHECK(sameCurl(curlOf(fg::Rule::Free, 2.0, -1.0, 3.0, 5.0).finger[fg::kMiddle], 0.0));
    // The touch eases in and out over kTouchBlendS; a hand that had no sensor starts where the touch is.
    CHECK(fg::EaseTouch(-1.0, true, 0.0) == 1.0 && fg::EaseTouch(-1.0, false, 0.0) == 0.0);
    CHECK(sameCurl(fg::EaseTouch(0.0, true, fg::kTouchBlendS / 2.0), 0.5));
    CHECK(sameCurl(fg::EaseTouch(0.5, false, fg::kTouchBlendS / 4.0), 0.25));
    CHECK(fg::EaseTouch(0.9, true, 1.0) == 1.0 && fg::EaseTouch(0.1, false, 1.0) == 0.0);
    CHECK(fg::EaseTouch(0.4, true, -1.0) == 0.4);  // no time, no change
}

// tests/InstallTests.cpp: the installer's logic (research/33).
void RunInstallTests(int& failures);
// tests/ReportTests.cpp: "Report a problem" (install/Report, research/33 section 8).
void RunReportTests(int& failures);
// tests/ReflectTests.cpp: our UE3 reflection layer (src/ue3, research/34).
int RunReflectTests();
int RunBreadcrumbTests();
int RunExeTests();

// core/Ini: the one reader of our settings file, as Win32's profile API reads it.
void TestIni() {
    namespace ini = mevr::ini;
    // Case-insensitive names, the first occurrence wins (a repeated section is ignored as a whole), comments skipped.
    std::string const text = "\xEF\xBB\xBF[Hands]\r\n; note\r\ndrawRate = 3\r\nDRAWRATE = 6\r\n  handPitch=-4.5 \r\n"
                             "[hands]\r\nleanM = 0.4\r\n[Sights]\r\n#c\r\neyeBox = 6\r\nbroken\r\n";
    ini::File const file = ini::Parse(text);
    CHECK(file.size() == 2);
    CHECK(ini::Get(file, "HANDS", "drawrate") == std::optional<std::string>("3"));
    CHECK(ini::GetNumber(file, "Hands", "HandPitch") == std::optional<double>(-4.5));
    CHECK(!ini::Get(file, "Hands", "leanM"));  // in the repeated [hands]: Win32 doesn't see it
    CHECK(ini::Get(file, "sights", "EYEBOX") == std::optional<std::string>("6"));
    CHECK(file.find("Hands") != file.end() && file.find("Hands")->second.count("drawRate") == 1);
    // The BOM doesn't hide the first section.
    CHECK(ini::Parse("\xEF\xBB\xBF[Comfort]\nturn=snap\n").count("comfort") == 1);
    // Encodings.
    CHECK(ini::Detect("\xEF\xBB\xBF[a]") == ini::Encoding::Utf8Bom);
    CHECK(ini::Detect("[a]\nb=1") == ini::Encoding::Narrow);
    std::string const utf16le("\xFF\xFE[\0H\0]\0\n\0k\0=\0" "\x34\0", 16);
    CHECK(ini::Detect(utf16le) == ini::Encoding::Utf16Le);
    CHECK(ini::IsUtf16(ini::Detect(std::string("[\0H\0]\0", 6))));
    CHECK(ini::Decode(utf16le, ini::Encoding::Utf16Le) == "[H]\nk=4");
    CHECK(ini::GetNumber(ini::Parse(ini::Decode(utf16le, ini::Encoding::Utf16Le)), "h", "K") == std::optional<double>(4.0));
    std::string const utf16be("\xFE\xFF\0[\0x\0]\x04\x1F", 10);  // "[x]" and U+041F (Cyrillic)
    CHECK(ini::Decode(utf16be, ini::Encoding::Utf16Be) == "[x]\xD0\x9F");
    // Numbers: no locale, a leading '+', trailing text, nothing.
    CHECK(ini::Number("0.5") == std::optional<double>(0.5));
    CHECK(ini::Number(" +2 ") == std::optional<double>(2.0));
    CHECK(ini::Number("1.25 ; note") == std::optional<double>(1.25));
    CHECK(!ini::Number("") && !ini::Number("abc") && !ini::Number("inf"));
    CHECK(ini::Fixed(0.5, 2) == "0.50" && ini::Fixed(-0.001, 2) == "0.00" && ini::Fixed(-1.26, 1) == "-1.3");
    // Paths outside the ANSI code page as UTF-8.
    CHECK(ini::Utf8(std::filesystem::path(L"C:\\\x0418\x0433\x0440\x044B\\\x6E38\x620F")) ==
          "C:\\\xD0\x98\xD0\xB3\xD1\x80\xD1\x8B\\\xE6\xB8\xB8\xE6\x88\x8F");
    // ParseSection (the host's GetPrivateProfileSection output) shares it: the first key wins.
    CHECK(mevr::vr::menu_settings::ParseSection("a=1\nA=2\n").at("a") == "1");
}

int main() {
    TestIni();
    RunInstallTests(gFailures);
    RunReportTests(gFailures);
    gFailures += RunReflectTests();
    gFailures += RunBreadcrumbTests();
    gFailures += RunExeTests();
    TestUpscale();
    TestComfort();
    TestCombatStance();
    TestPoseGraft();
    TestFingers();
    TestMetricStereo();
    TestMenuSettings();
    TestCalibration();
    TestViewLevel();
    TestAimAssist();
    TestRecenterTilt();
    TestTriggerOwner();
    TestSupportZone();
    TestReachWeight();
    TestSightAssist();
    TestSightPullReachesRelief();
    TestAds();
    TestInteractLabel();
    TestMakoControls();
    TestHudPieces();
    TestSightSettings();
    TestSightGameOptics();
    TestScopeArt();
    TestSightGeometry();
    TestScopeMapping();
    TestSightShots();
    TestSightVirtualEyepiece();
    TestScopeCapture();
    TestSceneCaptureScope();
    TestSpectator();
    TestGalaxyMap();
    TestGalaxyHologram();
    TestWorldUi();
    TestCineScreen();
    TestDevChannelsAndDiskBudget();
    TestBoneNames();
    TestIk();
    TestRigid();
    TestOrientation();
    TestArmChains();
    TestArmPose();
    TestArmStretch();
    TestTorsoPose();
    TestCameraHook();
    TestBodyFollow();
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
    TestGameMenuRequest();
    TestCloseLatch();
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
    TestThrowVelocity();
    TestThrowGrip();
    TestGrenadeMachine();
    TestVent();
    TestVentRack();
    TestSwing();
    TestArcPoints();
    TestFaceButtonsOff();
    TestBodySlotGeometry();
    TestBodySlotMachine();
    TestBodySlotHolding();
    TestContactHaptics();
    TestStandingHeight();
    TestCrouchFraction();
    TestCrouchDetector();
    TestCrouchArbiter();
    TestHapticChannel();
    TestTwoHand();
    TestOverlayPlacement();
    TestMenuPointer();
    TestGamePointer();
    TestWorldPanel();
    TestPoseRecording();
    TestImageConvert();
    TestColour();
    TestTonemap();
    TestColourStrip();
    TestLayerComposite();
    TestTestRequest();
    TestFrameProbe();
    TestPng();
    if (gFailures == 0) {
        std::printf("all tests passed\n");
    }
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

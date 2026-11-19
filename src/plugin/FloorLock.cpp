// SPDX-License-Identifier: MIT
#include "plugin/FloorLock.hpp"

#include "core/Log.hpp"
#include "core/vr/Calibration.hpp" // seated motion is shared even when this host has no calibration UI
#if defined(MEVR_WITH_HALCYON)
#include "halcyon/le1/mevr_bridge.h"  // LE1's render seams, implemented at the end
#endif
#if defined(MEVR_WITH_HALCYON) || defined(MEVR_WITH_MAIN_MENU)
#include "plugin/Calibration.hpp"
#endif
#include "plugin/Renderer.hpp"
#include "plugin/CrashBreadcrumbs.hpp"
#include "core/Breadcrumbs.hpp"
#include "core/vr/SettingsFile.hpp"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <mutex>

namespace mevr::host::floor_lock {
namespace {
namespace fl = vr::floor_lock;
std::mutex gMutex;
bool gEnabled = true;
float gManualHeight = 0.0f; // gMutex
fl::PawnSample gPawn;
struct Pending {
    vr::host_view::State view;
    vr::floor::State floor;
    fl::Context context;
    std::uint64_t arm = 0;
    std::uint64_t sequence = 0;
};
Pending gPending;
std::uint64_t gSequence = 0; // gMutex; also rejects a game-side publication racing session stop
// Game/render-build thread only; the replay uses precisely this pair's immutable inputs.
Pending gPair;
fl::Result gPairResult;
// A traversal move's hold of the correction (core/vr/FloorLock MoveHold), and what the eye did meanwhile, for the log.
fl::MoveHold gMoveHold;
struct MoveLog {
    bool on = false;
    std::uint64_t beganMs = 0;
    unsigned frames = 0;
    double firstEye = 0.0, lastEye = 0.0, eyeStep = 0.0;        // the drawn eye's Z (held)
    double lastFresh = 0.0, freshStep = 0.0;                    // where the unheld solve would have put it
    bool freshSeen = false;
    std::uint64_t endedMs = 0;                                  // the move's end (the settle's start)
};
MoveLog gMoveLog;
vr::calibration::SeatedMotion gSeatedMotion; // game/render-build thread, shared by both eyes
std::uint64_t gLogMs = 0;
double gLoggedCorrection = 0.0;
double gLoggedHeight = 0.0;
bool gWarned = false;
std::string gStatus = "null"; // gMutex, snapshots on the Present thread
}

void SetPawn(fl::PawnSample const* sample) {
    std::lock_guard lock(gMutex);
    gPawn = sample != nullptr ? *sample : fl::PawnSample{};
}

void SetManualHeight(float meters) {
    std::lock_guard lock(gMutex);
    gManualHeight = meters;
}

void RefreshSettings() {
    auto const& path = mevr::host::SettingsPath();
    // Held world views carry the corrected base too; an explicit 0 keeps the legacy path.
    bool const on = path.empty() || mevr::vr::settings_file::Int(L"Body", L"floorLock", 1, path.c_str()) != 0;
    std::lock_guard lock(gMutex);
    gEnabled = on;
}

bool Enabled() {
    std::lock_guard lock(gMutex);
    return gPending.context.enabled;
}

void Queue(vr::host_view::State const& view, vr::floor::State const& floor, bool ordinaryWorld) {
    Pending p;
    p.view = view;
    p.view.timeMs = GetTickCount64();
    p.floor = floor;
    p.arm = mevr::host::renderer::HeadArmSequence();
    p.context.world = view.valid && ordinaryWorld;
    p.context.firstPerson = mevr::host::renderer::FirstPersonOwned();
    // The shared menu is an overlay over this same world: hiding its controller
    // input must not lower the view to the native camera height. Ordinary-world,
    // first-person ownership and fresh pawn/floor gates still apply behind it.
    // Native flat screens fail those gates; the renderer menu and calibration
    // explicitly stand down. Never retain an old floor offset across a transition.
    bool calibrating = false;
#if defined(MEVR_WITH_HALCYON) || defined(MEVR_WITH_MAIN_MENU)
    calibrating = calibration::Active();
#endif
    p.context.menu = mevr::host::renderer::OwnMenuOpen() || calibrating;
    std::lock_guard lock(gMutex);
    p.context.enabled = gEnabled;
    p.context.heightOffsetM = gManualHeight;
    p.sequence = ++gSequence;
    gPending = p;
    if (!gEnabled) vr::host_view::Publish(p.view, p.view.timeMs); // legacy publication point, no corrected centre
}

void Reset() {
    std::lock_guard lock(gMutex);
    gPending = {};
    gPawn = {};
    gMoveHold.Reset();
    gMoveLog = {};
    gManualHeight = 0.0f;
    vr::host_view::Reset();
}

float Translate(bool replay, float rawCameraZ, float rightZ, float upZ, float forwardZ,
    float* right, float* up, float* forward) {
    if (!replay) {
        {
            std::lock_guard lock(gMutex);
            gPair = gPending;
            gPair.context.pawn = gPawn;
        }
        // The same head pose, offsets, units and floor sample are latched together. The disabled path keeps upstream's offsets.
        if (gPair.context.enabled) {
            *right = gPair.view.offsetRight;
            *up = gPair.view.offsetUp;
            *forward = gPair.view.offsetForward;
        }
        gPair.view.offsetRight = *right;
        gPair.view.offsetUp = *up;
        gPair.view.offsetForward = *forward;
        double const commonZ = *right * rightZ + *up * upZ + *forward * forwardZ;
        std::uint64_t const now = GetTickCount64();
        if (gPair.view.timeMs == 0 || now - gPair.view.timeMs > 250) {
            gPair.view.valid = false;
            gPair.context.world = false;
        }
        auto const position = gPair.context.pawn.seated ? vr::calibration::PlayPosition::Seated
                                                       : vr::calibration::PlayPosition::Standing;
        if (position == vr::calibration::PlayPosition::Standing) gSeatedMotion.Reset();
        auto const seatedEye = fl::Applies(gPair.context) && gPair.view.valid
            ? gSeatedMotion.Eye(position, gPair.context.pawn.standingEyeM,
                fl::ValidFloor(gPair.floor, now) ? std::optional<double>(gPair.floor.eyeHeight) : std::nullopt,
                gPair.view.head.position.y, gPair.floor.sessions, gPair.floor.recenters)
            : std::nullopt;
        fl::Result const fresh = fl::Correct(gPair.context, gPair.floor, now, rawCameraZ, commonZ, gPair.view.unitsPerMeter, seatedEye);
        bool const eligible = gPair.context.enabled && gPair.context.world && gPair.context.firstPerson && gPair.view.valid &&
            gPair.context.pawn.pawn && fl::Fresh(gPair.context.pawn.timeMs, now) && !gPair.context.menu;
        fl::MoveInput moveIn;
        moveIn.fresh = fresh;
        moveIn.moving = gPair.context.pawn.traversalMove;
        moveIn.eligible = eligible;
        moveIn.rawCameraZ = rawCameraZ;
        moveIn.commonWorldZ = commonZ;
        moveIn.floorZ = gPair.context.pawn.floorZ;
        moveIn.unitsPerMeter = gPair.view.unitsPerMeter;
        moveIn.nowMs = now;
        auto const phaseBefore = gMoveHold.State();
        double const eyeBefore = gMoveLog.lastEye;
        gPairResult = gMoveHold.Step(moveIn);
        auto const phase = gMoveHold.State();
        if (phase == fl::MoveHold::Phase::Moving) {
            if (!gMoveLog.on) {
                gMoveLog = {};
                gMoveLog.on = true;
                gMoveLog.beganMs = now;
                gMoveLog.firstEye = gMoveLog.lastEye = gPairResult.centreZ;
                log::Info("floor lock: a traversal move began: the pawn's floor held {:.1f} units below the game's camera, "
                          "correction {:+.3f} (the eye follows the game's camera, not the pawn's steps)",
                    -gMoveHold.FloorGap(), gMoveHold.Held());
            }
            if (fresh.active) {
                if (gMoveLog.freshSeen) {
                    gMoveLog.freshStep = std::max(gMoveLog.freshStep, std::abs(fresh.centreZ - gMoveLog.lastFresh));
                }
                gMoveLog.freshSeen = true;
                gMoveLog.lastFresh = fresh.centreZ;
            }
        }
        if (gMoveLog.on) {
            ++gMoveLog.frames;
            gMoveLog.eyeStep = std::max(gMoveLog.eyeStep, std::abs(gPairResult.centreZ - gMoveLog.lastEye));
            gMoveLog.lastEye = gPairResult.centreZ;
        }
        if (phaseBefore == fl::MoveHold::Phase::Moving && phase != fl::MoveHold::Phase::Moving && gMoveLog.on) {
            gMoveLog.endedMs = now;
            log::Info("floor lock: the traversal move ended after {:.2f} s ({} frames): the held eye's largest step {:.1f} units a "
                      "frame (ended {:+.1f} from its start); solved from the pawn's floor it would have stepped up to {:.1f} units "
                      "a frame; {}",
                (now - gMoveLog.beganMs) / 1000.0, gMoveLog.frames, gMoveLog.eyeStep, gMoveLog.lastEye - gMoveLog.firstEye,
                gMoveLog.freshStep,
                phase == fl::MoveHold::Phase::Settling
                    ? std::format("the game's camera is {:.1f} units off its place over the pawn before the move: held until "
                                  "it settles", -gMoveHold.SettleGap())
                    : std::format("released at once (the camera already settled, or the lock stood down): {:+.1f} units",
                          gPairResult.centreZ - eyeBefore));
            gMoveLog.frames = 0;
            gMoveLog.eyeStep = 0.0;
        }
        if (phaseBefore != fl::MoveHold::Phase::None && phase == fl::MoveHold::Phase::None && gMoveLog.on) {
            gMoveLog.on = false;
            log::Info("floor lock: the traversal hold released {:.2f} s after the move's end ({} frames{}): the eye's largest step {:.1f} units a "
                      "frame meanwhile, the release {:+.1f} units, the correction {:+.3f}",
                (now - gMoveLog.endedMs) / 1000.0, gMoveLog.frames,
                gMoveHold.Eased() ? ", eased after the wait" : ", the camera settled", gMoveLog.eyeStep,
                gPairResult.centreZ - eyeBefore, gPairResult.correction);
        }
        auto& view = gPair.view;
        view.floorLocked = gPairResult.active;
        view.centreValid = gPair.context.enabled && view.valid && std::isfinite(gPairResult.centreZ);
        view.centreZ = gPairResult.centreZ;
        view.pawnFloorZ = gPair.context.pawn.floorZ;
        view.stageEyeM = gPair.floor.eyeHeight;
        view.floorSequence = gPair.floor.sequence;
        view.floorCorrection = gPairResult.correction;
        if (gPair.context.enabled) {
            std::lock_guard lock(gMutex);
            if (gPair.sequence == gPending.sequence) vr::host_view::Publish(view, view.timeMs);
        }
        if (fl::Applies(gPair.context) && !gPair.context.pawn.seated && !fl::ValidFloor(gPair.floor, now) && !gWarned) {
            gWarned = true;
            log::Warn("floor lock: no fresh valid STAGE/LOCAL_FLOOR sample; using the old camera behaviour");
        }
        bool const changed = std::abs(gPairResult.correction - gLoggedCorrection) > 0.1 ||
            std::abs(view.stageEyeM - gLoggedHeight) > 0.001;
        if (gPairResult.active && (gLogMs == 0 || now - gLogMs >= (changed ? 1000u : 10000u))) {
            gLogMs = now;
            gLoggedCorrection = gPairResult.correction;
            gLoggedHeight = view.stageEyeM;
            log::Info("floor lock: raw camera Z {:.3f}, pawn floor Z {:.3f}, STAGE eye {:.4f} m, correction {:+.3f}, final eye {:.4f} m",
                rawCameraZ, view.pawnFloorZ, view.stageEyeM, view.floorCorrection,
                (view.centreZ - view.pawnFloorZ) / view.unitsPerMeter);
        }
        // JSON values are finite on the active path. Invalid inputs are represented by null, never NaN.
        auto number = [](double v) { return std::isfinite(v) ? std::format("{:.6f}", v) : std::string("null"); };
        std::string const status = std::format("{{\"enabled\": {}, \"active\": {}, \"rawCameraZ\": {}, \"pawnFloorZ\": {}, "
            "\"stageEyeM\": {}, \"correction\": {}, \"centreZ\": {}, \"floorSequence\": {}, \"floorTimeMs\": {}, \"unitsPerMeter\": {}, "
            "\"finalEyeM\": {}, \"viewArm\": {}, \"source\": \"{}\", \"seated\": {}, \"standingEyeM\": {}, \"heightOffsetM\": {}, "
            "\"moveHold\": \"{}\"}}",
            gPair.context.enabled, view.floorLocked, number(rawCameraZ), number(view.pawnFloorZ), number(view.stageEyeM),
            number(view.floorCorrection), number(view.centreZ), view.floorSequence, gPair.floor.timeMs, number(view.unitsPerMeter),
            view.unitsPerMeter > 0.0f ? number((view.centreZ - view.pawnFloorZ) / view.unitsPerMeter) : "null",
            gPair.arm, vr::floor::Name(gPair.floor.source), gPair.context.pawn.seated, number(gPair.context.pawn.standingEyeM),
            number(gPair.context.heightOffsetM),
            phase == fl::MoveHold::Phase::Moving ? "moving" : phase == fl::MoveHold::Phase::Settling ? "settling" : "none");
        std::lock_guard lock(gMutex);
        gStatus = status;
    } else if (gPair.context.enabled) {
        *right = gPair.view.offsetRight;
        *up = gPair.view.offsetUp;
        *forward = gPair.view.offsetForward;
    }
    return static_cast<float>(gPairResult.correction);
}

std::string StatusJson() {
    std::lock_guard lock(gMutex);
    return gStatus;
}
} // namespace mevr::host::floor_lock

#if defined(MEVR_WITH_HALCYON)
// LE1's render seams (render_hook.cpp); ME2's FloorViewTranslation is plugin/HalcyonLe2Bridge.cpp's, through Translate.
namespace MELEVR::MevrBridge {
bool FloorLockEnabled() noexcept { return mevr::host::floor_lock::Enabled(); }
void FloorViewLook(bool* on, float* yaw, float* pitch, std::uint64_t* arm) noexcept {
    using namespace mevr::host::floor_lock;
    if (!gPairResult.active) return;
    *on = gPair.view.headLook;
    *yaw = static_cast<float>(-gPair.view.lookYaw);
    *pitch = static_cast<float>(gPair.view.lookPitch);
    *arm = gPair.arm;
}
} // namespace MELEVR::MevrBridge
namespace MevrSeams {
using namespace MELEVR::MevrBridge;
float FloorViewTranslation(bool replay, float rawCameraZ, float rightZ, float upZ, float forwardZ,
    float* right, float* up, float* forward) noexcept {
    return mevr::host::floor_lock::Translate(replay, rawCameraZ, rightZ, upZ, forwardZ, right, up, forward);
}
} // namespace MevrSeams
namespace MELEVR::MevrBridge {
void JournalFloorViewWrite(void const* origin, void const* preTranslation) noexcept {
    mevr::host::crash::RecordWrite("FloorLock.viewOrigin", origin, 12, mevr::breadcrumbs::FirstBytes(origin, 12));
    mevr::host::crash::RecordWrite("FloorLock.preViewTranslation", preTranslation, 12, mevr::breadcrumbs::FirstBytes(preTranslation, 12));
}
} // namespace MELEVR::MevrBridge
#endif

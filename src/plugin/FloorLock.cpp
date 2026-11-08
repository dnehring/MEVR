// SPDX-License-Identifier: MIT
#include "plugin/FloorLock.hpp"

#include "core/Log.hpp"
#include "halcyon/le1/head_aim.h"
#include "halcyon/le1/mevr_bridge.h"
#include "halcyon/le1/vr_config.h"
#include "halcyon/le1/vr_menu.h"
#include "halcyon/le1/render_hook.h"
#include "plugin/Calibration.hpp"
#include "plugin/MevrMenu.hpp"
#include "plugin/CrashBreadcrumbs.hpp"
#include "core/Breadcrumbs.hpp"

#include <Windows.h>
#include <cmath>
#include <format>
#include <mutex>

namespace mevr::host::floor_lock {
namespace {
namespace fl = vr::floor_lock;
std::mutex gMutex;
bool gEnabled = true;
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

void RefreshSettings() {
    auto const& path = MELEVR::MevrBridge::SettingsPath();
    // Held world views carry the corrected base too; an explicit 0 keeps the legacy path.
    bool const on = path.empty() || GetPrivateProfileIntW(L"Body", L"floorLock", 1, path.c_str()) != 0;
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
    p.arm = MELEVR::RenderHook::GetHeadArmSeq();
    p.context.world = view.valid && ordinaryWorld;
    p.context.firstPerson = MELEVR::Config::Get().fpEnabled && MELEVR::HeadAim::FirstPersonOwnsAny();
    p.context.menu = MELEVR::Menu::IsOpen() || MELEVR::MevrBridge::MainMenuOpen() || calibration::Active();
    std::lock_guard lock(gMutex);
    p.context.enabled = gEnabled;
    p.sequence = ++gSequence;
    gPending = p;
    if (!gEnabled) vr::host_view::Publish(p.view, p.view.timeMs); // legacy publication point, no corrected centre
}

void Reset() {
    std::lock_guard lock(gMutex);
    gPending = {};
    gPawn = {};
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
        gPairResult = fl::Correct(gPair.context, gPair.floor, now, rawCameraZ, commonZ, gPair.view.unitsPerMeter, seatedEye);
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
            "\"finalEyeM\": {}, \"viewArm\": {}, \"source\": \"{}\", \"seated\": {}, \"standingEyeM\": {}}}",
            gPair.context.enabled, view.floorLocked, number(rawCameraZ), number(view.pawnFloorZ), number(view.stageEyeM),
            number(view.floorCorrection), number(view.centreZ), view.floorSequence, gPair.floor.timeMs, number(view.unitsPerMeter),
            view.unitsPerMeter > 0.0f ? number((view.centreZ - view.pawnFloorZ) / view.unitsPerMeter) : "null",
            gPair.arm, vr::floor::Name(gPair.floor.source), gPair.context.pawn.seated, number(gPair.context.pawn.standingEyeM));
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
float FloorViewTranslation(bool replay, float rawCameraZ, float rightZ, float upZ, float forwardZ,
    float* right, float* up, float* forward) noexcept {
    return mevr::host::floor_lock::Translate(replay, rawCameraZ, rightZ, upZ, forwardZ, right, up, forward);
}
void JournalFloorViewWrite(void const* origin, void const* preTranslation) noexcept {
    mevr::host::crash::RecordWrite("FloorLock.viewOrigin", origin, 12, mevr::breadcrumbs::FirstBytes(origin, 12));
    mevr::host::crash::RecordWrite("FloorLock.preViewTranslation", preTranslation, 12, mevr::breadcrumbs::FirstBytes(preTranslation, 12));
}
} // namespace MELEVR::MevrBridge

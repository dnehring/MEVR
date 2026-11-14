// SPDX-License-Identifier: MIT
#include "game/CameraProbe.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/ik/CameraHook.hpp"
#include "engine/Skeleton.hpp"
#include "game/HeadTracking.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <numbers>
#include <sstream>
#include <string>

namespace mevr::game::camera_probe {
namespace {

#if defined(MEVR_GAME_LE1)

// `mevr camera trace <seconds>`: one CSV row a mod tick (an idle sway report, 2026-11-14).
ue3::FieldRef gActorVelocity{"Engine.Actor", "Velocity"};
constexpr char const* kTraceBones[] = {"Camera", "God", "Head", "Neck", "LowerBack", "Pelvis"};
constexpr int kTraceBoneCount = static_cast<int>(sizeof(kTraceBones) / sizeof(kTraceBones[0]));
struct Trace {
    std::FILE* file = nullptr;
    std::string path;
    LARGE_INTEGER start{}, frequency{};
    double seconds = 0.0;
    ue3::Object* mesh = nullptr;
    int bones[kTraceBoneCount] = {};
    std::uint64_t rows = 0;
};
Trace gTrace;

void TraceClose(char const* why) {
    if (gTrace.file == nullptr) {
        return;
    }
    std::fclose(gTrace.file);
    log::Info("camera: trace {} ({} rows): {}", why, gTrace.rows, gTrace.path);
    gTrace = Trace{};
}

void TraceTick() {
    if (gTrace.file == nullptr) {
        return;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    double const t = static_cast<double>(now.QuadPart - gTrace.start.QuadPart) / static_cast<double>(gTrace.frequency.QuadPart);
    if (t >= gTrace.seconds) {
        TraceClose("done");
        return;
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    ue3::Object* const mesh = pawn != nullptr ? player::Mesh(pawn) : nullptr;
    ue3::Object* const camera = controller != nullptr ? player::Camera(controller) : nullptr;
    if (pawn == nullptr || mesh == nullptr || camera == nullptr) {
        return;
    }
    if (mesh != gTrace.mesh) {
        gTrace.mesh = mesh;
        auto const skeleton = engine::skeleton::ReadRefSkeleton(mesh);
        for (int b = 0; b < kTraceBoneCount; ++b) {
            gTrace.bones[b] = -1;
            for (std::size_t i = 0; skeleton && i < skeleton->names.size(); ++i) {
                if (skeleton->names[i] == kTraceBones[b]) {
                    gTrace.bones[b] = static_cast<int>(i);
                }
            }
        }
    }
    auto const at = player::Location(pawn).value_or(ue3::Vector{});
    auto const rot = player::Rotation(pawn).value_or(ue3::Rotator{});
    auto const vel = gActorVelocity.Read<ue3::Vector>(pawn).value_or(ue3::Vector{});
    auto const pov = player::CameraPov(camera);
    if (!pov || pov->location == nullptr || pov->rotation == nullptr) {
        return;
    }
    std::string row = std::format("{:.4f},{},{:.2f},{:.2f},{:.2f},{},{:.2f},{:.2f},{:.2f},{:.3f},{:.3f},{:.3f},{},{},{}", t,
        head_tracking::OnFoot() ? 1 : 0, at.x, at.y, at.z, rot.yaw, vel.x, vel.y, vel.z, pov->location->x, pov->location->y,
        pov->location->z, pov->rotation->pitch, pov->rotation->yaw, pov->rotation->roll);
    for (int b = 0; b < kTraceBoneCount; ++b) {
        auto const c = gTrace.bones[b] >= 0
            ? engine::skeleton::BoneLocation(mesh, gTrace.bones[b], engine::skeleton::Space::Component) : std::nullopt;
        row += c ? std::format(",{:.3f},{:.3f},{:.3f}", c->x, c->y, c->z) : std::string(",,,");
    }
    row += '\n';
    std::fputs(row.c_str(), gTrace.file);
    ++gTrace.rows;
}

// Our reflection layer (research/34): the fields research/18 read from the code (CalculateOffsetHook
// 0xCAEF50, GetCameraLocation 0xCB2240): PlayerController.PlayerCamera +0x6B0,
// SFXCameraNativeBase.CurrentCameraMode +0x568, SFXCameraMode m_pov +0x98, Offset +0xB4, HookOffset +0xC0,
// LastHookPos +0xCC, HookName +0xE4, SFXCameraMode_Interpolate.To +0x168; engine/ReflectCheck compares each.
ue3::ClassRef gCameraNativeBase{"SFXGame.SFXCameraNativeBase"};
ue3::ClassRef gCameraModeInterpolate{"SFXGame.SFXCameraMode_Interpolate"};
ue3::FieldRef gCurrentCameraMode{"SFXGame.SFXCameraNativeBase", "CurrentCameraMode"};
ue3::FieldRef gInterpolateTo{"SFXGame.SFXCameraMode_Interpolate", "To"};
ue3::FieldRef gModePov{"SFXGame.SFXCameraMode", "m_pov"};
ue3::FieldRef gModePovLocation{gModePov, "Location"};
ue3::FieldRef gModePovRotation{gModePov, "Rotation"};
ue3::FieldRef gModeOffset{"SFXGame.SFXCameraMode", "Offset"};
ue3::FieldRef gModeHookOffset{"SFXGame.SFXCameraMode", "HookOffset"};
ue3::FieldRef gModeLastHookPos{"SFXGame.SFXCameraMode", "LastHookPos"};
ue3::FieldRef gModeHookName{"SFXGame.SFXCameraMode", "HookName"};
ue3::FieldRef gModeFirstPerson{"SFXGame.SFXCameraMode", "bFirstPerson"};

constexpr ULONGLONG kMinIntervalMs = 250;
constexpr double kRotatorToRadians = std::numbers::pi / 32768.0;

ik::CameraModeKey gLogged;
bool gHaveLogged = false;
std::string gLoggedMode;
ULONGLONG gLastLogMs = 0;

ik::Vec3 V(ue3::Vector const& v) {
    return {v.x, v.y, v.z};
}

ik::Vec3 Read(ue3::FieldRef& field, void const* base) {
    return V(field.Read<ue3::Vector>(base).value_or(ue3::Vector{}));
}

std::string Text(ik::Vec3 const& v) {
    return std::format("({:.1f}, {:.1f}, {:.1f})", v.x, v.y, v.z);
}

std::string ModeName(ue3::Object* mode) {
    return mode != nullptr ? std::format("{} '{}'", ue3::runtime::FullName(ue3::runtime::Get().ClassOf(mode)), ue3::runtime::FullName(mode))
                           : std::string("none");
}

// The hook bone against the game camera and the formula's camera against the real one.
std::string HookPlacement(ue3::Object* mode, ue3::Object* camera, std::uint64_t hookName) {
    ue3::Object* const pawn = player::BioPawn(player::Controller());
    ue3::Object* const mesh = player::Mesh(pawn);
    if (pawn == nullptr || mesh == nullptr || player::SkeletalMesh(mesh) == nullptr) {
        return "no player body";
    }
    int const bone = engine::skeleton::FindBone(mesh, hookName);
    if (bone < 0) {
        return std::format("the hook isn't a bone of {} (None: the feet; CoverSlot: the cover slot)",
            ue3::runtime::FullName(player::SkeletalMesh(mesh)));
    }
    auto const at = engine::skeleton::BoneLocation(mesh, bone, engine::skeleton::Space::World);
    if (!at) {
        return std::format("hook bone {} not posed", bone);
    }
    auto const pov = player::CameraPov(camera);
    void* const own = gModePov.Address(mode);
    ue3::Rotator const* const ownRotation = gModePovRotation.In<ue3::Rotator>(own);
    if (!pov || ownRotation == nullptr) {
        return "the camera's views unreadable";
    }
    ik::Vec3 const hook{at->x, at->y, at->z};
    ik::Vec3 const game = V(*pov->location);
    double const gameYaw = pov->rotation->yaw * kRotatorToRadians;
    ik::Vec3 const fromView = ik::InViewAxes(hook - game, gameYaw);
    // GetCameraLocation's formula, with the mode's own view rotation (what it used).
    ik::Vec3 const predicted = ik::CameraFromHook(hook, Read(gModeHookOffset, mode), Read(gModeOffset, mode),
        ownRotation->pitch * kRotatorToRadians, ownRotation->yaw * kRotatorToRadians);
    ik::Vec3 const modeAt = Read(gModePovLocation, own);
    ik::Vec3 const lastHook = Read(gModeLastHookPos, mode);
    return std::format("hook bone {}({}) at {} world, {:.1f} forward, {:.1f} right, {:.1f} up of the game camera {} (yaw {}); "
                       "the formula from it puts the camera at {}: {:.1f} units from the mode's m_pov {}, {:.1f} from the "
                       "game camera; LastHookPos {} is {:.1f} from the bone",
        ue3::runtime::NameText(hookName), bone, Text(hook), fromView.x, fromView.y, fromView.z, Text(game), pov->rotation->yaw,
        Text(predicted), ik::Length(predicted - modeAt), Text(modeAt), ik::Length(predicted - game), Text(lastHook),
        ik::Length(lastHook - hook));
}

void TickLe1() {
    if (!head_tracking::OnFoot()) {
        return;  // conversations and cutscenes cut between their own cameras
    }
    ue3::Object* const camera = player::Camera(player::Controller());
    if (camera == nullptr || !ue3::IsA(camera, gCameraNativeBase)) {
        return;
    }
    ue3::Object* const mode = gCurrentCameraMode.Read<ue3::Object*>(camera).value_or(nullptr);
    if (mode == nullptr || !ue3::runtime::IsAlive(mode)) {
        return;
    }
    // The mode whose hook is logged: an interpolation's target (it blends from one mode to it).
    ue3::Object* placing = mode;
    std::string to;
    if (ue3::IsA(mode, gCameraModeInterpolate)) {
        ue3::Object* const target = gInterpolateTo.Read<ue3::Object*>(mode).value_or(nullptr);
        if (target != nullptr && ue3::runtime::IsAlive(target)) {
            to = std::format(", interpolating to {} (its hook below)", ModeName(target));
            placing = target;
        }
    }
    std::optional<ue3::Name> const hookName = gModeHookName.Read<ue3::Name>(placing);
    if (!hookName) {
        return;
    }
    ik::CameraModeKey const key{reinterpret_cast<std::uintptr_t>(mode), hookName->Key(), Read(gModeHookOffset, placing),
        Read(gModeOffset, placing)};
    if (gHaveLogged && !ik::CameraModeChanged(gLogged, key)) {
        return;
    }
    ULONGLONG const now = GetTickCount64();
    if (gHaveLogged && now - gLastLogMs < kMinIntervalMs) {
        return;  // seen again next tick
    }
    std::string const name = ModeName(mode);
    bool const sameMode = gHaveLogged && gLogged.mode == key.mode;
    log::Info("camera: mode {}{}{}: HookName {}, HookOffset {}, Offset {}{}; {}", name, to,
        sameMode ? " (its hook or offset changed)" : gHaveLogged ? std::format(" (was {})", gLoggedMode) : std::string(),
        ue3::runtime::NameText(*hookName), Text(key.hookOffset), Text(key.offset),
        gModeFirstPerson.ReadBool(placing).value_or(false) ? ", bFirstPerson" : "", HookPlacement(placing, camera, key.hookName));
    gLogged = key;
    gLoggedMode = name;
    gHaveLogged = true;
    gLastLogMs = now;
}

#endif

} // namespace

void Tick() {
#if defined(MEVR_GAME_LE1)
    TraceTick();
    TickLe1();
#endif
}

std::string Command(std::string const& args) {
#if defined(MEVR_GAME_LE1)
    std::istringstream words(args);
    std::string sub;
    double seconds = 10.0;
    words >> sub >> seconds;
    if (sub != "trace") {
        return "usage: mevr camera trace <seconds>";
    }
    seconds = std::clamp(seconds, 1.0, 120.0);
    TraceClose("replaced");
    wchar_t buffer[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) == 0) {
        return "camera: no LOCALAPPDATA";
    }
    std::filesystem::path const folder = std::filesystem::path(buffer) / L"MEVR";
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::filesystem::path const file = folder / std::format(L"camera-trace-{:04}-{:02}-{:02}_{:02}-{:02}-{:02}.csv", st.wYear,
        st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, file.c_str(), L"w") != 0 || f == nullptr) {
        return "camera: can't write the trace file";
    }
    std::string header = "t,onFoot,pawnX,pawnY,pawnZ,pawnYaw,velX,velY,velZ,camX,camY,camZ,camPitch,camYaw,camRoll";
    for (char const* name : kTraceBones) {
        header += std::format(",{0}X,{0}Y,{0}Z", name);
    }
    header += '\n';
    std::fputs(header.c_str(), f);
    gTrace.file = f;
    gTrace.path = ini::Utf8(file);
    gTrace.seconds = seconds;
    QueryPerformanceFrequency(&gTrace.frequency);
    QueryPerformanceCounter(&gTrace.start);
    log::Info("camera: trace for {:.0f} s: {}", seconds, gTrace.path);
    return std::format("camera: tracing {:.0f} s to {}", seconds, gTrace.path);
#else
    (void)args;
    return "camera: LE1 only";
#endif
}

void Shutdown() {
#if defined(MEVR_GAME_LE1)
    TraceClose("stopped (unload)");
    gLogged = {};
    gHaveLogged = false;
    gLoggedMode.clear();
    gLastLogMs = 0;
#endif
}

} // namespace mevr::game::camera_probe

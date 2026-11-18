// SPDX-License-Identifier: MIT
#include "game/CameraShake.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/HostView.hpp"
#include "engine/SettingsFile.hpp"
#include "game/HeadTracking.hpp"
#include "game/Player.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"
#include "core/vr/ActionCamera.hpp"
#include "engine/GameAdapter.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace mevr::game::camera_shake {

#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)

namespace {

// ME1's screen shakes are scaled at their sources (a weapon's stats, the Mako); ME2 and ME3 have none of these names, so
// none of them compiles into their builds (docs/trilogy-parity-ledger.md G11): their adapter is the camera's modifiers below.
#if defined(MEVR_GAME_LE1)
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Our reflection layer (research/34); each has a row in engine/ReflectCheck.
ue3::FieldRef gWeaponStats{"SFXGame.BioWeaponRanged", "Stats"};
ue3::FieldRef gFiringShake{"SFXGame.SFXWeaponStatsBase", "FiringShake"};
ue3::FieldRef gTightAimFiringShake{"SFXGame.SFXWeaponStatsBase", "TightAimFiringShake"};
ue3::ClassRef gVehicleWheeled{"SFXGame.BioVehicleWheeled"};
ue3::FieldRef gJumpShake{"SFXGame.BioVehicleWheeled", "JumpShake"};
ue3::FieldRef gBoostShake{"SFXGame.BioVehicleWheeled", "BoostShake"};
ue3::FieldRef gTheShake{"SFXGame.SFXCameraShakeBase", "TheShake"};
ue3::FieldRef gRotAmplitude{gTheShake, "RotAmplitude"};
ue3::FieldRef gLocAmplitude{gTheShake, "LocAmplitude"};
ue3::FieldRef gFovAmplitude{gTheShake, "FOVAmplitude"};
#endif
// The camera modes' shakes from the body: ME2's SFXCameraMode has the same switch (GetCurrentShake reads the pawn behaviour's
// camera shake interface only while it's set; research/le2-vrik.md "Camera shakes").
ue3::ClassRef gCameraNativeBase{"SFXGame.SFXCameraNativeBase", ue3::kEveryGame};
ue3::ClassRef gCameraModeInterpolate{"SFXGame.SFXCameraMode_Interpolate", ue3::kEveryGame};
ue3::FieldRef gCurrentCameraMode{"SFXGame.SFXCameraNativeBase", "CurrentCameraMode", ue3::kEveryGame};
ue3::FieldRef gInterpolateTo{"SFXGame.SFXCameraMode_Interpolate", "To", ue3::kEveryGame};
ue3::FieldRef gShakeEnabled{"SFXGame.SFXCameraMode", "bIsCameraShakeEnabled", ue3::kEveryGame};
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
// ME2's screen shakes have no per-weapon source (ME1's SFXWeaponStatsBase.FiringShake and SFXCameraShakeBase don't exist): every
// shake the game's script starts goes to the player camera's SFXCameraModifier_ScreenShake (SFXPlayerCamera.AddScreenShake:
// BioPlayerController.CameraShake, footsteps, hits, powers), and the engine's own (CameraShake objects: matinees, animation
// notifies, Kismet) to Camera.CameraShakeCamMod. Both are camera modifiers: their bDisabled keeps them off the view.
// ME3 has the same two modifiers on the same classes (its SDK lists SFXPlayerCamera.ScreenShakeModifier, Camera.
// CameraShakeCamMod and CameraModifier.bDisabled) and takes ME2's way; which of its shakes go through them is [verify] in
// the game (`camera shake: ... off in VR` names each modifier it found).
ue3::FieldRef gScreenShakeModifier{"SFXGame.SFXPlayerCamera", "ScreenShakeModifier", ue3::kEveryGame};
ue3::FieldRef gCameraShakeCamMod{"Engine.Camera", "CameraShakeCamMod", ue3::kEveryGame};
ue3::FieldRef gModifierDisabled{"Engine.CameraModifier", "bDisabled", ue3::kEveryGame};
// The modifiers we disabled (to put back), and whether a scale between none and all was asked for (not built on ME2).
std::vector<ue3::Object*> gModifiersOff;
bool gToldPartial = false;
#endif

struct Settings {
    float screen = 0.0f;  // [Comfort] screenShake: the screen shakes' share (0 none, 1 the game's)
    bool view = false;    // [Comfort] viewShake: the camera modes' shakes from the body (run, shots, blasts)
};
Settings gSettings;
std::optional<float> gScreenOverride;  // `mevr shake ...` (this session)
std::optional<bool> gViewOverride;
std::uint64_t gSettingsMs = 0;
std::string gSettingsText;

#if defined(MEVR_GAME_LE1)
// A screen shake we scaled: its object, its own amplitudes, and the scale written.
struct Scaled {
    ue3::Object* shake = nullptr;
    std::string name;
    Vec3 rot, loc;
    float fov = 0.0f;
    float scale = 1.0f;
};
std::vector<Scaled> gScaled;
#endif
std::vector<ue3::Object*> gModesCleared;
std::uint64_t gScales = 0, gModeClears = 0, gModeRestores = 0;
bool gScreenActive = false, gViewActive = false;
std::string gWhy = "not started";

void RefreshSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gSettingsMs != 0 && now - gSettingsMs < 1000) {
        return;
    }
    gSettingsMs = now;
    ini::Loaded const loaded = ini::Read(engine::SettingsFile());
    Settings s;
    if (auto const v = ini::GetNumber(loaded.file, "Comfort", "screenShake")) {
        s.screen = static_cast<float>(std::clamp(*v, 0.0, 1.0));
    }
    if (auto const v = ini::GetNumber(loaded.file, "Comfort", "viewShake")) {
        s.view = *v != 0.0;
    }
    if (gScreenOverride) {
        s.screen = *gScreenOverride;
    }
    if (gViewOverride) {
        s.view = *gViewOverride;
    }
    std::string const text = std::format("screenShake {:.2f} (the guns' and the Mako's screen shakes), viewShake {} (the body's "
                                         "camera shakes in first person: the run, shots, blasts)",
        s.screen, s.view ? 1 : 0);
    if (text != gSettingsText) {
        gSettingsText = text;
        log::Info("camera shake: [Comfort] {}", text);
    }
    gSettings = s;
}

// ---- 1. Screen shakes, scaled at their source ----------------------------------------------------------------------

#if defined(MEVR_GAME_LE1)  // ME1's sources (ME2 and ME3 turn their shake modifiers off instead)
bool WriteAmplitudes(Scaled const& s, float scale) {
    void* const base = gTheShake.Address(s.shake);
    if (base == nullptr) {
        return false;
    }
    Vec3* const rot = gRotAmplitude.In<Vec3>(base);
    Vec3* const loc = gLocAmplitude.In<Vec3>(base);
    float* const fov = gFovAmplitude.In<float>(base);
    if (rot == nullptr || loc == nullptr || fov == nullptr) {
        return false;
    }
    *rot = {s.rot.x * scale, s.rot.y * scale, s.rot.z * scale};
    *loc = {s.loc.x * scale, s.loc.y * scale, s.loc.z * scale};
    *fov = s.fov * scale;
    write_journal::Wrote("CameraShake.RotAmplitude", rot, sizeof(Vec3));
    write_journal::Wrote("CameraShake.LocAmplitude", loc, sizeof(Vec3));
    write_journal::Wrote("CameraShake.FOVAmplitude", fov, sizeof(float));
    return true;
}

void Scale(ue3::Object* shake, char const* what, float scale) {
    if (shake == nullptr || !ue3::runtime::IsAlive(shake)) {
        return;
    }
    auto it = std::find_if(gScaled.begin(), gScaled.end(), [&](Scaled const& s) { return s.shake == shake; });
    if (it == gScaled.end()) {
        void* const base = gTheShake.Address(shake);
        if (base == nullptr) {
            return;
        }
        auto const rot = gRotAmplitude.Read<Vec3>(base);
        auto const loc = gLocAmplitude.Read<Vec3>(base);
        auto const fov = gFovAmplitude.Read<float>(base);
        if (!rot || !loc || !fov) {
            return;
        }
        Scaled s;
        s.shake = shake;
        s.name = ue3::runtime::FullName(shake);
        s.rot = *rot;
        s.loc = *loc;
        s.fov = *fov;
        gScaled.push_back(s);
        it = gScaled.end() - 1;
        log::Info("camera shake: {} {}: rotation ({:.2f}, {:.2f}, {:.2f}), place ({:.2f}, {:.2f}, {:.2f}), FOV {:.2f}; at {:.0f}% in VR",
            what, s.name, s.rot.x, s.rot.y, s.rot.z, s.loc.x, s.loc.y, s.loc.z, s.fov, scale * 100.0f);
    } else if (it->scale == scale) {
        return;
    }
    if (WriteAmplitudes(*it, scale)) {
        it->scale = scale;
        ++gScales;
    }
}
#endif

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
bool SetModifier(ue3::Object* modifier, bool disabled) {
    if (modifier == nullptr || !ue3::runtime::IsAlive(modifier) || gModifierDisabled.ReadBool(modifier).value_or(!disabled) == disabled ||
        !gModifierDisabled.WriteBool(modifier, disabled)) {
        return false;
    }
    if (void* const field = gModifierDisabled.Address(modifier)) {
        write_journal::Wrote("CameraShake.modifier bDisabled", field, 4);
    }
    return true;
}
#endif

void RestoreScreen(char const* why) {
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    int back = 0;
    for (ue3::Object* const modifier : gModifiersOff) {
        back += SetModifier(modifier, false) ? 1 : 0;
    }
    if (!gModifiersOff.empty()) {
        log::Info("camera shake: the game's screen shakes back on ({} of {} camera modifiers; {})", back, gModifiersOff.size(), why);
    }
    gModifiersOff.clear();
#else
    int restored = 0;
    for (Scaled& s : gScaled) {
        if (s.scale != 1.0f && ue3::runtime::IsAlive(s.shake) && WriteAmplitudes(s, 1.0f)) {
            s.scale = 1.0f;
            ++restored;
        }
    }
    if (restored > 0) {
        log::Info("camera shake: the game's screen shakes back on {} of {} ({})", restored, gScaled.size(), why);
    }
#endif
}

void TickScreen(bool vr) {
#if defined(MEVR_GAME_LE1)
    std::erase_if(gScaled, [](Scaled const& s) { return !ue3::runtime::IsAlive(s.shake); });  // freed with a level
#endif
    float const scale = vr ? gSettings.screen : 1.0f;
    gScreenActive = vr && scale < 1.0f;
    if (!gScreenActive) {
        RestoreScreen(vr ? "[Comfort] screenShake = 1" : "VR is off");
        return;
    }
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    // ME2: the camera's two shake modifiers off in VR, as ME1's sources at 0. A share between none and all would scale each
    // shake's amplitudes as it's added; not built: below one, the shakes are off.
    if (scale > 0.0f && !gToldPartial) {
        gToldPartial = true;
        log::Info("camera shake: [Comfort] screenShake {:.2f}: this game's screen shakes are on or off (off below 1); a share of them isn't built",
            scale);
    }
    std::erase_if(gModifiersOff, [](ue3::Object* m) { return !ue3::runtime::IsAlive(m); });
    ue3::Object* const camera = player::Camera(player::Controller());
    if (camera == nullptr || !ue3::runtime::IsAlive(camera)) {
        return;
    }
    struct Named {
        ue3::FieldRef* field;
        char const* what;
    };
    for (Named const& named : {Named{&gScreenShakeModifier, "SFXPlayerCamera.ScreenShakeModifier (the game's screen shakes)"},
             Named{&gCameraShakeCamMod, "Camera.CameraShakeCamMod (the engine's camera shakes)"}}) {
        ue3::Object* const modifier = named.field->Read<ue3::Object*>(camera).value_or(nullptr);
        if (SetModifier(modifier, true)) {
            gModifiersOff.push_back(modifier);
            ++gScales;
            log::Info("camera shake: {} {} off in VR ([Comfort] screenShake {:.2f})", named.what, ue3::runtime::FullName(modifier),
                scale);
        }
    }
#else
    for (Scaled& s : gScaled) {  // a changed setting reaches the shakes already seen
        if (s.scale != scale && WriteAmplitudes(s, scale)) {
            s.scale = scale;
            ++gScales;
        }
    }
    if (ue3::Object* const held = weapon::HeldObject()) {
        if (ue3::Object* const stats = gWeaponStats.Read<ue3::Object*>(held).value_or(nullptr); stats != nullptr && ue3::runtime::IsAlive(stats)) {
            Scale(gFiringShake.Read<ue3::Object*>(stats).value_or(nullptr), "a shot's screen shake", scale);
            Scale(gTightAimFiringShake.Read<ue3::Object*>(stats).value_or(nullptr), "an aimed shot's screen shake", scale);
        }
    }
    ue3::Object* const pawn = player::Pawn(player::Controller());
    if (pawn != nullptr && ue3::runtime::IsAlive(pawn) && ue3::IsA(pawn, gVehicleWheeled)) {
        Scale(gJumpShake.Read<ue3::Object*>(pawn).value_or(nullptr), "the Mako's jump jets' screen shake", scale);
        Scale(gBoostShake.Read<ue3::Object*>(pawn).value_or(nullptr), "the Mako's boost's screen shake", scale);
    }
#endif
}

// ---- 2. The camera modes' shakes from the body, off in first person ------------------------------------------------

// Halcyon's first-person classes (head_aim.cpp FpStateForClass), as game/CameraCollision: never the Mako, the scope or blind fire.
bool FirstPersonClass(std::string_view name) {
    if (name.find("Vehicle") != std::string_view::npos || name.find("Sniper") != std::string_view::npos ||
        name.find("HipAim") != std::string_view::npos) {
        return false;
    }
    if constexpr (engine::adapter::kActionCameras) {
        if (vr::IsActionCamera(name)) {
            return true;
        }  // ME3's roll, melee and hit cameras: first person too (the owner, 2026-11-18: no shakes there)
    }
    return name.find("Explore") != std::string_view::npos || name.find("Combat") != std::string_view::npos ||
           name.find("TightAim") != std::string_view::npos;
}

void ClearMode(ue3::Object* mode) {
    if (mode == nullptr || !ue3::runtime::IsAlive(mode)) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    std::string const cls = r.NameText(r.ClassOf(mode));
    if (!FirstPersonClass(cls) || !gShakeEnabled.ReadBool(mode).value_or(false) || !gShakeEnabled.WriteBool(mode, false)) {
        return;
    }
    if (void* const field = gShakeEnabled.Address(mode)) {
        write_journal::Wrote("CameraShake.bIsCameraShakeEnabled", field, 4);
    }
    gModesCleared.push_back(mode);
    if (gModeClears++ == 0) {
        log::Info("camera shake: {} shook the first-person view with the body's camera shakes (the run, shots, blasts); off in VR "
                  "([Comfort] viewShake = 1 keeps them)", cls);
    }
}

void RestoreModes(char const* why) {
    int restored = 0;
    for (ue3::Object* const mode : gModesCleared) {
        if (ue3::runtime::IsAlive(mode) && gShakeEnabled.WriteBool(mode, true)) {
            if (void* const field = gShakeEnabled.Address(mode)) {
                write_journal::Wrote("CameraShake.bIsCameraShakeEnabled", field, 4);
            }
            ++restored;
        }
    }
    if (!gModesCleared.empty()) {
        log::Info("camera shake: the camera modes' shakes put back on {} of {} ({})", restored, gModesCleared.size(), why);
    }
    gModeRestores += static_cast<std::uint64_t>(restored);
    gModesCleared.clear();
}

void TickView(bool tracked) {
    bool const wanted = !gSettings.view;
    auto const mode = head_tracking::GameMode();
    bool const onFoot = mode && *mode == 0;
    gViewActive = wanted && onFoot && tracked;
    if (!gViewActive) {
        // Not on foot keeps what was cleared (the same modes come back); the setting or a view that isn't head-tracked
        // gives them back.
        if ((!wanted || (onFoot && !tracked)) && !gModesCleared.empty()) {
            RestoreModes(!wanted ? "[Comfort] viewShake = 1" : "the view isn't head-tracked");
        }
        return;
    }
    std::erase_if(gModesCleared, [](ue3::Object* m) { return !ue3::runtime::IsAlive(m); });
    ue3::Object* const camera = player::Camera(player::Controller());
    if (camera == nullptr || !ue3::IsA(camera, gCameraNativeBase)) {
        return;
    }
    ue3::Object* const current = gCurrentCameraMode.Read<ue3::Object*>(camera).value_or(nullptr);
    if (current == nullptr || !ue3::runtime::IsAlive(current)) {
        return;
    }
    ClearMode(current);
    if (ue3::IsA(current, gCameraModeInterpolate)) {
        ClearMode(gInterpolateTo.Read<ue3::Object*>(current).value_or(nullptr));  // the blend's destination
    }
}

bool gScreenReady = false, gViewReady = false;

} // namespace

bool TryStartup() {
    if (!ue3::runtime::Up()) {
        return false;
    }
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    gScreenReady = gScreenShakeModifier.Get() != nullptr && gCameraShakeCamMod.Get() != nullptr && gModifierDisabled.Get() != nullptr;
#else
    gScreenReady = gTheShake.Get() != nullptr && gRotAmplitude.Get() != nullptr && gLocAmplitude.Get() != nullptr &&
                   gFovAmplitude.Get() != nullptr && gFiringShake.Get() != nullptr && gWeaponStats.Get() != nullptr;
#endif
    gViewReady = gShakeEnabled.Get() != nullptr && gCurrentCameraMode.Get() != nullptr;
    if (!gScreenReady) {
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
        log::Warn("camera shake: SFXPlayerCamera.ScreenShakeModifier, Camera.CameraShakeCamMod or CameraModifier.bDisabled not found: "
                  "the game's screen shakes stay as they are");
#else
        log::Warn("camera shake: SFXCameraShakeBase.TheShake or the weapon's FiringShake not found: the game's screen shakes stay as "
                  "they are");
#endif
    }
    if (!gViewReady) {
        log::Warn("camera shake: SFXCameraMode.bIsCameraShakeEnabled not found: the body's camera shakes stay on in first person");
    }
    gWhy = "started";
    return gScreenReady || gViewReady;
}

void Tick() {
    RefreshSettings();
    bool const tracked = vr::host_view::Latest().valid;
    if (gScreenReady) {
        TickScreen(tracked);
    }
    if (gViewReady) {
        TickView(tracked);
    }
    gWhy = !tracked ? "no head-tracked view" : "VR";
}

std::string Command(std::string const& args) {
    std::istringstream words(args);
    std::string verb, value;
    words >> verb >> value;
    verb = ini::Lower(verb);
    value = ini::Lower(value);
    if (verb == "screen" && !value.empty()) {
        gScreenOverride = static_cast<float>(std::clamp(ini::Number(value).value_or(0.0), 0.0, 1.0));
        gSettingsMs = 0;
    } else if (verb == "view" && (value == "on" || value == "off")) {
        gViewOverride = value == "on";
        gSettingsMs = 0;
    } else if (!verb.empty() && verb != "status") {
        return "mevr shake [screen <0..1> | view on|off | status]";
    }
    RefreshSettings();
    return StatusJson();
}

std::string StatusJson() {
    std::string shakes;
#if defined(MEVR_GAME_LE1)
    for (Scaled const& s : gScaled) {
        shakes += std::format("{}{{\"name\": \"{}\", \"scale\": {:.2f}, \"rot\": [{:.2f}, {:.2f}, {:.2f}], \"loc\": [{:.2f}, {:.2f}, {:.2f}], "
                              "\"fov\": {:.2f}}}",
            shakes.empty() ? "" : ", ", s.name, s.scale, s.rot.x, s.rot.y, s.rot.z, s.loc.x, s.loc.y, s.loc.z, s.fov);
    }
#endif
    return std::format("{{\"why\": \"{}\", \"screenShake\": {:.2f}, \"viewShake\": {}, \"screenActive\": {}, \"viewActive\": {}, "
                       "\"scales\": {}, \"modesCleared\": {}, \"modeClears\": {}, \"modeRestores\": {}, \"shakes\": [{}]}}",
        gWhy, gSettings.screen, gSettings.view ? "true" : "false", gScreenActive ? "true" : "false", gViewActive ? "true" : "false",
        gScales, gModesCleared.size(), gModeClears, gModeRestores, shakes);
}

void Shutdown() {
    RestoreScreen("the mod unloads");
    RestoreModes("the mod unloads");
#if defined(MEVR_GAME_LE1)
    gScaled.clear();
#endif
    gScales = gModeClears = gModeRestores = 0;
    gScreenActive = gViewActive = gScreenReady = gViewReady = false;
    gScreenOverride.reset();
    gViewOverride.reset();
    gSettingsMs = 0;
    gSettingsText.clear();
    gWhy = "not started";
#if defined(MEVR_GAME_LE1)
    for (ue3::FieldRef* f : {&gWeaponStats, &gFiringShake, &gTightAimFiringShake, &gJumpShake, &gBoostShake, &gTheShake, &gRotAmplitude,
             &gLocAmplitude, &gFovAmplitude, &gCurrentCameraMode, &gInterpolateTo, &gShakeEnabled}) {
#else
    for (ue3::FieldRef* f : {&gCurrentCameraMode, &gInterpolateTo, &gShakeEnabled}) {
#endif
        f->Reset();
    }
#if defined(MEVR_GAME_LE1)
    gVehicleWheeled.Reset();
#endif
    gCameraNativeBase.Reset();
    gCameraModeInterpolate.Reset();
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    gScreenShakeModifier.Reset();
    gCameraShakeCamMod.Reset();
    gModifierDisabled.Reset();
    gToldPartial = false;
#endif
}

#else

bool TryStartup() {
    return false;
}

void Tick() {}

std::string Command(std::string const&) {
    return "mevr shake: no game in this build";
}

std::string StatusJson() {
    return "null";
}

void Shutdown() {}

#endif

} // namespace mevr::game::camera_shake

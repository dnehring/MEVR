// SPDX-License-Identifier: MIT
#include "game/ZoomedShots.hpp"

#if !defined(MEVR_GAME_LE1)
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CallFunction.hpp"
#include "engine/GameAdapter.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"
#include "ue3/Types.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#endif

// ME1 compiles nothing here: its support grip holds LT (engine::adapter::kSupportGripHoldsLt) and no caller asks.
#if !defined(MEVR_GAME_LE1)
namespace mevr::game::zoomed_shots {

namespace {

namespace adapter = engine::adapter;
constexpr std::size_t kCalls = std::size(adapter::kZoomedCalls);

// The held weapon (Engine.Pawn.Weapon, an SFXWeapon) and its zoom flag (SFXWeapon_NativeBase.bIsZoomed: ME2 0x3D0 bit 0x20,
// ME3 0x3BC bit 0x80), the same handles as game/Weapon's; the inventory manager's smoothed inaccuracy (0..1) for the measure.
ue3::FieldRef gPawnWeapon{"Engine.Pawn", "Weapon", ue3::kEveryGame};
ue3::ClassRef gWeaponClass{"SFXGame.SFXWeapon", ue3::kEveryGame};
ue3::FieldRef gIsZoomed{"SFXGame.SFXWeapon_NativeBase", "bIsZoomed", ue3::kEveryGame};
ue3::FieldRef gAccuracy{adapter::kAccuracyOwner, "Accuracy", ue3::kEveryGame};

struct Call {
    ue3::Function* function = nullptr;
    bool onWeapon = false;
    unsigned zoomed = 0;  // calls made as zoomed since the last heartbeat
    unsigned plain = 0;   // ...and as they were
    unsigned zoomedTotal = 0;
};
std::array<Call, kCalls> gCalls{};
bool gStarted = false;
std::int32_t gOffset = -1;
std::uint32_t gMask = 0;

enum class Force { Auto, On, Off };
Force gForce = Force::Auto;
bool gGate = false;
bool gShouldered = false;
bool gScoped = false;
bool gLoggedFirst[kCalls] = {};

// The measures, per state (0 as the call was, 1 made as zoomed): the inventory manager's Accuracy after each update, each
// shot's aim error, the last spread range and (ME3) the last sniper damage answered.
struct Mean {
    double sum = 0.0;
    unsigned n = 0;
    void Add(double v) { sum += v, ++n; }
    double Value() const { return n > 0 ? sum / n : -1.0; }
};
std::array<Mean, 2> gAccuracyMean{};
std::array<Mean, 2> gAimErrorMean{};
std::array<ue3::Vector2D, 2> gLastRange{};
std::array<bool, 2> gHaveRange{};
std::array<float, 2> gLastDamage{-1.0f, -1.0f};

int IndexOf(ue3::Function const* function) {
    for (std::size_t i = 0; i < kCalls; ++i) {
        if (gCalls[i].function == function) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

ue3::Object* HeldWeapon() {
    ue3::Object* const pawn = player::BioPawn(player::Controller());
    if (pawn == nullptr) {
        return nullptr;
    }
    ue3::Object* const weapon = gPawnWeapon.Read<ue3::Object*>(pawn).value_or(nullptr);
    return weapon != nullptr && ue3::runtime::IsAlive(weapon) && ue3::IsA(weapon, gWeaponClass) ? weapon : nullptr;
}

std::uint32_t* FlagWord(ue3::Object* weapon) {
    return reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(weapon) + gOffset);
}

// The bracket's way in: with the gate open and the call the held weapon's (or the player's inventory manager's), the weapon's
// bIsZoomed set unless the game has it set itself. The token is the weapon (0: nothing written).
std::uint64_t Enter(ue3::Object* context, ue3::Function* function) {
    int const index = IndexOf(function);
    if (index < 0) {
        return 0;
    }
    Call& call = gCalls[static_cast<std::size_t>(index)];
    ue3::Object* const weapon = gGate ? HeldWeapon() : nullptr;
    if (weapon == nullptr || (call.onWeapon && context != weapon)) {
        ++call.plain;
        return 0;
    }
    std::uint32_t* const word = FlagWord(weapon);
    if ((*word & gMask) != 0) {
        ++call.plain;  // the game's own zoom (never from our grip on the sequels): its own answer
        return 0;
    }
    *word |= gMask;
    write_journal::Wrote("ZoomedShots.bIsZoomed (one call)", word, sizeof(std::uint32_t));
    ++call.zoomed;
    ++call.zoomedTotal;
    if (!gLoggedFirst[index]) {
        gLoggedFirst[index] = true;
        log::Info("zoomed shots: first {} made as zoomed ({}): bIsZoomed set for that call only", adapter::kZoomedCalls[index].function,
            gScoped ? "a scope's picture" : gShouldered ? "the gun shouldered" : "forced: mevr zoomshots on");
    }
    return reinterpret_cast<std::uint64_t>(weapon);
}

// The way out, in the hook's __finally: the flag cleared again (it was clear when Enter set it) if the weapon still lives.
void Leave(ue3::Object* /*context*/, ue3::Function* /*function*/, std::uint64_t token) noexcept {
    if (token == 0) {
        return;
    }
    auto* const weapon = reinterpret_cast<ue3::Object*>(token);
    if (!ue3::runtime::IsAlive(weapon)) {
        return;
    }
    std::uint32_t* const word = FlagWord(weapon);
    *word &= ~gMask;
}

// After each call (inside the bracket): what it answered, by whether it was made as zoomed.
void After(ue3::Object* context, ue3::Function* function, void* result) {
    int const index = IndexOf(function);
    ue3::Object* const weapon = index >= 0 ? HeldWeapon() : nullptr;
    if (weapon == nullptr) {
        return;
    }
    Call const& call = gCalls[static_cast<std::size_t>(index)];
    if (call.onWeapon && context != weapon) {
        return;
    }
    std::size_t const zoomed = (*FlagWord(weapon) & gMask) != 0 ? 1 : 0;
    if (!call.onWeapon) {
        if (std::optional<float> const accuracy = gAccuracy.Read<float>(context); accuracy && std::isfinite(*accuracy)) {
            gAccuracyMean[zoomed].Add(*accuracy);
        }
    } else if (result != nullptr && index == 1) {
        gLastRange[zoomed] = *static_cast<ue3::Vector2D const*>(result);
        gHaveRange[zoomed] = true;
    } else if (result != nullptr && index == 2) {
        gLastDamage[zoomed] = *static_cast<float const*>(result);
    }
}

std::string MeanText(Mean const& m) {
    return m.n > 0 ? std::format("{:.4f} ({} samples)", m.Value(), m.n) : std::string("none");
}

} // namespace

bool TryStartup() {
    if (gStarted) {
        return true;
    }
    if (!ue3::runtime::Up()) {
        return false;
    }
    ue3::PropertyInfo const* const flag = gIsZoomed.Get();
    if (flag == nullptr || flag->kind != ue3::Kind::Bool || flag->boolMask == 0 || gPawnWeapon.Get() == nullptr ||
        gWeaponClass.Get() == nullptr) {
        log::Warn("zoomed shots: SFXWeapon_NativeBase.bIsZoomed, Pawn.Weapon or SFXWeapon not found: a shouldered shot keeps the "
                  "hip's accuracy (the support grip still holds no LT)");
        return false;
    }
    gOffset = flag->offset;
    gMask = flag->boolMask;
    std::string found;
    std::string missing;
    for (std::size_t i = 0; i < kCalls; ++i) {
        ue3::Function* const function = ue3::runtime::Get().FindFunction(adapter::kZoomedCalls[i].function);
        gCalls[i] = {function, adapter::kZoomedCalls[i].onWeapon};
        std::string& list = function != nullptr ? found : missing;
        list += std::format("{}{}", list.empty() ? "" : ", ", adapter::kZoomedCalls[i].function);
        if (function == nullptr) {
            continue;
        }
        if (!engine::call_function::AddScopedHandler(function, &Enter, &Leave)) {
            log::Warn("zoomed shots: the CallFunction hook refused a bracket on {}", adapter::kZoomedCalls[i].function);
            gCalls[i].function = nullptr;
            continue;
        }
        engine::call_function::AddPostHandler(function, &After);
    }
    gStarted = !found.empty();
    log::Info("zoomed shots: a shouldered or scoped shot's own calculations as zoomed, each call only (no LT, no camera zoom, no "
              "sway, no refusals): {}{}",
        found.empty() ? "none found" : found, missing.empty() ? std::string() : std::format("; not found: {}", missing));
    return gStarted;
}

void Update(bool shouldered, bool scoped, bool aimedFire) {
    gShouldered = shouldered && aimedFire;
    gScoped = scoped && aimedFire;
    bool const gate = gStarted && (gForce == Force::On || (gForce == Force::Auto && (gShouldered || gScoped)));
    if (gate != gGate) {
        gGate = gate;
        log::Info("zoomed shots: {}", gate ? std::format("on ({}): the shot's accuracy, spread{} as zoomed",
                                                 gForce == Force::On ? "forced" : gScoped ? "a scope's picture" : "the gun shouldered",
                                                 kCalls > 2 ? " and a sniper's damage" : "")
                                           : "off: the shot's calculations as the game makes them");
    }
}

bool Active() {
    return gGate;
}

void NoteAimError(float error) {
    if (std::isfinite(error)) {
        gAimErrorMean[gGate ? 1 : 0].Add(error);
    }
}

std::string Console(std::string_view args) {
    std::string verb(args);
    verb.erase(0, verb.find_first_not_of(' '));
    verb.erase(verb.find_last_not_of(' ') + 1);
    if (verb == "on") {
        gForce = Force::On;
    } else if (verb == "off") {
        gForce = Force::Off;
    } else if (verb == "auto") {
        gForce = Force::Auto;
    } else if (!verb.empty()) {
        return "mevr zoomshots [on|off|auto]: on makes every shot's calculations zoomed (a test without the support grip), off "
               "none, auto the shouldered gun's and the scope's rule";
    }
    Update(gShouldered, gScoped, true);
    std::string calls;
    for (std::size_t i = 0; i < kCalls; ++i) {
        calls += std::format("{}{} {}", i == 0 ? "" : ", ", adapter::kZoomedCalls[i].function,
            gCalls[i].function != nullptr ? std::format("zoomed {} times", gCalls[i].zoomedTotal) : std::string("not found"));
    }
    return std::format("zoomed shots: {} ({}; shouldered {}, scope {}); {}; accuracy zoomed {} / plain {}; aim error zoomed {} / "
                       "plain {}; spread range zoomed {} / plain {}{}",
        gGate ? "on" : "off", gForce == Force::On ? "forced on" : gForce == Force::Off ? "forced off" : "auto", gShouldered,
        gScoped, calls, MeanText(gAccuracyMean[1]), MeanText(gAccuracyMean[0]), MeanText(gAimErrorMean[1]),
        MeanText(gAimErrorMean[0]),
        gHaveRange[1] ? std::format("{:.4f}-{:.4f}", gLastRange[1].x, gLastRange[1].y) : std::string("none"),
        gHaveRange[0] ? std::format("{:.4f}-{:.4f}", gLastRange[0].x, gLastRange[0].y) : std::string("none"),
        kCalls > 2 ? std::format("; sniper damage zoomed {:.1f} / plain {:.1f}", gLastDamage[1], gLastDamage[0]) : std::string());
}

std::string StatusJson() {
    namespace json = vr::test_request;
    auto const range = [](std::size_t s) {
        return gHaveRange[s] ? std::format("[{}, {}]", json::JsonNumber(gLastRange[s].x), json::JsonNumber(gLastRange[s].y))
                             : std::string("null");
    };
    return std::format("{{\"on\": {}, \"shouldered\": {}, \"scoped\": {}, \"accuracy\": [{}, {}], \"aimError\": [{}, {}], "
                       "\"range\": [{}, {}], \"damage\": [{}, {}]}}",
        gGate ? "true" : "false", gShouldered ? "true" : "false", gScoped ? "true" : "false",
        json::JsonNumber(gAccuracyMean[0].Value()), json::JsonNumber(gAccuracyMean[1].Value()),
        json::JsonNumber(gAimErrorMean[0].Value()), json::JsonNumber(gAimErrorMean[1].Value()), range(0), range(1),
        json::JsonNumber(gLastDamage[0]), json::JsonNumber(gLastDamage[1]));
}

void LogStats() {
    if (!gStarted) {
        return;
    }
    std::string calls;
    bool any = false;
    for (std::size_t i = 0; i < kCalls; ++i) {
        Call& call = gCalls[i];
        any = any || call.zoomed > 0;
        calls += std::format("{}{} {}/{}", i == 0 ? "" : ", ", i == 0 ? "accuracy" : i == 1 ? "spread" : "damage", call.zoomed,
            call.zoomed + call.plain);
        call.zoomed = call.plain = 0;
    }
    if (!any && gAimErrorMean[1].n == 0) {
        return;
    }
    log::Info("zoomed shots: calls made as zoomed {}; accuracy zoomed {} / plain {}; aim error zoomed {} / plain {}; spread "
              "range zoomed {} / plain {}{}",
        calls, MeanText(gAccuracyMean[1]), MeanText(gAccuracyMean[0]), MeanText(gAimErrorMean[1]), MeanText(gAimErrorMean[0]),
        gHaveRange[1] ? std::format("{:.4f}-{:.4f}", gLastRange[1].x, gLastRange[1].y) : std::string("none"),
        gHaveRange[0] ? std::format("{:.4f}-{:.4f}", gLastRange[0].x, gLastRange[0].y) : std::string("none"),
        kCalls > 2 ? std::format("; sniper damage zoomed {:.1f} / plain {:.1f}", gLastDamage[1], gLastDamage[0]) : std::string());
}

void Shutdown() {
    // The handlers go with the CallFunction hook (ClearHandlers); a call in flight has put its flag back in its __finally.
    gStarted = false;
    gGate = false;
    gCalls = {};
}
} // namespace mevr::game::zoomed_shots
#endif

// SPDX-License-Identifier: MIT
#include "game/FocusPause.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "engine/ProcessEvent.hpp"
#include "game/HandCalibration.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <cstdint>
#include <format>
#include <string>

namespace mevr::game::focus_pause {

#if defined(MEVR_GAME_LE1)

namespace {

// Our reflection layer (research/34); each has a row in engine/ReflectCheck.
ue3::FunctionRef gWorldTick{"SFXGame.BioWorldInfo.Tick"};
ue3::FieldRef gPausedByFocusLoss{"SFXGame.BioWorldInfo", "m_bPausedByFocusLoss"};
ue3::FieldRef gPlayersOnly{"Engine.WorldInfo", "bPlayersOnly"};

std::uint64_t gUndone = 0;
bool gLogged = false;

void OnWorldTick(ue3::Object* world, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    if (world == nullptr || hand_calibration::GetGame().pauseUnfocused || !gPausedByFocusLoss.ReadBool(world).value_or(false)) {
        return;
    }
    gPausedByFocusLoss.WriteBool(world, false);
    gPlayersOnly.WriteBool(world, false);
    if (void* const field = gPlayersOnly.Address(world)) {
        write_journal::Wrote("FocusPause.bPlayersOnly", field, 4);
    }
    if (void* const field = gPausedByFocusLoss.Address(world)) {
        write_journal::Wrote("FocusPause.m_bPausedByFocusLoss", field, 4);
    }
    ++gUndone;
    if (!gLogged) {
        gLogged = true;
        log::Info("focus pause: the game window lost focus and the game froze the world; [Game] pauseUnfocused = 0: running on");
    }
}

} // namespace

bool TryStartup() {
    ue3::Function* const tick = ue3::runtime::Ready() ? gWorldTick.Get() : nullptr;
    if (tick == nullptr || gPausedByFocusLoss.Get() == nullptr || gPlayersOnly.Get() == nullptr) {
        log::Warn("focus pause: BioWorldInfo.Tick or its fields not found: the game pauses when its window loses focus, as always");
        return false;
    }
    engine::process_event::AddPostHandler(tick, &OnWorldTick);
    return true;
}

std::string StatusJson() {
    return std::format("{{\"pauseUnfocused\": {}, \"undone\": {}}}", hand_calibration::GetGame().pauseUnfocused ? "true" : "false", gUndone);
}

void Shutdown() {
    gUndone = 0;
    gLogged = false;
    gWorldTick.Reset();
    gPausedByFocusLoss.Reset();
    gPlayersOnly.Reset();
}

#else

bool TryStartup() {
    return false;
}

std::string StatusJson() {
    return "null";
}

void Shutdown() {}

#endif

} // namespace mevr::game::focus_pause

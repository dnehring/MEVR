// SPDX-License-Identifier: MIT
// The game's GUI cursor steered onto the pointer's point (game/GuiCursor.hpp, research/56 section 6).
#include "game/GuiCursor.hpp"

#include "core/Log.hpp"
#include "core/vr/GuiCursor.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <cstdint>
#include <format>
#include <sstream>
#include <string>

namespace mevr::game::gui_cursor {
namespace {

namespace mem = ue3::memory;
namespace steer = vr::gui_cursor;

ue3::FieldRef gScaleFormManager{"Engine.PlayerController", "m_oCachedScaleFormManager"};
ue3::FieldRef gFocusPanel{"Engine.BioSFManager", "m_FocusPanel"};

steer::Steer gSteer;
std::optional<std::pair<float, float>> gConsoleTarget;  // stage units (mevr guimouse to)
ue3::Object* gStagePanel = nullptr;                      // the panel whose stage size is known
float gStage[2] = {0.0f, 0.0f};
std::pair<float, float> gMouse{-1.0f, -1.0f};
std::optional<std::pair<float, float>> gTarget;          // this tick's, stage units
bool gSteering = false;
std::uint64_t gMoves = 0;
std::uint64_t gNotInFront = 0;
std::uint64_t gArrivals = 0;
bool gWasOnTarget = false;

ue3::Object* FocusPanel() {
    ue3::Object* const pc = player::Controller();
    ue3::Object* const manager = pc != nullptr ? gScaleFormManager.Read<ue3::Object*>(pc).value_or(nullptr) : nullptr;
    return manager != nullptr ? gFocusPanel.Read<ue3::Object*>(manager).value_or(nullptr) : nullptr;
}

std::optional<float> VariableFloat(ue3::Object* panel, wchar_t const* path) {
    if (panel == nullptr) {
        return std::nullopt;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Function* const f = r.FindFunction(r.ClassOf(panel), "GetVariableFloat");
    if (f == nullptr) {
        return std::nullopt;
    }
    ue3::Params p(r, f);
    mem::OwnedString name(path);
    if (!p.Valid() || !name.Valid()) {
        return std::nullopt;
    }
    p.Set("sPath", name.Get());
    if (!ue3::runtime::Call(panel, f, p)) {
        return std::nullopt;
    }
    return p.Return<float>();
}

std::optional<std::pair<float, float>> MovieMouse(ue3::Object* panel) {
    auto const x = VariableFloat(panel, L"_root._xmouse");
    auto const y = VariableFloat(panel, L"_root._ymouse");
    if (!x || !y) {
        return std::nullopt;
    }
    return std::pair{*x, *y};
}

// The movie's stage size (Stage.width/height: the window's aspect, 1280 wide), read once per panel.
bool StageOf(ue3::Object* panel) {
    if (panel == gStagePanel && gStage[0] > 0.0f) {
        return true;
    }
    auto const w = VariableFloat(panel, L"Stage.width");
    auto const h = VariableFloat(panel, L"Stage.height");
    if (!w || !h || !(*w > 1.0f) || !(*h > 1.0f)) {
        return false;
    }
    gStagePanel = panel;
    gStage[0] = *w;
    gStage[1] = *h;
    return true;
}

bool GameInFront() {
    HWND const front = GetForegroundWindow();
    DWORD pid = 0;
    if (front == nullptr || GetWindowThreadProcessId(front, &pid) == 0) {
        return false;
    }
    return pid == GetCurrentProcessId();
}

void Inject(int dx, int dy) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
}

} // namespace

void Tick(std::optional<std::pair<float, float>> pointer) {
    gTarget.reset();
    bool const wanted = gConsoleTarget.has_value() || pointer.has_value();
    if (!wanted) {
        if (gSteering) {
            gSteering = false;
            gSteer.Reset();
        }
        return;
    }
    ue3::Object* const panel = FocusPanel();
    if (panel == nullptr || !StageOf(panel)) {
        gSteer.Reset();
        return;
    }
    auto const mouse = MovieMouse(panel);
    if (!mouse) {
        gSteer.Reset();
        return;
    }
    gMouse = *mouse;
    gTarget = gConsoleTarget ? *gConsoleTarget : std::pair{pointer->first * gStage[0], pointer->second * gStage[1]};
    if (!GameInFront()) {
        ++gNotInFront;
        gSteer.Reset();
        return;
    }
    if (!gSteering) {
        gSteering = true;
        log::Info("gui cursor: steering the game's cursor from ({:.1f}, {:.1f}) to ({:.1f}, {:.1f}) of a {:.0f} x {:.0f} stage ({})",
            gMouse.first, gMouse.second, gTarget->first, gTarget->second, gStage[0], gStage[1],
            gConsoleTarget ? "the console's target" : "the pointer");
    }
    steer::Move const move = gSteer.Step(gMouse.first, gMouse.second, gTarget->first, gTarget->second);
    if (move.dx != 0 || move.dy != 0) {
        Inject(move.dx, move.dy);
        ++gMoves;
    }
    if (gSteer.OnTarget() && !gWasOnTarget) {
        ++gArrivals;
    }
    gWasOnTarget = gSteer.OnTarget();
}

std::string Command(std::string const& args) {
    std::istringstream words(args);
    std::string verb;
    words >> verb;
    if (verb == "to") {
        float x = 0.0f, y = 0.0f;
        words >> x >> y;
        gConsoleTarget = std::pair{x, y};
        return std::format("guimouse: steering to ({:.1f}, {:.1f}) stage units", x, y);
    }
    if (verb == "off") {
        gConsoleTarget.reset();
        return "guimouse: the console's target dropped";
    }
    ue3::Object* const panel = FocusPanel();
    auto const mouse = MovieMouse(panel);
    return std::format("guimouse: the movie's mouse {}; {}; {} moves sent, {} arrivals, {} ticks with the game not in front; gains "
                       "{:.3f}, {:.3f} stage units per count",
        mouse ? std::format("({:.1f}, {:.1f})", mouse->first, mouse->second) : std::string("unknown (no focused movie)"),
        gTarget ? std::format("target ({:.1f}, {:.1f})", gTarget->first, gTarget->second) : std::string("no target"), gMoves, gArrivals,
        gNotInFront, gSteer.Gain(0), gSteer.Gain(1));
}

std::string StatusJson() {
    return std::format("{{\"steering\":{},\"mouse\":[{:.1f},{:.1f}],\"target\":{},\"onTarget\":{},\"moves\":{},\"arrivals\":{},"
                       "\"notInFront\":{},\"gain\":[{:.3f},{:.3f}]}}",
        gSteering ? "true" : "false", gMouse.first, gMouse.second,
        gTarget ? std::format("[{:.1f},{:.1f}]", gTarget->first, gTarget->second) : std::string("null"),
        gSteer.OnTarget() ? "true" : "false", gMoves, gArrivals, gNotInFront, gSteer.Gain(0), gSteer.Gain(1));
}

} // namespace mevr::game::gui_cursor

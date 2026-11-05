// SPDX-License-Identifier: MIT
// Where the game's Flash cursor is (game/GuiCursor.hpp, research/56 section 6).
#include "game/GuiCursor.hpp"

#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <format>
#include <optional>
#include <string>

namespace mevr::game::gui_cursor {
namespace {

namespace mem = ue3::memory;

ue3::FieldRef gScaleFormManager{"Engine.PlayerController", "m_oCachedScaleFormManager"};
ue3::FieldRef gFocusPanel{"Engine.BioSFManager", "m_FocusPanel"};

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

} // namespace

std::string Command(std::string const& /*args*/) {
    ue3::Object* const panel = FocusPanel();
    if (panel == nullptr) {
        return "guimouse: no focused movie (open a screen)";
    }
    auto const x = VariableFloat(panel, L"_root._xmouse");
    auto const y = VariableFloat(panel, L"_root._ymouse");
    auto const w = VariableFloat(panel, L"Stage.width");
    auto const h = VariableFloat(panel, L"Stage.height");
    std::string out = std::format("guimouse: the movie's mouse ({:.1f}, {:.1f}) of a {:.0f} x {:.0f} stage", x.value_or(-1.0f),
        y.value_or(-1.0f), w.value_or(0.0f), h.value_or(0.0f));
    // The Windows cursor as the game sees it (GetCursorPos, answered by the host's pointer while it steers), in the foreground
    // window's client area, and the movie's mouse that would make.
    HWND const front = GetForegroundWindow();
    DWORD pid = 0;
    POINT cursor{};
    RECT client{};
    if (front != nullptr && GetWindowThreadProcessId(front, &pid) != 0 && pid == GetCurrentProcessId() && GetCursorPos(&cursor) &&
        ScreenToClient(front, &cursor) && GetClientRect(front, &client) && client.right > 0 && w && h) {
        float const scale = *w / static_cast<float>(client.right);
        out += std::format("; the cursor at ({}, {}) of the {}x{} client area would read ({:.1f}, {:.1f})", cursor.x, cursor.y, client.right,
            client.bottom, static_cast<float>(cursor.x) * scale, static_cast<float>(cursor.y) * scale - (*h - 720.0f) * 0.5f);
    } else {
        out += "; the game's window isn't in front";
    }
    return out;
}

} // namespace mevr::game::gui_cursor

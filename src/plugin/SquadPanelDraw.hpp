#pragma once

#include "imgui.h"

#include <string>

namespace mevr::vr::squad_menu {
struct View;
}

// The squad weapon menu's look, apart from where it's shown (plugin/SquadPanel puts it in a quad at the
// left hand; tools/squad-preview renders it to PNGs offline, so its look can be checked without a
// headset). Inside an ImGui frame of a context whose display is kWidth x kHeight.
namespace mevr::host::squad_panel {

inline constexpr unsigned kWidth = 1024;  // the canvas; the quad keeps its aspect
inline constexpr unsigned kHeight = 600;

// Draws the whole menu into `dl` (the background draw list), with `font` at any size.
void Draw(ImDrawList* dl, ImFont* font, vr::squad_menu::View const& view);
// Adds the menu's font to `io.Fonts`: Bahnschrift, else Segoe UI (Windows' own), else ImGui's
// default. `chosen` names it.
ImFont* LoadFont(ImGuiIO& io, std::string& chosen);

} // namespace mevr::host::squad_panel

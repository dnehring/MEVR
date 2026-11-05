// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/WorldUi.hpp"

#include <string>
#include <string_view>

// The squad member on the equipment, XMods and squad screens (research/56 section 7). The game draws them in its UI world
// (BioWorldInfo.m_UIWorld: a BioUIWorld, a world of its own with its camera actor, its lights and the pawns it spawns from the
// party), and renders that world instead of the level only while a screen is full screen, which the world UI clears to keep the
// level around the player. So a scene capture of ours (game/SceneCaptureRig, in the level) looks through the UI world's camera
// (its place, rotation and field of view, each tick) into a 16:9 render target, with the UI world's own post-process chain; the
// state tells the host which render target is ours and where both worlds are, the host renders that one capture with the UI
// world's scene instead of the level's (plugin/UiWorldFigure, in the capture loop's hook) and draws its picture behind the game's
// UI over the 16:9 stage, as the flat game shows it. [WorldUi] figure, figureOpacity. LE1, game thread.
namespace mevr::game::ui_world_figure {

// Each tick of the world UI with a screen of ours up: while it shows a squad member and [WorldUi] figure is on, our capture
// looks through the UI world's camera and `state` carries the figure's fields; otherwise the capture is off.
void Tick(vr::world_ui::Settings const& settings, vr::world_ui::Screen screen, vr::world_ui::State& state);
// No screen of ours: the capture off (kept for the next one).
void Off(char const* why);
// The mod unloads: the capture destroyed.
void Shutdown();
// `mevr uiworld`: the UI world, its camera, its post-process chain and our capture.
std::string Command(std::string_view args);
// Snapshot JSON (the world UI's "figure").
std::string StatusJson();

} // namespace mevr::game::ui_world_figure

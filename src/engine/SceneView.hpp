// SPDX-License-Identifier: MIT
#pragma once

namespace LESDK {
class Initializer;
}

// ULocalPlayer::CalcSceneView, where UE3 builds each frame's view. Inside it the engine asks the
// player controller for the view point (script GetPlayerViewPoint) and FOV (GetFOVAngle), then
// bakes them into the view matrices. ProcessEvent handlers that check InPrimaryView() therefore
// change only what is rendered: the game camera, aim and AI keep the unmodified view, and the
// engine still computes every derived matrix itself.
//
// Only the call from FViewportClient::Draw (the frame's player view) counts as primary; other
// callers (deprojection, scene captures) are left alone. Addresses: research/13.
namespace mevr::engine::scene_view {

// Game thread, at the start of each primary view, before the engine reads the view point.
using BeforePrimaryView = void (*)();

// False if this game has no known address or the address isn't a function start.
bool Install(::LESDK::Initializer& init, BeforePrimaryView before);
// False: the hook couldn't be removed, so this DLL must stay loaded.
bool Uninstall(::LESDK::Initializer& init);

// True on the game thread while the frame's player view is being built.
bool InPrimaryView();

// Calls from anywhere but the player view since the last LogStats (game thread).
unsigned OtherCalls();
// Logs and resets call counts: primary views, other callers.
void LogStats();

} // namespace mevr::engine::scene_view

#pragma once

// Aim with the right controller: the player's weapon fires along the controller's aim ray instead
// of the game camera's, by rewriting BioPlayerController.GetAdjustedAimFor's result (the
// weapon asks it for the aim of every shot). The ray comes into the world through
// game/view_anchor, so it works with our renderer and with the host's (Halcyon's). On foot
// only; the Mako turret keeps the game's aim. Without a view anchor or a tracked controller the
// game's aim stays (fail closed). The direction is the controller's; shots still start where the
// game starts them (the log reports how far that is from the ray once). No in-world reticle yet.
// Our renderer: on with each VR session. Host renderer: on once motion controllers are seen.
// `,` toggles either way.
namespace mevr::game::hand_aim {

// Finds the function and registers the call_function and process_event post-handlers (script
// calls it from script and natively). False until the script packages have loaded; call before
// either hook is installed.
bool TryStartup();
bool Toggle();
void SetEnabled(bool on);
bool Enabled();
void LogStats();
void Shutdown();

} // namespace mevr::game::hand_aim

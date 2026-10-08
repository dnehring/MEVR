#pragma once

// Aim with the right controller: the player's weapon fires along the controller's aim ray instead
// of the game camera's, by rewriting BioPlayerController.GetAdjustedAimFor's result (the
// weapon asks it for the aim of every shot). On foot only; the Mako turret keeps the game's
// aim. First step of gun handling; no in-world reticle yet. On with each VR session (`,` toggles).
namespace mevr::game::hand_aim {

// Finds the function and registers the call_function and process_event post-handlers (script
// calls it from script and natively). False until the script packages have loaded; call before
// either hook is installed.
bool TryStartup();
bool Toggle();
void SetEnabled(bool on);
void LogStats();
void Shutdown();

} // namespace mevr::game::hand_aim

#pragma once

// Research for VRIK: logs the player character's skeleton once per mesh (bone names, the
// component-space positions of the body's key bones, and the AnimTree's existing skeletal
// controls, e.g. any SkelControlLimb we could drive with the controllers instead of writing
// bones ourselves). Read-only; the results go to research/14.
namespace mevr::game::skeleton_probe {

// Finds the bone query functions. False until the script packages have loaded.
bool TryStartup();
// Game thread, about once per heartbeat: logs a mesh the first time it's seen.
void Tick();
void Shutdown();

} // namespace mevr::game::skeleton_probe

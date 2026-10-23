// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace LESDK {
class Initializer;
}

// The player's cover snapping (research/23). ME1 has no cover button: BioPawn's native tick tests for
// cover every CoverPlayerInterval (0.1 s) while the player is out of it (LE1 0xAB0FC0), and its test
// (0xAB1430, "TryAcquireCover" in our Ghidra names) takes cover by itself when the move input pushes
// within CoverTestAngle (20 degrees) into a surface CoverTestDistance (80 units) ahead and the camera
// faces it within CoverTestCameraAngle (60): it sets CoverType and bInCover, then fires the script
// event PlayerAcquiredCover (the controller goes to PlayerInCover, a third-person cover camera hooked
// to the CoverSlot actor). A pre-hook on that test refuses it for one pawn: no cover is ever taken, no
// state is written. The test's own cover trace (0xAA92F0, a pure query) still runs for that pawn, so
// we can say when an entry was refused.
//
// The hook runs on the game thread (the pawn's tick). LE1 only; no address for LE2/LE3.
namespace mevr::engine::cover_acquire {

// The hook goes in once (lazily, like engine/bone_pose): false if it couldn't (another game, not a
// function start, LEBinkProxy refused).
bool Install(::LESDK::Initializer& init);
// False: still live (or a call still inside it after waiting), so this module must stay loaded.
bool Uninstall(::LESDK::Initializer& init, bool waitForCalls);
bool Installed();

// The pawn whose cover tests are refused (null: none). Game thread.
void Refuse(void const* pawn);

// What the refused tests found since the last Take (game thread).
struct Stats {
    unsigned refused = 0;      // tests refused
    unsigned inReach = 0;      // ...of which the game's own trace found cover the move pushed into
    std::uint8_t lastType = 0; // the latest refused test's cover: 0 none (or not pushed into), 1 high, 2 low
    float lastAngle = -1.0f;   // the move's angle off the cover's normal there (degrees), -1 none
};
Stats Take();

} // namespace mevr::engine::cover_acquire

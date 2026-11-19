// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// A synthetic animation driver for tests (docs/trilogy-hair-plan.md, the hair and jiggle bench): any loaded animation played
// on a character's body the way a cutscene plays it (ME3's SFXStuntActor: its body's `MatineeAnim` node, the sequence's
// AnimSet added to the body's AnimSets, `SetAnim` and `PlayAnim`), optionally walking the character back and forth along its
// facing so the motion moves hair (and later jiggle) as a real walk would. Developer tool, off until a `mevr anim` command;
// everything it changed is put back by `mevr anim stop`, a level change or shutdown.
//
//   mevr anim list <text>                         live AnimSequences whose path holds the text (name, length, AnimSet)
//   mevr anim play <who> <sequence text> [rate] [walk m/s] [range m]
//                                                 <who>: part of a stunt actor's name or its hair/body mesh (ME3's Miranda:
//                                                 `miranda`); the first sequence whose path holds the text, looping
//   mevr anim clip <who> <clip> [rate] [walk m/s | auto] [range m] [turn deg]
//                                                 one of our generated clips (tools/motion: ardy-clip.py's
//                                                 <name>.mevrclip.json, by name) posing the body through engine/ClipPose;
//                                                 `auto` walks the character at the clip's own pace; turn: the
//                                                 walk's direction from the character's facing (90: across the view)
//   mevr anim at <seconds> | pause | resume      scrub, hold or go on
//   mevr anim stop                                the character's own animation sets and gestures back, the walk ended
//   mevr anim status
namespace mevr::game::anim_driver {

std::string Command(std::string const& args);
void Tick(double deltaSeconds);
std::string StatusJson();
void Shutdown();

} // namespace mevr::game::anim_driver

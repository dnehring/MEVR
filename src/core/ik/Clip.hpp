// SPDX-License-Identifier: MIT
#pragma once

#include "core/Json.hpp"
#include "core/ik/Rigid.hpp"

#include <optional>
#include <string>
#include <vector>

// Our generated animation clips (tools/motion/README.md, the `mevr-clip` format, version 1): loaded once, sampled per
// tick. Game-independent; the engine stage (engine/ClipPose) poses a body with a sample. Only the retarget's mapped bones
// (the clip's `retarget.map`, by name) and the root's position are played; every other bone keeps the game's animation.
namespace mevr::ik::clip {

struct Clip {
    std::string name;
    double fps = 20.0;
    int frames = 0;
    std::vector<std::string> bones;   // the mapped bones' names, in the order of each frame's `locals`
    struct Frame {
        Vec3 root;                     // the Root bone's position in component space, game units
        std::vector<Quat> locals;      // each mapped bone's local rotation (xyzw as the package stores it)
    };
    std::vector<Frame> clip;
    int loopStart = 0;                 // the loop played: [loopStart, loopEnd] frames (the whole clip without one)
    int loopEnd = 0;
    double TravelPerLoop() const;      // horizontal root travel over the loop, game units
    double LoopSeconds() const { return fps > 0.0 ? (loopEnd - loopStart) / fps : 0.0; }
};

// From the clip file's JSON. nullopt (and `error`) if it isn't a mevr-clip version 1 or is inconsistent.
std::optional<Clip> Load(json::Value const& file, std::string* error = nullptr);

struct Sample {
    // The root: x, y its offset from the loop's straight line of travel (the sway, the stride's surge; the travel itself is
    // the player's to add), z its height in component space (the bob).
    Vec3 root;
    std::vector<Quat> locals;   // per mapped bone
};
// The pose `seconds` into the loop (looping; rate already applied by the caller), frames slerped.
Sample At(Clip const& clip, double seconds);

} // namespace mevr::ik::clip

// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Clip.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace mevr::ue3 {
struct Object;
}

// Our generated clips played on a body (tools/motion/README.md): engine/BonePose's UpdateSkelPose hook calls AfterPose right
// after the engine composed the body's pose (and before the hair stage, which then reacts to it). The clip's mapped bones
// take the clip's local rotations, the root its height and sway; every other bone (face, fingers, twists, hair) keeps the
// game's animation, and the game's bone lengths stay. Matched by bone name per mesh; a body missing a mapped bone is
// refused. Game thread; inside SEH; journalled ("ClipPose.bones"). Every game. Mod side.
namespace mevr::engine::clip_pose {

// From the next pose update on; `rate` 1 is the clip's speed. Stop: the body is the game's again.
void Play(ue3::Object* body, std::shared_ptr<ik::clip::Clip const> clip, double rate);
void Stop();
// Scrub and hold: the clip's time set to `seconds` into its loop, and its rate (0 holds that pose).
void Seek(double seconds, double rate);
// Slow motion for footage (`mevr anim slowmo`): the clip's step time multiplied by `scale`.
void SetTimeScale(double scale);
bool Wants(ue3::Object const* component);
void AfterPose(ue3::Object* component, float deltaTime);

struct Stats {
    ue3::Object* body = nullptr;
    std::string clip;
    double seconds = 0.0;
    std::uint64_t calls = 0;
    std::uint64_t written = 0;
    char const* lastReject = nullptr;
    int mappedFound = 0;
    bool broken = false;
    ik::Vec3 rootBefore;   // the Root bone in component space as the engine posed it, last call
    ik::Vec3 rootAfter;    // ...and as the clip set it
    ik::Vec3 pelvisBefore; // the clip's first mapped bone (Pelvis) as the engine posed it
};
Stats GetStats();

} // namespace mevr::engine::clip_pose

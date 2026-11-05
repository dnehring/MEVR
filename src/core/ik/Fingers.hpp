// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Rigid.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>

// Fingers that follow the controllers (research/43): each finger joint turned about its flexion axis by a curl
// amount, on top of whatever the animation posed, with everything below the joint carried along. The flexion axes
// are measured from the skeleton's own pose, not assumed from a model axis: a finger bends toward the palm about
// an axis that lies in the palm plane across the finger, and the palm side is where the thumb's base sits (checked
// against the slight curl the rest pose already has). Poses are component-space bone transforms indexed like the
// skeleton, parents before children (UE3's RefSkeleton order). Engine-agnostic; tested offline.
namespace mevr::ik::fingers {

enum Finger { kThumb = 0, kIndex = 1, kMiddle = 2, kRing = 3, kPinky = 4, kFingerCount = 5 };
inline constexpr int kJointsPerFinger = 3;  // knuckle (the base bone), the middle joint, the tip
inline constexpr int kMaxBones = 1 + kFingerCount * kJointsPerFinger;  // the hand and every joint
inline constexpr char const* kFingerNames[kFingerCount] = {"thumb", "index", "middle", "ring", "pinky"};

// One finger's joints, knuckle first (-1: not found); `count` is how many lead the chain without a gap.
struct Chain {
    std::array<int, kJointsPerFinger> joints{-1, -1, -1};
    int count = 0;
};

// A hand's finger bones. Plain data, so a request can carry it across threads by copy.
struct HandBones {
    int hand = -1;
    std::array<Chain, kFingerCount> fingers{};
    // How many fingers have at least a knuckle.
    int Found() const;
    // The hand, then every joint found, in that order: the bones a writer checks and the names it keeps.
    int All(std::array<int, kMaxBones>& out) const;
};

// Finds the finger bones under `hand` by name: a token naming the finger (thumb, index, middle, ring, pinky / pink /
// pinkie / little) with a digit token for the joint (LE1: RightIndexFinger, RightIndexFinger1, RightIndexFinger2;
// none for the knuckle, 1 and 2 for the joints), an ancestor chain reaching `hand`, each joint the child of the one
// before it. End, nub and tip bones are not joints (but are carried as children). Empty (Found() == 0) if nothing fits.
HandBones FindHand(std::span<std::string const> names, std::span<int const> parents, int hand);

// The flexion axes in the hand bone's frame (unit vectors): a positive turn about `axis[f]` through a joint of finger
// f curls that finger toward the palm. `palm` is the palm's normal pointing out of the palm (the side the fingers
// close toward), in the hand's frame.
struct HandAxes {
    std::array<Vec3, kFingerCount> axis{};
    std::array<bool, kFingerCount> valid{};
    Vec3 palm;
    bool palmFromThumb = false;  // the palm side came from the thumb's base (else from the rest pose's curl)
    int restCurlAgree = 0;       // fingers whose rest curl bends toward `palm`...
    int restCurlDisagree = 0;    // ...and away from it (a diagnostic; the thumb decides when present)
};

// Measures the axes from a pose (the animation's, before any curl): the knuckle line (index to pinky) and the
// middle finger's direction span the palm plane; the thumb's base, or failing that the rest pose's own curl, picks
// the palm side; each finger's axis is its direction crossed with the palm normal; the thumb's turns it toward the
// middle finger's first joint. Needs the index, middle and pinky knuckles and the middle finger's first joint.
// Nullopt if they don't span a plane or the palm side can't be told.
std::optional<HandAxes> MeasureAxes(std::span<Transform const> pose, HandBones const& bones);

// How far each finger curls, -1..1: 1 is `fullCurlDegrees` at every joint toward the palm, negative extends
// (an index finger lifted off a trigger). The thumb's turn is scaled by kThumbScale.
struct Curl {
    std::array<double, kFingerCount> finger{};
};
inline constexpr double kThumbScale = 0.35;

// Turns each finger's joints by curl x degrees about their flexion axes (through each joint's own origin, so the
// knuckle stays put), carrying every bone below each joint (chain or not), in parent-first order. Writes into
// `pose`; returns how many bones changed (0 for an all-zero curl or bad input: nothing written). `parents` is the
// skeleton's; a joint whose parent isn't the previous joint (or the hand, for the knuckle) ends its chain.
int Apply(std::span<Transform> pose, std::span<int const> parents, HandBones const& bones, HandAxes const& axes,
    Curl const& curl, double fullCurlDegrees);

// The rotation by `degrees` about the unit `axis` (right-handed in components, as Rotate applies it).
Quat AboutAxis(Vec3 axis, double degrees);

// One controller's inputs for its hand's curls (research/43, "Touch sensors").
struct Controller {
    double trigger = 0.0;  // 0..1
    double squeeze = 0.0;  // the grip, 0..1
    // The capacitive sensors as weights, 0 (off) to 1 (touching), eased by EaseTouch; negative where the controller has
    // no such sensor (or they're switched off): that finger then follows the trigger or the grip alone.
    double triggerTouch = -1.0;  // a finger on the trigger
    double thumbTouch = -1.0;    // the thumb on the stick, a face button, the thumb rest or the trackpad
};

// What a hand is doing.
enum class Rule {
    Free,  // empty: the index follows the trigger, the other three the grip, the thumb its rest
    Gun,   // holding the gun (the animation's grip): only the index, off the trigger or on it
};

// The curls the rule gives.
//   Free: index = the trigger, or with the trigger's sensor kIndexPoint (lifted: pointing) easing to kIndexOnTrigger
//         (resting on it) and from there to 1 with the pull; middle, ring, pinky = the grip; thumb = the grip, or with
//         its sensor kThumbUp (lifted: an open hand, or a thumbs-up in a fist) easing to at least kThumbOnRest.
//   Gun:  index = -kIndexOffTrigger (along the frame) easing to 0 (the animation's finger on the trigger) as the trigger
//         is touched, or without a sensor as it is pulled; the others 0 (as animated).
// A pressed trigger counts as touched (a pull of 0.05 is a full touch), whatever its sensor says.
Curl FromController(Rule rule, Controller const& controller);
inline constexpr double kIndexPoint = -0.15;
inline constexpr double kIndexOnTrigger = 0.25;
inline constexpr double kIndexOffTrigger = 0.5;
inline constexpr double kThumbUp = -0.8;
inline constexpr double kThumbOnRest = 0.5;

// A touch weight moved toward `touching` (1) or not (0) by `seconds` / kTouchBlendS, so a finger lifting or landing
// takes kTouchBlendS instead of a frame. A negative weight (no sensor until now) starts where the touch is.
double EaseTouch(double weight, bool touching, double seconds);
inline constexpr double kTouchBlendS = 0.08;

} // namespace mevr::ik::fingers

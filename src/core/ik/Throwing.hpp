#pragma once

#include "core/ik/Ik.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// Physical grenades and melee (research/20): pure motion math and state machines, testable offline;
// game/throwables ties them to ME1. Tracking-space quantities are OpenXR LOCAL (meters; +X right,
// +Y up, -Z forward) unless a function says it works in the world.
//
// Conventions from the sources in research/20 (our own code, nothing copied):
//   - release velocity: the fastest moment of the last ~120 ms before the release, smoothed over a
//     few samples around it (SteamVR's AdvancedEstimation, HIGGS, Quake VR), not a plain average,
//     which blends in the wind-up (the Rescuties write-up); each velocity is a least-squares slope,
//     which shrugs off one jittery sample;
//   - the grip lets go early when the analog squeeze falls well below its peak while the hand moves
//     fast (Quake VR: the runtime's grip button lets go late);
//   - a release slower than a minimum speed is no throw (HIGGS 1.0 m/s, Quake VR 1.5 m/s): the grenade
//     goes back;
//   - a melee stroke needs speed and useful travel in one direction, measured relative to the head so
//     walking and turning add nothing (Quake VR, Overlord's melee_motion), fires once per stroke, and
//     has a cooldown (PLANCK 0.55 s, Overlord 0.4 s).
namespace mevr::ik::throwing {

// ---- Sample times ------------------------------------------------------------------------------
// Controller samples arrive with a sequence number (one per rendered frame) but no precise time of
// their own. The clock turns sequence steps into evenly spaced times (the frame period, learned from
// when new sequences are seen), slowly pulled toward the observed clock so it can't drift away.
class SampleClock {
public:
    // A new observation: the sample's sequence and the observer's clock (seconds). The sample's time,
    // or nullopt if the sequence didn't advance (the same sample seen again).
    std::optional<double> Update(std::uint64_t sequence, double observedSeconds);
    double Period() const { return period_; }
    void Reset() { have_ = false; }

private:
    bool have_ = false;
    std::uint64_t sequence_ = 0;
    double observed_ = 0.0;
    double time_ = 0.0;
    double period_ = 1.0 / 90.0;
};

// ---- Motion history and velocity ---------------------------------------------------------------
struct Sample {
    double t = 0.0;
    Vec3 p;
};

class MotionHistory {
public:
    static constexpr int kCapacity = 48;
    // A sample breaks the history (it starts over from it) when it comes this long after the last one,
    // or moves this far in one step (a tracking jump, a recenter).
    static constexpr double kMaxGapSeconds = 0.15;
    static constexpr double kMaxStepMeters = 0.5;

    // Adds a sample; one not later than the newest is ignored. True if it broke the history.
    bool Add(double t, Vec3 p);
    void Reset() { count_ = 0; }
    int Count() const { return count_; }
    Sample const& At(int i) const;  // 0 = the oldest
    Sample const& Newest() const { return At(count_ - 1); }
    // The least-squares velocity (m/s) of the samples with t in [t0, t1]; nullopt with fewer than two
    // or no time spread.
    std::optional<Vec3> FitVelocity(double t0, double t1) const;

private:
    std::array<Sample, kCapacity> samples_{};
    int start_ = 0;
    int count_ = 0;
};

struct Release {
    bool valid = false;
    Vec3 velocity;           // m/s
    double speed = 0.0;      // |velocity|
    double peakAgo = 0.0;    // how long before `now` the fastest moment was (s)
    int samples = 0;         // samples in the window
};

// The throw velocity at `now`: the fastest moment within `window` seconds before it (each moment's
// velocity a slope over +-`halfWidth`), its velocity a slope over +-`smoothing` around it.
Release EstimateRelease(MotionHistory const& history, double now, double window = 0.12, double halfWidth = 0.02,
    double smoothing = 0.03);

// ---- The grip, with an analog early release ----------------------------------------------------
struct GripParams {
    float on = 0.6f;           // squeezed at or above (the pad mapping's latch)
    float off = 0.4f;          // let go at or below
    float earlyDrop = 0.3f;    // ...or this far below the squeeze's peak while the hand moves fast
    double earlySpeed = 1.5;   // m/s: "fast"
};
struct GripLatch {
    bool down = false;
    float peak = 0.0f;
};
// One step. True while the grip counts as held; it lets go at `off`, or early (see GripParams).
bool UpdateGrip(GripLatch& latch, float squeeze, double handSpeed, GripParams const& params = {});

// ---- The grenade in a hand ---------------------------------------------------------------------
inline constexpr int kLeft = 0;
inline constexpr int kRight = 1;

struct HandInput {
    bool tracked = false;
    Vec3 position;            // the grip pose (tracking space)
    double distance = -1.0;   // from the grenade slot's centre (m); negative: no slot
    bool newSqueeze = false;  // a squeeze began this step that may take a grenade (not the support grip)
    float squeeze = 0.0f;     // analog grip
    bool allowed = true;      // this hand may take grenades (setting; the right hand while it holds the gun...)
};

struct Input {
    double now = 0.0;  // seconds; the time of the newest samples
    std::array<HandInput, 2> hands;
    double radius = 0.10;
    double exitMargin = 0.05;
    bool canGrab = true;          // grenades left, combat on, nothing in the way: else a grab is refused
    double throwSpeedMin = 1.5;   // m/s at the release: slower is no throw
    GripParams grip;
};

enum class Event {
    None,
    Grabbed,    // `hand` took a grenade
    Refused,    // a squeeze in the slot, but canGrab was false
    Thrown,     // let go fast enough: `release`
    Cancelled,  // let go too slowly: the grenade goes back
    Dropped,    // tracking lost while held: it goes back (no throw)
};
char const* Name(Event event);

struct Step {
    Event event = Event::None;
    int hand = -1;
    Release release;
    std::array<int, 2> entered{0, 0};  // 1: the hand entered the slot this step
    std::array<int, 2> left{0, 0};     // 1: it left
};

class GrenadeMachine {
public:
    // Samples are added for each hand whose position is new (`now` later than its newest sample).
    Step Update(Input const& input);
    int Holder() const { return holder_; }  // the hand holding a grenade, or -1
    bool InSlot(int hand) const { return inSlot_[hand]; }
    MotionHistory const& History(int hand) const { return history_[hand]; }
    double HandSpeed(int hand, double now) const;  // m/s over the last 40 ms (0: unknown)
    void Reset();

private:
    std::array<MotionHistory, 2> history_{};
    std::array<bool, 2> inSlot_{false, false};
    std::array<GripLatch, 2> grip_{};
    int holder_ = -1;
};

// ---- Melee: a fast stroke of a hand ------------------------------------------------------------
struct SwingParams {
    double minSpeed = 3.0;    // m/s, relative to the head
    double minTravel = 0.12;  // m of useful displacement in one direction while that fast
    double forwardMin = 0.0;  // the stroke's direction against the body's forward (cosine): 0 = not backward
    double cooldown = 0.8;    // s between strikes (both hands share it in the game; per detector here)
    double maxSpeed = 12.0;   // m/s: faster is a tracking glitch, not a hand
    double fitSeconds = 0.04;  // the velocity's slope window
};

struct SwingInput {
    double now = 0.0;
    bool tracked = false;
    Vec3 position;     // the hand relative to the head (tracking axes)
    Vec3 bodyForward{0.0, 0.0, -1.0};  // unit, horizontal
    bool suppressed = false;  // a grenade in this hand, firing, a draw just now...
};

struct SwingStep {
    bool strike = false;
    double speed = 0.0;
    double travel = 0.0;
    Vec3 direction;            // unit, of the stroke
    double forward = 0.0;      // its cosine against the body's forward
    // Why a stroke fast and long enough didn't strike (nullptr: it did, or there was none).
    char const* refused = nullptr;
};

class SwingDetector {
public:
    SwingStep Update(SwingInput const& input, SwingParams const& params);
    void Reset();
    double PeakSpeed() const { return peakSpeed_; }  // since the last ResetPeak (diagnostics)
    void ResetPeak() { peakSpeed_ = 0.0; }

private:
    MotionHistory history_;
    bool stroke_ = false;      // a stroke above the speed is running
    Vec3 strokeStart_;
    Vec3 strokeDirection_;
    bool spent_ = false;       // this stroke already struck or was refused
    double readyAt_ = 0.0;     // cooldown
    double peakSpeed_ = 0.0;
};

// ---- Where a throw lands -----------------------------------------------------------------------
// Points along a thrown object's path in the WORLD (Unreal axes: +Z up; units), every `dt` seconds
// from `start` at `velocity` under gravity `gravityZ` (negative), until `maxSeconds` or until it has
// fallen `maxDrop` below the start. The game traces between consecutive points.
std::vector<Vec3> ArcPoints(Vec3 start, Vec3 velocity, double gravityZ, double dt = 0.05, double maxSeconds = 4.0,
    double maxDrop = 5000.0);

// ---- Haptics -----------------------------------------------------------------------------------
struct Buzz {
    float amplitude = 0.0f;
    float frequency = 0.0f;
    float seconds = 0.0f;
};
Buzz GrabClick(double strength);    // a grenade comes into the hand
Buzz HoldBuzz(double strength);     // while it's held (continuous; faint)
Buzz ThrowPulse(double strength);   // it leaves the hand
Buzz StowClick(double strength);    // it goes back (a slow release)
Buzz RefusedBuzz(double strength);  // none left, or not now
Buzz StrikeTick(double strength);   // a melee starts
Buzz HitThump(double strength);     // the melee hurt someone

} // namespace mevr::ik::throwing

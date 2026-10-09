#pragma once

#include <cmath>

// Engine-agnostic body IK math for VR (VRIK): where the body faces, where the shoulders are, and
// where each elbow goes for a hand target. Pure functions on plain vectors, in OpenXR LOCAL
// space (meters; +X right, +Y up, -Z forward), so they're testable offline. A game adapter
// converts the results to its skeleton (bone names, axes, units). Our own implementation of the
// standard techniques.
namespace mevr::ik {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
// Unit vector, or `fallback` when `a` is (nearly) zero.
Vec3 Normalize(Vec3 a, Vec3 fallback = {0.0, 0.0, -1.0});

// Body heading (yaw about +Y, radians; 0 = -Z, positive = turned left) that follows the head
// only once the head turns past a deadzone, so looking around doesn't twist the torso, and
// slowly re-centers under the head while the head holds still.
class BodyYaw {
public:
    explicit BodyYaw(double deadzoneRadians = 0.6, double recenterPerSecond = 0.5)
        : deadzone_(deadzoneRadians), recenterRate_(recenterPerSecond) {}
    double Update(double headYaw, double deltaSeconds);
    double Value() const { return yaw_; }
    void Reset(double yaw) { yaw_ = yaw; initialized_ = true; }

private:
    double deadzone_;
    double recenterRate_;
    double yaw_ = 0.0;
    bool initialized_ = false;
};

// `a` turned toward `b` by `weight` (0: a, 1: b) the short way round; radians, wrapped to (-pi, pi].
double BlendYaw(double a, double b, double weight);

// Yaw (as BodyYaw uses it) of an OpenXR orientation's forward direction, ignoring pitch/roll.
double YawOf(double qx, double qy, double qz, double qw);

// Rough upper-body skeleton from the head and body heading. Proportions are for an average
// adult; a calibration step can scale them later.
struct BodyProportions {
    double neckDown = 0.12;        // head center to neck base
    double neckBack = 0.08;        // head center is ahead of the neck
    double shoulderDown = 0.06;    // neck base to the shoulder line
    double shoulderHalfWidth = 0.18;
    double upperArm = 0.30;
    double forearm = 0.28;         // elbow to the grip point
};

struct UpperBody {
    Vec3 neck;
    Vec3 leftShoulder;
    Vec3 rightShoulder;
    Vec3 forward;  // body forward (horizontal, unit)
    Vec3 right;    // body right (horizontal, unit)
};

UpperBody EstimateUpperBody(Vec3 headPosition, double bodyYaw, BodyProportions const& body = {});

// Two-bone IK (shoulder -> elbow -> hand). Places the elbow in the plane through root, target
// and `pole` (a point the elbow should bend toward), keeping both bone lengths exact. A target
// out of reach gets a straight arm pointing at it.
struct TwoBoneResult {
    Vec3 mid;   // elbow
    Vec3 end;   // hand (the target, or the reachable point toward it)
    bool reached = false;
};

TwoBoneResult SolveTwoBone(Vec3 root, Vec3 target, double upper, double lower, Vec3 pole);

// A natural elbow hint for one arm: below the shoulder, out to the side and slightly back.
// `side` is -1 for the left arm, +1 for the right.
Vec3 ElbowPole(UpperBody const& body, int side);

} // namespace mevr::ik

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// Comfort options (research/31): snap or smooth turning by the mod, the comfort vignette, and walking toward the
// left hand. The game-independent parts, unit-tested in tests/Main.cpp: the snap's state machine, the smooth
// turn's rate, yaw units with their rounding remainder, where the game camera will be after a turn (so arm IK
// can pose the body for the frame that shows it), the vignette's fade and picture, and the stick rotation for
// hand-relative walking.
namespace mevr::vr::comfort {

// [Comfort] turn: who turns the player with the right stick on foot.
enum class TurnStyle : std::uint8_t {
    Game,    // the game's own turn (its per-camera-mode rate, acceleration and dead zone): the default, the menu's Smooth
    Smooth,  // the mod turns at turnSpeed degrees a second at full deflection; file only
    Snap,    // the mod turns by snapDegrees per flick
};
char const* Name(TurnStyle style);
std::optional<TurnStyle> ParseTurn(std::string_view text);

// [Comfort] vignetteWhen.
enum class VignetteWhen : std::uint8_t {
    Turning,           // while the stick turns the player (and a moment after each snap)
    TurningAndMoving,  // ...and while the left stick moves him
};
char const* Name(VignetteWhen when);
std::optional<VignetteWhen> ParseVignetteWhen(std::string_view text);

// [Comfort] walk: what the left stick's forward means on foot.
enum class WalkToward : std::uint8_t {
    Head,      // where the player looks (Halcyon's moveFollowsHead)
    LeftHand,  // where the left controller points, flat on the floor
};
char const* Name(WalkToward walk);
std::optional<WalkToward> ParseWalk(std::string_view text);

struct Settings {
    TurnStyle turn = TurnStyle::Game;
    float snapDegrees = 45.0f;      // per flick (the menu offers 30, 45 and 60)
    float turnSpeed = 120.0f;       // smooth: degrees a second at full deflection
    float deadzone = 0.15f;         // smooth: a stick inside this doesn't turn (and doesn't count as turning)
    float snapThreshold = 0.7f;     // snap: a flick past this turns once...
    float snapRearm = 0.3f;         // ...and the next needs the stick back inside this first
    float snapCooldown = 0.25f;     // ...and at least this many seconds
    bool vignette = false;
    float vignetteStrength = 0.6f;  // 0..1: how dark the edges get
    VignetteWhen vignetteWhen = VignetteWhen::Turning;
    float moveThreshold = 0.2f;     // the left stick past this moves the player (the vignette's "moving")
    WalkToward walk = WalkToward::Head;
};
// Clamped to sane ranges (whatever a file says).
Settings Sanitize(Settings settings);

// ---- Turning ----------------------------------------------------------------------------------------------------

// Snap turning: one turn per flick of the right stick, then nothing until the stick has come back near the
// centre and the cooldown has passed. A fresh state (and Reset) starts disarmed, so a stick already held when the
// mod takes the turn over doesn't snap until it has been let go.
struct SnapTurn {
    bool armed = false;
    double readyAt = 0.0;  // seconds (the caller's clock)
    void Reset() { armed = false; }
};
// One step with the stick's X (-1..1, + right) at `now` seconds. The turn in degrees (+ right), 0 for none.
float StepSnap(SnapTurn& snap, float stickX, double now, Settings const& settings);

// Smooth turning: the degrees (+ right) for this step of `seconds`: linear past the dead zone, full speed at
// full deflection.
double SmoothTurnDegrees(float stickX, double seconds, Settings const& settings);
// Whether the stick turns at all (smooth: past the dead zone).
bool StickTurns(float stickX, Settings const& settings);

// Degrees to Unreal yaw units (65536 a turn, + right), keeping the rounding remainder for the next call, so a slow
// smooth turn isn't lost to rounding.
struct YawUnits {
    double remainder = 0.0;  // units
    int Take(double degrees);
    void Reset() { remainder = 0.0; }
};

// The game camera after the player controller (and, if it turned too, the pawn) turned `units`: the yaw plus the
// units, and with `pawnTurned` the location turned about the pawn's (x, y) as well (the first-person camera hangs
// off Shepard's mesh). Unreal coordinates (+ yaw turns X toward Y).
struct CameraGuess {
    double x = 0.0;
    double y = 0.0;
    int yaw = 0;
};
CameraGuess TurnCamera(double x, double y, int yaw, int units, bool pawnTurned, double pivotX, double pivotY);

// ---- The comfort vignette ---------------------------------------------------------------------------------------

inline constexpr float kVignetteFadeSeconds = 0.15f;
inline constexpr double kSnapPulseSeconds = 0.2;  // after a snap, the vignette is wanted this long

// The host's fade: toward `target` (0..1) at 1 / kVignetteFadeSeconds per second; `seconds` is clamped to 0.1.
struct VignetteFade {
    float value = 0.0f;
    float Step(float target, float seconds, float fadeSeconds = kVignetteFadeSeconds);
};

// What the mod asks the host for, each tick: strength while the player turns (or moves, with TurningAndMoving),
// and for kSnapPulseSeconds after each snap; 0 with the vignette off.
struct VignetteDrive {
    double pulseUntil = -1e9;
    float Target(bool turning, bool moving, bool snapped, double now, Settings const& settings);
};

// The picture: premultiplied black whose alpha rises with the angle from the view's centre, from 0 inside
// `innerDegrees` to 1 past `outerDegrees` (smoothstep). For a quad 1 m ahead, `halfWidth` x `halfHeight` meters
// from its centre to its edges. Row-major, top row first; alpha only (0..255).
inline constexpr float kVignetteInnerDegrees = 30.0f;
inline constexpr float kVignetteOuterDegrees = 58.0f;
std::vector<std::uint8_t> VignetteMask(int width, int height, float halfWidth, float halfHeight,
    float innerDegrees = kVignetteInnerDegrees, float outerDegrees = kVignetteOuterDegrees);
// RGBA8 pixels (premultiplied black: only the alpha byte is nonzero) at `strength` (0..1) of the mask.
void VignettePixels(std::vector<std::uint8_t> const& mask, float strength, std::vector<std::uint32_t>& pixels);
// The quad's half extents (meters at 1 m) that cover both eyes' fields of view (radians: left, right, up, down per
// eye, as OpenXR's XrFovf), with a margin, each angle clamped to 75 degrees.
struct HalfExtents {
    float width = 1.0f;
    float height = 1.0f;
};
HalfExtents CoverFov(float const fov[2][4]);

// ---- Walking toward the left hand -------------------------------------------------------------------------------

// The yaw (radians, + to the left, 0 = tracking -Z) of a direction (OpenXR: +Y up, -Z ahead) laid flat on the
// floor; nullopt when it points within about 75 degrees of straight up or down (its flat direction is noise).
std::optional<double> FlatYawLeft(float x, float y, float z);
// The forward (-Z) of an orientation (x, y, z, w).
void Forward(float qx, float qy, float qz, float qw, float& x, float& y, float& z);
// How far to turn the left stick (Unreal units, + right, as gamepad::RotateStick) so that after Halcyon turns it by
// its head look yaw (`headLookUnits`, + left, Halcyon's RenderHook::HeadLookYawUU; 0 when moveFollowsHead is off)
// its forward is the hand's yaw.
int HandWalkUnits(double handYawLeft, int headLookUnits);

} // namespace mevr::vr::comfort

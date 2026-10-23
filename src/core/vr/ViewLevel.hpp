#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// Keeping the game camera level while the hand aims (research/22). LE1's guns kick the player
// controller's rotation (ControlRotation) up with every shot (BioPlayerInput.UpdateViewRotation adds the
// recoil, SFXInventoryManager's natives), and the assault rifles' kick never fades (RecoilFadeSpeed 0 in
// BIOWeapon.ini): a flat-screen player pulls the stick down to compensate. In VR the stick's pitch is
// off (Halcyon's decoupledPitch) and the hand aims, so nothing brings it back: the camera Halcyon renders
// from tilts up shot by shot, and the head-tracked world with it. The hold puts ControlRotation's pitch
// and roll back to level (0) after each controller tick, before the camera reads it.
//
// Angles are Unreal rotator units (65536 = 360 degrees). Pure; unit-tested in tests/Main.cpp.
namespace mevr::vr::view_level {

inline constexpr int kUnitsPerTurn = 65536;

// A rotator component as a signed angle, in [-32768, 32767].
int Signed(int units);
double Degrees(int units);
int Units(double degrees);

// Why the hold is (or isn't) applied this tick.
enum class Why : std::uint8_t {
    Holding,     // level: pitch and roll put back to 0
    Off,         // [Hands] levelCamera = 0
    NoView,      // no head-tracked world view (menu, flat screen, VR off)
    HeadAim,     // the host's head aim writes ControlRotation itself: never fought
    NoHandAim,   // the right controller doesn't aim (the game's own aim needs the camera's pitch)
    NotOnFoot,   // the Mako, conversations, cutscenes, menus, wheels
    NoPawn,
    Cover,       // in cover (blind fire and the cover cameras pitch)
    Ladder,
    CameraMode,  // a camera mode that keeps the game's pitch (the sniper scope, cover, cinematic...)
};
char const* Name(Why why);

struct Inputs {
    bool enabled = true;           // [Hands] levelCamera
    bool headTrackedView = false;  // a head-tracked world view is on (the host's view, valid)
    bool headAim = false;          // the host's head aim drives ControlRotation
    bool handAim = false;          // the right controller aims, and the host knows
    bool onFoot = false;           // game mode 0
    bool pawn = false;             // the controller has a pawn
    bool cover = false;            // the pawn's CoverType or CoverAction is set
    bool ladder = false;           // the pawn's physics is PHYS_Ladder
    bool cameraModeAllowed = false;  // CameraModeAllows(the current camera mode, or its blend target)
};
Why Decide(Inputs const& in);

// Camera modes the hold applies in, by LE1 class name (SFXGame.SFXCameraMode_*): Explore, Combat,
// TightAim (aimed fire) and the sprint modes (ExploreStorm, CombatStorm). Cover (EnterCover,
// HipAimCover), the sniper scope (SniperAim), the Mako (Vehicle*), photo and cinematic modes keep the
// game's pitch. Interpolate isn't a mode of its own: callers pass the mode it blends to.
bool CameraModeAllows(std::string_view className);

// The correction that levels a rotator component: what to add (signed) so it becomes 0.
int Correction(int units);

// Rolling statistics in one-second buckets: the controller's rotation as the game left it each tick
// (before the hold), what the game changed and what the hold put back, and the shots.
struct Summary {
    bool any = false;          // at least one tick in the window
    double seconds = 0.0;      // the window's span with data
    int minPitch = 0;          // signed units, as the game left it (before the hold)
    int maxPitch = 0;
    int minRoll = 0;
    int maxRoll = 0;
    long long gameDrift = 0;   // the game's own pitch changes, summed (units): the drift without the hold
    long long corrected = 0;   // the hold's corrections, summed (units)
    long long recoil = 0;      // the pitch kicks the shots queued (units)
    int shots = 0;
    int frames = 0;
    int heldFrames = 0;
    int zoomSnaps = 0;         // zoom snaps (aim assist) kept from turning the camera
};

class Window {
public:
    // One controller tick at `now` (seconds, monotonic): pitch and roll as the game left them, the
    // game's pitch change since the tick before (after that tick's hold), and this tick's correction.
    void Frame(double now, int pitch, int roll, int gameDelta, int correction, bool held);
    void Shot(double now, int kick);
    void ZoomSnap(double now);
    // The last `seconds` (at most the window: 60 s).
    Summary Last(double now, double seconds = 60.0) const;
    void Clear();

private:
    struct Bucket {
        long long second = -1;  // which second (floor(now)) it holds; -1 empty
        bool any = false;
        int minPitch = 0;
        int maxPitch = 0;
        int minRoll = 0;
        int maxRoll = 0;
        long long gameDrift = 0;
        long long corrected = 0;
        long long recoil = 0;
        int shots = 0;
        int frames = 0;
        int heldFrames = 0;
        int zoomSnaps = 0;
    };
    Bucket& At(double now);
    std::array<Bucket, 64> buckets_{};
};

// When to write a log line about the rotation: once the game's pitch has moved `threshold` units since
// the last line (summed, so it also counts what the hold put back), or the roll has, at most every
// `minSeconds`.
class ChangeLog {
public:
    ChangeLog(int thresholdUnits, double minSeconds) : threshold_(thresholdUnits), minSeconds_(minSeconds) {}
    // Each tick: the game's pitch change and the roll as the game left it. True when a line is due.
    bool Feed(double now, int gameDelta, int roll);
    void Shot(int kick);
    // What the due line reports (since the last line), then starts the next one.
    struct Line {
        long long pitch = 0;  // the game's pitch change since the last line (units)
        int rollFrom = 0;
        int rollTo = 0;
        int shots = 0;
        long long recoil = 0;
        double seconds = 0.0;
    };
    Line Take(double now);

private:
    int threshold_;
    double minSeconds_;
    bool started_ = false;
    double lineAt_ = 0.0;
    long long pitch_ = 0;
    int rollAtLine_ = 0;
    int roll_ = 0;
    int shots_ = 0;
    long long recoil_ = 0;
};

// The recoil, felt in the hand instead of the view: a controller pulse for one shot's kick (degrees of
// pitch the game queued), scaled by [Hands] recoilHaptic (0..1; 0 = none).
struct Pulse {
    float seconds = 0.0f;
    float amplitude = 0.0f;  // 0: none
};
Pulse RecoilPulse(double kickDegrees, double scale);

} // namespace mevr::vr::view_level

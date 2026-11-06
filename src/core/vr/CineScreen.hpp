// SPDX-License-Identifier: MIT
#pragma once

// Conversations and cutscenes on a 3D cinema screen (research/52): instead of the director's camera filling the view
// (Halcyon's VR cine) or one flat image (its flat screen), both Stereo 2 passes render the shot as the game frames it (its
// native projection), from two cameras side by side on the director's camera, and each eye sees its own render on the
// same screen fixed in the room. The camera moves stay inside the frame; the picture has depth.
//
// Depth is set the way stereo films set it. The two cameras are parallel, so the scene at infinity lands on the screen
// plane and everything nearer would come out of it. Each eye therefore drops a sliver of its image on opposite sides (the
// left eye its left edge, the right eye its right edge: horizontal image translation), which pushes the whole picture back
// so infinity sits behind the screen with an on-screen disparity of `depth` (a fraction of the screen's width), and the
// cameras' distance is chosen per frame from the shot's own field of view so that the scene `convergeM` away lands on the
// screen plane: a close-up's narrow lens gets a narrow baseline, so cuts don't change the depth budget. Infinity's
// disparity is capped below the viewer's eye distance, so the eyes never diverge.
//
// Engine-agnostic and unit-tested (tests/Main.cpp TestCineScreen); the host's side is src/plugin/CineScreen.cpp and
// Halcyon's [MEVR] cine-screen seams in xr_session.cpp.
namespace mevr::vr::cine_screen {

// ---- Settings ([Cinematics] in ASI\LE1MEVR.ini, read by the host) ---------------------------------------------------
struct Settings {
    bool conversations = true;   // conversations: on the 3D screen while Halcyon's VR conversations are on
    bool cutscenes = true;       // cutscenes: the same for cutscenes (prerendered movies stay on the flat screen)
    bool frontEnd = true;        // frontEnd: the start-up's front end (the title, its main menu) on the 3D screen, whatever
                                 // `cutscenes` says; 0: the flat panel (research/56)
    // The owner's first headset trial (PS VR2, 59 mm eyes, 2026-11-06) of the fake-headset defaults (3.0 m wide at 2.6 m, 60
    // degrees; depth 1.5%: the far scene 0.99 degrees behind, its two images 77% of the eye distance apart): "a little
    // cross-eyed", the characters "physically large", "maybe even a little bit farther out". Now 45 degrees across, the angle
    // the game renders a typical shot at (people at the shot's own size), and the far scene 0.47 degrees behind.
    float distanceM = 3.5f;      // distanceM: how far ahead the screen stands when a scene starts
    float widthM = 2.9f;         // widthM: the screen's width
    float depth = 0.010f;        // depth: infinity's disparity, a fraction of the screen's width (0 = flat, at the screen)
    float convergeM = 1.2f;      // convergeM: the scene distance that lands on the screen plane (metres of the scene). LE1's
                                 // close-ups put the camera ~0.85 m from a face: at 2.5 m one came 1.9 degrees out of the
                                 // screen, at 1.2 m 0.5 (research/52, measured)
    float aspect = 16.0f / 9.0f; // aspect: the screen's shape (width / height): the eye image's middle band of that shape, the
                                 // director's 16:9 frame (UE3's lens is horizontal: a taller render only adds sky and floor);
                                 // 0 = the whole, nearly square image (the owner in the PS VR2, 2026-11-06: "more cinematic")
    bool fullRate = true;        // fullRate: the screen at the headset's own rate; 0: Halcyon's cinematic cap (cineFpsCap: half the
                                 // display rate, chosen for full-VR cutscenes on a 120 Hz headset, where half is the 60 the
                                 // cutscenes were made for; at 90 Hz it is 45, every picture shown twice, and the hold read as
                                 // ~30 ms of CPU frame time: the owner, 2026-11-06, "the cpu timing explodes")
    bool backdrop = true;        // backdrop: a floor and a horizon around the screen (plugin/CinemaBackdrop; the owner: "a better
                                 // sense of location so I don't get tunnel visioned and partially motion sick"); 0: the dark
    float backdropBrightness = 1.0f;  // backdropBrightness: 1 the default dimness, 0..3
};

inline constexpr float kMinDistanceM = 1.0f;
inline constexpr float kMaxDistanceM = 10.0f;
inline constexpr float kMinWidthM = 1.0f;
inline constexpr float kMaxWidthM = 12.0f;
inline constexpr float kMaxDepth = 0.05f;
inline constexpr float kMinAspect = 1.0f;   // a non-zero aspect below this is taken as 1 (square), above kMaxAspect as that
inline constexpr float kMaxAspect = 3.0f;
// The widest lens the cameras' distance is planned for (half angle, radians: 80 degrees across). LE1's cinematic shots measured
// 24-67 degrees; the first frame of a conversation still reports the gameplay window (the headset's 123 degrees), which set the
// cameras 6.5 units apart for that frame (the owner, cross-eyed at a conversation's start, 2026-11-06).
inline constexpr float kMaxLensHalfH = 0.6981317f;
inline constexpr float kMinConvergeM = 0.5f;
inline constexpr float kMaxConvergeM = 20.0f;
inline constexpr float kDefaultIpdM = 0.064f;
// The scene's own scale: Shepard's skeleton is about 100 units per metre (research/30), whatever Halcyon's lean gain makes
// the view's scale. `convergeM` is a distance in the scene.
inline constexpr float kSceneUnitsPerMeter = 100.0f;
// Infinity's on-screen disparity stays under this share of the viewer's eye distance.
inline constexpr float kDivergenceShare = 0.8f;

// Every value inside its range (a non-finite one takes the default).
Settings Clamped(Settings const& s);

// The largest depth a screen `widthM` wide allows for eyes `ipdM` apart (0 or less: kDefaultIpdM): infinity's disparity on
// the screen stays under kDivergenceShare of the eye distance.
float MaxDepth(float widthM, float ipdM);

// The share of its image's width each eye drops so that infinity's disparity is `depth` of the width shown: the shown part
// is (1 - c) wide and the two parts are c apart, so c / (1 - c) = depth.
float CropFraction(float depth);

struct Rect {
    int x = 0, y = 0, width = 0, height = 0;
};
struct EyeRects {
    Rect left, right;
};
// The part of each eye's image (`width` x `height` pixels) the screen shows: the left eye drops its left `crop`, the right
// eye its right `crop`; both the same size; with `aspect` > 0 only the middle band of rows that makes the shown part that
// shape (none when the image is already wider).
EyeRects Rects(int width, int height, float crop, float aspect = 0.0f);

// The screen's height for a shown part `rects` (its aspect), at `widthM`.
float HeightM(EyeRects const& rects, float widthM);

// The distance between the two cameras (game units) for a render whose horizontal half angle is `halfH` (radians) so that
// the scene `convergeUU` away lands on the screen plane after a crop of `crop`: a point at that distance has a disparity of
// baseline / (2 tan(halfH) convergeUU) of the image's width, which the crop cancels. 0 for a degenerate input; a lens wider
// than kMaxLensHalfH is planned as that (it isn't a director's lens).
float BaselineUU(float crop, float convergeUU, float halfH);

// What a frame uses, from the settings, the headset's eye distance and the render's horizontal half angle.
struct Frame {
    float depth = 0.0f;        // after the divergence cap
    float crop = 0.0f;
    float baselineUU = 0.0f;   // between the cameras: each pass is half of it to its side
    bool depthCapped = false;  // the settings asked for more than MaxDepth
};
Frame Plan(Settings const& s, float ipdM, float unitsPerMeter, float halfH);

} // namespace mevr::vr::cine_screen

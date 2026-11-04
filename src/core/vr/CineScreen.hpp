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
    bool conversations = false;  // conversations: on the 3D screen while Halcyon's VR conversations are on
    bool cutscenes = false;      // cutscenes: the same for cutscenes (prerendered movies stay on the flat screen)
    float distanceM = 2.6f;      // distanceM: how far ahead the screen stands when a scene starts
    float widthM = 3.0f;         // widthM: the screen's width
    float depth = 0.015f;        // depth: infinity's disparity, a fraction of the screen's width (0 = flat, at the screen)
    float convergeM = 2.5f;      // convergeM: the scene distance that lands on the screen plane (metres of the scene)
};

inline constexpr float kMinDistanceM = 1.0f;
inline constexpr float kMaxDistanceM = 10.0f;
inline constexpr float kMinWidthM = 1.0f;
inline constexpr float kMaxWidthM = 12.0f;
inline constexpr float kMaxDepth = 0.05f;
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
// eye its right `crop`; both the same size.
EyeRects Rects(int width, int height, float crop);

// The screen's height for a shown part `rects` (its aspect), at `widthM`.
float HeightM(EyeRects const& rects, float widthM);

// The distance between the two cameras (game units) for a render whose horizontal half angle is `halfH` (radians) so that
// the scene `convergeUU` away lands on the screen plane after a crop of `crop`: a point at that distance has a disparity of
// baseline / (2 tan(halfH) convergeUU) of the image's width, which the crop cancels. 0 for a degenerate input.
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

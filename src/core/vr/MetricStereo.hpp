#pragma once

// Life-size (metric) stereo for Halcyon's Stereo 2 (research/30). Game-independent math, unit-tested in
// tests/Main.cpp; the host's side is src/plugin/MetricStereo.cpp, the render-side edits are Halcyon's
// render_hook.cpp ([MEVR] metric seams).
//
// Conventions. OpenXR: x right, y up, -z forward, metres; FOV angles in radians, left and down negative.
// Halcyon's camera offsets are game units along the game camera's right, up and forward axes (the pre-look
// axes ApplyViewOffsetSEH uses); Halcyon maps its app space onto them as x -> right, y -> up, -z -> forward.
// UE3's projection is row-vector, left-handed, w = z: NDC x = (p0 x + p8 z) / z, NDC y = (p5 y + p9 z) / z.
namespace mevr::vr::metric_stereo {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};
// XrFovf's four angles (radians).
struct Fov {
    float left = 0.0f, right = 0.0f, up = 0.0f, down = 0.0f;
};
// The tangents of those angles: the frustum's edges at unit distance.
struct Tangents {
    float left = 0.0f, right = 0.0f, up = 0.0f, down = 0.0f;
};

// The largest half angle either way that's still a sane perspective (Halcyon accepts up to 2.2 rad of
// half FOV in its override, but tan() explodes past ~83 degrees).
inline constexpr float kMaxAngle = 1.45f;

// An eye's FOV with Halcyon's VR Fill applied (its angles, not their tangents, are scaled: vrFillH/V,
// ClampVrFill's 0.5-1.25), each angle limited to kMaxAngle.
Fov Filled(Fov const& eye, float fillH, float fillV);
// Halcyon's symmetric render window: the widest half angle of both eyes each way, times the fill. Every
// eye's own (filled) frustum lies inside it.
Fov UnionWindow(Fov const& left, Fov const& right, float fillH, float fillV);
Tangents TangentsOf(Fov const& fov);

// UE3's off-centre perspective terms for a frustum (w = z, so a projection whose M[2][3] is 1):
// p0 = 2/(r-l), p8 = -(r+l)/(r-l), p5 = 2/(u-d), p9 = -(u+d)/(u-d). A symmetric frustum has p8 = p9 = 0
// and p0 = 1/tan(half angle), which is what Halcyon writes.
struct OffCentre {
    float p0 = 1.0f, p5 = 1.0f, p8 = 0.0f, p9 = 0.0f;
};
OffCentre Projection(Tangents const& t);
// The NDC of a view-space point (x right, y up, z forward > 0) under those terms.
void Project(OffCentre const& p, Vec3 const& view, float* ndcX, float* ndcY);

// Each eye's camera offset for one frame.
struct EyeInputs {
    Quat orientation[2];   // the runtime's views (xrLocateViews), app space
    Vec3 position[2];
    // The orientation the rendered view faces in the app space (Halcyon's roll-removed head), and whether the
    // render actually turns with the head (head look on). When it doesn't (the menu, the Mako, head aim), the
    // eyes' head-relative offsets are applied along the game camera's own axes instead.
    Quat viewOrientation;
    bool viewFollowsHead = true;
    float unitsPerMeter = 50.0f;   // the view's scale: Halcyon's 50 times its lean gain
    float depth = 1.0f;            // the "extra depth" multiplier on the real eye distance
};
struct EyeOffsets {
    // Game units along the camera's right, up, forward, from the eye centre (where the lean puts the camera).
    Vec3 body[2];
    float ipdM = 0.0f;    // the runtime's eye distance (metres)
    float ipdUU = 0.0f;   // as rendered (game units: ipdM x unitsPerMeter x depth)
    float cantDeg = 0.0f; // the angle between the two eyes' orientations (0 on parallel-display headsets)
};
EyeOffsets BodyOffsets(EyeInputs const& in);

// ---- How a rendered pair reads in the headset (research/30's numbers) ----------------------------------------
// A pair as the viewer sees it: each eye's image was rendered from `eye` (metres, in the HEAD's frame: x right,
// z forward), then shifted sideways by `shiftTan` (tangent units; + = the content moves right), and is shown
// at the viewer's real eye (+-ipd/2) facing the head's direction (what Halcyon declares).
struct RenderedPair {
    Vec3 eye[2];
    float shiftTan[2] = {0.0f, 0.0f};
};
// Halcyon's Stereo 2: the eyes at +-halfEyeUU along the BODY's right (the game camera's, before head look),
// halfEyeUU / unitsPerMeter metres each; the whole image of the eye pass 0 goes to shifted right by
// convergence x tan(halfAngleH) and the other's left (its submit-time UV shift, conv/2 of the image width; the
// sign stays with the pass, so with swapEyes the shift is divergent). `headYawLeftRad`: how far the head is
// turned left of the body (head look).
RenderedPair HalcyonPair(float halfEyeUU, float unitsPerMeter, float convergence, float halfAngleH, bool swapEyes,
                         float headYawLeftRad);
// The metric pair: the real eyes, no shift.
RenderedPair MetricPair(float ipdM);
// The distance (metres) at which the viewer's eyes fuse a point `trueDepthM` straight ahead of the head:
// rays from the real eyes along each image's direction to it, triangulated. +infinity when the rays are
// parallel; negative when they diverge (the point reads beyond infinity).
double PerceivedDepth(RenderedPair const& pair, double trueDepthM, double ipdM);
// The pair's binocular disparity error for that point (arcminutes; + = more crossed than the truth).
double DisparityErrorArcmin(RenderedPair const& pair, double trueDepthM, double ipdM);

} // namespace mevr::vr::metric_stereo

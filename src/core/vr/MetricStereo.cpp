#include "core/vr/MetricStereo.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mevr::vr::metric_stereo {
namespace {

float ClampAngle(float a) {
    if (!std::isfinite(a)) {
        return 0.0f;
    }
    return std::clamp(a, -kMaxAngle, kMaxAngle);
}

Vec3 Rotate(Quat const& q, Vec3 const& v) {
    // v + 2w(u x v) + 2 u x (u x v)
    float const tx = 2.0f * (q.y * v.z - q.z * v.y);
    float const ty = 2.0f * (q.z * v.x - q.x * v.z);
    float const tz = 2.0f * (q.x * v.y - q.y * v.x);
    return {v.x + q.w * tx + (q.y * tz - q.z * ty), v.y + q.w * ty + (q.z * tx - q.x * tz),
        v.z + q.w * tz + (q.x * ty - q.y * tx)};
}

Quat Conjugate(Quat const& q) {
    return {-q.x, -q.y, -q.z, q.w};
}

} // namespace

Fov Filled(Fov const& eye, float fillH, float fillV) {
    return {ClampAngle(eye.left * fillH), ClampAngle(eye.right * fillH), ClampAngle(eye.up * fillV),
        ClampAngle(eye.down * fillV)};
}

Fov UnionWindow(Fov const& left, Fov const& right, float fillH, float fillV) {
    float const h = std::max(std::max(-left.left, left.right), std::max(-right.left, right.right));
    float const v = std::max(std::max(left.up, -left.down), std::max(right.up, -right.down));
    float const fh = ClampAngle(h * fillH);
    float const fv = ClampAngle(v * fillV);
    return {-fh, fh, fv, -fv};
}

Tangents TangentsOf(Fov const& fov) {
    return {std::tan(fov.left), std::tan(fov.right), std::tan(fov.up), std::tan(fov.down)};
}

OffCentre Projection(Tangents const& t) {
    OffCentre p;
    float const w = t.right - t.left;
    float const h = t.up - t.down;
    if (!(w > 1e-4f) || !(h > 1e-4f)) {
        return p;
    }
    p.p0 = 2.0f / w;
    p.p8 = -(t.right + t.left) / w;
    p.p5 = 2.0f / h;
    p.p9 = -(t.up + t.down) / h;
    return p;
}

void Project(OffCentre const& p, Vec3 const& view, float* ndcX, float* ndcY) {
    float const z = view.z;
    if (ndcX != nullptr) {
        *ndcX = (p.p0 * view.x + p.p8 * z) / z;
    }
    if (ndcY != nullptr) {
        *ndcY = (p.p5 * view.y + p.p9 * z) / z;
    }
}

EyeOffsets BodyOffsets(EyeInputs const& in) {
    EyeOffsets out;
    Vec3 const centre{0.5f * (in.position[0].x + in.position[1].x), 0.5f * (in.position[0].y + in.position[1].y),
        0.5f * (in.position[0].z + in.position[1].z)};
    float const dx = in.position[1].x - in.position[0].x;
    float const dy = in.position[1].y - in.position[0].y;
    float const dz = in.position[1].z - in.position[0].z;
    out.ipdM = std::sqrt(dx * dx + dy * dy + dz * dz);
    float const scale = in.unitsPerMeter * in.depth;
    out.ipdUU = out.ipdM * scale;
    for (int e = 0; e < 2; ++e) {
        Vec3 d{in.position[e].x - centre.x, in.position[e].y - centre.y, in.position[e].z - centre.z};
        if (!in.viewFollowsHead) {
            // The render faces the game camera, not the head: keep the eyes' place relative to the head (its roll
            // included) and lay it along the camera's axes.
            d = Rotate(Conjugate(in.viewOrientation), d);
        }
        out.body[e] = {d.x * scale, d.y * scale, -d.z * scale};
    }
    Quat const a = in.orientation[0];
    Quat const b = in.orientation[1];
    float const dot = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    out.cantDeg = 2.0f * std::acos(std::min(1.0f, dot)) * 57.29578f;
    return out;
}

RenderedPair HalcyonPair(float halfEyeUU, float unitsPerMeter, float convergence, float halfAngleH, bool swapEyes,
                         float headYawLeftRad) {
    RenderedPair pair;
    float const h = halfEyeUU / unitsPerMeter;
    // The body's right axis in the head's frame (x right, z forward) when the head is turned left by yaw.
    float const rx = std::cos(headYawLeftRad);
    float const rz = -std::sin(headYawLeftRad);
    pair.eye[0] = {-h * rx, 0.0f, -h * rz};
    pair.eye[1] = {h * rx, 0.0f, h * rz};
    // Submit: pass 0's image samples u - conv/2 (its content moves right by conv/2 of the width, conv in NDC,
    // conv x tan(half angle) in tangent units); pass 1's moves left. Pass 0 is the right eye with swapEyes.
    float const s = convergence * std::tan(halfAngleH);
    int const pass0Eye = swapEyes ? 1 : 0;
    pair.shiftTan[pass0Eye] = s;
    pair.shiftTan[1 - pass0Eye] = -s;
    return pair;
}

RenderedPair MetricPair(float ipdM) {
    RenderedPair pair;
    pair.eye[0] = {-0.5f * ipdM, 0.0f, 0.0f};
    pair.eye[1] = {0.5f * ipdM, 0.0f, 0.0f};
    return pair;
}

namespace {

// Each eye's image direction (tangent) to a point straight ahead of the head, as shown.
void ShownTangents(RenderedPair const& pair, double depth, double* tl, double* tr) {
    double t[2];
    for (int e = 0; e < 2; ++e) {
        double const ex = pair.eye[e].x;
        double const ez = pair.eye[e].z;
        t[e] = (0.0 - ex) / (depth - ez) + pair.shiftTan[e];
    }
    *tl = t[0];
    *tr = t[1];
}

} // namespace

double PerceivedDepth(RenderedPair const& pair, double trueDepthM, double ipdM) {
    double tl = 0.0, tr = 0.0;
    ShownTangents(pair, trueDepthM, &tl, &tr);
    // -ipd/2 + tl Z = ipd/2 + tr Z
    double const converge = tl - tr;
    if (std::fabs(converge) < 1e-12) {
        return std::numeric_limits<double>::infinity();
    }
    return ipdM / converge;
}

double DisparityErrorArcmin(RenderedPair const& pair, double trueDepthM, double ipdM) {
    double tl = 0.0, tr = 0.0;
    ShownTangents(pair, trueDepthM, &tl, &tr);
    double const shown = std::atan(tl) - std::atan(tr);
    double const truth = 2.0 * std::atan(0.5 * ipdM / trueDepthM);
    return (shown - truth) * (180.0 / 3.14159265358979) * 60.0;
}

} // namespace mevr::vr::metric_stereo

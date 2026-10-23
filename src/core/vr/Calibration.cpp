#include "core/vr/Calibration.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <vector>

namespace mevr::vr::calibration {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

// Double precision for the sums and the solve; OpenXR floats at the edges.
struct V {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V operator*(V a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double Length(V a) { return std::sqrt(Dot(a, a)); }
bool Finite(V a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
std::optional<V> Unit(V a) {
    double const length = Length(a);
    if (!(length > 1e-9) || !std::isfinite(length)) {
        return std::nullopt;
    }
    return a * (1.0 / length);
}
V From(XrVector3f const& v) { return {v.x, v.y, v.z}; }
XrVector3f To(V const& v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

constexpr V kUp{0.0, 1.0, 0.0};

// The sideways axis of a line: perpendicular to it and to up (nullopt for a vertical line).
std::optional<V> Sideways(V direction) {
    return Unit(Cross(direction, kUp));
}

} // namespace

// ---- Rotations ---------------------------------------------------------------------------------------------

XrQuaternionf Multiply(XrQuaternionf const& a, XrQuaternionf const& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

XrQuaternionf Conjugate(XrQuaternionf const& q) {
    return {-q.x, -q.y, -q.z, q.w};
}

XrQuaternionf Normalize(XrQuaternionf const& q) {
    double const n = std::sqrt(static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
        static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w);
    if (!(n > 1e-9) || !std::isfinite(n)) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    return {static_cast<float>(q.x / n), static_cast<float>(q.y / n), static_cast<float>(q.z / n), static_cast<float>(q.w / n)};
}

XrVector3f Rotate(XrQuaternionf const& q, XrVector3f const& v) {
    // v + 2w (u x v) + 2 u x (u x v), in double.
    V const u{q.x, q.y, q.z};
    V const p = From(v);
    V const t = Cross(u, p) * 2.0;
    return To(p + t * static_cast<double>(q.w) + Cross(u, t));
}

float AngleDeg(XrQuaternionf const& a, XrQuaternionf const& b) {
    XrQuaternionf const na = Normalize(a);
    XrQuaternionf const nb = Normalize(b);
    double const dot = std::abs(static_cast<double>(na.x) * nb.x + static_cast<double>(na.y) * nb.y +
        static_cast<double>(na.z) * nb.z + static_cast<double>(na.w) * nb.w);
    return static_cast<float>(2.0 * std::acos(std::clamp(dot, 0.0, 1.0)) / kDeg);
}

float DirectionAngleDeg(XrVector3f const& a, XrVector3f const& b) {
    auto const ua = Unit(From(a));
    auto const ub = Unit(From(b));
    if (!ua || !ub) {
        return 180.0f;
    }
    return static_cast<float>(std::acos(std::clamp(Dot(*ua, *ub), -1.0, 1.0)) / kDeg);
}

namespace {

// The quaternion of an orthonormal basis (columns x, y, z), Shepperd's method.
XrQuaternionf FromBasis(V x, V y, V z) {
    double const m00 = x.x, m01 = y.x, m02 = z.x;
    double const m10 = x.y, m11 = y.y, m12 = z.y;
    double const m20 = x.z, m21 = y.z, m22 = z.z;
    double const trace = m00 + m11 + m22;
    double qx, qy, qz, qw;
    if (trace > 0.0) {
        double const s = std::sqrt(trace + 1.0) * 2.0;
        qw = 0.25 * s;
        qx = (m21 - m12) / s;
        qy = (m02 - m20) / s;
        qz = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        double const s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        qw = (m21 - m12) / s;
        qx = 0.25 * s;
        qy = (m01 + m10) / s;
        qz = (m02 + m20) / s;
    } else if (m11 > m22) {
        double const s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        qw = (m02 - m20) / s;
        qx = (m01 + m10) / s;
        qy = 0.25 * s;
        qz = (m12 + m21) / s;
    } else {
        double const s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
        qw = (m10 - m01) / s;
        qx = (m02 + m20) / s;
        qy = (m12 + m21) / s;
        qz = 0.25 * s;
    }
    return Normalize({static_cast<float>(qx), static_cast<float>(qy), static_cast<float>(qz), static_cast<float>(qw)});
}

} // namespace

std::optional<XrQuaternionf> LookRotation(XrVector3f const& forward, XrVector3f const& up) {
    auto const f = Unit(From(forward));
    auto const u0 = Unit(From(up));
    if (!f || !u0) {
        return std::nullopt;
    }
    // -Z is forward: z = -f. x = up x z (right), y = z x x.
    V const z = *f * -1.0;
    auto const x = Unit(Cross(*u0, z));
    if (!x || std::abs(Dot(*f, *u0)) > 0.9995) {
        return std::nullopt;
    }
    V const y = Cross(z, *x);
    return FromBasis(*x, y, z);
}

XrQuaternionf YawRotation(float yaw) {
    return {0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
}

float HeadingOf(XrQuaternionf const& orientation) {
    XrVector3f f = Rotate(orientation, {0.0f, 0.0f, -1.0f});
    if (std::hypot(f.x, f.z) < 1e-4f) {
        XrVector3f const u = Rotate(orientation, {0.0f, 1.0f, 0.0f});
        f = {-u.x * (f.y > 0.0f ? 1.0f : -1.0f), 0.0f, -u.z * (f.y > 0.0f ? 1.0f : -1.0f)};
    }
    return std::atan2(-f.x, -f.z);
}

std::optional<XrQuaternionf> Mean(std::span<XrQuaternionf const> rotations) {
    if (rotations.empty()) {
        return std::nullopt;
    }
    XrQuaternionf const first = Normalize(rotations.front());
    double sum[4] = {};
    for (XrQuaternionf const& raw : rotations) {
        XrQuaternionf const q = Normalize(raw);
        double const dot = static_cast<double>(q.x) * first.x + static_cast<double>(q.y) * first.y +
            static_cast<double>(q.z) * first.z + static_cast<double>(q.w) * first.w;
        double const sign = dot < 0.0 ? -1.0 : 1.0;
        sum[0] += q.x * sign;
        sum[1] += q.y * sign;
        sum[2] += q.z * sign;
        sum[3] += q.w * sign;
    }
    return Normalize({static_cast<float>(sum[0]), static_cast<float>(sum[1]), static_cast<float>(sum[2]), static_cast<float>(sum[3])});
}

// ---- The solve ---------------------------------------------------------------------------------------------

EyeChoice ChooseEye(std::span<AimSample const> samples) {
    EyeChoice out;
    double sum[2] = {};
    int count = 0;
    for (AimSample const& s : samples) {
        double lateral[2] = {};
        bool ok = true;
        for (int e = 0; e < 2 && ok; ++e) {
            V const eye = From(s.eyes[e]);
            auto const d = Unit(From(s.target) - eye);
            auto const side = d ? Sideways(*d) : std::nullopt;
            if (!d || !side) {
                ok = false;
                break;
            }
            lateral[e] = std::abs(Dot(From(s.right.position) - eye, *side));
        }
        if (!ok) {
            continue;
        }
        sum[0] += lateral[0];
        sum[1] += lateral[1];
        ++count;
    }
    if (count == 0) {
        out.lateral[0] = out.lateral[1] = -1.0f;
        return out;
    }
    out.lateral[0] = static_cast<float>(sum[0] / count);
    out.lateral[1] = static_cast<float>(sum[1] / count);
    out.eye = out.lateral[0] < out.lateral[1] ? 0 : 1;
    return out;
}

Stance Solve(std::span<AimSample const> samples, int eye) {
    Stance out;
    if (samples.empty()) {
        return out;
    }
    out.eyeMeasured = eye != 0 && eye != 1;
    out.eye = out.eyeMeasured ? ChooseEye(samples).eye : eye;
    std::vector<XrQuaternionf> trims;
    std::vector<XrQuaternionf> leftTrims;
    std::vector<V> lines;
    std::vector<V> handLines;
    V lineSum{};
    V foregripSum{};
    V stockSum{};
    double reliefSum = 0.0;
    int lefts = 0;
    for (AimSample const& s : samples) {
        V const e = From(s.eyes[out.eye]);
        V const g = From(s.right.position);
        auto const d = Unit(From(s.target) - e);
        if (!d || !Finite(g)) {
            return Stance{};
        }
        auto const gun = LookRotation(To(*d));
        if (!gun) {
            return Stance{};
        }
        XrQuaternionf const grip = Normalize(s.right.orientation);
        trims.push_back(Multiply(Conjugate(grip), *gun));
        // The line's point nearest the grip's origin, in the grip's frame.
        double const along = Dot(g - e, *d);
        V const nearest = e + *d * along;
        V const inGrip = From(Rotate(Conjugate(grip), To(nearest - g)));
        lineSum = lineSum + inGrip;
        reliefSum += along;
        // The stock: the right grip from the eyes' centre, in the head's level frame.
        XrQuaternionf const level = YawRotation(HeadingOf(s.head.orientation));
        stockSum = stockSum + From(Rotate(Conjugate(level), To(g - From(s.head.position))));
        if (s.hasLeft) {
            V const foregrip = From(Rotate(Conjugate(*gun), To(From(s.left.position) - g)));
            foregripSum = foregripSum + foregrip;
            leftTrims.push_back(Multiply(Conjugate(*gun), Normalize(s.left.orientation)));
            if (auto const u = Unit(foregrip)) {
                handLines.push_back(*u);
            }
            ++lefts;
        }
    }
    int const n = static_cast<int>(samples.size());
    auto const mean = Mean(trims);
    if (!mean) {
        return Stance{};
    }
    out.trim = *mean;
    out.samples = n;
    for (XrQuaternionf const& t : trims) {
        out.spreadDeg = std::max(out.spreadDeg, AngleDeg(t, out.trim));
    }
    out.line = To(lineSum * (1.0 / n));
    out.eyeRelief = static_cast<float>(reliefSum / n);
    out.stock = To(stockSum * (1.0 / n));
    if (lefts > 0 && lefts == n) {
        out.hasLeft = true;
        out.foregrip = To(foregripSum * (1.0 / lefts));
        out.leftTrim = Mean(leftTrims).value_or(XrQuaternionf{0.0f, 0.0f, 0.0f, 1.0f});
        V lineMean{};
        for (V const& l : handLines) {
            lineMean = lineMean + l;
        }
        auto const hands = Unit(lineMean);
        out.handsLine = hands ? To(*hands) : To(Unit(From(out.foregrip)).value_or(V{0.0, 0.0, -1.0}));
        for (V const& l : handLines) {
            out.handsSpreadDeg = std::max(out.handsSpreadDeg, DirectionAngleDeg(To(l), out.handsLine));
        }
    }
    out.valid = true;
    return out;
}

// ---- Applying ----------------------------------------------------------------------------------------------

XrQuaternionf GunOrientation(XrQuaternionf const& grip, Stance const& stance) {
    return Normalize(Multiply(Normalize(grip), stance.trim));
}

Line SightLine(XrPosef const& grip, Stance const& stance) {
    Line line;
    XrVector3f const offset = Rotate(Normalize(grip.orientation), stance.line);
    line.point = {grip.position.x + offset.x, grip.position.y + offset.y, grip.position.z + offset.z};
    line.direction = Rotate(GunOrientation(grip.orientation, stance), {0.0f, 0.0f, -1.0f});
    return line;
}

XrVector3f ShiftOntoLine(XrVector3f const& point, Line const& line, float maxLength) {
    auto const d = Unit(From(line.direction));
    if (!d || !Finite(From(point)) || !Finite(From(line.point))) {
        return {};
    }
    V const delta = From(line.point) - From(point);
    V shift = delta - *d * Dot(delta, *d);
    double const length = Length(shift);
    if (maxLength >= 0.0f && length > maxLength) {
        shift = shift * (maxLength / length);
    }
    return To(shift);
}

XrVector3f OnPlane(Line const& line, XrVector3f const& planePoint, XrVector3f const& planeNormal, float minDistance,
    float fallback) {
    auto const d = Unit(From(line.direction));
    V const p = From(line.point);
    if (!d) {
        return line.point;
    }
    V const n = From(planeNormal);
    double const denominator = Dot(*d, n);
    if (std::abs(denominator) > 1e-6) {
        double const t = Dot(From(planePoint) - p, n) / denominator;
        if (t >= minDistance && std::isfinite(t)) {
            return To(p + *d * t);
        }
    }
    return To(p + *d * static_cast<double>(fallback));
}

// ---- Body --------------------------------------------------------------------------------------------------

float ShoulderWidth(XrVector3f const& left, XrVector3f const& right, XrVector3f const& forward) {
    V f = From(forward);
    f.y = 0.0;
    auto const unit = Unit(f);
    V const across = unit ? Cross(*unit, kUp) : V{1.0, 0.0, 0.0};
    return static_cast<float>(std::abs(Dot(From(right) - From(left), across)));
}

std::optional<float> ArmLength(float tPoseSpan, float shoulderWidth) {
    if (!std::isfinite(tPoseSpan) || !std::isfinite(shoulderWidth) || shoulderWidth < 0.15f || shoulderWidth > 0.75f) {
        return std::nullopt;
    }
    float const arm = 0.5f * (tPoseSpan - shoulderWidth);
    if (!(arm >= 0.3f && arm <= 1.2f)) {
        return std::nullopt;
    }
    return arm;
}

std::optional<CrouchFractions> CrouchFromHeights(double standingEye, double crouchedEye, double crouchAt, double standAt) {
    if (!std::isfinite(standingEye) || !std::isfinite(crouchedEye) || !(standingEye > 0.8) || !(crouchedEye > 0.2) ||
        standingEye - crouchedEye < 0.15) {
        return std::nullopt;
    }
    double const depth = standingEye - crouchedEye;
    CrouchFractions out;
    out.crouchBelow = std::clamp(1.0 - std::clamp(crouchAt, 0.05, 0.95) * depth / standingEye, 0.3, 0.92);
    out.standAbove = std::clamp(1.0 - std::clamp(standAt, 0.0, 0.9) * depth / standingEye, out.crouchBelow + 0.02, 0.99);
    return out;
}

// ---- Stillness ---------------------------------------------------------------------------------------------

void Stillness::Reset() {
    started_ = false;
    frames_ = 0;
    held_ = 0.0f;
}

float Stillness::Update(XrPosef const poses[kMaxPoses], bool const used[kMaxPoses], float dt, StillSettings const& settings) {
    bool moved = !started_;
    for (int i = 0; i < kMaxPoses && !moved; ++i) {
        if (used[i] != used_[i]) {
            moved = true;
        } else if (used[i]) {
            double const distance = Length(From(poses[i].position) - From(reference_[i].position));
            moved = distance > settings.position || AngleDeg(poses[i].orientation, reference_[i].orientation) > settings.angleDeg;
        }
    }
    if (moved) {
        started_ = true;
        frames_ = 0;
        held_ = 0.0f;
        for (int i = 0; i < kMaxPoses; ++i) {
            used_[i] = used[i];
            reference_[i] = poses[i];
            for (double& v : positionSum_[i]) {
                v = 0.0;
            }
            for (double& v : rotationSum_[i]) {
                v = 0.0;
            }
        }
    } else if (std::isfinite(dt) && dt > 0.0f) {
        held_ += std::min(dt, 0.25f);
    }
    for (int i = 0; i < kMaxPoses; ++i) {
        if (!used_[i]) {
            continue;
        }
        positionSum_[i][0] += poses[i].position.x;
        positionSum_[i][1] += poses[i].position.y;
        positionSum_[i][2] += poses[i].position.z;
        XrQuaternionf const q = Normalize(poses[i].orientation);
        XrQuaternionf const r = Normalize(reference_[i].orientation);
        double const sign = (static_cast<double>(q.x) * r.x + static_cast<double>(q.y) * r.y + static_cast<double>(q.z) * r.z +
                                static_cast<double>(q.w) * r.w) < 0.0 ? -1.0 : 1.0;
        rotationSum_[i][0] += q.x * sign;
        rotationSum_[i][1] += q.y * sign;
        rotationSum_[i][2] += q.z * sign;
        rotationSum_[i][3] += q.w * sign;
    }
    ++frames_;
    return held_;
}

XrPosef Stillness::Average(int i) const {
    if (i < 0 || i >= kMaxPoses || frames_ == 0) {
        return i >= 0 && i < kMaxPoses ? reference_[i] : XrPosef{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    }
    XrPosef out;
    double const n = frames_;
    out.position = {static_cast<float>(positionSum_[i][0] / n), static_cast<float>(positionSum_[i][1] / n),
        static_cast<float>(positionSum_[i][2] / n)};
    out.orientation = Normalize({static_cast<float>(rotationSum_[i][0]), static_cast<float>(rotationSum_[i][1]),
        static_cast<float>(rotationSum_[i][2]), static_cast<float>(rotationSum_[i][3])});
    return out;
}

// ---- Criteria ----------------------------------------------------------------------------------------------

bool LookingAhead(XrQuaternionf const& head, float maxPitchDeg) {
    XrVector3f const f = Rotate(Normalize(head), {0.0f, 0.0f, -1.0f});
    double const pitch = std::asin(std::clamp(static_cast<double>(f.y), -1.0, 1.0)) / kDeg;
    return std::abs(pitch) <= maxPitchDeg;
}

bool TPose(XrVector3f const& head, XrVector3f const& left, XrVector3f const& right, float minSpan) {
    V const l = From(left);
    V const r = From(right);
    V const h = From(head);
    double const span = Length(r - l);
    if (!(span >= minSpan)) {
        return false;
    }
    V const axis = (r - l) * (1.0 / span);
    double const along = Dot(h - l, axis);
    if (along < 0.28 || along > span - 0.28) {
        return false;
    }
    return std::abs(l.y - h.y) <= 0.35 && std::abs(r.y - h.y) <= 0.35;
}

bool ArmsForward(XrVector3f const& head, XrVector3f const& left, XrVector3f const& right, XrVector3f const& forward,
    float minAhead) {
    V f = From(forward);
    f.y = 0.0;
    auto const unit = Unit(f);
    if (!unit) {
        return false;
    }
    V const h = From(head);
    for (XrVector3f const& grip : {left, right}) {
        V const g = From(grip);
        if (Dot(g - h, *unit) < minAhead || g.y < h.y - 0.6 || g.y > h.y + 0.15) {
            return false;
        }
    }
    double const apart = Length(From(right) - From(left));
    return apart >= 0.1 && apart <= 0.8;
}

bool Crouched(float standingHeight, float height, float minDrop) {
    return std::isfinite(standingHeight) && std::isfinite(height) && standingHeight - height >= minDrop;
}

bool OnTarget(XrPosef const& head, XrVector3f const& grip, XrVector3f const& target, float maxGazeDeg, float maxOffLine,
    float minAhead) {
    V const eye = From(head.position);
    auto const toTarget = Unit(From(target) - eye);
    if (!toTarget) {
        return false;
    }
    XrVector3f const look = Rotate(Normalize(head.orientation), {0.0f, 0.0f, -1.0f});
    if (DirectionAngleDeg(look, To(*toTarget)) > maxGazeDeg) {
        return false;
    }
    V const g = From(grip) - eye;
    double const along = Dot(g, *toTarget);
    double const off = Length(g - *toTarget * along);
    return along >= minAhead && off <= maxOffLine;
}

// ---- Layout ------------------------------------------------------------------------------------------------

XrVector3f AnchorForward(Anchor const& anchor) {
    return {-std::sin(anchor.yaw), 0.0f, -std::cos(anchor.yaw)};
}

XrVector3f AnchorRight(Anchor const& anchor) {
    return {std::cos(anchor.yaw), 0.0f, -std::sin(anchor.yaw)};
}

XrVector3f AnchorPoint(Anchor const& anchor, float right, float up, float forward) {
    XrVector3f const f = AnchorForward(anchor);
    XrVector3f const r = AnchorRight(anchor);
    return {anchor.eyes.x + r.x * right + f.x * forward, anchor.eyes.y + up, anchor.eyes.z + r.z * right + f.z * forward};
}

XrPosef BoardPose(Anchor const& anchor) {
    XrPosef pose;
    pose.orientation = YawRotation(anchor.yaw);  // its +Z (front) faces back toward the anchor
    pose.position = AnchorPoint(anchor, 0.0f, 0.0f, kBoardDistance);
    return pose;
}

XrVector3f TargetPosition(Anchor const& anchor, int index) {
    int const i = std::clamp(index, 0, kTargets - 1);
    return AnchorPoint(anchor, kTargetX[i], kTargetY[i], kBoardDistance);
}

XrPosef Billboard(XrVector3f const& center, XrVector3f const& eye) {
    XrPosef pose;
    pose.position = center;
    V const toEye = From(eye) - From(center);
    auto const normal = Unit(toEye);
    // Front (+Z) toward the eye, upright.
    auto const facing = normal ? LookRotation(To(*normal * -1.0)) : std::nullopt;
    pose.orientation = facing.value_or(XrQuaternionf{0.0f, 0.0f, 0.0f, 1.0f});
    return pose;
}

XrPosef PanelPose(Anchor const& anchor, bool aiming) {
    XrVector3f center;
    if (aiming) {
        // The lower left, out of the targets' way: 27 degrees left, 23 down, 1.3 m.
        float const yaw = 27.0f * static_cast<float>(kDeg);
        float const pitch = -23.0f * static_cast<float>(kDeg);
        float const distance = 1.3f;
        float const forward = distance * std::cos(pitch) * std::cos(yaw);
        float const right = -distance * std::cos(pitch) * std::sin(yaw);
        float const up = distance * std::sin(pitch);
        center = AnchorPoint(anchor, right, up, forward);
    } else {
        center = AnchorPoint(anchor, 0.0f, -0.16f, 1.4f);
    }
    return Billboard(center, anchor.eyes);
}

XrVector3f OrbRest(Anchor const& stepAnchor, Orb orb) {
    switch (orb) {
    case Orb::RifleRear: return AnchorPoint(stepAnchor, 0.14f, -0.32f, 0.36f);
    case Orb::RifleFront: return AnchorPoint(stepAnchor, -0.03f, -0.36f, 0.72f);
    case Orb::Pistol: break;
    }
    return AnchorPoint(stepAnchor, 0.16f, -0.36f, 0.42f);
}

// ---- Text --------------------------------------------------------------------------------------------------

std::string FormatVector(XrVector3f const& v) {
    char text[96];
    std::snprintf(text, sizeof text, "%.4f, %.4f, %.4f", v.x, v.y, v.z);
    return text;
}

std::string FormatRotation(XrQuaternionf const& q) {
    char text[128];
    std::snprintf(text, sizeof text, "%.6f, %.6f, %.6f, %.6f", q.x, q.y, q.z, q.w);
    return text;
}

namespace {

// Up to `count` numbers separated by commas or spaces; false unless exactly `count`.
bool Numbers(std::string_view text, float* out, int count) {
    int n = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        char const c = text[i];
        if (c == ',' || c == ' ' || c == '\t') {
            ++i;
            continue;
        }
        if (n >= count) {
            return false;
        }
        float value = 0.0f;
        auto const [end, ec] = std::from_chars(text.data() + i, text.data() + text.size(), value);
        if (ec != std::errc() || !std::isfinite(value)) {
            return false;
        }
        out[n++] = value;
        i = static_cast<std::size_t>(end - text.data());
    }
    return n == count;
}

std::string Sanitize(std::string_view text) {
    std::string out;
    bool lastUnderscore = false;
    for (char const raw : text) {
        unsigned char const c = static_cast<unsigned char>(raw);
        if (std::isalnum(c) != 0) {
            out.push_back(static_cast<char>(std::tolower(c)));
            lastUnderscore = false;
        } else if (!lastUnderscore && !out.empty()) {
            out.push_back('_');
            lastUnderscore = true;
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out;
}

// "/interaction_profiles/oculus/touch_controller" -> "oculus/touch_controller".
std::string_view ProfileTail(std::string_view profile) {
    constexpr std::string_view kPrefix = "/interaction_profiles/";
    if (profile.substr(0, kPrefix.size()) == kPrefix) {
        profile.remove_prefix(kPrefix.size());
    }
    while (!profile.empty() && profile.front() == '/') {
        profile.remove_prefix(1);
    }
    return profile;
}

} // namespace

std::optional<XrVector3f> ParseVector(std::string_view text) {
    float v[3];
    if (!Numbers(text, v, 3)) {
        return std::nullopt;
    }
    return XrVector3f{v[0], v[1], v[2]};
}

std::optional<XrQuaternionf> ParseRotation(std::string_view text) {
    float v[4];
    if (!Numbers(text, v, 4)) {
        return std::nullopt;
    }
    double const n = std::sqrt(static_cast<double>(v[0]) * v[0] + static_cast<double>(v[1]) * v[1] +
        static_cast<double>(v[2]) * v[2] + static_cast<double>(v[3]) * v[3]);
    if (!(n > 0.5) || !(n < 1.5)) {
        return std::nullopt;
    }
    return Normalize({v[0], v[1], v[2], v[3]});
}

std::string ProfileKey(std::string_view interactionProfile, std::string_view systemName) {
    std::string key = Sanitize(ProfileTail(interactionProfile));
    if (key.empty()) {
        key = "unknown";
    }
    std::string const system = Sanitize(systemName);
    if (!system.empty()) {
        key += "__" + system;
    }
    if (key.size() > 90) {
        key.resize(90);
    }
    return key;
}

std::string ProfileLabel(std::string_view interactionProfile, std::string_view systemName) {
    std::string_view const tail = ProfileTail(interactionProfile);
    std::string_view name = tail.substr(tail.find('/') == std::string_view::npos ? 0 : tail.find('/') + 1);
    std::string label;
    bool start = true;
    for (char const c : name) {
        if (c == '_') {
            label.push_back(' ');
            start = true;
        } else {
            label.push_back(start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
            start = false;
        }
    }
    if (label.empty()) {
        label = "Unknown controllers";
    }
    if (!systemName.empty()) {
        label += " (" + std::string(systemName) + ")";
    }
    return label;
}

bool ListHas(std::string_view list, std::string_view key) {
    if (key.empty()) {
        return false;
    }
    std::size_t start = 0;
    while (start <= list.size()) {
        std::size_t const comma = list.find(',', start);
        std::string_view item = list.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) {
            item.remove_prefix(1);
        }
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) {
            item.remove_suffix(1);
        }
        if (item == key) {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return false;
}

} // namespace mevr::vr::calibration

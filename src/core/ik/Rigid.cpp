// SPDX-License-Identifier: MIT
#include "core/ik/Rigid.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::ik {

Quat operator*(Quat a, Quat b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

Quat Conjugate(Quat q) {
    return {-q.x, -q.y, -q.z, q.w};
}

double Norm(Quat q) {
    return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

Quat Normalize(Quat q) {
    double const n = Norm(q);
    if (!(n > 1e-12)) {
        return {};
    }
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

Vec3 Rotate(Quat q, Vec3 v) {
    Vec3 const u{q.x, q.y, q.z};
    Vec3 const t = Cross(u, v) * 2.0;
    return v + t * q.w + Cross(u, t);
}

Quat FromTo(Vec3 from, Vec3 to) {
    Vec3 const a = ik::Normalize(from, {1.0, 0.0, 0.0});
    Vec3 const b = ik::Normalize(to, {1.0, 0.0, 0.0});
    double const c = std::clamp(Dot(a, b), -1.0, 1.0);
    if (c < -0.999999) {
        // Opposite directions: a half turn about any axis perpendicular to `a`.
        Vec3 const helper = std::abs(a.x) < 0.8 ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 1.0, 0.0};
        Vec3 const axis = ik::Normalize(Cross(a, helper));
        return {axis.x, axis.y, axis.z, 0.0};
    }
    Vec3 const axis = Cross(a, b);
    return Normalize(Quat{axis.x, axis.y, axis.z, 1.0 + c});
}

Quat FromAxes(Vec3 x, Vec3 y, Vec3 z) {
    // Column-vector rotation matrix whose columns are the images of the unit axes.
    double const r00 = x.x, r01 = y.x, r02 = z.x;
    double const r10 = x.y, r11 = y.y, r12 = z.y;
    double const r20 = x.z, r21 = y.z, r22 = z.z;
    double const trace = r00 + r11 + r22;
    Quat q;
    if (trace > 0.0) {
        double const s = 2.0 * std::sqrt(trace + 1.0);
        q = {(r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25 * s};
    } else if (r00 > r11 && r00 > r22) {
        double const s = 2.0 * std::sqrt(std::max(0.0, 1.0 + r00 - r11 - r22));
        q = {0.25 * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s};
    } else if (r11 > r22) {
        double const s = 2.0 * std::sqrt(std::max(0.0, 1.0 + r11 - r00 - r22));
        q = {(r01 + r10) / s, 0.25 * s, (r12 + r21) / s, (r02 - r20) / s};
    } else {
        double const s = 2.0 * std::sqrt(std::max(0.0, 1.0 + r22 - r00 - r11));
        q = {(r02 + r20) / s, (r12 + r21) / s, 0.25 * s, (r10 - r01) / s};
    }
    return Normalize(q);
}

double AngleBetween(Quat a, Quat b) {
    Quat const na = Normalize(a);
    Quat const nb = Normalize(b);
    double const d = std::abs(na.x * nb.x + na.y * nb.y + na.z * nb.z + na.w * nb.w);
    return 2.0 * std::acos(std::clamp(d, 0.0, 1.0));
}

Vec3 Apply(Transform const& t, Vec3 p) {
    return Rotate(t.rotation, p * t.scale) + t.translation;
}

Transform Compose(Transform const& local, Transform const& parent) {
    return {
        Normalize(parent.rotation * local.rotation),
        Rotate(parent.rotation, local.translation * parent.scale) + parent.translation,
        local.scale * parent.scale,
    };
}

Transform Inverse(Transform const& t) {
    Quat const inverse = Conjugate(Normalize(t.rotation));
    double const scale = t.scale != 0.0 ? 1.0 / t.scale : 0.0;
    return {inverse, Rotate(inverse, t.translation) * -scale, scale};
}

Transform Relative(Transform const& child, Transform const& parent) {
    return Compose(child, Inverse(parent));
}

Vec3 TransformPoint(Affine const& a, Vec3 p) {
    return TransformDirection(a, p) + Vec3{a.m[3][0], a.m[3][1], a.m[3][2]};
}

Vec3 TransformDirection(Affine const& a, Vec3 v) {
    return {
        v.x * a.m[0][0] + v.y * a.m[1][0] + v.z * a.m[2][0],
        v.x * a.m[0][1] + v.y * a.m[1][1] + v.z * a.m[2][1],
        v.x * a.m[0][2] + v.y * a.m[1][2] + v.z * a.m[2][2],
    };
}

double Determinant3(Affine const& a) {
    auto const& m = a.m;
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
        m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

std::optional<Affine> InverseAffine(Affine const& a) {
    auto const& m = a.m;
    double const det = Determinant3(a);
    double const scale = std::max({std::abs(m[0][0]), std::abs(m[0][1]), std::abs(m[0][2]), std::abs(m[1][0]),
        std::abs(m[1][1]), std::abs(m[1][2]), std::abs(m[2][0]), std::abs(m[2][1]), std::abs(m[2][2])});
    if (!(std::abs(det) > 1e-12 * scale * scale * scale) || !std::isfinite(det)) {
        return std::nullopt;
    }
    double const k = 1.0 / det;
    Affine out;
    out.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * k;
    out.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * k;
    out.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * k;
    out.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * k;
    out.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * k;
    out.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * k;
    out.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * k;
    out.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * k;
    out.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * k;
    out.m[0][3] = out.m[1][3] = out.m[2][3] = 0.0;
    // p = (p' - o) * A^-1, so the new origin is -o * A^-1.
    Vec3 const origin = TransformDirection(out, {-m[3][0], -m[3][1], -m[3][2]});
    out.m[3][0] = origin.x;
    out.m[3][1] = origin.y;
    out.m[3][2] = origin.z;
    out.m[3][3] = 1.0;
    return out;
}

std::optional<Quat> RotationOf(Affine const& a, double tolerance) {
    Vec3 const rows[3] = {{a.m[0][0], a.m[0][1], a.m[0][2]}, {a.m[1][0], a.m[1][1], a.m[1][2]}, {a.m[2][0], a.m[2][1], a.m[2][2]}};
    double const lengths[3] = {Length(rows[0]), Length(rows[1]), Length(rows[2])};
    double const scale = (lengths[0] + lengths[1] + lengths[2]) / 3.0;
    if (!(scale > 1e-9) || !std::isfinite(scale)) {
        return std::nullopt;
    }
    for (double const length : lengths) {
        if (std::abs(length - scale) > tolerance * scale) {
            return std::nullopt;  // uneven scale
        }
    }
    Vec3 const x = rows[0] * (1.0 / lengths[0]);
    Vec3 const y = rows[1] * (1.0 / lengths[1]);
    Vec3 const z = rows[2] * (1.0 / lengths[2]);
    if (std::abs(Dot(x, y)) > tolerance || std::abs(Dot(y, z)) > tolerance || std::abs(Dot(z, x)) > tolerance) {
        return std::nullopt;  // shear
    }
    if (Dot(Cross(x, y), z) < 0.0) {
        return std::nullopt;  // mirrored
    }
    Vec3 const yOrtho = ik::Normalize(y - x * Dot(x, y));
    return FromAxes(x, yOrtho, Cross(x, yOrtho));
}

} // namespace mevr::ik

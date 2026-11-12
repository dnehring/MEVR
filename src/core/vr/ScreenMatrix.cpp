// SPDX-License-Identifier: MIT
#include "core/vr/ScreenMatrix.hpp"

#include <cmath>

namespace mevr::vr::screen_matrix {
namespace {

using Mat4d = std::array<double, 16>;

Mat4d ToDouble(Mat4 const& m) {
    Mat4d d{};
    for (int i = 0; i < 16; ++i) {
        d[i] = m[i];
    }
    return d;
}

Mat4d MulD(Mat4d const& a, Mat4d const& b) {
    Mat4d o{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            double v = 0.0;
            for (int k = 0; k < 4; ++k) {
                v += a[r * 4 + k] * b[k * 4 + c];
            }
            o[r * 4 + c] = v;
        }
    }
    return o;
}

// Gauss-Jordan with partial pivoting.
std::optional<Mat4d> InvD(Mat4d m) {
    Mat4d inv{};
    for (int i = 0; i < 4; ++i) {
        inv[i * 4 + i] = 1.0;
    }
    double scale = 0.0;
    for (double const v : m) {
        scale = std::fmax(scale, std::fabs(v));
    }
    if (!(scale > 0.0) || !std::isfinite(scale)) {
        return std::nullopt;
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r) {
            if (std::fabs(m[r * 4 + col]) > std::fabs(m[pivot * 4 + col])) {
                pivot = r;
            }
        }
        if (std::fabs(m[pivot * 4 + col]) < 1e-12 * scale) {
            return std::nullopt;
        }
        if (pivot != col) {
            for (int c = 0; c < 4; ++c) {
                std::swap(m[pivot * 4 + c], m[col * 4 + c]);
                std::swap(inv[pivot * 4 + c], inv[col * 4 + c]);
            }
        }
        double const d = m[col * 4 + col];
        for (int c = 0; c < 4; ++c) {
            m[col * 4 + c] /= d;
            inv[col * 4 + c] /= d;
        }
        for (int r = 0; r < 4; ++r) {
            if (r == col) {
                continue;
            }
            double const f = m[r * 4 + col];
            if (f == 0.0) {
                continue;
            }
            for (int c = 0; c < 4; ++c) {
                m[r * 4 + c] -= f * m[col * 4 + c];
                inv[r * 4 + c] -= f * inv[col * 4 + c];
            }
        }
    }
    return inv;
}

std::optional<Mat4d> ScreenToClipD(Mat4 const& tvp) {
    // TVP[r][3] = TV[r][2] and TVP[r][2] = TV[r][2] P22 (+ P32 in row 3, where TV has no translation).
    int best = 0;
    for (int r = 1; r < 3; ++r) {
        if (std::fabs(tvp[r * 4 + 3]) > std::fabs(tvp[best * 4 + 3])) {
            best = r;
        }
    }
    double const w = tvp[best * 4 + 3];
    if (std::fabs(w) < 1e-6 || std::fabs(tvp[15]) > 1e-4) {
        return std::nullopt;
    }
    double const p22 = tvp[best * 4 + 2] / w;
    double const p32 = tvp[14];
    // The other rows must agree, or this isn't a projection of that shape.
    for (int r = 0; r < 3; ++r) {
        double const expect = tvp[r * 4 + 3] * p22;
        if (std::fabs(tvp[r * 4 + 2] - expect) > 1e-3 * (1.0 + std::fabs(tvp[r * 4 + 2]))) {
            return std::nullopt;
        }
    }
    if (std::fabs(p32) < 1e-9) {
        return std::nullopt;
    }
    Mat4d s{1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, p22, 1,
            0, 0, p32, 0};
    return s;
}

} // namespace

Mat4 Multiply(Mat4 const& a, Mat4 const& b) {
    Mat4d const o = MulD(ToDouble(a), ToDouble(b));
    Mat4 f{};
    for (int i = 0; i < 16; ++i) {
        f[i] = static_cast<float>(o[i]);
    }
    return f;
}

std::optional<Mat4> Inverse(Mat4 const& m) {
    auto const inv = InvD(ToDouble(m));
    if (!inv) {
        return std::nullopt;
    }
    Mat4 f{};
    for (int i = 0; i < 16; ++i) {
        f[i] = static_cast<float>((*inv)[i]);
    }
    return f;
}

std::optional<Mat4> ScreenToClip(Mat4 const& tvp) {
    auto const s = ScreenToClipD(tvp);
    if (!s) {
        return std::nullopt;
    }
    Mat4 f{};
    for (int i = 0; i < 16; ++i) {
        f[i] = static_cast<float>((*s)[i]);
    }
    return f;
}

std::optional<Mat4> SecondEyeCorrection(Mat4 const& tvp1, Mat4 const& tvp2, float const pvt1[3], float const pvt2[3]) {
    auto const s1 = ScreenToClipD(tvp1);
    auto const s2 = ScreenToClipD(tvp2);
    if (!s1 || !s2) {
        return std::nullopt;
    }
    auto const s1Inv = InvD(*s1);
    // Sub = T(pvt2 - pvt1) x TVP2: rows 0-2 of TVP2, row 3 += t.x row0 + t.y row1 + t.z row2.
    Mat4d sub = ToDouble(tvp2);
    double const t[3] = {static_cast<double>(pvt2[0]) - pvt1[0], static_cast<double>(pvt2[1]) - pvt1[1],
                         static_cast<double>(pvt2[2]) - pvt1[2]};
    for (int c = 0; c < 4; ++c) {
        sub[12 + c] += t[0] * sub[c] + t[1] * sub[4 + c] + t[2] * sub[8 + c];
    }
    auto const subInv = InvD(sub);
    if (!s1Inv || !subInv) {
        return std::nullopt;
    }
    Mat4d const c = MulD(MulD(MulD(*s2, *subInv), ToDouble(tvp1)), *s1Inv);
    Mat4 f{};
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(c[i])) {
            return std::nullopt;
        }
        f[i] = static_cast<float>(c[i]);
    }
    return f;
}

void Apply(Mat4 const& c, float* m, bool rows) {
    Mat4d const cd = ToDouble(c);
    Mat4d md{};
    for (int i = 0; i < 16; ++i) {
        md[i] = m[i];
    }
    Mat4d out{};
    if (rows) {
        out = MulD(cd, md);  // C x M
    } else {
        // Stored transposed: M^T in memory; (C x M)^T = M^T x C^T.
        Mat4d ct{};
        for (int r = 0; r < 4; ++r) {
            for (int k = 0; k < 4; ++k) {
                ct[r * 4 + k] = cd[k * 4 + r];
            }
        }
        out = MulD(md, ct);
    }
    for (int i = 0; i < 16; ++i) {
        m[i] = static_cast<float>(out[i]);
    }
}

} // namespace mevr::vr::screen_matrix

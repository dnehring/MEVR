// SPDX-License-Identifier: MIT
#include "core/ik/ClubBoost.hpp"

#include <algorithm>

namespace mevr::ik::club {

std::uint64_t StableHash(std::string_view text) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    // splitmix64's finalizer: FNV's low bits alone are poorly spread for short, similar keys.
    h += 0x9E3779B97F4A7C15ull;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    return h ^ (h >> 31);
}

bool InClub(std::string_view lowerPath, std::span<std::string const> parts) {
    for (std::string const& part : parts) {
        if (!part.empty() && lowerPath.find(part) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

bool FemaleBody(std::string_view lowerMeshPath) {
    // The mesh's own name (after the last dot): a rigged stand-in keeps it ("mevr_bodies_le2.hmf_arm_cthd_mdl_mevr").
    std::size_t const dot = lowerMeshPath.rfind('.');
    std::string_view const name = dot == std::string_view::npos ? lowerMeshPath : lowerMeshPath.substr(dot + 1);
    return name.rfind("hmf_", 0) == 0 || name.rfind("asa_", 0) == 0;
}

namespace {

// Two independent uniforms in [0, 1] from a person's key.
void Units(std::string_view key, double& u1, double& u2) {
    std::uint64_t const h = StableHash(key);
    u1 = static_cast<double>((h >> 8) & 0xFFFFFu) / static_cast<double>(0xFFFFFu);
    u2 = static_cast<double>((h >> 36) & 0xFFFFFu) / static_cast<double>(0xFFFFFu);
}

double Cap(double breastMax, SizeSettings const& settings) {
    return std::clamp(breastMax > 0.0 ? breastMax : settings.breastMaxDefault, 1.0, 1.8);
}

} // namespace

Sizes VariationFor(std::string_view key, double breastMax, double clubBreastA, double breastVary, double gluteVary,
    SizeSettings const& settings) {
    double u1 = 0.0;
    double u2 = 0.0;
    Units(key, u1, u2);
    double const bv = std::clamp(breastVary, 0.0, 0.5);
    double const gv = std::clamp(gluteVary, 0.0, 0.5);
    double const baked = std::max(clubBreastA, 0.0);
    Sizes s;
    s.breast = (2.0 * u1 - 1.0) * bv;
    // (1 + baked) (1 + a) <= cap
    double const room = Cap(breastMax, settings) / (1.0 + baked) - 1.0;
    s.breast = std::max(std::min(s.breast, room), -0.5);
    s.glute = (2.0 * u2 - 1.0) * gv;
    return s;
}

Sizes FallbackSizes(std::string_view key, double breastMax, SizeSettings const& settings, double breastLimit, double gluteLimit) {
    Sizes s = SizesFor(key, breastMax, settings);
    double const capA = Cap(breastMax, settings) - 1.0;
    double const breastTop = std::clamp(breastLimit, 0.0, 0.8);
    if (capA > 1e-9 && capA > breastTop) {
        s.breast *= breastTop / capA;
    }
    double const high = std::max(settings.gluteLow, settings.gluteHigh);
    double const gluteTop = std::clamp(gluteLimit, 0.0, 0.4);
    if (high > 1e-9 && high > gluteTop) {
        s.glute *= gluteTop / high;
    }
    return s;
}

Sizes SizesFor(std::string_view key, double breastMax, SizeSettings const& settings) {
    double u1 = 0.0;
    double u2 = 0.0;
    Units(key, u1, u2);
    double const cap = Cap(breastMax, settings);
    double const least = std::clamp(settings.breastMin, 0.0, 1.0);
    Sizes s;
    s.breast = (least + u1 * (1.0 - least)) * (cap - 1.0);
    double const low = std::min(settings.gluteLow, settings.gluteHigh);
    double const high = std::max(settings.gluteLow, settings.gluteHigh);
    double const mid = std::clamp(settings.gluteMid, low, high);
    double const glute = u2 < 0.5 ? low + 2.0 * u2 * (mid - low) : mid + (2.0 * u2 - 1.0) * (high - mid);
    s.glute = std::clamp(glute, 0.0, 0.4);
    return s;
}

Vec3 PivotLocal(Transform const& bind, Vec3 offset) {
    double const scale = bind.scale > 1e-9 ? bind.scale : 1.0;
    return Rotate(Conjugate(Normalize(bind.rotation)), offset) * (1.0 / scale);
}

Transform Grow(Transform const& bone, double a, Vec3 pivotLocal) {
    double const s = 1.0 + a;
    Transform out = bone;
    out.scale = bone.scale * s;
    // Q = T + R (scale q); keeping Q: T' = T + (1 - s) R (scale q).
    out.translation = bone.translation + Rotate(bone.rotation, pivotLocal * bone.scale) * (1.0 - s);
    return out;
}

} // namespace mevr::ik::club

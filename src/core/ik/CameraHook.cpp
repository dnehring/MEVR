#include "core/ik/CameraHook.hpp"

#include <cctype>
#include <cmath>
#include <string_view>

namespace mevr::ik {
namespace {

bool EqualsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<int> FindCameraHookBones(std::span<std::string const> names) {
    std::vector<int> bones;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (EqualsNoCase(names[i], "Camera") || EqualsNoCase(names[i], "God")) {
            bones.push_back(static_cast<int>(i));
        }
    }
    return bones;
}

std::vector<int> InSubtrees(std::span<int const> parents, std::span<int const> roots, std::span<int const> bones) {
    int const count = static_cast<int>(parents.size());
    std::vector<int> inside;
    for (int bone : bones) {
        if (bone < 0 || bone >= count) {
            continue;
        }
        bool found = false;
        // Walk up to the root; a chain longer than the skeleton is a cycle.
        for (int current = bone, guard = 0; current >= 0 && current < count && guard <= count && !found;
             current = parents[current], ++guard) {
            for (int root : roots) {
                found = found || (root >= 0 && current == root);
            }
        }
        if (found) {
            inside.push_back(bone);
        }
    }
    return inside;
}

Vec3 CameraFromHook(Vec3 hookBone, Vec3 hookOffset, Vec3 offset, double pitch, double yaw) {
    double const cp = std::cos(pitch), sp = std::sin(pitch), cy = std::cos(yaw), sy = std::sin(yaw);
    // FRotationMatrix(pitch, yaw, 0): X = forward, Y = right, Z = up; v' = v.x X + v.y Y + v.z Z.
    Vec3 const x{cp * cy, cp * sy, sp};
    Vec3 const y{-sy, cy, 0.0};
    Vec3 const z{-sp * cy, -sp * sy, cp};
    Vec3 const levelX{cy, sy, 0.0};
    Vec3 const up{0.0, 0.0, 1.0};
    Vec3 const hook = hookBone + levelX * hookOffset.x + y * hookOffset.y + up * hookOffset.z;
    return hook - (x * offset.x + y * offset.y + z * offset.z);
}

Vec3 InViewAxes(Vec3 v, double yaw) {
    double const cy = std::cos(yaw), sy = std::sin(yaw);
    return {v.x * cy + v.y * sy, -v.x * sy + v.y * cy, v.z};
}

bool CameraModeChanged(CameraModeKey const& a, CameraModeKey const& b, double tolerance) {
    return a.mode != b.mode || a.hookName != b.hookName || Length(a.hookOffset - b.hookOffset) > tolerance ||
        Length(a.offset - b.offset) > tolerance;
}

} // namespace mevr::ik

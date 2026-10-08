#include "core/ik/HandFrame.hpp"

#include <cmath>
#include <numbers>

namespace mevr::ik {

std::optional<Quat> AnatomyInHand(Vec3 middleLocal, Vec3 indexLocal, Vec3 pinkLocal) {
    double const reach = Length(middleLocal);
    Vec3 const across = indexLocal - pinkLocal;
    if (reach < 1e-3 || Length(across) < 1e-3) {
        return std::nullopt;
    }
    Vec3 const x = middleLocal * (1.0 / reach);
    Vec3 const side = across - x * Dot(across, x);
    if (Length(side) < 0.2 * Length(across)) {
        return std::nullopt;  // knuckles nearly in line with the fingers: no reliable thumb side
    }
    Vec3 const y = Normalize(side);
    return FromAxes(x, y, Cross(x, y));
}

Quat GripTrim(double pitchDegrees, double yawDegrees, double rollDegrees) {
    auto const about = [](Vec3 axis, double degrees) {
        double const half = degrees * std::numbers::pi / 360.0;
        double const s = std::sin(half);
        return Quat{axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
    };
    // Rotate() is active and right-handed in components: +pitch about +Y takes X toward -Z (down);
    // +yaw about +Z takes X toward +Y (right, in UE's axes); +roll about +X takes Z toward -Y, so
    // turn it the other way to roll the top (thumb side) to the right.
    return Normalize(about({0.0, 0.0, 1.0}, yawDegrees) * about({0.0, 1.0, 0.0}, pitchDegrees) *
        about({1.0, 0.0, 0.0}, -rollDegrees));
}

Quat HandFromGrip(Quat grip, Quat trim, Quat anatomyInHand) {
    // The grip's anatomical frame: fingers along its X, thumb side along its Z, so the third axis
    // (X cross Z) is its -Y.
    Quat const anatomyInGrip = FromAxes({1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, -1.0, 0.0});
    return Normalize(grip * trim * anatomyInGrip * Conjugate(anatomyInHand));
}

namespace {

// The rotation taking X to `forward` and Z to `up` made perpendicular to it (Y = Z cross X).
std::optional<Quat> ForwardUpFrame(Vec3 forward, Vec3 up) {
    double const forwardLength = Length(forward);
    double const upLength = Length(up);
    if (!(forwardLength > 1e-9) || !(upLength > 1e-9)) {
        return std::nullopt;
    }
    Vec3 const x = forward * (1.0 / forwardLength);
    Vec3 const across = up - x * Dot(up, x);
    // sin(3 degrees): an up this close to the barrel says nothing about the roll.
    if (Length(across) < 0.05 * upLength) {
        return std::nullopt;
    }
    Vec3 const z = Normalize(across);
    return FromAxes(x, Cross(z, x), z);
}

} // namespace

std::optional<Quat> HandForBarrel(Vec3 barrelInHand, Vec3 upInHand, Vec3 aim, Vec3 up) {
    auto const held = ForwardUpFrame(barrelInHand, upInHand);
    auto const wanted = ForwardUpFrame(aim, up);
    if (!held || !wanted) {
        return std::nullopt;
    }
    // hand * held = wanted: the hand turns the object's frame onto the wanted one.
    return Normalize(*wanted * Conjugate(*held));
}

} // namespace mevr::ik

// SPDX-License-Identifier: MIT
// Offline tests for the body's spheres (core/ik/BodySpheres): the rig tool's file read, bound to a skeleton's bind pose, and
// carried by a bone that turns and moves; a bone the skeleton lacks left out; malformed files refused. Made-up data.

#include "core/ik/BodySpheres.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/BodySpheresTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace ik = mevr::ik;
namespace bs = mevr::ik::body_spheres;

bool Near(ik::Vec3 a, ik::Vec3 b, double tolerance) {
    return std::abs(a.x - b.x) < tolerance && std::abs(a.y - b.y) < tolerance && std::abs(a.z - b.z) < tolerance;
}

void TestParseBindPlace() {
    std::string const file = R"({"format": "mevr-body-spheres", "version": 1, "mesh": "Body", "spheres": [
        {"bone": "Chest2", "c": [-10, 0, 140], "r": 5},
        {"bone": "Neck", "c": [0, 0, 160], "r": 3},
        {"bone": "Tail", "c": [0, 0, 0], "r": 1}]})";
    std::string error;
    auto const fitted = bs::Parse(file, &error);
    CHECK(fitted.has_value() && fitted->size() == 3);
    if (!fitted) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    // Two bones: Chest2 at (0, 0, 140), Neck at (0, 0, 155), both unrotated in the bind pose.
    std::vector<std::string> const names{"Chest2", "Neck"};
    std::vector<ik::Transform> bind(2);
    bind[0].translation = {0.0, 0.0, 140.0};
    bind[1].translation = {0.0, 0.0, 155.0};
    int missing = 0;
    std::vector<bs::Sphere> const spheres = bs::Bind(*fitted, names, bind, &missing);
    CHECK(spheres.size() == 2 && missing == 1);
    CHECK(Near(spheres[0].local, {-10.0, 0.0, 0.0}, 1e-9));
    // The chest turned 90 degrees about Z and moved 1 m along X (world in metres, 100 units a metre): the sphere 10 cm behind
    // it is now 10 cm along -Y from the bone.
    std::vector<ik::Transform> world(2);
    double const h = 3.14159265358979323846 / 4.0;
    world[0].rotation = {0.0, 0.0, std::sin(h), std::cos(h)};
    world[0].translation = {1.0, 0.0, 1.4};
    world[1].translation = {1.0, 0.0, 1.55};
    std::vector<ik::hand_contact::Capsule> out;
    bs::Place(spheres, world, 0.01, out);
    CHECK(out.size() == 2);
    if (out.size() == 2) {
        CHECK(Near(out[0].a, {1.0, -0.10, 1.4}, 1e-9) && Near(out[0].a, out[0].b, 1e-12));
        CHECK(std::abs(out[0].radius - 0.05) < 1e-12);
        CHECK(Near(out[1].a, {1.0, 0.0, 1.6}, 1e-9));
    }
}

void TestRefusals() {
    CHECK(!bs::Parse(R"({"format": "other", "version": 1, "spheres": []})").has_value());
    CHECK(!bs::Parse(R"({"format": "mevr-body-spheres", "version": 2, "spheres": []})").has_value());
    CHECK(!bs::Parse(R"({"format": "mevr-body-spheres", "version": 1, "spheres": [{"bone": "A", "c": [0, 0], "r": 1}]})").has_value());
    CHECK(!bs::Parse(R"({"format": "mevr-body-spheres", "version": 1, "spheres": [{"bone": "A", "c": [0, 0, 0], "r": 0}]})").has_value());
}

} // namespace

int RunBodySpheresTests() {
    TestParseBindPlace();
    TestRefusals();
    std::printf("body spheres tests: %d failure(s)\n", gFailures);
    return gFailures;
}

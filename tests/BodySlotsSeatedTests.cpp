// SPDX-License-Identifier: MIT
#include "core/ik/BodySlots.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>

namespace {
int failures = 0;
void Check(bool ok, char const* what, int line) {
    if (!ok) { std::printf("FAIL tests/BodySlotsSeatedTests.cpp:%d: %s\n", line, what); ++failures; }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)
bool NearD(double a, double b, double epsilon) { return std::abs(a - b) <= epsilon; }
bool NearIk(mevr::ik::Vec3 a, mevr::ik::Vec3 b) { return mevr::ik::Length(a - b) < 1e-8; }
}

int RunBodySlotsSeatedTests() {
    using namespace mevr::ik;
    using namespace mevr::ik::body_slots;
    double const left90 = std::numbers::pi / 2.0;
    // Seated reach is independent of standing height / avatar scale and of the floor's origin.
    // A 1.2 m seated head puts the pistol at 0.78 m, ahead of the seat rather than behind the hip.
    Layout standing = DefaultLayout();
    standing.eyeHeight = 2.2;
    Layout seated = SeatedLayout(standing);
    Frame chair{{0.0, 1.2, 0.0}, 0.0};
    CHECK(NearD(Scale(seated), 1.0, 1e-9));
    CHECK(NearIk(SlotCenter(chair, seated.slots[0], Scale(seated)), {0.28, 0.78, -0.18}));
    CHECK(NearIk(seated.slots[2].offset, {-0.28, -0.42, 0.18}));
    standing.eyeHeight = 0.9;
    CHECK(NearIk(SeatedLayout(standing).slots[0].offset, seated.slots[0].offset));
    standing.eyeHeight = std::numeric_limits<double>::quiet_NaN();
    CHECK(NearD(Scale(SeatedLayout(standing)), 1.0, 1e-9));
    Frame local{{1.0, -0.3, 2.0}, left90};
    CHECK(NearIk(SlotCenter(local, seated.slots[0], 1.0), {0.82, -0.72, 1.72}));
    // Lean/recenter moves every reach point with the live neutral eye, with no floor correction.
    Frame moved = chair;
    moved.anchor = moved.anchor + Vec3{0.1, -0.15, 0.2};
    for (int i = 0; i < kSlots; ++i) {
        CHECK(NearIk(SlotCenter(moved, seated.slots[i], 1.0) - SlotCenter(chair, seated.slots[i], 1.0),
            {0.1, -0.15, 0.2}));
        CHECK(seated.slots[i].weapon == standing.slots[i].weapon);
    }
    // Remapped and disabled slots survive the seated geometry change.
    standing.slots[0].weapon = Weapon::None;
    standing.slots[2].weapon = Weapon::Pistol;
    CHECK(SeatedLayout(standing).slots[0].weapon == Weapon::None);
    CHECK(SeatedLayout(standing).slots[2].weapon == Weapon::Pistol);
    CHECK(NearIk(DefaultLayout().slots[0].offset, {0.20, -0.62, -0.05}));
    CHECK(NearD(DefaultLayout().slots[0].radius, 0.14, 1e-9));
    // Default grab spheres don't overlap each other or the grenade; normal cheek aim is outside.
    Slot const grenade = SeatedGrenadeSlot();
    Vec3 const aim = chair.anchor + Vec3{0.15, -0.15, -0.10};
    for (int i = 0; i < kSlots; ++i) {
        Vec3 const c = SlotCenter(chair, seated.slots[i], 1.0);
        CHECK(Length(c - chair.anchor) > seated.slots[i].radius);
        CHECK(Length(c - aim) > seated.slots[i].radius + seated.exitMargin);
        CHECK(Length(c - SlotCenter(chair, grenade, 1.0)) > seated.slots[i].radius + grenade.radius);
        for (int j = i + 1; j < kSlots; ++j)
            CHECK(Length(c - SlotCenter(chair, seated.slots[j], 1.0)) > seated.slots[i].radius + seated.slots[j].radius);
    }
    // The larger zone provides earlier entry/contact and draws only on a fresh squeeze.
    Input reach;
    reach.distance[0] = 0.17;  // outside the old 0.14 m hip zone
    reach.radius[0] = seated.slots[0].radius;
    reach.weapon[0] = Weapon::Pistol;
    reach.available[0] = true;
    Machine assisted;
    auto step = assisted.Update(reach);
    CHECK(step.entered == 0 && step.action == Action::None);
    CHECK(Contact(Depth(reach.distance[0], reach.radius[0]), 0.5, false).amplitude > 0.0f);
    reach.newSqueeze = reach.gripDown = true;
    CHECK(assisted.Update(reach).action == Action::Draw);
    reach.newSqueeze = false;
    CHECK(assisted.Update(reach).action != Action::Draw);  // held grip never repeats a draw
    return failures;
}

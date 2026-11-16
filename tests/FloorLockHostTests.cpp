// SPDX-License-Identifier: MIT
// Exercise the host Queue/Translate implementation with the real floor solve,
// HostView publication and seated calibration math. No renderer/game is started.
#ifndef MEVR_WITH_MAIN_MENU
#define MEVR_WITH_MAIN_MENU
#endif
#include "plugin/FloorLock.cpp"

#include <cstdio>

namespace {
namespace host = mevr::host::floor_lock;
namespace fl = mevr::vr::floor_lock;
namespace hv = mevr::vr::host_view;
namespace floor_vr = mevr::vr::floor;
bool sharedMenu = false, liveAdjustment = false, ownMenu = false, calibrating = false;
bool firstPerson = true;
std::uint64_t armSequence = 1;
int failures = 0;
void Check(bool pass, int line) {
    if (!pass) { ++failures; std::printf("FloorLockHostTests:%d failed\n", line); }
}
#define CHECK(value) Check((value), __LINE__)
bool Near(double a, double b) { return std::abs(a - b) < 0.0001; }

struct Fixture {
    hv::State view;
    floor_vr::State floor;
    fl::PawnSample pawn;
    float rawZ = 171.5f; // native camera: 1.43 m above the pawn's floor
    bool ordinary = true;
    Fixture() {
        host::Reset();
        // Inject the backing setting, not a replacement Queue/Translate policy.
        // No user INI is read or changed by this test.
        host::gEnabled = true;
        host::gSeatedMotion.Reset();
        sharedMenu = liveAdjustment = ownMenu = calibrating = false;
        firstPerson = true;
        armSequence = 1;
        view.valid = view.headLook = true;
        view.head = {{0, 0, 0, 1}, {0, 1.7526f, 0}};
        view.unitsPerMeter = 50;
        view.offsetRight = 2;
        view.offsetUp = 3;
        view.offsetForward = 4;
        floor.valid = true;
        floor.eyeHeight = 1.7526f;
        floor.source = floor_vr::Source::Stage;
        floor.sequence = 1;
        floor.sessions = floor.recenters = 1;
        pawn.onFoot = pawn.pawn = pawn.cameraOwned = pawn.floorValid = true;
        pawn.cover = false;
        pawn.floorZ = 100;
        Fresh();
    }
    void Fresh() {
        auto const now = GetTickCount64();
        view.timeMs = floor.timeMs = pawn.timeMs = now;
    }
    void Queue() {
        host::SetPawn(&pawn);
        host::Queue(view, floor, ordinary);
    }
    float Translate(float& right, float& up, float& forward, bool replay = false) const {
        return host::Translate(replay, rawZ, 0, 1, 0, &right, &up, &forward);
    }
    hv::State Solve() {
        Queue();
        float right = -90, up = -91, forward = -92;
        auto const correction = Translate(right, up, forward);
        auto const published = hv::Latest();
        CHECK(Near(right, view.offsetRight) && Near(up, view.offsetUp) && Near(forward, view.offsetForward));
        CHECK(Near(published.centreZ, rawZ + up + correction));
        return published;
    }
};
void Corrected(Fixture const& f, hv::State const& view, double height) {
    CHECK(view.valid && view.floorLocked && view.centreValid);
    CHECK(Near((view.centreZ - f.pawn.floorZ) / f.view.unitsPerMeter, height));
    CHECK(Near(view.pawnFloorZ, f.pawn.floorZ));
    CHECK(view.floorSequence == f.floor.sequence);
}
} // namespace

namespace mevr::host {
std::wstring const& SettingsPath() noexcept { static std::wstring const path; return path; }
}
namespace mevr::host::renderer {
bool OwnMenuOpen() noexcept { return ownMenu; }
bool FirstPersonOwned() noexcept { return firstPerson; }
std::uint64_t HeadArmSequence() noexcept { return armSequence; }
}
namespace mevr::host::main_menu {
bool IsOpen() { return sharedMenu; }
bool LiveAdjusting() { return liveAdjustment; }
}
namespace mevr::host::calibration {
bool Active() { return calibrating; }
}
namespace mevr::log {
void Write(std::string_view, std::string_view) {}
}

int main() {
    {
        Fixture f;
        auto const closed = f.Solve();
        Corrected(f, closed, 1.7526);
        sharedMenu = true;
        f.Fresh();
        auto const open = f.Solve();
        Corrected(f, open, 1.7526); // fails with the former shared-menu blocking gate
        CHECK(Near(open.centreZ, closed.centreZ) && Near(open.floorCorrection, closed.floorCorrection));
        liveAdjustment = true;
        f.Fresh();
        auto const adjusting = f.Solve();
        Corrected(f, adjusting, 1.7526);
        CHECK(Near(adjusting.centreZ, closed.centreZ));
        liveAdjustment = sharedMenu = false;
        f.Fresh();
        Corrected(f, f.Solve(), 1.7526);
    }
    {
        Fixture f;
        sharedMenu = true;
        Corrected(f, f.Solve(), 1.7526);
        f.pawn.floorZ = 120;
        f.floor.eyeHeight = 1.6f;
        ++f.floor.sequence;
        f.view.head.position = {0.2f, 1.6f, -0.3f};
        f.view.offsetUp = 7;
        f.view.offsetForward = 10;
        f.rawZ = 188;
        f.Fresh();
        auto const moved = f.Solve();
        Corrected(f, moved, 1.6);
        CHECK(Near(moved.head.position.x, 0.2) && Near(moved.head.position.y, 1.6));
        CHECK(Near(moved.floorCorrection, 5)); // fresh pawn floor + head, not a held old centre
        host::SetManualHeight(0.1f);
        f.Fresh();
        Corrected(f, f.Solve(), 1.7);
    }
    // Every rejection uses the actual host gate and actual core Correct(). The
    // shared menu is open throughout: removing its gate must not remove others.
    for (int rejection = 0; rejection < 13; ++rejection) {
        Fixture f;
        sharedMenu = true;
        if (rejection == 0) ownMenu = true;
        if (rejection == 1) calibrating = true;
        if (rejection == 2) f.ordinary = false;
        if (rejection == 3) firstPerson = false;
        auto const now = GetTickCount64();
        if (rejection == 4) f.pawn.timeMs = now > 1000 ? now - 1000 : 0;
        if (rejection == 5) f.floor.timeMs = now > 1000 ? now - 1000 : 0;
        if (rejection == 6) f.pawn.floorValid = false;
        if (rejection == 7) f.floor.valid = false;
        if (rejection == 8) f.pawn.pawn = false;
        if (rejection == 9) f.pawn.cover = true;
        if (rejection == 10) f.pawn.cameraOwned = false;
        if (rejection == 11) f.view.valid = false;
        if (rejection == 12) f.pawn.onFoot = false;
        auto const rejected = f.Solve();
        CHECK(!rejected.floorLocked && Near(rejected.floorCorrection, 0));
    }
    {
        Fixture f;
        sharedMenu = true;
        f.floor.source = floor_vr::Source::LocalFloor;
        Corrected(f, f.Solve(), 1.7526);
        f.floor.source = floor_vr::Source::None;
        f.Fresh();
        CHECK(!f.Solve().floorLocked);
    }
    {
        Fixture f;
        f.pawn.seated = true;
        f.pawn.standingEyeM = 1.7526;
        f.floor.eyeHeight = f.view.head.position.y = 1.43f;
        Corrected(f, f.Solve(), 1.7526);
        sharedMenu = true;
        f.Fresh();
        Corrected(f, f.Solve(), 1.7526); // real SeatedMotion baseline survives opening the overlay
    }
    {
        Fixture f;
        auto const first = f.Solve();
        auto const firstSequence = first.sequence;
        // A newer publication and menu change between stereo passes must not
        // mix the next frame's floor/head with the first eye's correction.
        f.pawn.floorZ = 150;
        f.floor.eyeHeight = 1.9f;
        ++f.floor.sequence;
        f.view.offsetUp = 11;
        f.view.offsetRight = 12;
        f.view.offsetForward = 13;
        sharedMenu = true;
        f.Fresh();
        f.Queue();
        float right = -1, up = -2, forward = -3;
        auto const replayCorrection = f.Translate(right, up, forward, true);
        CHECK(Near(replayCorrection, first.floorCorrection));
        CHECK(Near(right, 2) && Near(up, 3) && Near(forward, 4));
        CHECK(hv::Latest().sequence == firstSequence); // replay never republishes the newer pending view
        f.Fresh();
        Corrected(f, f.Solve(), 1.9);
    }
    {
        Fixture f;
        sharedMenu = true;
        host::gEnabled = false;
        f.Queue();
        CHECK(!host::Enabled());
        float right = 8, up = 9, forward = 10;
        CHECK(Near(f.Translate(right, up, forward), 0));
        CHECK(Near(right, 8) && Near(up, 9) && Near(forward, 10));
        auto const legacy = hv::Latest();
        CHECK(legacy.valid && !legacy.floorLocked && !legacy.centreValid);
    }
    host::Reset();
    CHECK(!hv::Latest().valid);
    std::printf("FloorLock host: %s (shared overlay, fresh solve, admission, stereo latch, disabled path)\n",
        failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

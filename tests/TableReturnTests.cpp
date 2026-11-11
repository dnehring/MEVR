// SPDX-License-Identifier: MIT
#include "core/vr/GalaxyMap.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include "core/vr/PresentationHold.hpp"
#include "core/vr/WorldViewHold.hpp"

#include <cstdio>

namespace {
int failures = 0;
void Check(bool value, char const* expression, int line) {
    if (!value) {
        std::printf("FAIL TableReturnTests.cpp:%d: %s\n", line, expression);
        ++failures;
    }
}
#define CHECK(x) Check((x), #x, __LINE__)
}

int RunTableReturnTests() {
    namespace gm = mevr::vr::galaxy_map;
    namespace gh = mevr::vr::galaxy_holo;
    namespace wv = mevr::vr::world_view;
    failures = 0;
    wv::Reference reference;
    CHECK(reference.Observe({11, 21}));
    CHECK(!reference.Available());
    CHECK(!reference.Remember(0, false, false)); // outgoing map camera in a mode-0 closing tick
    CHECK(reference.Remember(0, true, false));
    CHECK(reference.Available());

    // An ordinary opening, all four levels, including the planet page's empty actor list.
    gm::ClosingHold closing;
    CHECK(!closing.Update(6, false, 100));
    CHECK(gm::TableView(6, true, false));
    for (int level = 1; level <= 4; ++level) {
        gh::MapModel model;
        model.state = level;
        auto const scene = gh::BuildScene(model);
        CHECK(scene.level == level && scene.objectCount == 0);
        CHECK(gm::TableView(9, true, false)); // no dependency on object count, map level or ready capture targets
        CHECK(gm::FlashInteraction(level, false) == (level == 4));
        CHECK(gm::FlashInteraction(level, true));
        CHECK(!closing.Update(9, true, 200 + level));
    }
    // Owner's travel: mode 9 -> 8 -> 9 -> conversation 5 -> 9. Rendering/input stop
    // for the movie, but the saved room and the first returning frame still belong to the CIC.
    for (int mode : {8, 9, 5, 9}) {
        bool const suspended = gm::RememberTable(mode);
        CHECK(!reference.Observe({11, 21}));
        CHECK(!reference.Remember(mode, false, false));
        CHECK(reference.Available());
        CHECK(gm::TableView(mode, true, suspended) == (mode == 9));
        CHECK(!gm::TableView(mode, false, suspended)); // explicit flat-map setting remains an opt-out
    }
    CHECK(gm::TableView(9, true, true)); // the mod has not yet seen the return: suspended reservation suffices
    for (int mode : {5, 7, 8, 10, -1, 255}) CHECK(!gm::TableView(mode, true, false));
    CHECK(!gm::TableView(6, true, true)); // travel's engine cinematic is not the closing animation
    CHECK(!gm::TableView(0, true, true));

    // Existing close bridge, unchanged: initial 0 -> fly-out 6 -> final 0 releases.
    closing.Update(9, true, 1000);
    CHECK(closing.Update(0, true, 1010));
    CHECK(closing.Update(6, true, 1030));
    CHECK(!closing.Update(0, true, 1400));
    CHECK(!reference.Remember(0, false, false));
    CHECK(!reference.Remember(0, true, true));
    CHECK(reference.Remember(0, true, false));

    // Loading another world (landing), losing the player, or replacing the controller
    // invalidates all remembered camera/anchor data before any GUI may use it.
    CHECK(reference.Observe({12, 22}));
    CHECK(!reference.Available());
    CHECK(!reference.Remember(7, false, false));
    CHECK(reference.Remember(0, true, false));
    CHECK(reference.Observe({12, 23}));
    CHECK(!reference.Available());
    CHECK(reference.Observe({}));
    CHECK(!reference.Remember(0, true, false));

    // The authorized return bypasses the generic 100 ms movie/VR entry delay.
    mevr::vr::presentation::VrEntry entry;
    CHECK(!entry.Hold(false, 1000));
    CHECK(entry.Hold(true, 1010));
    entry.AcceptWorldUi();
    CHECK(!entry.Hold(true, 1011));

    // The console's use: 4 s as before; a conversation the use began keeps it, and its 4 s run from the conversation's end.
    gm::ConsoleUse use;
    CHECK(!use.Holds(6, 1000));               // never used
    use.Used(10000);
    CHECK(use.Holds(6, 12000));               // the map's opening right after the use
    CHECK(!use.Holds(6, 14001));              // 4 s later, nothing kept it
    gm::ConsoleUse call;
    call.Used(20000);
    for (std::uint64_t t = 20500; t <= 33000; t += 500) CHECK(call.Holds(5, t));  // Hackett's call, 13 s
    CHECK(call.Holds(6, 34000));              // the map's opening after the call
    CHECK(call.Holds(6, 36900));
    CHECK(!call.Holds(6, 37001));             // 4 s after the call's last tick
    gm::ConsoleUse late;
    late.Used(50000);
    CHECK(!late.Holds(5, 55000));             // a conversation that begins after the window isn't the use's
    gm::ConsoleUse capped;
    capped.Used(1000);
    std::uint64_t t = 1000;
    for (; t < 1000 + gm::ConsoleUse::kMaxMs + 2000; t += 1000) {
        if (!capped.Holds(5, t)) break;
    }
    CHECK(t >= 1000 + gm::ConsoleUse::kMaxMs && t < 1000 + gm::ConsoleUse::kMaxMs + 1000);  // never past 3 minutes
    return failures;
}

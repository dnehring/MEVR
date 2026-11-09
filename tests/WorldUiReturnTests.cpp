// SPDX-License-Identifier: MIT
#include "core/vr/PresentationHold.hpp"
#include "core/vr/WorldUi.hpp"
#include "core/vr/WorldViewHold.hpp"

#include <cstdio>

namespace {
int failures = 0;
void Check(bool value, char const* expression, int line) {
    if (!value) {
        std::printf("FAIL WorldUiReturnTests.cpp:%d: %s\n", line, expression);
        ++failures;
    }
}
#define CHECK(x) Check((x), #x, __LINE__)
}

int RunWorldUiReturnTests() {
    namespace wu = mevr::vr::world_ui;
    namespace pr = mevr::vr::presentation;
    failures = 0;
    mevr::vr::world_view::Reference view;
    CHECK(view.Observe({100, 200})); // Normandy
    CHECK(view.Remember(0, true, false));
    CHECK(wu::CanPlaceWithView(wu::Screen::PartySelect, view.Available()));
    CHECK(!view.Remember(0, false, false)); // map fly-out cannot replace Shepard's view
    CHECK(!pr::WorldGuiOwnsView(8, true, false)); // the landing movie still plays

    CHECK(view.Observe({101, 201})); // destination loaded during the movie
    CHECK(!view.Available());
    CHECK(!view.Remember(7, false, false)); // PartySelect before the first foot camera
    CHECK(wu::CanPlaceWithView(wu::Screen::PartySelect, view.Available()));
    CHECK(!wu::CanPlaceWithView(wu::Screen::Inventory, view.Available()));
    CHECK(!wu::CanPlaceWithView(wu::Screen::Shop, view.Available()));
    CHECK(!wu::ShowsFigure(wu::Screen::PartySelect)); // portraits, not the UI world's mannequin
    CHECK(pr::WorldGuiOwnsView(7, true, false)); // admitted screen beats outgoing cinema/movie state
    CHECK(!pr::WorldGuiOwnsView(7, false, false)); // cinematic's GUI blip is not a screen of ours
    CHECK(!pr::WorldGuiOwnsView(7, true, true)); // never override the front end
    for (int mode : {-1, 0, 5, 6, 8, 9, 255}) CHECK(!pr::WorldGuiOwnsView(mode, true, false));
    CHECK(pr::WorldGuiOwnsView(10, true, false)); // same ownership rule for the admitted minigame

    CHECK(!view.Observe({101, 201}));
    CHECK(view.Remember(0, true, false)); // after party selection, only destination foot view is saved
    CHECK(wu::CanPlaceWithView(wu::Screen::Shop, view.Available()));
    CHECK(view.Observe({101, 202})); // even a controller replacement in the same world discards the view
    CHECK(!view.Available());
    return failures;
}

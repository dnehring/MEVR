// SPDX-License-Identifier: MIT
// Compiled independently for all three adapters: identical native IDs must not acquire another game's meaning.
#include "engine/GameAdapter.hpp"
#include <cstdio>

namespace a = mevr::engine::adapter;
int main() {
    int failures = 0;
    auto check = [&](bool ok, char const* name) {
        if (!ok) { std::fprintf(stderr, "%s: %s\n", MEVR_GAME_NAME_A, name); ++failures; }
    };
    for (int mode : {-1, 255, 256, 263, 265}) {
        check(a::GameMode(mode) == a::Mode::Unknown && !a::IsFlatScreenMode(mode) &&
            !a::IsMapMode(mode) && !a::IsWristMenuMode(mode), "invalid/loading mode is inert, never truncated");
    }
    check(a::GameMode(0) == a::Mode::OnFoot && a::IsWristMenuMode(0), "on-foot wrist access");
#if defined(MEVR_GAME_LE3)
    constexpr int conversation = 7, gui = 9, galaxy = 11, orbit = 12;
    check(a::GameMode(2) == a::Mode::Atlas && !a::IsWristMenuMode(2), "Atlas is not a weapon wheel");
    check(a::GameMode(5) == a::Mode::Command && !a::IsConversationMode(5), "command is not dialogue");
    check(a::GameMode(6) == a::Mode::Injured && !a::IsFlatScreenMode(6), "injured is not a cinematic");
    check(!a::IsMapMode(9) && !a::IsMapMode(10), "GUI/movie cannot open galaxy table");
    check(!a::IsWristMenuMode(10), "movie closes wrist rail");
#else
    constexpr int conversation = 5, gui = 7, galaxy = 9, orbit = 10;
#if defined(MEVR_GAME_LE1)
    check(a::GameMode(1) == a::Mode::Vehicle && a::GameMode(2) == a::Mode::PowerWheel, "LE1 native wheel/vehicle mapping");
    check(a::GameMode(10) == a::Mode::Minigame && !a::IsOrbitalMode(10) && a::IsWristMenuMode(10), "LE1 bypass is not orbital");
#else
    check(a::GameMode(1) == a::Mode::PowerWheel && a::GameMode(4) == a::Mode::Vehicle, "LE2 native wheel/vehicle mapping");
    check(a::IsOrbitalMode(10) && !a::IsWristMenuMode(10), "LE2 orbital is not bypass");
#endif
#endif
    check(a::IsConversationMode(conversation) && !a::IsWristMenuMode(conversation), "dialogue closes wrist rail");
    check(a::IsWristMenuMode(gui) && !a::IsMapMode(gui), "GUI permits wrist without a map");
    check(a::IsGalaxyMode(galaxy) && !a::IsWristMenuMode(galaxy), "galaxy isolates input ownership");
    for (int mode = 0; mode < 25; ++mode) {
        check(a::IsFlatScreenMode(mode) == (mode >= conversation && mode <= orbit), "flat-screen behavior preserved");
    }
    return failures == 0 ? 0 : 1;
}

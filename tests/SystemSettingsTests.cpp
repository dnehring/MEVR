// SPDX-License-Identifier: MIT
// Real settings-command path with simulated modes, world changes and guard health. Compiled once per game
// (mevr-system-settings is LE1's; mevr-le2-/mevr-le3-system-settings the sequels'): the guard and settling sequence is
// ME1's, the built-in values and the character-shadow rule every game's.
#include "game/SystemSettings.cpp"
#include "mod/ModApi.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace settings = mevr::game::system_settings;
namespace {
int failures = 0;
std::vector<std::string> commands;
bool playerReady = true, protectedNow = true;
int protectionQueries = 0;
void Check(bool ok, int line) {
    if (!ok) { std::printf("FAIL SystemSettingsTests.cpp:%d\n", line); ++failures; }
}
#define CHECK(x) Check((x), __LINE__)
bool Protection() { ++protectionQueries; return protectedNow; }
}
namespace mevr::log { void Write(std::string_view, std::string_view) {} }
namespace mevr::engine { void const* World() { return nullptr; } }
namespace mevr::game::head_tracking { std::optional<unsigned char> GameMode() { return std::nullopt; } }
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
namespace mevr::game::world_drawn { bool Now(std::uint64_t) { return true; } }
#endif
namespace mevr::game::console_request {
bool RunCommand(std::string const& command, std::string*) {
    if (!playerReady) return false;
    commands.push_back(command);
    return true;
}
}

int main() {
    // Old/short/absent hosts cannot accidentally retire the fallback.
    mevr::HostServices host;
    host.servicesSize = sizeof host;
    host.motionBlurProtected = &Protection;
    CHECK(!mevr::HasMotionBlurProtection(nullptr));
    host.version = 36;
    CHECK(!mevr::HasMotionBlurProtection(&host));
    host.version = 37;
    host.servicesSize = static_cast<std::uint32_t>(offsetof(mevr::HostServices, motionBlurProtected));
    CHECK(!mevr::HasMotionBlurProtection(&host));
    CHECK(protectionQueries == 0);
    host.servicesSize = sizeof host;
    host.motionBlurProtected = nullptr;
    CHECK(!mevr::HasMotionBlurProtection(&host));
    host.motionBlurProtected = &Protection;
    CHECK(mevr::HasMotionBlurProtection(&host));
    protectedNow = false;
    CHECK(!mevr::HasMotionBlurProtection(&host));

    namespace fs = std::filesystem;
    // The game's layout: <game>\Binaries\Win64\ASI\<GAME>MEVR.ini and <game>\BioGame\Config\GamerSettings.ini.
    auto const folder = fs::temp_directory_path() / ("mevr-settings-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    fs::create_directories(folder / "Binaries" / "Win64" / "ASI");
    fs::create_directories(folder / "BioGame" / "Config");
    auto const file = folder / "Binaries" / "Win64" / "ASI" / "test.ini";
    auto const gamer = folder / "BioGame" / "Config" / "GamerSettings.ini";
    auto writeGamer = [&](std::string const& text) {
        { std::ofstream out(gamer, std::ios::binary); out << text; }
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, gamer.c_str());
    };
    auto stamp = fs::file_time_type::clock::now();
    auto write = [&](std::string const& text) {
        { std::ofstream out(file, std::ios::binary); out << text; }
        stamp += std::chrono::seconds(2);
        fs::last_write_time(file, stamp);  // deterministic change without sleeps
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
    };
    void const* world = nullptr;
    auto poll = [&](unsigned char mode, bool healthy) {
        settings::ObserveMode(mode);
        settings::Poll(1.0f, mode, healthy, file, world);
    };
    auto contains = [](std::string const& command) {
        return std::find(commands.begin(), commands.end(), command) != commands.end();
    };
    // The built-in dynamic shadows (engine::adapter::kVrDynamicShadows): off on every game (ME1's cost lever; on ME2 they
    // differed between the eyes, the owner 2026-11-17). Every game's character shadows are always off too
    // (kVrCharacterShadowsOff, ME1's since 2026-11-18): one more boolean key, two more commands, in every full application.
    std::string const builtInShadows = "False";
    std::string const otherShadows = "True";
    constexpr bool kCharacterOff = mevr::engine::adapter::kVrCharacterShadowsOff;
    std::size_t const fullApply = kCharacterOff ? 7 : 5;
#if defined(MEVR_GAME_LE1)
    // ME1's guard and settling sequence (LE2 holds a new level's settings for 3 s; the sequels' runs are below).
    int firstWorld = 1, nextWorld = 2;
    world = &firstWorld;
    write("[SystemSettings]\nAntiAliasing=True\n");
    poll(0, true);
    CHECK(commands.size() == 9);  // initial values, including AA, still apply
    CHECK(contains("scale set LightEnvironmentShadows False"));  // character shadows off: sharePass 5 by default
    CHECK(contains("scale set MotionBlur False") && contains("scale set AntiAliasing True"));
    commands.clear();
    for (int i = 0; i < 16; ++i) poll(0, true);
    CHECK(commands.empty());  // healthy native protection replaces both redundant settling resets
    commands.clear();
    // Back to gameplay with the guard healthy: the shadows alone again, through their opposites, once (2026-11-18: the
    // cinematics turned them back on while the whole reapply was skipped).
    std::vector<std::string> const shadowsAgain{"scale set DynamicShadows True", "scale set DynamicShadows False",
        "scale set LightEnvironmentShadows True", "scale set LightEnvironmentShadows False"};
    poll(7, true); poll(0, true); poll(0, true);
    CHECK(commands == shadowsAgain);
    commands.clear();
    settings::ObserveMode(static_cast<unsigned char>(5));  // short conversation between polls also counts
    poll(0, true);
    CHECK(commands == shadowsAgain && !settings::gLeftGameplay);
    commands.clear();
    poll(7, true);
    write("[SystemSettings]\nAntiAliasing=True\n");  // menu saves identical values: nothing more than the shadows
    poll(0, true);
    CHECK(commands == shadowsAgain);
    commands.clear();
    write("[SystemSettings]\nAntiAliasing=True\nFoliageDrawRadiusMultiplier=0.75\n");
    poll(0, true);
    CHECK(commands.size() == 9 && contains("scale set FoliageDrawRadiusMultiplier 0.75"));
    commands.clear();
    playerReady = false;
    poll(0, false);  // protection lost without a menu transition
    CHECK(commands.empty() && settings::gGuardFallbackPending);
    playerReady = true;
    poll(0, false);
    CHECK(commands.size() == 9 && !settings::gGuardFallbackPending);
    commands.clear();
    poll(0, false);
    CHECK(commands.empty());
    poll(7, false); poll(0, false);
    CHECK(commands.size() == 9);  // unavailable guard retains menu-return protection
    commands.clear();
    world = &nextWorld;
    poll(0, true);
    CHECK(commands.size() == 9);  // new level still gets the saved graphics values
    commands.clear();
    for (int i = 0; i < 16; ++i) poll(0, true);
    CHECK(commands.empty());
    int unprotectedWorld = 3;
    world = &unprotectedWorld;
    poll(0, false);
    CHECK(commands.size() == 9);
    commands.clear();
    for (int i = 0; i < 16; ++i) poll(0, false);
    CHECK(commands.size() == 18);  // legacy/unhealthy hosts retain both settling attempts
    commands.clear();
    int delayedWorld = 4;
    world = &delayedWorld;
    poll(0, true);
    CHECK(commands.size() == 9);
    commands.clear();
    settings::Poll(20.0f, static_cast<unsigned char>(0), true, file, world);
    CHECK(commands.empty() && settings::gSettleApplies == 2);  // a loading gap consumes both passed deadlines
    poll(0, false);
    CHECK(commands.size() == 9);  // immediate fallback, without a backlog of timed resets
    commands.clear();
    poll(0, false);
    CHECK(commands.empty());
#endif
    // Character shadows (research/37, 2026-11-13; HostServices v43): off only while the host says the second eye is a
    // replay, whatever [Performance] sharePass reads; the file's own value wins.
    auto has = [](std::vector<settings::Setting> const& w, char const* key, char const* value) {
        return std::any_of(w.begin(), w.end(), [&](settings::Setting const& v) { return v.key == key && v.value == value; });
    };
    auto names = [](std::vector<settings::Setting> const& w, char const* key) {
        return std::any_of(w.begin(), w.end(), [&](settings::Setting const& v) { return v.key == key; });
    };
    write("[Performance]\nsharePass=5\n[SystemSettings]\n");
    CHECK(has(settings::Wanted(file, true), "LightEnvironmentShadows", "False"));
    if (kCharacterOff) {
        CHECK(has(settings::Wanted(file, false), "LightEnvironmentShadows", "False"));  // the sequels: off with no replay
    } else {
        CHECK(!names(settings::Wanted(file, false), "LightEnvironmentShadows"));  // no replay, no value of the player's: untouched
    }
    writeGamer("[SystemSettings]\nLightEnvironmentShadows=True\n");
    // The player's own value put back on ME1; the sequels keep theirs off.
    CHECK(has(settings::Wanted(file, false), "LightEnvironmentShadows", kCharacterOff ? "False" : "True"));
    CHECK(has(settings::Wanted(file, true), "LightEnvironmentShadows", "False"));   // the replay still wins over it
    writeGamer("[SystemSettings]\nLightEnvironmentShadows=False\n");
    CHECK(has(settings::Wanted(file, false), "LightEnvironmentShadows", "False"));
    writeGamer("[SystemSettings]\nLightEnvironmentShadows=True\n");
    write("[Performance]\nsharePass=5\n[SystemSettings]\nLightEnvironmentShadows=True\n");
    CHECK(has(settings::Wanted(file, true), "LightEnvironmentShadows", "True") &&
          !has(settings::Wanted(file, true), "LightEnvironmentShadows", "False"));
    // A host before v43 says nothing: the file's sharePass rule, on the game whose older hosts replayed (LE1's only).
    write("[Performance]\nsharePass=0\n[SystemSettings]\n");
    CHECK(!settings::ReplayByFile(file));
    write("[Performance]\nsharePass=\n[SystemSettings]\n");
#if defined(MEVR_GAME_LE1)
    CHECK(settings::ReplayByFile(file));  // empty is the default, 5
    write("[Performance]\nsharePass=3\n[SystemSettings]\n");
    CHECK(settings::ReplayByFile(file));
#else
    CHECK(!settings::ReplayByFile(file));  // a sequel's host never replayed: whatever the file says
    write("[Performance]\nsharePass=5\n[SystemSettings]\n");
    CHECK(!settings::ReplayByFile(file));
#endif
    // The host's helper: nothing from an older or shorter table, else the callback's answer (false without one).
    {
        static bool replay = true;
        mevr::HostServices h;
        h.servicesSize = sizeof h;
        h.secondEyeIsReplay = [] { return replay; };
        CHECK(!mevr::SecondEyeIsReplay(nullptr).has_value());
        h.version = 42;
        CHECK(!mevr::SecondEyeIsReplay(&h).has_value());
        h.version = 43;
        h.servicesSize = static_cast<std::uint32_t>(offsetof(mevr::HostServices, secondEyeIsReplay));
        CHECK(!mevr::SecondEyeIsReplay(&h).has_value());
        h.servicesSize = sizeof h;
        CHECK(mevr::SecondEyeIsReplay(&h) == std::optional<bool>(true));
        replay = false;
        CHECK(mevr::SecondEyeIsReplay(&h) == std::optional<bool>(false));
        h.secondEyeIsReplay = nullptr;
        CHECK(mevr::SecondEyeIsReplay(&h) == std::optional<bool>(false));
    }
    // The host's answer changing applies again, once: a replay starting turns the shadows off, one ending puts the
    // player's value back; an unchanged answer does nothing.
    {
        int replayWorld = 5;
        world = &replayWorld;
        write("[SystemSettings]\n");
        commands.clear();
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, false);
        CHECK(commands.size() == 7);
        // Through its opposite, the wanted value last.
        CHECK(commands.back() == (kCharacterOff ? "scale set LightEnvironmentShadows False"
                                                : "scale set LightEnvironmentShadows True"));
        // The game's dynamic shadows, first and through their opposite.
        CHECK(commands.size() >= 2 && commands[0] == "scale set DynamicShadows " + otherShadows &&
              commands[1] == "scale set DynamicShadows " + builtInShadows);
        commands.clear();
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, false);
        CHECK(commands.empty());
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, true);
        if (kCharacterOff) {
            CHECK(commands.empty());  // the sequels: always off, the host's answer changes nothing
        } else {
            CHECK(!commands.empty() && commands.back() == "scale set LightEnvironmentShadows False");
        }
        commands.clear();
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, true);
        CHECK(commands.empty());
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, false);
        if (kCharacterOff) {
            CHECK(commands.empty());
        } else {
            CHECK(!commands.empty() && commands.back() == "scale set LightEnvironmentShadows True");
        }
        commands.clear();
        writeGamer("[SystemSettings]\n");  // no value of the player's and no replay: the key is not sent at all
        int plainWorld = 6;
        world = &plainWorld;
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, false);
#if defined(MEVR_GAME_LE2)
        // ME2 holds a new level's settings until it has been in gameplay for 3 s (a scale set during the load faulted).
        CHECK(commands.empty());
        settings::Poll(3.0f, static_cast<unsigned char>(0), true, file, world, false);
#endif
        if (kCharacterOff) {
            CHECK(commands.size() == fullApply && commands.back() == "scale set LightEnvironmentShadows False");
        } else {
            CHECK(commands.size() == 5 && !contains("scale set LightEnvironmentShadows True") &&
                  !contains("scale set LightEnvironmentShadows False"));
        }
        commands.clear();
    }
    // Dynamic shadows: the game's built-in value, and a player's [SystemSettings] DynamicShadows over it, either way.
    {
        CHECK(has(settings::Wanted(file, false), "DynamicShadows", builtInShadows.c_str()) &&
              has(settings::Wanted(file, true), "DynamicShadows", builtInShadows.c_str()));
        write("[SystemSettings]\nDynamicShadows=" + otherShadows + "\n");
        CHECK(has(settings::Wanted(file, false), "DynamicShadows", otherShadows.c_str()) &&
              !has(settings::Wanted(file, false), "DynamicShadows", builtInShadows.c_str()));
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, false);
        CHECK(commands.size() == fullApply && commands[0] == "scale set DynamicShadows " + builtInShadows &&
              commands[1] == "scale set DynamicShadows " + otherShadows);
        commands.clear();
        write("[SystemSettings]\n");   // the player's key removed: the built-in value again
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, false);
        CHECK(commands.size() == fullApply && commands[0] == "scale set DynamicShadows " + otherShadows &&
              commands[1] == "scale set DynamicShadows " + builtInShadows);
        commands.clear();
    }
    // Back to gameplay with the blur guard healthy, every game (2026-11-18): the shadow keys alone, through their opposites,
    // once; on ME2 only once the level has been in gameplay for 3 s (it has, here).
    {
        write("[SystemSettings]\n");
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, false);
        commands.clear();
        unsigned char away = 5;
        while (mevr::engine::adapter::IsHudGameplayMode(away)) ++away;
        settings::ObserveMode(away);
        settings::Poll(1.0f, away, true, file, world, false);
        CHECK(commands.empty());
        settings::ObserveMode(static_cast<unsigned char>(0));
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, false);
        std::vector<std::string> const again{"scale set DynamicShadows " + otherShadows,
            "scale set DynamicShadows " + builtInShadows, "scale set LightEnvironmentShadows True",
            "scale set LightEnvironmentShadows False"};
        CHECK(commands == again);
        commands.clear();
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, false);
        CHECK(commands.empty());
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
        // The sequels: a return to gameplay while the world isn't drawn (ME3's cab ride loads in mode 0, 2026-11-20) waits,
        // every poll, and goes once it is drawn: never dropped. The same for a new level.
        settings::ObserveMode(away);
        settings::Poll(1.0f, away, true, file, world, false);
        settings::ObserveMode(static_cast<unsigned char>(0));
        settings::gWorldDrawn = false;
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, false);
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, false);
        CHECK(commands.empty());
        settings::gWorldDrawn = true;
        settings::Poll(1.0f, static_cast<unsigned char>(0), true, file, world, false);
        CHECK(commands == again);
        commands.clear();
        int loadingWorld = 7;
        world = &loadingWorld;
        settings::gWorldDrawn = false;
        settings::Poll(4.0f, static_cast<unsigned char>(0), true, file, world, false);
        CHECK(commands.empty());
        settings::gWorldDrawn = true;
        settings::Poll(3.0f, static_cast<unsigned char>(0), true, file, world, false);  // 3 s: ME2's new level held that long
        CHECK(commands.size() == fullApply);
        commands.clear();
#endif
    }
    write("[SystemSettings]\napply=0\n");
    poll(0, true); poll(0, false); poll(7, false); poll(0, false);
    CHECK(commands.empty() && !settings::gGuardFallbackPending);
    fs::remove_all(folder);
    CHECK(mevr::engine::adapter::kVrDynamicShadows == (builtInShadows == "True"));
    std::printf("System settings (%s): %s (%scharacter shadows, dynamic shadows %s)\n",
#if defined(MEVR_GAME_LE1)
                "LE1", failures ? "FAIL" : "PASS",
                "guarded returns, fallback/retry, ABI gates, settings edits, startup/load/settle, ",
#elif defined(MEVR_GAME_LE2)
                "LE2", failures ? "FAIL" : "PASS", "ABI gates, settings edits, the new level held 3 s, ",
#else
                "LE3", failures ? "FAIL" : "PASS", "ABI gates, settings edits, ",
#endif
                builtInShadows.c_str());
    return failures ? 1 : 0;
}

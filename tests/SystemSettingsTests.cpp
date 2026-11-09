// SPDX-License-Identifier: MIT
// Real settings-command path with simulated modes, world changes and guard health.
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
    auto const folder = fs::temp_directory_path() / ("mevr-settings-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    fs::create_directory(folder);
    auto const file = folder / "test.ini";
    auto stamp = fs::file_time_type::clock::now();
    auto write = [&](std::string const& text) {
        { std::ofstream out(file, std::ios::binary); out << text; }
        stamp += std::chrono::seconds(2);
        fs::last_write_time(file, stamp);  // deterministic change without sleeps
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
    };
    int firstWorld = 1, nextWorld = 2;
    void const* world = &firstWorld;
    auto poll = [&](unsigned char mode, bool healthy) {
        settings::ObserveMode(mode);
        settings::Poll(1.0f, mode, healthy, file, world);
    };
    auto contains = [](std::string const& command) {
        return std::find(commands.begin(), commands.end(), command) != commands.end();
    };
    write("[SystemSettings]\nAntiAliasing=True\n");
    poll(0, true);
    CHECK(commands.size() == 7);  // initial values, including AA, still apply
    CHECK(contains("scale set MotionBlur False") && contains("scale set AntiAliasing True"));
    commands.clear();
    for (int i = 0; i < 16; ++i) poll(0, true);
    CHECK(commands.empty());  // healthy native protection replaces both redundant settling resets
    commands.clear();
    poll(7, true); poll(0, true); poll(0, true);
    CHECK(commands.empty());
    settings::ObserveMode(static_cast<unsigned char>(5));  // short conversation between polls also counts
    poll(0, true);
    CHECK(commands.empty() && !settings::gLeftGameplay);
    poll(7, true);
    write("[SystemSettings]\nAntiAliasing=True\n");  // menu saves identical values
    poll(0, true);
    CHECK(commands.empty());
    write("[SystemSettings]\nAntiAliasing=True\nFoliageDrawRadiusMultiplier=0.75\n");
    poll(0, true);
    CHECK(commands.size() == 7 && contains("scale set FoliageDrawRadiusMultiplier 0.75"));
    commands.clear();
    playerReady = false;
    poll(0, false);  // protection lost without a menu transition
    CHECK(commands.empty() && settings::gGuardFallbackPending);
    playerReady = true;
    poll(0, false);
    CHECK(commands.size() == 7 && !settings::gGuardFallbackPending);
    commands.clear();
    poll(0, false);
    CHECK(commands.empty());
    poll(7, false); poll(0, false);
    CHECK(commands.size() == 7);  // unavailable guard retains menu-return protection
    commands.clear();
    world = &nextWorld;
    poll(0, true);
    CHECK(commands.size() == 7);  // new level still gets the saved graphics values
    commands.clear();
    for (int i = 0; i < 16; ++i) poll(0, true);
    CHECK(commands.empty());
    int unprotectedWorld = 3;
    world = &unprotectedWorld;
    poll(0, false);
    CHECK(commands.size() == 7);
    commands.clear();
    for (int i = 0; i < 16; ++i) poll(0, false);
    CHECK(commands.size() == 14);  // legacy/unhealthy hosts retain both settling attempts
    commands.clear();
    int delayedWorld = 4;
    world = &delayedWorld;
    poll(0, true);
    CHECK(commands.size() == 7);
    commands.clear();
    settings::Poll(20.0f, static_cast<unsigned char>(0), true, file, world);
    CHECK(commands.empty() && settings::gSettleApplies == 2);  // a loading gap consumes both passed deadlines
    poll(0, false);
    CHECK(commands.size() == 7);  // immediate fallback, without a backlog of timed resets
    commands.clear();
    poll(0, false);
    CHECK(commands.empty());
    write("[SystemSettings]\napply=0\n");
    poll(0, true); poll(0, false); poll(7, false); poll(0, false);
    CHECK(commands.empty() && !settings::gGuardFallbackPending);
    fs::remove(file);
    fs::remove(folder);
    std::printf("System settings: %s (guarded returns, fallback/retry, ABI gates, settings edits, startup/load/settle)\n",
                failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

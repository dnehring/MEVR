// SPDX-License-Identifier: MIT
// Exercise the real loader lifecycle with a private dummy source and a Windows system DLL.
// No game, renderer, global runtime configuration or external mod is loaded.
#include "plugin/ModLoader.cpp"

#include <cstdio>
#include <vector>

namespace {
int failures = 0;
unsigned presentChanges = 0, eventChanges = 0, prepares = 0, shutdowns = 0, ticks = 0;
bool allowPrepare = false;
std::vector<std::string> messages;
void Check(bool ok, char const* expression, int line) {
    if (!ok) { ++failures; std::printf("line %d: %s\n", line, expression); }
}
#define CHECK(expression) Check((expression), #expression, __LINE__)
bool Prepare() { ++prepares; return allowPrepare; }
void Shutdown() { ++shutdowns; }
void Tick(float) { ++ticks; }
}

namespace mevr::log {
void Write(std::string_view, std::string_view message) { messages.emplace_back(message); }
}
namespace mevr::ini {
std::string Utf8(std::filesystem::path const& path) {
    auto const bytes = path.u8string();
    return {bytes.begin(), bytes.end()};
}
}
namespace mevr::host::present {
void SetCallback(Callback) { ++presentChanges; }
}
namespace mevr::host::process_event {
bool SetCallback(Callback, std::uint32_t) { ++eventChanges; return true; }
}
namespace mevr::host::crash {
void RecordWrite(char const*, void const*, std::uint32_t, std::uint64_t) {}
std::uint64_t BeginScriptCall(ue3::Function*, ue3::Object*) { return 0; }
void EndScriptCall(std::uint64_t) {}
}

int main() {
    using namespace mevr::host;
    auto const testDir = fs::temp_directory_path() / (L"mevr-mod-reload-test-" + std::to_wstring(GetCurrentProcessId()));
    if (fs::exists(testDir)) { std::puts("private test directory already exists"); return 1; }
    fs::create_directory(testDir);
    gSourcePath = testDir / "candidate.dll";
    gShadowDir = testDir / "shadow";
    fs::create_directory(gShadowDir);
    { std::ofstream source(gSourcePath, std::ios::binary); source << "private invalid DLL fixture"; }
    FILETIME sourceStamp{};
    CHECK(ReadStamp(sourceStamp));
    auto systemModule = [] {
        wchar_t directory[MAX_PATH]{};
        auto const size = GetSystemDirectoryW(directory, MAX_PATH);
        return size > 0 && size < MAX_PATH ? LoadLibraryW((fs::path(directory) / L"version.dll").c_str()) : nullptr;
    };
    HMODULE module = systemModule();
    CHECK(module != nullptr);
    if (!module) { fs::remove_all(testDir); return 1; }
    gMod = {module, {}, &::Tick, &Shutdown, &Prepare};
    // Two stable polls offer the candidate. Refusal leaves both callbacks, old module and
    // loaded stamp untouched, and normal ticks continue between repeated attempts.
    mevr::host::Tick(0.5f);
    CHECK(prepares == 0 && ticks == 1 && gHavePending);
    mevr::host::Tick(0.5f);
    CHECK(prepares == 1 && ticks == 2 && shutdowns == 0);
    CHECK(presentChanges == 0 && eventChanges == 0 && gMod.module == module);
    CHECK(gGeneration == 0 && !SameStamp(gLoadedStamp, sourceStamp));
    mevr::host::Tick(0.5f);
    CHECK(prepares == 2 && ticks == 3 && gGeneration == 0);
    CHECK(messages.size() == 1); // bounded defer log, no per-poll warning flood

    // Native restoration recovers: exactly one ordinary shutdown and one load attempt.
    // Invalid-image loading deliberately fails after the real copy path, avoiding game code.
    allowPrepare = true;
    mevr::host::Tick(0.5f);
    CHECK(prepares == 3 && shutdowns == 1 && ticks == 3);
    CHECK(presentChanges == 1 && eventChanges == 1 && gMod.module == nullptr);
    CHECK(gGeneration == 1 && SameStamp(gLoadedStamp, sourceStamp) && !gHavePending);
    mevr::host::Tick(0.5f);
    CHECK(shutdowns == 1 && gGeneration == 1); // failed image is not repeatedly reloaded

    // An older module without the optional export follows the original unload path.
    module = systemModule(); CHECK(module != nullptr);
    gMod = {module, {}, &::Tick, &Shutdown, nullptr};
    allowPrepare = false;
    CHECK(Unload());
    CHECK(prepares == 3 && shutdowns == 2 && gMod.module == nullptr);

    // Process exit never waits on native restore. The real loader leaves this DLL mapped.
    module = systemModule(); CHECK(module != nullptr);
    gMod = {module, {}, &::Tick, &Shutdown, &Prepare};
    SetProcessExiting();
    CHECK(Unload());
    CHECK(prepares == 3 && shutdowns == 3 && gMod.module == nullptr);
    CHECK(FreeLibrary(module)); // release only the private reference retained by the exit path
    fs::remove_all(testDir);
    return failures;
}

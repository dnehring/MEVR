// SPDX-License-Identifier: MIT
// Exercise the actual host detour with native-shaped proxy objects and render callbacks.
// Layout equivalence of the game specializations is separately checked against the local exe.
#include "plugin/MotionBlurGuard.cpp"

#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace guard = mevr::host::motion_blur_guard;
namespace {
int failures = 0, originalCalls = 0, sharpCalls = 0, journalCalls = 0;
std::array<std::array<void*, 10>, 5> blur{}, sharp{};
struct Proxy { void* table; std::array<std::uint8_t, 256> fields; };
Proxy* expectedProxy = nullptr;
void* expectedTable = nullptr;
std::array<std::uint8_t, 256> expectedFields{};
void* const sceneArg = reinterpret_cast<void*>(0x1000);
void* const thirdArg = reinterpret_cast<void*>(0x2000);
void* const viewArg = reinterpret_cast<void*>(0x3000);
std::uintptr_t constexpr fifthArg = 0x123456789abcdef0ull;
std::uint64_t output = 0;
void Check(bool ok, int line) {
    if (!ok) { std::printf("FAIL MotionBlurGuardTests.cpp:%d\n", line); ++failures; }
}
#define CHECK(x) Check((x), __LINE__)
std::uintptr_t Sharp(void* self, void* scene, void* arg3, void* view, std::uintptr_t arg5, void* arg6) {
    ++sharpCalls;
    auto* proxy = static_cast<Proxy*>(self);
    CHECK(proxy == expectedProxy && proxy->table == expectedTable);
    CHECK(proxy->fields == expectedFields);
    CHECK(scene == sceneArg && arg3 == thirdArg && view == viewArg && arg5 == fifthArg && arg6 == &output);
    ++output;
    // Native updates must stay on the original proxy, not disappear into a copied object.
    ++proxy->fields[42];
    expectedFields = proxy->fields;
    return 0xfedcba9876543210ull;
}
std::uintptr_t Original(void* self, void*, void*, void*, std::uintptr_t, void*) {
    ++originalCalls;
    CHECK(static_cast<Proxy*>(self)->table == expectedTable);
    CHECK(static_cast<Proxy*>(self)->fields == expectedFields);
    return 0x9999;
}
void Journal(char const*, void const* address, std::uint32_t size, std::uint64_t) {
    CHECK(address == expectedProxy && size == sizeof(void*));
    ++journalCalls;
}
std::uintptr_t Dispatch(Proxy& proxy) {
    auto const* table = static_cast<void* const*>(proxy.table);
    auto* render = reinterpret_cast<guard::Render*>(table[1]);
    return render(&proxy, sceneArg, thirdArg, viewArg, fifthArg, &output);
}
} // namespace

// Dependencies not involved in the detour test. No engine, runtime, or headset is required.
namespace MELEVR::MevrBridge { std::wstring const& SettingsPath() noexcept { static std::wstring path; return path; } }
namespace mevr::log { void Write(std::string_view, std::string_view) {} }
namespace mevr::exe {
std::uintptr_t Base() { return 0; }
void* Resolve(std::uintptr_t) { return nullptr; }
Result const* Find(std::uint32_t) { return nullptr; }
}
namespace mevr::hook {
bool Install(Context&, char const*, void*, void*, void**) { throw std::runtime_error("unverified code must not be hooked"); }
}

int main() {
    CHECK(!guard::ProtectsGameplay());
    guard::gOriginal = &Original;
    guard::gSharpRender = &Sharp;
    mevr::write_journal::Connect(&Journal);
    for (std::size_t i = 0; i < blur.size(); ++i) {
        blur[i][1] = reinterpret_cast<void*>(&guard::RenderHook);
        sharp[i][1] = reinterpret_cast<void*>(&Sharp);
        guard::gPairs[i] = {blur[i].data(), sharp[i].data()};
        // Recreated effects after a cinematic must be guarded just like the original proxy.
        for (int recreation = 0; recreation < 2; ++recreation) {
            Proxy proxy{blur[i].data(), {}};
            for (std::size_t k = 0; k < proxy.fields.size(); ++k) proxy.fields[k] = static_cast<std::uint8_t>(k + i);
            expectedProxy = &proxy;
            expectedTable = sharp[i].data();
            expectedFields = proxy.fields;
            auto const before = guard::gConverted.load();
            CHECK(Dispatch(proxy) == 0xfedcba9876543210ull);
            CHECK(guard::gConverted.load() == before + 1);
            // Cached proxy / second eye dispatches directly to its no-blur implementation.
            CHECK(Dispatch(proxy) == 0xfedcba9876543210ull);
            CHECK(guard::gConverted.load() == before + 1);
        }
        CHECK(guard::gVariants[i].load() == 2);
    }
    std::array<void*, 10> foreign{};
    foreign[1] = reinterpret_cast<void*>(&guard::RenderHook);
    guard::gInstalled.store(true);
    CHECK(guard::ProtectsGameplay());
    CHECK(guard::StatsJson().find("\"protectsGameplay\":true") != std::string::npos);
    Proxy unknown{foreign.data(), {}};
    expectedProxy = &unknown; expectedTable = foreign.data(); expectedFields = unknown.fields;
    CHECK(Dispatch(unknown) == 0x9999);
    CHECK(guard::gUnknown.load() == 1 && originalCalls == 1);
    CHECK(!guard::ProtectsGameplay());
    guard::gInstalled.store(false);
    CHECK(sharpCalls == 20 && journalCalls == 10 && output == 20);
    CHECK(guard::StatsJson().find("\"converted\":10") != std::string::npos);
    mevr::hook::Context context{nullptr, "test"};
    CHECK(!guard::Install(context));  // unverified addresses leave the existing workaround in charge
    CHECK(!guard::gInstalled.load());
    std::printf("Motion blur guard: %s (five variants, cached/recreated proxies, arguments, persistent fields, foreign fallback)\n",
                failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

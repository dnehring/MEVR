// SPDX-License-Identifier: MIT
// Exercise the actual detour, including SEH restoration, with a synthetic native caller.
#include "engine/SelectionGaze.cpp"
#include <cstdio>

namespace mevr::log { void Write(std::string_view, std::string_view) {} }
namespace mevr::exe { void* Resolve(std::uintptr_t) { return nullptr; } }
namespace mevr::hook {
bool Install(Context&, char const*, void*, void*, void**) { return false; }
bool Uninstall(Context&, char const*) { return false; }
}

namespace {
namespace gaze = mevr::engine::selection_gaze;
mevr::ue3::Vector location{1, 2, 3};
mevr::ue3::Rotator rotation{4, 5, 6};
int selection = 0, failures = 0, calls = 0;
bool expectSwap = false, nativeFault = false, providerFault = false, badPointer = false;
void Check(bool pass, int line) {
    if (!pass) { ++failures; std::printf("SelectionGazeTests:%d failed\n", line); }
}
#define CHECK(value) Check((value), __LINE__)

bool Provide(void const* object, gaze::Swap& swap) {
    if (providerFault) RaiseException(0xE0424242, 0, 0, nullptr);
    if (object != &selection) return false;
    swap.povLocation = &location;
    swap.povRotation = badPointer ? nullptr : &rotation;
    swap.location = {10, 20, 30}; swap.rotation = {40, 50, 60};
    return true;
}
void ObserveNative(void* object) {
    CHECK(object == &selection || object == nullptr);
    CHECK(location.x == (expectSwap ? 10.0f : 1.0f));
    CHECK(rotation.yaw == (expectSwap ? 50 : 5));
    ++calls;
    if (nativeFault) RaiseException(0xE0424243, 0, 0, nullptr);
}
#if defined(MEVR_GAME_LE3)
void Native(void* object) { ObserveNative(object); }
void Call(void* object) { gaze::UpdateSelectionHook(object); }
#else
int Native(void* object, float seconds) { CHECK(seconds == 0.25f); ObserveNative(object); return 17; }
void Call(void* object) { CHECK(gaze::UpdateSelectionHook(object, 0.25f) == 17); }
#endif
bool CallWithFault() {
    __try { Call(&selection); }
    __except (GetExceptionCode() == 0xE0424243 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}
void Restored() {
    CHECK(location.x == 1 && location.y == 2 && location.z == 3);
    CHECK(rotation.pitch == 4 && rotation.yaw == 5 && rotation.roll == 6);
    CHECK(gaze::gInFlight.load() == 0);
}
}

int main() {
    gaze::gOrig = &Native;
    gaze::SetProvider(nullptr); Call(&selection); Restored();
    gaze::SetProvider(&Provide); expectSwap = true; Call(&selection); Restored();
    nativeFault = true; CHECK(CallWithFault()); Restored(); nativeFault = false;
    expectSwap = false; Call(nullptr); Restored();
    badPointer = true; Call(&selection); Restored(); badPointer = false;
    providerFault = true; Call(&selection); Restored();
    CHECK(gaze::gProvider.load() == nullptr); providerFault = false;
    Call(&selection); Restored();
    auto const stats = gaze::Take();
    CHECK(calls == 7 && stats.calls == 7 && stats.swapped == 2);
    CHECK(gaze::Take().calls == 0);
    std::printf("Selection gaze call/restore/exception checks: %d failures\n", failures);
    return failures ? 1 : 0;
}

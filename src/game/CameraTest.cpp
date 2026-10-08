#include "game/CameraTest.hpp"

#include "core/Log.hpp"
#include "engine/Objects.hpp"
#include "engine/ProcessEvent.hpp"

#include <LESDK/Headers.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace mevr::game::camera_test {
namespace {

// UnrealScript events that might carry the player's view; counted to see which ME1 uses.
constexpr std::array<wchar_t const*, 4> kWatchedNames = {
    L"GetPlayerViewPoint",
    L"UpdateCamera",
    L"GetCameraViewPoint",
    L"CalcCamera",
};

struct Watched {
    UFunction* function = nullptr;
    std::string name;
    bool isViewPoint = false;  // a GetPlayerViewPoint implementation: the sweep applies here
    std::atomic<unsigned> calls{0};
};

std::vector<std::unique_ptr<Watched>> gWatched;

// Backslash on US layouts. Not F-keys (bound in game) and not backtick (opens the UE console).
constexpr int kVirtualKeyToggle = VK_OEM_5;
constexpr double kSweepPeriodSeconds = 4.0;
constexpr double kSweepDegrees = 30.0;
constexpr double kRotatorUnitsPerDegree = 65536.0 / 360.0;

bool gSweepOn = false;
bool gKeyWasDown = false;
double gSweepTime = 0.0;
std::atomic<int> gYawOffset{0};
std::atomic<unsigned> gAdjustedViews{0};

Watched* FindWatched(UFunction* function) {
    for (auto& watched : gWatched) {
        if (watched->function == function) {
            return watched.get();
        }
    }
    return nullptr;
}

void OnWatchedEvent(UObject* context, UFunction* function, void* parms, void* /*result*/) {
    Watched* const watched = FindWatched(function);
    if (watched == nullptr) {
        return;
    }
    watched->calls.fetch_add(1, std::memory_order_relaxed);

    // Only the player's own view; AI controllers also implement GetPlayerViewPoint.
    if (gSweepOn && watched->isViewPoint && context != nullptr &&
        context->IsA(APlayerController::StaticClass())) {
        auto* const p = static_cast<AController_eventGetPlayerViewPoint_Parms*>(parms);
        p->out_Rotation.Yaw += gYawOffset.load(std::memory_order_relaxed);
        gAdjustedViews.fetch_add(1, std::memory_order_relaxed);
    }
}

bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

void PollHotkey() {
    bool const down = GameHasFocus() && (GetAsyncKeyState(kVirtualKeyToggle) & 0x8000) != 0;
    if (down && !gKeyWasDown) {
        gSweepOn = !gSweepOn;
        gSweepTime = 0.0;
        gYawOffset.store(0, std::memory_order_relaxed);
        log::Info("backslash: camera sweep {}", gSweepOn ? "ON" : "OFF");
    }
    gKeyWasDown = down;
}

} // namespace

bool TryStartup() {
    // The player's view point lives in Engine.u, which loads some seconds after attach.
    std::vector<std::unique_ptr<Watched>> found;
    bool haveViewPoint = false;
    for (wchar_t const* name : kWatchedNames) {
        for (UFunction* function : engine::FindFunctionsNamed(name)) {
            auto watched = std::make_unique<Watched>();
            watched->function = function;
            watched->name = engine::FullName(function);
            watched->isViewPoint = std::wstring_view(name) == L"GetPlayerViewPoint";
            haveViewPoint |= watched->name == "Function Engine.PlayerController.GetPlayerViewPoint";
            found.push_back(std::move(watched));
        }
    }
    if (!haveViewPoint) {
        return false;
    }

    for (auto& watched : found) {
        log::Info("watching {}", watched->name);
        engine::process_event::AddPostHandler(watched->function, &OnWatchedEvent);
        gWatched.push_back(std::move(watched));
    }
    log::Info("camera test ready: press backslash in game to toggle a +/-{} degree yaw sweep",
        kSweepDegrees);
    return true;
}

void Tick(float deltaSeconds) {
    PollHotkey();
    if (gSweepOn) {
        gSweepTime += deltaSeconds;
        double const angle = std::sin(2.0 * std::numbers::pi * gSweepTime / kSweepPeriodSeconds);
        gYawOffset.store(static_cast<int>(angle * kSweepDegrees * kRotatorUnitsPerDegree),
            std::memory_order_relaxed);
    }
}

void LogStats() {
    log::Info("    camera sweep {}, adjusted views {}", gSweepOn ? "on" : "off", gAdjustedViews.exchange(0));
    for (auto& watched : gWatched) {
        unsigned const calls = watched->calls.exchange(0);
        if (calls > 0) {
            log::Info("    {:>6} calls  {}", calls, watched->name);
        }
    }
}

void Shutdown() {
    gSweepOn = false;
    gWatched.clear();
}

} // namespace mevr::game::camera_test

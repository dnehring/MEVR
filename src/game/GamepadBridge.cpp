// SPDX-License-Identifier: MIT
#include "game/GamepadBridge.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "game/GamepadMapping.hpp"
#include "game/HeadTracking.hpp"
#include "game/SupportGrip.hpp"

#include <Windows.h>
#include <Xinput.h>

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

namespace mevr::game::gamepad_bridge {
namespace {

using tGetState = DWORD(WINAPI*)(DWORD index, XINPUT_STATE* state);

// The DLLs the game might use. XInputGetStateEx (ordinal 100) exists in 1_3 and 1_4 only.
constexpr char const* kDlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", "xinput1_2.dll", "xinput1_1.dll"};
constexpr std::size_t kMaxSlots = 2 * std::size(kDlls);

struct Slot {
    std::string name;  // hook name
    void* target = nullptr;
    tGetState original = nullptr;
};
std::array<Slot, kMaxSlots> gSlots;
std::size_t gSlotCount = 0;

std::mutex gMutex;  // the latches and packet numbering below
gamepad::Latches gLatches;
XINPUT_GAMEPAD gLastPad{};
DWORD gPacket = 0;

std::atomic<unsigned> gCalls{0};
std::atomic<unsigned> gControllerFrames{0};
std::atomic<unsigned> gRealPadFrames{0};

DWORD Bridge(tGetState original, DWORD index, XINPUT_STATE* state) {
    DWORD const result = original != nullptr ? original(index, state) : static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED);
    if (index != 0 || state == nullptr) {
        return result;
    }
    gCalls.fetch_add(1, std::memory_order_relaxed);
    bool const realPad = result == ERROR_SUCCESS;
    if (realPad) {
        gRealPadFrames.fetch_add(1, std::memory_order_relaxed);
    }
    vr::controllers::State const controllers = vr::controllers::Latest();
    int const yaw = head_tracking::RenderYawOffset();
    if (!controllers.active && (!realPad || yaw == 0)) {
        return result;  // nothing to add or change
    }

    std::lock_guard lock(gMutex);
    XINPUT_GAMEPAD pad = realPad ? state->Gamepad : XINPUT_GAMEPAD{};
    if (controllers.active) {
        pad = gamepad::Merge(pad, gamepad::FromControllers(controllers, gLatches, support_grip::PadOptions()));
        support_grip::SetHeld(gLatches.left == gamepad::LeftGrip::Support);
        gControllerFrames.fetch_add(1, std::memory_order_relaxed);
    }
    gamepad::RotateStick(pad.sThumbLX, pad.sThumbLY, yaw);
    if (std::memcmp(&pad, &gLastPad, sizeof pad) != 0) {
        gLastPad = pad;
        ++gPacket;  // the game only re-reads a pad whose packet number changed
    }
    state->dwPacketNumber = gPacket;
    state->Gamepad = pad;
    return ERROR_SUCCESS;
}

template <std::size_t N>
DWORD WINAPI Detour(DWORD index, XINPUT_STATE* state) {
    return Bridge(gSlots[N].original, index, state);
}

template <std::size_t... N>
constexpr std::array<tGetState, sizeof...(N)> MakeDetours(std::index_sequence<N...>) {
    return {&Detour<N>...};
}
constexpr auto kDetours = MakeDetours(std::make_index_sequence<kMaxSlots>{});

bool AlreadyHooked(void* target) {
    for (std::size_t i = 0; i < gSlotCount; ++i) {
        if (gSlots[i].target == target) {
            return true;
        }
    }
    return false;
}

void Hook(::LESDK::Initializer& init, void* target, std::string name) {
    if (target == nullptr || AlreadyHooked(target) || gSlotCount >= kMaxSlots) {
        return;
    }
    Slot& slot = gSlots[gSlotCount];
    slot.name = std::move(name);
    slot.target = target;
    // LEBinkProxy writes `original` before the detour goes live (core/Hook); XInput is polled
    // from any thread. (Bridge still treats a null one as a disconnected pad.)
    if (!hook::Install(init, slot.name.c_str(), target, reinterpret_cast<void*>(kDetours[gSlotCount]),
            reinterpret_cast<void**>(&slot.original))) {
        log::Warn("gamepad: hooking {} failed", slot.name);
        slot = {};
        return;
    }
    ++gSlotCount;
    log::Info("gamepad: hooked {}", slot.name);
}

} // namespace

bool TryInstall(::LESDK::Initializer& init) {
    for (char const* dll : kDlls) {
        HMODULE const module = GetModuleHandleA(dll);
        if (module == nullptr) {
            continue;
        }
        Hook(init, reinterpret_cast<void*>(GetProcAddress(module, "XInputGetState")), std::string("XInputGetState@") + dll);
        Hook(init, reinterpret_cast<void*>(GetProcAddress(module, MAKEINTRESOURCEA(100))), std::string("XInputGetStateEx@") + dll);
    }
    return gSlotCount > 0;
}

bool Uninstall(::LESDK::Initializer& init) {
    bool removed = true;
    for (std::size_t i = 0; i < gSlotCount; ++i) {
        if (!hook::Uninstall(init, gSlots[i].name.c_str())) {
            removed = false;  // still live: its slot (and original) must stay as they are
            continue;
        }
        log::Info("gamepad: unhooked {}", gSlots[i].name);
        gSlots[i] = {};
    }
    if (removed) {
        gSlotCount = 0;
    }
    std::lock_guard lock(gMutex);
    gLatches = {};
    gLastPad = {};
    return removed;
}

void LogStats() {
    if (gSlotCount == 0) {
        return;
    }
    unsigned const calls = gCalls.exchange(0);
    unsigned const controllerFrames = gControllerFrames.exchange(0);
    unsigned const realPadFrames = gRealPadFrames.exchange(0);
    log::Info("    gamepad: {} XInput polls of pad 0, {} with motion controllers, {} with a real pad; stick turned by {} units",
        calls, controllerFrames, realPadFrames, head_tracking::RenderYawOffset());
}

} // namespace mevr::game::gamepad_bridge

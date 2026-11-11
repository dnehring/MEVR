// SPDX-License-Identifier: MIT
#include "engine/BodyBounds.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "engine/Addresses.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>

namespace mevr::engine::body_bounds {
namespace {

constexpr char const* kHookName = "USkeletalMeshComponent::UpdateBounds";

// research/69 (Ghidra on the LE1 project): 0x58EEC0 is the skeletal mesh component's UpdateBounds (its vtable's slot +0x368;
// it calls the physics asset's box of the bodies, 0x638E20, which tests bConsiderAllBodiesForBounds) and writes the
// component's Bounds: Origin, BoxExtent, SphereRadius. LE1 only (engine/Addresses.hpp).
constexpr std::uint32_t kUpdateBoundsRva = addresses::kSkelUpdateBounds;
// 0x25CFE0: UpdateBounds' last call. With an Umbra object behind the component (+0x210) it makes the object's matrix from
// Bounds (a unit box scaled to the extents at the origin) and gives it to Umbra (0xD43030). Umbra culls by that box, not by
// the one the scene gets with the transform update: padding Bounds alone left Umbra with the tight box (the body was still
// culled at the podium with the padded box around the camera). Called again after the pad.
constexpr std::uint32_t kUmbraBoundsRva = addresses::kPrimitiveUmbraBounds;

using tUpdateBounds = void(void* component);
using tUmbraBounds = void(void* component);

tUpdateBounds* gOrig = nullptr;  // written before the hook goes live (core/Hook)
tUmbraBounds* gUmbraBounds = nullptr;
std::atomic<unsigned> gUmbraFaults{0};
bool gInstalled = false;
std::atomic<int> gInFlight{0};
std::atomic<void const*> gMeshes[kMaxMeshes];
std::atomic<float> gPad{0.0f};
std::atomic<unsigned> gPadded{0};
// Each mesh's bounds as we last left them (game thread only): the engine's UpdateBounds can return without computing
// anything (an actor under a matinee's physics after a few ticks), and bounds still holding our last result are not
// padded a second time.
float gLast[kMaxMeshes][7] = {};
// PrimitiveComponent.Bounds (a BoxSphereBounds: Origin, BoxExtent, SphereRadius; 28 bytes), resolved on the game thread at
// Install; -1: not found, nothing is padded.
std::int32_t gBoundsOffset = -1;

void ResolveFields() {
    if (!ue3::runtime::Ready()) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    std::optional<ue3::PropertyInfo> const bounds = r.PropertyOf(r.FindClass("Engine.PrimitiveComponent"), "Bounds");
    ue3::runtime::FlushNotes();
    gBoundsOffset = bounds && bounds->kind == ue3::Kind::Struct && bounds->elementSize == 28 ? bounds->offset : -1;
}

bool IsFunctionStart(void* address) {
    DWORD64 imageBase = 0;
    auto const* const entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &imageBase, nullptr);
    return entry != nullptr && imageBase + entry->BeginAddress == reinterpret_cast<DWORD64>(address);
}

std::string EntryBytes(void const* address) {
    auto const* const bytes = static_cast<unsigned char const*>(address);
    std::string text;
    for (int i = 0; i < 12; ++i) {
        text += std::format("{:02x} ", bytes[i]);
    }
    return text;
}

// The box and the sphere the engine just computed, grown by the pad on every side.
void PadBounds(void* component, float pad, int slot) {
    auto* const bounds = reinterpret_cast<float*>(static_cast<std::uint8_t*>(component) + gBoundsOffset);
    float* const extent = bounds + 3;
    float* const radius = bounds + 6;
    if (std::memcmp(bounds, gLast[slot], sizeof(gLast[slot])) == 0) {
        return;  // still our last result: the engine computed nothing this call
    }
    if (!std::isfinite(extent[0]) || !std::isfinite(extent[1]) || !std::isfinite(extent[2]) || !std::isfinite(*radius)) {
        return;
    }
    extent[0] += pad;
    extent[1] += pad;
    extent[2] += pad;
    *radius += pad * 1.7320508f;  // the sphere still holds the box's corners
    write_journal::Wrote("BodyBounds.pad", extent, 4 * sizeof(float));
    std::memcpy(gLast[slot], bounds, sizeof(gLast[slot]));
    gPadded.fetch_add(1, std::memory_order_relaxed);
    if (gUmbraBounds != nullptr) {
        __try {
            gUmbraBounds(component);  // Umbra's box for the mesh, from the padded Bounds
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            gUmbraBounds = nullptr;  // never again this load
            gUmbraFaults.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

// No C++ objects with destructors here, so __finally keeps the in-flight count right even when an exception unwinds
// through the engine's call.
void UpdateBoundsHook(void* component) {
    gInFlight.fetch_add(1, std::memory_order_acquire);
    __try {
        gOrig(component);
        float const pad = gPad.load(std::memory_order_relaxed);
        if (component != nullptr && pad > 0.0f && gBoundsOffset >= 0) {
            for (int i = 0; i < kMaxMeshes; ++i) {
                if (gMeshes[i].load(std::memory_order_relaxed) == component) {
                    PadBounds(component, pad, i);
                    break;
                }
            }
        }
    } __finally {
        gInFlight.fetch_sub(1, std::memory_order_release);
    }
}

} // namespace

bool Install(hook::Context& init) {
    if (gInstalled) {
        return true;
    }
    if (kUpdateBoundsRva == 0) {
        return false;  // another game: no address
    }
    void* const target = init.Resolve(kUpdateBoundsRva);
    if (target == nullptr || !IsFunctionStart(target)) {
        log::Error("body bounds: UpdateBounds at {} is not a function start; not hooking", target);
        return false;
    }
    ResolveFields();
    if (gBoundsOffset < 0) {
        log::Warn("body bounds: PrimitiveComponent.Bounds not found through our reflection layer: the player's body isn't padded");
        return false;
    }
    void* const umbra = kUmbraBoundsRva != 0 ? init.Resolve(kUmbraBoundsRva) : nullptr;
    gUmbraBounds = umbra != nullptr && IsFunctionStart(umbra) ? reinterpret_cast<tUmbraBounds*>(umbra) : nullptr;
    log::Info("body bounds: UpdateBounds at {} (entry bytes {}); Bounds at +0x{:X}; Umbra's box from Bounds at {}{}", target,
        EntryBytes(target), gBoundsOffset, umbra, gUmbraBounds != nullptr ? "" : " (not a function start: Umbra keeps the tight box)");
    if (!hook::Install(init, kHookName, reinterpret_cast<tUpdateBounds*>(target), &UpdateBoundsHook, gOrig)) {
        log::Error("body bounds: the UpdateBounds hook failed");
        return false;
    }
    gInstalled = true;
    log::Info("hooked {}", kHookName);
    return true;
}

bool Uninstall(hook::Context& init, bool waitForCalls) {
    Pad(nullptr, 0, 0.0f);
    if (!gInstalled) {
        return true;
    }
    if (!hook::Uninstall(init, kHookName)) {
        return false;  // still live, and still calling gOrig
    }
    gInstalled = false;
    // A call already inside the detour finishes with the old code: this module must outlive it.
    for (int i = 0; waitForCalls && i < 200 && gInFlight.load() > 0; ++i) {
        Sleep(1);
    }
    bool const drained = gInFlight.load() == 0;
    if (!drained && waitForCalls) {
        log::Error("body bounds: {} UpdateBounds call(s) still in the hook 200 ms after unhooking", gInFlight.load());
    }
    gUmbraBounds = nullptr;
    log::Info("unhooked {}", kHookName);
    return drained;
}

bool Installed() {
    return gInstalled;
}

void Pad(void const* const* meshes, int count, float padUnits) {
    bool const on = meshes != nullptr && count > 0 && std::isfinite(padUnits) && padUnits > 0.0f;
    gPad.store(on ? padUnits : 0.0f, std::memory_order_relaxed);
    for (int i = 0; i < kMaxMeshes; ++i) {
        void const* const mesh = on && i < count ? meshes[i] : nullptr;
        if (gMeshes[i].load(std::memory_order_relaxed) != mesh) {
            std::memset(gLast[i], 0, sizeof(gLast[i]));  // another mesh in this slot: nothing of ours to recognise
            gMeshes[i].store(mesh, std::memory_order_relaxed);
        }
    }
}

Stats Take() {
    return {gPadded.exchange(0, std::memory_order_relaxed), gPad.load(std::memory_order_relaxed)};
}

} // namespace mevr::engine::body_bounds

// SPDX-License-Identifier: MIT
#include "engine/VoicePlace.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "engine/Addresses.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>

namespace mevr::engine::voice_place {
namespace {

constexpr char const* kHookName = "FISACTSoundSource::Update";

// research/69 "The voices, placed" (Ghidra on the LE1 project). 0x7BA90 is the ISACT sound source's Update (its vtable's
// slot +0x770 in the run at 0xFAD2A0). What it reads, by offset (native structs: no reflection):
constexpr std::uint32_t kUpdateRva = addresses::kSoundSourceUpdate;
constexpr std::size_t kSourceDevice = 0x8;     // the audio device
constexpr std::size_t kSourceWave = 0x10;      // the wave instance being played
constexpr std::size_t kSourcePaused = 0x38;    // not 0: Update returns at once
constexpr std::size_t kSourceEntity = 0x44;    // an ISACT handle of the first kind (0: none) ...
constexpr std::size_t kSourceStream = 0x4C;    // ... or of the second kind
constexpr std::size_t kDeviceIsact = 0x43C;    // the device's ISACT interface object
constexpr std::size_t kWaveComponent = 0x10;   // the wave instance's audio component
constexpr std::size_t kWaveVolume = 0x18;      // ... and its volume (sent clamped to 0..1)
// The ISACT interface's virtual calls Update makes for a handle of each kind: (interface, handle, relative flag),
// (interface, handle, position as three floats) and the volume.
constexpr std::size_t kEntityVolume = 0x310;   // (interface, handle, volume, 0)
constexpr std::size_t kEntityRelative = 0x328;
constexpr std::size_t kEntityPosition = 0x330;
constexpr std::size_t kStreamRelative = 0x448;
constexpr std::size_t kStreamPosition = 0x450;
constexpr std::size_t kStreamVolume = 0x478;   // (interface, handle, volume)

using tUpdate = void(void* source);
using tSetFlag = void(void* isact, void* handle, std::uint64_t flag);
using tSetVector = void(void* isact, void* handle, float const* xyz);
using tEntityVolume = void(void* isact, void* handle, float volume, std::uint64_t zero);
using tStreamVolume = void(void* isact, void* handle, float volume);

tUpdate* gOrig = nullptr;  // written before the hook goes live (core/Hook)
bool gInstalled = false;
std::atomic<int> gInFlight{0};

SRWLOCK gLock = SRWLOCK_INIT;
Voice gVoices[kMaxVoices];
std::atomic<int> gCount{0};
std::atomic<DWORD> gNamingThread{0};

std::atomic<unsigned> gUpdates{0};
std::atomic<unsigned> gPlaced{0};
std::atomic<unsigned> gOffThread{0};
std::atomic<unsigned> gFaults{0};
std::atomic<bool> gLoggedFirst{false};

// ActorComponent.Owner, resolved on the game thread at Install; -1: not found, nothing is placed.
std::int32_t gOwnerOffset = -1;
// The ISACT interface's vtable whose slots were checked to be code (once per vtable seen).
std::atomic<void const*> gCheckedVtable{nullptr};

void ResolveFields() {
    if (!ue3::runtime::Ready()) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    std::optional<ue3::PropertyInfo> const owner = r.PropertyOf(r.FindClass("Engine.ActorComponent"), "Owner");
    ue3::runtime::FlushNotes();
    gOwnerOffset = owner && owner->kind == ue3::Kind::Object ? owner->offset : -1;
}

bool IsFunctionStart(void* address) {
    DWORD64 imageBase = 0;
    auto const* const entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &imageBase, nullptr);
    return entry != nullptr && imageBase + entry->BeginAddress == reinterpret_cast<DWORD64>(address);
}

bool IsCode(void const* address) {
    MEMORY_BASIC_INFORMATION info{};
    if (address == nullptr || VirtualQuery(address, &info, sizeof(info)) == 0) {
        return false;
    }
    return info.State == MEM_COMMIT && info.Type == MEM_IMAGE && (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0;
}

// Every slot we call through is code inside a loaded image; checked once per vtable.
bool VtableChecked(void* const* vtable) {
    if (gCheckedVtable.load(std::memory_order_relaxed) == vtable) {
        return true;
    }
    for (std::size_t const slot : {kEntityVolume, kEntityRelative, kEntityPosition, kStreamRelative, kStreamPosition, kStreamVolume}) {
        if (!IsCode(vtable[slot / sizeof(void*)])) {
            return false;
        }
    }
    gCheckedVtable.store(vtable, std::memory_order_relaxed);
    return true;
}

template <typename T>
T Read(void const* base, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<std::uint8_t const*>(base) + offset, sizeof(T));
    return value;
}

struct Placed {
    bool done = false;
    bool entity = false;
    float at[3] = {};
    float volume = 0.0f;
    float gain = 1.0f;
};

// After the game's own update of this source: if its audio component belongs to a named actor, the position and the
// volume are sent again. No C++ objects with destructors (SEH).
void PlaceGuarded(void* source, Placed& placed) {
    __try {
        void* const device = Read<void*>(source, kSourceDevice);
        void* const wave = Read<void*>(source, kSourceWave);
        if (device == nullptr || wave == nullptr || Read<std::int32_t>(source, kSourcePaused) != 0) {
            return;
        }
        void* const isact = Read<void*>(device, kDeviceIsact);
        void* const component = Read<void*>(wave, kWaveComponent);
        if (isact == nullptr || component == nullptr) {
            return;
        }
        void const* const owner = Read<void*>(component, static_cast<std::size_t>(gOwnerOffset));
        if (owner == nullptr) {
            return;
        }
        Voice voice;
        bool found = false;
        AcquireSRWLockShared(&gLock);
        int const count = gCount.load(std::memory_order_relaxed);
        for (int i = 0; i < count; ++i) {
            if (gVoices[i].actor == owner) {
                voice = gVoices[i];
                found = true;
                break;
            }
        }
        ReleaseSRWLockShared(&gLock);
        if (!found) {
            return;
        }
        void* const* const vtable = Read<void* const*>(isact, 0);
        if (vtable == nullptr || !VtableChecked(vtable)) {
            return;
        }
        // ISACT's axes: the engine's X, Z and -Y, in metres.
        float const at[3] = {voice.at[0] * 0.01f, voice.at[2] * 0.01f, voice.at[1] * -0.01f};
        float const volume = std::clamp(Read<float>(wave, kWaveVolume), 0.0f, 1.0f) * voice.gain;
        void* const entity = Read<void*>(source, kSourceEntity);
        void* const stream = Read<void*>(source, kSourceStream);
        if (entity != nullptr) {
            reinterpret_cast<tSetFlag*>(vtable[kEntityRelative / sizeof(void*)])(isact, entity, 0);
            reinterpret_cast<tSetVector*>(vtable[kEntityPosition / sizeof(void*)])(isact, entity, at);
            if (voice.gain != 1.0f) {
                reinterpret_cast<tEntityVolume*>(vtable[kEntityVolume / sizeof(void*)])(isact, entity, volume, 0);
            }
        } else if (stream != nullptr) {
            reinterpret_cast<tSetFlag*>(vtable[kStreamRelative / sizeof(void*)])(isact, stream, 0);
            reinterpret_cast<tSetVector*>(vtable[kStreamPosition / sizeof(void*)])(isact, stream, at);
            if (voice.gain != 1.0f) {
                reinterpret_cast<tStreamVolume*>(vtable[kStreamVolume / sizeof(void*)])(isact, stream, volume);
            }
        } else {
            return;
        }
        placed.done = true;
        placed.entity = entity != nullptr;
        placed.at[0] = at[0];
        placed.at[1] = at[1];
        placed.at[2] = at[2];
        placed.volume = Read<float>(wave, kWaveVolume);
        placed.gain = voice.gain;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gFaults.fetch_add(1, std::memory_order_relaxed);
    }
}

void Place(void* source) {
    if (gOwnerOffset < 0 || gFaults.load(std::memory_order_relaxed) >= 3) {
        return;
    }
    if (GetCurrentThreadId() != gNamingThread.load(std::memory_order_relaxed)) {
        gOffThread.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    Placed placed;
    PlaceGuarded(source, placed);
    if (!placed.done) {
        return;
    }
    gPlaced.fetch_add(1, std::memory_order_relaxed);
    if (!gLoggedFirst.exchange(true, std::memory_order_relaxed)) {
        log::Info("voice place: the first voice placed: a source of the {} kind, at ({:.2f}, {:.2f}, {:.2f}) m in ISACT's axes, the game's "
                  "volume {:.2f} times {:.2f}", placed.entity ? "first (entity)" : "second (stream)", placed.at[0], placed.at[1], placed.at[2],
            placed.volume, placed.gain);
    }
}

void UpdateGuarded(void* source) {
    __try {
        gOrig(source);
    } __finally {
        gInFlight.fetch_sub(1, std::memory_order_release);
    }
}

void UpdateHook(void* source) {
    gInFlight.fetch_add(1, std::memory_order_acquire);
    UpdateGuarded(source);  // the count is given back there, whatever unwinds through the engine's call
    gUpdates.fetch_add(1, std::memory_order_relaxed);
    if (source != nullptr && gCount.load(std::memory_order_relaxed) > 0) {
        gInFlight.fetch_add(1, std::memory_order_acquire);
        Place(source);
        gInFlight.fetch_sub(1, std::memory_order_release);
    }
}

} // namespace

bool Install(hook::Context& init) {
    if (gInstalled) {
        return true;
    }
    if (kUpdateRva == 0) {
        return false;  // another game: no address
    }
    void* const target = init.Resolve(kUpdateRva);
    if (target == nullptr || !IsFunctionStart(target)) {
        log::Error("voice place: the sound source's Update at {} is not a function start; not hooking", target);
        return false;
    }
    ResolveFields();
    if (gOwnerOffset < 0) {
        log::Warn("voice place: ActorComponent.Owner not found through our reflection layer: voices stay as the game mixes them");
        return false;
    }
    if (!hook::Install(init, kHookName, reinterpret_cast<tUpdate*>(target), &UpdateHook, gOrig)) {
        log::Error("voice place: the sound source Update hook failed");
        return false;
    }
    gInstalled = true;
    log::Info("hooked {} at {} (ActorComponent.Owner at +0x{:X})", kHookName, target, gOwnerOffset);
    return true;
}

bool Uninstall(hook::Context& init, bool waitForCalls) {
    Set(nullptr, 0);
    if (!gInstalled) {
        return true;
    }
    if (!hook::Uninstall(init, kHookName)) {
        return false;  // still live, and still calling gOrig
    }
    gInstalled = false;
    for (int i = 0; waitForCalls && i < 200 && gInFlight.load() > 0; ++i) {
        Sleep(1);
    }
    bool const drained = gInFlight.load() == 0;
    if (!drained && waitForCalls) {
        log::Error("voice place: {} Update call(s) still in the hook 200 ms after unhooking", gInFlight.load());
    }
    log::Info("unhooked {}", kHookName);
    return drained;
}

bool Installed() {
    return gInstalled;
}

void Set(Voice const* voices, int count) {
    int const n = voices != nullptr ? std::clamp(count, 0, kMaxVoices) : 0;
    gNamingThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    if (n == 0 && gCount.load(std::memory_order_relaxed) == 0) {
        return;
    }
    AcquireSRWLockExclusive(&gLock);
    for (int i = 0; i < n; ++i) {
        gVoices[i] = voices[i];
    }
    gCount.store(n, std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&gLock);
}

Stats Take() {
    Stats stats;
    stats.updates = gUpdates.exchange(0, std::memory_order_relaxed);
    stats.placed = gPlaced.exchange(0, std::memory_order_relaxed);
    stats.offThread = gOffThread.exchange(0, std::memory_order_relaxed);
    stats.faults = gFaults.load(std::memory_order_relaxed);
    stats.voices = gCount.load(std::memory_order_relaxed);
    return stats;
}

} // namespace mevr::engine::voice_place

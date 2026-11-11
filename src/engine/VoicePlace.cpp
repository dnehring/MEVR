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
// ... and whether the game places it in the world (research/69 A1, 2026-11-10, read from the same function): the int the
// update reads to choose between the wave's place in the world (not 0) and the listener's front (0), and which it clears
// itself for a cue it forces to the front (a sound group of its own, a speaker channel). Read after the game's update: 0 is
// a source the game left in the centre.
constexpr std::size_t kWaveSpatialized = 0x78;
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

// Each publisher's list (replaced whole by its Set) and who of it was heard since its last TakeHeard. Written and read on
// the game thread; the lock because the hook reads the lists.
struct List {
    Voice voices[kMaxPerPublisher];
    int count = 0;
};
struct Heard {
    void const* actors[kMaxPerPublisher] = {};
    int count = 0;
};
SRWLOCK gLock = SRWLOCK_INIT;
List gLists[kPublishers];
Heard gHeard[kPublishers];
std::atomic<int> gTotal{0};  // actors named, all lists
std::atomic<DWORD> gNamingThread{0};

std::atomic<unsigned> gUpdates{0};
std::atomic<unsigned> gPlaced{0};
std::atomic<unsigned> gPlacedBy[kPublishers]{};
std::atomic<unsigned> gLeftPlaced{0};
std::atomic<unsigned> gOffThread{0};
std::atomic<unsigned> gFaults{0};
std::atomic<bool> gLoggedFirst[kPublishers]{};
// Never reset (SinceInstall).
std::atomic<unsigned long long> gAllUpdates{0};
std::atomic<unsigned long long> gAllPlacedBy[kPublishers]{};
std::atomic<unsigned long long> gAllLeftPlaced{0};

// The census (StartCensus): game thread only (the hook records it on the naming thread).
std::atomic<std::uint64_t> gCensusUntilMs{0};
CensusEntry gCensus[kCensusEntries];
int gCensusCount = 0;
bool gCensusFull = false;

// Resolved on the game thread at Install; -1: not found. Owner is required (nothing is placed without it); the other two
// are for the census only.
std::int32_t gOwnerOffset = -1;      // ActorComponent.Owner
std::int32_t gCueOffset = -1;        // AudioComponent.SoundCue
std::int32_t gSpatialOffset = -1;    // AudioComponent.CurrentUseSpatialization
// The ISACT interface's vtable whose slots were checked to be code (once per vtable seen).
std::atomic<void const*> gCheckedVtable{nullptr};

void ResolveFields() {
    if (!ue3::runtime::Ready()) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    std::optional<ue3::PropertyInfo> const owner = r.PropertyOf(r.FindClass("Engine.ActorComponent"), "Owner");
    ue3::Class* const audio = r.FindClass("Engine.AudioComponent");
    std::optional<ue3::PropertyInfo> const cue = audio != nullptr ? r.PropertyOf(audio, "SoundCue") : std::nullopt;
    std::optional<ue3::PropertyInfo> const spatial = audio != nullptr ? r.PropertyOf(audio, "CurrentUseSpatialization") : std::nullopt;
    ue3::runtime::FlushNotes();
    gOwnerOffset = owner && owner->kind == ue3::Kind::Object ? owner->offset : -1;
    gCueOffset = cue && cue->kind == ue3::Kind::Object ? cue->offset : -1;
    gSpatialOffset = spatial && spatial->kind == ue3::Kind::Int ? spatial->offset : -1;
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
    bool leftPlaced = false;   // a centred-only actor's source the game had placed: left alone
    bool entity = false;
    int publisher = -1;
    void const* actor = nullptr;
    int census = -1;           // the census entry of this source (-1: none)
    float at[3] = {};
    float volume = 0.0f;
    float gain = 1.0f;
};

// The census entry of (owner, component), added if new; -1 when full. No C++ objects with destructors (called under SEH).
int CensusEntryOf(void const* owner, void const* component, void const* cue, bool spatialized, int componentSpatialization) {
    for (int i = 0; i < gCensusCount; ++i) {
        if (gCensus[i].owner == owner && gCensus[i].component == component) {
            gCensus[i].cue = cue;
            gCensus[i].spatialized = spatialized;
            gCensus[i].componentSpatialization = componentSpatialization;
            ++gCensus[i].updates;
            return i;
        }
    }
    if (gCensusCount >= kCensusEntries) {
        gCensusFull = true;
        return -1;
    }
    CensusEntry& e = gCensus[gCensusCount];
    e = CensusEntry{};
    e.owner = owner;
    e.component = component;
    e.cue = cue;
    e.spatialized = spatialized;
    e.componentSpatialization = componentSpatialization;
    e.updates = 1;
    return gCensusCount++;
}

// After the game's own update of this source: if its audio component belongs to a named actor, the position and the
// volume are sent again. No C++ objects with destructors (SEH).
void PlaceGuarded(void* source, bool census, Placed& placed) {
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
        bool const spatialized = Read<std::int32_t>(wave, kWaveSpatialized) != 0;
        if (census) {
            void const* const cue = gCueOffset >= 0 ? Read<void*>(component, static_cast<std::size_t>(gCueOffset)) : nullptr;
            int const componentSpatial = gSpatialOffset >= 0 ? Read<std::int32_t>(component, static_cast<std::size_t>(gSpatialOffset)) : -1;
            placed.census = CensusEntryOf(owner, component, cue, spatialized, componentSpatial);
        }
        if (owner == nullptr) {
            return;
        }
        Voice voice;
        int publisher = -1;
        AcquireSRWLockShared(&gLock);
        for (int p = 0; p < kPublishers && publisher < 0; ++p) {
            List const& list = gLists[p];
            for (int i = 0; i < list.count; ++i) {
                if (list.voices[i].actor == owner) {
                    voice = list.voices[i];
                    publisher = p;
                    break;
                }
            }
        }
        ReleaseSRWLockShared(&gLock);
        if (publisher < 0) {
            return;
        }
        if (voice.centredOnly && spatialized) {
            placed.leftPlaced = true;  // the game places this one itself (a bark with its falloff, a footstep): its mix stands
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
        placed.publisher = publisher;
        placed.actor = owner;
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

void NoteHeard(int publisher, void const* actor) {
    AcquireSRWLockExclusive(&gLock);
    Heard& heard = gHeard[publisher];
    bool known = false;
    for (int i = 0; i < heard.count && !known; ++i) {
        known = heard.actors[i] == actor;
    }
    if (!known && heard.count < kMaxPerPublisher) {
        heard.actors[heard.count++] = actor;
    }
    ReleaseSRWLockExclusive(&gLock);
}

void Place(void* source) {
    if (gOwnerOffset < 0 || gFaults.load(std::memory_order_relaxed) >= 3) {
        return;
    }
    if (GetCurrentThreadId() != gNamingThread.load(std::memory_order_relaxed)) {
        gOffThread.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::uint64_t const censusUntil = gCensusUntilMs.load(std::memory_order_relaxed);
    bool const census = censusUntil != 0 && GetTickCount64() < censusUntil;
    Placed placed;
    PlaceGuarded(source, census, placed);
    if (placed.leftPlaced) {
        gLeftPlaced.fetch_add(1, std::memory_order_relaxed);
        gAllLeftPlaced.fetch_add(1, std::memory_order_relaxed);
    }
    if (!placed.done) {
        return;
    }
    if (placed.census >= 0) {
        ++gCensus[placed.census].placed;
        gCensus[placed.census].placedBy = placed.publisher;
    }
    gPlaced.fetch_add(1, std::memory_order_relaxed);
    gPlacedBy[placed.publisher].fetch_add(1, std::memory_order_relaxed);
    gAllPlacedBy[placed.publisher].fetch_add(1, std::memory_order_relaxed);
    NoteHeard(placed.publisher, placed.actor);
    if (!gLoggedFirst[placed.publisher].exchange(true, std::memory_order_relaxed)) {
        log::Info("voice place: the first source placed for {}: a source of the {} kind, at ({:.2f}, {:.2f}, {:.2f}) m in ISACT's axes, the "
                  "game's volume {:.2f} times {:.2f}", Name(static_cast<Publisher>(placed.publisher)),
            placed.entity ? "first (entity)" : "second (stream)", placed.at[0], placed.at[1], placed.at[2], placed.volume, placed.gain);
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
    gAllUpdates.fetch_add(1, std::memory_order_relaxed);
    if (source != nullptr && (gTotal.load(std::memory_order_relaxed) > 0 || gCensusUntilMs.load(std::memory_order_relaxed) != 0)) {
        gInFlight.fetch_add(1, std::memory_order_acquire);
        Place(source);
        gInFlight.fetch_sub(1, std::memory_order_release);
    }
}

} // namespace

char const* Name(Publisher publisher) {
    switch (publisher) {
    case Publisher::Conversation: return "the conversation";
    case Publisher::Overheard: return "the people near the player";
    case Publisher::Gun: return "the player's gun";
    }
    return "?";
}

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
    log::Info("hooked {} at {} (ActorComponent.Owner at +0x{:X}; the census reads AudioComponent.SoundCue {} and "
              "CurrentUseSpatialization {})", kHookName, target, gOwnerOffset,
        gCueOffset >= 0 ? std::format("at +0x{:X}", gCueOffset) : std::string("(not found)"),
        gSpatialOffset >= 0 ? std::format("at +0x{:X}", gSpatialOffset) : std::string("(not found)"));
    return true;
}

bool Uninstall(hook::Context& init, bool waitForCalls) {
    for (int p = 0; p < kPublishers; ++p) {
        Set(static_cast<Publisher>(p), nullptr, 0);
    }
    gCensusUntilMs.store(0, std::memory_order_relaxed);
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
    Set(Publisher::Conversation, voices, std::min(count, kMaxVoices));
}

void Set(Publisher publisher, Voice const* voices, int count) {
    int const p = static_cast<int>(publisher);
    if (p < 0 || p >= kPublishers) {
        return;
    }
    int const n = voices != nullptr ? std::clamp(count, 0, kMaxPerPublisher) : 0;
    gNamingThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    AcquireSRWLockExclusive(&gLock);
    List& list = gLists[p];
    if (n == 0 && list.count == 0) {
        ReleaseSRWLockExclusive(&gLock);
        return;
    }
    for (int i = 0; i < n; ++i) {
        list.voices[i] = voices[i];
    }
    list.count = n;
    int total = 0;
    for (List const& l : gLists) {
        total += l.count;
    }
    gTotal.store(total, std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&gLock);
}

int TakeHeard(Publisher publisher, void const** out, int max) {
    int const p = static_cast<int>(publisher);
    if (p < 0 || p >= kPublishers || out == nullptr || max <= 0) {
        return 0;
    }
    AcquireSRWLockExclusive(&gLock);
    Heard& heard = gHeard[p];
    int const n = std::min(heard.count, max);
    for (int i = 0; i < n; ++i) {
        out[i] = heard.actors[i];
    }
    heard.count = 0;
    ReleaseSRWLockExclusive(&gLock);
    return n;
}

Stats Take() {
    Stats stats;
    stats.updates = gUpdates.exchange(0, std::memory_order_relaxed);
    gPlaced.exchange(0, std::memory_order_relaxed);
    for (int p = 0; p < kPublishers; ++p) {
        stats.placedBy[p] = gPlacedBy[p].exchange(0, std::memory_order_relaxed);
    }
    stats.leftPlaced = gLeftPlaced.exchange(0, std::memory_order_relaxed);
    stats.offThread = gOffThread.exchange(0, std::memory_order_relaxed);
    stats.faults = gFaults.load(std::memory_order_relaxed);
    AcquireSRWLockShared(&gLock);
    for (int p = 0; p < kPublishers; ++p) {
        stats.voicesBy[p] = gLists[p].count;
    }
    ReleaseSRWLockShared(&gLock);
    // `placed` and `voices` keep their meaning for game/ConversationWorld's heartbeat line: the conversation's list.
    stats.placed = stats.placedBy[static_cast<int>(Publisher::Conversation)];
    stats.voices = stats.voicesBy[static_cast<int>(Publisher::Conversation)];
    return stats;
}

Totals SinceInstall() {
    Totals t;
    t.updates = gAllUpdates.load(std::memory_order_relaxed);
    for (int p = 0; p < kPublishers; ++p) {
        t.placedBy[p] = gAllPlacedBy[p].load(std::memory_order_relaxed);
    }
    t.leftPlaced = gAllLeftPlaced.load(std::memory_order_relaxed);
    return t;
}

void StartCensus(double seconds) {
    gNamingThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    gCensusCount = 0;
    gCensusFull = false;
    double const ms = std::clamp(seconds, 0.5, 120.0) * 1000.0;
    gCensusUntilMs.store(GetTickCount64() + static_cast<std::uint64_t>(ms), std::memory_order_relaxed);
}

bool CensusRunning() {
    std::uint64_t const until = gCensusUntilMs.load(std::memory_order_relaxed);
    return until != 0 && GetTickCount64() < until;
}

std::vector<CensusEntry> TakeCensus(bool* full) {
    std::vector<CensusEntry> out(gCensus, gCensus + gCensusCount);
    if (full != nullptr) {
        *full = gCensusFull;
    }
    gCensusCount = 0;
    gCensusFull = false;
    if (!CensusRunning()) {
        gCensusUntilMs.store(0, std::memory_order_relaxed);  // the hook stops looking at every source for it
    }
    return out;
}

} // namespace mevr::engine::voice_place

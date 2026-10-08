#include "game/Reflections.hpp"

#include "core/Log.hpp"
#include "engine/Objects.hpp"

#include <LESDK/Headers.hpp>

#include <cstdint>

namespace mevr::game::reflections {
namespace {

constexpr float kFrameRate = 1000.0f;  // "every frame": higher than any real frame rate
constexpr double kScanSeconds = 3.0;  // between passes
// Objects examined per tick: a full pass over ~185k objects spreads over ~10 ticks instead of
// spiking one frame (which would itself be a stutter).
constexpr int kSliceObjects = 20000;
constexpr char const* kSetFrameRate = "Function Engine.SceneCaptureComponent.SetFrameRate";
// EObjectFlags. The SDK's operators truncate them to int, so test the 64-bit value directly.
constexpr std::uint64_t kTemplateFlags = 0x200 | 0x400;  // ClassDefaultObject | ArchetypeObject
constexpr std::uint64_t kPendingKill = 0x2000000000000000ull;

UFunction* gSetFrameRate = nullptr;
bool gDisabled = false;
double gTimer = kScanSeconds;  // first pass on the first tick
int gCursor = -1;  // next object index of the running pass; -1 between passes
unsigned gPassRaised = 0;
unsigned gPassFailed = 0;
unsigned gPassSeen = 0;
unsigned gRaised = 0;  // since the last LogStats
unsigned gFailed = 0;
unsigned gSeen = 0;  // in the last complete pass

bool IsLiveInstance(UObject const* object) {
    auto const flags = static_cast<std::uint64_t>(object->ObjectFlags);
    return (flags & (kTemplateFlags | kPendingKill)) == 0;
}

// One slice of the current pass. The class has no subclasses (SDK), so an exact class match
// replaces IsA's superclass walk.
void ScanSlice() {
    UClass* const reflectClass = USceneCaptureReflectComponent::StaticClass();
    auto* const objects = UObject::GObjObjects;
    int const count = static_cast<int>(objects->Count());
    int const end = gCursor + kSliceObjects < count ? gCursor + kSliceObjects : count;
    for (int i = gCursor; i < end && reflectClass != nullptr; ++i) {
        UObject* const object = objects->GetData()[i];
        if (object == nullptr || object->Class != reflectClass || !IsLiveInstance(object)) {
            continue;
        }
        auto* const capture = static_cast<USceneCaptureReflectComponent*>(object);
        ++gPassSeen;
        if (capture->FrameRate >= kFrameRate) {
            continue;
        }
        // Called directly: the SDK's generated wrapper corrupts FunctionFlags (CLAUDE.md).
        USceneCaptureComponent_execSetFrameRate_Parms parms;
        parms.NewFrameRate = kFrameRate;
        capture->ProcessEvent(gSetFrameRate, &parms, nullptr);
        if (capture->FrameRate >= kFrameRate) {
            ++gPassRaised;
        } else {
            ++gPassFailed;
        }
    }
    gCursor = end;
    if (end < count) {
        return;
    }
    gCursor = -1;  // pass complete
    gSeen = gPassSeen;
    gRaised += gPassRaised;
    gFailed += gPassFailed;
    if (gPassFailed > 0 && gPassRaised == 0) {
        // The engine didn't run the native this way; stop rather than retry every pass.
        gDisabled = true;
        log::Warn("reflections: SetFrameRate had no effect on {} components; fix disabled", gPassFailed);
    }
}

} // namespace

bool TryStartup() {
    for (UFunction* function : engine::FindFunctionsNamed(L"SetFrameRate")) {
        if (engine::FullName(function) == kSetFrameRate) {
            gSetFrameRate = function;
            log::Info("reflections: will raise planar reflection captures to every frame");
            return true;
        }
    }
    return false;
}

void Tick(float deltaSeconds) {
    if (gSetFrameRate == nullptr || gDisabled) {
        return;
    }
    if (gCursor < 0) {
        gTimer += deltaSeconds;
        if (gTimer < kScanSeconds) {
            return;
        }
        gTimer = 0.0;
        gCursor = 0;
        gPassSeen = gPassRaised = gPassFailed = 0;
    }
    ScanSlice();
}

void LogStats() {
    if (gSetFrameRate == nullptr) {
        return;
    }
    if (gRaised > 0 || gFailed > 0 || gSeen > 0) {
        log::Info("    reflections: {} reflection captures in the level; raised {}, failed {}{}", gSeen, gRaised, gFailed,
            gDisabled ? " (fix disabled)" : "");
    }
    gRaised = gFailed = 0;
}

void Shutdown() {
    gSetFrameRate = nullptr;
    gDisabled = false;
    gTimer = kScanSeconds;
    gCursor = -1;
    gPassSeen = gPassRaised = gPassFailed = 0;
    gRaised = gFailed = gSeen = 0;
}

} // namespace mevr::game::reflections

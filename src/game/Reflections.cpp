#include "game/Reflections.hpp"

#include "core/Log.hpp"
#include "engine/Objects.hpp"

#include <LESDK/Headers.hpp>

#include <cstdint>

namespace mevr::game::reflections {
namespace {

constexpr float kFrameRate = 1000.0f;  // "every frame": higher than any real frame rate
constexpr double kScanSeconds = 3.0;
constexpr char const* kSetFrameRate = "Function Engine.SceneCaptureComponent.SetFrameRate";
// EObjectFlags. The SDK's operators truncate them to int, so test the 64-bit value directly.
constexpr std::uint64_t kTemplateFlags = 0x200 | 0x400;  // ClassDefaultObject | ArchetypeObject
constexpr std::uint64_t kPendingKill = 0x2000000000000000ull;

UFunction* gSetFrameRate = nullptr;
bool gDisabled = false;
double gTimer = kScanSeconds;  // first scan on the first tick
unsigned gRaised = 0;
unsigned gFailed = 0;
unsigned gSeen = 0;

bool IsLiveInstance(UObject const* object) {
    auto const flags = static_cast<std::uint64_t>(object->ObjectFlags);
    return (flags & (kTemplateFlags | kPendingKill)) == 0;
}

void Scan() {
    UClass* const reflectClass = USceneCaptureReflectComponent::StaticClass();
    if (reflectClass == nullptr) {
        return;
    }
    unsigned raised = 0;
    unsigned failed = 0;
    unsigned seen = 0;
    auto* const objects = UObject::GObjObjects;
    for (int i = 0; i < static_cast<int>(objects->Count()); ++i) {
        UObject* const object = objects->GetData()[i];
        if (object == nullptr || !object->IsA(reflectClass) || !IsLiveInstance(object)) {
            continue;
        }
        auto* const capture = static_cast<USceneCaptureReflectComponent*>(object);
        ++seen;
        if (capture->FrameRate >= kFrameRate) {
            continue;
        }
        // Called directly: the SDK's generated wrapper corrupts FunctionFlags (CLAUDE.md).
        USceneCaptureComponent_execSetFrameRate_Parms parms;
        parms.NewFrameRate = kFrameRate;
        capture->ProcessEvent(gSetFrameRate, &parms, nullptr);
        if (capture->FrameRate >= kFrameRate) {
            ++raised;
        } else {
            ++failed;
        }
    }
    gSeen = seen;
    gRaised += raised;
    gFailed += failed;
    if (failed > 0 && raised == 0) {
        // The engine didn't run the native this way; stop rather than retry every scan.
        gDisabled = true;
        log::Warn("reflections: SetFrameRate had no effect on {} components; fix disabled", failed);
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
    gTimer += deltaSeconds;
    if (gTimer < kScanSeconds) {
        return;
    }
    gTimer = 0.0;
    Scan();
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
    gRaised = gFailed = gSeen = 0;
}

} // namespace mevr::game::reflections

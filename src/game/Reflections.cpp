// SPDX-License-Identifier: MIT
#include "game/Reflections.hpp"

#include "core/Log.hpp"
#include "ue3/Handles.hpp"

#include <cstdint>

namespace mevr::game::reflections {
namespace {

constexpr float kFrameRate = 1000.0f;  // "every frame": higher than any real frame rate
constexpr double kScanSeconds = 3.0;  // between passes
// Objects examined per tick: a full pass over ~185k objects spreads over ~10 ticks instead of
// spiking one frame (which would itself be a stutter).
constexpr int kSliceObjects = 20000;
constexpr char const* kSetFrameRate = "Engine.SceneCaptureComponent.SetFrameRate";

ue3::Function* gSetFrameRate = nullptr;
bool gDisabled = false;
double gTimer = kScanSeconds;  // first pass on the first tick
int gCursor = -1;  // next object index of the running pass; -1 between passes
unsigned gPassRaised = 0;
unsigned gPassFailed = 0;
unsigned gPassSeen = 0;
unsigned gRaised = 0;  // since the last LogStats
unsigned gFailed = 0;
unsigned gSeen = 0;  // in the last complete pass

// Our reflection layer on every game (research/34; LE2's since M1, docs/le2-continuation-plan.md; LE3's since T04,
// docs/trilogy-plan.md). With the layer off the fix is off (no LExSDKv2 fallback).
ue3::FunctionRef gSetFrameRateRef{kSetFrameRate, ue3::kEveryGame};
ue3::ClassRef gReflectClass{"Engine.SceneCaptureReflectComponent", ue3::kEveryGame};
ue3::FieldRef gFrameRate{"Engine.SceneCaptureComponent", "FrameRate", ue3::kEveryGame};
// EObjectFlags (64-bit): ClassDefaultObject | ArchetypeObject.
constexpr std::uint64_t kTemplateFlags = 0x200 | 0x400;

ue3::Function* FindSetFrameRate() {
    if (!ue3::runtime::Up()) {
        log::Warn("reflections: off: our reflection layer is off (see the 'reflect:' log lines)");
        return nullptr;
    }
    return gSetFrameRateRef.Get();
}

// Objects [begin, begin + kSliceObjects) of the pass. The class has no subclasses, so an exact class
// match replaces IsA's superclass walk. Returns where the pass continues; `done`: that was the end.
int Slice(int begin, bool& done) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const reflectClass = gReflectClass.Get();
    int const count = ue3::runtime::Up() ? r.ObjectCount() : 0;
    int const end = begin + kSliceObjects < count ? begin + kSliceObjects : count;
    done = end >= count;
    for (int i = begin; i < end && reflectClass != nullptr; ++i) {
        ue3::Object* const object = r.ObjectAt(i);
        if (object == nullptr || r.ClassOf(object) != reflectClass) {
            continue;
        }
        if ((r.FlagsOf(object) & (kTemplateFlags | r.layout().pendingKillFlag)) != 0) {
            continue;
        }
        float* const frameRate = gFrameRate.In<float>(object);
        if (frameRate == nullptr) {
            continue;
        }
        ++gPassSeen;
        if (*frameRate >= kFrameRate) {
            continue;
        }
        // Through ProcessEvent with the function's own block (never a generated wrapper: CLAUDE.md).
        ue3::Params parms(r, gSetFrameRate);
        parms.Set("NewFrameRate", kFrameRate);
        ue3::runtime::Call(object, gSetFrameRate, parms);
        if (*frameRate >= kFrameRate) {
            ++gPassRaised;
        } else {
            ++gPassFailed;
        }
    }
    return end;
}


// One slice of the current pass.
void ScanSlice() {
    bool done = false;
    gCursor = Slice(gCursor, done);
    if (!done) {
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
    gSetFrameRate = FindSetFrameRate();
    if (gSetFrameRate == nullptr) {
        return false;
    }
    log::Info("reflections: will raise planar reflection captures to every frame");
    return true;
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

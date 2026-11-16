// SPDX-License-Identifier: MIT
#include "plugin/Le3SceneCapture.hpp"
#include "plugin/SceneCaptureTarget.hpp"
#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "core/vr/SceneCaptureScope.hpp"
#include "engine/Addresses.hpp"

namespace mevr::host::le3_scene_capture {
namespace {
using Capture = void(void*, void*);
using Render = void(void*);
using Backbuffer = void(void*, int);
Capture* captureOriginal = nullptr;
Render* renderOriginal = nullptr;
Render* umbraOriginal = nullptr;
Backbuffer* backbufferOriginal = nullptr;
void** dynamicRhi = nullptr;
thread_local int depth = 0;
thread_local void* captureRenderer = nullptr;
unsigned bindLogs = 0, failureLogs = 0;

void CaptureHook(void* capture, void* renderer) {
    void* const outer = captureRenderer;
    ++depth; captureRenderer = nullptr;
    __try { captureOriginal(capture, renderer); }
    __finally { captureRenderer = outer; --depth; }
}
void RenderScoped(void* renderer, Render* original) {
    void* const outer = captureRenderer;
    if (depth > 0) captureRenderer = renderer;
    __try { original(renderer); }
    __finally { captureRenderer = outer; }
}
void RenderHook(void* renderer) { RenderScoped(renderer, renderOriginal); }
void UmbraHook(void* renderer) { RenderScoped(renderer, umbraOriginal); }
bool Bind(scene_capture_target::Info const& target) noexcept {
    __try { return dynamicRhi && scene_capture_target::Bind(target, *dynamicRhi); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void BackbufferHook(void* targets, int flags) {
    if (depth > 0) {
        auto const target = scene_capture_target::Read(captureRenderer);
        if (vr::scene_capture::ChooseFinalTarget(depth, target.target != nullptr, target.sizeX, target.sizeY) ==
                vr::scene_capture::FinalTarget::CaptureTarget && Bind(target)) {
            if (bindLogs++ < 3) log::Info("LE3 scene capture: final post effect bound its own {}x{} target {}", target.sizeX, target.sizeY, target.target);
            return;
        }
        if (failureLogs++ < 3) log::Warn("LE3 scene capture: final target unavailable; native binding retained");
    }
    backbufferOriginal(targets, flags);
}
}
void Attach(hook::Context& hooks) {
    namespace a = engine::addresses::le3;
    // Do not install a new render hook on a mere structural/changed-byte match.
    for (auto rva : {a::kCaptureScene, a::kCaptureRender, a::kCaptureRenderUmbra, a::kBeginBackbuffer, a::kDynamicRhi}) {
        auto const* result = exe::Find(rva);
        if (!result || result->status != exe::Status::AtRva) {
            log::Warn("LE3 scene capture: native contract not exact at 0x{:X}; target fix disabled", rva);
            return;
        }
    }
    dynamicRhi = hooks.ResolveTyped<void*>(a::kDynamicRhi);
    // Install the depth hook last. Partial installation is pass-through only.
    bool const ok = hook::Install(hooks, "Capture.BeginBackbuffer", hooks.ResolveTyped<Backbuffer>(a::kBeginBackbuffer), &BackbufferHook, backbufferOriginal) &&
        hook::Install(hooks, "Capture.Render", hooks.ResolveTyped<Render>(a::kCaptureRender), &RenderHook, renderOriginal) &&
        hook::Install(hooks, "Capture.RenderUmbra", hooks.ResolveTyped<Render>(a::kCaptureRenderUmbra), &UmbraHook, umbraOriginal) &&
        hook::Install(hooks, "Capture.Scene", hooks.ResolveTyped<Capture>(a::kCaptureScene), &CaptureHook, captureOriginal);
    log::Info("LE3 scene capture: shared final-target correction {}", ok ? "installed" : "unavailable");
}
}

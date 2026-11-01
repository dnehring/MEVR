// SPDX-License-Identifier: MIT
#pragma once

// A scene capture renders INSIDE the eye's render command (research/47 "Why a capture took an eye"): the capture loop
// runs at the top of 0x76EE20, before that eye's own passes. So the per-render state the eye's hooks keep is the wrong
// state for a capture's passes: the rules for it, free of the engine so the tests can read them (plugin/RenderLevers
// holds the thread-local state and the hooks).

#include <cstdint>

namespace mevr::vr::scene_capture {

// What the eye's scene-render hook arranged for the render it is inside.
struct EyeRenderState {
    int renderIndex = -1;         // the render's place in the frame; -1 = no eye (every eye check false)
    bool skipPasses = false;      // sharePass 3: the pass functions return at once
    bool oneDrawFinish = false;   // sharePass 5: this render's finish ends the recording and replays the second eye
    bool oneDrawCheck = false;    // sharePass 5 in the fallback: this render's finish checks the pass-1 record
};

// The state a capture's passes run under: no eye index, the passes run, and no finish of the capture's may be taken for
// the eye's. The caller puts the returned-from state back when the capture is done.
constexpr EyeRenderState DuringCapture(EyeRenderState) {
    return EyeRenderState{};
}

// The final post-process pass asks the scene targets for the backbuffer. The eye's render gets the backbuffer. A
// capture's gets its own target (the view family's), when that is known and has a size: its quad covers the family's
// target in clip space, so on the backbuffer it would fill the whole eye.
enum class FinalTarget { Backbuffer, CaptureTarget };
constexpr FinalTarget ChooseFinalTarget(int captureDepth, bool haveRenderer, std::uint32_t sizeX, std::uint32_t sizeY) {
    return captureDepth > 0 && haveRenderer && sizeX != 0 && sizeY != 0 ? FinalTarget::CaptureTarget : FinalTarget::Backbuffer;
}

// The eye's draw counter runs through the captures inside it; what the recorder should have seen is the difference.
constexpr std::uint32_t EyeDrawsIssued(std::uint32_t after, std::uint32_t before, std::uint32_t inCaptures) {
    std::uint32_t const issued = after - before;
    return inCaptures < issued ? issued - inCaptures : 0;
}

}  // namespace mevr::vr::scene_capture

// SPDX-License-Identifier: MIT
// A conversation's subtitle and wheel on quads of their own (plugin/ConversationUi.hpp).
#include "plugin/ConversationUi.hpp"

#include "core/Log.hpp"
#include "core/vr/InteractLabel.hpp"
#include "plugin/QuadSwapchain.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/WheelPanel.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace mevr::host::conversation_ui {
namespace {

namespace cwv = vr::conversation_world;
namespace il = vr::interact;

void LogBoth(std::string const& line) {
    log::Info("{}", line);
    renderer::LogLine(line);
}

QuadSwapchain gSubtitleSwapchain;
XrCompositionLayerQuad gSubtitleQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
struct Shown {
    bool subtitle = false;
    bool wheel = false;
    std::uint32_t speakerSerial = 0;
    std::uint32_t conversation = 0;
    XrPosef wheelPose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
};
Shown gShown;

il::Rect CropOr(float const crop[4], il::Rect const& fallback) {
    return crop[2] > 0.0f && crop[3] > 0.0f ? il::Rect{crop[0], crop[1], crop[2], crop[3]} : fallback;
}

} // namespace

unsigned Layers(Frame const& frame, cwv::State const& convo, hud_panel::Masks& masks, XrCompositionLayerQuad const** out) {
    unsigned n = 0;
    std::uint32_t const width = hud_panel::Width();
    std::uint32_t const height = hud_panel::Height();
    bool const ready = frame.allowed && frame.device != nullptr && frame.context != nullptr && width > 0 && height > 0;
    // The speaker's line under their face, facing the eyes, the same angular size at any distance (the interaction label's way).
    il::Pixels const subPx = il::ToPixels(CropOr(convo.subtitleCrop, cwv::DefaultSubtitleCrop(width, height)), width, height);
    bool subtitleShown = false;
    if (ready && convo.subtitle != 0 && !gSubtitleSwapchain.Refused() && subPx.width > 0 && subPx.height > 0) {
        il::Quad placed;
        XrVector3f const at{convo.subtitleAt[0], convo.subtitleAt[1], convo.subtitleAt[2]};
        float const aspect = static_cast<float>(subPx.width) / static_cast<float>(subPx.height);
        if (il::Place(at, frame.head.position, convo.subtitleHeightDeg > 0.0f ? convo.subtitleHeightDeg : 4.0f, aspect, placed) &&
            quad_swapchain::Ensure(frame.session, gSubtitleSwapchain, subPx.width, subPx.height, "conversation subtitle", "convosub") &&
            hud_panel::Blit(frame.context, frame.device, gSubtitleSwapchain, {subPx.x, subPx.y, subPx.width, subPx.height}, 1,
                hud_panel::Masks{}, frame.whiteScale, frame.options) != nullptr) {
            gSubtitleQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gSubtitleQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gSubtitleQuad.space = frame.appSpace;
            gSubtitleQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gSubtitleQuad.subImage.swapchain = gSubtitleSwapchain.handle;
            gSubtitleQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(subPx.width), static_cast<int32_t>(subPx.height)}};
            gSubtitleQuad.pose = placed.pose;
            gSubtitleQuad.size = {placed.width, placed.height};
            masks.Add(subPx.x, subPx.y, subPx.width, subPx.height);
            out[n++] = &gSubtitleQuad;
            subtitleShown = true;
            if (!gShown.subtitle || gShown.speakerSerial != convo.speakerSerial) {
                char line[320];
                std::snprintf(line, sizeof line,
                    "[MEVR-CONVO] the subtitle at the speaker: (%.2f, %.2f, %.2f) m, %.2f x %.2f m (%.1f deg tall); the UI's part (%u, %u) "
                    "%ux%u of %ux%u (%s)", placed.pose.position.x, placed.pose.position.y, placed.pose.position.z, placed.width,
                    placed.height, convo.subtitleHeightDeg, subPx.x, subPx.y, subPx.width, subPx.height, width, height,
                    convo.subtitleCrop[2] > 0.0f ? "[Cinematics] subtitleCrop" : "the default band");
                LogBoth(line);
            }
            gShown.speakerSerial = convo.speakerSerial;
        }
    }
    if (!subtitleShown && gShown.subtitle) LogBoth("[MEVR-CONVO] the subtitle at the speaker is gone");
    gShown.subtitle = subtitleShown;
    // The game's own wheel where it opens: in front of the player by default, or above a hand (the weapon wheel's way), its band
    // wide enough for the replies' whole text, the ring wheelWidthM wide whatever the band.
    il::Rect const wheelRect = CropOr(convo.wheelCrop, cwv::DefaultWheelCrop(width, height));
    il::Pixels const wheelPx = il::ToPixels(wheelRect, width, height);
    QuadSwapchain& wheelSwapchain = wheel_panel::Swapchain();
    bool wheelShown = false;
    if (ready && frame.wheelAllowed && convo.wheel != 0 && !wheelSwapchain.Refused() && wheelPx.width > 0 && wheelPx.height > 0) {
        float const quadWidth = std::clamp(cwv::WheelQuadWidth(convo.wheelWidthM > 0.0f ? convo.wheelWidthM : 0.40f, wheelRect.width),
            0.15f, 3.0f);
        if (!gShown.wheel || gShown.conversation != convo.conversation) {
            char const* from = "";
            gShown.wheelPose = wheel_panel::Place(convo.wheelHand, std::clamp(convo.wheelDistanceM, 0.05f, 0.8f), frame.head, from);
            char line[320];
            std::snprintf(line, sizeof line,
                "[MEVR-CONVO] the dialogue wheel %.2f m from %s, at (%.2f, %.2f, %.2f) m, %.2f m wide; the UI's part (%u, %u) %ux%u of "
                "%ux%u (%s)", convo.wheelDistanceM, from, gShown.wheelPose.position.x, gShown.wheelPose.position.y,
                gShown.wheelPose.position.z, quadWidth, wheelPx.x, wheelPx.y, wheelPx.width, wheelPx.height, width, height,
                convo.wheelCrop[2] > 0.0f ? "[Cinematics] wheelCrop" : "the default band");
            LogBoth(line);
        }
        gShown.conversation = convo.conversation;
        // At most 2048 px wide (the band holds the replies' whole text): the text keeps its size at its width on the quad.
        UINT const factor = std::clamp<UINT>((wheelPx.width + 2047u) / 2048u, 1u, 4u);
        std::uint32_t const w = std::max<std::uint32_t>(wheelPx.width / factor, 16);
        std::uint32_t const h = std::max<std::uint32_t>(wheelPx.height / factor, 16);
        if (quad_swapchain::Ensure(frame.session, wheelSwapchain, w, h, "conversation wheel", "wheel") &&
            hud_panel::Blit(frame.context, frame.device, wheelSwapchain, {wheelPx.x, wheelPx.y, wheelPx.width, wheelPx.height}, factor,
                hud_panel::Masks{}, frame.whiteScale, frame.options) != nullptr) {
            XrCompositionLayerQuad& quad = wheel_panel::Quad();
            quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            quad.space = frame.appSpace;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = wheelSwapchain.handle;
            quad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(w), static_cast<int32_t>(h)}};
            quad.pose = gShown.wheelPose;
            quad.size = {quadWidth, quadWidth * static_cast<float>(wheelPx.height) / static_cast<float>(wheelPx.width)};
            masks.Add(wheelPx.x, wheelPx.y, wheelPx.width, wheelPx.height);
            out[n++] = &quad;
            wheelShown = true;
        }
    }
    if (!wheelShown && gShown.wheel) LogBoth("[MEVR-CONVO] the dialogue wheel is gone");
    gShown.wheel = wheelShown;
    return n;
}

void ForgetSession() {
    gSubtitleSwapchain.Forget();
    gShown = {};
}

} // namespace mevr::host::conversation_ui

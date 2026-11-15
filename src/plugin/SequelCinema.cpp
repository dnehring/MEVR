// SPDX-License-Identifier: MIT
// Extracted from our LE2 bridge; one cinema layout, baseline and UI placement for the sequel renderers.
#include "plugin/SequelCinema.hpp"
#include "plugin/CineScreen.hpp"
#include "plugin/CinemaBackdrop.hpp"
#include "plugin/WorldPanels.hpp"
#include <atomic>
#include <cmath>

namespace mevr::host::sequel_cinema {
namespace {
std::atomic<float> gIpdM{0.0f};
XrCompositionLayerQuad gScreenQuads[2] = {{XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD}};
}
float HalfSeparationUU(float halfH) noexcept {
    namespace cs = mevr::vr::cine_screen;
    // The lens is THIS view's (calcview's SFR branch reads the projection it builds): no rescale for a cut, as LE1's render hook
    // needs. Unknown, it is planned at the widest lens a director uses (kMaxLensHalfH), as LE1's plan does.
    float const planHalfH = halfH > 0.0f ? halfH : cs::kMaxLensHalfH;
    cs::Frame const plan = mevr::host::cine_screen::PlanFrame(planHalfH, gIpdM.load(std::memory_order_relaxed),
        cs::kSceneUnitsPerMeter);   // convergeM is a distance in the scene, at its own scale (ME2's skeleton is ME1's)
    return 0.5f * plan.baselineUU;
}

unsigned Layers(const Frame& frame, const void** layersOut, unsigned capacity, State& gScreen) noexcept {
    gScreen = {};
    if (layersOut == nullptr || capacity < 2 || frame.session == nullptr || frame.appSpace == nullptr ||
        frame.eyeSwapchain[0] == nullptr || frame.eyeSwapchain[1] == nullptr || frame.eyeWidth == 0 || frame.eyeHeight == 0) {
        return 0;
    }
    float headQ[4] = {frame.eyeOrientation[0][0], frame.eyeOrientation[0][1], frame.eyeOrientation[0][2],
        frame.eyeOrientation[0][3]};
    float headP[3] = {};
    for (int i = 0; i < 3; ++i) {
        headP[i] = 0.5f * (frame.eyePosition[0][i] + frame.eyePosition[1][i]);
    }
    if (frame.headValid) {
        float const dx = frame.eyePosition[1][0] - frame.eyePosition[0][0];
        float const dy = frame.eyePosition[1][1] - frame.eyePosition[0][1];
        float const dz = frame.eyePosition[1][2] - frame.eyePosition[0][2];
        gIpdM.store(std::sqrt(dx * dx + dy * dy + dz * dz), std::memory_order_relaxed);
    }
    mevr::host::cine_screen::Quads const q = mevr::host::cine_screen::QuadsNow(frame.eyeWidth, frame.eyeHeight);
    float const viewPos[3] = {0.0f, 0.0f, -q.distanceM};
    namespace wp = mevr::host::world_panels;
    wp::Pose const placed = wp::Place(frame.conversation ? wp::Kind::Conversation : wp::Kind::Cutscene, viewPos, frame.viewSpace,
        frame.appSpace, frame.headValid, headQ, headP);
    float const heightM = q.heightM > 0.1f ? q.heightM : q.widthM * 9.0f / 16.0f;
    unsigned count = 0;
    if (capacity >= 3) {   // the floor and horizon under the screen's quads (LE1's order)
        mevr::host::cinema_backdrop::Frame bf;
        bf.session = frame.session;
        bf.appSpace = frame.appSpace;
        bf.context = frame.context;
        bf.headValid = frame.headValid && !placed.headLocked;   // a head-locked screen isn't in the room: no floor under it
        for (int e = 0; e < 2; ++e) {
            for (int i = 0; i < 4; ++i) {
                bf.eyeOrientation[e][i] = frame.eyeOrientation[e][i];
                bf.eyeFov[e][i] = frame.eyeFov[e][i];
            }
            for (int i = 0; i < 3; ++i) {
                bf.eyePosition[e][i] = frame.eyePosition[e][i];
            }
        }
        for (int i = 0; i < 4; ++i) {
            bf.screenOrientation[i] = placed.orientation[i];
        }
        for (int i = 0; i < 3; ++i) {
            bf.screenPosition[i] = placed.position[i];
        }
        bf.screenWidthM = q.widthM;
        // Its images in the shape of the eye's field of view (ME2's eye images are the game's 16:9 window, the headset's eyes
        // nearly square; LE1's eye images are the headset's shape): the eye images' height across, as tall as the view is.
        float const tanW = std::tan(frame.eyeFov[0][1]) - std::tan(frame.eyeFov[0][0]);
        float const tanH = std::tan(frame.eyeFov[0][2]) - std::tan(frame.eyeFov[0][3]);
        bf.eyeWidth = static_cast<int>(frame.eyeHeight);
        bf.eyeHeight = tanW > 0.1f && tanH > 0.1f ? static_cast<int>(static_cast<float>(frame.eyeHeight) * tanH / tanW)
                                                  : static_cast<int>(frame.eyeHeight);
        if (const void* backdrop = mevr::host::cinema_backdrop::Layer(bf)) {
            layersOut[count++] = backdrop;
        }
    }
    for (int e = 0; e < 2; ++e) {
        XrCompositionLayerQuad& quad = gScreenQuads[e];
        quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        quad.layerFlags = 0;
        quad.space = static_cast<XrSpace>(placed.space);
        quad.eyeVisibility = e == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
        quad.subImage.swapchain = static_cast<XrSwapchain>(frame.eyeSwapchain[e]);
        quad.subImage.imageArrayIndex = 0;
        bool const rectOk = q.rect[e][2] > 0 && q.rect[e][3] > 0;
        quad.subImage.imageRect = {{rectOk ? q.rect[e][0] : 0, rectOk ? q.rect[e][1] : 0},
            {rectOk ? q.rect[e][2] : static_cast<int32_t>(frame.eyeWidth), rectOk ? q.rect[e][3] : static_cast<int32_t>(frame.eyeHeight)}};
        if (!frame.stereoSource && rectOk) {
            // An identical stale-source fallback stays on the same screen at zero disparity.
            mevr::vr::cine_screen::EyeRects const stereo{
                {q.rect[0][0], q.rect[0][1], q.rect[0][2], q.rect[0][3]},
                {q.rect[1][0], q.rect[1][1], q.rect[1][2], q.rect[1][3]}};
            auto const mono = mevr::vr::cine_screen::MonoSourceRects(stereo).left;
            quad.subImage.imageRect = {{mono.x, mono.y}, {mono.width, mono.height}};
        }
        quad.pose.orientation = {placed.orientation[0], placed.orientation[1], placed.orientation[2], placed.orientation[3]};
        quad.pose.position = {placed.position[0], placed.position[1], placed.position[2]};
        quad.size = {q.widthM, heightM};
        layersOut[count++] = &quad;
    }
    mevr::host::cine_screen::NoteSubmit(frame.conversation, heightM, frame.stereoSource);
    gScreen.active = true;
    gScreen.space = placed.space;
    gScreen.headLocked = placed.headLocked;
    for (int i = 0; i < 4; ++i) {
        gScreen.orientation[i] = placed.orientation[i];
    }
    for (int i = 0; i < 3; ++i) {
        gScreen.position[i] = placed.position[i];
    }
    gScreen.widthM = q.widthM;
    gScreen.heightM = heightM;
    gScreen.crop = q.crop > 0.0f && q.crop < 0.25f ? q.crop : 0.0f;
    if (q.rect[0][3] > 0) {
        gScreen.rows[0] = static_cast<float>(q.rect[0][1]) / static_cast<float>(frame.eyeHeight);
        gScreen.rows[1] = static_cast<float>(q.rect[0][1] + q.rect[0][3]) / static_cast<float>(frame.eyeHeight);
    }
    return count;
}

void FillUi(State const& gScreen, float const headPosition[3], hud_panel::Frame& panel) noexcept {
        if (mevr::host::cine_screen::UiLayer const uiLayer = mevr::host::cine_screen::UiLayerWanted();
            gScreen.active && uiLayer.on && gScreen.widthM > 0.0f && gScreen.heightM > 0.0f) {
            namespace cs = mevr::vr::cine_screen;
            float const origin[3] = {0.0f, 0.0f, 0.0f};
            cs::UiPlace const at = cs::PlaceUi(gScreen.position, gScreen.headLocked ? origin : headPosition, uiLayer.forwardM);
            cs::UiPart const part = cs::UiPartShown(gScreen.crop, gScreen.rows[0], gScreen.rows[1]);
            float const scale = at.valid ? at.scale : 1.0f;
            panel.screenUi = true;
            panel.screenSpace = static_cast<XrSpace>(gScreen.space);
            panel.screenPose.orientation = {gScreen.orientation[0], gScreen.orientation[1], gScreen.orientation[2],
                gScreen.orientation[3]};
            float const* const p = at.valid ? at.position : gScreen.position;
            panel.screenPose.position = {p[0], p[1], p[2]};
            panel.screenSize = {gScreen.widthM * scale, gScreen.heightM * scale};
            panel.screenPart[0] = part.u0;
            panel.screenPart[1] = part.v0;
            panel.screenPart[2] = part.u1;
            panel.screenPart[3] = part.v1;
            panel.screenForwardM = at.valid ? uiLayer.forwardM : 0.0f;
        }
}
}

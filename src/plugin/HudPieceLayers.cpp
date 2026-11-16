// SPDX-License-Identifier: MIT
// The HUD pieces on the body, the host's half for both Halcyon hosts (plugin/HudPieceLayers.hpp): LE1's
// plugin/HalcyonOverlays.cpp code moved here unchanged, its log lines word for word.

#include "plugin/HudPieceLayers.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/InteractLabel.hpp"
#include "plugin/Renderer.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace mevr::host::hud_piece_layers {
namespace {

// Logs a line to our log and to the renderer's (as LE1's overlays).
void LogBoth(std::string const& line) {
    log::Info("{}", line);
    renderer::LogLine(line);
}

QuadSwapchain gWristSwapchain;
QuadSwapchain gGunSwapchain;
QuadSwapchain gInteractionSwapchain;
XrCompositionLayerQuad gInteractionQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
XrCompositionLayerQuad gWristQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
XrCompositionLayerQuad gGunQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};

struct PiecesState {
    bool wristShown = false;          // the wrist piece went out last frame
    bool gunShown = false;            // the gun piece went out last frame
    std::uint32_t gunSerial = 0;      // the mod's weapon it was logged for
    std::uint32_t lastPiecesSize = 0; // the mod's Pieces size last seen (to log a mismatch once)
    std::uint64_t nextLogMs = 0;      // the wrist's place, now and then
};
PiecesState gPieces;

hp::Settings gHudSettings;
std::string gHudSettingsText;
std::uint64_t gHudSettingsNextMs = 0;
FILETIME gHudSettingsStamp{};
bool gHudSettingsLoaded = false;

} // namespace

hp::Settings const& Settings(std::uint64_t now) {
    if (now < gHudSettingsNextMs) {
        return gHudSettings;
    }
    gHudSettingsNextMs = now + 1000;
    std::wstring const& path = mevr::host::SettingsPath();
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return gHudSettings;
    }
    if (gHudSettingsLoaded && CompareFileTime(&data.ftLastWriteTime, &gHudSettingsStamp) == 0) {
        return gHudSettings;
    }
    gHudSettingsStamp = data.ftLastWriteTime;
    gHudSettingsLoaded = true;
    mevr::ini::Loaded const loaded = mevr::ini::Read(std::filesystem::path(path));
    std::string problems;
    gHudSettings = hp::ReadSettings(loaded.file, &problems);
    if (!problems.empty()) {
        mevr::log::Warn("overlays: [Hud] {}", problems);
    }
    if (std::string const text = hp::Describe(gHudSettings); text != gHudSettingsText) {
        gHudSettingsText = text;
        LogBoth("[MEVR-HUD] pieces: " + text);
    }
    return gHudSettings;
}

hp::Pieces Wanted() {
    hp::Pieces latest = hp::Latest();
    if (latest.size == sizeof(hp::Pieces)) {
        return latest;
    }
    if (latest.size != 0 && latest.size != gPieces.lastPiecesSize) {
        gPieces.lastPiecesSize = latest.size;
        mevr::log::Warn("overlays: the mod's HUD pieces are {} bytes, the host's {}: the weapon panel stays on the HUD; rebuild both",
            latest.size, sizeof(hp::Pieces));
    }
    return hp::Pieces{};
}

void ForgetSession() {
    gWristSwapchain.Forget();
    gGunSwapchain.Forget();
    gInteractionSwapchain.Forget();
}

XrCompositionLayerQuad const* Interaction(Common const& c, bool wanted, hud_panel::Masks& masks) {
    namespace il = vr::interact;
    il::Label const label = il::Latest();
    if (!wanted || label.size != sizeof(il::Label) || !label.visible || gInteractionSwapchain.Refused()) return nullptr;
    // LE1's top-centre band is not ME2's layout. Require a measured band supplied by the operator until the native
    // ME2 prompt is captured and its default is verified. The native HUD remains intact without it.
    il::Rect const rect = il::ExplicitCrop(label);
    if (!rect.Valid()) return nullptr;
    il::Pixels const px = il::ToPixels(rect, hud_panel::Width(), hud_panel::Height());
    if (px.width == 0 || px.height == 0) return nullptr;
    il::Quad placed;
    if (!il::Place(label.position, c.head, label.heightDeg, static_cast<float>(px.width) / px.height, placed)) return nullptr;
    if (!Show(c.session, c.context, c.device, gInteractionSwapchain, gInteractionQuad, "interaction label", "label",
            {px.x, px.y, px.width, px.height}, label.position, c.head, placed.width, {}, c.appSpace, c.whiteScale, c.options)) {
        return nullptr;
    }
    masks.Add(px.x, px.y, px.width, px.height);
    return &gInteractionQuad;
}

bool WristRefused() {
    return gWristSwapchain.Refused();
}

bool GunRefused() {
    return gGunSwapchain.Refused();
}

bool Show(XrSession session, ID3D11DeviceContext* context, ID3D11Device* device, QuadSwapchain& swapchain,
    XrCompositionLayerQuad& quad, char const* what, char const* name, hp::Pixels const& px, XrVector3f const& position,
    XrVector3f const& head, float widthM, XrVector3f const& up, XrSpace appSpace, float whiteScale,
    hud_panel::BlitOptions const& options) {
    hp::Quad placed;
    float const aspect = static_cast<float>(px.width) / static_cast<float>(px.height);
    if (px.width == 0 || px.height == 0 || !hp::Place(position, head, widthM, aspect, placed, up) ||
        !quad_swapchain::Ensure(session, swapchain, px.width, px.height, what, name) ||
        hud_panel::Blit(context, device, swapchain, {px.x, px.y, px.width, px.height}, 1, hud_panel::Masks{}, whiteScale,
            options) == nullptr) {
        return false;
    }
    quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
    quad.space = appSpace;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = swapchain.handle;
    quad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(px.width), static_cast<int32_t>(px.height)}};
    quad.pose = placed.pose;
    quad.size = {placed.width, placed.height};
    return true;
}

// The player's status on the back of the left wrist: a small quad a fixed offset from the left grip pose, facing the
// eyes, upright (research/41).
XrCompositionLayerQuad const* Wrist(Common const& c, Piece const& piece) {
    if (piece.wanted && piece.px.width > 0) {
        if (Show(c.session, c.context, c.device, gWristSwapchain, gWristQuad, "wrist status", "wrist", piece.px, piece.position,
                c.head, piece.widthM, {0.0f, 0.0f, 0.0f}, c.appSpace, c.whiteScale, c.options)) {
            if (!gPieces.wristShown || c.nowMs >= gPieces.nextLogMs) {
                char line[320];
                std::snprintf(line, sizeof line,
                    "[MEVR-HUD] the player's status on the left wrist: (%.2f, %.2f, %.2f) m, %.3f x %.3f m; the UI's part (%u, %u) %ux%u "
                    "of %ux%u (%s)", gWristQuad.pose.position.x, gWristQuad.pose.position.y, gWristQuad.pose.position.z,
                    gWristQuad.size.width, gWristQuad.size.height, piece.px.x, piece.px.y, piece.px.width, piece.px.height,
                    hud_panel::Width(), hud_panel::Height(), piece.cropByHand ? "[Hud] wristCrop" : "the measured band");
                LogBoth(line);
                gPieces.nextLogMs = c.nowMs + 60000;
            }
            gPieces.wristShown = true;
            return &gWristQuad;
        }
        return nullptr;
    }
    if (gPieces.wristShown) {
        gPieces.wristShown = false;
        LogBoth(std::string("[MEVR-HUD] the player's status is back on the HUD panel") +
            (!piece.leftTracked ? " (the left controller isn't tracked)" : ""));
    }
    return nullptr;
}

// The weapon panel on the gun, where the mod says the held gun is (above the receiver, behind the sights), facing the
// eyes, leaning with the gun's top.
XrCompositionLayerQuad const* Gun(Common const& c, Piece const& piece) {
    if (piece.wanted && piece.px.width > 0) {
        if (Show(c.session, c.context, c.device, gGunSwapchain, gGunQuad, "gun status", "gun", piece.px, piece.position, c.head,
                piece.widthM, piece.up, c.appSpace, c.whiteScale, c.options)) {
            if (!gPieces.gunShown || gPieces.gunSerial != piece.serial) {
                char line[320];
                std::snprintf(line, sizeof line,
                    "[MEVR-HUD] the weapon panel on the gun: (%.2f, %.2f, %.2f) m, %.3f x %.3f m; the UI's part (%u, %u) %ux%u of %ux%u "
                    "(%s)", gGunQuad.pose.position.x, gGunQuad.pose.position.y, gGunQuad.pose.position.z, gGunQuad.size.width,
                    gGunQuad.size.height, piece.px.x, piece.px.y, piece.px.width, piece.px.height, hud_panel::Width(),
                    hud_panel::Height(), piece.cropByHand ? "[Hud] gunCrop" : "the measured band");
                LogBoth(line);
            }
            gPieces.gunShown = true;
            gPieces.gunSerial = piece.serial;
            return &gGunQuad;
        }
        return nullptr;
    }
    if (gPieces.gunShown) {
        gPieces.gunShown = false;
        LogBoth("[MEVR-HUD] the weapon panel is back on the HUD panel");
    }
    return nullptr;
}

Plan Decide(Frame const& frame, std::uint64_t nowMs, hud_panel::Masks& masks) {
    hp::Settings const& hud = Settings(nowMs);
    hp::Pieces const pieces = Wanted();
    vr::controllers::State const controllers = vr::controllers::Latest();
    bool const allowed = frame.gameplayOnFoot && frame.worldFrame && frame.headValid && frame.panelReady;
    Plan plan;
    plan.wrist.wanted = allowed && hud.wristStatus && !WristRefused() && controllers.active && controllers.left.gripValid;
    plan.wrist.leftTracked = controllers.active && controllers.left.gripValid;
    plan.wrist.px = hp::ToPixels(hp::WristCrop(hud, frame.panelWidth, frame.panelHeight, frame.layout), frame.panelWidth,
        frame.panelHeight);
    plan.wrist.cropByHand = hud.wristCrop.Valid();
    plan.wrist.widthM = hud.wristWidthM;
    if (plan.wrist.leftTracked) {
        plan.wrist.position = hp::WristPoint(controllers.left.grip, hud.wristOffset);
    }
    plan.gun.wanted = allowed && hud.gunStatus && pieces.gunVisible && !GunRefused();
    plan.gun.px = hp::ToPixels(hp::GunCrop(hud, frame.panelWidth, frame.panelHeight, frame.layout), frame.panelWidth,
        frame.panelHeight);
    plan.gun.cropByHand = hud.gunCrop.Valid();
    plan.gun.position = pieces.gunAnchor;
    plan.gun.widthM = hud.gunWidthM;
    plan.gun.up = pieces.gunUp;
    plan.gun.serial = pieces.serial;
    if (plan.wrist.wanted && plan.wrist.px.width > 0) {
        masks.Add(plan.wrist.px.x, plan.wrist.px.y, plan.wrist.px.width, plan.wrist.px.height);
    }
    if (plan.gun.wanted && plan.gun.px.width > 0) {
        masks.Add(plan.gun.px.x, plan.gun.px.y, plan.gun.px.width, plan.gun.px.height);
    }
    return plan;
}

unsigned Layers(Common const& common, Plan const& plan, bool panelShown, void const** layersOut, unsigned count,
    unsigned capacity) {
    Piece wrist = plan.wrist;
    Piece gun = plan.gun;
    wrist.wanted = wrist.wanted && panelShown && count < capacity;
    if (XrCompositionLayerQuad const* layer = Wrist(common, wrist)) {
        layersOut[count++] = layer;
    }
    gun.wanted = gun.wanted && panelShown && count < capacity;
    if (XrCompositionLayerQuad const* layer = Gun(common, gun)) {
        layersOut[count++] = layer;
    }
    return count;
}

} // namespace mevr::host::hud_piece_layers

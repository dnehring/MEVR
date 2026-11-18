// SPDX-License-Identifier: MIT
// The game's wheels on a panel of their own at the hand, the host's half for both Halcyon hosts (plugin/WheelPanel.hpp):
// LE1's plugin/HalcyonOverlays.cpp code moved here unchanged, its log lines word for word.

#include "plugin/WheelPanel.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "engine/GameAdapter.hpp"
#include "plugin/Renderer.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace mevr::host::wheel_panel {
namespace {

using Microsoft::WRL::ComPtr;
namespace wheel = vr::squad_menu::wheel;
namespace crop = vr::wheel_crop;

// Logs a line to our log and to the renderer's (as LE1's overlays).
void LogBoth(std::string const& line) {
    log::Info("{}", line);
    renderer::LogLine(line);
}

constexpr std::uint32_t kGridColumns = 48;
constexpr std::uint32_t kGridRows = 50;
constexpr std::uint64_t kBeforeEveryMs = 1000;  // a HUD-only grid this often while on foot (until measured)
constexpr std::uint64_t kWheelGridAfterMs = 600; // the wheel's grid this long after it opened (past its grow animation)

QuadSwapchain gWheelSwapchain;
XrCompositionLayerQuad gWheelQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};

struct WheelState {
    int mode = -1;                // 2 the power wheel, 3 the weapon wheel, -1 none (as last shown)
    std::uint32_t serial = 0;     // the mod's opening it was placed for
    bool placed = false;
    XrPosef pose{};
    std::uint64_t openedMs = 0;
    bool gridTaken = false;       // this opening's wheel grid
    bool loggedThisOpening = false;
    bool loggedSettings = false;
    std::uint32_t lastPanelSize = 0;  // the mod's Panel size last seen (to log a mismatch once)
};
WheelState gWheel;
std::atomic<bool> gWheelRecentered{false};

// The measured part of a frame of this size (once per session and size).
struct Measurement {
    UINT width = 0;
    UINT height = 0;
    crop::Rect rect;
    bool Valid(UINT w, UINT h) const { return width == w && height == h && rect.Valid(); }
};
Measurement gMeasured;

// Grid readbacks in flight, and the last HUD-only grid.
struct GridShot {
    ComPtr<ID3D11Texture2D> staging;
    bool wheel = false;
    int frames = 0;
    UINT width = 0;
    UINT height = 0;
};
std::vector<GridShot> gGridShots;
ComPtr<ID3D11Texture2D> gGridTarget;
ComPtr<ID3D11RenderTargetView> gGridRtv;
crop::Grid gBeforeGrid;
UINT gBeforeWidth = 0;
UINT gBeforeHeight = 0;
std::uint64_t gNextBeforeMs = 0;
bool gGridFailed = false;

// Draws the coverage grid of the panel texture and copies it for a readback a few frames later.
void CaptureGrid(ID3D11DeviceContext* context, ID3D11Device* device, bool wheelShot) {
    if (gGridFailed || gGridShots.size() >= 4 || !hud_panel::Ready()) {
        return;
    }
    if (gGridTarget == nullptr) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = kGridColumns;
        td.Height = kGridRows;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(device->CreateTexture2D(&td, nullptr, gGridTarget.GetAddressOf())) ||
            FAILED(device->CreateRenderTargetView(gGridTarget.Get(), nullptr, gGridRtv.GetAddressOf()))) {
            mevr::log::Error("overlays: the wheel's coverage grid target failed; the wheel panel keeps its default crop");
            gGridFailed = true;
            return;
        }
    }
    GridShot shot;
    D3D11_TEXTURE2D_DESC sd{};
    gGridTarget->GetDesc(&sd);
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&sd, nullptr, shot.staging.GetAddressOf()))) {
        gGridFailed = true;
        return;
    }
    if (!hud_panel::DrawCoverageGrid(context, device, gGridRtv.Get(), kGridColumns, kGridRows)) {
        return;
    }
    context->CopyResource(shot.staging.Get(), gGridTarget.Get());
    shot.wheel = wheelShot;
    shot.width = hud_panel::Width();
    shot.height = hud_panel::Height();
    gGridShots.push_back(std::move(shot));
}

// Reads finished grids: a HUD-only one is kept; a wheel one is compared with it (crop::MeasureCrop).
void PumpGrids(ID3D11DeviceContext* context) {
    for (auto it = gGridShots.begin(); it != gGridShots.end();) {
        GridShot& shot = *it;
        if (++shot.frames < 3) {
            ++it;
            continue;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT const hr = context->Map(shot.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING && shot.frames < 120) {
            ++it;
            continue;
        }
        if (FAILED(hr)) {
            it = gGridShots.erase(it);
            continue;
        }
        crop::Grid grid;
        grid.columns = kGridColumns;
        grid.rows = kGridRows;
        grid.cells.resize(static_cast<size_t>(kGridColumns) * kGridRows);
        for (std::uint32_t y = 0; y < kGridRows; ++y) {
            std::memcpy(grid.cells.data() + static_cast<size_t>(y) * kGridColumns,
                static_cast<BYTE const*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, kGridColumns);
        }
        context->Unmap(shot.staging.Get(), 0);
        if (!shot.wheel) {
            gBeforeGrid = std::move(grid);
            gBeforeWidth = shot.width;
            gBeforeHeight = shot.height;
        } else if (gBeforeWidth != shot.width || gBeforeHeight != shot.height || !gBeforeGrid.Valid()) {
            LogBoth("[MEVR-WHEEL] the wheel's place wasn't measured: no HUD-only picture of this size yet (the default crop stays)");
        } else if (auto const measured = crop::MeasureCrop(gBeforeGrid, grid); !measured) {
            LogBoth("[MEVR-WHEEL] the wheel's place wasn't measured: too little appeared when it opened (the default crop stays)");
        } else if (measured->rect.width < 0.1f || measured->rect.height < 0.05f || (measured->rect.width > 0.95f && measured->rect.height > 0.95f)) {
            char line[256];
            std::snprintf(line, sizeof line,
                "[MEVR-WHEEL] measured wheel area (%.3f, %.3f) %.3f x %.3f of the frame looks wrong: ignored (the default crop stays)",
                measured->rect.x, measured->rect.y, measured->rect.width, measured->rect.height);
            LogBoth(line);
        } else {
            gMeasured = {shot.width, shot.height, measured->rect};
            crop::Rect const def = crop::DefaultCrop(shot.width, shot.height);
            char line[384];
            std::snprintf(line, sizeof line,
                "[MEVR-WHEEL] measured: the wheel is at (%.3f, %.3f), %.3f x %.3f of the %ux%u frame (%u of %u changed cells); the "
                "default guess was (%.3f, %.3f), %.3f x %.3f. Used from now on ([Squad] wheelCrop sets one by hand)",
                measured->rect.x, measured->rect.y, measured->rect.width, measured->rect.height, shot.width, shot.height,
                measured->cells, measured->changed, def.x, def.y, def.width, def.height);
            LogBoth(line);
        }
        it = gGridShots.erase(it);
    }
}

// The settings the mod published (HostServices::setWheelPanel), or the defaults.
wheel::Panel WheelSettings() {
    wheel::Panel latest = wheel::LatestPanel();
    if (latest.size == sizeof(wheel::Panel)) {
        return latest;
    }
    if (latest.size != 0 && latest.size != gWheel.lastPanelSize) {
        gWheel.lastPanelSize = latest.size;
        mevr::log::Warn("overlays: the mod's wheel panel settings are {} bytes, the host's {}: defaults used; rebuild both",
            latest.size, sizeof(wheel::Panel));
    }
    return wheel::Panel{};
}

} // namespace

void ForgetSession() {
    gWheelSwapchain.Forget();
}

void Recentered() {
    gWheelRecentered.store(true, std::memory_order_release);
}

QuadSwapchain& Swapchain() {
    return gWheelSwapchain;
}

XrCompositionLayerQuad& Quad() {
    return gWheelQuad;
}

XrPosef Place(int place, float distance, XrPosef const& head, char const*& from) {
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (place == 0 || place == 1) {
        vr::controllers::Hand const& hand = place == 0 ? controllers.left : controllers.right;
        if (controllers.active && (hand.gripValid || hand.aimValid)) {
            from = place == 0 ? "the left hand" : "the right hand";
            return vr::squad_menu::PlaceAtHand(hand.gripValid ? hand.grip.position : hand.aim.position, head.position, distance);
        }
    }
    from = place == 2 ? "in front of the head" : "in front of the head (no hand)";
    XrQuaternionf const& q = head.orientation;
    XrVector3f const forward{-2.0f * (q.x * q.z + q.w * q.y), 0.0f, -(1.0f - 2.0f * (q.x * q.x + q.y * q.y))};
    float const length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    XrVector3f const f = length > 1e-4f ? XrVector3f{forward.x / length, 0.0f, forward.z / length} : XrVector3f{0.0f, 0.0f, -1.0f};
    XrVector3f const spot{head.position.x + f.x * 0.45f, head.position.y - 0.35f, head.position.z + f.z * 0.45f};
    return vr::squad_menu::PlaceAtHand(spot, head.position, distance);
}

int WheelOf(int gameMode) {
    switch (engine::adapter::GameMode(gameMode)) {
    case engine::adapter::Mode::PowerWheel: return 2;
    case engine::adapter::Mode::WeaponWheel: return 3;
    default: return -1;
    }
}

Plan Decide(ID3D11DeviceContext* context, int gameMode, bool allowed) {
    PumpGrids(context);
    Plan plan;
    plan.settings = WheelSettings();
    wheel::Panel const& wheelSettings = plan.settings;
    if (!gWheel.loggedSettings && wheelSettings.size == sizeof(wheel::Panel)) {
        gWheel.loggedSettings = true;
        mevr::log::Info("overlays: wheel panel settings from the mod: {}, weapon wheel {}, power wheel {}, {:.2f} m wide, {:.2f} m from "
                        "the hand, crop {}", wheelSettings.enabled ? "on" : "off",
            wheelSettings.weaponPlace == 0 ? "at the left hand" : wheelSettings.weaponPlace == 1 ? "at the right hand" : "in front",
            wheelSettings.powerPlace == 0 ? "at the left hand" : wheelSettings.powerPlace == 1 ? "at the right hand" : "in front",
            wheelSettings.width, wheelSettings.distance,
            wheelSettings.crop[2] > 0.0f ? std::format("({:.3f}, {:.3f}) {:.3f} x {:.3f}", wheelSettings.crop[0], wheelSettings.crop[1],
                                               wheelSettings.crop[2], wheelSettings.crop[3])
                                         : std::string("measured (auto)"));
    }
    plan.mode = WheelOf(gameMode);
    plan.place = plan.mode == 3 ? wheelSettings.weaponPlace : wheelSettings.powerPlace;  // -1: stays on the HUD panel
    plan.wanted = wheelSettings.enabled && plan.place >= 0 && allowed && plan.mode >= 0 && hud_panel::Ready() &&
        !gWheelSwapchain.Refused();
    crop::Rect cropRect;
    if (wheelSettings.crop[2] > 0.0f && wheelSettings.crop[3] > 0.0f) {
        cropRect = {wheelSettings.crop[0], wheelSettings.crop[1], wheelSettings.crop[2], wheelSettings.crop[3]};
        plan.cropFrom = "[Squad] wheelCrop";
    } else if (gMeasured.Valid(hud_panel::Width(), hud_panel::Height())) {
        cropRect = gMeasured.rect;
        plan.cropFrom = "measured";
    } else {
        cropRect = crop::DefaultCrop(hud_panel::Width(), hud_panel::Height());
    }
    plan.px = crop::ToPixels(cropRect, hud_panel::Width(), hud_panel::Height());
    plan.autoCrop = !(wheelSettings.crop[2] > 0.0f && wheelSettings.crop[3] > 0.0f);
    return plan;
}

XrCompositionLayerQuad const* Layer(Common const& c, Plan const& plan, bool panelShown, bool onFoot) {
    std::uint64_t const now = c.nowMs;
    wheel::Panel const& wheelSettings = plan.settings;
    crop::Pixels const& cropPx = plan.px;
    int const wheelMode = plan.mode;
    // A HUD-only picture of where the UI is, now and then, until the wheel's place has been measured.
    if (panelShown && wheelMode < 0 && onFoot && plan.autoCrop && !gMeasured.Valid(hud_panel::Width(), hud_panel::Height()) &&
        now >= gNextBeforeMs) {
        gNextBeforeMs = now + kBeforeEveryMs;
        CaptureGrid(c.context, c.device, false);
    }
    // The wheel's panel: placed when a wheel opens (and when the mod opens it again, or on recenter).
    bool const wheelRecentered = gWheelRecentered.exchange(false, std::memory_order_acq_rel);
    XrCompositionLayerQuad const* wheelLayer = nullptr;
    if (plan.wanted && panelShown) {
        bool const fresh = wheelMode != gWheel.mode;
        bool const reopened = wheelMode == 3 && wheelSettings.modOpen && wheelSettings.serial != gWheel.serial;
        if (fresh) {
            gWheel.openedMs = now;
            gWheel.gridTaken = false;
            gWheel.loggedThisOpening = false;
        }
        if (fresh || reopened || wheelRecentered || !gWheel.placed) {
            char const* from = "";
            gWheel.pose = Place(plan.place, std::clamp(wheelSettings.distance, 0.1f, 0.8f), c.head, from);
            gWheel.placed = true;
            char line[320];
            std::snprintf(line, sizeof line,
                "[MEVR-WHEEL] the %s wheel on its own panel%s: %.2f m from %s, at (%.2f, %.2f, %.2f) m, %.2f m wide; the UI's part "
                "(%u, %u) %ux%u of %ux%u (%s)", wheelMode == 3 ? "weapon" : "power", wheelRecentered ? " again (recentered)" : "",
                wheelSettings.distance, from, gWheel.pose.position.x, gWheel.pose.position.y, gWheel.pose.position.z,
                wheelSettings.width, cropPx.x, cropPx.y, cropPx.width, cropPx.height, hud_panel::Width(), hud_panel::Height(),
                plan.cropFrom);
            LogBoth(line);
        }
        gWheel.mode = wheelMode;
        gWheel.serial = wheelSettings.serial;
        UINT const factor = std::clamp<UINT>(static_cast<UINT>(std::lround(cropPx.width / 1024.0)), 1u, 4u);
        std::uint32_t const width = std::max<std::uint32_t>(cropPx.width / factor, 16);
        std::uint32_t const height = std::max<std::uint32_t>(cropPx.height / factor, 16);
        if (quad_swapchain::Ensure(c.session, gWheelSwapchain, width, height, "wheel panel", "wheel") &&
            hud_panel::Blit(c.context, c.device, gWheelSwapchain, {cropPx.x, cropPx.y, cropPx.width, cropPx.height}, factor,
                hud_panel::Masks{}, c.whiteScale, c.options) != nullptr) {
            float const quadWidth = std::clamp(wheelSettings.width, 0.15f, 1.5f);
            gWheelQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gWheelQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gWheelQuad.space = c.appSpace;
            gWheelQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gWheelQuad.subImage.swapchain = gWheelSwapchain.handle;
            gWheelQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(width), static_cast<int32_t>(height)}};
            gWheelQuad.pose = gWheel.pose;
            gWheelQuad.size = {quadWidth, quadWidth * static_cast<float>(cropPx.height) / static_cast<float>(cropPx.width)};
            wheelLayer = &gWheelQuad;
        }
        // Where the wheel really is: once per opening, after its grow animation, until measured.
        if (plan.autoCrop && !gWheel.gridTaken && !gMeasured.Valid(hud_panel::Width(), hud_panel::Height()) &&
            now - gWheel.openedMs >= kWheelGridAfterMs) {
            gWheel.gridTaken = true;
            CaptureGrid(c.context, c.device, true);
        }
    } else if (gWheel.mode >= 0 && wheelMode < 0) {
        LogBoth(std::string("[MEVR-WHEEL] the ") + (gWheel.mode == 3 ? "weapon" : "power") + " wheel closed; its panel is gone");
        gWheel.mode = -1;
        gWheel.placed = false;
    }
    return wheelLayer;
}

void Submitted() {
    if (!gWheel.loggedThisOpening) {
        gWheel.loggedThisOpening = true;
        mevr::log::Info("overlays: wheel panel submitted ({}x{}, {:.2f} x {:.2f} m)", gWheelSwapchain.width, gWheelSwapchain.height,
            gWheelQuad.size.width, gWheelQuad.size.height);
    }
}

} // namespace mevr::host::wheel_panel

// SPDX-License-Identifier: MIT
// Our quads on top of Halcyon's world layer (seams in src/halcyon/le1/mevr_bridge.h):
// - the body-locked HUD panel: d3d_capture.cpp's draw hooks send the game's UI draws of a
//   head-tracked frame into a panel texture instead of the eye images; here that texture goes into a
//   swapchain and becomes a quad fixed in the tracking space in front of the player's body (its core, game-independent
//   and shared with LE2's host: plugin/HudPanel);
// - the hand reticle: a small ring on the weapon's aim ray (or the right controller's aim pose
//   until the mod publishes the ray), replacing the game's screen-centre crosshair (plugin/HandReticle, also LE2's);
// - the squad weapon menu at the left hand while the mod has it open (plugin/SquadPanel.cpp);
// - the game's own weapon or power wheel on a panel of its own at a hand while one is up: its part of
//   the HUD panel texture (core/vr/WheelCrop: a default, then measured the first time a wheel opens),
//   cut out of the HUD panel meanwhile so it shows once (docs/squad-weapons-design.md, "Native wheel");
// - the game's interaction prompt (the target status widget: the object's name, its action and the A glyph) cut out
//   of the HUD panel and shown big at the object the mod says A would use (core/vr/InteractLabel, research/40);
// - the HUD pieces on the body (core/vr/HudPieces, research/41): the player's status (the health and shield arc, the
//   squad's portraits, the medi-gel count) on the back of the left wrist, a fixed offset from the left grip pose, and
//   the weapon panel (the gun, its heat strip, the grenade count) on the drawn gun where the mod says it is, both cut
//   out of the HUD panel meanwhile ([Hud] in ASI\LE1MEVR.ini, read here);
// - the comfort vignette (plugin/Vignette.cpp, research/31): head-locked, under the others, only while wanted;
// - [MENUVIS] readbacks that tell whether Halcyon's Insert menu texture has visible pixels.
//
// The panel texture holds premultiplied colour and coverage from the UI's "over" draws; Scaleform's other
// blend modes (multiply, screen) add a tint texture (white = the world shows unchanged) that the panel's
// alpha is made from, so they reach the panel too instead of staying in the eyes.

#include "halcyon/le1/mevr_bridge.h"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/AimRay.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/FrameProbe.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include "core/vr/GalaxyMap.hpp"
#include "core/vr/HudPieces.hpp"
#include "core/vr/MakoControls.hpp"
#include "core/vr/InteractLabel.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "core/vr/SquadMenu.hpp"
#include "core/vr/WheelCrop.hpp"
#include "plugin/Calibration.hpp"
#include "plugin/CineScreen.hpp"
#include "core/vr/CineScreen.hpp"
#include "plugin/FloorLock.hpp"
#include "plugin/Colour.hpp"
#include "plugin/ConversationWorld.hpp"
#include "plugin/GalaxyHologram.hpp"
#include "plugin/SquadPlaceDraw.hpp"
#include "plugin/UiWorldFigure.hpp"
#include "plugin/WorldUi.hpp"
#include "plugin/GamePointer.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/HandReticle.hpp"
#include "plugin/HudPanel.hpp"
#include "plugin/HudPieceLayers.hpp"
#include "plugin/QuadSwapchain.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/MainMenu.hpp"
#include "plugin/OmniPad.hpp"
#include "plugin/OmniToolRail.hpp"
#include "plugin/Thespian.hpp"
#endif
#include "plugin/Renderer.hpp"
#include "plugin/PerfProbe.hpp"
#include "plugin/RecenterTilt.hpp"
#include "plugin/SightLayers.hpp"
#include "plugin/Spectator.hpp"
#include "plugin/Swapchains.hpp"
#include "plugin/PowerWheel.hpp"
#include "plugin/SquadPanel.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/Vignette.hpp"
#include "plugin/WristBeam.hpp"
#include "plugin/WorldPanels.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace MELEVR::MevrBridge {
namespace {

using Microsoft::WRL::ComPtr;
namespace overlay = mevr::vr::overlay;

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;
float gPanelBackdrop = 0.0f;  // the world UI's backdrop for this frame's panel blit (research/53; 0: the HUD as it is)
// The UI world's squad member behind the world UI's panel this frame (research/57 section 7; nullopt: none).
std::optional<mevr::host::ui_world_figure::Picture> gPanelFigure;
constexpr float kGalaxyUiWidthM = 1.50f;  // the game's UI frame over the galaxy hologram's table (its travel box ~1 m of it)

// Logs a line to our log and to Halcyon's (one-shots that belong next to Halcyon's [XRSUBMIT] lines).
void LogBoth(std::string const& line) {
    mevr::log::Info("{}", line);
    mevr::host::renderer::LogLine(line);
}

// ---- The HUD panel's core (the panel texture, its blit, swapchain and placement) is plugin/HudPanel -------------
namespace hud_panel = mevr::host::hud_panel;
std::atomic<bool> gGalaxyEarlyUi{false};

// Immediate-context thread only. The eye-copy flag is false throughout Present, so it cannot
// distinguish next-frame Flash from runtime/desktop overlay work inside Present.
struct PanelPresent {
    bool active = false, galaxy = false, shown = false;
    unsigned long long number = 0;
    unsigned stage = 0; // before copy, copy-to-retire, after retire (including late wait), downstream Present
    unsigned draws = 0, text = 0, early = 0, startClears = 0;
    unsigned rejected[4]{}, textRejected[4]{}, firstShader[4]{};
};
thread_local PanelPresent tPanelPresent;
unsigned gGalaxyOrderLines = 0;

// Non-indexed Scaleform batches are the text/glyph proxy, not a pixel visibility test.
// Cumulative per opening: zero-text runs catch missing frames even between periodic log lines.
struct GalaxyUiStats {
    std::uint64_t frames = 0, noUi = 0, noText = 0, earlyFrames = 0, earlyUi = 0, earlyText = 0;
    unsigned noTextRun = 0, maxNoTextRun = 0, minDraws = ~0u, maxDraws = 0;
    std::uint64_t lastLogMs = 0;
};
GalaxyUiStats gGalaxyUiStats;

void NoteGalaxyUi(bool window, unsigned draws, unsigned text, unsigned early, unsigned earlyText,
                  bool armed, bool shown, std::uint64_t now) {
    auto& s = gGalaxyUiStats;
    if (window) {
        if (s.frames == 0) s.lastLogMs = now;
        ++s.frames;
        s.noUi += draws == 0 ? 1u : 0u;
        s.noText += text == 0 ? 1u : 0u;
        s.noTextRun = text == 0 ? s.noTextRun + 1 : 0;
        s.maxNoTextRun = std::max(s.maxNoTextRun, s.noTextRun);
        s.minDraws = std::min(s.minDraws, draws);
        s.maxDraws = std::max(s.maxDraws, draws);
        s.earlyFrames += early != 0 ? 1u : 0u;
        s.earlyUi += early;
        s.earlyText += earlyText;
    }
    if (s.frames != 0 && (!window || now - s.lastLogMs >= 5000)) {
        mevr::log::Info("galaxy UI: {} frames, {} with UI / {} without, {} with text / {} without (longest gap {}); "
                       "UI draws min/max {}/{}; before eye copy: {} frames, {} UI / {} text draws; armed {} shown {}{}",
            s.frames, s.frames - s.noUi, s.noUi, s.frames - s.noText, s.noText, s.maxNoTextRun,
            s.minDraws, s.maxDraws, s.earlyFrames, s.earlyUi, s.earlyText, armed, shown, window ? "" : "; window ended");
        s.lastLogMs = now;
    }
    if (!window) s = GalaxyUiStats{};
}

// ---- Swapchains (Present thread) ---------------------------------------------------------------
using mevr::host::QuadSwapchain;

XrSession gSession = XR_NULL_HANDLE;

// `evict`: how plugin/Swapchains lets go of it for room (default: Destroy; it's made again when next wanted).
bool CreateSwapchain(XrSession session, std::uint32_t width, std::uint32_t height, QuadSwapchain& out,
    char const* what, std::function<void()> evict = {}) {
    return mevr::host::quad_swapchain::Create(session, width, height, out, what, std::move(evict));
}

// Acquire + wait; false (and nothing held) if either fails.
bool AcquireImage(QuadSwapchain& swapchain, std::uint32_t& index) {
    return mevr::host::quad_swapchain::Acquire(swapchain, index);
}

void ReleaseImage(QuadSwapchain& swapchain) {
    mevr::host::quad_swapchain::Release(swapchain);
}

// ---- [MENUVIS] readback probes (Present thread) ---------------------------------------------------
struct Probe {
    ComPtr<ID3D11Texture2D> staging;
    std::string tag;
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    int frames = 0;
};
std::vector<Probe> gProbes;

int AlphaOffset(DXGI_FORMAT f, int& stride) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        stride = 4;
        return 3;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        stride = 8;
        return 6;
    default:
        stride = 0;
        return -1;
    }
}

float AlphaAt(BYTE const* px, DXGI_FORMAT f) {
    if (f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R16G16B16A16_TYPELESS) {
        std::uint16_t h = 0;
        std::memcpy(&h, px, 2);
        std::uint32_t const exp = (h >> 10) & 0x1F;
        float const mant = static_cast<float>(h & 0x3FF) / 1024.0f;
        float const v = exp == 0 ? mant * std::ldexp(1.0f, -14) : (1.0f + mant) * std::ldexp(1.0f, static_cast<int>(exp) - 15);
        return (h & 0x8000) ? -v : v;
    }
    return static_cast<float>(*px) / 255.0f;
}

void PumpProbes(ID3D11DeviceContext* context) {
    for (auto it = gProbes.begin(); it != gProbes.end();) {
        Probe& probe = *it;
        if (++probe.frames < 3) {  // let the GPU finish the copy first
            ++it;
            continue;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT const hr = context->Map(probe.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING && probe.frames < 120) {
            ++it;
            continue;
        }
        if (FAILED(hr)) {
            LogBoth("[MENUVIS] " + probe.tag + ": readback failed hr=" + std::to_string(static_cast<long>(hr)));
            it = gProbes.erase(it);
            continue;
        }
        int stride = 0;
        int const alphaOffset = AlphaOffset(probe.format, stride);
        std::uint64_t visible = 0;
        double alphaSum = 0.0;
        std::uint64_t samples = 0;
        if (alphaOffset >= 0) {
            for (UINT y = 0; y < probe.height; y += 2) {
                BYTE const* row = static_cast<BYTE const*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
                for (UINT x = 0; x < probe.width; x += 2) {
                    float const a = AlphaAt(row + static_cast<size_t>(x) * stride + alphaOffset, probe.format);
                    alphaSum += a;
                    visible += a > 0.02f ? 1 : 0;
                    ++samples;
                }
            }
        }
        BYTE const* center = static_cast<BYTE const*>(mapped.pData) +
            static_cast<size_t>(probe.height / 2) * mapped.RowPitch + static_cast<size_t>(probe.width / 2) * (stride > 0 ? stride : 4);
        char line[320];
        std::snprintf(line, sizeof line,
            "[MENUVIS] %s %ux%u fmt=%d: %.1f%% of pixels visible (alpha > 0.02), mean alpha %.3f, centre bytes "
            "%02X %02X %02X %02X%s",
            probe.tag.c_str(), probe.width, probe.height, static_cast<int>(probe.format),
            samples > 0 ? 100.0 * static_cast<double>(visible) / static_cast<double>(samples) : 0.0,
            samples > 0 ? alphaSum / static_cast<double>(samples) : 0.0, center[0], center[1], center[2], center[3],
            alphaOffset < 0 ? " (format not decoded)" : "");
        context->Unmap(probe.staging.Get(), 0);
        LogBoth(line);
        it = gProbes.erase(it);
    }
}

// ---- Placement and layers (Present thread) ---------------------------------------------------------
std::uint64_t gLastFrameMs = 0;
XrCompositionLayerQuad gPanelQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
bool gLoggedPanel = false;
bool gScreenUiShown = false;   // the 3D screen's UI is on its own layer (research/52 "Cross-eyed")
int gLastGameMode = -1;   // the last readable USFXGameModeManager mode
std::uint64_t gSightSeenMs = 0;    // an eye last looked through the gun's sight (the ring hides meanwhile)
std::uint64_t gNextPanelLogMs = 0;

// One blit of a window of the panel texture (and its tint) into a swapchain image, `factor` times
// smaller; pixels inside any of `masks` come out transparent (plugin/HudPanel). Returns the image written (released), or nullptr.
using Window = hud_panel::Window;
using Mask = hud_panel::Mask;
using Masks = hud_panel::Masks;

// This frame's blit options for `swapchain`: the world UI's backdrop for every blit, the UI world's squad member behind the
// world UI's panel (research/57 section 7), the HUD panel's own dither pattern.
hud_panel::BlitOptions BlitOptionsFor(QuadSwapchain const& swapchain) {
    hud_panel::BlitOptions options;
    options.backdrop = gPanelBackdrop;
    bool const panel = &swapchain == &hud_panel::Swapchain();
    options.colourSeed = panel ? 2u : 3u;
    if (panel && gPanelFigure && gPanelFigure->srv != nullptr && gPanelFigure->opacity > 0.0f) {
        auto const pw = static_cast<float>(hud_panel::Width());
        auto const ph = static_cast<float>(hud_panel::Height());
        mevr::vr::world_ui::Rect const stage = mevr::vr::world_ui::Stage(pw, ph);
        float const w = (stage.u1 - stage.u0) * pw;
        float const h = (stage.v1 - stage.v0) * ph;
        if (w > 1.0f && h > 1.0f) {
            options.figureRect[0] = stage.u0 * pw;
            options.figureRect[1] = stage.v0 * ph;
            options.figureRect[2] = 1.0f / w;
            options.figureRect[3] = 1.0f / h;
            options.figureOpacity = std::clamp(gPanelFigure->opacity, 0.0f, 1.0f);
            options.figureGamma = gPanelFigure->decodeGamma;
            options.figure = gPanelFigure->srv;
        }
    }
    return options;
}

// A quad's own blit options (not the panel's): the HUD pieces' (plugin/HudPieceLayers).
hud_panel::BlitOptions PieceBlitOptions() {
    static QuadSwapchain const notThePanel;
    return BlitOptionsFor(notThePanel);
}

ID3D11Texture2D* Blit(ID3D11DeviceContext* context, ID3D11Device* device, QuadSwapchain& swapchain, Window const& window,
    UINT factor, Masks const& masks, float whiteScale) {
    return hud_panel::Blit(context, device, swapchain, window, factor, masks, whiteScale, BlitOptionsFor(swapchain));
}

// (Re)makes `swapchain` at width x height; false if it can't.
bool EnsureSwapchain(QuadSwapchain& swapchain, std::uint32_t width, std::uint32_t height, char const* what, char const* name,
    std::function<void()> evict = {}) {
    return mevr::host::quad_swapchain::Ensure(gSession, swapchain, width, height, what, name, std::move(evict));
}

// One look at what the headset gets: the panel image after the blit, and the panel texture ([MENUVIS]).
void ProbePanel(ID3D11DeviceContext* context, ID3D11Texture2D* image, ID3D11Texture2D* panel) {
    ProbeTextureAlpha(context, image, "HUD panel swapchain image (first frame with UI)");
    ProbeTextureAlpha(context, panel, "HUD panel texture (the game's UI draws)");
}

bool RenderPanel(ID3D11DeviceContext* context, ID3D11Device* device, float whiteScale, Masks const& masks) {
    hud_panel::BlitOptions options = BlitOptionsFor(hud_panel::Swapchain());
    options.probe = &ProbePanel;
    return hud_panel::Render(context, device, gSession, whiteScale, masks, options);
}

// ---- The game's wheels on a panel of their own (Present thread) -------------------------------------
namespace wheel = mevr::vr::squad_menu::wheel;
namespace crop = mevr::vr::wheel_crop;

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

// Where the wheel's panel goes when it opens: above a hand, facing the eyes, or in front of the head.
XrPosef PlaceWheel(int place, float distance, XrPosef const& head, char const*& from) {
    mevr::vr::controllers::State const controllers = mevr::vr::controllers::Latest();
    if (place == 0 || place == 1) {
        mevr::vr::controllers::Hand const& hand = place == 0 ? controllers.left : controllers.right;
        if (controllers.active && (hand.gripValid || hand.aimValid)) {
            from = place == 0 ? "the left hand" : "the right hand";
            return mevr::vr::squad_menu::PlaceAtHand(hand.gripValid ? hand.grip.position : hand.aim.position, head.position, distance);
        }
    }
    from = place == 2 ? "in front of the head" : "in front of the head (no hand)";
    XrQuaternionf const& q = head.orientation;
    XrVector3f const forward{-2.0f * (q.x * q.z + q.w * q.y), 0.0f, -(1.0f - 2.0f * (q.x * q.x + q.y * q.y))};
    float const length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    XrVector3f const f = length > 1e-4f ? XrVector3f{forward.x / length, 0.0f, forward.z / length} : XrVector3f{0.0f, 0.0f, -1.0f};
    XrVector3f const spot{head.position.x + f.x * 0.45f, head.position.y - 0.35f, head.position.z + f.z * 0.45f};
    return mevr::vr::squad_menu::PlaceAtHand(spot, head.position, distance);
}

// ---- The interaction prompt at the object (Present thread; research/40) ----------------------------------------
namespace il = mevr::vr::interact;

QuadSwapchain gLabelSwapchain;
XrCompositionLayerQuad gLabelQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};

struct LabelState {
    bool shown = false;               // a label went out last frame
    std::uint32_t serial = 0;         // the mod's selection it was logged for
    std::uint32_t lastLabelSize = 0;  // the mod's Label size last seen (to log a mismatch once)
};
LabelState gLabel;

// A conversation in the world (research/59): its subtitle and wheel on the label's and the game wheel's swapchains.
struct ConvoUiState {
    bool subtitleShown = false;
    std::uint32_t speakerSerial = 0;
    bool wheelShown = false;
    std::uint32_t conversation = 0;
    XrPosef wheelPose{};
};
ConvoUiState gConvoUi;

// The mod's latest label, or none (a struct of another size: host and mod from different builds).
il::Label LabelWanted() {
    il::Label latest = il::Latest();
    if (latest.size == sizeof(il::Label)) {
        return latest;
    }
    if (latest.size != 0 && latest.size != gLabel.lastLabelSize) {
        gLabel.lastLabelSize = latest.size;
        mevr::log::Warn("overlays: the mod's interaction label is {} bytes, the host's {}: no label; rebuild both", latest.size,
            sizeof(il::Label));
    }
    return il::Label{};
}

// ---- HUD pieces on the body (Present thread; research/41) ------------------------------------------------------
// The player's status on the left wrist (from the left grip pose, host only) and the weapon panel on the gun (where
// the mod says, HostServices v22). [Hud] is read here from the settings file, once a second while it changes.
namespace hp = mevr::vr::hud_pieces;

// The wrist and gun pieces, the [Hud] settings and the mod's pieces: plugin/HudPieceLayers (shared with ME2's bridge).
namespace hpl = mevr::host::hud_piece_layers;
// The cockpit in the Mako (research/47): the radar on a display of its own (the vehicle's status uses the gun piece's).
QuadSwapchain gRadarSwapchain;
XrCompositionLayerQuad gRadarQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
bool gCockpitHudShown = false;

// One HUD piece's quad: its band blitted into its swapchain (1:1), the quad placed. False if anything failed.
bool ShowPiece(ID3D11DeviceContext* context, ID3D11Device* device, QuadSwapchain& swapchain, XrCompositionLayerQuad& quad,
    char const* what, char const* name, hp::Pixels const& px, XrVector3f const& position, XrVector3f const& head, float widthM,
    XrVector3f const& up, void* appSpace, float whiteScale) {
    return hpl::Show(gSession, context, device, swapchain, quad, what, name, px, position, head, widthM, up,
        static_cast<XrSpace>(appSpace), whiteScale, BlitOptionsFor(swapchain));
}

// ---- The Mako's lever and stick (research/46): two quads lying flat at the seat, drawn on the CPU when the picture changes ----
namespace mc = mevr::vr::mako_controls;

struct ControlQuad {
    QuadSwapchain swapchain;
    ComPtr<ID3D11Texture2D> upload;
    std::uint32_t key = 0xFFFFFFFFu;  // the picture in the swapchain's last released image
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};

    void Forget() {
        swapchain.Forget();
        key = 0xFFFFFFFFu;
    }
};
ControlQuad gLeverQuad;
ControlQuad gStickQuad;
bool gMakoControlsShown = false;
std::uint32_t gMakoViewSize = 0;  // a mismatch already logged

// The picture into the quad's swapchain if it changed. False if anything failed (the quad isn't shown).
template <class Pixels>
bool UpdateControl(ID3D11DeviceContext* context, ID3D11Device* device, ControlQuad& control, std::uint32_t width, std::uint32_t height,
    std::uint32_t key, char const* what, char const* name, Pixels const& pixels) {
    if (!EnsureSwapchain(control.swapchain, width, height, what, name, [&control] {
            control.swapchain.Destroy();
            control.key = 0xFFFFFFFFu;  // made again, it's drawn again
        })) {
        return false;
    }
    if (key == control.key) {
        return true;
    }
    if (control.upload == nullptr) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = width;
        td.Height = height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        if (FAILED(device->CreateTexture2D(&td, nullptr, control.upload.GetAddressOf()))) {
            mevr::log::Error("overlays: {} texture creation failed", what);
            control.swapchain.Destroy();
            control.swapchain.Refuse();
            return false;
        }
    }
    std::vector<std::uint32_t> image = pixels();
    context->UpdateSubresource(control.upload.Get(), 0, nullptr, image.data(), width * 4, 0);
    std::uint32_t index = 0;
    if (!AcquireImage(control.swapchain, index)) {
        return false;
    }
    context->CopyResource(control.swapchain.images[index], control.upload.Get());
    ReleaseImage(control.swapchain);
    control.key = key;
    // Snapshots draw it from these pixels: the image is released only when the picture changes.
    mevr::host::testloop::SetStaticImage(control.swapchain.handle, width, height, std::move(image));
    return true;
}

void PlaceControl(ControlQuad& control, XrVector3f const& at, XrVector3f const& forward, std::uint32_t width, std::uint32_t height, float widthM,
    float lengthM, void* appSpace) {
    control.quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    control.quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
    control.quad.space = static_cast<XrSpace>(appSpace);
    control.quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    control.quad.subImage.swapchain = control.swapchain.handle;
    control.quad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(width), static_cast<int32_t>(height)}};
    control.quad.pose = mc::FlatPose(at, forward);
    control.quad.size = {widthM, lengthM};
}

// The game pointer's dot: plugin/GamePointer's DotLayer (both hosts since 2026-11-15).
bool gLoggedPointerDot = false;

XrVector3f ToVec(float const v[3]) { return {v[0], v[1], v[2]}; }
XrQuaternionf ToQuat(float const q[4]) { return {q[0], q[1], q[2], q[3]}; }

} // namespace

bool HudPanelArmed() noexcept {
    return hud_panel::Armed();
}

bool HudPanelCapturesEarlyUi() noexcept {
    return gGalaxyEarlyUi.load(std::memory_order_acquire);
}

void HudPanelPresentBegin(unsigned long long present) noexcept {
    tPanelPresent = {};
    tPanelPresent.active = true;
    tPanelPresent.galaxy = HudPanelCapturesEarlyUi();
    tPanelPresent.number = present;
    tPanelPresent.startClears = hud_panel::TakeClears();
}

void HudPanelPresentDesktop() noexcept {
    tPanelPresent.stage = 3;
}

bool HudPanelPresentDraw(unsigned shaderHash, bool textBatch) noexcept {
    auto& p = tPanelPresent;
    if (!p.active || !(p.galaxy || HudPanelCapturesEarlyUi())) return false;
    ++p.rejected[p.stage];
    p.textRejected[p.stage] += textBatch ? 1u : 0u;
    if (p.firstShader[p.stage] == 0) p.firstShader[p.stage] = shaderHash;
    return true; // draw normally into its own target; never contaminate the next galaxy UI frame
}

void HudPanelPresentEnd(int mirrorPath) noexcept {
    auto const p = tPanelPresent;
    tPanelPresent.active = false;
    if (!p.galaxy) return;
    // Bounded trace, with adjacent odd/even samples even after the opening; no readback or stack walk.
    if (gGalaxyOrderLines++ < 16 || p.number % 300 < 2) {
        mevr::log::Info("galaxy UI order: present {} parity {} mirror {}; copied UI/text {}/{} early {}; start-clears {}; shown {}; "
                       "rejected UI/text before-copy {}/{}, copy-retire {}/{}, after-retire {}/{}, desktop {}/{}; "
                       "first PS {:08X}/{:08X}/{:08X}/{:08X}",
            p.number, p.number % 2, mirrorPath, p.draws, p.text, p.early, p.startClears, p.shown,
            p.rejected[0], p.textRejected[0], p.rejected[1], p.textRejected[1],
            p.rejected[2], p.textRejected[2], p.rejected[3], p.textRejected[3],
            p.firstShader[0], p.firstShader[1], p.firstShader[2], p.firstShader[3]);
    }
}

void OnSessionStopped() noexcept {
    mevr::host::floor_lock::Reset();
    hud_panel::SetArmed(false);  // no OverlayLayers runs now to disarm it: the UI back to the frame
    gGalaxyEarlyUi.store(false, std::memory_order_release);
}

ID3D11RenderTargetView* HudPanelTarget(ID3D11DeviceContext* context, unsigned width, unsigned height, int format) noexcept {
    return hud_panel::Target(context, width, height, format);
}

ID3D11BlendState* HudPanelBlend(ID3D11DeviceContext* context, ID3D11BlendState* game) noexcept {
    return hud_panel::Blend(context, game);
}

void NoteHudPanelDraw(bool beforeEyeCopy, bool textBatch) noexcept {
    hud_panel::NoteDraw(beforeEyeCopy, textBatch);
}

void HudPanelTintBlends(ID3D11DeviceContext* context, ID3D11BlendState* game, ID3D11BlendState** colour,
    ID3D11BlendState** tint) noexcept {
    hud_panel::TintBlends(context, game, colour, tint);
}

ID3D11RenderTargetView* HudPanelTintTarget() noexcept {
    return hud_panel::TintTarget();
}

ID3D11DepthStencilState* HudPanelNoWrites(ID3D11DeviceContext* context, ID3D11DepthStencilState* game) noexcept {
    return hud_panel::NoWrites(context, game);
}

void NoteHudPanelTint() noexcept {
    hud_panel::NoteTint();
}

bool HudPanelIsFloat() noexcept {
    return hud_panel::IsFloat();
}

void OnRecentered() noexcept {
    mevr::host::recenter_tilt::Committed();  // the orientation RecenterOrientation chose is the app space's now
    hud_panel::Recentered();
    gWheelRecentered.store(true, std::memory_order_release);  // an open wheel's panel is placed again
    RecenterWorldPanels();  // the world-locked flat panels too (plugin/WorldPanels.cpp)
    mevr::host::squad_panel::Recentered();  // and an open squad menu
    mevr::host::calibration::Recentered();  // and the guided calibration's layout
#if defined(MEVR_WITH_MAIN_MENU)
    mevr::host::main_menu::Recentered();  // and our open main menu
#endif
    mevr::host::halcyon::NoteRecentered();  // the holsters take the standing eye height there
}

void RequestHudPanelReplace() noexcept {
    hud_panel::RequestReplace();
}

unsigned HudPanelDrawsLastFrame() noexcept {
    return hud_panel::DrawsLastFrame();
}

void ProbeTextureAlpha(ID3D11DeviceContext* context, ID3D11Texture2D* texture, char const* tag) noexcept {
    if (context == nullptr || texture == nullptr || gProbes.size() >= 8) {
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1) {
        return;
    }
    D3D11_TEXTURE2D_DESC sd = desc;
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    Probe probe;
    if (device == nullptr || FAILED(device->CreateTexture2D(&sd, nullptr, probe.staging.GetAddressOf()))) {
        LogBoth(std::string("[MENUVIS] ") + tag + ": no staging copy (fmt=" + std::to_string(static_cast<int>(desc.Format)) + ")");
        return;
    }
    context->CopySubresourceRegion(probe.staging.Get(), 0, 0, 0, 0, texture, 0, nullptr);
    probe.tag = tag;
    probe.width = desc.Width;
    probe.height = desc.Height;
    probe.format = desc.Format;
    gProbes.push_back(std::move(probe));
}

unsigned OverlayLayers(OverlayFrame const& frame, void const** layersOut, unsigned capacity) noexcept {
    mevr::host::perf::ScopedMarks const timed(mevr::vr::frame_probe::kOverlayBegin, mevr::vr::frame_probe::kOverlayEnd);
    ID3D11DeviceContext* const context = frame.context;
    if (context == nullptr || frame.session == nullptr) {
        hud_panel::SetArmed(false);
        gGalaxyEarlyUi.store(false, std::memory_order_release);
        return 0;
    }
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    PumpProbes(context);
    // The game's UI gets the eyes' colour correction (plugin/Colour, research/27): set before the panel's first blit.
    static bool const colourHooked = [] {
        hud_panel::SetColour({mevr::vr::colour::ShaderPrelude(), &mevr::host::colour::BeginPanel, &mevr::host::colour::EndPanel});
        return true;
    }();
    (void)colourHooked;

    XrSession const session = static_cast<XrSession>(frame.session);
    if (session != gSession) {
        hud_panel::ForgetSession();
        mevr::host::game_pointer::ForgetSession();  // the game pointer's dot
        gWheelSwapchain.Forget();
        gLabelSwapchain.Forget();
        hpl::ForgetSession();
        gRadarSwapchain.Forget();
        gLeverQuad.Forget();
        gStickQuad.Forget();
        mevr::host::ui_world_figure::Forget();
        gSession = session;
    }
    mevr::host::renderer::HudSettings const cfg = mevr::host::renderer::Hud();
    std::uint64_t const now = GetTickCount64();
    float const dt = gLastFrameMs == 0 ? 0.0f : static_cast<float>(now - gLastFrameMs) / 1000.0f;
    gLastFrameMs = now;
    mevr::host::recenter_tilt::NoteHead(frame.headOrientation, frame.headValid);  // the tilt-correcting recenter's watch

    // The panel is for gameplay (modes 0-4: on foot, Mako, wheels, squad command). Conversations and
    // cutscenes in VR keep Halcyon's presentation: head tracking is parked there and their UI stays in
    // the eye images, with Halcyon's conversation layout. An unreadable mode (-1) keeps the last one.
    if (frame.gameMode >= 0) {
        gLastGameMode = frame.gameMode;
    }
    bool const gameplay = gLastGameMode >= 0 && gLastGameMode <= 4;
    // The galaxy map open in the CIC (research/51 section 8): the game's UI (its own map hidden, so its travel question) goes to
    // the panel too, which then stands over the hologram's table instead of in front of the body.
    auto const galaxyScene = mevr::vr::galaxy_holo::Latest(500);
    // Ownership is published by the mod, not last Present's Layer result. The first return
    // frame already belongs to the table even while its captures are warming up.
    bool const galaxyUi = gLastGameMode == 9 && galaxyScene && galaxyScene->inWorld && galaxyScene->anchorValid;
    bool const galaxyWindowUi = galaxyUi && galaxyScene && galaxyScene->windowOn != 0;
    // One of the game's screens standing in the room (research/53: the shop...): its UI goes to the panel, placed where it opened.
    bool const worldUi = mevr::vr::world_ui::ScreenMode(gLastGameMode) && mevr::host::world_ui::Active();
    bool const worldUiPending = (mevr::host::world_ui::Ownership(gLastGameMode) & 6u) != 0;
    // A conversation in the world (research/59): its movie goes to the panel, its subtitle and wheel cut out of it below.
    mevr::host::conversation_world::NoteFrame(frame.gameMode);
    std::optional<mevr::vr::conversation_world::State> const convo =
        gLastGameMode == 5 ? mevr::host::conversation_world::Current() : std::nullopt;
    // The 3D cinema screen (research/52 "Cross-eyed"): its UI (the dialogue wheel, subtitles, the title's menus) goes to the panel,
    // which stands just in front of the screen. In the eye images the screen's crop put it at the far depth, behind the faces and
    // shoulders it is drawn over (0.4-0.6 degrees, measured), so reading the wheel crossed the eyes.
    mevr::host::cine_screen::UiLayer const screenUiWanted = mevr::host::cine_screen::UiLayerWanted();
    bool const screenUi = frame.screenActive && screenUiWanted.on && frame.screenWidthM > 0.0f && frame.screenHeightM > 0.0f;
    bool const uiToPanel = gameplay || galaxyUi || worldUi || worldUiPending || convo.has_value() || screenUi;

    // The game's own crosshair marks the screen centre (the head), not the gun: hidden while our
    // hand reticle stands in for it. The HUD movie keeps its authored layout on the panel (Halcyon's
    // head-locked shrink-and-centre would only make it small there).
    bool const handAim = ControllerAimActive();
    bool const panelUsable = hud_panel::Usable();
    bool const engaged = cfg.hudPanel && frame.sfr2 && panelUsable;
    mevr::host::renderer::SetAuthoredHudLayout(engaged);

    unsigned count = 0;
    hud_panel::Draws const taken = hud_panel::TakeDraws();
    unsigned const draws = taken.draws;
    unsigned const textDraws = taken.text;
    unsigned const earlyDraws = taken.early;
    unsigned const earlyTextDraws = taken.earlyText;
    tPanelPresent.galaxy = tPanelPresent.galaxy || galaxyWindowUi;
    tPanelPresent.draws = draws;
    tPanelPresent.text = textDraws;
    tPanelPresent.early = earlyDraws;

    // ---- HUD panel ----
    overlay::PanelSettings settings;
    settings.distance = std::clamp(cfg.hudPanelDistanceM, 0.5f, 5.0f);
    settings.drop = std::clamp(cfg.hudPanelDropM, -1.0f, 1.5f);
    settings.followAfter = cfg.hudPanelFollowDeg > 0.0f ? std::clamp(cfg.hudPanelFollowDeg, 10.0f, 170.0f) * kDegToRad : 0.0f;
    XrQuaternionf const head = ToQuat(frame.headOrientation);
    hud_panel::Place(head, frame.headValid, settings, cfg.hudPanelFollowDeg, dt);

    // ---- The game's wheels (power: mode 2, weapon: mode 3) on a panel of their own ----
    PumpGrids(context);
    wheel::Panel const wheelSettings = WheelSettings();
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
    int const wheelMode = frame.gameMode == 2 || frame.gameMode == 3 ? frame.gameMode : -1;
    int const wheelPlace = wheelMode == 3 ? wheelSettings.weaponPlace : wheelSettings.powerPlace;  // -1: stays on the HUD panel
    bool const wheelWanted = wheelSettings.enabled && wheelPlace >= 0 && engaged && gameplay && frame.worldFrame && wheelMode >= 0 &&
        hud_panel::Ready() && !gWheelSwapchain.Refused() && device != nullptr;
    char const* cropFrom = "the default guess";
    crop::Rect cropRect;
    if (wheelSettings.crop[2] > 0.0f && wheelSettings.crop[3] > 0.0f) {
        cropRect = {wheelSettings.crop[0], wheelSettings.crop[1], wheelSettings.crop[2], wheelSettings.crop[3]};
        cropFrom = "[Squad] wheelCrop";
    } else if (gMeasured.Valid(hud_panel::Width(), hud_panel::Height())) {
        cropRect = gMeasured.rect;
        cropFrom = "measured";
    } else {
        cropRect = crop::DefaultCrop(hud_panel::Width(), hud_panel::Height());
    }
    crop::Pixels const cropPx = crop::ToPixels(cropRect, hud_panel::Width(), hud_panel::Height());
    Masks hudMasks;
    if (wheelWanted) {
        hudMasks.Add(cropPx.x, cropPx.y, cropPx.width, cropPx.height);
    }
    // The interaction prompt at the object (never with a wheel up: both take their part out of the same panel).
    il::Label const label = LabelWanted();
    bool const labelWanted = label.visible && !wheelWanted && engaged && gameplay && gLastGameMode == 0 && frame.worldFrame &&
        !frame.menuOpen && frame.headValid && hud_panel::Ready() && !gLabelSwapchain.Refused() &&
        device != nullptr;
    il::Rect const labelRect = label.crop[2] > 0.0f && label.crop[3] > 0.0f
        ? il::Rect{label.crop[0], label.crop[1], label.crop[2], label.crop[3]}
        : il::DefaultCrop(hud_panel::Width(), hud_panel::Height());
    il::Pixels const labelPx = il::ToPixels(labelRect, hud_panel::Width(), hud_panel::Height());
    if (labelWanted && labelPx.width > 0) {
        hudMasks.Add(labelPx.x, labelPx.y, labelPx.width, labelPx.height);
    }
    // The conversation's subtitle at the speaker and its wheel at a hand (research/59): their bands out of the panel. The label's
    // and the game wheel's swapchains serve them (neither is up in a conversation; the runtime limits swapchains).
    namespace cwv = mevr::vr::conversation_world;
    bool const convoAllowed = convo && engaged && frame.worldFrame && !frame.menuOpen && frame.headValid && hud_panel::Ready() &&
        device != nullptr;
    bool const convoSubtitleWanted = convoAllowed && convo->subtitle != 0 && !gLabelSwapchain.Refused();
    // Not while Thespian mode's panel stands with the game's wheel hidden (research/70): the replies are its rows. Once the
    // left stick brings the game's wheel back (a pick by hand) its quad is drawn as ever.
#if defined(MEVR_WITH_MAIN_MENU)
    bool const thespianPanel = mevr::host::thespian::HidesGameWheel();
#else
    bool const thespianPanel = false;
#endif
    bool const convoWheelWanted = convoAllowed && convo->wheel != 0 && !gWheelSwapchain.Refused() && !thespianPanel;
    il::Pixels const convoSubPx = convo ? il::ToPixels(convo->subtitleCrop[2] > 0.0f && convo->subtitleCrop[3] > 0.0f
                                                           ? il::Rect{convo->subtitleCrop[0], convo->subtitleCrop[1], convo->subtitleCrop[2], convo->subtitleCrop[3]}
                                                           : cwv::DefaultSubtitleCrop(hud_panel::Width(), hud_panel::Height()),
                                              hud_panel::Width(), hud_panel::Height())
                                        : il::Pixels{};
    il::Rect const convoWheelRect = convo && convo->wheelCrop[2] > 0.0f && convo->wheelCrop[3] > 0.0f
        ? il::Rect{convo->wheelCrop[0], convo->wheelCrop[1], convo->wheelCrop[2], convo->wheelCrop[3]}
        : cwv::DefaultWheelCrop(hud_panel::Width(), hud_panel::Height());
    il::Pixels const convoWheelPx = convo ? il::ToPixels(convoWheelRect, hud_panel::Width(), hud_panel::Height()) : il::Pixels{};
    if (convoSubtitleWanted && convoSubPx.width > 0) {
        hudMasks.Add(convoSubPx.x, convoSubPx.y, convoSubPx.width, convoSubPx.height);
    }
    if (convoWheelWanted && convoWheelPx.width > 0) {
        hudMasks.Add(convoWheelPx.x, convoWheelPx.y, convoWheelPx.width, convoWheelPx.height);
    }
    // The HUD pieces on the body (research/41): on foot only, with the HUD on the panel, never with a wheel up. The wrist
    // needs the left controller tracked; the gun needs the mod's anchor (a drawn gun aimed along its barrel).
    hp::Settings const& hud = hpl::Settings(now);
    hp::Pieces const pieces = hpl::Wanted();
    mevr::vr::controllers::State const pieceControllers = mevr::vr::controllers::Latest();
    bool const piecesAllowed = !wheelWanted && engaged && gameplay && gLastGameMode == 0 && frame.worldFrame && !frame.menuOpen &&
        frame.headValid && hud_panel::Ready() && device != nullptr;
    // In the Mako's cockpit (the mod's pieces say where its displays are): the vehicle's status (the weapon panel's band) and
    // the radar on them, the rest of the HUD panel at the cockpit's anchor, none over its screens (research/47).
    bool const cockpitHud = pieces.cockpit && !wheelWanted && engaged && gameplay && gLastGameMode == 1 && frame.worldFrame &&
        !frame.menuOpen && frame.headValid && hud_panel::Ready() && device != nullptr;
    if (cockpitHud != gCockpitHudShown) {
        gCockpitHudShown = cockpitHud;
        LogBoth(cockpitHud ? "[MEVR-HUD] the cockpit's HUD: the Mako's status and radar on its displays, the HUD panel at its anchor"
                           : "[MEVR-HUD] the cockpit's HUD is off: the HUD panel as before");
    }
    bool const wristWanted = piecesAllowed && hud.wristStatus && !hpl::WristRefused() && pieceControllers.active &&
        pieceControllers.left.gripValid;
    bool const gunWanted = ((piecesAllowed && hud.gunStatus && pieces.gunVisible) || cockpitHud) && !hpl::GunRefused();
    bool const radarWanted = cockpitHud && !gRadarSwapchain.Refused();
    hp::Pixels const wristPx = hp::ToPixels(hp::WristCrop(hud, hud_panel::Width(), hud_panel::Height()), hud_panel::Width(), hud_panel::Height());
    hp::Pixels const gunPx = hp::ToPixels(hp::GunCrop(hud, hud_panel::Width(), hud_panel::Height()), hud_panel::Width(), hud_panel::Height());
    hp::Pixels const radarPx = hp::ToPixels(hp::DefaultRadarCrop(hud_panel::Width(), hud_panel::Height()), hud_panel::Width(), hud_panel::Height());
    if (wristWanted && wristPx.width > 0) {
        hudMasks.Add(wristPx.x, wristPx.y, wristPx.width, wristPx.height);
    }
    if (gunWanted && gunPx.width > 0) {
        hudMasks.Add(gunPx.x, gunPx.y, gunPx.width, gunPx.height);
    }
    if (radarWanted && radarPx.width > 0) {
        hudMasks.Add(radarPx.x, radarPx.y, radarPx.width, radarPx.height);
    }

    bool panelShown = false;
    bool galaxyPanel = false;  // the panel goes after the galaxy hologram's layer (over it)
    std::optional<mevr::host::world_ui::Shown> worldShown;
    if (worldUi && hud_panel::HasTexture()) {
        // The left wrist (where the HUD's wrist piece sits): a screen marked for it is projected from there (research/53 section 6).
        std::optional<XrVector3f> wrist;
        if (pieceControllers.active && pieceControllers.left.gripValid) {
            wrist = hp::WristPoint(pieceControllers.left.grip, hud.wristOffset);
        }
        worldShown = mevr::host::world_ui::Current(ToVec(frame.headPosition), ToQuat(frame.headOrientation), frame.headValid, hud_panel::Width(),
            hud_panel::Height(), wrist);
    }
    gPanelBackdrop = worldShown ? std::max(worldShown->backdrop, 1.0f / 256.0f) : 0.0f;
    gPanelFigure = worldShown && device != nullptr ? mevr::host::ui_world_figure::ForFrame(device.Get(), gPanelBackdrop) : std::nullopt;
    if ((frame.worldFrame || screenUi) && engaged && uiToPanel && hud_panel::HasTexture() && count < capacity && device != nullptr &&
        (!worldUiPending || worldUi) && (!worldUi || worldShown)) {
        hud_panel::NoteDrawsLastFrame(draws);
        if (RenderPanel(context, device.Get(), frame.floatWhiteScale, worldUi || screenUi ? Masks{} : hudMasks)) {
            tPanelPresent.stage = 1; // the swapchain now contains this game's complete panel
            float const width = std::clamp(cfg.hudPanelWidthM, 0.3f, 4.0f);
            gPanelQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gPanelQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gPanelQuad.space = static_cast<XrSpace>(frame.appSpace);
            gPanelQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gPanelQuad.subImage.swapchain = hud_panel::Swapchain().handle;
            gPanelQuad.subImage.imageRect = {{0, 0},
                {static_cast<int32_t>(hud_panel::Swapchain().width), static_cast<int32_t>(hud_panel::Swapchain().height)}};
            gPanelQuad.pose = hud_panel::Placement().Pose(settings);
            gPanelQuad.size = {width, width * static_cast<float>(hud_panel::Height()) / static_cast<float>(hud_panel::Width())};
            hp::Quad cockpitPanel;
            if (cockpitHud && hp::Place(pieces.panelAnchor, ToVec(frame.headPosition), pieces.panelWidthM,
                                  static_cast<float>(hud_panel::Width()) / static_cast<float>(hud_panel::Height()), cockpitPanel, pieces.cockpitUp)) {
                gPanelQuad.pose = cockpitPanel.pose;  // the cockpit's place for what's left of the HUD, clear of its screens
                gPanelQuad.size = {cockpitPanel.width, cockpitPanel.height};
            }
            if (worldShown) {
                // The screen's shown part (the stage's content) of the panel image, where it stood when it opened; the game
                // pointer gets the whole window on its plane.
                auto const sw = static_cast<float>(hud_panel::Swapchain().width);
                auto const sh = static_cast<float>(hud_panel::Swapchain().height);
                gPanelQuad.subImage.imageRect = {{static_cast<int32_t>(worldShown->part.u0 * sw), static_cast<int32_t>(worldShown->part.v0 * sh)},
                    {std::max<int32_t>(1, static_cast<int32_t>((worldShown->part.u1 - worldShown->part.u0) * sw)),
                        std::max<int32_t>(1, static_cast<int32_t>((worldShown->part.v1 - worldShown->part.v0) * sh))}};
                gPanelQuad.pose = worldShown->panel.pose;
                gPanelQuad.size = {worldShown->panel.widthM, worldShown->panel.heightM};
                // Not while the bypass puzzle is played: any mouse motion on its screen turns its marker without end (a ray
                // anywhere on the window spun it, research/53 section 6.3); the omni-tool's d-pad plays it (plugin/OmniPad).
                if (!worldShown->puzzle) {
                    float const touchArea[4] = {worldShown->part.u0, worldShown->part.v0, worldShown->part.u1, worldShown->part.v1};
                    mevr::host::game_pointer::NoteGameUiQuad(worldShown->whole.pose, worldShown->whole.widthM, worldShown->whole.heightM,
                        mevr::host::game_pointer::PanelKind::GameMenu, touchArea, worldShown->onWrist ? 0 : -1);
                } else {
                    mevr::host::game_pointer::HoldOffForPuzzle();  // its first frame forgets the quad at once
                }
            }
            if (screenUi) {
                // The part of the window the screen shows (the crops' average columns, the band's rows), on a quad `forwardM` in
                // front of the screen toward the eyes, as large to the eye as on the screen: no disparity of its own, so it sits
                // nearer than the scene behind it.
                namespace cs = mevr::vr::cine_screen;
                float const origin[3] = {0.0f, 0.0f, 0.0f};
                cs::UiPlace const at = cs::PlaceUi(frame.screenPosition, frame.screenHeadLocked ? origin : frame.headPosition,
                    screenUiWanted.forwardM);
                cs::UiPart const part = cs::UiPartShown(frame.screenCrop, frame.screenRows[0], frame.screenRows[1]);
                auto const sw = static_cast<float>(hud_panel::Swapchain().width);
                auto const sh = static_cast<float>(hud_panel::Swapchain().height);
                gPanelQuad.subImage.imageRect = {{static_cast<int32_t>(part.u0 * sw), static_cast<int32_t>(part.v0 * sh)},
                    {std::max<int32_t>(1, static_cast<int32_t>((part.u1 - part.u0) * sw)),
                        std::max<int32_t>(1, static_cast<int32_t>((part.v1 - part.v0) * sh))}};
                gPanelQuad.space = frame.screenSpace != nullptr ? static_cast<XrSpace>(frame.screenSpace) : static_cast<XrSpace>(frame.appSpace);
                gPanelQuad.pose.orientation = ToQuat(frame.screenOrientation);
                gPanelQuad.pose.position = ToVec(at.valid ? at.position : frame.screenPosition);
                float const scale = at.valid ? at.scale : 1.0f;
                gPanelQuad.size = {frame.screenWidthM * scale, frame.screenHeightM * scale};
                if (!gScreenUiShown) {
                    char line[320];
                    std::snprintf(line, sizeof line,
                        "[MEVR-HUD] the 3D screen's UI on its own layer %.2f m in front of the screen (%.2f x %.2f m%s); the UI's part "
                        "u %.3f..%.3f v %.3f..%.3f of %ux%u", at.valid ? screenUiWanted.forwardM : 0.0f, gPanelQuad.size.width,
                        gPanelQuad.size.height, frame.screenHeadLocked ? ", head-locked" : "", part.u0, part.u1, part.v0, part.v1,
                        hud_panel::Width(), hud_panel::Height());
                    LogBoth(line);
                }
                gScreenUiShown = true;
            }
            if (galaxyUi) {
                float const uiWidth = mevr::host::galaxy_hologram::UiPanelWidthM(kGalaxyUiWidthM);
                gPanelQuad.pose = mevr::host::galaxy_hologram::UiPanelPose(ToVec(frame.headPosition));
                gPanelQuad.size = {uiWidth, uiWidth * static_cast<float>(hud_panel::Height()) / static_cast<float>(hud_panel::Width())};
                galaxyPanel = true;
            } else {
                // A screen projected from the wrist: its fan of light first, under the panel.
                if (worldShown && worldShown->beam && count + 1 < capacity) {
                    mevr::host::wrist_beam::Frame beamFrame;
                    beamFrame.session = session;
                    beamFrame.appSpace = static_cast<XrSpace>(frame.appSpace);
                    beamFrame.context = context;
                    beamFrame.device = device.Get();
                    if (XrCompositionLayerQuad const* beam = mevr::host::wrist_beam::Layer(beamFrame, *worldShown->beam)) {
                        layersOut[count++] = beam;
                    }
                }
                layersOut[count++] = &gPanelQuad;
            }
            panelShown = true;
            if (!gLoggedPanel || now >= gNextPanelLogMs) {
                char line[288];
                std::snprintf(line, sizeof line,
                    "[MEVR-HUD] panel layer: %u UI draws this frame, %ux%u (1/%u of %ux%u), %.2f x %.2f m at "
                    "(%.2f, %.2f, %.2f) m, heading %.0f deg",
                    draws, hud_panel::Swapchain().width, hud_panel::Swapchain().height, hud_panel::Factor(), hud_panel::Width(), hud_panel::Height(),
                    gPanelQuad.size.width, gPanelQuad.size.height, gPanelQuad.pose.position.x,
                    gPanelQuad.pose.position.y, gPanelQuad.pose.position.z, hud_panel::Placement().Yaw() / kDegToRad);
                LogBoth(line);
                gLoggedPanel = true;
                gNextPanelLogMs = now + 30000;
            }
        }
    }
    if (gScreenUiShown && !(screenUi && panelShown)) {
        gScreenUiShown = false;
        LogBoth("[MEVR-HUD] the 3D screen's UI layer is gone");
    }
    // The prompt's part of the UI on a quad at the object, facing the eyes, the same angular size at any distance.
    if (labelWanted && panelShown && labelPx.width > 0 && count < capacity) {
        il::Quad quad;
        float const aspect = static_cast<float>(labelPx.width) / static_cast<float>(labelPx.height);
        if (il::Place(label.position, ToVec(frame.headPosition), label.heightDeg, aspect, quad) &&
            EnsureSwapchain(gLabelSwapchain, labelPx.width, labelPx.height, "interaction label", "label") &&
            Blit(context, device.Get(), gLabelSwapchain, {labelPx.x, labelPx.y, labelPx.width, labelPx.height}, 1, Masks{},
                frame.floatWhiteScale) != nullptr) {
            gLabelQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gLabelQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gLabelQuad.space = static_cast<XrSpace>(frame.appSpace);
            gLabelQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gLabelQuad.subImage.swapchain = gLabelSwapchain.handle;
            gLabelQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(labelPx.width), static_cast<int32_t>(labelPx.height)}};
            gLabelQuad.pose = quad.pose;
            gLabelQuad.size = {quad.width, quad.height};
            layersOut[count++] = &gLabelQuad;
            if (!gLabel.shown || gLabel.serial != label.serial) {
                char line[320];
                std::snprintf(line, sizeof line,
                    "[MEVR-INTERACT] the prompt at the object: (%.2f, %.2f, %.2f) m, %.2f x %.2f m (%.1f deg tall); the UI's part "
                    "(%u, %u) %ux%u of %ux%u (%s)", quad.pose.position.x, quad.pose.position.y, quad.pose.position.z, quad.width,
                    quad.height, label.heightDeg, labelPx.x, labelPx.y, labelPx.width, labelPx.height, hud_panel::Width(), hud_panel::Height(),
                    label.crop[2] > 0.0f ? "[Interact] crop" : "the game's own place");
                LogBoth(line);
            }
            gLabel.shown = true;
            gLabel.serial = label.serial;
        }
    } else if (gLabel.shown) {
        gLabel.shown = false;
        LogBoth("[MEVR-INTERACT] the prompt at the object is gone");
    }
    // A conversation in the world (research/59): the speaker's line under their face, facing the eyes, the same angular size at
    // any distance (the interaction label's way); the dialogue wheel above a hand, placed where it opens (the game wheel's way).
    if (convoSubtitleWanted && panelShown && convoSubPx.width > 0 && count < capacity) {
        il::Quad quad;
        float const aspect = static_cast<float>(convoSubPx.width) / static_cast<float>(convoSubPx.height);
        XrVector3f const at{convo->subtitleAt[0], convo->subtitleAt[1], convo->subtitleAt[2]};
        if (il::Place(at, ToVec(frame.headPosition), convo->subtitleHeightDeg, aspect, quad) &&
            EnsureSwapchain(gLabelSwapchain, convoSubPx.width, convoSubPx.height, "conversation subtitle", "label") &&
            Blit(context, device.Get(), gLabelSwapchain, {convoSubPx.x, convoSubPx.y, convoSubPx.width, convoSubPx.height}, 1, Masks{},
                frame.floatWhiteScale) != nullptr) {
            gLabelQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gLabelQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gLabelQuad.space = static_cast<XrSpace>(frame.appSpace);
            gLabelQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gLabelQuad.subImage.swapchain = gLabelSwapchain.handle;
            gLabelQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(convoSubPx.width), static_cast<int32_t>(convoSubPx.height)}};
            gLabelQuad.pose = quad.pose;
            gLabelQuad.size = {quad.width, quad.height};
            layersOut[count++] = &gLabelQuad;
            if (!gConvoUi.subtitleShown || gConvoUi.speakerSerial != convo->speakerSerial) {
                char line[320];
                std::snprintf(line, sizeof line,
                    "[MEVR-CONVO] the subtitle at the speaker: (%.2f, %.2f, %.2f) m, %.2f x %.2f m (%.1f deg tall); the UI's part (%u, %u) "
                    "%ux%u of %ux%u (%s)", quad.pose.position.x, quad.pose.position.y, quad.pose.position.z, quad.width, quad.height,
                    convo->subtitleHeightDeg, convoSubPx.x, convoSubPx.y, convoSubPx.width, convoSubPx.height, hud_panel::Width(), hud_panel::Height(),
                    convo->subtitleCrop[2] > 0.0f ? "[Cinematics] subtitleCrop" : "the default band");
                LogBoth(line);
            }
            gConvoUi.subtitleShown = true;
            gConvoUi.speakerSerial = convo->speakerSerial;
        }
    } else if (gConvoUi.subtitleShown) {
        gConvoUi.subtitleShown = false;
        LogBoth("[MEVR-CONVO] the subtitle at the speaker is gone");
    }
    XrCompositionLayerQuad const* convoWheelLayer = nullptr;
    if (convoWheelWanted && panelShown && convoWheelPx.width > 0) {
        if (!gConvoUi.wheelShown || gConvoUi.conversation != convo->conversation) {
            XrPosef headPose{};
            headPose.orientation = head;
            headPose.position = ToVec(frame.headPosition);
            char const* from = "";
            gConvoUi.wheelPose = PlaceWheel(convo->wheelHand, std::clamp(convo->wheelDistanceM, 0.05f, 0.8f), headPose, from);
            char line[320];
            std::snprintf(line, sizeof line,
                "[MEVR-CONVO] the dialogue wheel %.2f m from %s, at (%.2f, %.2f, %.2f) m, %.2f m wide; the UI's part (%u, %u) %ux%u of %ux%u (%s)",
                convo->wheelDistanceM, from, gConvoUi.wheelPose.position.x, gConvoUi.wheelPose.position.y, gConvoUi.wheelPose.position.z,
                std::clamp(cwv::WheelQuadWidth(convo->wheelWidthM, convoWheelRect.width), 0.15f, 3.0f), convoWheelPx.x, convoWheelPx.y,
                convoWheelPx.width, convoWheelPx.height, hud_panel::Width(), hud_panel::Height(),
                convo->wheelCrop[2] > 0.0f ? "[Cinematics] wheelCrop" : "the default band");
            LogBoth(line);
        }
        gConvoUi.wheelShown = true;
        gConvoUi.conversation = convo->conversation;
        // At most 2048 px wide (the band is ~3240 px of a 3840 frame since it holds the replies' whole text): the text keeps the
        // 1.5 image px a stage px it had in the narrower band.
        UINT const factor = std::clamp<UINT>((convoWheelPx.width + 2047u) / 2048u, 1u, 4u);
        std::uint32_t const width = std::max<std::uint32_t>(convoWheelPx.width / factor, 16);
        std::uint32_t const height = std::max<std::uint32_t>(convoWheelPx.height / factor, 16);
        if (EnsureSwapchain(gWheelSwapchain, width, height, "conversation wheel", "wheel") &&
            Blit(context, device.Get(), gWheelSwapchain, {convoWheelPx.x, convoWheelPx.y, convoWheelPx.width, convoWheelPx.height}, factor,
                Masks{}, frame.floatWhiteScale) != nullptr) {
            float const quadWidth = std::clamp(cwv::WheelQuadWidth(convo->wheelWidthM, convoWheelRect.width), 0.15f, 3.0f);
            gWheelQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gWheelQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gWheelQuad.space = static_cast<XrSpace>(frame.appSpace);
            gWheelQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gWheelQuad.subImage.swapchain = gWheelSwapchain.handle;
            gWheelQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(width), static_cast<int32_t>(height)}};
            gWheelQuad.pose = gConvoUi.wheelPose;
            gWheelQuad.size = {quadWidth, quadWidth * static_cast<float>(convoWheelPx.height) / static_cast<float>(convoWheelPx.width)};
            convoWheelLayer = &gWheelQuad;
        }
    } else if (gConvoUi.wheelShown) {
        gConvoUi.wheelShown = false;
        LogBoth("[MEVR-CONVO] the dialogue wheel is gone");
    }
    // The player's status on the back of the left wrist: a small quad a fixed offset from the left grip pose, facing the
    // eyes, upright (research/41).
    XrVector3f const headPosition = ToVec(frame.headPosition);
    hpl::Common pieceCommon;
    pieceCommon.session = gSession;
    pieceCommon.appSpace = static_cast<XrSpace>(frame.appSpace);
    pieceCommon.context = context;
    pieceCommon.device = device.Get();
    pieceCommon.head = headPosition;
    pieceCommon.whiteScale = frame.floatWhiteScale;
    pieceCommon.nowMs = now;
    {
        hpl::Piece wrist;
        wrist.wanted = wristWanted && panelShown && wristPx.width > 0 && count < capacity;
        wrist.px = wristPx;
        if (wrist.wanted) {
            wrist.position = hp::WristPoint(pieceControllers.left.grip, hud.wristOffset);
        }
        wrist.widthM = hud.wristWidthM;
        wrist.cropByHand = hud.wristCrop.Valid();
        wrist.leftTracked = pieceControllers.active && pieceControllers.left.gripValid;
        pieceCommon.options = PieceBlitOptions();
        if (XrCompositionLayerQuad const* layer = hpl::Wrist(pieceCommon, wrist)) {
            layersOut[count++] = layer;
        }
    }
    {
        hpl::Piece gun;
        gun.wanted = gunWanted && panelShown && gunPx.width > 0 && count < capacity;
        gun.px = gunPx;
        gun.position = cockpitHud ? pieces.statusAnchor : pieces.gunAnchor;
        gun.widthM = cockpitHud ? pieces.statusWidthM : hud.gunWidthM;
        gun.up = cockpitHud ? pieces.cockpitUp : pieces.gunUp;
        gun.cropByHand = hud.gunCrop.Valid();
        gun.serial = pieces.serial;
        pieceCommon.options = PieceBlitOptions();
        if (XrCompositionLayerQuad const* layer = hpl::Gun(pieceCommon, gun)) {
            layersOut[count++] = layer;
        }
    }
    // The Mako's radar on the cockpit's display (research/47).
    if (radarWanted && panelShown && radarPx.width > 0 && count < capacity) {
        if (ShowPiece(context, device.Get(), gRadarSwapchain, gRadarQuad, "cockpit radar", "radar", radarPx, pieces.radarAnchor, headPosition,
                pieces.radarWidthM, pieces.cockpitUp, frame.appSpace, frame.floatWhiteScale)) {
            layersOut[count++] = &gRadarQuad;
        }
    }
    // A HUD-only picture of where the UI is, now and then, until the wheel's place has been measured.
    bool const autoCrop = !(wheelSettings.crop[2] > 0.0f && wheelSettings.crop[3] > 0.0f);
    if (panelShown && wheelMode < 0 && frame.gameMode == 0 && autoCrop && !gMeasured.Valid(hud_panel::Width(), hud_panel::Height()) &&
        now >= gNextBeforeMs) {
        gNextBeforeMs = now + kBeforeEveryMs;
        CaptureGrid(context, device.Get(), false);
    }
    // The wheel's panel: placed when a wheel opens (and when the mod opens it again, or on recenter).
    bool const wheelRecentered = gWheelRecentered.exchange(false, std::memory_order_acq_rel);
    XrCompositionLayerQuad const* wheelLayer = nullptr;
    if (wheelWanted && panelShown) {
        bool const fresh = wheelMode != gWheel.mode;
        bool const reopened = wheelMode == 3 && wheelSettings.modOpen && wheelSettings.serial != gWheel.serial;
        if (fresh) {
            gWheel.openedMs = now;
            gWheel.gridTaken = false;
            gWheel.loggedThisOpening = false;
        }
        if (fresh || reopened || wheelRecentered || !gWheel.placed) {
            XrPosef headPose{};
            headPose.orientation = head;
            headPose.position = ToVec(frame.headPosition);
            char const* from = "";
            gWheel.pose = PlaceWheel(wheelPlace, std::clamp(wheelSettings.distance, 0.1f, 0.8f), headPose, from);
            gWheel.placed = true;
            char line[320];
            std::snprintf(line, sizeof line,
                "[MEVR-WHEEL] the %s wheel on its own panel%s: %.2f m from %s, at (%.2f, %.2f, %.2f) m, %.2f m wide; the UI's part "
                "(%u, %u) %ux%u of %ux%u (%s)", wheelMode == 3 ? "weapon" : "power", wheelRecentered ? " again (recentered)" : "",
                wheelSettings.distance, from, gWheel.pose.position.x, gWheel.pose.position.y, gWheel.pose.position.z,
                wheelSettings.width, cropPx.x, cropPx.y, cropPx.width, cropPx.height, hud_panel::Width(), hud_panel::Height(), cropFrom);
            LogBoth(line);
        }
        gWheel.mode = wheelMode;
        gWheel.serial = wheelSettings.serial;
        UINT const factor = std::clamp<UINT>(static_cast<UINT>(std::lround(cropPx.width / 1024.0)), 1u, 4u);
        std::uint32_t const width = std::max<std::uint32_t>(cropPx.width / factor, 16);
        std::uint32_t const height = std::max<std::uint32_t>(cropPx.height / factor, 16);
        if (EnsureSwapchain(gWheelSwapchain, width, height, "wheel panel", "wheel") &&
            Blit(context, device.Get(), gWheelSwapchain, {cropPx.x, cropPx.y, cropPx.width, cropPx.height}, factor, Masks{},
                frame.floatWhiteScale) != nullptr) {
            float const quadWidth = std::clamp(wheelSettings.width, 0.15f, 1.5f);
            gWheelQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gWheelQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gWheelQuad.space = static_cast<XrSpace>(frame.appSpace);
            gWheelQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gWheelQuad.subImage.swapchain = gWheelSwapchain.handle;
            gWheelQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(width), static_cast<int32_t>(height)}};
            gWheelQuad.pose = gWheel.pose;
            gWheelQuad.size = {quadWidth, quadWidth * static_cast<float>(cropPx.height) / static_cast<float>(cropPx.width)};
            wheelLayer = &gWheelQuad;
        }
        // Where the wheel really is: once per opening, after its grow animation, until measured.
        if (autoCrop && !gWheel.gridTaken && !gMeasured.Valid(hud_panel::Width(), hud_panel::Height()) && now - gWheel.openedMs >= kWheelGridAfterMs) {
            gWheel.gridTaken = true;
            CaptureGrid(context, device.Get(), true);
        }
    } else if (gWheel.mode >= 0 && wheelMode < 0) {
        LogBoth(std::string("[MEVR-WHEEL] the ") + (gWheel.mode == 3 ? "weapon" : "power") + " wheel closed; its panel is gone");
        gWheel.mode = -1;
        gWheel.placed = false;
    }

    hud_panel::EndFrame(context, draws, hud_panel::TakeTintDraws(), galaxyWindowUi);
    tPanelPresent.stage = 2;
    tPanelPresent.shown = panelShown;
    // Next frame's UI goes to the panel only if this one showed it (or nothing went there yet, so
    // the texture can be made), and the panel works.
    bool const armNext = engaged && uiToPanel && (frame.worldFrame || worldUiPending || worldUi || screenUi) &&
        (panelShown || !hud_panel::HasTexture() || worldUiPending || worldUi);
    mevr::host::world_ui::RedirectReady(worldUi && panelShown && draws > 0 && hud_panel::Armed());
    if (armNext != hud_panel::Armed()) {
        mevr::log::Info("overlays: game UI goes to {}", !armNext ? "the eye images" : galaxyUi ? "a panel over the galaxy hologram's table"
                                                                                                : screenUi ? "a layer in front of the 3D screen"
                                                                                                : worldUi ? "a screen standing in the room"
                                                                                                : "the body-locked HUD panel");
        hud_panel::NoteArmChange();
    }
    hud_panel::SetArmed(armNext);
    NoteGalaxyUi(galaxyWindowUi, draws, textDraws, earlyDraws, earlyTextDraws,
        gGalaxyEarlyUi.load(std::memory_order_relaxed), panelShown, now);
    gGalaxyEarlyUi.store(armNext && galaxyWindowUi, std::memory_order_release);
    if (!galaxyWindowUi) gGalaxyOrderLines = 0;

    // ---- The gun's sight (plugin/SightLayers): a red dot or holo reticle, or a scope's picture ----
    // Worked out before the ring reticle, which hides while an eye looks through the sight (held 0.3 s so
    // the edge of the window doesn't make it blink); its quads go on top of the ring's.
    mevr::host::sight_layers::Frame sightFrame;
    sightFrame.session = session;
    sightFrame.appSpace = static_cast<XrSpace>(frame.appSpace);
    sightFrame.context = context;
    sightFrame.device = device.Get();
    sightFrame.worldFrame = frame.worldFrame;
    sightFrame.menuOpen = frame.menuOpen;
    sightFrame.onFoot = gLastGameMode == 0;
    sightFrame.eyesValid = frame.headValid;
    sightFrame.projectionValid = frame.projectionValid;
    sightFrame.pairArm = frame.pairArm;  // the scope capture's pose for this pair (research/39)
    mevr::host::spectator::NoteFrame(frame.pairArm, frame.floatWhiteScale);  // and the spectator camera's (research/49)
    for (int e = 0; e < 2; ++e) {
        sightFrame.eyes[e].orientation = ToQuat(frame.eyeOrientation[e]);
        sightFrame.eyes[e].position = ToVec(frame.eyePosition[e]);
        sightFrame.projectionSwapchain[e] = static_cast<XrSwapchain>(frame.projectionSwapchain[e]);
        sightFrame.projectionRect[e] = {{frame.projectionRect[e][0], frame.projectionRect[e][1]},
            {frame.projectionRect[e][2], frame.projectionRect[e][3]}};
        sightFrame.projectionPose[e].orientation = ToQuat(frame.projectionOrientation[e]);
        sightFrame.projectionPose[e].position = ToVec(frame.projectionPosition[e]);
        sightFrame.projectionFov[e] = {frame.projectionFov[e][0], frame.projectionFov[e][1], frame.projectionFov[e][2],
            frame.projectionFov[e][3]};
    }
    mevr::host::sight_layers::Output const sight = mevr::host::sight_layers::Layers(sightFrame);
    if (sight.looking && sight.replaceRing) {
        gSightSeenMs = now;
    }
    bool const sightHidesRing = gSightSeenMs != 0 && now - gSightSeenMs < 300;

    // ---- The Mako's lever and stick (research/46): under the reticle ----
    {
        mc::View const view = mc::Latest();
        if (view.size != 0 && view.size != sizeof(mc::View) && view.size != gMakoViewSize) {
            gMakoViewSize = view.size;
            mevr::log::Warn("overlays: the mod's Mako controls are {} bytes, the host's {}: not drawn; rebuild both", view.size, sizeof(mc::View));
        }
        bool const wanted = view.size == sizeof(mc::View) && view.visible && frame.worldFrame && !frame.menuOpen && frame.headValid &&
            gLastGameMode == 1 && device != nullptr;
        bool shown = false;
        if (wanted && count + 2 <= capacity) {
            bool const lever = !gLeverQuad.swapchain.Refused() &&
                UpdateControl(context, device.Get(), gLeverQuad, mc::kLeverPixelsWide, mc::kLeverPixelsLong,
                    mc::LeverKey(view.lever, view.detent, view.leverNear, view.leverHeld), "Mako lever", "makolever",
                    [&view] { return mc::LeverPixels(view.lever, view.travelM, view.detent, view.leverNear, view.leverHeld); });
            bool const stick = !gStickQuad.swapchain.Refused() &&
                UpdateControl(context, device.Get(), gStickQuad, mc::kStickPixels, mc::kStickPixels,
                    mc::StickKey(view.steer, view.stickNear, view.stickHeld), "Mako stick", "makostick",
                    [&view] { return mc::StickPixels(view.steer, view.stickNear, view.stickHeld); });
            if (lever) {
                PlaceControl(gLeverQuad, view.leverBase, view.forward, mc::kLeverPixelsWide, mc::kLeverPixelsLong, mc::kLeverWidthM, mc::kLeverLengthM, frame.appSpace);
                layersOut[count++] = &gLeverQuad.quad;
            }
            if (stick) {
                PlaceControl(gStickQuad, view.stickBase, view.forward, mc::kStickPixels, mc::kStickPixels, mc::kStickSizeM, mc::kStickSizeM, frame.appSpace);
                layersOut[count++] = &gStickQuad.quad;
            }
            shown = lever || stick;
        }
        if (shown != gMakoControlsShown) {
            gMakoControlsShown = shown;
            if (shown) {
                char line[256];
                std::snprintf(line, sizeof line, "[MEVR-MAKO] the lever and the stick are drawn: lever at (%.2f, %.2f, %.2f), stick at (%.2f, %.2f, %.2f) m",
                    view.leverBase.x, view.leverBase.y, view.leverBase.z, view.stickBase.x, view.stickBase.y, view.stickBase.z);
                LogBoth(line);
            } else {
                LogBoth("[MEVR-MAKO] the lever and the stick are gone");
            }
        }
    }

    // ---- Hand reticle (plugin/HandReticle) ----
    // Where the weapon points: the mod's aim ray (muzzle and barrel) once it publishes one, else the
    // right controller's aim pose. Game-reticle suppression lasts throughout VR gameplay.
    // On foot; in the Mako (game mode 1, research/44) only on a ray the mod publishes (the controller's, while it aims the
    // turret), never the controller fallback.
    {
        mevr::host::hand_reticle::Frame reticle;
        reticle.session = session;
        reticle.appSpace = static_cast<XrSpace>(frame.appSpace);
        reticle.context = context;
        reticle.device = device.Get();
        reticle.worldFrame = frame.worldFrame;
        reticle.menuOpen = frame.menuOpen;
        reticle.headValid = frame.headValid;
        reticle.headPosition = ToVec(frame.headPosition);
        reticle.gameMode = gLastGameMode;
        reticle.handAim = handAim;
        reticle.sightHidesRing = sightHidesRing;
        reticle.enabled = cfg.handReticle;
        reticle.distanceM = cfg.handReticleDistanceM;
        reticle.sizeDeg = cfg.handReticleSizeDeg;
        XrCompositionLayerQuad const* const ring = mevr::host::hand_reticle::Layer(reticle);
        // Independent of a weapon, aim ray, controller tracking or a free turret hand.
        // The game-thread consumer preserves the native sniper movie and the game's visibility.
        // OverlayLayers runs only inside an XR frame, including a Mako shown on Halcyon's flat VR panel.
        mevr::host::renderer::SetScreenReticleHidden(cfg.handReticle && gameplay);
        if (ring != nullptr && count < capacity) {
            layersOut[count++] = ring;
        }
    }

    // The sight's quads, on top of the ring.
    for (unsigned i = 0; i < sight.count && count < capacity; ++i) {
        layersOut[count++] = sight.layers[i];
    }

    // ---- The galaxy hologram (plugin/GalaxyHologram, research/51 section 8): the map on a table, over the game's flat screen;
    // before the game pointer, which leaves the trigger alone while a controller points at the table ----
    {
        mevr::host::galaxy_hologram::Frame galaxy;
        galaxy.session = session;
        galaxy.appSpace = static_cast<XrSpace>(frame.appSpace);
        galaxy.context = context;
        galaxy.device = device.Get();
        galaxy.gameMode = frame.gameMode;
        galaxy.menuOpen = frame.menuOpen;
        galaxy.headValid = frame.headValid;
        galaxy.head = {ToQuat(frame.headOrientation), ToVec(frame.headPosition)};
        for (int e = 0; e < 2; ++e) {
            galaxy.eyes[e] = {ToQuat(frame.eyeOrientation[e]), ToVec(frame.eyePosition[e])};
            XrFovf const fov{frame.projectionFov[e][0], frame.projectionFov[e][1], frame.projectionFov[e][2], frame.projectionFov[e][3]};
            bool const sane = fov.angleRight - fov.angleLeft > 0.5f && fov.angleUp - fov.angleDown > 0.5f;
            galaxy.fov[e] = sane ? fov : XrFovf{-0.785398f, 0.785398f, 0.785398f, -0.785398f};
        }
        galaxy.eyeWidth = frame.projectionRect[0][2];
        galaxy.eyeHeight = frame.projectionRect[0][3];
        if (XrCompositionLayerBaseHeader const* layer = mevr::host::galaxy_hologram::Layer(galaxy); layer != nullptr && count < capacity) {
            layersOut[count++] = layer;
        }
        // Squad placement's arc and ring while the right B is held (game/SquadPlace, HostServices v32).
        {
            mevr::host::squad_place_draw::Frame place;
            place.session = galaxy.session;
            place.appSpace = galaxy.appSpace;
            place.context = galaxy.context;
            place.device = galaxy.device;
            place.headValid = galaxy.headValid;
            place.head = galaxy.head;
            for (int e = 0; e < 2; ++e) {
                place.eyes[e] = galaxy.eyes[e];
                place.fov[e] = galaxy.fov[e];
            }
            place.eyeWidth = galaxy.eyeWidth;
            place.eyeHeight = galaxy.eyeHeight;
            if (XrCompositionLayerBaseHeader const* layer = mevr::host::squad_place_draw::Layer(place); layer != nullptr && count < capacity) {
                layersOut[count++] = layer;
            }
        }
        // The game's own map as a stereo window over the table (the mod's captures), under the game's UI.
        XrCompositionLayerBaseHeader const* window[2] = {};
        int const windowLayers = mevr::host::galaxy_hologram::WindowLayers(galaxy, window);
        for (int i = 0; i < windowLayers && count < capacity; ++i) {
            layersOut[count++] = window[i];
        }
        // The game's UI over the table (its travel question; with the window, its whole map: labels, title, buttons): over the
        // hologram, and the game pointer's while the game asks or the window shows the map.
        if (galaxyPanel && count < capacity) {
            layersOut[count++] = &gPanelQuad;
            if (galaxyWindowUi || mevr::host::galaxy_hologram::AskingTravel()) {
                bool const flash = galaxyScene && mevr::vr::galaxy_map::FlashInteraction(galaxyScene->level, galaxyScene->askTravel != 0);
                mevr::host::game_pointer::NoteGameUiQuad(gPanelQuad.pose, gPanelQuad.size.width, gPanelQuad.size.height,
                    flash ? mevr::host::game_pointer::PanelKind::GameMenu : mevr::host::game_pointer::PanelKind::GalaxyMap);
            }
        }
    }

#if defined(MEVR_WITH_MAIN_MENU)
    XrCompositionLayerQuad const* railLayer = nullptr;
    // ---- The omni-tool's rail (plugin/OmniToolRail, research/57): beside the game's screen on the wrist, or alone on the wrist
    // when the player taps it open ----
    {
        mevr::host::omni_rail::Frame rail;
        rail.session = session;
        rail.appSpace = static_cast<XrSpace>(frame.appSpace);
        rail.device = device.Get();
        rail.context = context;
        rail.headValid = frame.headValid;
        rail.eyes = ToVec(frame.headPosition);
        rail.screen = worldShown;
        rail.display = mevr::vr::world_ui::LatestEmitter(400);  // the mod sends it every tick while the rail stands alone
        if (pieceControllers.active && pieceControllers.left.gripValid) {
            rail.wristValid = true;
            rail.wrist = hp::WristPoint(pieceControllers.left.grip, hud.wristOffset);
        }
        if (pieces.bodyValid) {
            rail.body = mevr::vr::omni_rail::Body{pieces.leftWrist, pieces.leftElbow, pieces.rightIndex1, pieces.rightIndex2};
        }
        rail.gunDrawn = pieces.gunVisible;
        rail.gameMode = gLastGameMode;
        rail.othersOwnInput = frame.menuOpen || mevr::host::main_menu::IsOpen() || mevr::host::calibration::Active();
        railLayer = mevr::host::omni_rail::Layer(rail);
    }
#endif

    // ---- The game pointer (plugin/GamePointer, research/42): on a flat frame (the game's menus, the galaxy map), a
    // controller's ray on the flat screen is the game's mouse; its dot sits on the quad, above it in layer order.
    // Stepped every frame (it releases what it holds when the flat screen goes away) ----
    {
        mevr::host::game_pointer::Frame pointerFrame;
        pointerFrame.appSpace = static_cast<XrSpace>(frame.appSpace);
        pointerFrame.halcyonMenuOpen = frame.menuOpen;
#if defined(MEVR_WITH_MAIN_MENU)
        pointerFrame.railOwnsInput = railLayer != nullptr && mevr::host::omni_rail::OwnsTriggers();
#endif
        if (pieces.bodyValid) pointerFrame.body = mevr::vr::omni_rail::Body{pieces.leftWrist, pieces.leftElbow, pieces.rightIndex1, pieces.rightIndex2};
        pointerFrame.headValid = frame.headValid;
        pointerFrame.head = ToVec(frame.headPosition);
        mevr::host::game_pointer::Dot dot;
        XrCompositionLayerQuad const* dotLayer = nullptr;
        if (mevr::host::game_pointer::Step(pointerFrame, dot) && count < capacity && device != nullptr &&
            (dotLayer = mevr::host::game_pointer::DotLayer(session, static_cast<XrSpace>(frame.appSpace), context, device.Get(), dot)) != nullptr) {
            layersOut[count++] = dotLayer;
            if (!gLoggedPointerDot) {
                gLoggedPointerDot = true;
                mevr::log::Info("overlays: first game pointer dot at ({:.2f}, {:.2f}, {:.2f}) m, {:.3f} m across", dot.pose.position.x,
                    dot.pose.position.y, dot.pose.position.z, dot.size);
            }
        }
    }

    // ---- The power wheel at the left hand (plugin/PowerWheel, research/55), its hint card, the lock's marker: on top of ours ----
    if (count < capacity) {
        mevr::host::power_wheel::Frame wheel;
        wheel.session = session;
        wheel.appSpace = static_cast<XrSpace>(frame.appSpace);
        wheel.context = context;
        wheel.device = device.Get();
        wheel.worldFrame = frame.worldFrame;
        wheel.menuOpen = frame.menuOpen;
        wheel.head = ToVec(frame.headPosition);
        wheel.headValid = frame.headValid;
        XrCompositionLayerQuad const* wheelLayers[3] = {};
        int const n = mevr::host::power_wheel::Layers(wheel, wheelLayers, static_cast<int>(std::min<unsigned>(3u, capacity - count)));
        for (int i = 0; i < n; ++i) {
            layersOut[count++] = wheelLayers[i];
        }
    }
    // ---- The squad weapon menu, at the left hand (plugin/SquadPanel): last, on top of ours ----
    if (count < capacity) {
        mevr::host::squad_panel::Frame squad;
        squad.session = session;
        squad.appSpace = static_cast<XrSpace>(frame.appSpace);
        squad.context = context;
        squad.device = device.Get();
        squad.worldFrame = frame.worldFrame;
        squad.menuOpen = frame.menuOpen;
        squad.headValid = frame.headValid;
        squad.head.orientation = ToQuat(frame.headOrientation);
        squad.head.position = ToVec(frame.headPosition);
        if (XrCompositionLayerQuad const* layer = mevr::host::squad_panel::Layer(squad)) {
            layersOut[count++] = layer;
        }
    }
    // ---- The game's wheel at the hand: last, on top (the conversation's wheel likewise, research/59) ----
    if (convoWheelLayer != nullptr && count < capacity) {
        layersOut[count++] = convoWheelLayer;
    }
    if (wheelLayer != nullptr && count < capacity) {
        layersOut[count++] = wheelLayer;
        if (!gWheel.loggedThisOpening) {
            gWheel.loggedThisOpening = true;
            mevr::log::Info("overlays: wheel panel submitted ({}x{}, {:.2f} x {:.2f} m)", gWheelSwapchain.width, gWheelSwapchain.height,
                gWheelQuad.size.width, gWheelQuad.size.height);
        }
    }

    // ---- The comfort vignette (plugin/Vignette, research/31): head-locked, under all of ours (first), so the HUD,
    // the reticle and the sight stay clear. Asked for last, so it never takes a slot from the others (when it shows,
    // the player is turning or walking: no squad menu, no wheel). Nothing at all while it isn't wanted ----
    if (count < capacity) {
        mevr::host::vignette::Frame vignette;
        vignette.session = session;
        vignette.context = context;
        vignette.device = device.Get();
        vignette.worldFrame = frame.worldFrame;
        vignette.menuOpen = frame.menuOpen;
        vignette.fovValid = frame.projectionValid;
        std::memcpy(vignette.fov, frame.projectionFov, sizeof vignette.fov);
        if (XrCompositionLayerQuad const* layer = mevr::host::vignette::Layer(vignette)) {
            for (unsigned i = count; i > 0; --i) {
                layersOut[i] = layersOut[i - 1];
            }
            layersOut[0] = layer;
            ++count;
        }
    }

    // ---- The guided calibration (plugin/Calibration): while it runs, its quads are the only ones of ours (the
    // first-run prompt goes on top of the rest) ----
    {
        mevr::host::calibration::Frame calibration;
        calibration.session = session;
        calibration.appSpace = static_cast<XrSpace>(frame.appSpace);
        calibration.context = context;
        calibration.device = device.Get();
        calibration.worldFrame = frame.worldFrame;
#if defined(MEVR_WITH_MAIN_MENU)
        // Our menu hides the first-run prompt as Halcyon's does: its delay and timeout wait for it to close.
        calibration.menuOpen = frame.menuOpen || mevr::host::main_menu::IsOpen();
#else
        calibration.menuOpen = frame.menuOpen;
#endif
        calibration.gameMode = gLastGameMode;
        calibration.headValid = frame.headValid;
        calibration.head.orientation = head;
        calibration.head.position = ToVec(frame.headPosition);
        for (int e = 0; e < 2; ++e) {
            calibration.eyes[e] = ToVec(frame.eyePosition[e]);
        }
        XrCompositionLayerQuad const* calibrationLayers[6] = {};
        bool const running = mevr::host::calibration::Running();
        unsigned const room = std::min<unsigned>(running ? capacity : capacity - count, 6u);
        unsigned const added = mevr::host::calibration::Layers(calibration, calibrationLayers, room);
        if (mevr::host::calibration::Active()) {
            count = 0;
        }
        for (unsigned i = 0; i < added && count < capacity; ++i) {
            layersOut[count++] = calibrationLayers[i];
        }
    }
#if defined(MEVR_WITH_MAIN_MENU)
    {
        if (XrCompositionLayerQuad const* layer = railLayer; layer != nullptr && count < capacity) {
            layersOut[count++] = layer;
        }
        if (XrCompositionLayerQuad const* target = mevr::host::omni_rail::TargetLayer(); target != nullptr && count < capacity) {
            layersOut[count++] = target;  // the wrist tap's target, over the wrist readout's layer
            static bool logged = false;
            if (!logged) {
                logged = true;
                mevr::log::Info("omni-tool rail: the wrist tap's target goes in as our quad {} of {}", count, capacity);
            }
        }
    }
    // ---- The bypass puzzle's d-pad (plugin/OmniPad, research/53 section 6.3): to the right of its screen while it's played ----
    {
        mevr::host::omni_pad::Frame padFrame;
        padFrame.session = session;
        padFrame.appSpace = static_cast<XrSpace>(frame.appSpace);
        padFrame.device = device.Get();
        padFrame.context = context;
        padFrame.screen = worldShown;
        if (pieces.bodyValid) padFrame.body = mevr::vr::omni_rail::Body{pieces.leftWrist, pieces.leftElbow, pieces.rightIndex1, pieces.rightIndex2};
        padFrame.othersOwnInput = frame.menuOpen || mevr::host::main_menu::IsOpen() || mevr::host::calibration::Active();
        if (XrCompositionLayerQuad const* layer = mevr::host::omni_pad::Layer(padFrame); layer != nullptr && count < capacity) {
            layersOut[count++] = layer;
        }
    }
    // ---- Thespian mode's panel (plugin/Thespian, research/70): the replies as rows to read aloud, under the speaker ----
    {
        mevr::host::thespian::Frame panelFrame;
        panelFrame.session = session;
        panelFrame.appSpace = static_cast<XrSpace>(frame.appSpace);
        panelFrame.device = device.Get();
        panelFrame.context = context;
        panelFrame.head.orientation = ToQuat(frame.headOrientation);
        panelFrame.head.position = ToVec(frame.headPosition);
        panelFrame.othersOwnInput = frame.menuOpen || mevr::host::main_menu::IsOpen() || mevr::host::calibration::Active();
        if (XrCompositionLayerQuad const* layer = mevr::host::thespian::Layer(panelFrame); layer != nullptr && count < capacity) {
            layersOut[count++] = layer;
        }
    }
    // ---- Our main menu (plugin/MainMenu): while it's open, its quad is the only one of ours (SteamVR may sort quads by
    // distance, and nothing of the game's is for now). Closed, it returns at once ----
    {
        mevr::host::main_menu::Frame menu;
        menu.session = session;
        menu.appSpace = static_cast<XrSpace>(frame.appSpace);
        menu.context = context;
        menu.device = device.Get();
        menu.worldFrame = frame.worldFrame;
        menu.gameMode = gLastGameMode;  // the first run waits for on foot
        menu.headValid = frame.headValid;
        menu.head.orientation = head;
        menu.head.position = ToVec(frame.headPosition);
        menu.projectionValid = frame.projectionValid;
        menu.projectionSwapchain = static_cast<XrSwapchain>(frame.projectionSwapchain[0]);
        menu.projectionRect = {{frame.projectionRect[0][0], frame.projectionRect[0][1]}, {frame.projectionRect[0][2], frame.projectionRect[0][3]}};
        menu.projectionPose.orientation = ToQuat(frame.projectionOrientation[0]);
        menu.projectionPose.position = ToVec(frame.projectionPosition[0]);
        menu.projectionFov = {frame.projectionFov[0][0], frame.projectionFov[0][1], frame.projectionFov[0][2], frame.projectionFov[0][3]};
        if (XrCompositionLayerQuad const* layer = mevr::host::main_menu::Layer(menu); layer != nullptr && capacity > 0) {
            if (!mevr::host::main_menu::LiveAdjusting()) count = 0;
            else {
                mevr::host::calibration::Frame markers;
                markers.session = session;
                markers.appSpace = menu.appSpace;
                markers.context = context;
                markers.device = device.Get();
                markers.worldFrame = frame.worldFrame;
                markers.menuOpen = frame.menuOpen;
                markers.headValid = menu.headValid;
                markers.head = menu.head;
                for (int e = 0; e < 2; ++e) markers.eyes[e] = ToVec(frame.eyePosition[e]);
                XrCompositionLayerQuad const* refs[2]{};
                unsigned const added = mevr::host::calibration::ManualMarkerLayers(markers,
                    mevr::host::main_menu::LiveSelectedHand(), refs, capacity - 1);
                mevr::host::main_menu::NoteLiveMarkers(added == 2);
                // Reserve the paired references and the live panel; game-world hands remain in the projection.
                count = std::min(count, capacity - added - 1);
                for (unsigned i = 0; i < added; ++i) layersOut[count++] = refs[i];
            }
            if (count >= capacity) count = capacity - 1; // live controls always retain a layer slot
            layersOut[count++] = layer;
        }
    }
#endif
    return count;
}

} // namespace MELEVR::MevrBridge

// SPDX-License-Identifier: MIT
// The omni-tool's rail in the host (plugin/OmniToolRail.hpp, research/57).
#include "plugin/OmniToolRail.hpp"

#include "plugin/MainMenu.hpp"
#include "plugin/menu/MenuRenderer.hpp"
#include "plugin/menu/MenuUi.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/GameMenuRequest.hpp"
#include "core/vr/GamePointer.hpp"
#include "core/vr/MenuPointer.hpp"
#include "core/vr/OmniRail.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "halcyon/le1/mevr_bridge.h"
#include "plugin/MevrMenu.hpp"
#include "plugin/RailArt.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/Swapchains.hpp"

#include <RmlUi/Core.h>

#include <d3d11.h>
#include <wrl/client.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr_platform.h>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <vector>

namespace mevr::host::omni_rail {
namespace {

using Microsoft::WRL::ComPtr;
namespace rail = vr::omni_rail;
namespace gm = vr::game_menu;
namespace gp = vr::game_pointer;
namespace wu = vr::world_ui;

constexpr int kWidth = 360;    // the document's size (dp = pixels), rail::kRailAspect
constexpr int kHeight = 640;
constexpr std::int64_t kSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // our bytes are sRGB already
constexpr std::uint64_t kSettingsEveryMs = 2000;
constexpr float kTipAheadM = 0.035f;  // the index fingertip ahead of the aim pose (plugin/GamePointer's touchTipM)

// The rail's buttons by touch (2026-11-06, the owner's QA: only a perfectly aligned poke selected anything, and the hand went
// through without a feel of the surface): a click the moment the fingertip reaches a button, where it touched (a poke that
// slides along the surface as it pushes in used to turn into a scroll and never click), 2 cm of margin around the rail.
vr::game_pointer::Settings RailPointerSettings() {
    vr::game_pointer::Settings s;
    s.touchClickOnContact = true;
    s.touchScroll = false;
    s.touchMarginM = 0.02f;
    return s;
}

// ---- Any thread ----
std::atomic<bool> gOpen{false};         // the player opened it (the tap, the main menu)
std::atomic<bool> gOwnsTriggers{false};
std::atomic<bool> gShown{false};         // g.shown for other threads (the mod's flags)
std::mutex gStatusMutex;
std::string gStatus = "{\"shown\": false}";
// The tap target's inputs and result, the last frame (for snapshots: why it shows or not).
struct TargetStatus {
    bool stepped = false;
    bool wristValid = false;
    bool headValid = false;
    bool handsTracked = false;
    bool allowed = false;
    char const* refused = nullptr;
    int look = 0;
    bool shown = false;
    float distanceM = -1.0f;
    float belowEyesM = 0.0f;
    float acrossM = 0.0f;
    XrVector3f target{};
    XrVector3f eyes{};
};
TargetStatus gTargetStatus;

// ---- Render thread ----
struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;
    bool failed = false;  // refused at failedMs
    std::uint64_t failedMs = 0;
};

struct Settings {
    bool enabled = true;  // [OmniTool] enabled
    bool tap = true;      // [OmniTool] tap: the left wrist's tap opens it
    bool tapOnBody = true;  // [OmniTool] tapOnBody: the tap's target on Shepard's drawn forearm, his index fingertip (else the controllers')
    float aloneHeightM = rail::kAloneHeightM;
};

struct State {
    bool initTried = false;
    bool ready = false;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> canvas;
    ComPtr<ID3D11RenderTargetView> canvasRtv;
    menu::Ui* ui = nullptr;  // never destroyed (the process's exit mustn't touch D3D)
    Rml::DataModelHandle model;
    Rml::String active;      // the entry of the screen up ("" none)
    bool alone = true;       // no screen beside it: its header shows
    bool art = false;        // the game's own fonts, tab and frame are loaded (plugin/RailArt): the document draws with them
    Rml::String picked;      // set by the document's pick(id) event, handled after the input step
    Swapchain swapchain;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    bool needRender = true;
    bool shown = false;
    gp::Pointer pointer{RailPointerSettings()};
    float behindBuzzS = 0.0f;  // the fingertip through the surface: a faint buzz every 0.08 s, the surface's feel
    rail::Tap tap;
    std::optional<vr::world_ui::Panel> lastPanel;  // where the rail stood last frame (the tap isn't taken while going for it)
    std::uint64_t tapsHeld = 0;                    // frames a fingertip near a panel kept the tap off
    LARGE_INTEGER lastCounter{};
    Settings settings;
    std::uint64_t settingsMs = 0;
    // The tap's target (rail::TargetLookFor): one small static swapchain per look, faint / near / armed / pressed.
    Swapchain target;                         // one for all four looks (EnsureTarget)
    ComPtr<ID3D11Texture2D> targetSource[4];  // each look's picture
    int targetDrawn = -1;                     // the look the swapchain holds
    XrCompositionLayerQuad targetQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    bool targetShown = false;
    bool onBody = false;  // the tap on Shepard's drawn arm this frame
    rail::TargetLook targetLook = rail::TargetLook::Hidden;
    float pressedS = 0.0f;       // the pressed look's time left after a tap
    // A reach for the target (the right fingertip within kTargetNearM): its closest approach, logged when it ends.
    bool reaching = false;
    float closestM = 0.0f;
    bool reachTapped = false;
    char const* reachRefused = nullptr;  // why the tap wasn't allowed during it, if it wasn't
    int reaches = 0;
    std::uint64_t reachLogMs = 0;
    int taps = 0;
    int picks = 0;
    std::string last = "none";
};
State g;

double Seconds(LARGE_INTEGER counter) {
    static LARGE_INTEGER const frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

void Say(menu::LogLevel level, std::string const& text) {
    switch (level) {
    case menu::LogLevel::Error: log::Error("omni-tool rail: {}", text); break;
    case menu::LogLevel::Warn: log::Warn("omni-tool rail: {}", text); break;
    case menu::LogLevel::Info: log::Info("omni-tool rail: {}", text); break;
    }
}

void ReadSettings() {
    std::uint64_t const now = GetTickCount64();
    if (now - g.settingsMs < kSettingsEveryMs && g.settingsMs != 0) {
        return;
    }
    g.settingsMs = now;
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    if (path.empty()) {
        return;
    }
    Settings s;
    s.enabled = GetPrivateProfileIntW(L"OmniTool", L"enabled", 1, path.c_str()) != 0;
    s.tap = GetPrivateProfileIntW(L"OmniTool", L"tap", 1, path.c_str()) != 0;
    s.tapOnBody = GetPrivateProfileIntW(L"OmniTool", L"tapOnBody", 1, path.c_str()) != 0;
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"OmniTool", L"aloneHeightM", L"", text, 32, path.c_str());
    if (auto const v = ini::Number(ini::Utf8(std::filesystem::path(text)))) {
        s.aloneHeightM = std::clamp(static_cast<float>(*v), 0.10f, 0.60f);
    }
    static bool logged = false;
    if (!logged || s.enabled != g.settings.enabled || s.tap != g.settings.tap || s.tapOnBody != g.settings.tapOnBody ||
        s.aloneHeightM != g.settings.aloneHeightM) {
        logged = true;
        log::Info("omni-tool rail: [OmniTool] enabled = {}, tap = {} (tap the left wrist with the right index fingertip), tapOnBody = {}, "
                  "aloneHeightM = {:.2f}",
            s.enabled ? 1 : 0, s.tap ? 1 : 0, s.tapOnBody ? 1 : 0, s.aloneHeightM);
    }
    g.settings = s;
    if (!s.enabled) {
        gOpen.store(false, std::memory_order_relaxed);
    }
}

bool Init(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (g.initTried) {
        return g.ready;
    }
    g.initTried = true;
    g.device = device;
    g.context = context;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kWidth;
    td.Height = kHeight;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&td, nullptr, g.canvas.GetAddressOf())) ||
        FAILED(device->CreateRenderTargetView(g.canvas.Get(), nullptr, g.canvasRtv.GetAddressOf()))) {
        log::Error("omni-tool rail: canvas {}x{} failed; no rail", kWidth, kHeight);
        return false;
    }
    g.ui = new menu::Ui();
    menu::UiSetup setup;
    setup.device = device;
    setup.documents = main_menu::AssetsFolder();
    setup.document = L"omnitool.rml";
    setup.fontDirs = {main_menu::FontsFolder()};
    setup.width = kWidth;
    setup.height = kHeight;
    setup.log = &Say;
    if (!g.ui->Init(setup)) {
        return false;
    }
    Rml::DataModelConstructor ctor = g.ui->Context()->CreateDataModel("omnitool");
    if (!ctor) {
        log::Error("omni-tool rail: its data model couldn't be made; no rail");
        return false;
    }
    ctor.Bind("active", &g.active);
    ctor.Bind("alone", &g.alone);
    ctor.Bind("art", &g.art);
    ctor.BindEventCallback("pick", [](Rml::DataModelHandle, Rml::Event&, Rml::VariantList const& args) {
        if (!args.empty()) {
            g.picked = args[0].Get<Rml::String>();
        }
    });
    g.model = ctor.GetModelHandle();
    // The game's own art (research/57 section 5), before the document loads so its fonts resolve; without it the rail keeps
    // its own drawn look.
    rail_art::Status const art = rail_art::Load(device, g.ui->GetRenderer(), {});
    g.art = art.fonts && art.tabs && art.panel;
    log::Info("omni-tool rail: {}{}", art.summary, g.art ? "" : " (its own look until the game's art is cached)");
    if (!g.ui->LoadDocument()) {
        log::Error("omni-tool rail: omnitool.rml didn't load from {} (deployed? see the rmlui: lines); no rail",
            ini::Utf8(main_menu::AssetsFolder()));
        return false;
    }
    g.ready = true;
    log::Info("omni-tool rail: ready ({}x{} canvas, {})", kWidth, kHeight, ini::Utf8(main_menu::AssetsFolder() / L"omnitool.rml"));
    return true;
}

bool EnsureSwapchain(XrSession session) {
    if (g.swapchain.handle != XR_NULL_HANDLE) {
        return true;
    }
    if (g.swapchain.failed && !swapchains::RetryDue(g.swapchain.failedMs)) {
        return false;  // refused just now (plugin/Swapchains: tried again in 2 s, or once room was freed)
    }
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    info.format = kSwapchainFormat;
    info.sampleCount = 1;
    info.width = kWidth;
    info.height = kHeight;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    // Freed for room (plugin/Swapchains) when no frame has shown it for a second; made and drawn again when next wanted.
    auto evict = [] {
        swapchains::Destroy(g.swapchain.handle);
        g.swapchain = {};
        g.needRender = true;
    };
    if (XR_FAILED(swapchains::Create(session, info, "omni-tool rail", evict, g.swapchain.handle, g.swapchain.images))) {
        g.swapchain = {};  // plugin/Swapchains logged it
        g.swapchain.failed = true;
        g.swapchain.failedMs = GetTickCount64();
        return false;
    }
    testloop::NameSwapchain(g.swapchain.handle, "omnirail");  // snapshots: <stamp>-omnirail.png, and in the eyes
    g.needRender = true;  // a new swapchain holds no picture yet (the rail redraws only on change)
    return true;
}

bool Submit() {
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    std::uint32_t index = 0;
    if (XR_FAILED(xrAcquireSwapchainImage(g.swapchain.handle, &acquire, &index))) {
        return false;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    bool const ok = XR_SUCCEEDED(xrWaitSwapchainImage(g.swapchain.handle, &wait)) && index < g.swapchain.images.size();
    if (ok) {
        g.context->CopyResource(g.swapchain.images[index], g.canvas.Get());
        testloop::OnImageWritten(g.swapchain.handle, g.swapchain.images[index]);
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(g.swapchain.handle, &release);
    return ok;
}

// ---- The tap's target ----
constexpr std::uint32_t kTargetPixels = 128;

// One look's picture, premultiplied in linear light, sRGB-encoded (0xAABBGGRR): a soft disc, a ring and a centre dot, the
// omni-tool's orange (faint, near) or ME1's cyan (armed, pressed).
std::vector<std::uint32_t> TargetPixels(int look) {
    struct Style {
        float r, g, b;
        float strength;
        float ring;  // ring thickness (of the half-size)
        float fill;
    };
    static constexpr Style kStyles[4] = {
        {1.0f, 0.36f, 0.08f, 0.45f, 0.05f, 0.08f},  // faint: the wrist raised
        {1.0f, 0.40f, 0.10f, 0.85f, 0.07f, 0.14f},  // near: the right hand coming
        {0.22f, 0.66f, 1.0f, 1.0f, 0.08f, 0.22f},   // armed: in tap range
        {0.75f, 0.93f, 1.0f, 1.0f, 0.10f, 0.70f},   // pressed
    };
    Style const& s = kStyles[std::clamp(look, 0, 3)];
    auto encode = [](float linear) {
        float const c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint32_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    float const px = 2.0f / static_cast<float>(kTargetPixels);
    auto band = [px](float d, float lo, float hi) {
        return std::clamp((d - lo) / px + 0.5f, 0.0f, 1.0f) * std::clamp((hi - d) / px + 0.5f, 0.0f, 1.0f);
    };
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kTargetPixels) * kTargetPixels);
    for (std::uint32_t y = 0; y < kTargetPixels; ++y) {
        for (std::uint32_t x = 0; x < kTargetPixels; ++x) {
            float const u = (static_cast<float>(x) + 0.5f) * px - 1.0f;
            float const v = (static_cast<float>(y) + 0.5f) * px - 1.0f;
            float const d = std::sqrt(u * u + v * v);
            float const ring = band(d, 0.74f - s.ring, 0.74f);
            float const glow = 0.35f * std::exp(-((d - 0.70f) / 0.13f) * ((d - 0.70f) / 0.13f));
            float const dot = band(d, -1.0f, 0.20f);
            float const fill = s.fill * band(d, -1.0f, 0.70f);
            float const a = std::clamp(s.strength * (ring + glow + 0.9f * dot + fill), 0.0f, 1.0f);
            pixels[static_cast<std::size_t>(y) * kTargetPixels + x] = encode(s.r * a) | (encode(s.g * a) << 8) | (encode(s.b * a) << 16) |
                                                                       (static_cast<std::uint32_t>(a * 255.0f + 0.5f) << 24);
        }
    }
    return pixels;
}

// One swapchain for all four looks (the runtime's swapchains are limited: on 2026-11-06 the calibration's board failed with
// XR_ERROR_LIMIT_REACHED and showed no targets, when this target had taken up to four, one per look). Each look's picture is a
// small immutable texture; a change of look copies it into the swapchain once.
bool EnsureTarget(XrSession session, ID3D11Device* device, ID3D11DeviceContext* context, int look) {
    Swapchain& chain = g.target;
    if (chain.failed && !swapchains::RetryDue(chain.failedMs)) {
        return false;  // refused just now (plugin/Swapchains: tried again in 2 s, or once room was freed)
    }
    if (chain.handle == XR_NULL_HANDLE) {
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format = kSwapchainFormat;
        info.sampleCount = 1;
        info.width = kTargetPixels;
        info.height = kTargetPixels;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;
        // Freed for room (plugin/Swapchains) when no frame has shown it for a second; made and filled again when next wanted.
        auto evict = [] {
            swapchains::Destroy(g.target.handle);
            g.target = {};
            g.targetDrawn = -1;
        };
        if (XR_FAILED(swapchains::Create(session, info, "omni-tool tap target", evict, chain.handle, chain.images))) {
            chain = {};  // no target until it's made; plugin/Swapchains logged it
            chain.failed = true;
            chain.failedMs = GetTickCount64();
            return false;
        }
        testloop::NameSwapchain(chain.handle, "omnitarget");
        g.targetDrawn = -1;
    }
    if (look == g.targetDrawn) {
        return true;
    }
    std::vector<std::uint32_t> const pixels = TargetPixels(look);
    if (!g.targetSource[look]) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = kTargetPixels;
        td.Height = kTargetPixels;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(), kTargetPixels * 4, 0};
        if (FAILED(device->CreateTexture2D(&td, &data, g.targetSource[look].GetAddressOf()))) {
            return false;
        }
    }
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    std::uint32_t index = 0;
    if (XR_FAILED(xrAcquireSwapchainImage(chain.handle, &acquire, &index))) {
        return false;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    bool const ok = XR_SUCCEEDED(xrWaitSwapchainImage(chain.handle, &wait)) && index < chain.images.size();
    if (ok) {
        context->CopyResource(chain.images[index], g.targetSource[look].Get());
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(chain.handle, &release);
    if (ok) {
        g.targetDrawn = look;
        testloop::SetStaticImage(chain.handle, kTargetPixels, kTargetPixels, pixels);  // snapshots draw the look shown
    }
    return ok;
}
char const* LookName(rail::TargetLook look) {
    switch (look) {
    case rail::TargetLook::Faint: return "faint";
    case rail::TargetLook::Near: return "near";
    case rail::TargetLook::Armed: return "armed";
    default: return "hidden";
    }
}

// The target's look and quad this frame, the approach's haptic tick and the reach's log line. `tip` is valid with handsTracked.
void StepTarget(Frame const& frame, bool handsTracked, XrVector3f const& tip, XrVector3f const& target, bool allowed,
    char const* refused, bool tapped, float dt) {
    g.targetShown = false;
    if (tapped) {
        g.pressedS = 0.25f;
    }
    g.pressedS = std::max(0.0f, g.pressedS - dt);
    rail::TargetLook const look = frame.wristValid && frame.headValid
        ? rail::TargetLookFor(allowed, frame.eyes, target, handsTracked, tip, g.tap.Armed(), rail::TapSettings{}.releaseM)
        : rail::TargetLook::Hidden;
    if (look == rail::TargetLook::Armed && g.targetLook != rail::TargetLook::Armed) {
        halcyon::RequestHaptic(1, 0.012f, 0.2f, 0.0f);  // in tap range: a light tick in the right hand
    }
    g.targetLook = look;
    // The reach: begins with the fingertip within kTargetNearM, ends 5 cm past it or with the tap.
    float const d = handsTracked ? std::sqrt((tip.x - target.x) * (tip.x - target.x) + (tip.y - target.y) * (tip.y - target.y) +
                                             (tip.z - target.z) * (tip.z - target.z))
                                 : 1e9f;
    if (frame.wristValid && d <= rail::kTargetNearM) {
        if (!g.reaching) {
            g.reaching = true;
            g.closestM = d;
            g.reachTapped = false;
            g.reachRefused = nullptr;
        }
        g.closestM = std::min(g.closestM, d);
        if (!allowed && refused != nullptr) {
            g.reachRefused = refused;
        }
        g.reachTapped = g.reachTapped || tapped;
    } else if (g.reaching && (!frame.wristValid || d > rail::kTargetNearM + 0.05f)) {
        g.reaching = false;
        ++g.reaches;
        std::uint64_t const now = GetTickCount64();
        if (g.reachTapped || g.closestM < 0.15f || now - g.reachLogMs > 10000) {  // every near miss; far ones every 10 s
            g.reachLogMs = now;
            log::Info("omni-tool rail: a reach for the wrist target (#{}): closest {:.1f} cm (a tap counts within {:.1f} cm){}", g.reaches,
                g.closestM * 100.0f, rail::TapSettings{}.pressM * 100.0f,
                g.reachTapped ? ": tapped" : g.reachRefused != nullptr ? std::format(": no tap, {}", g.reachRefused).c_str() : ": no tap");
        }
    }
    {
        std::lock_guard lock(gStatusMutex);
        gTargetStatus = {true, frame.wristValid, frame.headValid, handsTracked, allowed, refused, static_cast<int>(look), false,
            handsTracked ? d : -1.0f, frame.eyes.y - target.y, std::hypot(target.x - frame.eyes.x, target.z - frame.eyes.z), target,
            frame.eyes};
    }
    rail::TargetLook const shown = g.pressedS > 0.0f ? rail::TargetLook::Armed : look;  // the pressed picture below
    if (shown == rail::TargetLook::Hidden && g.pressedS <= 0.0f) {
        return;
    }
    int const index = g.pressedS > 0.0f ? 3 : static_cast<int>(shown) - 1;
    if (frame.device == nullptr || frame.context == nullptr || !EnsureTarget(frame.session, frame.device, frame.context, index)) {
        return;
    }
    XrVector3f toward{frame.eyes.x - target.x, frame.eyes.y - target.y, frame.eyes.z - target.z};
    float const n = std::sqrt(toward.x * toward.x + toward.y * toward.y + toward.z * toward.z);
    if (!(n > 1e-4f)) {
        return;
    }
    toward = {toward.x / n, toward.y / n, toward.z / n};
    g.targetQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    g.targetQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
    g.targetQuad.space = frame.appSpace;
    g.targetQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    g.targetQuad.subImage.swapchain = g.target.handle;
    g.targetQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(kTargetPixels), static_cast<int32_t>(kTargetPixels)}};
    g.targetQuad.pose.orientation = vr::overlay::Facing(toward);
    g.targetQuad.pose.position = target;
    g.targetQuad.size = {rail::kTargetSizeM, rail::kTargetSizeM};
    g.targetShown = true;
    std::lock_guard lock(gStatusMutex);
    gTargetStatus.shown = true;
}

void Publish(bool shown, char const* place) {
    std::lock_guard lock(gStatusMutex);
    gStatus = std::format("{{\"shown\": {}, \"open\": {}, \"place\": \"{}\", \"active\": \"{}\", \"taps\": {}, \"picks\": {}, \"last\": \"{}\", "
                          "\"art\": {}, \"pose\": [{:.3f}, {:.3f}, {:.3f}], \"size\": [{:.3f}, {:.3f}]}}",
        shown, gOpen.load(std::memory_order_relaxed), place, g.active, g.taps, g.picks, g.last, g.art, g.quad.pose.position.x,
        g.quad.pose.position.y, g.quad.pose.position.z, g.quad.size.width, g.quad.size.height);
}

// The pointer: the controllers' rays and index fingertips on the rail, as RmlUi's mouse.
void PointerStep(rail::Rail const& at, float dt) {
    vr::controllers::State const state = vr::controllers::Latest();
    gp::Input input;
    input.active = state.active;
    for (int i = 0; i < 2; ++i) {
        vr::controllers::Hand const& hand = i == 0 ? state.left : state.right;
        input.hands[i].aimValid = hand.aimValid && i != 0;  // the left hand wears the omni-tool: no ray or fingertip on its rail
        input.hands[i].aim = hand.aim;
        input.hands[i].trigger = hand.trigger;
        if (hand.aimValid && i != 0) {
            XrVector3f d = vr::menu_pointer::AimDirection(hand.aim);
            float const n = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (n > 1e-6f) {
                input.hands[i].tipValid = true;
                input.hands[i].tip = {hand.aim.position.x + d.x / n * kTipAheadM, hand.aim.position.y + d.y / n * kTipAheadM,
                    hand.aim.position.z + d.z / n * kTipAheadM};
            }
        }
    }
    input.width = kWidth;
    input.height = kHeight;
    input.dt = dt;
    gp::Output const out = g.pointer.Step(gp::Quad{at.pose, at.widthM, at.heightM}, input);
    Rml::Context* const context = g.ui->Context();
    if (out.hit && (out.moved || out.pressEdge)) {
        context->ProcessMouseMove(out.x, out.y, 0);
        g.needRender = true;
    }
    if (out.pressEdge) {
        if (out.pressed) {
            context->ProcessMouseButtonDown(0, 0);
            halcyon::RequestHaptic(out.hand, 0.02f, 0.45f, 0.0f);
        } else {
            context->ProcessMouseButtonUp(0, 0);
            halcyon::RequestHaptic(out.hand, 0.012f, 0.25f, 0.0f);
        }
        g.needRender = true;
    }
    if (out.left) {
        context->ProcessMouseLeave();
        g.needRender = true;
    }
    // Through the surface the hand can't be stopped, so it's felt: a faint buzz while the fingertip is behind it.
    g.behindBuzzS = std::max(0.0f, g.behindBuzzS - dt);
    if (out.touch && out.touchDistance < 0.0f && g.behindBuzzS <= 0.0f) {
        halcyon::RequestHaptic(out.hand, 0.01f, 0.15f, 0.0f);
        g.behindBuzzS = 0.08f;
    }
    gOwnsTriggers.store(out.hit, std::memory_order_relaxed);
}

void Hide(char const* why) {
    if (g.shown) {
        log::Info("omni-tool rail: hidden ({})", why);
        if (g.ready) {
            gp::Output const out = g.pointer.Release();
            if (out.pressEdge && !out.pressed) {
                g.ui->Context()->ProcessMouseButtonUp(0, 0);
            }
            g.ui->Context()->ProcessMouseLeave();
        }
    }
    g.shown = false;
    gShown.store(false, std::memory_order_relaxed);
    gOwnsTriggers.store(false, std::memory_order_relaxed);
    Publish(false, "none");
}

// An entry the document reported: the request to the mod (the main menu's slot, run on the game thread).
void HandlePick(std::optional<wu::Screen> screen) {
    if (g.picked.empty()) {
        return;
    }
    std::string const id = g.picked;
    g.picked.clear();
    auto const entry = rail::FromId(id);
    if (!entry) {
        log::Warn("omni-tool rail: the document picked '{}', which isn't one of the rail's entries", id);
        return;
    }
    ++g.picks;
    rail::EntryInfo const& info = rail::Info(*entry);
    if (*entry == rail::Entry::Close) {
        gOpen.store(false, std::memory_order_relaxed);
        if (screen) {
            main_menu::PutGameRequest(gm::Action::CloseScreens);
        }
        g.last = "close";
        log::Info("omni-tool rail: Close{}", screen ? ": the game's screens close" : "");
        return;
    }
    if (screen && rail::ForScreen(*screen) == entry) {
        g.last = std::format("{} (already up)", info.id);
        return;
    }
    gOpen.store(true, std::memory_order_relaxed);  // stays when the screen it opens goes again
    main_menu::PutGameRequest(info.action);
    g.last = info.id;
    log::Info("omni-tool rail: {} picked: {} asked of the mod", info.label, gm::Name(info.action));
}

} // namespace

XrCompositionLayerQuad const* Layer(Frame const& frame) {
    ReadSettings();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    float const dt = g.lastCounter.QuadPart != 0 ? std::clamp(static_cast<float>(Seconds(counter) - Seconds(g.lastCounter)), 0.0f, 0.1f) : 0.0f;
    g.lastCounter = counter;
    if (!g.settings.enabled || frame.othersOwnInput || frame.session == XR_NULL_HANDLE || frame.device == nullptr) {
        g.targetShown = false;
        Hide(!g.settings.enabled ? "[OmniTool] enabled = 0" : "a menu of ours or the calibration is up");
        return nullptr;
    }
    std::optional<wu::Screen> const screen = frame.screen ? std::optional<wu::Screen>(frame.screen->screen) : std::nullopt;
    // The tap on the left wrist: opens it, or closes it and the screen it stands by.
    vr::controllers::State const live = vr::controllers::Latest();
    bool const handsTracked = live.active && live.right.aimValid && frame.wristValid;
    // On Shepard's drawn arm when the mod sends it (rail::BodyTarget), else at the controllers.
    std::optional<XrVector3f> const bodyTarget =
        g.settings.tapOnBody && frame.body && frame.headValid ? rail::BodyTarget(*frame.body, frame.eyes) : std::nullopt;
    std::optional<XrVector3f> const bodyTip = bodyTarget ? rail::BodyFingertip(*frame.body) : std::nullopt;
    bool const onBody = bodyTarget && bodyTip;
    XrVector3f tip{};
    if (handsTracked && onBody) {
        tip = *bodyTip;
    } else if (handsTracked) {
        XrVector3f d = vr::menu_pointer::AimDirection(live.right.aim);
        float const n = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        tip = n > 1e-6f ? XrVector3f{live.right.aim.position.x + d.x / n * kTipAheadM, live.right.aim.position.y + d.y / n * kTipAheadM,
                              live.right.aim.position.z + d.z / n * kTipAheadM}
                        : live.right.aim.position;
    }
    // Not while the fingertip goes for the wrist screen or the rail (the wrist sits in front of their lower part, research/57
    // section 6).
    bool const goingForPanel = handsTracked && ((frame.screen && frame.screen->onWrist && rail::GoingForPanel(tip, frame.screen->panel)) ||
                                                   (g.lastPanel && rail::GoingForPanel(tip, *g.lastPanel)));
    if (goingForPanel) {
        ++g.tapsHeld;
    }
    bool const tapAllowed = g.settings.tap && !frame.gunDrawn && (frame.gameMode == 0 || frame.gameMode == 7) && !goingForPanel;
    char const* const refused = !g.settings.tap                                ? "[OmniTool] tap = 0"
                                : frame.gunDrawn                               ? "a weapon is drawn (holster it first)"
                                : frame.gameMode != 0 && frame.gameMode != 7   ? "not on foot"
                                : goingForPanel                                ? "the fingertip was going for a screen"
                                                                               : nullptr;
    // The tap aims at its target, a button just below the wrist readout (rail::TargetPoint).
    XrVector3f const target = onBody ? *bodyTarget : rail::TargetPoint(frame.wrist);
    if (onBody != g.onBody) {
        g.onBody = onBody;
        log::Info("omni-tool rail: the tap's target {}", onBody ? "is on Shepard's drawn forearm, the fingertip his" : "is at the controllers");
    }
    bool const visibleBefore = g.shown;
    bool const tapped = g.tap.Step(handsTracked, tip, target, tapAllowed, dt);
    StepTarget(frame, handsTracked, tip, target, tapAllowed && g.settings.tap, refused, tapped, dt);
    if (tapped) {
        ++g.taps;
        halcyon::RequestHaptic(0, 0.03f, 0.5f, 0.0f);
        halcyon::RequestHaptic(1, 0.03f, 0.5f, 0.0f);
        if (visibleBefore) {
            gOpen.store(false, std::memory_order_relaxed);
            if (screen) {
                main_menu::PutGameRequest(gm::Action::CloseScreens);
            }
            log::Info("omni-tool rail: the wrist tapped: closed{}", screen ? " with the game's screen" : "");
        } else {
            gOpen.store(true, std::memory_order_relaxed);
            log::Info("omni-tool rail: the wrist tapped: open");
        }
    }
    // Shown: opened by the player, or beside a screen of the rail's on the wrist (the game's own key or wheel opened it).
    bool const screenOnWrist = frame.screen && frame.screen->onWrist && rail::ForScreen(frame.screen->screen).has_value();
    bool const open = gOpen.load(std::memory_order_relaxed);
    if (!open && !screenOnWrist) {
        Hide(screen ? "the screen up isn't one of the rail's" : "closed");
        return nullptr;
    }
    std::optional<rail::Rail> at;
    char const* place = "beside the screen";
    if (frame.screen) {
        at = rail::Beside(frame.screen->panel);
    } else if (frame.wristValid && frame.headValid) {
        // At the omni-tool's display as Shepard's arm shows it (where the screens are projected from), else the controller's wrist.
        at = rail::Alone(frame.eyes, frame.display.value_or(frame.wrist), g.settings.aloneHeightM, 0.10f, 0.0f);
        place = frame.display ? "on the omni-tool's display" : "on the wrist";
    }
    if (!at) {
        Hide("nowhere to stand (no screen, the left controller not tracked)");
        return nullptr;
    }
    g.lastPanel = vr::world_ui::Panel{at->pose, at->widthM, at->heightM};  // the next frame's tap rule
    if (!Init(frame.device, frame.context) || !EnsureSwapchain(frame.session)) {
        return nullptr;
    }
    if (!g.shown) {
        log::Info("omni-tool rail: shown {} ({:.2f} x {:.2f} m){}", place, at->widthM, at->heightM, open ? "" : ": a screen of its own is up");
        g.shown = true;
        gShown.store(true, std::memory_order_relaxed);
        g.needRender = true;
    }
    // The document's state: the screen up, and whether it stands alone.
    Rml::String const active = screen && rail::ForScreen(*screen) ? rail::Info(*rail::ForScreen(*screen)).id : "";
    bool const alone = !frame.screen.has_value();
    if (active != g.active) {
        g.active = active;
        g.model.DirtyVariable("active");
        g.needRender = true;
    }
    if (alone != g.alone) {
        g.alone = alone;
        g.model.DirtyVariable("alone");
        g.needRender = true;
    }
    if (testloop::SnapshotArmed()) {
        g.needRender = true;  // the snapshot copies the image as it's released: draw one (it redraws only on change)
    }
    if (g.ui->ReloadIfChanged()) {
        g.needRender = true;
    }
    if (rail_art::CacheChanged()) {
        // The mod (game/GameArt) cached the movies, or they changed: the art again, and the document again so new fonts resolve.
        rail_art::Status const art = rail_art::Load(g.device.Get(), g.ui->GetRenderer(), {});
        g.art = art.fonts && art.tabs && art.panel;
        log::Info("omni-tool rail: the cache changed; {}", art.summary);
        g.ui->LoadDocument();
        g.model.DirtyVariable("art");
        g.needRender = true;
    }
    PointerStep(*at, dt);
    HandlePick(screen);
    if (g.needRender) {
        float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        g.context->ClearRenderTargetView(g.canvasRtv.Get(), clear);
        g.ui->Render(g.context.Get(), g.canvasRtv.Get());
        if (!Submit()) {
            return nullptr;
        }
        g.needRender = false;
    }
    g.quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    g.quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
    g.quad.space = frame.appSpace;
    g.quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    g.quad.subImage.swapchain = g.swapchain.handle;
    g.quad.subImage.imageRect = {{0, 0}, {kWidth, kHeight}};
    g.quad.pose = at->pose;
    g.quad.size = {at->widthM, at->heightM};
    Publish(true, place);
    return &g.quad;
}

std::atomic<std::uint64_t> gTargetCalls{0};
std::atomic<std::uint64_t> gTargetGiven{0};

XrCompositionLayerQuad const* TargetLayer() {
    gTargetCalls.fetch_add(1, std::memory_order_relaxed);
    if (!g.targetShown) {
        return nullptr;
    }
    gTargetGiven.fetch_add(1, std::memory_order_relaxed);
    return &g.targetQuad;
}

void Toggle(char const* why) {
    bool const open = !gOpen.load(std::memory_order_relaxed);
    gOpen.store(open, std::memory_order_relaxed);
    log::Info("omni-tool rail: {} ({})", open ? "opened" : "closed", why);
}

bool OwnsTriggers() {
    return gOwnsTriggers.load(std::memory_order_relaxed);
}

bool Shown() {
    return gShown.load(std::memory_order_relaxed);
}

std::string StatusJson() {
    std::lock_guard lock(gStatusMutex);
    TargetStatus const& t = gTargetStatus;
    return std::format("{{\"rail\": {}, \"target\": {{\"stepped\": {}, \"wristValid\": {}, \"headValid\": {}, \"handsTracked\": {}, "
                       "\"allowed\": {}, \"refused\": \"{}\", \"look\": \"{}\", \"shown\": {}, \"fingertipM\": {:.3f}, "
                       "\"belowEyesM\": {:.3f}, \"acrossM\": {:.3f}, \"at\": [{:.3f}, {:.3f}, {:.3f}], \"eyes\": [{:.3f}, {:.3f}, {:.3f}], "
                       "\"layerCalls\": {}, \"layerGiven\": {}, \"swapchain\": \"{:#x}\", \"onBody\": {}}}}}",
        gStatus, t.stepped, t.wristValid, t.headValid, t.handsTracked, t.allowed, t.refused != nullptr ? t.refused : "",
        LookName(static_cast<rail::TargetLook>(t.look)), t.shown, t.distanceM, t.belowEyesM, t.acrossM, t.target.x, t.target.y,
        t.target.z, t.eyes.x, t.eyes.y, t.eyes.z, gTargetCalls.load(std::memory_order_relaxed),
        gTargetGiven.load(std::memory_order_relaxed), reinterpret_cast<std::uintptr_t>(g.targetQuad.subImage.swapchain), g.onBody);
}

} // namespace mevr::host::omni_rail

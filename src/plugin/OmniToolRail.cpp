// SPDX-License-Identifier: MIT
// The omni-tool's rail in the host (plugin/OmniToolRail.hpp, research/56).
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
#include "halcyon/le1/mevr_bridge.h"
#include "plugin/MevrMenu.hpp"
#include "plugin/RailArt.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/TestLoop.hpp"

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
constexpr int kHeight = 1000;
constexpr std::int64_t kSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // our bytes are sRGB already
constexpr std::uint64_t kSettingsEveryMs = 2000;
constexpr float kTipAheadM = 0.035f;  // the index fingertip ahead of the aim pose (plugin/GamePointer's touchTipM)

// ---- Any thread ----
std::atomic<bool> gOpen{false};         // the player opened it (the tap, the main menu)
std::atomic<bool> gOwnsTriggers{false};
std::atomic<bool> gShown{false};         // g.shown for other threads (the mod's flags)
std::mutex gStatusMutex;
std::string gStatus = "{\"shown\": false}";

// ---- Render thread ----
struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;
    bool failed = false;
};

struct Settings {
    bool enabled = true;  // [OmniTool] enabled
    bool tap = true;      // [OmniTool] tap: the left wrist's tap opens it
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
    gp::Pointer pointer;
    rail::Tap tap;
    LARGE_INTEGER lastCounter{};
    Settings settings;
    std::uint64_t settingsMs = 0;
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
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"OmniTool", L"aloneHeightM", L"", text, 32, path.c_str());
    if (auto const v = ini::Number(ini::Utf8(std::filesystem::path(text)))) {
        s.aloneHeightM = std::clamp(static_cast<float>(*v), 0.10f, 0.60f);
    }
    static bool logged = false;
    if (!logged || s.enabled != g.settings.enabled || s.tap != g.settings.tap || s.aloneHeightM != g.settings.aloneHeightM) {
        logged = true;
        log::Info("omni-tool rail: [OmniTool] enabled = {}, tap = {} (tap the left wrist with the right index fingertip), aloneHeightM = {:.2f}",
            s.enabled ? 1 : 0, s.tap ? 1 : 0, s.aloneHeightM);
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
    // The game's own art (research/56 section 5), before the document loads so its fonts resolve; without it the rail keeps
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
    if (g.swapchain.failed) {
        return false;
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
    XrResult r = xrCreateSwapchain(session, &info, &g.swapchain.handle);
    std::uint32_t count = 0;
    if (XR_SUCCEEDED(r)) {
        r = xrEnumerateSwapchainImages(g.swapchain.handle, 0, &count, nullptr);
    }
    std::vector<XrSwapchainImageD3D11KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if (XR_SUCCEEDED(r) && count > 0) {
        r = xrEnumerateSwapchainImages(g.swapchain.handle, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    }
    if (XR_FAILED(r) || count == 0) {
        log::Error("omni-tool rail: swapchain {}x{} failed (XrResult {}); no rail", kWidth, kHeight, static_cast<int>(r));
        if (g.swapchain.handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(g.swapchain.handle);
        }
        g.swapchain = {};
        g.swapchain.failed = true;
        return false;
    }
    for (auto const& image : images) {
        g.swapchain.images.push_back(image.texture);
    }
    testloop::NameSwapchain(g.swapchain.handle, "omnirail");  // snapshots: <stamp>-omnirail.png, and in the eyes
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
        input.hands[i].aimValid = hand.aimValid;
        input.hands[i].aim = hand.aim;
        input.hands[i].trigger = hand.trigger;
        if (hand.aimValid) {
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
        Hide(!g.settings.enabled ? "[OmniTool] enabled = 0" : "a menu of ours or the calibration is up");
        return nullptr;
    }
    std::optional<wu::Screen> const screen = frame.screen ? std::optional<wu::Screen>(frame.screen->screen) : std::nullopt;
    // The tap on the left wrist: opens it, or closes it and the screen it stands by.
    vr::controllers::State const live = vr::controllers::Latest();
    bool const handsTracked = live.active && live.right.aimValid && frame.wristValid;
    XrVector3f tip{};
    if (handsTracked) {
        XrVector3f d = vr::menu_pointer::AimDirection(live.right.aim);
        float const n = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        tip = n > 1e-6f ? XrVector3f{live.right.aim.position.x + d.x / n * kTipAheadM, live.right.aim.position.y + d.y / n * kTipAheadM,
                              live.right.aim.position.z + d.z / n * kTipAheadM}
                        : live.right.aim.position;
    }
    bool const tapAllowed = g.settings.tap && !frame.gunDrawn && (frame.gameMode == 0 || frame.gameMode == 7);
    bool const visibleBefore = g.shown;
    if (g.tap.Step(handsTracked, tip, frame.wrist, tapAllowed, dt)) {
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
        at = rail::Alone(frame.eyes, frame.wrist, g.settings.aloneHeightM, 0.10f, 0.0f);
        place = "on the wrist";
    }
    if (!at) {
        Hide("nowhere to stand (no screen, the left controller not tracked)");
        return nullptr;
    }
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
    return gStatus;
}

} // namespace mevr::host::omni_rail

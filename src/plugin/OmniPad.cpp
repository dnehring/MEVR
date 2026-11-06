// SPDX-License-Identifier: MIT
// The omni-tool's d-pad in the host (plugin/OmniPad.hpp, research/53 section 6.3).
#include "plugin/OmniPad.hpp"

#include "plugin/GamePointer.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/MainMenu.hpp"
#include "plugin/MevrMenu.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/Swapchains.hpp"
#include "plugin/menu/MenuUi.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/GamePointer.hpp"
#include "core/vr/MenuPointer.hpp"
#include "core/vr/OmniPad.hpp"
#include "halcyon/le1/mevr_bridge.h"

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

namespace mevr::host::omni_pad {
namespace {

using Microsoft::WRL::ComPtr;
namespace pad = vr::omni_pad;
namespace gp = vr::game_pointer;
namespace wu = vr::world_ui;

constexpr int kSize = 480;  // the document's size (dp = pixels): a square canvas
constexpr std::int64_t kSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // our bytes are sRGB already
constexpr std::uint64_t kSettingsEveryMs = 2000;
constexpr float kTipAheadM = 0.035f;  // the index fingertip ahead of the aim pose (as the rail's)
constexpr std::uint64_t kGraceMs = 300;  // the screen gone this long (a hitch) before the pad goes and lets its arm go

// ---- Any thread ----
std::atomic<bool> gOwnsTriggers{false};
std::mutex gStatusMutex;
std::string gStatus = "{\"shown\": false}";

// ---- Render thread ----
struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;
    bool failed = false;  // refused at failedMs
    std::uint64_t failedMs = 0;
};

struct Settings {
    bool enabled = true;      // [OmniTool] pad
    float sizeM = pad::kSizeM;  // [OmniTool] padSizeM
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
    Rml::String held;        // the arm held ("" none): the document lights it
    Swapchain swapchain;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    bool needRender = true;
    bool shown = false;
    gp::Pointer pointer{[] {
        gp::Settings s;
        s.touchScroll = false;  // a press at once: no wait to tell a tap from a drag, and a slide stays a press (to the next arm)
        return s;
    }()};
    pad::Arm arm = pad::Arm::None;  // the arm whose press the game has
    int hand = 1;
    pad::Pad lastAt;                 // where it stood last frame, and when: a hitch that drops the screen for a frame keeps it
    std::uint64_t lastShownMs = 0;
    bool lastOnWrist = true;
    LARGE_INTEGER lastCounter{};
    Settings settings;
    std::uint64_t settingsMs = 0;
    int presses = 0;
    int changes = 0;
    int events = 0;
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
    case menu::LogLevel::Error: log::Error("omni-tool pad: {}", text); break;
    case menu::LogLevel::Warn: log::Warn("omni-tool pad: {}", text); break;
    case menu::LogLevel::Info: log::Info("omni-tool pad: {}", text); break;
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
    s.enabled = GetPrivateProfileIntW(L"OmniTool", L"pad", 1, path.c_str()) != 0;
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"OmniTool", L"padSizeM", L"", text, 32, path.c_str());
    if (auto const v = ini::Number(ini::Utf8(std::filesystem::path(text)))) {
        s.sizeM = std::clamp(static_cast<float>(*v), 0.08f, 0.40f);
    }
    static bool logged = false;
    if (!logged || s.enabled != g.settings.enabled || s.sizeM != g.settings.sizeM) {
        logged = true;
        log::Info("omni-tool pad: [OmniTool] pad = {} (the bypass puzzle's d-pad beside its screen), padSizeM = {:.2f}", s.enabled ? 1 : 0,
            s.sizeM);
    }
    g.settings = s;
}

bool Init(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (g.initTried) {
        return g.ready;
    }
    g.initTried = true;
    g.device = device;
    g.context = context;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kSize;
    td.Height = kSize;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&td, nullptr, g.canvas.GetAddressOf())) ||
        FAILED(device->CreateRenderTargetView(g.canvas.Get(), nullptr, g.canvasRtv.GetAddressOf()))) {
        log::Error("omni-tool pad: canvas {}x{} failed; no d-pad", kSize, kSize);
        return false;
    }
    g.ui = new menu::Ui();
    menu::UiSetup setup;
    setup.device = device;
    setup.documents = main_menu::AssetsFolder();
    setup.document = L"omnipad.rml";
    setup.fontDirs = {main_menu::FontsFolder()};
    setup.width = kSize;
    setup.height = kSize;
    setup.log = &Say;
    if (!g.ui->Init(setup)) {
        return false;
    }
    Rml::DataModelConstructor ctor = g.ui->Context()->CreateDataModel("omnipad");
    if (!ctor) {
        log::Error("omni-tool pad: its data model couldn't be made; no d-pad");
        return false;
    }
    ctor.Bind("held", &g.held);
    g.model = ctor.GetModelHandle();
    if (!g.ui->LoadDocument()) {
        log::Error("omni-tool pad: omnipad.rml didn't load from {} (deployed? see the rmlui: lines); no d-pad",
            ini::Utf8(main_menu::AssetsFolder()));
        return false;
    }
    g.ready = true;
    log::Info("omni-tool pad: ready ({}x{} canvas, {})", kSize, kSize, ini::Utf8(main_menu::AssetsFolder() / L"omnipad.rml"));
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
    info.width = kSize;
    info.height = kSize;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    // Freed for room (plugin/Swapchains) when no frame has shown it for a second; made and drawn again when next wanted.
    auto evict = [] {
        swapchains::Destroy(g.swapchain.handle);
        g.swapchain = {};
        g.needRender = true;
    };
    if (XR_FAILED(swapchains::Create(session, info, "omni-tool pad", evict, g.swapchain.handle, g.swapchain.images))) {
        g.swapchain = {};  // plugin/Swapchains logged it
        g.swapchain.failed = true;
        g.swapchain.failedMs = GetTickCount64();
        return false;
    }
    testloop::NameSwapchain(g.swapchain.handle, "omnipad");  // snapshots: <stamp>-omnipad.png, and in the eyes
    g.needRender = true;
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

void Publish(bool shown) {
    std::lock_guard lock(gStatusMutex);
    gStatus = std::format("{{\"shown\": {}, \"held\": \"{}\", \"presses\": {}, \"changes\": {}, \"events\": {}, \"last\": \"{}\", "
                          "\"pose\": [{:.3f}, {:.3f}, {:.3f}], \"sizeM\": {:.3f}}}",
        shown, pad::Name(g.arm), g.presses, g.changes, g.events, g.last, g.quad.pose.position.x, g.quad.pose.position.y,
        g.quad.pose.position.z, g.quad.size.width);
}

// The held arm becomes `to`: the game hears the old one let go and the new one pressed (its own d-pad events).
void Hold(pad::Arm to, int hand, char const* how) {
    if (to == g.arm) {
        return;
    }
    for (std::uint8_t const event : pad::Transition(g.arm, to)) {
        game_pointer::QueueGuiControl(event);
        ++g.events;
    }
    if (to != pad::Arm::None) {
        if (g.arm == pad::Arm::None) {
            ++g.presses;
        } else {
            ++g.changes;
        }
        halcyon::RequestHaptic(hand, 0.02f, 0.45f, 0.0f);
        g.last = std::format("{} ({})", pad::Name(to), how);
        log::Info("omni-tool pad: {} pressed ({}){}", pad::Name(to), how,
            g.arm != pad::Arm::None ? std::format(", slid from {}", pad::Name(g.arm)) : std::string());
    } else {
        halcyon::RequestHaptic(hand, 0.012f, 0.25f, 0.0f);
        log::Info("omni-tool pad: {} let go ({})", pad::Name(g.arm), how);
    }
    g.arm = to;
    g.held = pad::Name(to);
    g.model.DirtyVariable("held");
    g.needRender = true;
}

// The pointer: the controllers' rays and index fingertips on the pad. While a press is held, the arm under it is the one held.
void PointerStep(pad::Pad const& at, bool leftProjects, float dt) {
    vr::controllers::State const state = vr::controllers::Latest();
    gp::Input input;
    input.active = state.active;
    for (int i = 0; i < 2; ++i) {
        vr::controllers::Hand const& hand = i == 0 ? state.left : state.right;
        bool const usable = hand.aimValid && !(i == 0 && leftProjects);  // the hand the screen is projected from works nothing on it
        input.hands[i].aimValid = usable;
        input.hands[i].aim = hand.aim;
        input.hands[i].trigger = hand.trigger;
        if (usable) {
            XrVector3f d = vr::menu_pointer::AimDirection(hand.aim);
            float const n = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (n > 1e-6f) {
                input.hands[i].tipValid = true;
                input.hands[i].tip = {hand.aim.position.x + d.x / n * kTipAheadM, hand.aim.position.y + d.y / n * kTipAheadM,
                    hand.aim.position.z + d.z / n * kTipAheadM};
            }
        }
    }
    input.width = kSize;
    input.height = kSize;
    input.dt = dt;
    gp::Output const out = g.pointer.Step(gp::Quad{at.pose, at.sizeM, at.sizeM}, input);
    Rml::Context* const context = g.ui->Context();
    if (out.hit && out.moved) {
        context->ProcessMouseMove(out.x, out.y, 0);  // the hover's light
        g.needRender = true;
    }
    if (out.left) {
        context->ProcessMouseLeave();
        g.needRender = true;
    }
    float const size = static_cast<float>(kSize);
    pad::Arm const want = out.pressed ? pad::ArmAt(static_cast<float>(out.x), static_cast<float>(out.y), size, size) : pad::Arm::None;
    Hold(want, out.hand, out.touch ? "fingertip" : "trigger");
    gOwnsTriggers.store(out.hit, std::memory_order_relaxed);
}

void Hide(char const* why) {
    if (g.shown) {
        log::Info("omni-tool pad: hidden ({})", why);
        if (g.ready) {
            g.pointer.Release();
            g.ui->Context()->ProcessMouseLeave();
        }
    }
    if (g.arm != pad::Arm::None) {
        Hold(pad::Arm::None, g.hand, why);  // never leave the marker turning
    }
    g.shown = false;
    gOwnsTriggers.store(false, std::memory_order_relaxed);
    Publish(false);
}

} // namespace

XrCompositionLayerQuad const* Layer(Frame const& frame) {
    ReadSettings();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    float const dt = g.lastCounter.QuadPart != 0 ? std::clamp(static_cast<float>(Seconds(counter) - Seconds(g.lastCounter)), 0.0f, 0.1f) : 0.0f;
    g.lastCounter = counter;
    if (!g.settings.enabled || frame.othersOwnInput || frame.session == XR_NULL_HANDLE || frame.device == nullptr) {
        Hide(!g.settings.enabled ? "[OmniTool] pad = 0" : "a menu of ours or the calibration is up");
        return nullptr;
    }
    std::uint64_t const now = GetTickCount64();
    bool const playing = frame.screen && frame.screen->screen == wu::Screen::SkillGame && frame.screen->puzzle;
    if (!playing && !(g.shown && now - g.lastShownMs < kGraceMs)) {
        Hide(frame.screen && frame.screen->screen == wu::Screen::SkillGame ? "the puzzle isn't being played" : "no bypass puzzle up");
        return nullptr;
    }
    // A frame or two without the screen (a hitch: the mod's state went stale) keeps the pad where it was, and the arm held.
    pad::Pad const at = playing ? pad::Beside(frame.screen->panel, g.settings.sizeM) : g.lastAt;
    bool const onWrist = playing ? frame.screen->onWrist : g.lastOnWrist;
    if (playing) {
        g.lastAt = at;
        g.lastShownMs = now;
        g.lastOnWrist = onWrist;
    }
    if (!Init(frame.device, frame.context) || !EnsureSwapchain(frame.session)) {
        return nullptr;
    }
    if (!g.shown) {
        log::Info("omni-tool pad: shown to the right of the bypass puzzle's screen {} ({:.2f} m square)",
            onWrist ? "on the wrist" : "in the room", at.sizeM);
        g.shown = true;
        g.needRender = true;
    }
    if (testloop::SnapshotArmed()) {
        g.needRender = true;  // the snapshot copies the image as it's released: draw one (it redraws only on change)
    }
    if (g.ui->ReloadIfChanged()) {
        g.needRender = true;
    }
    PointerStep(at, onWrist, dt);
    g.hand = g.pointer.Hand();
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
    g.quad.subImage.imageRect = {{0, 0}, {kSize, kSize}};
    g.quad.pose = at.pose;
    g.quad.size = {at.sizeM, at.sizeM};
    Publish(true);
    return &g.quad;
}

bool OwnsTriggers() {
    return gOwnsTriggers.load(std::memory_order_relaxed);
}

std::string StatusJson() {
    std::lock_guard lock(gStatusMutex);
    return gStatus;
}

} // namespace mevr::host::omni_pad

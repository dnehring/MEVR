// SPDX-License-Identifier: MIT
// Thespian mode's panel in the host (plugin/Thespian.hpp, research/70 section 8 step 5).
#include "plugin/Thespian.hpp"

#include "plugin/MainMenu.hpp"
#include "plugin/Swapchains.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/menu/MenuUi.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"

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
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <vector>

namespace mevr::host::thespian {
namespace {

using Microsoft::WRL::ComPtr;
namespace th = vr::thespian;

constexpr int kWidth = th::kCanvasWidth;
constexpr int kHeight = th::kCanvasHeight;
constexpr std::int64_t kSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // our bytes are sRGB already
constexpr std::uint64_t kStaleMs = 1500;     // the mod's panel not renewed this long: gone (a hot reload, a level change; a
                                             // conversation's start stalls the game thread over half a second, seen 2026-11-13)
constexpr std::uint64_t kCommittedMs = 250;  // the taken row's flash stays this long after the mod's panel went
constexpr int kPanelMarginDp = 12;           // thespian.rcss: the panel's top and bottom margin

// ---- Any thread ----
std::mutex gPanelMutex;
th::Panel gPanel;              // the mod's last
std::uint64_t gPanelMs = 0;    // when it came
std::atomic<bool> gShown{false};
std::atomic<bool> gHidesWheel{false};
std::mutex gStatusMutex;
std::string gStatus = "{\"shown\": false}";

// ---- Render thread ----
struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;
    bool failed = false;
    std::uint64_t failedMs = 0;
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
    Rml::String speaker, heard, prompt, footerClass = "footer", levelWidth = "0%";
    std::string rows;        // the rows' markup now in the document
    Swapchain swapchain;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    bool needRender = true;
    bool shown = false;
    std::uint32_t placedSerial = 0;
    bool placed = false;
    XrPosef pose{};
    int heightPx = kHeight;
    th::Panel last;                 // the last panel drawn (kept through the committed flash)
    std::uint64_t holdUntilMs = 0;
    int panels = 0;
    int renders = 0;
};
State g;

void Say(menu::LogLevel level, std::string const& text) {
    switch (level) {
    case menu::LogLevel::Error: log::Error("thespian panel: {}", text); break;
    case menu::LogLevel::Warn: log::Warn("thespian panel: {}", text); break;
    case menu::LogLevel::Info: log::Info("thespian panel: {}", text); break;
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
        log::Error("thespian panel: canvas {}x{} failed; no panel", kWidth, kHeight);
        return false;
    }
    g.ui = new menu::Ui();
    menu::UiSetup setup;
    setup.device = device;
    setup.documents = main_menu::AssetsFolder();
    setup.document = L"thespian.rml";
    setup.fontDirs = {main_menu::FontsFolder()};
    setup.width = kWidth;
    setup.height = kHeight;
    setup.log = &Say;
    if (!g.ui->Init(setup)) {
        return false;
    }
    Rml::DataModelConstructor ctor = g.ui->Context()->CreateDataModel("thespian");
    if (!ctor) {
        log::Error("thespian panel: its data model couldn't be made; no panel");
        return false;
    }
    ctor.Bind("speaker", &g.speaker);
    ctor.Bind("heard", &g.heard);
    ctor.Bind("prompt", &g.prompt);
    ctor.Bind("footer_class", &g.footerClass);
    ctor.Bind("level_width", &g.levelWidth);
    g.model = ctor.GetModelHandle();
    if (!g.ui->LoadDocument()) {
        log::Error("thespian panel: thespian.rml didn't load from {} (deployed? see the rmlui: lines); no panel", ini::Utf8(main_menu::AssetsFolder()));
        return false;
    }
    g.ready = true;
    log::Info("thespian panel: ready ({}x{} canvas, {})", kWidth, kHeight, ini::Utf8(main_menu::AssetsFolder() / L"thespian.rml"));
    return true;
}

bool EnsureSwapchain(XrSession session) {
    if (g.swapchain.handle != XR_NULL_HANDLE) {
        return true;
    }
    if (g.swapchain.failed && !swapchains::RetryDue(g.swapchain.failedMs)) {
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
    auto evict = [] {
        swapchains::Destroy(g.swapchain.handle);
        g.swapchain = {};
        g.needRender = true;
    };
    if (XR_FAILED(swapchains::Create(session, info, "thespian panel", evict, g.swapchain.handle, g.swapchain.images))) {
        g.swapchain = {};
        g.swapchain.failed = true;
        g.swapchain.failedMs = GetTickCount64();
        return false;
    }
    testloop::NameSwapchain(g.swapchain.handle, "thespian");  // snapshots: <stamp>-thespian.png, and in the eyes
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
    gShown.store(shown, std::memory_order_relaxed);
    gHidesWheel.store(shown && g.last.gameWheel == 0, std::memory_order_relaxed);
    std::lock_guard lock(gStatusMutex);
    gStatus = std::format("{{\"shown\": {}, \"cards\": {}, \"mic\": \"{}\", \"anchored\": {}, \"pose\": [{:.3f}, {:.3f}, {:.3f}], \"sizeM\": [{:.3f}, {:.3f}], "
                          "\"panels\": {}, \"renders\": {}}}",
        shown, g.last.cardCount, th::MicClass(static_cast<th::Mic>(g.last.mic)), g.last.anchored != 0, g.pose.position.x, g.pose.position.y, g.pose.position.z,
        g.quad.size.width, g.quad.size.height, g.panels, g.renders);
}

void Hide(char const* why) {
    if (g.shown) {
        log::Info("thespian panel: hidden ({})", why);
    }
    g.shown = false;
    g.placed = false;
    Publish(false);
}

// The document brought up to the panel. True when anything it shows changed.
bool Fill(th::Panel const& panel) {
    bool changed = false;
    auto set = [&](Rml::String& slot, std::string const& value, char const* name) {
        if (slot != value) {
            slot = value;
            g.model.DirtyVariable(name);
            changed = true;
        }
    };
    set(g.speaker, std::string(panel.speaker, strnlen(panel.speaker, sizeof panel.speaker)), "speaker");
    set(g.heard, std::string(panel.heard, strnlen(panel.heard, sizeof panel.heard)), "heard");
    set(g.prompt, th::PromptText(panel), "prompt");
    set(g.footerClass, std::string("footer ") + th::MicClass(static_cast<th::Mic>(panel.mic)), "footer_class");
    // The level in steps of 4%: the meter moves without a redraw every frame.
    int const percent = static_cast<int>(std::clamp(panel.level * 6.0f, 0.0f, 1.0f) * 25.0f) * 4;
    set(g.levelWidth, std::to_string(percent) + "%", "level_width");
    std::string rows = th::RowsRml(panel);
    if (rows != g.rows) {
        if (Rml::Element* const element = g.ui->Document() != nullptr ? g.ui->Document()->GetElementById("rows") : nullptr) {
            element->SetInnerRML(rows);
        }
        g.rows = std::move(rows);
        changed = true;
    }
    return changed;
}

}  // namespace

void SetPanel(th::Panel const* panel) {
    if (panel == nullptr || panel->size != sizeof(th::Panel)) {
        return;
    }
    std::lock_guard lock(gPanelMutex);
    gPanel = *panel;
    gPanelMs = GetTickCount64();
}

XrCompositionLayerQuad const* Layer(Frame const& frame) {
    th::Panel panel;
    std::uint64_t at = 0;
    {
        std::lock_guard lock(gPanelMutex);
        panel = gPanel;
        at = gPanelMs;
    }
    std::uint64_t const now = GetTickCount64();
    bool live = panel.shown != 0 && now - at < kStaleMs;
    if (live) {
        // A committed row holds the panel a moment after the mod's is gone: the taken line's flash.
        for (std::size_t i = 0; i < std::min<std::size_t>(panel.cardCount, th::kMaxCards); ++i) {
            if (static_cast<th::Look>(panel.cards[i].look) == th::Look::Committed) {
                g.holdUntilMs = now + kCommittedMs;
            }
        }
    } else if (g.shown && now < g.holdUntilMs) {
        panel = g.last;
        live = true;
    }
    if (!live || frame.othersOwnInput || frame.session == XR_NULL_HANDLE || frame.device == nullptr) {
        Hide(!live ? "the turn is over" : "a menu of ours or the calibration is up");
        return nullptr;
    }
    if (!Init(frame.device, frame.context) || !EnsureSwapchain(frame.session)) {
        return nullptr;
    }
    if (g.ui->ReloadIfChanged()) {
        g.rows.clear();  // the new document's rows are empty
        g.needRender = true;
    }
    if (Fill(panel)) {
        g.needRender = true;
    }
    if (testloop::SnapshotArmed()) {
        g.needRender = true;
    }
    if (g.needRender) {
        float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        g.context->ClearRenderTargetView(g.canvasRtv.Get(), clear);
        g.ui->Render(g.context.Get(), g.canvasRtv.Get());
        if (!Submit()) {
            return nullptr;
        }
        g.needRender = false;
        ++g.renders;
        // The quad shows the canvas down to the panel's lower edge.
        Rml::Element* const element = g.ui->Document() != nullptr ? g.ui->Document()->GetElementById("panel") : nullptr;
        int const tall = element != nullptr ? static_cast<int>(std::ceil(element->GetOffsetHeight())) + 2 * kPanelMarginDp : kHeight;
        g.heightPx = std::clamp(tall, 120, kHeight);
    }
    float const heightM = th::kPanelWidthM * static_cast<float>(g.heightPx) / static_cast<float>(kWidth);
    if (!g.placed || g.placedSerial != panel.serial) {
        // Once per wheel: under the line of sight to the speaker (or ahead of the head), where it stays while the player reads.
        XrQuaternionf const& q = frame.head.orientation;
        XrVector3f const forward{-2.0f * (q.x * q.z + q.w * q.y), 0.0f, -(1.0f - 2.0f * (q.x * q.x + q.y * q.y))};
        XrVector3f const speaker{panel.speakerHead[0], panel.speakerHead[1], panel.speakerHead[2]};
        g.pose = th::PlacePanel(frame.head.position, forward, panel.anchored != 0, speaker, heightM);
        g.placed = true;
        g.placedSerial = panel.serial;
        ++g.panels;
        log::Info("thespian panel: shown at ({:.2f}, {:.2f}, {:.2f}) m, {:.2f} x {:.2f} m, {} rows, {}", g.pose.position.x, g.pose.position.y, g.pose.position.z,
            th::kPanelWidthM, heightM, panel.cardCount,
            panel.anchored != 0 ? std::format("under the speaker's head at ({:.2f}, {:.2f}, {:.2f})", speaker.x, speaker.y, speaker.z) : std::string("ahead of the head (no speaker in the room)"));
    }
    g.shown = true;
    g.last = panel;
    g.quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    g.quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
    g.quad.space = frame.appSpace;
    g.quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    g.quad.subImage.swapchain = g.swapchain.handle;
    g.quad.subImage.imageRect = {{0, 0}, {kWidth, g.heightPx}};
    g.quad.pose = g.pose;
    g.quad.size = {th::kPanelWidthM, heightM};
    Publish(true);
    return &g.quad;
}

bool Shown() {
    return gShown.load(std::memory_order_relaxed);
}

bool HidesGameWheel() {
    return gHidesWheel.load(std::memory_order_relaxed);
}

std::string StatusJson() {
    std::lock_guard lock(gStatusMutex);
    return gStatus;
}

}  // namespace mevr::host::thespian

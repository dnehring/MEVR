// Our VR main menu in the host (plugin/MainMenu.hpp, research/28): open and close, its quad, the laser pointer and the
// pad as RmlUi's mouse and keys, the frosted backdrop from the left eye's image, and the cost while open.

#include "plugin/MainMenu.hpp"

#include "plugin/MainMenuSettings.hpp"
#include "plugin/menu/MenuBackdrop.hpp"
#include "plugin/menu/MenuModel.hpp"
#include "plugin/menu/MenuRenderer.hpp"
#include "plugin/menu/MenuStrip.hpp"
#include "plugin/menu/MenuUi.hpp"

#include "halcyon/le1/vr_menu.h"

#include "core/Log.hpp"
#include "core/vr/MenuPointer.hpp"
#include "core/vr/TestRequest.hpp"
#include "plugin/Calibration.hpp"
#include "plugin/Colour.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/MevrMenu.hpp"
#include "plugin/PerfProbe.hpp"
#include "plugin/TestLoop.hpp"
#include "mod/ModApi.hpp"

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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace mevr::host::main_menu {
namespace {

using Microsoft::WRL::ComPtr;
namespace pointer = vr::menu_pointer;

constexpr int kWidth = 1440;   // the document's size (dp = pixels)
constexpr int kHeight = 900;
constexpr std::int64_t kSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // our bytes are sRGB already
constexpr std::uint64_t kBackdropEveryMs = 100;   // the world behind the glass, ten times a second
constexpr std::uint64_t kRefreshEveryMs = 500;    // the settings re-read while open
constexpr float kTriggerOn = 0.6f;
constexpr float kTriggerOff = 0.4f;
constexpr float kStickNav = 0.6f;
constexpr float kStickDeadZone = 0.2f;
constexpr float kWheelPerSecond = 10.0f;  // RmlUi wheel steps per second at full deflection
constexpr float kSmoothing = 0.5f;
constexpr float kDeadBandPx = 1.5f;

// ---- Any thread -----------------------------------------------------------------------------------------------
std::atomic<bool> gOpen{false};
std::atomic<bool> gPauseWanted{false};
std::mutex gPadMutex;
XINPUT_GAMEPAD gPad{};
std::uint64_t gPadMs = 0;

// ---- Present thread -------------------------------------------------------------------------------------------
struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;  // the runtime's
    bool failed = false;
};

struct State {
    bool initTried = false;
    bool ready = false;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> canvas;
    ComPtr<ID3D11RenderTargetView> canvasRtv;
    // Made once and kept for the process (a Ui is never destroyed: the process's exit mustn't touch D3D).
    menu::Ui* ui = nullptr;
    menu::Model* model = nullptr;
    Settings* settings = nullptr;
    menu::Backdrop* backdrop = nullptr;
    menu::StripPair* strips = nullptr;
    Swapchain swapchain;
    XrSession session = XR_NULL_HANDLE;
    bool submitted = false;   // the swapchain holds a picture of the open menu
    // Where it is: fixed while one opening lasts.
    bool placed = false;
    XrPosef pose{};
    float width = 1.45f;
    float height = 1.45f * kHeight / kWidth;
    // The eye image the backdrop is made from: copied as it's released.
    ComPtr<ID3D11Texture2D> eyeCopy;
    ComPtr<ID3D11ShaderResourceView> eyeSrv;
    D3D11_TEXTURE2D_DESC eyeDesc{};
    bool eyeFresh = false;      // copied this frame
    XrSwapchain tapSwapchain = XR_NULL_HANDLE;
    std::uint64_t lastBackdropMs = 0;
    bool backdropShown = false;
    // Input.
    vr::controllers::State live;
    int hand = 1;
    bool trigger[2] = {false, false};
    bool pressing = false;
    bool hit = false;
    bool wasHit = false;
    float x = 0.0f, y = 0.0f, smoothX = 0.0f, smoothY = 0.0f;
    Rml::Element* hoverControl = nullptr;
    Rml::Element* dotElement = nullptr;  // the document's #pointer (a reloaded document has a new one)
    bool dotShown = false;
    float dotX = -1.0f, dotY = -1.0f;
    bool aHeld = false, bHeld = false;
    int navDir = -1;                 // the direction held (Rml key) for repeats
    std::uint64_t navNextMs = 0;
    std::uint64_t lastRefreshMs = 0;
    std::uint64_t lastFrameMs = 0;
    bool needRender = true;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    // Cost while open.
    double cpuMs = 0.0;
    int frames = 0;
    int renders = 0;
    int backdrops = 0;
    std::uint64_t statsSinceMs = 0;
    ComPtr<ID3D11Query> disjoint, gpuBegin, gpuEnd;
    bool gpuPending = false;
    double gpuMs = 0.0;
    int gpuSamples = 0;
    double lastCpuMs = -1.0, lastGpuMs = -1.0, lastRendersPerSecond = 0.0;
};
State g;
std::atomic<bool> gRecentered{false};

void Say(menu::LogLevel level, std::string const& text) {
    switch (level) {
    case menu::LogLevel::Error: log::Error("{}", text); break;
    case menu::LogLevel::Warn: log::Warn("{}", text); break;
    case menu::LogLevel::Info: log::Info("{}", text); break;
    }
}

bool Init(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (g.initTried) {
        return g.ready;
    }
    g.initTried = true;
    auto const start = std::chrono::steady_clock::now();
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
        log::Error("main menu: canvas {}x{} failed; no menu", kWidth, kHeight);
        return false;
    }
    g.settings = new Settings();
    g.backdrop = new menu::Backdrop();
    if (!g.backdrop->Init(device, context)) {
        log::Error("main menu: the backdrop's shaders failed; no menu");
        return false;
    }
    g.strips = new menu::StripPair();
    if (!g.strips->Init(device)) {
        log::Warn("main menu: the colour preview strip failed (the Display page shows none)");
    }
    g.ui = new menu::Ui();
    menu::UiSetup setup;
    setup.device = device;
    setup.documents = g.settings->Assets();
    setup.fontDirs = {g.settings->Fonts()};
    setup.width = kWidth;
    setup.height = kHeight;
    setup.log = &Say;
    if (!g.ui->Init(setup)) {
        return false;
    }
    menu::Renderer* const renderer = g.ui->GetRenderer();
    renderer->SetExternalTexture("backdrop", g.backdrop->View(), static_cast<int>(menu::kBackdropWidth),
        static_cast<int>(menu::kBackdropHeight));
    renderer->SetExternalTexture("strip_original", g.strips->Original(), menu::kStripWidth, menu::kStripHeight);
    renderer->SetExternalTexture("strip_adjusted", g.strips->Adjusted(), menu::kStripWidth, menu::kStripHeight);
    g.model = new menu::Model();
    if (!g.model->Bind(g.ui->Context(), g.settings) || !g.ui->LoadDocument()) {
        log::Error("main menu: the document didn't load from {} (deployed? see the rmlui: lines); no menu",
            g.settings->Assets().string());
        return false;
    }
    D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    device->CreateQuery(&qd, g.disjoint.GetAddressOf());
    qd.Query = D3D11_QUERY_TIMESTAMP;
    device->CreateQuery(&qd, g.gpuBegin.GetAddressOf());
    device->CreateQuery(&qd, g.gpuEnd.GetAddressOf());
    g.ready = true;
    log::Info("main menu: ready in {:.0f} ms ({}x{} canvas, documents {}, fonts {})",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), kWidth, kHeight,
        g.settings->Assets().string(), g.settings->Fonts().string());
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
        log::Error("main menu: swapchain {}x{} failed (XrResult {}); no menu in the headset", kWidth, kHeight, static_cast<int>(r));
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
    testloop::NameSwapchain(g.swapchain.handle, "mainmenu");  // snapshots: <stamp>-mainmenu.png, and in the eyes
    log::Info("main menu: swapchain {}x{}, {} images", kWidth, kHeight, count);
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

// In front of the head (its heading only, level), a little below the eyes, facing them.
void Place(Frame const& frame) {
    g.width = g.settings->WidthM();
    g.height = g.width * static_cast<float>(kHeight) / static_cast<float>(kWidth);
    float const distance = g.settings->DistanceM();
    XrVector3f head{0.0f, 0.0f, 0.0f};
    float fx = 0.0f, fz = -1.0f;
    if (frame.headValid) {
        XrQuaternionf const& q = frame.head.orientation;
        head = frame.head.position;
        fx = -2.0f * (q.x * q.z + q.w * q.y);
        fz = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        float const length = std::sqrt(fx * fx + fz * fz);
        if (length > 1e-3f) {
            fx /= length;
            fz /= length;
        } else {
            fx = 0.0f;
            fz = -1.0f;
        }
    }
    float const yaw = std::atan2(-fx, -fz);
    g.pose.orientation = {0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
    g.pose.position = {head.x + fx * distance, head.y - 0.10f, head.z + fz * distance};
    g.placed = true;
    log::Info("main menu: placed {:.2f} m ahead at heading {:.0f} deg, {:.2f} x {:.2f} m, at ({:.2f}, {:.2f}, {:.2f}){}", distance,
        yaw * 57.29578f, g.width, g.height, g.pose.position.x, g.pose.position.y, g.pose.position.z,
        frame.headValid ? "" : " (no head yet)");
}

// testloop's release tap: the left eye's image, right before Halcyon hands it to the runtime.
void OnEyeReleased(XrSwapchain, ID3D11Texture2D* image) {
    D3D11_TEXTURE2D_DESC desc{};
    image->GetDesc(&desc);
    DXGI_FORMAT family = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT view = DXGI_FORMAT_UNKNOWN;
    switch (desc.Format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        family = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        view = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        break;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        family = DXGI_FORMAT_B8G8R8A8_TYPELESS;
        view = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        break;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        family = DXGI_FORMAT_R16G16B16A16_TYPELESS;
        view = DXGI_FORMAT_R16G16B16A16_FLOAT;
        break;
    default:
        return;  // the plain dark ground stays
    }
    if (desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || g.device == nullptr) {
        return;
    }
    if (g.eyeCopy == nullptr || g.eyeDesc.Width != desc.Width || g.eyeDesc.Height != desc.Height || g.eyeDesc.Format != family) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = desc.Width;
        td.Height = desc.Height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = family;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = view;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        g.eyeCopy.Reset();
        g.eyeSrv.Reset();
        if (FAILED(g.device->CreateTexture2D(&td, nullptr, g.eyeCopy.GetAddressOf())) ||
            FAILED(g.device->CreateShaderResourceView(g.eyeCopy.Get(), &sd, g.eyeSrv.GetAddressOf()))) {
            g.eyeCopy.Reset();
            g.eyeSrv.Reset();
            return;
        }
        g.eyeDesc = td;
        log::Info("main menu: the backdrop samples the left eye's image ({}x{}, format {})", desc.Width, desc.Height,
            static_cast<int>(desc.Format));
    }
    g.context->CopyResource(g.eyeCopy.Get(), image);
    g.eyeFresh = true;
}

// The backdrop: a fresh eye image this frame is projected onto the panel; a new tap is set up now and then.
void UpdateBackdrop(Frame const& frame, std::uint64_t now) {
    if (g.eyeFresh && g.eyeSrv != nullptr && frame.projectionValid && frame.projectionRect.extent.width > 0) {
        g.eyeFresh = false;
        int const rect[4] = {frame.projectionRect.offset.x, frame.projectionRect.offset.y, frame.projectionRect.extent.width,
            frame.projectionRect.extent.height};
        if (g.backdrop->Capture(g.context.Get(), g.eyeSrv.Get(), rect, g.eyeDesc.Width, g.eyeDesc.Height, true)) {
            // The panel in that eye's view space (the pose the image was rendered for).
            XrQuaternionf const e = frame.projectionPose.orientation;
            XrQuaternionf const inv{-e.x, -e.y, -e.z, e.w};
            auto rotate = [](XrQuaternionf const& q, XrVector3f const& v) {
                XrVector3f const u{q.x, q.y, q.z};
                XrVector3f const t{2.0f * (u.y * v.z - u.z * v.y), 2.0f * (u.z * v.x - u.x * v.z), 2.0f * (u.x * v.y - u.y * v.x)};
                return XrVector3f{v.x + q.w * t.x + (u.y * t.z - u.z * t.y), v.y + q.w * t.y + (u.z * t.x - u.x * t.z),
                    v.z + q.w * t.z + (u.x * t.y - u.y * t.x)};
            };
            XrVector3f const rel{g.pose.position.x - frame.projectionPose.position.x, g.pose.position.y - frame.projectionPose.position.y,
                g.pose.position.z - frame.projectionPose.position.z};
            XrVector3f const center = rotate(inv, rel);
            XrVector3f const right = rotate(inv, rotate(g.pose.orientation, {g.width * 0.5f, 0.0f, 0.0f}));
            XrVector3f const up = rotate(inv, rotate(g.pose.orientation, {0.0f, g.height * 0.5f, 0.0f}));
            menu::BackdropView view;
            view.center[0] = center.x;
            view.center[1] = center.y;
            view.center[2] = center.z;
            view.right[0] = right.x;
            view.right[1] = right.y;
            view.right[2] = right.z;
            view.up[0] = up.x;
            view.up[1] = up.y;
            view.up[2] = up.z;
            view.tanLeft = std::tan(frame.projectionFov.angleLeft);
            view.tanRight = std::tan(frame.projectionFov.angleRight);
            view.tanUp = std::tan(frame.projectionFov.angleUp);
            view.tanDown = std::tan(frame.projectionFov.angleDown);
            if (g.backdrop->Compose(g.context.Get(), view)) {
                g.needRender = true;
                g.backdropShown = true;
                ++g.backdrops;
            }
        }
    }
    if (g.tapSwapchain != XR_NULL_HANDLE) {
        testloop::SetReleaseTap(XR_NULL_HANDLE, nullptr);  // one copy per request
        g.tapSwapchain = XR_NULL_HANDLE;
    }
    if (!frame.worldFrame || !frame.projectionValid || frame.projectionSwapchain == XR_NULL_HANDLE) {
        if (g.backdropShown) {
            g.backdrop->Clear(g.context.Get());  // a flat screen or a loading screen: no world behind the glass
            g.backdropShown = false;
            g.needRender = true;
        }
        return;
    }
    if (now - g.lastBackdropMs >= kBackdropEveryMs) {
        g.lastBackdropMs = now;
        g.tapSwapchain = frame.projectionSwapchain;
        testloop::SetReleaseTap(frame.projectionSwapchain, &OnEyeReleased);  // the next frame's left eye
    }
}

Rml::Element* Control(Rml::Element* e) {
    for (; e != nullptr; e = e->GetParentNode()) {
        Rml::String const& tag = e->GetTagName();
        if (tag == "button" || tag == "input") {
            return e;
        }
        if (tag == "body") {
            break;
        }
    }
    return nullptr;
}

void Haptic(int hand, float seconds, float amplitude) {
    halcyon::RequestHaptic(hand, seconds, amplitude, 0.0f);
}

bool Latch(bool& latched, float value) {
    latched = latched ? value > kTriggerOff : value >= kTriggerOn;
    return latched;
}

// The aim rays on the quad: RmlUi's mouse (move, the trigger's button, the right stick's wheel).
void Pointer(Rml::Context* context, float dt) {
    vr::controllers::State const& s = g.live;
    pointer::Quad quad{g.pose, g.width, g.height};
    vr::controllers::Hand const* hands[2] = {&s.left, &s.right};
    bool triggers[2];
    pointer::Hit hits[2];
    bool on[2];
    for (int i = 0; i < 2; ++i) {
        triggers[i] = Latch(g.trigger[i], s.active ? hands[i]->trigger : 0.0f);
        on[i] = s.active && hands[i]->aimValid &&
            pointer::Intersect(quad, hands[i]->aim.position, pointer::AimDirection(hands[i]->aim), hits[i]);
    }
    int const other = 1 - g.hand;
    if (!g.pressing && on[other] && (!on[g.hand] || (triggers[other] && !triggers[g.hand]))) {
        g.hand = other;
    }
    bool const pointing = on[g.hand];
    bool const wasHit = g.wasHit;
    if (pointing) {
        float const px = std::clamp(hits[g.hand].u, 0.0f, 1.0f) * kWidth;
        float const py = std::clamp(hits[g.hand].v, 0.0f, 1.0f) * kHeight;
        g.smoothX = wasHit ? g.smoothX + (px - g.smoothX) * kSmoothing : px;
        g.smoothY = wasHit ? g.smoothY + (py - g.smoothY) * kSmoothing : py;
        if (!wasHit || std::fabs(g.smoothX - g.x) > kDeadBandPx || std::fabs(g.smoothY - g.y) > kDeadBandPx) {
            g.x = g.smoothX;
            g.y = g.smoothY;
            context->ProcessMouseMove(static_cast<int>(std::lround(g.x)), static_cast<int>(std::lround(g.y)), 0);
            g.needRender = true;
        }
    }
    bool const press = g.pressing ? triggers[g.hand] : pointing && triggers[g.hand];
    if (press != g.pressing) {
        g.pressing = press;
        if (press) {
            context->ProcessMouseButtonDown(0, 0);
            if (Control(context->GetHoverElement()) != nullptr) {
                Haptic(g.hand, 0.02f, 0.45f);  // the click
            }
        } else {
            context->ProcessMouseButtonUp(0, 0);
        }
        g.needRender = true;
    }
    g.hit = pointing || g.pressing;
    if (!g.hit && wasHit) {
        context->ProcessMouseLeave();
        g.needRender = true;
    }
    g.wasHit = g.hit;
    // A tick on each new control under the pointer.
    Rml::Element* const control = g.hit ? Control(context->GetHoverElement()) : nullptr;
    if (control != g.hoverControl) {
        g.hoverControl = control;
        if (control != nullptr) {
            Haptic(g.hand, 0.012f, 0.2f);
        }
    }
    float const stick = s.active ? s.right.stickY : 0.0f;
    if (g.hit && std::fabs(stick) > kStickDeadZone && dt > 0.0f) {
        context->ProcessMouseWheel(-stick * kWheelPerSecond * std::min(dt, 0.1f), 0);
        g.needRender = true;
    }
    // The dot, moved only when it moves.
    if (Rml::ElementDocument* const doc = g.ui->Document()) {
        if (Rml::Element* const dot = doc->GetElementById("pointer")) {
            if (g.hit != g.dotShown || dot != g.dotElement) {
                dot->SetProperty(Rml::PropertyId::Display, Rml::Property(g.hit ? Rml::Style::Display::Block : Rml::Style::Display::None));
                g.dotShown = g.hit;
                g.dotElement = dot;
            }
            if (g.hit && (g.x != g.dotX || g.y != g.dotY)) {
                dot->SetProperty(Rml::PropertyId::Left, Rml::Property(g.x, Rml::Unit::DP));
                dot->SetProperty(Rml::PropertyId::Top, Rml::Property(g.y, Rml::Unit::DP));
                g.dotX = g.x;
                g.dotY = g.y;
            }
            dot->SetClass("pressing", g.pressing);
        }
    }
}

// The pad: the d-pad or the left stick moves the focus (repeating while held), A activates, B goes back or closes.
// Touch controllers count as a pad here (the right A and B, the left stick).
void Pad(Rml::Context* context, std::uint64_t now) {
    XINPUT_GAMEPAD pad{};
    {
        std::lock_guard lock(gPadMutex);
        if (now - gPadMs < 250) {
            pad = gPad;
        }
    }
    vr::controllers::State const& s = g.live;
    auto larger = [](float a, float b) { return std::fabs(a) >= std::fabs(b) ? a : b; };
    float const lx = larger(s.active ? s.left.stickX : 0.0f, static_cast<float>(pad.sThumbLX) / 32767.0f);
    float const ly = larger(s.active ? s.left.stickY : 0.0f, static_cast<float>(pad.sThumbLY) / 32767.0f);
    int dir = -1;
    if ((pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0 || ly > kStickNav) {
        dir = Rml::Input::KI_UP;
    } else if ((pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0 || ly < -kStickNav) {
        dir = Rml::Input::KI_DOWN;
    } else if ((pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0 || lx < -kStickNav) {
        dir = Rml::Input::KI_LEFT;
    } else if ((pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0 || lx > kStickNav) {
        dir = Rml::Input::KI_RIGHT;
    }
    if (dir != g.navDir) {
        g.navDir = dir;
        g.navNextMs = now;
    }
    if (dir >= 0 && now >= g.navNextMs) {
        g.navNextMs = now + (g.navNextMs == now ? 400 : 140);
        if (context->GetFocusElement() == nullptr || context->GetFocusElement() == g.ui->Document()) {
            // The first move focuses the open section's button in the rail.
            if (Rml::ElementDocument* doc = g.ui->Document()) {
                if (Rml::Element* first = doc->QuerySelector(".navb.on")) {
                    first->Focus(true);
                }
            }
        } else {
            context->ProcessKeyDown(static_cast<Rml::Input::KeyIdentifier>(dir), 0);
            context->ProcessKeyUp(static_cast<Rml::Input::KeyIdentifier>(dir), 0);
        }
        Haptic(1, 0.01f, 0.15f);
        g.needRender = true;
    }
    bool const a = (pad.wButtons & XINPUT_GAMEPAD_A) != 0 || (s.active && s.right.primary);
    bool const b = (pad.wButtons & XINPUT_GAMEPAD_B) != 0 || (s.active && s.right.secondary);
    // On the Display page, holding A shows the uncorrected picture.
    bool const display = g.settings->OnDisplayPage(g.model->Get());
    if (a && display) {
        colour::HoldCompare();
    }
    g.model->SetNumber("comparing", a && display ? 1.0f : 0.0f);
    if (a && !g.aHeld) {
        Rml::Element* const focus = context->GetFocusElement();
        if (focus != nullptr && focus != g.ui->Document() && Control(focus) != nullptr) {
            focus->Click();
            Haptic(1, 0.02f, 0.45f);
        }
        g.needRender = true;
    }
    if (b && !g.bHeld) {
        if (g.settings->CancelPending()) {
            g.model->DirtyAll();  // B first takes back an armed Reset or a waiting measurement
            g.needRender = true;
            g.aHeld = a;
            g.bHeld = b;
            return;
        }
        Rml::Element* const focus = context->GetFocusElement();
        bool const inContent = focus != nullptr && focus != g.ui->Document() && Control(focus) != nullptr &&
            focus->Closest(".content") != nullptr;
        if (inContent) {
            // Back: from a row to the section's button in the rail.
            if (Rml::ElementDocument* doc = g.ui->Document()) {
                if (Rml::Element* rail = doc->QuerySelector(".navb.on")) {
                    rail->Focus(true);
                }
            }
            g.needRender = true;
        } else {
            SetOpen(false, "B");
        }
    }
    g.aHeld = a;
    g.bHeld = b;
}

void Stats(std::uint64_t now, double cpuMs) {
    g.cpuMs += cpuMs;
    ++g.frames;
    if (g.gpuPending) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        UINT64 t0 = 0, t1 = 0;
        if (g.context->GetData(g.disjoint.Get(), &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            g.context->GetData(g.gpuBegin.Get(), &t0, sizeof t0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            g.context->GetData(g.gpuEnd.Get(), &t1, sizeof t1, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
            g.gpuPending = false;
            if (!dj.Disjoint && dj.Frequency > 0 && t1 >= t0) {
                g.gpuMs += static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(dj.Frequency);
                ++g.gpuSamples;
            }
        }
    }
    if (g.statsSinceMs == 0) {
        g.statsSinceMs = now;
    }
    if (now - g.statsSinceMs >= 10000 && g.frames > 0) {
        double const seconds = static_cast<double>(now - g.statsSinceMs) / 1000.0;
        g.lastCpuMs = g.cpuMs / g.frames;
        g.lastGpuMs = g.gpuSamples > 0 ? g.gpuMs / g.gpuSamples : -1.0;
        g.lastRendersPerSecond = g.renders / seconds;
        log::Info("main menu: open {:.0f} s: {:.3f} ms CPU per frame, {} GPU per frame, {:.1f} renders/s, {:.1f} backdrops/s",
            seconds, g.lastCpuMs, g.lastGpuMs >= 0.0 ? std::format("{:.3f} ms", g.lastGpuMs) : std::string("? ms"),
            g.lastRendersPerSecond, g.backdrops / seconds);
        g.cpuMs = 0.0;
        g.frames = g.renders = g.backdrops = g.gpuSamples = 0;
        g.gpuMs = 0.0;
        g.statsSinceMs = now;
    }
}

void AfterCommand(After after) {
    switch (after) {
    case After::Nothing: return;
    case After::Close: SetOpen(false, "Resume"); return;
    case After::Calibrate:
        SetOpen(false, "Start calibration");
        calibration::Start("the main menu");
        return;
    case After::Classic:
        SetOpen(false, "the classic renderer menu");
        MELEVR::Menu::SetOpen(true);
        return;
    case After::Align:
        SetOpen(false, "Align");
        MELEVR::MevrBridge::StartCameraAlign("the main menu");
        return;
    case After::Snapshot: {
        SetOpen(false, "Snapshot");
        testloop::SnapshotRequest request;
        request.delaySeconds = 1.0;
        request.trigger = "main menu";
        testloop::RequestSnapshot(request);
        return;
    }
    case After::Perf:
        SetOpen(false, "Measure frame timing");
        perf::Request(20.0, "", "main menu", 1.0);
        return;
    }
}

} // namespace

bool IsOpen() {
    return gOpen.load(std::memory_order_relaxed);
}

std::uint32_t ModFlags() {
    if (!gOpen.load(std::memory_order_relaxed)) {
        return 0;
    }
    return kMainMenuOpen | (gPauseWanted.load(std::memory_order_relaxed) ? kMainMenuPause : 0u);
}

void SetOpen(bool open, char const* why) {
    if (open == gOpen.load()) {
        return;
    }
    if (open && MELEVR::Menu::IsOpen()) {
        MELEVR::Menu::SetOpen(false);  // one menu at a time
    }
    if (open && calibration::Running()) {
        log::Info("main menu: not opened ({}): the calibration runs", why != nullptr ? why : "?");
        return;
    }
    gOpen.store(open);
    if (open) {
        g.placed = false;
        g.needRender = true;
        g.submitted = false;
        g.lastBackdropMs = 0;
        g.wasHit = g.hit = g.pressing = false;
        g.trigger[0] = g.trigger[1] = true;  // a trigger still held from opening isn't a click
        g.aHeld = g.bHeld = true;            // nor A or B
        g.hoverControl = nullptr;
        g.statsSinceMs = 0;
        g.cpuMs = 0.0;
        g.frames = g.renders = g.backdrops = 0;
        if (g.settings != nullptr) {
            gPauseWanted.store(g.settings->PauseWanted());
        }
    } else {
        if (g.settings != nullptr) {
            g.settings->Closed();
        }
        if (g.tapSwapchain != XR_NULL_HANDLE) {
            testloop::SetReleaseTap(XR_NULL_HANDLE, nullptr);
            g.tapSwapchain = XR_NULL_HANDLE;
        }
        if (g.ui != nullptr && g.ui->Context() != nullptr) {
            g.ui->Context()->ProcessMouseLeave();
        }
        gPauseWanted.store(false);
    }
    log::Info("main menu: {} ({})", open ? "opened" : "closed", why != nullptr ? why : "?");
}

void Toggle(char const* why) {
    SetOpen(!gOpen.load(), why);
}

void NoteControllers(vr::controllers::State const& live) {
    g.live = live;
}

void NotePad(XINPUT_GAMEPAD const& pad) {
    std::lock_guard lock(gPadMutex);
    gPad = pad;
    gPadMs = GetTickCount64();
}

void Recentered() {
    gRecentered.store(true, std::memory_order_release);
}

XrCompositionLayerQuad const* Layer(Frame const& frame) {
    if (frame.session != g.session) {
        g.swapchain = {};  // the old session's swapchain died with it
        g.session = frame.session;
        g.submitted = false;
    }
    if (!gOpen.load(std::memory_order_relaxed)) {
        return nullptr;  // closed: nothing at all
    }
    auto const start = std::chrono::steady_clock::now();
    if (MELEVR::Menu::IsOpen()) {
        SetOpen(false, "Halcyon's menu opened");
        return nullptr;
    }
    if (frame.session == XR_NULL_HANDLE || frame.context == nullptr || frame.device == nullptr || !Init(frame.device, frame.context) ||
        !EnsureSwapchain(frame.session)) {
        return nullptr;
    }
    std::uint64_t const now = GetTickCount64();
    float const dt = g.lastFrameMs == 0 ? 1.0f / 72.0f : static_cast<float>(now - g.lastFrameMs) / 1000.0f;
    g.lastFrameMs = now;
    if (!g.placed || gRecentered.exchange(false, std::memory_order_acq_rel)) {
        Place(frame);
        g.needRender = true;
    }
    Rml::Context* const context = g.ui->Context();
    if (g.ui->ReloadIfChanged()) {
        g.model->DirtyAll();
        g.needRender = true;
    }
    if (now - g.lastRefreshMs >= kRefreshEveryMs) {
        g.lastRefreshMs = now;
        g.model->Refresh();
        gPauseWanted.store(g.settings->PauseWanted());
    }
    g.settings->Tick(g.live, frame.headValid, frame.head.position);
    g.model->SetUserInput(true);  // only the player's input changes a setting through a slider
    Pointer(context, dt);
    Pad(context, now);
    g.model->SetUserInput(false);
    if (!gOpen.load()) {
        return nullptr;  // B closed it
    }
    AfterCommand(g.settings->TakeAfter());
    if (!gOpen.load()) {
        return nullptr;
    }
    bool const timed = g.gpuBegin != nullptr && !g.gpuPending;
    if (timed) {
        g.context->Begin(g.disjoint.Get());
        g.context->End(g.gpuBegin.Get());
    }
    UpdateBackdrop(frame, now);
    if (g.model->Get().Text("section") == "display") {
        g.strips->Update(g.context.Get(), g.settings->PreviewColour());
    }
    if (g.model->TakeChanged()) {
        g.needRender = true;
    }
    context->Update();
    if (context->GetNextUpdateDelay() <= 0.0) {
        g.needRender = true;  // a transition or an animation runs
    }
    if (g.needRender || !g.submitted) {
        g.needRender = false;
        g.ui->GetRenderer()->Render(g.context.Get(), context, g.canvasRtv.Get(), kWidth, kHeight);
        if (Submit()) {
            g.submitted = true;
            ++g.renders;
        }
    }
    if (timed) {
        g.context->End(g.gpuEnd.Get());
        g.context->End(g.disjoint.Get());
        g.gpuPending = true;
    }
    if (!g.submitted) {
        return nullptr;
    }
    g.quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    g.quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied; the cut corners are clear
    g.quad.space = frame.appSpace;
    g.quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    g.quad.subImage.swapchain = g.swapchain.handle;
    g.quad.subImage.imageRect = {{0, 0}, {kWidth, kHeight}};
    g.quad.pose = g.pose;
    g.quad.size = {g.width, g.height};
    Stats(now, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    return &g.quad;
}

std::string StatusJson() {
    namespace json = vr::test_request;
    return std::format("{{\"open\": {}, \"ready\": {}, \"pose\": [{}, {}, {}], \"sizeM\": [{}, {}], \"pointer\": {{\"hit\": {}, \"x\": {}, "
                       "\"y\": {}, \"hand\": \"{}\", \"pressing\": {}}}, \"section\": {}, \"pause\": {}, \"cpuMs\": {}, \"gpuMs\": {}, "
                       "\"rendersPerSecond\": {}}}",
        gOpen.load() ? "true" : "false", g.ready ? "true" : "false", json::JsonNumber(g.pose.position.x), json::JsonNumber(g.pose.position.y),
        json::JsonNumber(g.pose.position.z), json::JsonNumber(g.width), json::JsonNumber(g.height), g.hit ? "true" : "false",
        json::JsonNumber(g.x), json::JsonNumber(g.y), g.hand == 0 ? "left" : "right", g.pressing ? "true" : "false",
        json::JsonString(g.model != nullptr ? g.model->Get().Text("section") : std::string()),
        gPauseWanted.load() ? "true" : "false", g.lastCpuMs >= 0.0 ? json::JsonNumber(g.lastCpuMs) : std::string("null"),
        g.lastGpuMs >= 0.0 ? json::JsonNumber(g.lastGpuMs) : std::string("null"), json::JsonNumber(g.lastRendersPerSecond));
}

} // namespace mevr::host::main_menu

// SPDX-License-Identifier: MIT
// Squad placement's arc and landing ring (plugin/SquadPlaceDraw.hpp).

#include "plugin/SquadPlaceDraw.hpp"

#include "core/Log.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "core/vr/SquadPlace.hpp"
#include "plugin/GalaxyHologramDraw.hpp"
#include "plugin/MevrMenu.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/menu/StateGuard.hpp"

#include <Windows.h>
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
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <mutex>
#include <vector>

namespace mevr::host::squad_place_draw {
namespace {

using Microsoft::WRL::ComPtr;
namespace gh = vr::galaxy_holo;
namespace gd = galaxy_draw;
namespace sp = vr::squad_place;

constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
constexpr float kScale = 0.6f;  // of the eye's size: thin light on a dark nothing, MSAA does the edges

// The game's UI blue (research/55 section 5's samples), its amber, a calm green, a warning red; linear light.
constexpr gh::Colour kSquad{0.30f, 0.70f, 1.00f, 1.0f};
constexpr gh::Colour kMember{1.00f, 0.62f, 0.22f, 1.0f};
constexpr gh::Colour kFollow{0.35f, 1.00f, 0.55f, 1.0f};
constexpr gh::Colour kRefused{1.00f, 0.25f, 0.18f, 1.0f};
constexpr gh::Colour kWeapon{0.95f, 0.85f, 0.45f, 1.0f};  // the weapon row: a pale gold, apart from who
// The weapon row's names as the mod sends them (squad_menu's WeaponName, capitalised), to mark the one in hand.
constexpr char const* kWeaponNames[4] = {"Pistol", "Shotgun", "Assault rifle", "Sniper rifle"};

struct EyeChain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;
    int width = 0;
    int height = 0;
    bool failed = false;
    void Destroy() {
        if (handle != XR_NULL_HANDLE) {
            testloop::ForgetSwapchain(handle);
            xrDestroySwapchain(handle);
        }
        handle = XR_NULL_HANDLE;
        images.clear();
        width = height = 0;
    }
};

XrSession gSession = XR_NULL_HANDLE;
EyeChain gChains[2];
ComPtr<ID3D11Texture2D> gMsaa;
ComPtr<ID3D11RenderTargetView> gMsaaRtv;
int gMsaaWidth = 0;
int gMsaaHeight = 0;
gd::Renderer gRenderer;
bool gRendererTried = false;
XrCompositionLayerProjection gLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
XrCompositionLayerProjectionView gViews[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
std::uint64_t gStartMs = 0;
unsigned gFrames = 0;
double gDrawMsSum = 0.0;
std::uint64_t gLastCostLogMs = 0;
std::mutex gStatusMutex;
std::string gStatus = "{\"shown\": false}";

void SetStatus(std::string text) {
    std::lock_guard lock(gStatusMutex);
    gStatus = std::move(text);
}

bool MakeChain(XrSession session, int width, int height, EyeChain& out, char const* name) {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    info.format = kFormat;
    info.sampleCount = 1;
    info.width = static_cast<std::uint32_t>(width);
    info.height = static_cast<std::uint32_t>(height);
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    XrResult r = xrCreateSwapchain(session, &info, &out.handle);
    std::uint32_t count = 0;
    if (XR_SUCCEEDED(r)) r = xrEnumerateSwapchainImages(out.handle, 0, &count, nullptr);
    std::vector<XrSwapchainImageD3D11KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if (XR_SUCCEEDED(r) && count > 0) {
        r = xrEnumerateSwapchainImages(out.handle, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    }
    if (XR_FAILED(r) || count == 0) {
        log::Error("squad place: the {} eye's swapchain {}x{} failed (XrResult {}); no arc drawn", name, width, height, static_cast<int>(r));
        out.Destroy();
        out.failed = true;
        return false;
    }
    for (auto const& image : images) out.images.push_back(image.texture);
    out.width = width;
    out.height = height;
    testloop::NameSwapchain(out.handle, name);
    return true;
}

bool EnsureTargets(ID3D11Device* device, XrSession session, int width, int height) {
    if (session != gSession) {
        for (EyeChain& c : gChains) {
            c.Destroy();
            c.failed = false;
        }
        gSession = session;
    }
    for (int e = 0; e < 2; ++e) {
        EyeChain& c = gChains[e];
        if (c.failed) return false;
        if (c.handle == XR_NULL_HANDLE || c.width != width || c.height != height) {
            c.Destroy();
            if (!MakeChain(session, width, height, c, e == 0 ? "squadplace-left" : "squadplace-right")) return false;
            log::Info("squad place: the {} eye's swapchain {}x{}, {} images", e == 0 ? "left" : "right", width, height, c.images.size());
        }
    }
    if (gMsaa == nullptr || gMsaaWidth != width || gMsaaHeight != height) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(width);
        td.Height = static_cast<UINT>(height);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = kFormat;
        td.SampleDesc.Count = 4;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        gMsaa.Reset();
        gMsaaRtv.Reset();
        if (FAILED(device->CreateTexture2D(&td, nullptr, gMsaa.GetAddressOf())) ||
            FAILED(device->CreateRenderTargetView(gMsaa.Get(), nullptr, gMsaaRtv.GetAddressOf()))) {
            log::Error("squad place: the {}x{} 4x MSAA target failed; no arc drawn", width, height);
            for (EyeChain& c : gChains) c.failed = true;
            return false;
        }
        gMsaaWidth = width;
        gMsaaHeight = height;
    }
    return true;
}

bool EnsureRenderer(ID3D11Device* device) {
    if (gRendererTried) return gRenderer.Ready();
    gRendererTried = true;
    // The selector's names in the menus' font (deploy-asi puts it in ASI\fonts); without it, no names (the tiles' order and
    // the haptics still tell who).
    std::filesystem::path font;
    std::wstring const& settings = MELEVR::MevrBridge::SettingsPath();
    if (!settings.empty()) {
        font = std::filesystem::path(settings).parent_path() / L"fonts" / L"Rajdhani-SemiBold.ttf";
    }
    std::string error;
    bool const ok = gRenderer.Init(device, font, &error);
    if (!ok) {
        log::Error("squad place: the renderer failed ({}); no arc drawn", error);
    } else {
        log::Info("squad place: renderer ready{}", error.empty() ? std::string() : " (" + error + ")");
    }
    return ok;
}

gh::Colour Scaled(gh::Colour c, float a) {
    c.a = a;
    return c;
}

// The selector above the hand: its tiles side by side across the line of sight, the lit one larger, in its colour, underlined.
void AddSelector(gh::DrawList& list, sp::View const& view, XrPosef const& head) {
    int const tiles = std::min<int>(view.tiles, 3);
    if (tiles <= 0) return;
    XrVector3f const right = vr::overlay::Rotate(head.orientation, {1.0f, 0.0f, 0.0f});
    XrVector3f const flatRight = gh::Normalize({right.x, 0.0f, right.z});
    XrVector3f const centre = gh::Add(view.hand.position, {0.0f, 0.09f, 0.0f});
    float const distance = std::max(gh::Length(gh::Sub(centre, head.position)), 0.2f);
    float const scale = std::clamp(distance / 0.5f, 0.6f, 2.0f);  // about the same angular size wherever the hand is
    float const spacing = 0.075f * scale;
    for (int t = 0; t < tiles; ++t) {
        bool const lit = t == view.tile;
        int const member = sp::MemberOfTile(t, tiles == 3 ? 2 : 1);
        gh::Colour const colour = member == sp::kSquadTile ? kSquad : kMember;
        XrVector3f const at = gh::Add(centre, gh::Mul(flatRight, (static_cast<float>(t) - static_cast<float>(tiles - 1) * 0.5f) * spacing));
        gh::Label label;
        label.anchor = at;
        label.heightM = (lit ? 0.020f : 0.015f) * scale;
        label.colour = lit ? Scaled(colour, 1.0f) : gh::Colour{0.55f, 0.6f, 0.65f, 0.8f};
        label.text = view.names[t][0] != 0 ? std::string(view.names[t], strnlen(view.names[t], sizeof(view.names[t]))) : "?";
        label.plate = true;
        list.labels.push_back(label);
        if (lit) {
            XrVector3f const under = gh::Add(at, {0.0f, -0.006f * scale, 0.0f});
            float const half = 0.026f * scale;
            list.lines.push_back({gh::Sub(under, gh::Mul(flatRight, half)), gh::Add(under, gh::Mul(flatRight, half)), 0.0022f * scale,
                Scaled(colour, 0.95f)});
        }
    }
    // The weapon row (stick down): the lit squadmate's weapons under the names, the lit one in the weapon colour and underlined,
    // the one in hand marked by a dot. The names above stay, their lit tile showing whose weapons these are.
    int const weapons = std::min<int>(view.weaponTiles, 4);
    if (view.row != 1 || weapons <= 0) return;
    XrVector3f const rowCentre = gh::Add(centre, {0.0f, -0.045f * scale, 0.0f});
    float const weaponSpacing = 0.085f * scale;
    for (int w = 0; w < weapons; ++w) {
        bool const lit = w == view.weaponTile;
        XrVector3f const at =
            gh::Add(rowCentre, gh::Mul(flatRight, (static_cast<float>(w) - static_cast<float>(weapons - 1) * 0.5f) * weaponSpacing));
        gh::Label label;
        label.anchor = at;
        label.heightM = (lit ? 0.018f : 0.013f) * scale;
        label.colour = lit ? kWeapon : gh::Colour{0.55f, 0.6f, 0.65f, 0.8f};
        label.text = view.weaponNames[w][0] != 0 ? std::string(view.weaponNames[w], strnlen(view.weaponNames[w], sizeof(view.weaponNames[w])))
                                                : "?";
        label.plate = true;
        list.labels.push_back(label);
        if (lit) {
            XrVector3f const under = gh::Add(at, {0.0f, -0.006f * scale, 0.0f});
            float const half = 0.03f * scale;
            list.lines.push_back({gh::Sub(under, gh::Mul(flatRight, half)), gh::Add(under, gh::Mul(flatRight, half)), 0.0022f * scale, kWeapon});
        }
        std::string const name(view.weaponNames[w], strnlen(view.weaponNames[w], sizeof(view.weaponNames[w])));
        if (view.weaponInHand >= 0 && view.weaponInHand < 4 && name == kWeaponNames[view.weaponInHand]) {
            list.sprites.push_back({gh::Add(at, {0.0f, 0.03f * scale, 0.0f}), 0.004f * scale, kWeapon, gh::SpriteStyle::Star, {}});
        }
    }
}

// The arc and its end, in the app space.
gh::DrawList Build(sp::View const& view, XrPosef const& headPose, float timeS) {
    XrVector3f const head = headPose.position;
    gh::DrawList list;
    sp::Landing const landing = static_cast<sp::Landing>(view.landing);
    gh::Colour const colour = landing != sp::Landing::Floor ? kRefused : view.follow != 0 ? kFollow : view.member >= 0 ? kMember : kSquad;
    int const count = std::min<int>(view.count, sp::kMaxPoints);
    if (count < 2) {  // the weapon row: no arc, no ring, only the selector
        AddSelector(list, view, headPose);
        return list;
    }
    // The ribbon: about 0.15 degrees wide at any distance (never under 4 mm), dim at the hand, bright toward the end; a bead of
    // light runs along it so it reads as a throw.
    float const run = std::fmod(timeS * 1.6f, 1.0f);
    for (int i = 0; i + 1 < count; ++i) {
        XrVector3f const a = view.points[i];
        XrVector3f const b = view.points[i + 1];
        XrVector3f const mid = gh::Mul(gh::Add(a, b), 0.5f);
        float const distance = gh::Length(gh::Sub(mid, head));
        float const along = static_cast<float>(i) / static_cast<float>(std::max(count - 2, 1));
        float const bead = std::max(0.0f, 1.0f - std::abs(along - run) * 8.0f);
        float const alpha = (landing == sp::Landing::Floor ? 0.35f : 0.25f) + 0.45f * along + 0.4f * bead;
        list.lines.push_back({a, b, std::max(0.004f, distance * 0.0026f), Scaled(colour, std::min(alpha, 1.0f))});
    }
    XrVector3f normal = view.landingNormal;
    if (gh::Length(normal) < 1e-3f) normal = {0.0f, 1.0f, 0.0f};
    normal = gh::Normalize(normal);
    float const distance = gh::Length(gh::Sub(view.landingPoint, head));
    if (landing == sp::Landing::Floor) {
        // The ring lying on the surface, a hair above it; it grows in over the first 0.15 s and breathes slowly.
        float const grow = std::clamp(view.heldS / 0.15f, 0.3f, 1.0f);
        float const breathe = 1.0f + 0.04f * std::sin(timeS * 4.0f);
        float const radius = view.ringRadiusM * grow * breathe;
        XrVector3f const centre = gh::Add(view.landingPoint, gh::Mul(normal, 0.03f));
        float const width = std::max(0.02f, distance * 0.003f);
        list.rings.push_back({centre, normal, radius, width, Scaled(colour, 0.95f)});
        list.rings.push_back({centre, normal, radius * 0.35f, width * 0.7f, Scaled(colour, 0.6f)});
        list.sprites.push_back({centre, std::max(0.03f, distance * 0.004f), Scaled(colour, 0.7f), gh::SpriteStyle::Halo, {}});
        // Who goes there, over the ring: about 1.6 degrees tall at any distance.
        if (view.tiles > 0 && view.tile < 3 && view.names[view.tile][0] != 0) {
            gh::Label name;
            name.anchor = gh::Add(centre, gh::Mul(normal, std::max(0.25f, distance * 0.02f)));
            name.heightM = std::max(0.03f, distance * 0.028f);
            name.colour = Scaled(colour, 1.0f);
            name.text = view.follow != 0 ? "Regroup"
                                         : std::string(view.names[view.tile], strnlen(view.names[view.tile], sizeof(view.names[view.tile])));
            name.plate = true;
            list.labels.push_back(name);
        }
    } else {
        // Refused: a red spark where the flight ends.
        list.sprites.push_back({view.landingPoint, std::max(0.04f, distance * 0.006f), Scaled(colour, 0.9f), gh::SpriteStyle::Star, {}});
    }
    AddSelector(list, view, headPose);
    return list;
}

} // namespace

XrCompositionLayerBaseHeader const* Layer(Frame const& frame) {
    if (frame.session == XR_NULL_HANDLE || frame.appSpace == XR_NULL_HANDLE || frame.context == nullptr || frame.device == nullptr ||
        !frame.headValid) {
        return nullptr;
    }
    sp::View const view = sp::Latest();
    std::uint64_t const now = GetTickCount64();
    if (view.active == 0 || view.size != sizeof(sp::View) || (view.count < 2 && view.tiles == 0)) {
        if (gStartMs != 0) SetStatus("{\"shown\": false}");
        gStartMs = 0;
        return nullptr;
    }
    if (gStartMs == 0) gStartMs = now;
    if (!EnsureRenderer(frame.device)) return nullptr;
    int const width = std::clamp(frame.eyeWidth > 0 ? static_cast<int>(static_cast<float>(frame.eyeWidth) * kScale) : 1200, 512, 2048);
    int const height = std::clamp(frame.eyeHeight > 0 ? static_cast<int>(static_cast<float>(frame.eyeHeight) * kScale) : 1200, 512, 2048);
    if (!EnsureTargets(frame.device, frame.session, width, height)) return nullptr;
    float const timeS = static_cast<float>(now - gStartMs) / 1000.0f;
    gh::DrawList const list = Build(view, frame.head, timeS);
    LARGE_INTEGER t0{}, t1{}, freq{};
    QueryPerformanceCounter(&t0);
    gRenderer.Prepare(list, frame.head);
    {
        menu::StateGuard guard(frame.context);
        for (int e = 0; e < 2; ++e) {
            EyeChain& c = gChains[e];
            std::uint32_t index = 0;
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (XR_FAILED(xrAcquireSwapchainImage(c.handle, &acquire, &index))) return nullptr;
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout = XR_INFINITE_DURATION;
            if (XR_FAILED(xrWaitSwapchainImage(c.handle, &wait)) || index >= c.images.size()) {
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrReleaseSwapchainImage(c.handle, &release);
                return nullptr;
            }
            float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            frame.context->ClearRenderTargetView(gMsaaRtv.Get(), clear);
            gd::Eye const eye{frame.eyes[e], frame.fov[e]};
            gRenderer.Draw(frame.context, gMsaaRtv.Get(), width, height, eye, timeS);
            frame.context->ResolveSubresource(c.images[index], 0, gMsaa.Get(), 0, kFormat);
            testloop::OnImageWritten(c.handle, c.images[index]);
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            xrReleaseSwapchainImage(c.handle, &release);
            gViews[e].pose = frame.eyes[e];
            gViews[e].fov = frame.fov[e];
            gViews[e].subImage.swapchain = c.handle;
            gViews[e].subImage.imageRect = {{0, 0}, {width, height}};
            gViews[e].subImage.imageArrayIndex = 0;
        }
    }
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&freq);
    gDrawMsSum += static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
    ++gFrames;
    if (now - gLastCostLogMs > 10000) {
        if (gLastCostLogMs != 0 && gFrames > 0) {
            log::Info("squad place: last 10 s: {} frames drawn, {:.3f} ms a frame on the CPU (both eyes)", gFrames, gDrawMsSum / gFrames);
        }
        gLastCostLogMs = now;
        gFrames = 0;
        gDrawMsSum = 0.0;
    }
    SetStatus(std::format("{{\"shown\": true, \"landing\": \"{}\", \"follow\": {}, \"member\": {}, \"points\": {}, "
                          "\"landingPoint\": [{:.3f}, {:.3f}, {:.3f}], \"tiles\": {}, \"tile\": {}, \"target\": [{}, {}]}}",
        sp::Name(static_cast<sp::Landing>(view.landing)), view.follow != 0 ? "true" : "false", view.member, view.count,
        view.landingPoint.x, view.landingPoint.y, view.landingPoint.z, view.tiles, view.tile, width, height));
    gLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    gLayer.space = frame.appSpace;
    gLayer.viewCount = 2;
    gLayer.views = gViews;
    return reinterpret_cast<XrCompositionLayerBaseHeader const*>(&gLayer);
}

std::string StatusJson() {
    std::lock_guard lock(gStatusMutex);
    return gStatus;
}

} // namespace mevr::host::squad_place_draw

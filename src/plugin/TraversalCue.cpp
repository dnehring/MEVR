// SPDX-License-Identifier: MIT
// The traversal cue and blink quads (plugin/TraversalCue.hpp, research/le2-traversal.md).

#include "plugin/TraversalCue.hpp"

#include "core/Log.hpp"
#include "plugin/Swapchains.hpp"
#include "plugin/TestLoop.hpp"

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
#include <cstring>
#include <mutex>
#include <vector>

namespace mevr::host::traversal_cue {
namespace {

using Microsoft::WRL::ComPtr;
namespace tv = vr::traversal;

constexpr std::int64_t kFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // as our other quads
constexpr int kLabelWidth = 512;
constexpr int kLabelHeight = 128;          // one label; the swapchain holds both, stacked (Vault, then Climb up)
constexpr int kBlinkSize = 8;
constexpr int kBlinkLevels = 32;
constexpr std::uint64_t kStaleMs = 300;
constexpr float kBlinkDistance = 0.5f;     // metres ahead of the eyes
constexpr float kBlinkHalf = 4.0f;         // half the quad's side at that distance: past 80 degrees each way

std::mutex gMutex;
tv::Cue gCue{};
bool gHave = false;

// Present thread.
struct Picture {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;
    ComPtr<ID3D11Texture2D> texture;
    bool failed = false;
    std::uint64_t failedMs = 0;
    int level = -1;  // what the swapchain holds (-1: nothing yet)
};
XrSession gSession = XR_NULL_HANDLE;
XrSpace gViewSpace = XR_NULL_HANDLE;
bool gViewSpaceFailed = false;
Picture gLabel;
Picture gBlink;
std::vector<std::uint32_t> gLabelPixels;
XrCompositionLayerQuad gLabelQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
XrCompositionLayerQuad gBlinkQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
bool gToldLabel = false;
bool gToldBlink = false;
std::uint64_t gShownFrames = 0;
std::uint64_t gBlinkFrames = 0;
std::uint64_t gStatsMs = 0;

// sRGB-encoded channel values (0..255) premultiplied by `a` (0..1) in 0xAABBGGRR.
std::uint32_t Rgba(double r, double g, double b, double a) {
    a = std::clamp(a, 0.0, 1.0);
    auto const c = [a](double v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0, 255.0) * a)); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (static_cast<std::uint32_t>(std::lround(a * 255.0)) << 24);
}

// The signed distance (pixels) from a rounded rectangle's edge (negative inside).
double RoundedBox(double x, double y, double cx, double cy, double hw, double hh, double radius) {
    double const qx = std::abs(x - cx) - (hw - radius);
    double const qy = std::abs(y - cy) - (hh - radius);
    double const ox = std::max(qx, 0.0);
    double const oy = std::max(qy, 0.0);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0) - radius;
}

// One label's text and arrow as coverage (0..255 per pixel), drawn by GDI (grey anti-aliasing, white on black).
std::vector<std::uint8_t> TextCoverage(wchar_t const* text, bool up) {
    std::vector<std::uint8_t> coverage(static_cast<std::size_t>(kLabelWidth) * kLabelHeight, 0);
    HDC const dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr) {
        return coverage;
    }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kLabelWidth;
    bi.bmiHeader.biHeight = -kLabelHeight;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP const bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr) {
        DeleteDC(dc);
        return coverage;
    }
    HGDIOBJ const oldBitmap = SelectObject(dc, bitmap);
    std::memset(bits, 0, static_cast<std::size_t>(kLabelWidth) * kLabelHeight * 4);
    HFONT const font = CreateFontW(-76, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ const oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT rect{112, 0, kLabelWidth - 24, kLabelHeight};
    DrawTextW(dc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    // The arrow: a chevron over the obstacle (vault) or straight up (climb), left of the word.
    HBRUSH const brush = CreateSolidBrush(RGB(255, 255, 255));
    HGDIOBJ const oldBrush = SelectObject(dc, brush);
    HGDIOBJ const oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    if (up) {
        POINT const arrow[] = {{60, 22}, {92, 58}, {72, 58}, {72, 104}, {48, 104}, {48, 58}, {28, 58}};
        Polygon(dc, arrow, 7);
    } else {
        POINT const chevron[] = {{24, 92}, {60, 40}, {96, 92}, {80, 104}, {60, 74}, {40, 104}};
        Polygon(dc, chevron, 6);
    }
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
    GdiFlush();
    auto const* const p = static_cast<std::uint8_t const*>(bits);
    for (std::size_t i = 0; i < coverage.size(); ++i) {
        coverage[i] = std::max({p[i * 4], p[i * 4 + 1], p[i * 4 + 2]});
    }
    if (oldFont != nullptr) {
        SelectObject(dc, oldFont);
    }
    if (font != nullptr) {
        DeleteObject(font);
    }
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return coverage;
}

// Both labels, stacked: a dark glass plate with an orange rim, the white word and arrow.
std::vector<std::uint32_t> LabelPixels() {
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kLabelWidth) * kLabelHeight * 2, 0);
    for (int label = 0; label < 2; ++label) {
        std::vector<std::uint8_t> const text = TextCoverage(label == 0 ? L"Vault" : L"Climb up", label == 1);
        for (int y = 0; y < kLabelHeight; ++y) {
            for (int x = 0; x < kLabelWidth; ++x) {
                double const d = RoundedBox(x + 0.5, y + 0.5, kLabelWidth / 2.0, kLabelHeight / 2.0, kLabelWidth / 2.0 - 4.0,
                    kLabelHeight / 2.0 - 4.0, 30.0);
                double const inside = std::clamp(0.5 - d, 0.0, 1.0);
                double const rim = std::clamp(1.0 - std::abs(d + 3.0) / 3.0, 0.0, 1.0) * inside;
                double const t = text[static_cast<std::size_t>(y) * kLabelWidth + x] / 255.0;
                // Plate, rim over it, the text over both (straight colours, then premultiplied).
                double a = 0.62 * inside;
                double r = 18.0, g = 24.0, b = 32.0;
                auto const over = [&](double cr, double cg, double cb, double ca) {
                    double const out = ca + a * (1.0 - ca);
                    if (out > 0.0) {
                        r = (cr * ca + r * a * (1.0 - ca)) / out;
                        g = (cg * ca + g * a * (1.0 - ca)) / out;
                        b = (cb * ca + b * a * (1.0 - ca)) / out;
                    }
                    a = out;
                };
                over(240.0, 140.0, 40.0, 0.95 * rim);
                over(246.0, 246.0, 246.0, t * inside);
                pixels[static_cast<std::size_t>(label * kLabelHeight + y) * kLabelWidth + x] = Rgba(r, g, b, a);
            }
        }
    }
    return pixels;
}

void ForgetSession() {
    gViewSpace = XR_NULL_HANDLE;
    gViewSpaceFailed = false;
    for (Picture* p : {&gLabel, &gBlink}) {
        swapchains::Forget(p->swapchain);
        p->swapchain = XR_NULL_HANDLE;
        p->images.clear();
        p->failed = false;
        p->level = -1;
    }
}

bool EnsureViewSpace() {
    if (gViewSpace != XR_NULL_HANDLE) {
        return true;
    }
    if (gViewSpaceFailed) {
        return false;
    }
    XrReferenceSpaceCreateInfo info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    info.poseInReferenceSpace.orientation.w = 1.0f;
    if (XR_FAILED(xrCreateReferenceSpace(gSession, &info, &gViewSpace))) {
        gViewSpace = XR_NULL_HANDLE;
        gViewSpaceFailed = true;
        log::Error("traversal cue: the VIEW space couldn't be made: no blink this session");
        return false;
    }
    return true;
}

bool EnsurePicture(Picture& p, ID3D11Device* device, int width, int height, char const* name) {
    if (p.swapchain != XR_NULL_HANDLE && p.texture != nullptr) {
        return true;
    }
    if ((p.failed && !swapchains::RetryDue(p.failedMs)) || device == nullptr) {
        return false;
    }
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = kFormat;
    info.sampleCount = 1;
    info.width = static_cast<std::uint32_t>(width);
    info.height = static_cast<std::uint32_t>(height);
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    // Freed for room (plugin/Swapchains) when no frame has shown it for a second; made and filled again when next wanted.
    Picture* const owner = &p;
    auto evict = [owner] {
        swapchains::Destroy(owner->swapchain);
        owner->swapchain = XR_NULL_HANDLE;
        owner->images.clear();
        owner->level = -1;
    };
    XrResult const r = p.swapchain == XR_NULL_HANDLE ? swapchains::Create(gSession, info, name, evict, p.swapchain, p.images) : XR_SUCCESS;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(width);
    td.Height = static_cast<UINT>(height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (XR_FAILED(r) || p.images.empty() || FAILED(device->CreateTexture2D(&td, nullptr, p.texture.ReleaseAndGetAddressOf()))) {
        swapchains::Destroy(p.swapchain);
        p.swapchain = XR_NULL_HANDLE;
        p.images.clear();
        p.texture.Reset();
        p.failed = true;
        p.failedMs = GetTickCount64();
        return false;
    }
    p.failed = false;
    p.level = -1;
    testloop::NameSwapchain(p.swapchain, name);
    return true;
}

bool Fill(Picture& p, ID3D11DeviceContext* context, std::vector<std::uint32_t> const& pixels, int width, int height) {
    context->UpdateSubresource(p.texture.Get(), 0, nullptr, pixels.data(), static_cast<UINT>(width) * 4, 0);
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    std::uint32_t index = 0;
    if (XR_FAILED(xrAcquireSwapchainImage(p.swapchain, &acquire, &index))) {
        return false;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    bool const ok = XR_SUCCEEDED(xrWaitSwapchainImage(p.swapchain, &wait)) && index < p.images.size();
    if (ok) {
        context->CopyResource(p.images[index], p.texture.Get());
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(p.swapchain, &release);
    if (ok) {
        testloop::SetStaticImage(p.swapchain, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), pixels);
    }
    return ok;
}

} // namespace

void Set(tv::Cue const* cue) {
    std::lock_guard lock(gMutex);
    if (cue == nullptr || cue->size != sizeof(tv::Cue)) {
        gHave = false;
        return;
    }
    gCue = *cue;
    gCue.timeMs = GetTickCount64();
    gHave = true;
}

void Forget() {
    std::lock_guard lock(gMutex);
    gHave = false;
}

unsigned Layers(Frame const& frame, void const** layersOut, unsigned capacity) {
    tv::Cue cue;
    {
        std::lock_guard lock(gMutex);
        if (!gHave) {
            return 0;
        }
        cue = gCue;
    }
    if (GetTickCount64() - cue.timeMs > kStaleMs) {
        return 0;  // the mod stopped sending (it sends only while there is something to show)
    }
    bool const drawable = frame.worldFrame && !frame.menuOpen && frame.session != XR_NULL_HANDLE && frame.context != nullptr &&
        frame.appSpace != XR_NULL_HANDLE;
    if (!drawable || layersOut == nullptr || capacity == 0) {
        return 0;
    }
    if (frame.session != gSession) {
        ForgetSession();
        gSession = frame.session;
    }
    unsigned count = 0;
    // The label at the interaction point.
    if (cue.visible && frame.headValid && (cue.kind == tv::Kind::Vault || cue.kind == tv::Kind::ClimbUp) && count < capacity &&
        EnsurePicture(gLabel, frame.device, kLabelWidth, kLabelHeight * 2, "traversalcue")) {
        bool ready = gLabel.level == 1;
        if (!ready) {
            if (gLabelPixels.empty()) {
                gLabelPixels = LabelPixels();
            }
            ready = Fill(gLabel, frame.context, gLabelPixels, kLabelWidth, kLabelHeight * 2);
            gLabel.level = ready ? 1 : -1;
        }
        tv::Quad quad;
        if (ready && tv::PlaceCue(cue.position, frame.head, cue.heightDeg, static_cast<float>(kLabelWidth) / kLabelHeight, quad)) {
            gLabelQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gLabelQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gLabelQuad.space = frame.appSpace;
            gLabelQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gLabelQuad.subImage.swapchain = gLabel.swapchain;
            int const row = cue.kind == tv::Kind::ClimbUp ? 1 : 0;
            gLabelQuad.subImage.imageRect = {{0, row * kLabelHeight}, {kLabelWidth, kLabelHeight}};
            gLabelQuad.pose = quad.pose;
            gLabelQuad.size = {quad.width, quad.height};
            layersOut[count++] = &gLabelQuad;
            ++gShownFrames;
            if (!gToldLabel) {
                gToldLabel = true;
                log::Info("traversal cue: first shown: '{}' at ({:.2f}, {:.2f}, {:.2f}) m in the room, {:.2f} x {:.2f} m ({:.1f} deg tall), the head at "
                          "({:.2f}, {:.2f}, {:.2f})",
                    tv::Label(cue.kind), cue.position.x, cue.position.y, cue.position.z, quad.width, quad.height, cue.heightDeg, frame.head.x,
                    frame.head.y, frame.head.z);
            }
        }
    }
    // The blink: black over both eyes, last of ours.
    if (cue.blink > 0.0f && count < capacity && EnsureViewSpace() && EnsurePicture(gBlink, frame.device, kBlinkSize, kBlinkSize, "traversalblink")) {
        int const level = std::clamp(static_cast<int>(std::lround(cue.blink * kBlinkLevels)), 1, kBlinkLevels);
        bool ready = gBlink.level == level;
        if (!ready) {
            std::vector<std::uint32_t> const pixels(static_cast<std::size_t>(kBlinkSize) * kBlinkSize,
                Rgba(0.0, 0.0, 0.0, static_cast<double>(level) / kBlinkLevels));
            ready = Fill(gBlink, frame.context, pixels, kBlinkSize, kBlinkSize);
            gBlink.level = ready ? level : gBlink.level;
            ready = ready || gBlink.level > 0;
        }
        if (ready) {
            gBlinkQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gBlinkQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            gBlinkQuad.space = gViewSpace;
            gBlinkQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gBlinkQuad.subImage.swapchain = gBlink.swapchain;
            gBlinkQuad.subImage.imageRect = {{0, 0}, {kBlinkSize, kBlinkSize}};
            gBlinkQuad.pose.orientation.w = 1.0f;
            gBlinkQuad.pose.position = {0.0f, 0.0f, -kBlinkDistance};
            gBlinkQuad.size = {2.0f * kBlinkHalf * kBlinkDistance, 2.0f * kBlinkHalf * kBlinkDistance};
            layersOut[count++] = &gBlinkQuad;
            ++gBlinkFrames;
            if (!gToldBlink) {
                gToldBlink = true;
                log::Info("traversal cue: first blink ({:.0f}% black): a head-locked quad {:.1f} m wide at {:.1f} m", cue.blink * 100.0f,
                    gBlinkQuad.size.width, kBlinkDistance);
            }
        }
    }
    std::uint64_t const now = GetTickCount64();
    if (gStatsMs == 0) {
        gStatsMs = now;
    } else if (now - gStatsMs >= 10000 && (gShownFrames > 0 || gBlinkFrames > 0)) {
        log::Info("traversal cue: the label shown in {} frames, the blink in {} frames of the last {:.0f} s", gShownFrames, gBlinkFrames,
            (now - gStatsMs) / 1000.0);
        gShownFrames = gBlinkFrames = 0;
        gStatsMs = now;
    }
    return count;
}

} // namespace mevr::host::traversal_cue

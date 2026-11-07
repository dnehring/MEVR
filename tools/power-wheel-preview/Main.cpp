// SPDX-License-Identifier: MIT
// Renders the power wheel's look (src/plugin/PowerWheelDraw, research/55) to PNGs offline, so it can be checked against
// the user's concept (concept/power-wheel.png) without the game or a headset: the host's own ImGui drawing, icons and
// glow (plugin/Glow) on a D3D11 device of its own (hardware, else WARP), each canvas composited over a dark backdrop
// the way the runtime blends a premultiplied quad.
//
//   mevr-power-wheel-preview [output folder] [fonts folder]   (default: .\power-wheel-preview; fonts: third_party\fonts)
//
//   <scene>.png         the wheel with its glow: biotic-six (the concept's arrangement, Lift chosen), each tile chosen in
//                       turn, none chosen, tech, sentinel (one cooling down, one disabled), eight entries, half open,
//                       the hand moved toward a tile; source-*: the light from the omni-tool on the hand, moving
//   <scene>-raw.png     the same without the glow (biotic-six only)
//   hint.png            the first-time hint card
//   icons.png           every icon on a tile's glass, in Icon order

#include "core/PngFile.hpp"
#include "core/vr/ImageConvert.hpp"
#include "core/vr/PowerWheel.hpp"
#include "plugin/Glow.hpp"
#include "plugin/PowerIcons.hpp"
#include "plugin/PowerWheelDraw.hpp"

#include "imgui.h"
#include "imgui_impl_dx11.h"

#include <Windows.h>
#include <d3d11.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;
namespace draw = mevr::host::power_wheel_draw;
namespace icons = mevr::host::power_icons;
namespace image = mevr::vr::image;
namespace fs = std::filesystem;

struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    draw::Fonts fonts;
    mevr::host::glow::Glow glow;
    std::map<icons::Icon, ComPtr<ID3D11ShaderResourceView>> icons;
};

struct Picture {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<std::uint8_t> rgba;  // premultiplied, sRGB-encoded (the canvas as the quad gets it)
};

bool MakeTarget(Gpu& gpu, unsigned w, unsigned h, ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11RenderTargetView>& rtv,
    ComPtr<ID3D11ShaderResourceView>& srv) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    return SUCCEEDED(gpu.device->CreateTexture2D(&td, nullptr, texture.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreateRenderTargetView(texture.Get(), nullptr, rtv.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf()));
}

// One ImGui frame drawn by `paint` into a w x h canvas, with or without the glow, read back.
Picture Render(Gpu& gpu, unsigned w, unsigned h, std::function<void(ImDrawList*)> const& paint, bool withGlow,
    mevr::host::glow::Settings const& glowSettings = {}) {
    Picture out;
    ComPtr<ID3D11Texture2D> canvas, result;
    ComPtr<ID3D11RenderTargetView> canvasRtv, resultRtv;
    ComPtr<ID3D11ShaderResourceView> canvasSrv, resultSrv;
    if (!MakeTarget(gpu, w, h, canvas, canvasRtv, canvasSrv) || !MakeTarget(gpu, w, h, result, resultRtv, resultSrv)) {
        return out;
    }
    D3D11_TEXTURE2D_DESC sd{};
    canvas->GetDesc(&sd);
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(gpu.device->CreateTexture2D(&sd, nullptr, staging.GetAddressOf()))) {
        return out;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
    for (int frame = 0; frame < 2; ++frame) {  // the first frame bakes the glyphs
        io.DeltaTime = 1.0f / 72.0f;
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        paint(ImGui::GetBackgroundDrawList());
        ImGui::Render();
        ID3D11RenderTargetView* target = canvasRtv.Get();
        gpu.context->OMSetRenderTargets(1, &target, nullptr);
        float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        gpu.context->ClearRenderTargetView(target, clear);
        D3D11_VIEWPORT const viewport{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
        gpu.context->RSSetViewports(1, &viewport);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    ID3D11RenderTargetView* none = nullptr;
    gpu.context->OMSetRenderTargets(1, &none, nullptr);
    if (withGlow && gpu.glow.Apply(gpu.context.Get(), canvasSrv.Get(), w, h, resultRtv.Get(), glowSettings)) {
        gpu.context->CopyResource(staging.Get(), result.Get());
    } else {
        gpu.context->CopyResource(staging.Get(), canvas.Get());
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(gpu.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return out;
    }
    out.width = w;
    out.height = h;
    out.rgba.resize(static_cast<std::size_t>(w) * h * 4);
    for (unsigned y = 0; y < h; ++y) {
        std::memcpy(out.rgba.data() + static_cast<std::size_t>(y) * w * 4, static_cast<std::uint8_t const*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
            static_cast<std::size_t>(w) * 4);
    }
    gpu.context->Unmap(staging.Get(), 0);
    return out;
}

// The backdrop: the concept's mood, a dark blue-grey, lighter at the top.
std::uint32_t Backdrop(unsigned y, unsigned h) {
    float const t = static_cast<float>(y) / static_cast<float>(std::max(h, 1u));
    auto channel = [&](float top, float bottom) { return static_cast<std::uint8_t>(std::lround(top + (bottom - top) * t)); };
    return image::Pack(channel(34, 11), channel(40, 13), channel(58, 22), 255);
}

// A premultiplied quad over the backdrop as the runtime blends it: each channel decoded, src + (1 - a) * dst, encoded.
std::vector<std::uint32_t> OverBackdrop(Picture const& picture) {
    std::vector<std::uint32_t> out(static_cast<std::size_t>(picture.width) * picture.height);
    for (unsigned y = 0; y < picture.height; ++y) {
        std::uint32_t const back = Backdrop(y, picture.height);
        float const br = image::SrgbToLinear(static_cast<float>(back & 0xFF) / 255.0f);
        float const bg = image::SrgbToLinear(static_cast<float>((back >> 8) & 0xFF) / 255.0f);
        float const bb = image::SrgbToLinear(static_cast<float>((back >> 16) & 0xFF) / 255.0f);
        for (unsigned x = 0; x < picture.width; ++x) {
            std::uint8_t const* p = picture.rgba.data() + (static_cast<std::size_t>(y) * picture.width + x) * 4;
            float const a = static_cast<float>(p[3]) / 255.0f;
            float const r = image::SrgbToLinear(static_cast<float>(p[0]) / 255.0f) + (1.0f - a) * br;
            float const g = image::SrgbToLinear(static_cast<float>(p[1]) / 255.0f) + (1.0f - a) * bg;
            float const b = image::SrgbToLinear(static_cast<float>(p[2]) / 255.0f) + (1.0f - a) * bb;
            out[static_cast<std::size_t>(y) * picture.width + x] =
                image::Pack(image::LinearToSrgb8(std::min(r, 1.0f)), image::LinearToSrgb8(std::min(g, 1.0f)), image::LinearToSrgb8(std::min(b, 1.0f)), 255);
        }
    }
    return out;
}

bool Save(fs::path const& path, unsigned w, unsigned h, std::vector<std::uint32_t> const& pixels) {
    std::string error;
    bool const ok = mevr::png::Write(path, static_cast<int>(w), static_cast<int>(h), pixels.data(), &error);
    std::printf("%s: %s\n", path.filename().string().c_str(), ok ? "written" : error.c_str());
    return ok;
}

ComPtr<ID3D11ShaderResourceView> Texture(Gpu& gpu, std::vector<std::uint32_t> const& pixels, unsigned n) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = n;
    td.Height = n;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(), n * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(gpu.device->CreateTexture2D(&td, &data, texture.GetAddressOf())) ||
        FAILED(gpu.device->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf()))) {
        return nullptr;
    }
    return srv;
}

ImTextureID IconId(Gpu& gpu, icons::Icon icon) {
    auto it = gpu.icons.find(icon);
    if (it == gpu.icons.end()) {
        it = gpu.icons.emplace(icon, Texture(gpu, icons::Hologram(icons::Picture(icon)), icons::kPixels)).first;
    }
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(it->second.Get()));
}

draw::Entry Make(Gpu& gpu, char const* label, icons::Icon icon, draw::Discipline discipline) {
    draw::Entry e;
    e.label = label;
    e.icon = IconId(gpu, icon);
    e.discipline = discipline;
    return e;
}

}  // namespace

int main(int argc, char** argv) {
    fs::path const out = argc > 1 ? fs::path(argv[1]) : fs::path("power-wheel-preview");
    std::error_code ec;
    fs::create_directories(out, ec);
    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    Gpu gpu;
    D3D_FEATURE_LEVEL level{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, gpu.device.GetAddressOf(),
        &level, gpu.context.GetAddressOf());
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, gpu.device.GetAddressOf(), &level,
            gpu.context.GetAddressOf());
    }
    if (FAILED(hr)) {
        std::printf("no D3D11 device (0x%08lX)\n", static_cast<unsigned long>(hr));
        return 1;
    }
    std::string error;
    if (!gpu.glow.Initialize(gpu.device.Get(), &error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    std::string chosen;
    std::vector<fs::path> folders{argc > 2 ? fs::path(argv[2]) : fs::path("third_party") / "fonts"};
    gpu.fonts = draw::LoadFonts(io, folders, chosen);
    if (!ImGui_ImplDX11_Init(gpu.device.Get(), gpu.context.Get())) {
        std::printf("ImGui's D3D11 backend didn't start\n");
        return 1;
    }
    std::printf("fonts: %s\n", chosen.c_str());

    using D = draw::Discipline;
    using I = icons::Icon;
    unsigned const n = draw::kCanvas;
    auto wheel = [&](draw::WheelView const& view, std::string const& name, bool raw = false) {
        Picture const picture = Render(gpu, n, n, [&](ImDrawList* dl) { draw::DrawWheel(dl, gpu.fonts, view); }, true);
        Save(out / (name + ".png"), n, n, OverBackdrop(picture));
        if (raw) {
            Picture const plain = Render(gpu, n, n, [&](ImDrawList* dl) { draw::DrawWheel(dl, gpu.fonts, view); }, false);
            Save(out / (name + "-raw.png"), n, n, OverBackdrop(plain));
        }
    };

    // The concept's arrangement with LE1's six biotics: Barrier, Stasis, Lift (chosen, at the top), Throw, Warp, Singularity.
    draw::WheelView six;
    six.entries = {Make(gpu, "Barrier", I::Barrier, D::Biotic), Make(gpu, "Stasis", I::Stasis, D::Biotic), Make(gpu, "Lift", I::Lift, D::Biotic),
        Make(gpu, "Throw", I::Throw, D::Biotic), Make(gpu, "Warp", I::Warp, D::Biotic), Make(gpu, "Singularity", I::Singularity, D::Biotic)};
    six.selected = 2;
    six.time = 0.3f;
    wheel(six, "biotic-six", true);
    for (int i = 0; i < 6; ++i) {
        draw::WheelView v = six;
        v.selected = i;
        wheel(v, "biotic-six-sel" + std::to_string(i));
    }
    {
        draw::WheelView v = six;
        v.selected = -1;
        wheel(v, "biotic-six-none");
        v = six;
        v.emblem = draw::Emblem::None;
        wheel(v, "biotic-six-no-emblem");
        v = six;
        v.open = 0.5f;
        wheel(v, "biotic-six-opening");
        v = six;
        v.selected = 3;
        v.hand = {70.0f, 45.0f};
        wheel(v, "biotic-six-hand");
        // The light from the omni-tool's display on the hand (plugin/PowerWheel's source): at the centre, then moved toward
        // Throw with the hand, and frames 0.1 s apart to see it move (the pulses, the shimmer, the turning rings).
        v = six;
        v.source = mevr::vr::power_wheel::Vec2{0.0f, -20.0f};
        wheel(v, "source-centre");
        v.selected = 3;
        v.source = mevr::vr::power_wheel::Vec2{60.0f, 30.0f};
        wheel(v, "source-toward-throw");
        v.selectedFor = 0.1f;
        wheel(v, "source-new-pick");
        v.selectedFor = 1.0f;
        for (int f = 0; f < 4; ++f) {
            v.time = 0.3f + 0.1f * static_cast<float>(f);
            wheel(v, "source-frame" + std::to_string(f));
        }
    }
    {
        draw::WheelView v;
        v.entries = {Make(gpu, "Overload", I::Overload, D::Tech), Make(gpu, "Damping", I::Damping, D::Tech),
            Make(gpu, "Sabotage", I::Sabotage, D::Tech), Make(gpu, "AI Hacking", I::Hacking, D::Tech),
            Make(gpu, "Neural Shock", I::NeuralShock, D::Tech), Make(gpu, "Shield Boost", I::ShieldBoost, D::Tech)};
        v.selected = 0;
        wheel(v, "tech");
    }
    {
        draw::WheelView v;
        v.entries = {Make(gpu, "Throw", I::Throw, D::Biotic), Make(gpu, "Lift", I::Lift, D::Biotic), Make(gpu, "Overload", I::Overload, D::Tech),
            Make(gpu, "Damping", I::Damping, D::Tech), Make(gpu, "Barrier", I::Barrier, D::Biotic)};
        v.entries[1].cooldown = 0.6f;
        v.entries[3].enabled = false;
        v.selected = 0;
        wheel(v, "sentinel");
    }
    {
        draw::WheelView v;
        v.entries = {Make(gpu, "Adrenaline Burst", I::Adrenaline, D::Combat), Make(gpu, "Immunity", I::Immunity, D::Combat),
            Make(gpu, "Unity", I::Unity, D::Combat), Make(gpu, "Marksman", I::Marksman, D::Combat), Make(gpu, "Overkill", I::Overkill, D::Combat),
            Make(gpu, "Carnage", I::Carnage, D::Combat), Make(gpu, "Assassination", I::Assassination, D::Combat),
            Make(gpu, "Shield Boost", I::ShieldBoost, D::Tech)};
        v.selected = 4;
        wheel(v, "eight");
    }

    // The hint card.
    {
        draw::HintView hint;
        Picture const picture = Render(gpu, draw::kHintWidth, draw::kHintHeight, [&](ImDrawList* dl) { draw::DrawHint(dl, gpu.fonts, hint); }, true);
        Save(out / "hint.png", draw::kHintWidth, draw::kHintHeight, OverBackdrop(picture));
    }

    // The lock's marker (research/55 section 4): locked and ready, closing in, cooling down, a place, a cast's flash. Shown
    // over the backdrop at 2x for a closer look.
    {
        unsigned const m = draw::kLockCanvas;
        auto lock = [&](draw::LockDrawView const& v, std::string const& name) {
            Picture const picture = Render(gpu, m, m, [&](ImDrawList* dl) { draw::DrawLock(dl, v); }, true);
            Save(out / (name + ".png"), m, m, OverBackdrop(picture));
        };
        draw::LockDrawView v;
        v.icon = IconId(gpu, I::Throw);
        lock(v, "lock-ready");
        draw::LockDrawView closing = v;
        closing.lockedFor = 0.06f;
        lock(closing, "lock-closing");
        draw::LockDrawView cooling = v;
        cooling.ready = false;
        cooling.cooldown = 0.35f;
        lock(cooling, "lock-cooling");
        draw::LockDrawView place = v;
        place.onActor = false;
        place.icon = IconId(gpu, I::Singularity);
        place.time = 0.4f;
        lock(place, "lock-place");
        draw::LockDrawView cast = v;
        cast.sinceCast = 0.10f;
        cast.ready = false;
        cast.cooldown = 0.98f;
        lock(cast, "lock-cast");
    }

    // Every icon on a tile's glass.
    {
        int const count = static_cast<int>(I::Count);
        int const columns = 5;
        int const rows = (count + columns - 1) / columns;
        unsigned const cell = 200;
        unsigned const w = cell * columns;
        unsigned const h = cell * static_cast<unsigned>(rows);
        Picture const picture = Render(gpu, w, h,
            [&](ImDrawList* dl) {
                for (int i = 0; i < count; ++i) {
                    float const x = static_cast<float>((i % columns) * static_cast<int>(cell));
                    float const y = static_cast<float>((i / columns) * static_cast<int>(cell));
                    dl->AddRectFilled({x + 6.0f, y + 6.0f}, {x + cell - 6.0f, y + cell - 6.0f}, IM_COL32(10, 24, 56, 220), 6.0f);
                    dl->AddRect({x + 6.0f, y + 6.0f}, {x + cell - 6.0f, y + cell - 6.0f}, IM_COL32(92, 158, 255, 255), 6.0f, 0, 2.0f);
                    dl->AddImage(ImTextureRef(IconId(gpu, static_cast<I>(i))), {x + 22.0f, y + 14.0f}, {x + cell - 22.0f, y + cell - 30.0f});
                    dl->AddText(gpu.fonts.label, 22.0f, {x + 14.0f, y + cell - 32.0f}, IM_COL32(230, 238, 255, 255), icons::Name(static_cast<I>(i)));
                }
            },
            true);
        Save(out / "icons.png", w, h, OverBackdrop(picture));
    }

    gpu.icons.clear();
    gpu.glow.Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    return 0;
}

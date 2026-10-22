// Renders the squad weapon menu (src/plugin/SquadPanelDraw) to PNGs offline, with sample squads, so its
// look can be checked without the game or a headset: the same ImGui drawing and D3D11 backend the host
// uses, on a D3D11 device of its own (hardware, else WARP).
//
//   mevr-squad-preview [output folder]   (default: .\squad-preview)
//
// Each scene is written twice: <name>.png over a dark, game-like backdrop (what the eye sees through the
// translucent panel) and <name>-alpha.png as the quad's own pixels (straight alpha).

#include "core/PngFile.hpp"
#include "core/vr/SquadMenu.hpp"
#include "plugin/SquadPanelDraw.hpp"

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
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;
namespace menu = mevr::vr::squad_menu;
namespace panel = mevr::host::squad_panel;

void Member(menu::MemberView& m, char const* name, char const* role, char const* initials, int discipline, int current,
    std::uint8_t carried, float health) {
    std::snprintf(m.name, sizeof m.name, "%s", name);
    std::snprintf(m.role, sizeof m.role, "%s", role);
    std::snprintf(m.initials, sizeof m.initials, "%s", initials);
    m.discipline = static_cast<std::int8_t>(discipline);
    m.current = static_cast<std::int8_t>(current);
    m.carried = carried;
    m.health = health;
}

struct Scene {
    char const* name;
    menu::View view;
};

std::vector<Scene> Scenes() {
    std::vector<Scene> scenes;
    menu::View v;
    v.open = true;
    v.count = 2;
    Member(v.members[0], "ASHLEY", "SOLDIER", "AS", 0, 1, 0x0F, 0.25f);
    Member(v.members[1], "GARRUS", "TURIAN AGENT", "GA", 0, 3, 0x0F, 0.9f);
    v.selected = 1;
    v.highlighted = 2;
    scenes.push_back({"two-garrus-rifle", v});
    v.selected = 0;
    v.highlighted = -1;
    scenes.push_back({"two-ashley-nothing", v});
    Member(v.members[0], "TALI", "MACHINIST", "TA", 1, 1, 0x03, 0.6f);
    Member(v.members[1], "LIARA", "SCIENTIST", "LI", 2, 0, 0x05, -1.0f);
    v.selected = 1;
    v.highlighted = 0;
    v.confirmOnRelease = false;
    scenes.push_back({"two-liara-pistol-sticky", v});
    v.count = 1;
    Member(v.members[0], "WREX", "BATTLEMASTER", "WR", 0, 1, 0x0F, 1.0f);
    v.selected = 0;
    v.highlighted = 3;
    v.confirmOnRelease = true;
    scenes.push_back({"one-wrex-sniper", v});
    return scenes;
}

// sRGB byte <-> linear, for compositing over the backdrop the way the runtime blends.
float ToLinear(std::uint8_t c) {
    float const v = static_cast<float>(c) / 255.0f;
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
std::uint8_t ToSrgb(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    float const c = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(std::lround(c * 255.0f));
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path const out = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("squad-preview");
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        device.GetAddressOf(), &level, context.GetAddressOf());
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(),
            &level, context.GetAddressOf());
    }
    if (FAILED(hr)) {
        std::printf("no D3D11 device (0x%08lX)\n", static_cast<unsigned long>(hr));
        return 1;
    }

    UINT const w = panel::kWidth;
    UINT const h = panel::kHeight;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&td, nullptr, texture.GetAddressOf())) ||
        FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, rtv.GetAddressOf())) ||
        FAILED(device->CreateTexture2D(&sd, nullptr, staging.GetAddressOf()))) {
        std::printf("texture creation failed\n");
        return 1;
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
    std::string chosen;
    ImFont* const font = panel::LoadFont(io, chosen);
    if (!ImGui_ImplDX11_Init(device.Get(), context.Get())) {
        std::printf("ImGui's D3D11 backend didn't start\n");
        return 1;
    }
    std::printf("font: %s\n", chosen.c_str());

    int written = 0;
    for (Scene const& scene : Scenes()) {
        for (int frame = 0; frame < 2; ++frame) {  // the first frame builds the font atlas
            io.DeltaTime = 1.0f / 72.0f;
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            panel::Draw(ImGui::GetBackgroundDrawList(), font, scene.view);
            ImGui::Render();
            ID3D11RenderTargetView* target = rtv.Get();
            context->OMSetRenderTargets(1, &target, nullptr);
            float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            context->ClearRenderTargetView(target, clear);
            D3D11_VIEWPORT const viewport{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
            context->RSSetViewports(1, &viewport);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        context->CopyResource(staging.Get(), texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            std::printf("%s: readback failed\n", scene.name);
            continue;
        }
        std::vector<std::uint32_t> over(static_cast<size_t>(w) * h);
        std::vector<std::uint32_t> alone(static_cast<size_t>(w) * h);
        for (UINT y = 0; y < h; ++y) {
            auto const* row = static_cast<std::uint8_t const*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
            for (UINT x = 0; x < w; ++x) {
                std::uint8_t const* p = row + static_cast<size_t>(x) * 4;
                float const a = static_cast<float>(p[3]) / 255.0f;
                // A dark warehouse-like backdrop: a vertical gradient with a few lighter bands.
                float const by = static_cast<float>(y) / static_cast<float>(h);
                float const band = (x / 96 + y / 96) % 2 == 0 ? 0.012f : 0.0f;
                float const bg[3] = {0.020f + band + 0.03f * by, 0.016f + band + 0.02f * by, 0.012f + band};
                std::uint8_t rgb[3];
                std::uint8_t straight[3];
                for (int c = 0; c < 3; ++c) {
                    // Premultiplied over: the bytes are sRGB-encoded premultiplied colour (ImGui over a clear canvas),
                    // which the runtime reads as sRGB: decode, then blend linearly.
                    float const src = ToLinear(p[c]);
                    rgb[c] = ToSrgb(src + bg[c] * (1.0f - a));
                    straight[c] = a > 0.0f ? ToSrgb(std::min(src / a, 1.0f)) : 0;
                }
                over[static_cast<size_t>(y) * w + x] = rgb[0] | (rgb[1] << 8) | (rgb[2] << 16) | 0xFF000000u;
                alone[static_cast<size_t>(y) * w + x] =
                    straight[0] | (straight[1] << 8) | (straight[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
            }
        }
        context->Unmap(staging.Get(), 0);
        std::string error;
        bool const ok = mevr::png::Write(out / (std::string(scene.name) + ".png"), static_cast<int>(w), static_cast<int>(h), over.data(),
                            &error) &&
            mevr::png::Write(out / (std::string(scene.name) + "-alpha.png"), static_cast<int>(w), static_cast<int>(h), alone.data(),
                &error);
        std::printf("%s: %s\n", scene.name, ok ? "written" : error.c_str());
        written += ok ? 1 : 0;
    }
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    std::printf("%d scenes in %s\n", written, out.string().c_str());
    return written > 0 ? 0 : 1;
}

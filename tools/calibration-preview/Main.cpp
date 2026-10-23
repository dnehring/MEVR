// Renders the guided calibration's look (src/plugin/CalibrationDraw, research/25) to PNGs offline, so it can be
// checked without the game or a headset: the host's own ImGui drawing and D3D11 backend on a device of its own
// (hardware, else WARP), and the quads composited into an eye image the way the runtime shows them
// (core/vr/LayerComposite), laid out by the same core/vr/Calibration functions the host uses.
//
//   mevr-calibration-preview [output folder]   (default: .\calibration-preview)
//
//   panel-<screen>.png      the instruction panel over a dark backdrop, one per screen
//   board.png               the target board (the second target current, the first taken)
//   orb.png, orb-active.png, dot.png   the small pictures, over grey
//   eye-<scene>.png         the right eye's view of a scene: the quads over a simple room (a floor grid)

#include "core/PngFile.hpp"
#include "core/vr/Calibration.hpp"
#include "core/vr/ImageConvert.hpp"
#include "core/vr/LayerComposite.hpp"
#include "plugin/CalibrationDraw.hpp"

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
#include <string>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;
namespace draw = mevr::host::calibration_draw;
namespace cal = mevr::vr::calibration;
namespace composite = mevr::vr::composite;
namespace image = mevr::vr::image;
namespace fs = std::filesystem;

struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ImFont* font = nullptr;
};

// One ImGui frame drawn by `paint` into a w x h canvas, read back as straight-alpha sRGB pixels.
composite::Image RenderCanvas(Gpu& gpu, unsigned w, unsigned h, std::function<void(ImDrawList*)> const& paint) {
    composite::Image out;
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
    if (FAILED(gpu.device->CreateTexture2D(&td, nullptr, texture.GetAddressOf())) ||
        FAILED(gpu.device->CreateRenderTargetView(texture.Get(), nullptr, rtv.GetAddressOf())) ||
        FAILED(gpu.device->CreateTexture2D(&sd, nullptr, staging.GetAddressOf()))) {
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
        ID3D11RenderTargetView* target = rtv.Get();
        gpu.context->OMSetRenderTargets(1, &target, nullptr);
        float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        gpu.context->ClearRenderTargetView(target, clear);
        D3D11_VIEWPORT const viewport{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
        gpu.context->RSSetViewports(1, &viewport);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    gpu.context->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(gpu.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return out;
    }
    out.width = static_cast<int>(w);
    out.height = static_cast<int>(h);
    out.pixels.resize(static_cast<size_t>(w) * h);
    image::Options options;
    options.premultiplied = true;  // ImGui over a clear canvas: premultiplied, as the quad layer says
    for (unsigned y = 0; y < h; ++y) {
        image::ConvertRow(image::format::kR8G8B8A8Unorm, static_cast<std::uint8_t const*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
            static_cast<int>(w), options, out.pixels.data() + static_cast<size_t>(y) * w);
    }
    gpu.context->Unmap(staging.Get(), 0);
    return out;
}

// The host's premultiplied pictures as straight-alpha images.
composite::Image Picture(std::vector<std::uint32_t> const& premultiplied, unsigned n) {
    composite::Image out;
    out.width = out.height = static_cast<int>(n);
    out.pixels.resize(premultiplied.size());
    image::Options options;
    options.premultiplied = true;
    for (unsigned y = 0; y < n; ++y) {
        image::ConvertRow(image::format::kR8G8B8A8Unorm, premultiplied.data() + static_cast<size_t>(y) * n, static_cast<int>(n), options,
            out.pixels.data() + static_cast<size_t>(y) * n);
    }
    return out;
}

// An image over a flat backdrop (sRGB 0xAABBGGRR), for looking at a canvas on its own.
composite::Image Over(composite::Image const& image, std::uint32_t backdrop) {
    composite::Image out = image;
    for (std::uint32_t& p : out.pixels) {
        p = image::Over(backdrop, p);
    }
    return out;
}

bool Save(fs::path const& path, composite::Image const& image) {
    std::string error;
    bool const ok = mevr::png::Write(path, image.width, image.height, image.pixels.data(), &error);
    std::printf("%s: %s\n", path.filename().string().c_str(), ok ? "written" : error.c_str());
    return ok;
}

// ---- The eye's view -------------------------------------------------------------------------------------

constexpr int kEyeSize = 1400;
constexpr float kHalfFov = 0.8727f;  // 50 degrees
constexpr XrFovf kFov{-kHalfFov, kHalfFov, kHalfFov, -kHalfFov};

// A simple room: a dark floor with a faint grid at y = 0, a darker wall 9 m ahead, a dim ceiling glow.
composite::Image Room(composite::Eye const& eye) {
    composite::Image out;
    out.width = out.height = kEyeSize;
    out.pixels.resize(static_cast<size_t>(kEyeSize) * kEyeSize);
    for (int y = 0; y < kEyeSize; ++y) {
        for (int x = 0; x < kEyeSize; ++x) {
            XrVector3f const local = composite::EyeRay(eye.fov, (static_cast<float>(x) + 0.5f) / kEyeSize, (static_cast<float>(y) + 0.5f) / kEyeSize);
            XrVector3f const d = cal::Rotate(eye.pose.orientation, local);
            XrVector3f const o = eye.pose.position;
            float r = 0.006f, g = 0.008f, b = 0.012f;
            if (d.y < -1e-4f) {
                float const t = -o.y / d.y;
                float const px = o.x + d.x * t;
                float const pz = o.z + d.z * t;
                if (pz > -9.0f) {
                    float const gx = std::abs(px - std::round(px));
                    float const gz = std::abs(pz - std::round(pz));
                    float const line = (gx < 0.012f || gz < 0.012f) ? 1.0f : 0.0f;
                    float const fade = std::exp(-t * 0.12f);
                    r = 0.008f + 0.03f * line * fade;
                    g = 0.010f + 0.045f * line * fade;
                    b = 0.014f + 0.06f * line * fade;
                } else {
                    r = 0.005f, g = 0.006f, b = 0.009f;
                }
            } else if (d.z < -1e-4f) {
                float const t = (-9.0f - o.z) / d.z;
                float const py = o.y + d.y * t;
                float const band = std::abs(py - 2.4f) < 0.05f ? 0.06f : 0.0f;
                r = 0.006f + band * 0.3f;
                g = 0.008f + band * 0.45f;
                b = 0.012f + band * 0.8f;
            }
            out.pixels[static_cast<size_t>(y) * kEyeSize + x] =
                image::Pack(image::LinearToSrgb8(r), image::LinearToSrgb8(g), image::LinearToSrgb8(b), 255);
        }
    }
    return out;
}

struct Layer {
    XrPosef pose;
    float width;
    float height;
    composite::Image const* image;
};

void EyeView(fs::path const& path, XrPosef const& eyePose, std::vector<Layer> const& layers) {
    composite::Eye eye;
    eye.pose = eyePose;
    eye.fov = kFov;
    composite::Image target = Room(eye);
    for (Layer const& l : layers) {
        composite::Quad quad;
        quad.pose = l.pose;
        quad.width = l.width;
        quad.height = l.height;
        quad.image = l.image;
        composite::Draw(target, eye, quad);
    }
    Save(path, target);
}

} // namespace

int main(int argc, char** argv) {
    fs::path const out = argc > 1 ? fs::path(argv[1]) : fs::path("calibration-preview");
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
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    std::string chosen;
    gpu.font = draw::LoadFont(io, chosen);
    if (!ImGui_ImplDX11_Init(gpu.device.Get(), gpu.context.Get())) {
        std::printf("ImGui's D3D11 backend didn't start\n");
        return 1;
    }
    std::printf("font: %s\n", chosen.c_str());

    std::uint32_t const backdrop = image::Pack(18, 20, 26, 255);
    std::uint32_t const grey = image::Pack(70, 70, 74, 255);
    std::string const device = cal::ProfileLabel("/interaction_profiles/oculus/touch_controller", "SteamVR/OpenXR : playstation_vr2");

    // ---- The panel's screens ----
    struct Shot {
        char const* name;
        draw::PanelView view;
    };
    std::vector<Shot> shots;
    auto base = [&](draw::Screen screen) {
        draw::ScreenText const text = draw::TextFor(screen);
        draw::PanelView v;
        v.prompt = screen == draw::Screen::Prompt;
        v.device = device;
        v.step = text.pip;
        v.steps = draw::kSteps;
        v.title = text.title;
        v.body = text.body;
        v.time = 0.3f;
        v.buttons = v.prompt ? std::vector<int>{draw::kCalibrateNow, draw::kDismiss}
                             : std::vector<int>{draw::kSkip, draw::kRestart, draw::kCancel};
        return v;
    };
    {
        draw::PanelView v = base(draw::Screen::Prompt);
        v.pointer = true;
        auto const buttons = draw::LayoutButtons(v);
        v.pointerX = (buttons[0].x0 + buttons[0].x1) * 0.5f + 12.0f;
        v.pointerY = (buttons[0].y0 + buttons[0].y1) * 0.5f - 6.0f;
        v.hovered = draw::ButtonAt(v, v.pointerX, v.pointerY);
        shots.push_back({"prompt", v});
    }
    {
        draw::PanelView v = base(draw::Screen::Stand);
        v.status = "Hold still";
        v.progress = 0.62f;
        shots.push_back({"stand", v});
    }
    {
        draw::PanelView v = base(draw::Screen::TPose);
        v.status = "Arms straight out, level with your shoulders";
        v.progress = 0.0f;
        v.message = "Standing eye height 1.68 m. Got it.";
        shots.push_back({"tpose", v});
    }
    {
        draw::PanelView v = base(draw::Screen::Crouch);
        v.status = "Lower";
        v.progress = 0.0f;
        shots.push_back({"crouch", v});
    }
    {
        draw::PanelView v = base(draw::Screen::PistolGrab);
        v.status = "Squeeze the right grip at the orb";
        shots.push_back({"pistol-grab", v});
    }
    {
        draw::PanelView v = base(draw::Screen::PistolAim);
        v.status = "Target 2 of 3: hold still";
        v.progress = 0.4f;
        v.flash = 0.0f;
        shots.push_back({"pistol-aim", v});
    }
    {
        draw::PanelView v = base(draw::Screen::RifleAim);
        v.status = "Target 1 of 3: aim at the orange target";
        v.progress = 0.0f;
        v.message = "Those three aims were 4.6 degrees apart: once more, please, the same way each time.";
        v.warn = true;
        shots.push_back({"rifle-aim-again", v});
    }
    {
        draw::PanelView v = base(draw::Screen::Check);
        v.summary = {"Aiming eye: right (measured)", "Pistol: three aims within 1.2 degrees", "Rifle: three aims within 0.9 degrees",
            "Eye height 1.68 m standing, 1.06 m crouched; arms 0.66 m, shoulders 0.39 m"};
        v.buttons = {draw::kRedoBody, draw::kRedoPistol, draw::kRedoRifle, draw::kRowBreak, draw::kSave, draw::kSwitchEye, draw::kCancel};
        v.pointer = true;
        auto const buttons = draw::LayoutButtons(v);
        for (auto const& b : buttons) {
            if (b.id == draw::kSave) {
                v.pointerX = (b.x0 + b.x1) * 0.5f;
                v.pointerY = (b.y0 + b.y1) * 0.5f;
            }
        }
        v.hovered = draw::ButtonAt(v, v.pointerX, v.pointerY);
        shots.push_back({"check", v});
    }
    {
        draw::PanelView v = base(draw::Screen::Saved);
        v.buttons.clear();
        v.message = "Saved for " + device + ".";
        shots.push_back({"saved", v});
    }
    std::vector<composite::Image> panels;
    for (Shot const& shot : shots) {
        composite::Image const panel = RenderCanvas(gpu, draw::kPanelWidth, draw::kPanelHeight,
            [&](ImDrawList* dl) { draw::DrawPanel(dl, gpu.font, shot.view); });
        Save(out / (std::string("panel-") + shot.name + ".png"), Over(panel, backdrop));
        panels.push_back(panel);
    }

    // ---- The board and the pictures ----
    draw::BoardView board;
    board.current = 1;
    board.done[0] = true;
    board.time = 0.3f;
    composite::Image const boardImage =
        RenderCanvas(gpu, draw::kBoardSize, draw::kBoardSize, [&](ImDrawList* dl) { draw::DrawBoard(dl, gpu.font, board); });
    Save(out / "board.png", Over(boardImage, backdrop));
    draw::BoardView checkBoard;
    composite::Image const checkBoardImage =
        RenderCanvas(gpu, draw::kBoardSize, draw::kBoardSize, [&](ImDrawList* dl) { draw::DrawBoard(dl, gpu.font, checkBoard); });
    composite::Image const orb = Picture(draw::OrbPicture(false), draw::kOrbPixels);
    composite::Image const orbActive = Picture(draw::OrbPicture(true), draw::kOrbPixels);
    composite::Image const dot = Picture(draw::DotPicture(), draw::kDotPixels);
    Save(out / "orb.png", Over(orb, grey));
    Save(out / "orb-active.png", Over(orbActive, grey));
    Save(out / "dot.png", Over(dot, grey));

    // ---- What the right eye sees, laid out as the host lays it out ----
    cal::Anchor const anchor{{0.0f, 1.62f, 0.0f}, 0.0f};
    XrPosef eye;
    eye.position = {0.032f, 1.62f, 0.0f};
    eye.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    float const panelHeight = cal::kPanelWidth * static_cast<float>(draw::kPanelHeight) / static_cast<float>(draw::kPanelWidth);
    auto const panelLayer = [&](std::size_t shot, bool aiming) {
        return Layer{cal::PanelPose(anchor, aiming), cal::kPanelWidth, panelHeight, &panels[shot]};
    };
    Layer const boardLayer{cal::BoardPose(anchor), cal::kBoardSize, cal::kBoardSize, &boardImage};
    // Standing: the panel straight ahead.
    EyeView(out / "eye-stand.png", eye, {panelLayer(1, false)});
    // The pistol orb waiting, the panel ahead.
    {
        XrVector3f const rest = cal::OrbRest(anchor, cal::Orb::Pistol);
        EyeView(out / "eye-pistol-grab.png", eye,
            {panelLayer(4, false), {cal::Billboard(rest, eye.position), cal::kOrbSize, cal::kOrbSize, &orbActive}});
    }
    // Aiming the pistol at the second target (upper left): looking at it, the orb in the hand on the line of sight.
    {
        XrPosef look = eye;
        XrVector3f const target = cal::TargetPosition(anchor, 1);
        look.orientation = *cal::LookRotation({target.x - eye.position.x, target.y - eye.position.y, target.z - eye.position.z});
        XrVector3f const toTarget{target.x - eye.position.x, target.y - eye.position.y, target.z - eye.position.z};
        float const length = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z);
        XrVector3f const hand{eye.position.x + toTarget.x / length * 0.5f, eye.position.y + toTarget.y / length * 0.5f - 0.07f,
            eye.position.z + toTarget.z / length * 0.5f};
        EyeView(out / "eye-pistol-aim.png", look,
            {boardLayer, panelLayer(5, true), {cal::Billboard(hand, look.position), cal::kOrbSize, cal::kOrbSize, &orbActive}});
        EyeView(out / "eye-pistol-aim-ahead.png", eye,
            {boardLayer, panelLayer(5, true), {cal::Billboard(hand, eye.position), cal::kOrbSize, cal::kOrbSize, &orbActive}});
    }
    // Both rifle orbs waiting.
    {
        XrVector3f const rear = cal::OrbRest(anchor, cal::Orb::RifleRear);
        XrVector3f const front = cal::OrbRest(anchor, cal::Orb::RifleFront);
        EyeView(out / "eye-rifle-grab.png", eye,
            {panelLayer(6, false), {cal::Billboard(rear, eye.position), cal::kOrbSize, cal::kOrbSize, &orbActive},
                {cal::Billboard(front, eye.position), cal::kOrbSize, cal::kOrbSize, &orbActive}});
        EyeView(out / "eye-rifle-held.png", eye,
            {panelLayer(6, false), {cal::Billboard(rear, eye.position), cal::kOrbSize, cal::kOrbSize, &orb},
                {cal::Billboard(front, eye.position), cal::kOrbSize, cal::kOrbSize, &orbActive}});
    }
    // The check: the dot on the centre target, the panel at the lower left.
    {
        XrVector3f const target = cal::TargetPosition(anchor, 0);
        XrVector3f const dotAt{target.x + 0.03f, target.y - 0.02f, target.z + 0.01f};
        Layer const checkLayer{cal::BoardPose(anchor), cal::kBoardSize, cal::kBoardSize, &checkBoardImage};
        EyeView(out / "eye-check.png", eye,
            {checkLayer, panelLayer(7, true), {cal::Billboard(dotAt, eye.position), cal::kDotSize, cal::kDotSize, &dot}});
    }
    // The prompt.
    EyeView(out / "eye-prompt.png", eye, {panelLayer(0, false)});

    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    std::printf("written to %s\n", out.string().c_str());
    return 0;
}

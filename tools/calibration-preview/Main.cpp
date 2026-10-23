// SPDX-License-Identifier: MIT
// Renders the guided calibration's look (src/plugin/CalibrationDraw, research/25) to PNGs offline, so it can be
// checked without the game or a headset: the host's own ImGui drawing and D3D11 backend on a device of its own
// (hardware, else WARP), and the quads composited into an eye image the way the runtime shows them
// (core/vr/LayerComposite), laid out by the same core/vr/Calibration functions the host uses.
//
//   mevr-calibration-preview [output folder] [fonts folder]   (default: .\calibration-preview; fonts: .\fonts)
//
// The fonts folder may hold Rajdhani-SemiBold.ttf and TitilliumWeb-Regular.ttf (SIL OFL, not bundled), the
// menu mockup's fonts; without them Windows' Bahnschrift stands in, as in the game.
//
//   panel-<screen>.png      the instruction panel over a dark backdrop, one per screen
//   board.png               the target board (the second target current, the first taken)
//   board-hint-*.png        the board with the reason an aim isn't captured, across its top
//   marker.png              the ring at each hand's grab point
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
    draw::Fonts fonts;
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

// About a unit axis by an angle (radians).
XrQuaternionf AxisRotation(XrVector3f axis, float angle) {
    float const s = std::sin(angle * 0.5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)};
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
    std::vector<fs::path> folders{argc > 2 ? fs::path(argv[2]) : fs::path("fonts")};
    gpu.fonts = draw::LoadFonts(io, folders, chosen);
    if (!ImGui_ImplDX11_Init(gpu.device.Get(), gpu.context.Get())) {
        std::printf("ImGui's D3D11 backend didn't start\n");
        return 1;
    }
    std::printf("fonts: %s\n", chosen.c_str());

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
        v.buttons = v.prompt ? std::vector<int>{draw::kDismiss, draw::kCalibrateNow} : std::vector<int>{draw::kSkip, draw::kRestart};
        v.hints = !v.prompt;
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
        v.progress = -1.0f;
        shots.push_back({"pistol-grab", v});
    }
    std::size_t const pistolGrabShot = shots.size() - 1;
    {
        draw::PanelView v = base(draw::Screen::PistolAim);
        v.status = "Hold still";
        v.targets = 3;
        v.targetTaken[0] = true;
        v.targetCurrent = 1;
        v.progress = 0.4f;
        v.flash = 0.0f;
        shots.push_back({"pistol-aim", v});
    }
    std::size_t const pistolAimShot = shots.size() - 1;
    // The rifle's aims: why nothing is captured (orange), the hold, a single target to aim again, all three again.
    auto rifleAim = [&](char const* name, char const* status, bool warn, float progress, bool t0, bool t1, bool t2, int current,
                        char const* message) {
        draw::PanelView v = base(draw::Screen::RifleAim);
        v.status = status;
        v.statusWarn = warn;
        v.targets = 3;
        v.targetTaken[0] = t0;
        v.targetTaken[1] = t1;
        v.targetTaken[2] = t2;
        v.targetCurrent = current;
        v.progress = progress;
        if (message != nullptr) {
            v.message = message;
            v.warn = true;
        }
        shots.push_back({name, v});
    };
    rifleAim("rifle-aim-look", "Look at target 1", true, 0.0f, false, false, false, 0, nullptr);
    rifleAim("rifle-aim-shoulder", "Bring the stock to your shoulder: right hand by your cheek", true, 0.0f, true, false, false, 1, nullptr);
    rifleAim("rifle-aim-point", "Point the rifle at target 2: it's 31 degrees off", true, 0.0f, true, false, false, 1, nullptr);
    rifleAim("rifle-aim-grip", "Squeeze the left grip again", true, 0.0f, true, false, false, 1, nullptr);
    rifleAim("rifle-aim-hold", "Hold still: your left hand moved", false, 0.35f, true, false, false, 1, nullptr);
    rifleAim("rifle-aim-redo-one", "Hold still", false, 0.6f, true, false, true, 1,
        "Aim 2 was 6.1 degrees off the other two: aim it again.");
    rifleAim("rifle-aim-again", "Look at target 1", true, 0.0f, false, false, false, 0,
        "Those aims were 7.4 degrees apart (even the closest two 5.2 degrees): all three again, the same way each time.");
    std::size_t const rifleAimShot = shots.size() - 2;  // redo-one, for the eye view
    {
        draw::PanelView v = base(draw::Screen::RifleGrab);
        v.status = "Left hand: the orb is 27 cm away (the ring is your hand)";
        v.statusWarn = true;
        v.progress = -1.0f;
        shots.push_back({"rifle-grab-reach", v});
    }
    std::size_t const rifleGrabShot = shots.size() - 1;
    {
        draw::PanelView v = base(draw::Screen::Check);
        v.summary = {"Aiming eye: right (measured)", "Pistol: three aims within 1.2 degrees",
            "Rifle: two aims within 1.1 degrees (aim 3 left out, 6.4 degrees off)",
            "Eye height 1.68 m standing, 1.06 m crouched; arms 0.66 m, shoulders 0.39 m"};
        v.buttons = {draw::kRedoBody, draw::kRedoPistol, draw::kRedoRifle, draw::kRowBreak, draw::kSwitchEye, draw::kSave};
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
    std::size_t const checkShot = shots.size() - 1;
    {
        draw::PanelView v = base(draw::Screen::Saved);
        v.buttons.clear();
        v.hints = false;
        v.message = "Saved for " + device + ".";
        shots.push_back({"saved", v});
    }
    std::vector<composite::Image> panels;
    for (Shot const& shot : shots) {
        composite::Image const panel = RenderCanvas(gpu, draw::kPanelWidth, draw::kPanelHeight,
            [&](ImDrawList* dl) { draw::DrawPanel(dl, gpu.fonts, shot.view); });
        Save(out / (std::string("panel-") + shot.name + ".png"), Over(panel, backdrop));
        panels.push_back(panel);
    }

    // ---- The board and the pictures ----
    draw::BoardView board;
    board.current = 1;
    board.done[0] = true;
    board.time = 0.3f;
    composite::Image const boardImage =
        RenderCanvas(gpu, draw::kBoardSize, draw::kBoardSize, [&](ImDrawList* dl) { draw::DrawBoard(dl, gpu.fonts, board); });
    Save(out / "board.png", Over(boardImage, backdrop));
    // The reasons on the board, where the player looks while aiming.
    auto const boardWith = [&](char const* name, int current, bool done0, bool done2, char const* hint, bool warn) {
        draw::BoardView b;
        b.current = current;
        b.done[0] = done0;
        b.done[2] = done2;
        b.hint = hint;
        b.hintWarn = warn;
        b.time = 0.3f;
        composite::Image const image =
            RenderCanvas(gpu, draw::kBoardSize, draw::kBoardSize, [&](ImDrawList* dl) { draw::DrawBoard(dl, gpu.fonts, b); });
        Save(out / name, Over(image, backdrop));
        return image;
    };
    boardWith("board-hint-shoulder.png", 1, true, false, "STOCK TO YOUR SHOULDER", true);
    boardWith("board-hint-hold.png", 1, true, false, "HOLD STILL", false);
    composite::Image const redoBoard = boardWith("board-hint-redo.png", 1, true, true, "AIM 2 AGAIN: 6 DEGREES OFF", true);
    draw::BoardView checkBoard;
    composite::Image const checkBoardImage =
        RenderCanvas(gpu, draw::kBoardSize, draw::kBoardSize, [&](ImDrawList* dl) { draw::DrawBoard(dl, gpu.fonts, checkBoard); });
    composite::Image const orb = Picture(draw::OrbPicture(false), draw::kOrbPixels);
    composite::Image const orbActive = Picture(draw::OrbPicture(true), draw::kOrbPixels);
    composite::Image const dot = Picture(draw::DotPicture(), draw::kDotPixels);
    composite::Image const marker = Picture(draw::HandMarkerPicture(), draw::kMarkerPixels);
    Save(out / "marker.png", Over(marker, grey));
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
            {panelLayer(pistolGrabShot, false), {cal::Billboard(rest, eye.position), cal::kOrbSize, cal::kOrbSize, &orbActive}});
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
            {boardLayer, panelLayer(pistolAimShot, true), {cal::Billboard(hand, look.position), cal::kOrbSize, cal::kOrbSize, &orbActive}});
        EyeView(out / "eye-pistol-aim-ahead.png", eye,
            {boardLayer, panelLayer(pistolAimShot, true), {cal::Billboard(hand, eye.position), cal::kOrbSize, cal::kOrbSize, &orbActive}});
    }
    // Both rifle orbs waiting (placed for the user's measured arms), each hand's ring short of its orb; then the right
    // orb in the hand and the left hand reaching.
    {
        cal::BodyMeasures const body{0.73f, 0.29f};
        XrVector3f const rear = cal::OrbRest(anchor, cal::Orb::RifleRear, body);
        XrVector3f const front = cal::OrbRest(anchor, cal::Orb::RifleFront, body);
        XrVector3f const rightHand{rear.x + 0.03f, rear.y - 0.03f, rear.z + 0.07f};   // 8 cm short of its orb
        XrVector3f const leftHand{front.x - 0.06f, front.y - 0.05f, front.z + 0.18f};  // 20 cm short
        XrPosef down = eye;  // looking down at the orbs
        down.orientation = AxisRotation({1.0f, 0.0f, 0.0f}, -0.61f);
        Layer const rightRing{cal::Billboard(rightHand, eye.position), cal::kHandMarkerSize, cal::kHandMarkerSize, &marker};
        Layer const leftRing{cal::Billboard(leftHand, eye.position), cal::kHandMarkerSize, cal::kHandMarkerSize, &marker};
        EyeView(out / "eye-rifle-grab.png", down,
            {panelLayer(rifleGrabShot, false), {cal::Billboard(rear, eye.position), cal::kOrbSize, cal::kOrbSize, &orbActive},
                {cal::Billboard(front, eye.position), cal::kOrbSize, cal::kOrbSize, &orbActive}, rightRing, leftRing});
        EyeView(out / "eye-rifle-held.png", down,
            {panelLayer(rifleGrabShot, false), {cal::Billboard(rightHand, eye.position), cal::kHeldOrbSize, cal::kHeldOrbSize, &orb},
                {cal::Billboard(front, eye.position), cal::kOrbSize, cal::kOrbSize, &orbActive}, leftRing});
    }
    // Shouldering the rifle at the second target, aim 2 to redo: the board says so where the player looks.
    {
        XrPosef look = eye;
        XrVector3f const target = cal::TargetPosition(anchor, 1);
        look.orientation = *cal::LookRotation({target.x - eye.position.x, target.y - eye.position.y, target.z - eye.position.z});
        Layer const board2{cal::BoardPose(anchor), cal::kBoardSize, cal::kBoardSize, &redoBoard};
        XrVector3f const toTarget{target.x - eye.position.x, target.y - eye.position.y, target.z - eye.position.z};
        float const length = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z);
        XrVector3f const d{toTarget.x / length, toTarget.y / length, toTarget.z / length};
        XrVector3f const rightHand{eye.position.x + 0.03f, eye.position.y - 0.10f, eye.position.z};
        XrVector3f const leftHand{rightHand.x + d.x * 0.27f - 0.04f, rightHand.y + d.y * 0.27f - 0.04f, rightHand.z + d.z * 0.27f};
        EyeView(out / "eye-rifle-aim.png", look,
            {board2, panelLayer(rifleAimShot, true), {cal::Billboard(rightHand, look.position), cal::kHeldOrbSize, cal::kHeldOrbSize, &orb},
                {cal::Billboard(leftHand, look.position), cal::kHeldOrbSize, cal::kHeldOrbSize, &orb}});
    }
    // The check: the dot on the centre target, the panel at the lower left.
    {
        XrVector3f const target = cal::TargetPosition(anchor, 0);
        XrVector3f const dotAt{target.x + 0.03f, target.y - 0.02f, target.z + 0.01f};
        Layer const checkLayer{cal::BoardPose(anchor), cal::kBoardSize, cal::kBoardSize, &checkBoardImage};
        EyeView(out / "eye-check.png", eye,
            {checkLayer, panelLayer(checkShot, true), {cal::Billboard(dotAt, eye.position), cal::kDotSize, cal::kDotSize, &dot}});
    }
    // The prompt.
    EyeView(out / "eye-prompt.png", eye, {panelLayer(0, false)});

    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    std::printf("written to %s\n", out.string().c_str());
    return 0;
}

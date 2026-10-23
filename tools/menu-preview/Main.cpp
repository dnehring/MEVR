// Renders the VR main menu (research/28) to PNGs offline, so its look can be checked without the game or a
// headset: the host's own RmlUi setup, D3D11 renderer, decorators, document and data model (plugin/menu), on a
// device of its own (hardware, else WARP), over a stand-in for the game picture blurred by the host's own
// backdrop passes. Sample values stand in for the real settings.
//
//   mevr-menu-preview [output folder] [--warp] [--assets <folder>] [--fonts <folder>]
//
//   section-<name>.png       each section (the menu over the stand-in world, as the headset composites it)
//   section-<name>-<x>.png   a section's other states (Comfort's snap and movement, Display's image quality rows with
//                            the headset known and before VR has started)
//   advanced-<tab>.png       each of Advanced's tabs
//   state-hover.png          the pointer resting on Resume (its light sweep part-way) and a row hovered
//   state-pressed.png        a button held down (the dip)
//   state-focus.png          the pad's focus ring on a toggle
//   menu-alpha.png           the menu texture alone (premultiplied, over a checkerboard)
//   timing.txt               CPU and GPU time per frame (static, and while the pointer moves)

#include "core/PngFile.hpp"
#include "core/vr/Colour.hpp"
#include "core/vr/MenuSettings.hpp"
#include "plugin/menu/MenuBackdrop.hpp"
#include "plugin/menu/MenuModel.hpp"
#include "plugin/menu/MenuRenderer.hpp"
#include "plugin/menu/MenuStrip.hpp"
#include "plugin/menu/MenuUi.hpp"

#include <RmlUi/Core.h>

#include <Windows.h>
#include <d3d11.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace ms = mevr::vr::menu_settings;

double gClock = 0.0;  // the time RmlUi animates with

// ---- The settings, in memory --------------------------------------------------------------------------------
class PreviewBackend final : public mevr::menu::Backend {
public:
    void Read(mevr::menu::Values& v) override {
        if (!seeded_) {
            seeded_ = true;
            v.texts["chip_headset"] = "PlayStation VR2 \xC2\xB7 SteamVR";
            v.texts["chip_controllers"] = "Sense controllers";
            v.texts["chip_calibrated"] = "Calibrated";
            v.numbers["calibrated"] = 1;
            v.texts["cal_status"] = "Calibrated 22 Oct \xC2\xB7 PlayStation VR2 \xC2\xB7 Sense controllers";
            v.texts["cal_action"] = "Calibrate again";
            v.texts["t_stand"] = "1.64";
            v.texts["t_crouch"] = "1.02";
            v.texts["t_arm"] = "0.72";
            v.texts["t_eye"] = "Right";
            v.texts["t_pistol"] = "1.1";
            v.texts["t_rifle"] = "1.4";
            v.texts["profile"] = "auto";
            v.texts["profile_info"] = "Detected PlayStation VR2 (OLED): no correction is needed, the game's colours exactly as rendered.";
            v.numbers["c_brightness"] = -2;
            v.numbers["c_contrast"] = 8;
            v.numbers["c_saturation"] = 12;
            v.numbers["c_midtones"] = 0.92f;
            v.numbers["c_black"] = -0.01f;
            // The PS VR2's recommended eye size (SteamVR), the target matched to it (research/32).
            v.texts["quality"] = ms::ResolutionChoice(2824, 2880, 2824, 2880);
            v.texts["quality_text"] = ms::SizeText(2824, 2880);
            v.numbers["headset_known"] = 1;
            v.texts["headset_text"] = ms::HeadsetSizesText(2824, 2880);
            v.texts["prof_active"] = "1";
            v.numbers["lifesize"] = 1;
            v.numbers["eye_distance"] = 3.4f;
            v.numbers["convergence"] = 0.012f;
            v.numbers["fp_back"] = 0.23f;
            v.numbers["fp_up"] = -0.05f;
            v.numbers["lean_gain"] = 2.0f;
            v.numbers["panel_follow"] = 70;
        }
    }
    void Write(std::string const& key, mevr::menu::Values const&, bool final) override {
        std::printf("  write %s%s\n", key.c_str(), final ? "" : " (moving)");
    }
    void Command(std::string const& name, std::string const& argument, mevr::menu::Values& v) override {
        std::printf("  command %s %s\n", name.c_str(), argument.c_str());
        if (name == "reset") {
            v.numbers["reset_armed"] = v.Flag("reset_armed") ? 0.0f : 1.0f;
        }
    }

private:
    bool seeded_ = false;
};

// ---- A stand-in for the game's picture (one eye, 1024 x 1024): a dusky sky, a warm light, a teal glow, towers ----
std::vector<std::uint32_t> Scene(int size) {
    std::vector<std::uint32_t> px(static_cast<std::size_t>(size) * size);
    auto glow = [](float x, float y, float cx, float cy, float rx, float ry) {
        float const dx = (x - cx) / rx;
        float const dy = (y - cy) / ry;
        return std::max(0.0f, 1.0f - std::sqrt(dx * dx + dy * dy));
    };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float const u = static_cast<float>(x) / static_cast<float>(size);
            float const v = static_cast<float>(y) / static_cast<float>(size);
            float r = 0.13f + 0.18f * v, g = 0.22f + 0.2f * v, b = 0.34f + 0.12f * v;  // a hazy sky
            if (v > 0.62f) {  // the ground
                float const t = (v - 0.62f) / 0.38f;
                r = 0.36f - 0.2f * t;
                g = 0.28f - 0.16f * t;
                b = 0.22f - 0.12f * t;
            }
            float const warm = glow(u, v, 0.80f, 0.22f, 0.32f, 0.26f);
            r += 0.85f * warm * warm;
            g += 0.48f * warm * warm;
            b += 0.18f * warm * warm;
            float const teal = glow(u, v, 0.12f, 0.88f, 0.4f, 0.4f);
            g += 0.35f * teal;
            b += 0.4f * teal;
            float const violet = glow(u, v, 0.5f, 0.55f, 0.3f, 0.25f);
            r += 0.25f * violet;
            b += 0.35f * violet;
            // Towers.
            bool const tower = (u > 0.14f && u < 0.30f && v > 0.18f && v < 0.62f) || (u > 0.40f && u < 0.48f && v > 0.32f && v < 0.62f) ||
                (u > 0.62f && u < 0.70f && v > 0.40f && v < 0.62f);
            if (tower) {
                r = 0.10f;
                g = 0.15f;
                b = 0.21f;
                if ((static_cast<int>(v * 60.0f) % 4 == 0) && (static_cast<int>(u * 120.0f) % 3 == 0)) {
                    r = 0.95f;
                    g = 0.75f;
                    b = 0.45f;  // lit windows
                }
            }
            auto byte = [](float c) { return static_cast<std::uint32_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
            px[static_cast<std::size_t>(y) * size + x] = 0xFF000000u | (byte(b) << 16) | (byte(g) << 8) | byte(r);
        }
    }
    return px;
}

struct Target {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11Texture2D> staging;
};

bool MakeTarget(ID3D11Device* device, int w, int h, Target& t) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    return SUCCEEDED(device->CreateTexture2D(&td, nullptr, t.texture.GetAddressOf())) &&
        SUCCEEDED(device->CreateRenderTargetView(t.texture.Get(), nullptr, t.rtv.GetAddressOf())) &&
        SUCCEEDED(device->CreateTexture2D(&sd, nullptr, t.staging.GetAddressOf()));
}

std::vector<std::uint32_t> ReadBack(ID3D11DeviceContext* context, Target const& t, int w, int h) {
    std::vector<std::uint32_t> out(static_cast<std::size_t>(w) * h);
    context->CopyResource(t.staging.Get(), t.texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(t.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        for (int y = 0; y < h; ++y) {
            std::memcpy(&out[static_cast<std::size_t>(y) * w], static_cast<std::uint8_t const*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                static_cast<std::size_t>(w) * 4);
        }
        context->Unmap(t.staging.Get(), 0);
    }
    return out;
}

// The menu (premultiplied) over `under` (the world as the headset shows it behind the quad).
std::vector<std::uint32_t> Over(std::vector<std::uint32_t> const& menu, std::vector<std::uint32_t> const& under) {
    std::vector<std::uint32_t> out(menu.size());
    for (std::size_t i = 0; i < menu.size(); ++i) {
        std::uint32_t const m = menu[i];
        std::uint32_t const u = under[i];
        float const a = static_cast<float>(m >> 24) / 255.0f;
        std::uint32_t pixel = 0xFF000000u;
        for (int c = 0; c < 3; ++c) {
            float const top = static_cast<float>((m >> (8 * c)) & 0xFF);
            float const bottom = static_cast<float>((u >> (8 * c)) & 0xFF);
            pixel |= static_cast<std::uint32_t>(std::clamp(top + bottom * (1.0f - a), 0.0f, 255.0f) + 0.5f) << (8 * c);
        }
        out[i] = pixel;
    }
    return out;
}

// Un-premultiplied, for a PNG of the texture alone.
std::vector<std::uint32_t> Straight(std::vector<std::uint32_t> const& menu) {
    std::vector<std::uint32_t> out(menu.size());
    for (std::size_t i = 0; i < menu.size(); ++i) {
        std::uint32_t const m = menu[i];
        std::uint32_t const a = m >> 24;
        std::uint32_t pixel = a << 24;
        for (int c = 0; c < 3; ++c) {
            std::uint32_t const v = (m >> (8 * c)) & 0xFF;
            pixel |= (a == 0 ? 0 : std::min<std::uint32_t>(255, v * 255 / a)) << (8 * c);
        }
        out[i] = pixel;
    }
    return out;
}

fs::path Find(fs::path const& start, fs::path const& relative) {
    for (fs::path dir = start; !dir.empty(); dir = dir.parent_path()) {
        std::error_code ec;
        if (fs::exists(dir / relative, ec)) {
            return dir / relative;
        }
        if (dir == dir.parent_path()) {
            break;
        }
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    fs::path output = "menu-preview";
    bool warp = false;
    fs::path assets;
    fs::path fonts;
    for (int i = 1; i < argc; ++i) {
        std::string const a = argv[i];
        if (a == "--warp") {
            warp = true;
        } else if (a == "--assets" && i + 1 < argc) {
            assets = argv[++i];
        } else if (a == "--fonts" && i + 1 < argc) {
            fonts = argv[++i];
        } else {
            output = a;
        }
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (assets.empty()) {
        assets = Find(fs::path(exe).parent_path(), "assets/menu");
    }
    if (fonts.empty()) {
        fonts = Find(fs::path(exe).parent_path(), "third_party/fonts");
    }
    fs::create_directories(output);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL const levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = E_FAIL;
    if (!warp) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, device.GetAddressOf(),
            nullptr, context.GetAddressOf());
    }
    if (FAILED(hr)) {
        warp = true;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
            context.GetAddressOf());
    }
    if (FAILED(hr)) {
        std::fprintf(stderr, "no D3D11 device\n");
        return 1;
    }
    std::printf("device: %s; assets %s; fonts %s\n", warp ? "WARP" : "hardware", assets.string().c_str(), fonts.string().c_str());

    // The world behind the panel: the stand-in scene through the host's backdrop passes.
    int const sceneSize = 1024;
    std::vector<std::uint32_t> const scene = Scene(sceneSize);
    ComPtr<ID3D11Texture2D> sceneTexture;
    ComPtr<ID3D11ShaderResourceView> sceneSrv;
    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = td.Height = sceneSize;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{scene.data(), sceneSize * 4u, 0};
        device->CreateTexture2D(&td, &data, sceneTexture.GetAddressOf());
        device->CreateShaderResourceView(sceneTexture.Get(), nullptr, sceneSrv.GetAddressOf());
    }
    mevr::menu::Backdrop backdrop;
    if (!backdrop.Init(device.Get(), context.Get())) {
        std::fprintf(stderr, "backdrop shaders failed\n");
        return 1;
    }
    int const rect[4] = {0, 0, sceneSize, sceneSize};
    backdrop.Capture(context.Get(), sceneSrv.Get(), rect, sceneSize, sceneSize, false);
    // The panel 1.44 m wide at 1.2 m, in front of an eye with a 50-degree half FOV each way.
    mevr::menu::BackdropView view;
    view.center[2] = -1.2f;
    view.right[0] = 0.72f;
    view.up[1] = 0.45f;
    float const t = std::tan(50.0f * 3.14159265f / 180.0f);
    view.tanLeft = -t;
    view.tanRight = t;
    view.tanUp = t;
    view.tanDown = -t;
    backdrop.Compose(context.Get(), view);

    // What the panel covers of the scene, for compositing the PNGs the way the headset does.
    int const w = 1440;
    int const h = 900;
    std::vector<std::uint32_t> under(static_cast<std::size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float const px = (2.0f * (static_cast<float>(x) + 0.5f) / w - 1.0f) * 0.72f;
            float const py = (1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / h) * 0.45f;
            float const su = (px / 1.2f + t) / (2.0f * t);
            float const sv = (t - py / 1.2f) / (2.0f * t);
            int const sx = std::clamp(static_cast<int>(su * sceneSize), 0, sceneSize - 1);
            int const sy = std::clamp(static_cast<int>(sv * sceneSize), 0, sceneSize - 1);
            under[static_cast<std::size_t>(y) * w + x] = scene[static_cast<std::size_t>(sy) * sceneSize + sx];
        }
    }

    mevr::menu::StripPair strips;
    strips.Init(device.Get());
    mevr::vr::colour::Settings colour;
    colour.brightness = 0.98f;
    colour.contrast = 1.08f;
    colour.saturation = 1.12f;
    colour.gamma = 0.92f;
    colour.black = -0.01f;
    strips.Update(context.Get(), colour);

    mevr::menu::Ui ui;
    mevr::menu::UiSetup setup;
    setup.device = device.Get();
    setup.documents = assets;
    setup.fontDirs = {fonts};
    setup.log = [](mevr::menu::LogLevel level, std::string const& text) {
        std::printf("%s%s\n", level == mevr::menu::LogLevel::Error ? "ERROR " : level == mevr::menu::LogLevel::Warn ? "warn " : "", text.c_str());
    };
    setup.clock = [] { return gClock; };
    if (!ui.Init(setup)) {
        return 1;
    }
    ui.GetRenderer()->SetExternalTexture("backdrop", backdrop.View(), static_cast<int>(mevr::menu::kBackdropWidth),
        static_cast<int>(mevr::menu::kBackdropHeight));
    ui.GetRenderer()->SetExternalTexture("strip_original", strips.Original(), mevr::menu::kStripWidth, mevr::menu::kStripHeight);
    ui.GetRenderer()->SetExternalTexture("strip_adjusted", strips.Adjusted(), mevr::menu::kStripWidth, mevr::menu::kStripHeight);
    PreviewBackend backend;
    mevr::menu::Model model;
    if (!model.Bind(ui.Context(), &backend) || !ui.LoadDocument()) {
        return 1;
    }
    Target target;
    if (!MakeTarget(device.Get(), w, h, target)) {
        return 1;
    }
    Rml::Context* const rml = ui.Context();

    auto frames = [&](int n, double step = 1.0 / 72.0) {
        for (int i = 0; i < n; ++i) {
            gClock += step;
            ui.Render(context.Get(), target.rtv.Get());
        }
    };
    auto save = [&](std::string const& name) {
        std::vector<std::uint32_t> const menu = ReadBack(context.Get(), target, w, h);
        std::string error;
        if (!mevr::png::Write(output / (name + ".png"), w, h, Over(menu, under).data(), &error)) {
            std::fprintf(stderr, "%s: %s\n", name.c_str(), error.c_str());
        } else {
            std::printf("wrote %s\n", (output / (name + ".png")).string().c_str());
        }
        return menu;
    };
    auto element = [&](char const* id) { return ui.Document() != nullptr ? ui.Document()->GetElementById(id) : nullptr; };
    auto centre = [&](Rml::Element* e) {
        Rml::Vector2f const p = e->GetAbsoluteOffset(Rml::BoxArea::Border) + e->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
        return Rml::Vector2i(static_cast<int>(p.x), static_cast<int>(p.y));
    };

    rml->ProcessMouseMove(-50, -50, 0);
    for (char const* section : {"comfort", "controls", "body", "display", "hud"}) {
        model.SetText("section", section);
        frames(30);
        save(std::string("section-") + section);
    }
    // Comfort's other states (research/31): snap with its angle, the vignette on (its rows enabled), walking toward the hand.
    model.SetText("section", "comfort");
    model.SetText("turn", "snap");
    model.SetText("snap", "60");
    model.SetNumber("vignette", 1.0f);
    model.SetText("vignette_when", "move");
    model.SetText("walk", "hand");
    frames(30);
    save("section-comfort-snap");
    if (Rml::Element* content = element("content")) {  // the page's lower half: walking toward the left hand
        content->SetScrollTop(content->GetScrollHeight());
        frames(10);
        save("section-comfort-movement");
        content->SetScrollTop(0.0f);
    }
    // Display's image quality rows (research/32): Headset × 0.85 with FSR, then before VR has started (no recommendation).
    model.SetText("section", "display");
    model.SetText("quality", "headset85");
    model.SetText("quality_text", ms::SizeText(2400, 2448));
    model.SetText("upscale", "fsr");
    frames(30);
    if (Rml::Element* content = element("content")) {
        content->SetScrollTop(content->GetScrollHeight());
        frames(10);
        save("section-display-quality");
        model.SetNumber("headset_known", 0.0f);
        model.SetText("headset_text", ms::HeadsetSizesText(0, 0));
        model.SetText("quality", ms::ResolutionChoice(2824, 2880, 0, 0));
        model.SetText("quality_text", ms::SizeText(2824, 2880));
        model.SetText("upscale", "off");
        frames(10);
        save("section-display-quality-novr");
        content->SetScrollTop(0.0f);
    }
    model.SetNumber("headset_known", 1.0f);
    model.SetText("headset_text", ms::HeadsetSizesText(2824, 2880));
    model.SetText("quality", "headset");
    model.SetText("section", "comfort");
    model.SetText("turn", "smooth");
    model.SetText("snap", "45");
    model.SetNumber("vignette", 0.0f);
    model.SetText("vignette_when", "turn");
    model.SetText("walk", "head");
    model.SetText("section", "advanced");
    for (char const* tab : {"tracking", "fp", "view", "stereo", "hud", "profiles", "diag"}) {
        model.SetText("advtab", tab);
        frames(30);
        std::vector<std::uint32_t> const menu = save(std::string("advanced-") + tab);
        if (std::string(tab) == "stereo") {
            mevr::png::Write(output / "menu-alpha.png", w, h, Straight(menu).data());
        }
    }

    // Hover: the pointer on Resume, part-way through its sweep; then on a row's toggle. From here on, the player's input
    // (until now the sliders only showed values: any "write" printed above would be a setting changed by showing it).
    model.SetUserInput(true);
    model.SetText("section", "controls");
    frames(10);
    if (Rml::Element* resume = element("resume")) {
        Rml::Vector2i const at = centre(resume);
        rml->ProcessMouseMove(at.x, at.y, 0);
        frames(1);
        save("state-hover-sweep-1");  // the light sweep's first frames (0.55 s, cubic-out)
        frames(2);
        save("state-hover-sweep-2");
        frames(2);
        save("state-hover-sweep-3");
        frames(4);
        save("state-hover");  // the band across the middle
        rml->ProcessMouseButtonDown(0, 0);
        frames(8);
        save("state-pressed");
        rml->ProcessMouseButtonUp(0, 0);
    }
    rml->ProcessMouseMove(-50, -50, 0);
    frames(60);
    // The pad's focus: arrow keys from the rail.
    if (Rml::Element* first = ui.Document() != nullptr ? ui.Document()->QuerySelector(".tog") : nullptr) {
        first->Focus(true);
        rml->ProcessKeyDown(Rml::Input::KI_DOWN, 0);
        rml->ProcessKeyUp(Rml::Input::KI_DOWN, 0);
        frames(10);
        if (Rml::Element* focus = rml->GetFocusElement()) {
            std::printf("focus after right: <%s class=\"%s\">\n", focus->GetTagName().c_str(), focus->GetClassNames().c_str());
        }
        save("state-focus");
    }

    // Timing: a still menu, then a moving pointer (what the headset sees while pointing).
    D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    ComPtr<ID3D11Query> disjoint, begin, end;
    device->CreateQuery(&qd, disjoint.GetAddressOf());
    qd.Query = D3D11_QUERY_TIMESTAMP;
    device->CreateQuery(&qd, begin.GetAddressOf());
    device->CreateQuery(&qd, end.GetAddressOf());
    auto measure = [&](bool moving, double& cpu, double& gpu) {
        int const n = 60;
        cpu = 0.0;
        gpu = 0.0;
        for (int i = 0; i < n; ++i) {
            if (moving) {
                rml->ProcessMouseMove(400 + (i * 13) % 900, 200 + (i * 7) % 500, 0);
            }
            context->Begin(disjoint.Get());
            context->End(begin.Get());
            auto const t0 = std::chrono::steady_clock::now();
            gClock += 1.0 / 72.0;
            ui.Render(context.Get(), target.rtv.Get());
            cpu += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            context->End(end.Get());
            context->End(disjoint.Get());
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
            while (context->GetData(disjoint.Get(), &dj, sizeof dj, 0) == S_FALSE) {
            }
            UINT64 t0g = 0, t1g = 0;
            while (context->GetData(begin.Get(), &t0g, sizeof t0g, 0) == S_FALSE) {
            }
            while (context->GetData(end.Get(), &t1g, sizeof t1g, 0) == S_FALSE) {
            }
            if (!dj.Disjoint && dj.Frequency > 0) {
                gpu += static_cast<double>(t1g - t0g) * 1000.0 / static_cast<double>(dj.Frequency);
            }
        }
        cpu /= n;
        gpu /= n;
    };
    double cpuStill = 0, gpuStill = 0, cpuMoving = 0, gpuMoving = 0;
    model.SetText("section", "display");
    frames(10);
    measure(false, cpuStill, gpuStill);
    measure(true, cpuMoving, gpuMoving);
    std::ofstream timing(output / "timing.txt");
    char line[256];
    std::snprintf(line, sizeof line,
        "device %s\nmenu frame (Display section, 1440x900, 4x MSAA): still %.3f ms CPU, %.3f ms GPU; pointer moving %.3f ms CPU, %.3f ms GPU\n",
        warp ? "WARP" : "hardware", cpuStill, gpuStill, cpuMoving, gpuMoving);
    timing << line;
    std::printf("%s", line);
    ui.Shutdown();
    return 0;
}

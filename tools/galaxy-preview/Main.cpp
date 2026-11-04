// SPDX-License-Identifier: MIT
// The galaxy hologram, offline (research/51 section 8): each level of a made-up map drawn with the host's own code
// (core/vr/GalaxyHologram, plugin/GalaxyHologramDraw) for both eyes, as PNGs, plus a stereo check: every star's centre is
// found in the rendered left and right images and triangulated back into the room, against where the scene put it.
//
//   mevr-galaxy-preview <out folder> [--font <ttf>] [--size N] [--point <level>:<object index>]
//
// Writes <level>-left.png, <level>-right.png, <level>-sbs.png and report.txt (the check's numbers and the frame's cost).

#include "core/PngFile.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "plugin/GalaxyHologramDraw.hpp"

#include <d3d11.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace gh = mevr::vr::galaxy_holo;
namespace gd = mevr::host::galaxy_draw;

namespace {

constexpr float kPi = 3.14159265358979f;

void SetLabel(char (&dst)[gh::kLabelBytes], char const* text) {
    std::snprintf(dst, gh::kLabelBytes, "%s", text);
}

gh::Object& Add(gh::Scene& s, float x, float y, float z, gh::Kind kind, char const* label, std::uint8_t flags = gh::kSelectable,
                float size = 0.0f) {
    gh::Object& o = s.objects[s.objectCount];
    o.position[0] = x;
    o.position[1] = y;
    o.position[2] = z;
    o.kind = static_cast<std::uint8_t>(kind);
    o.flags = flags;
    o.size = size;
    o.id = s.objectCount + 1u;
    SetLabel(o.label, label);
    ++s.objectCount;
    return o;
}

void Link(gh::Scene& s, int a, int b) {
    s.links[s.linkCount].a = static_cast<std::uint8_t>(a);
    s.links[s.linkCount].b = static_cast<std::uint8_t>(b);
    ++s.linkCount;
}

// Made-up maps in game-like units (the names are invented, not the game's).
gh::Scene GalaxyScene() {
    gh::Scene s;
    s.level = static_cast<std::uint8_t>(gh::Level::Galaxy);
    SetLabel(s.title, "The Galaxy");
    float const r = 20000.0f;
    struct C { float a, d; char const* name; std::uint8_t flags; };
    C const clusters[] = {{0.3f, 0.25f, "Home Cluster", gh::kSelectable | gh::kHere}, {1.4f, 0.55f, "Arcturus Reach", gh::kSelectable},
                          {2.3f, 0.75f, "Veil Nebula", gh::kSelectable},           {3.1f, 0.45f, "Kepler Verge", gh::kSelectable | gh::kMarked},
                          {4.0f, 0.82f, "Hollow Expanse", gh::kSelectable},       {4.8f, 0.6f, "Orion Spur", gh::kSelectable},
                          {5.6f, 0.35f, "Serpent Drift", gh::kSelectable},        {0.9f, 0.9f, "Far Tide", gh::kSelectable}};
    for (C const& c : clusters) {
        Add(s, std::cos(c.a) * c.d * r, std::sin(c.a) * c.d * r, 400.0f * std::sin(c.a * 3.0f), gh::Kind::Cluster, c.name, c.flags);
    }
    Link(s, 0, 1); Link(s, 0, 3); Link(s, 0, 6); Link(s, 1, 2); Link(s, 1, 7); Link(s, 3, 4); Link(s, 4, 5); Link(s, 5, 6);
    s.extent = r * 1.05f;
    return s;
}

gh::Scene ClusterScene() {
    gh::Scene s;
    s.level = static_cast<std::uint8_t>(gh::Level::Cluster);
    SetLabel(s.title, "Arcturus Reach");
    Add(s, 0.0f, 0.0f, 0.0f, gh::Kind::Relay, "Mass Relay", 0);
    char const* names[] = {"Tamsin", "Ulgrave", "Pellucid", "Myrrh", "Dovecote", "Saffron"};
    for (int i = 0; i < 6; ++i) {
        float const a = static_cast<float>(i) * 1.05f + 0.3f;
        float const d = 3000.0f + 1400.0f * static_cast<float>(i % 3);
        Add(s, std::cos(a) * d, std::sin(a) * d, 600.0f * std::sin(a * 2.0f), gh::Kind::System, names[i],
            i == 2 ? gh::kSelectable | gh::kSelected : gh::kSelectable);
    }
    return s;
}

gh::Scene SystemScene() {
    gh::Scene s;
    s.level = static_cast<std::uint8_t>(gh::Level::System);
    SetLabel(s.title, "Pellucid");
    Add(s, 0.0f, 0.0f, 0.0f, gh::Kind::Star, "Pellucid", 0, 1.0f);
    char const* names[] = {"Ashfall", "Corvane", "Lumen", "Drift IV", "Outer Reach"};
    float const radii[] = {1500.0f, 2600.0f, 3800.0f, 5200.0f, 6800.0f};
    float const sizes[] = {0.8f, 1.1f, 1.4f, 1.8f, 1.0f};
    for (int i = 0; i < 5; ++i) {
        float const a = static_cast<float>(i) * 2.2f + 0.4f;
        Add(s, std::cos(a) * radii[i], std::sin(a) * radii[i], 0.0f, gh::Kind::Planet, names[i],
            i == 2 ? gh::kSelectable | gh::kSelected : gh::kSelectable, sizes[i]);
    }
    return s;
}

gh::Scene PlanetScene() {
    gh::Scene s;
    s.level = static_cast<std::uint8_t>(gh::Level::Planet);
    SetLabel(s.title, "Lumen");
    Add(s, 0.0f, 0.0f, 0.0f, gh::Kind::Planet, "Lumen", gh::kSelectable | gh::kSelected, 3.0f);
    Add(s, -4000.0f, 3000.0f, 800.0f, gh::Kind::Star, "", 0, 0.5f);
    s.extent = 5000.0f;
    return s;
}

struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
};

bool MakeGpu(Gpu& gpu) {
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        gpu.device.GetAddressOf(), nullptr, gpu.context.GetAddressOf());
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, gpu.device.GetAddressOf(),
            nullptr, gpu.context.GetAddressOf());
    }
    return SUCCEEDED(hr);
}

// One eye's picture: drawn into a 4x MSAA sRGB target over the background, resolved, read back (0xAABBGGRR, sRGB).
std::vector<std::uint32_t> RenderEye(Gpu& gpu, gd::Renderer& renderer, gd::Eye const& eye, int size, float timeS, double* gpuMs) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = static_cast<UINT>(size);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    td.SampleDesc.Count = 4;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> msaa, resolved, staging;
    gpu.device->CreateTexture2D(&td, nullptr, msaa.GetAddressOf());
    td.SampleDesc.Count = 1;
    gpu.device->CreateTexture2D(&td, nullptr, resolved.GetAddressOf());
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    gpu.device->CreateTexture2D(&td, nullptr, staging.GetAddressOf());
    ComPtr<ID3D11RenderTargetView> rtv;
    gpu.device->CreateRenderTargetView(msaa.Get(), nullptr, rtv.GetAddressOf());
    float const background[4] = {0.004f, 0.006f, 0.012f, 1.0f};  // a dark room
    gpu.context->ClearRenderTargetView(rtv.Get(), background);

    D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    ComPtr<ID3D11Query> disjoint, begin, end;
    gpu.device->CreateQuery(&qd, disjoint.GetAddressOf());
    qd.Query = D3D11_QUERY_TIMESTAMP;
    gpu.device->CreateQuery(&qd, begin.GetAddressOf());
    gpu.device->CreateQuery(&qd, end.GetAddressOf());
    gpu.context->Begin(disjoint.Get());
    gpu.context->End(begin.Get());
    renderer.Draw(gpu.context.Get(), rtv.Get(), size, size, eye, timeS);
    gpu.context->End(end.Get());
    gpu.context->End(disjoint.Get());
    gpu.context->ResolveSubresource(resolved.Get(), 0, msaa.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    gpu.context->CopyResource(staging.Get(), resolved.Get());
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    while (gpu.context->GetData(disjoint.Get(), &dj, sizeof(dj), 0) == S_FALSE) {
    }
    UINT64 t0 = 0, t1 = 0;
    while (gpu.context->GetData(begin.Get(), &t0, sizeof(t0), 0) == S_FALSE) {
    }
    while (gpu.context->GetData(end.Get(), &t1, sizeof(t1), 0) == S_FALSE) {
    }
    if (gpuMs != nullptr) {
        *gpuMs = dj.Disjoint || dj.Frequency == 0 ? -1.0 : static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(dj.Frequency);
    }
    std::vector<std::uint32_t> pixels(static_cast<size_t>(size) * size);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(gpu.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
        for (int y = 0; y < size; ++y) {
            std::memcpy(&pixels[static_cast<size_t>(y) * size], static_cast<std::uint8_t const*>(m.pData) + static_cast<size_t>(y) * m.RowPitch,
                        static_cast<size_t>(size) * 4);
        }
        gpu.context->Unmap(staging.Get(), 0);
    }
    for (std::uint32_t& p : pixels) {
        p |= 0xFF000000u;
    }
    return pixels;
}

// A room point's pixel in an eye's image (x right, y down), or false behind it.
bool Project(gd::Eye const& eye, int size, XrVector3f const& p, float& px, float& py) {
    float rows[4][4];
    gd::Renderer::ViewProjection(eye, 0.03f, 50.0f, rows);
    float const v[4] = {p.x, p.y, p.z, 1.0f};
    float clip[4];
    for (int r = 0; r < 4; ++r) {
        clip[r] = rows[r][0] * v[0] + rows[r][1] * v[1] + rows[r][2] * v[2] + rows[r][3] * v[3];
    }
    if (clip[3] <= 1e-4f) {
        return false;
    }
    px = (clip[0] / clip[3] * 0.5f + 0.5f) * static_cast<float>(size);
    py = (0.5f - clip[1] / clip[3] * 0.5f) * static_cast<float>(size);
    return true;
}

// The brightness-weighted centre of the light near (px, py), from the rendered pixels.
bool Centroid(std::vector<std::uint32_t> const& pixels, int size, float px, float py, int window, float& cx, float& cy) {
    double sx = 0.0, sy = 0.0, sw = 0.0;
    int const x0 = static_cast<int>(px) - window, y0 = static_cast<int>(py) - window;
    for (int y = std::max(0, y0); y < std::min(size, y0 + 2 * window + 1); ++y) {
        for (int x = std::max(0, x0); x < std::min(size, x0 + 2 * window + 1); ++x) {
            std::uint32_t const p = pixels[static_cast<size_t>(y) * size + x];
            double const lum = 0.3 * (p & 0xFF) + 0.59 * ((p >> 8) & 0xFF) + 0.11 * ((p >> 16) & 0xFF);
            double const w = std::pow(std::max(0.0, lum - 60.0), 2.0);  // the bright core, not the glow or the labels
            sx += w * (x + 0.5);
            sy += w * (y + 0.5);
            sw += w;
        }
    }
    if (sw <= 0.0) {
        return false;
    }
    cx = static_cast<float>(sx / sw);
    cy = static_cast<float>(sy / sw);
    return true;
}

// The point nearest two rays (the eyes through the found centres), room space.
XrVector3f Triangulate(gd::Eye const& left, gd::Eye const& right, int size, float lx, float ly, float rx, float ry) {
    auto const ray = [size](gd::Eye const& e, float px, float py) {
        float const nx = px / static_cast<float>(size) * 2.0f - 1.0f;
        float const ny = 1.0f - py / static_cast<float>(size) * 2.0f;
        float const tl = std::tan(e.fov.angleLeft), tr = std::tan(e.fov.angleRight);
        float const tu = std::tan(e.fov.angleUp), td = std::tan(e.fov.angleDown);
        float const x = (nx * (tr - tl) + (tr + tl)) * 0.5f;
        float const y = (ny * (tu - td) + (tu + td)) * 0.5f;
        return gh::Normalize(mevr::vr::overlay::Rotate(e.pose.orientation, {x, y, -1.0f}));
    };
    XrVector3f const p1 = left.pose.position, d1 = ray(left, lx, ly);
    XrVector3f const p2 = right.pose.position, d2 = ray(right, rx, ry);
    XrVector3f const w = gh::Sub(p1, p2);
    float const a = gh::Dot(d1, d1), b = gh::Dot(d1, d2), c = gh::Dot(d2, d2), d = gh::Dot(d1, w), e = gh::Dot(d2, w);
    float const den = a * c - b * b;
    float const s = den > 1e-9f ? (b * e - c * d) / den : 0.0f;
    float const t = den > 1e-9f ? (a * e - b * d) / den : 0.0f;
    return gh::Mul(gh::Add(gh::Add(p1, gh::Mul(d1, s)), gh::Add(p2, gh::Mul(d2, t))), 0.5f);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: mevr-galaxy-preview <out folder> [--font <ttf>] [--size N] [--point <level>:<object index>]\n");
        return 2;
    }
    std::filesystem::path const out = argv[1];
    std::filesystem::path font = std::filesystem::path(__FILE__).parent_path() / "../../third_party/fonts/Rajdhani-SemiBold.ttf";
    int size = 1400;
    int pointLevel = 3, pointIndex = 3;
    for (int i = 2; i < argc; ++i) {
        std::string const a = argv[i];
        if (a == "--font" && i + 1 < argc) {
            font = argv[++i];
        } else if (a == "--size" && i + 1 < argc) {
            size = std::clamp(std::atoi(argv[++i]), 256, 4096);
        } else if (a == "--point" && i + 1 < argc) {
            std::string const v = argv[++i];
            size_t const colon = v.find(':');
            if (colon != std::string::npos) {
                pointLevel = std::atoi(v.substr(0, colon).c_str());
                pointIndex = std::atoi(v.substr(colon + 1).c_str());
            }
        }
    }
    std::filesystem::create_directories(out);
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        return 1;
    }
    Gpu gpu;
    if (!MakeGpu(gpu)) {
        std::printf("no D3D11 device\n");
        return 1;
    }
    gd::Renderer renderer;
    std::string error;
    if (!renderer.Init(gpu.device.Get(), font, &error)) {
        std::printf("renderer: %s\n", error.c_str());
        return 1;
    }
    if (!error.empty()) {
        std::printf("renderer: %s\n", error.c_str());
    }

    // A standing player at 1.6 m looking down at the table, 64 mm between the eyes, 90 degrees square.
    float const pitch = -28.0f * kPi / 180.0f;
    XrPosef head{{std::sin(pitch * 0.5f), 0.0f, 0.0f, std::cos(pitch * 0.5f)}, {0.0f, 1.6f, 0.0f}};
    gh::TableSettings const settings;
    gh::Table const table = gh::PlaceTable(XrPosef{{0.0f, 0.0f, 0.0f, 1.0f}, head.position}, settings);
    XrVector3f const headRight = mevr::vr::overlay::Rotate(head.orientation, {1.0f, 0.0f, 0.0f});
    gd::Eye left, right;
    left.pose = right.pose = head;
    left.pose.position = gh::Sub(head.position, gh::Mul(headRight, 0.032f));
    right.pose.position = gh::Add(head.position, gh::Mul(headRight, 0.032f));
    left.fov = right.fov = XrFovf{-0.785398f, 0.785398f, 0.785398f, -0.785398f};

    std::ofstream report(out / "report.txt");
    report << std::format("galaxy hologram preview: {} px square eyes, 4x MSAA, head at 1.60 m pitched {:.0f} deg, eyes 64 mm apart; "
                          "table {:.2f} m ahead, {:.2f} m below the eyes, radius {:.2f} m\n",
                          size, pitch * 180.0f / kPi, settings.distanceM, settings.belowM, settings.radiusM);
    struct Named { char const* name; gh::Scene scene; };
    Named const scenes[] = {{"galaxy", GalaxyScene()}, {"cluster", ClusterScene()}, {"system", SystemScene()}, {"planet", PlanetScene()}};
    float worstError = 0.0f;
    for (Named const& n : scenes) {
        gh::Scene const& scene = n.scene;
        auto const placed = gh::PlaceObjects(scene, table, settings);
        // A right controller below and beside the head, pointing at one object (or nothing).
        int hovered = -1;
        XrVector3f const hand{0.20f, 1.15f, -0.20f};
        if (scene.level == pointLevel && pointIndex >= 0 && pointIndex < static_cast<int>(placed.size())) {
            XrVector3f const dir = gh::Normalize(gh::Sub(placed[static_cast<size_t>(pointIndex)].position, hand));
            hovered = gh::Pick(hand, gh::Add(dir, XrVector3f{0.004f, 0.0f, 0.0f}), placed, scene);
        }
        auto const t0 = std::chrono::steady_clock::now();
        gh::DrawList const list = gh::Build(scene, table, settings, placed, hovered, 0.6f);
        gd::Stats const stats = renderer.Prepare(list, head);
        double const cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        double gpuL = 0.0, gpuR = 0.0;
        auto const imgL = RenderEye(gpu, renderer, left, size, 0.6f, &gpuL);
        auto const imgR = RenderEye(gpu, renderer, right, size, 0.6f, &gpuR);
        mevr::png::Write(out / (std::string(n.name) + "-left.png"), size, size, imgL.data());
        mevr::png::Write(out / (std::string(n.name) + "-right.png"), size, size, imgR.data());
        int const half = size / 2;
        std::vector<std::uint32_t> sbs(static_cast<size_t>(size) * half);
        for (int y = 0; y < half; ++y) {
            for (int x = 0; x < half; ++x) {
                sbs[static_cast<size_t>(y) * size + x] = imgL[static_cast<size_t>(2 * y) * size + 2 * x];
                sbs[static_cast<size_t>(y) * size + half + x] = imgR[static_cast<size_t>(2 * y) * size + 2 * x];
            }
        }
        mevr::png::Write(out / (std::string(n.name) + "-sbs.png"), size, half, sbs.data());
        report << std::format("\n{}: {}\n  {} draw items: {} discs, {} rings, {} lines, {} sprites, {} labels; {} vertices, {} glyphs; "
                              "build + geometry {:.2f} ms (CPU), draw {:.3f} / {:.3f} ms (GPU, left / right){}\n",
            n.name, gh::Describe(scene), list.discs.size() + list.rings.size() + list.lines.size() + list.sprites.size() + list.labels.size(),
            list.discs.size(), list.rings.size(), list.lines.size(), list.sprites.size(), list.labels.size(), stats.vertices, stats.glyphs,
            cpuMs, gpuL, gpuR, hovered >= 0 ? std::format("; pointing at object {} ('{}')", hovered, scene.objects[hovered].label) : "");
        // The stereo check: each selectable object's light found in both images and triangulated.
        for (auto const& p : placed) {
            gh::Object const& o = scene.objects[p.index];
            float plx = 0, ply = 0, prx = 0, pry = 0;
            if (!Project(left, size, p.position, plx, ply) || !Project(right, size, p.position, prx, pry)) {
                continue;
            }
            float clx = 0, cly = 0, crx = 0, cry = 0;
            int const window = std::max(4, static_cast<int>(size * 0.006f));
            if (!Centroid(imgL, size, plx, ply, window, clx, cly) || !Centroid(imgR, size, prx, pry, window, crx, cry)) {
                report << std::format("  [{}] '{}': no light found near its place\n", p.index, o.label);
                continue;
            }
            XrVector3f const found = gh::Sub(Triangulate(left, right, size, clx, cly, crx, cry), XrVector3f{});
            float const errorMm = gh::Length(gh::Sub(found, p.position)) * 1000.0f;
            float const distance = gh::Length(gh::Sub(p.position, head.position));
            worstError = std::max(worstError, errorMm);
            report << std::format("  [{}] {:<14} at {:.2f} m: disparity {:+.1f} px; found {:+.1f}/{:+.1f} px off in L/R; triangulated {:.1f} mm "
                                  "from its place\n",
                p.index, o.label, distance, plx - prx, clx - plx, crx - prx, errorMm);
        }
    }
    report << std::format("\nworst triangulation error: {:.1f} mm\n", worstError);
    report.close();
    std::printf("wrote %s (worst triangulation error %.1f mm)\n", (out / "report.txt").string().c_str(), worstError);
    CoUninitialize();
    return 0;
}

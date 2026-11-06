// SPDX-License-Identifier: MIT
// The cinema screen's floor and horizon, offline (plugin/CinemaBackdropDraw, research/52): both eyes with the PS VR2's field of
// view, standing 1.65 m above the floor, the screen 2.9 x 1.63 m at 3.5 m drawn as a grey frame for scale, as PNGs.
//
//     mevr-cinema-preview <out folder> [--brightness b]

#include "core/PngFile.hpp"
#include "plugin/CinemaBackdropDraw.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;
namespace cb = mevr::host::cinema_backdrop_draw;

constexpr float kDeg = 3.14159265358979f / 180.0f;

XrQuaternionf YawPitch(float yawDeg, float pitchDeg) {
    float const hy = yawDeg * kDeg * 0.5f, hp = pitchDeg * kDeg * 0.5f;
    XrQuaternionf const y{0.0f, std::sin(hy), 0.0f, std::cos(hy)};
    XrQuaternionf const p{std::sin(hp), 0.0f, 0.0f, std::cos(hp)};
    return {y.w * p.x + y.x * p.w + y.y * p.z - y.z * p.y, y.w * p.y - y.x * p.z + y.y * p.w + y.z * p.x,
            y.w * p.z + y.x * p.y - y.y * p.x + y.z * p.w, y.w * p.w - y.x * p.x - y.y * p.y - y.z * p.z};
}

XrVector3f Rotate(XrQuaternionf const& q, XrVector3f const& v) {
    XrVector3f const u{q.x, q.y, q.z};
    XrVector3f const c1{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    XrVector3f const c2{u.y * c1.z - u.z * c1.y, u.z * c1.x - u.x * c1.z, u.x * c1.y - u.y * c1.x};
    return {v.x + 2.0f * (q.w * c1.x + c2.x), v.y + 2.0f * (q.w * c1.y + c2.y), v.z + 2.0f * (q.w * c1.z + c2.z)};
}

// A room point's pixel (x right, y down) in an eye's image of `w` x `h`, false behind the eye.
bool Project(cb::Eye const& eye, XrVector3f const& p, int w, int h, float& px, float& py) {
    XrQuaternionf const inv{-eye.pose.orientation.x, -eye.pose.orientation.y, -eye.pose.orientation.z, eye.pose.orientation.w};
    XrVector3f const rel{p.x - eye.pose.position.x, p.y - eye.pose.position.y, p.z - eye.pose.position.z};
    XrVector3f const v = Rotate(inv, rel);
    if (v.z >= -1e-4f) return false;
    float const x = v.x / -v.z, y = v.y / -v.z;
    float const l = std::tan(eye.fov.angleLeft), r = std::tan(eye.fov.angleRight);
    float const u = std::tan(eye.fov.angleUp), d = std::tan(eye.fov.angleDown);
    px = (x - l) / (r - l) * static_cast<float>(w);
    py = (u - y) / (u - d) * static_cast<float>(h);
    return true;
}

// The screen as a flat grey quad (convex), over the backdrop in the CPU image, so the preview shows the proportions.
void FillScreen(std::vector<std::uint32_t>& img, int w, int h, cb::Eye const& eye, XrVector3f const corners[4]) {
    float xs[4], ys[4];
    for (int i = 0; i < 4; ++i) {
        if (!Project(eye, corners[i], w, h, xs[i], ys[i])) return;
    }
    int const y0 = std::max(0, static_cast<int>(std::floor(std::min({ys[0], ys[1], ys[2], ys[3]}))));
    int const y1 = std::min(h - 1, static_cast<int>(std::ceil(std::max({ys[0], ys[1], ys[2], ys[3]}))));
    for (int y = y0; y <= y1; ++y) {
        for (int x = 0; x < w; ++x) {
            bool inside = true;
            for (int i = 0; i < 4 && inside; ++i) {
                int const j = (i + 1) % 4;
                float const cross = (xs[j] - xs[i]) * (static_cast<float>(y) - ys[i]) - (ys[j] - ys[i]) * (static_cast<float>(x) - xs[i]);
                inside = cross >= 0.0f;
            }
            if (inside) img[static_cast<size_t>(y) * w + x] = 0xFF5A5A5Au;
        }
    }
}

std::vector<std::uint32_t> Render(ID3D11Device* device, ID3D11DeviceContext* context, cb::Renderer& renderer, cb::Eye const& eye,
    cb::Scene const& scene, int w, int h) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target, staging;
    device->CreateTexture2D(&td, nullptr, target.GetAddressOf());
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    device->CreateTexture2D(&td, nullptr, staging.GetAddressOf());
    ComPtr<ID3D11RenderTargetView> rtv;
    device->CreateRenderTargetView(target.Get(), nullptr, rtv.GetAddressOf());
    renderer.Draw(context, rtv.Get(), 0, 0, w, h, eye, scene);
    context->CopyResource(staging.Get(), target.Get());
    std::vector<std::uint32_t> pixels(static_cast<size_t>(w) * h);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
        for (int y = 0; y < h; ++y) {
            std::memcpy(&pixels[static_cast<size_t>(y) * w], static_cast<std::uint8_t const*>(m.pData) + static_cast<size_t>(y) * m.RowPitch,
                static_cast<size_t>(w) * 4);
        }
        context->Unmap(staging.Get(), 0);
    }
    for (std::uint32_t& p : pixels) p |= 0xFF000000u;
    return pixels;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: mevr-cinema-preview <out folder> [--brightness b]\n");
        return 2;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // the PNG writer is WIC
    std::filesystem::path const out = argv[1];
    std::filesystem::create_directories(out);
    float brightness = 1.0f;
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--brightness") brightness = std::strtof(argv[i + 1], nullptr);
    }
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(),
            nullptr, context.GetAddressOf())) &&
        FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(),
            nullptr, context.GetAddressOf()))) {
        std::fprintf(stderr, "no D3D11 device\n");
        return 1;
    }
    cb::Renderer renderer;
    std::string error;
    if (!renderer.Init(device.Get(), &error)) {
        std::fprintf(stderr, "renderer: %s\n", error.c_str());
        return 1;
    }
    // The PS VR2's per-eye field of view as SteamVR reports it (research/37), the eyes 59 mm apart.
    XrFovf const fovL{-61.5f * kDeg, 43.4f * kDeg, 53.0f * kDeg, -53.0f * kDeg};
    XrFovf const fovR{-43.4f * kDeg, 61.5f * kDeg, 53.0f * kDeg, -53.0f * kDeg};
    float const ipd = 0.059f;
    cb::Scene scene;
    scene.floorY = -1.65f;
    scene.screenFoot = {0.0f, -1.65f, -3.5f};
    scene.screenWidthM = 2.9f;
    scene.brightness = brightness;
    float const sw = 2.9f, sh = 2.9f * 9.0f / 16.0f;
    XrVector3f const corners[4] = {{-sw / 2, sh / 2, -3.5f}, {sw / 2, sh / 2, -3.5f}, {sw / 2, -sh / 2, -3.5f}, {-sw / 2, -sh / 2, -3.5f}};
    struct View { char const* name; float yaw, pitch; };
    View const views[] = {{"ahead", 0.0f, -5.0f}, {"down-right", -40.0f, -30.0f}, {"left", 70.0f, 0.0f}};
    int const w = 1400, h = static_cast<int>(1400.0f * (std::tan(53.0f * kDeg) * 2.0f) / (std::tan(61.5f * kDeg) + std::tan(43.4f * kDeg)));
    for (View const& v : views) {
        XrQuaternionf const q = YawPitch(v.yaw, v.pitch);
        XrVector3f const rightAxis = Rotate(q, {1.0f, 0.0f, 0.0f});
        cb::Eye eyes[2];
        for (int e = 0; e < 2; ++e) {
            float const side = e == 0 ? -0.5f * ipd : 0.5f * ipd;
            eyes[e].pose.orientation = q;
            eyes[e].pose.position = {rightAxis.x * side, rightAxis.y * side, rightAxis.z * side};
            eyes[e].fov = e == 0 ? fovL : fovR;
        }
        std::vector<std::uint32_t> imgs[2];
        for (int e = 0; e < 2; ++e) {
            imgs[e] = Render(device.Get(), context.Get(), renderer, eyes[e], scene, w, h);
            FillScreen(imgs[e], w, h, eyes[e], corners);
        }
        std::vector<std::uint32_t> sbs(static_cast<size_t>(w) * 2 * h);
        for (int y = 0; y < h; ++y) {
            std::memcpy(&sbs[static_cast<size_t>(y) * w * 2], &imgs[0][static_cast<size_t>(y) * w], static_cast<size_t>(w) * 4);
            std::memcpy(&sbs[static_cast<size_t>(y) * w * 2 + w], &imgs[1][static_cast<size_t>(y) * w], static_cast<size_t>(w) * 4);
        }
        std::string pngError;
        if (!mevr::png::Write(out / (std::string(v.name) + "-sbs.png"), w * 2, h, sbs.data(), &pngError)) {
            std::fprintf(stderr, "%s: %s\n", v.name, pngError.c_str());
        }
        std::printf("%s: %dx%d per eye\n", v.name, w, h);
    }
    return 0;
}

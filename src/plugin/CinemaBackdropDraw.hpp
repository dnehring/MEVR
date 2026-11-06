// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <string>

// The cinema screen's floor and horizon (plugin/CinemaBackdrop; research/52 "A floor and a horizon"): one full-screen pass per
// eye in which every pixel's ray from that eye either meets the floor plane (a dark deck, a faint metre grid, the screen's light
// pooled at its foot, fogging into the horizon) or the sky (a dim gradient, a sparse starfield), so the floor has true stereo
// depth with no geometry. Optional textures stand in for the procedural floor (tiled) and sky (equirectangular). Writes linear
// light into an sRGB target, opaque. Used by the host's layer and by tools/cinema-preview.
namespace mevr::host::cinema_backdrop_draw {

struct Eye {
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrFovf fov{-0.785f, 0.785f, 0.785f, -0.785f};
};

struct Scene {
    float floorY = -1.6f;                           // the floor's height in the eyes' space (meters)
    XrVector3f screenFoot{0.0f, -1.6f, -3.5f};      // the floor under the screen's centre (the light pool)
    float screenYaw = 0.0f;                         // the screen's heading (radians): the floor texture's axes
    float screenWidthM = 2.9f;                      // the pool's size follows the screen
    float brightness = 1.0f;                        // 1: the default dimness
    float fade = 1.0f;                              // 0..1: the fade in
    float timeS = 0.0f;
};

class Renderer {
public:
    bool Init(ID3D11Device* device, std::string* error);
    bool Ready() const { return ready_; }
    // The game's textures (or null for the procedural look): the floor tiled every `floorTileM`, the sky an equirectangular map.
    void SetTextures(ID3D11ShaderResourceView* floor, float floorTileM, ID3D11ShaderResourceView* sky);
    // Draws `width` x `height` pixels of the target at (x, y) (opaque) as `eye` sees the scene.
    void Draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, int x, int y, int width, int height, Eye const& eye,
        Scene const& scene);

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> floor_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sky_;
    float floorTileM_ = 2.0f;
    bool ready_ = false;
};

} // namespace mevr::host::cinema_backdrop_draw

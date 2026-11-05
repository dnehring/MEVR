// SPDX-License-Identifier: MIT
#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <string>

// A bloom for our holographic quads (first the power wheel, research/55): what is bright in a canvas glows. The
// canvas is ImGui's output, premultiplied sRGB-encoded RGBA8 over a clear background, as our quads take it. Its bright
// parts (by their brightest channel, so saturated blue and orange count) are blurred at half and at quarter size and
// added to its colour, alpha unchanged: in a premultiplied layer that is additive light, so the glow brightens what
// is behind the quad instead of covering it. Runs inside an ID3DDeviceContextState of its own (swapped in and back
// out), so the game's pipeline state is untouched. Used the same way by the host and by tools/power-wheel-preview.
namespace mevr::host::glow {

struct Settings {
    float threshold = 0.30f;  // brightest channel below which nothing glows (it fades in up to 1)
    float nearAmount = 0.85f; // the half-size blur's share: a tight halo
    float farAmount = 0.75f;  // the quarter-size blur's: the wide haze
    float spread = 1.0f;      // scales both blurs' reach
};

class Glow {
public:
    bool Initialize(ID3D11Device* device, std::string* error = nullptr);
    void Shutdown();
    bool Ready() const { return device_ != nullptr; }

    // `source`: the canvas (width x height); `target`: where the result goes, the same size, a UNORM view (the values
    // stay sRGB-encoded, as the canvas's). `source` and `target` must be different textures.
    bool Apply(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, unsigned width, unsigned height,
        ID3D11RenderTargetView* target, Settings const& settings = {});

private:
    struct Level {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture[2];
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv[2];
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv[2];
        unsigned width = 0;
        unsigned height = 0;
    };
    bool EnsureLevels(unsigned width, unsigned height);
    bool EnsureLevel(Level& level, unsigned width, unsigned height);
    void Pass(ID3D11DeviceContext* context, ID3D11PixelShader* shader, ID3D11ShaderResourceView* const* inputs, unsigned inputCount,
        ID3D11RenderTargetView* target, unsigned width, unsigned height, float const (&constants)[8]);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> state_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> prefilter_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> down_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> blur_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> composite_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
    Level half_;
    Level quarter_;
};

}  // namespace mevr::host::glow

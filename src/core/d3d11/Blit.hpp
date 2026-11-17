// SPDX-License-Identifier: MIT
#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

namespace mevr::d3d11 {

// Copies a texture the game owns (e.g. the backbuffer, which usually can't be sampled) into an
// internal shader-readable copy, then draws it into a render target. Drawing happens on the
// game's immediate context inside our own ID3DDeviceContextState, swapped in and back out, so
// the game's pipeline state is untouched. (Deferred contexts aren't an option: ME LE creates its
// device single-threaded.)
class Blit {
public:
    bool Initialize(ID3D11Device* device);
    void Shutdown();

    // `target` should be an _SRGB view: output is linear. Sources in plain UNORM formats are
    // assumed to hold sRGB-encoded values (typical SDR backbuffer) and are decoded first.
    // `shiftX` != 0: drawn a second time over the first, moved that many pixels right (left if negative), as Halcyon's
    // ME2 renderer applies its convergence to an eye (the vacated strip keeps the unshifted picture).
    // False when resources/context cannot support the draw; callers must not mark that image ready.
    bool Draw(ID3D11DeviceContext* immediate, ID3D11Texture2D* source, ID3D11RenderTargetView* target,
        UINT targetWidth, UINT targetHeight, int shiftX = 0);
    // The same with a pixel shader of the caller's (made on this device; it reads t0 through s0, the vertex shader's
    // TEXCOORD0 uv and SV_Position) and its constant buffer at b1 (may be null): no sRGB decode of our own.
    bool DrawWith(ID3D11DeviceContext* immediate, ID3D11Texture2D* source, ID3D11RenderTargetView* target,
        UINT targetWidth, UINT targetHeight, int shiftX, ID3D11PixelShader* pixelShader, ID3D11Buffer* constantsB1);

    // Several passes of the caller's own (the eye upscale: plugin/Upscale): `source` is staged as for Draw, our pipeline
    // state swapped in with the triangle list, no input layout and the default raster, blend and depth states, and
    // `passes` draws with `stage` (it binds its own shaders, targets, viewports and constant buffers; `source` is the
    // staged copy's view, `sampler` linear and clamping). Afterwards t0, the render targets and the pixel shader's
    // b0..b2 are unbound and the game's state comes back. False when the stage couldn't be set up (nothing drawn) or
    // `passes` returned false.
    struct Stage {
        ID3D11DeviceContext* context = nullptr;
        ID3D11ShaderResourceView* source = nullptr;
        ID3D11SamplerState* sampler = nullptr;
    };
    bool Run(ID3D11DeviceContext* immediate, ID3D11Texture2D* source, bool (*passes)(Stage const& stage, void* user),
        void* user);

private:
    bool DrawWithShader(ID3D11DeviceContext* immediate, ID3D11Texture2D* source, ID3D11RenderTargetView* target,
        UINT targetWidth, UINT targetHeight, int shiftX, ID3D11PixelShader* pixelShader, ID3D11Buffer* constantsB1);
    bool EnsureCopy(ID3D11Texture2D* source);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> state_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psPassthrough_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psDecodeSrgb_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> copy_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> copySrv_;
    D3D11_TEXTURE2D_DESC copyDesc_{};
    bool sourceIsMultisampled_ = false;
    bool decodeSrgb_ = false;
};

// True for UNORM formats whose contents are conventionally sRGB-encoded by the game.
bool IsGammaEncodedUnorm(DXGI_FORMAT format);
char const* FormatName(DXGI_FORMAT format);

} // namespace mevr::d3d11

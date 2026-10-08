#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace mevr::d3d11 {

// Copies a texture the game owns (e.g. the backbuffer, which usually can't be sampled) into an
// internal shader-readable copy, then draws it into a render target. Commands are recorded on a
// deferred context and executed with state restore, so the game's immediate-context state is
// untouched.
class Blit {
public:
    bool Initialize(ID3D11Device* device);
    void Shutdown();

    // `target` should be an _SRGB view: output is linear. Sources in plain UNORM formats are
    // assumed to hold sRGB-encoded values (typical SDR backbuffer) and are decoded first.
    void Draw(ID3D11DeviceContext* immediate, ID3D11Texture2D* source, ID3D11RenderTargetView* target,
        UINT targetWidth, UINT targetHeight);

private:
    bool EnsureCopy(ID3D11Texture2D* source);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred_;
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

#include "plugin/menu/MenuStrip.hpp"

#include "core/vr/ColourStrip.hpp"

#include <d3d11.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace mevr::menu {
namespace {

namespace cv = vr::colour;

bool Texture(ID3D11Device* device, std::vector<std::uint32_t> const& pixels, Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kStripWidth;
    td.Height = kStripHeight;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // sRGB-encoded bytes, as RmlUi's colours are
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(), kStripWidth * 4u, 0};
    return SUCCEEDED(device->CreateTexture2D(&td, &data, texture.GetAddressOf())) &&
        SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf()));
}

bool Same(cv::Settings const& a, cv::Settings const& b) {
    return std::memcmp(&a, &b, sizeof a) == 0;
}

} // namespace

bool StripPair::Init(ID3D11Device* device) {
    if (ready_) {
        return true;
    }
    cv::Strip const strip = cv::TestStrip(kStripWidth, kStripHeight);
    if (device == nullptr || strip.width != kStripWidth || strip.height != kStripHeight || !Texture(device, strip.pixels, original_, originalSrv_) ||
        !Texture(device, strip.pixels, adjusted_, adjustedSrv_)) {
        return false;
    }
    ready_ = true;
    return true;
}

void StripPair::Update(ID3D11DeviceContext* context, cv::Settings const& settings) {
    if (!ready_ || context == nullptr || (shown_ && Same(settings, settings_))) {
        return;
    }
    shown_ = true;
    settings_ = settings;
    cv::Strip const strip = cv::TestStrip(kStripWidth, kStripHeight);
    std::vector<std::uint32_t> pixels = strip.pixels;
    if (!cv::IsNeutral(settings)) {
        cv::Constants const k = cv::MakeConstants(cv::Clamped(settings), 0.0f, false, 0);
        for (int y = 0; y < kStripHeight; ++y) {
            for (int x = 0; x < kStripWidth; ++x) {
                std::uint32_t& p = pixels[static_cast<std::size_t>(y) * kStripWidth + x];
                cv::Rgb const in{static_cast<float>(p & 0xFF) / 255.0f, static_cast<float>((p >> 8) & 0xFF) / 255.0f,
                    static_cast<float>((p >> 16) & 0xFF) / 255.0f};
                cv::Rgb const out = cv::Correct(k, in, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), false);
                p = 0xFF000000u | (static_cast<std::uint32_t>(cv::ToUnorm8(out[2])) << 16) |
                    (static_cast<std::uint32_t>(cv::ToUnorm8(out[1])) << 8) | cv::ToUnorm8(out[0]);
            }
        }
    }
    context->UpdateSubresource(adjusted_.Get(), 0, nullptr, pixels.data(), kStripWidth * 4u, 0);
}

} // namespace mevr::menu

#pragma once

#include "core/vr/Colour.hpp"

#include <wrl/client.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

// The Display page's preview (research/27 and 28): the colour test strip (core/vr/ColourStrip) as the game makes it
// and through the correction's CPU reference (core/vr/Colour Correct, no dither), as the textures
// "mevr:strip_original" and "mevr:strip_adjusted". Remade only when the settings change. Render thread.
namespace mevr::menu {

inline constexpr int kStripWidth = 400;
inline constexpr int kStripHeight = 224;

class StripPair {
public:
    bool Init(ID3D11Device* device);
    // The adjusted strip for `settings` (no-op if they're what it shows already).
    void Update(ID3D11DeviceContext* context, vr::colour::Settings const& settings);
    ID3D11ShaderResourceView* Original() const { return originalSrv_.Get(); }
    ID3D11ShaderResourceView* Adjusted() const { return adjustedSrv_.Get(); }

private:
    bool ready_ = false;
    bool shown_ = false;
    vr::colour::Settings settings_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> original_, adjusted_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> originalSrv_, adjustedSrv_;
};

} // namespace mevr::menu

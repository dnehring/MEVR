#pragma once

#include <RmlUi_Renderer_DX11.h>

#include <map>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;

namespace Rml {
class Context;
}

// The main menu's Direct3D 11 renderer (research/28): RmlUi's own DX11 render interface (Backends, MIT: geometry,
// textures, scissor and stencil clip masks, transforms, layers and render-to-texture, the filters blur, drop-shadow,
// opacity, colour matrices and mask images, the gradient and creation shaders), on the game's device and its
// immediate context, which is SINGLETHREADED: call it only on the render thread (the host's Present callback).
//
// What we add on top:
//   - every piece of pipeline state it may touch is saved before and put back after (RmlUi's own backup misses the
//     render targets, the second constant buffer and texture slot, and leaves a bound geometry shader active);
//   - textures RmlUi asks for as "mevr:<name>" (an <img src> or a decorator's image) are ours, registered with
//     SetExternalTexture and updated in place (the blurred backdrop, the colour test strip);
//   - the time each frame took, on the CPU.
namespace mevr::menu {

class Renderer final : public RenderInterface_DX11 {
public:
    explicit Renderer(ID3D11Device* device);
    ~Renderer() override;
    Renderer(Renderer const&) = delete;
    Renderer& operator=(Renderer const&) = delete;

    // A texture of ours that RmlUi finds as "mevr:<name>". The view must stay alive (and keep its size) as long as the
    // renderer does; its contents may change at any time. Register before the document that uses it is loaded.
    void SetExternalTexture(std::string const& name, ID3D11ShaderResourceView* view, int width, int height);

    // Renders `context` (Update() already called) into `target`, a `width` x `height` RGBA8 render target that ends up
    // holding premultiplied, sRGB-encoded colour over transparent black. All state is restored afterwards.
    void Render(ID3D11DeviceContext* immediate, Rml::Context* context, ID3D11RenderTargetView* target, int width, int height);

    // The CPU time of the last Render (milliseconds).
    double LastRenderMs() const { return lastMs_; }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, Rml::String const& source) override;

private:
    struct External {
        ID3D11ShaderResourceView* view = nullptr;
        int width = 0;
        int height = 0;
    };
    std::map<std::string, External> external_;
    double lastMs_ = 0.0;
};

} // namespace mevr::menu

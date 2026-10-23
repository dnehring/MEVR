// SPDX-License-Identifier: MIT
#include "plugin/menu/MenuRenderer.hpp"

#include "plugin/menu/StateGuard.hpp"

#include <RmlUi/Core/Context.h>

#include <Windows.h>
#include <d3d11.h>

#include <chrono>

namespace mevr::menu {

Renderer::Renderer(ID3D11Device* device) : RenderInterface_DX11(device) {}

Renderer::~Renderer() {
    for (auto& [name, texture] : external_) {
        if (texture.view != nullptr) {
            texture.view->Release();
        }
    }
}

void Renderer::SetExternalTexture(std::string const& name, ID3D11ShaderResourceView* view, int width, int height) {
    External& slot = external_[name];
    if (view != nullptr) {
        view->AddRef();
    }
    if (slot.view != nullptr) {
        slot.view->Release();
    }
    slot = {view, width, height};
}

Rml::TextureHandle Renderer::LoadTexture(Rml::Vector2i& dimensions, Rml::String const& source) {
    constexpr char kPrefix[] = "mevr:";
    if (source.rfind(kPrefix, 0) == 0) {
        auto const it = external_.find(source.substr(sizeof kPrefix - 1));
        if (it == external_.end() || it->second.view == nullptr) {
            return Rml::TextureHandle(0);
        }
        it->second.view->AddRef();  // RmlUi releases its handle (ReleaseTexture) when it's done with it
        dimensions = {it->second.width, it->second.height};
        return reinterpret_cast<Rml::TextureHandle>(it->second.view);
    }
    return RenderInterface_DX11::LoadTexture(dimensions, source);
}

void Renderer::Render(ID3D11DeviceContext* immediate, Rml::Context* context, ID3D11RenderTargetView* target, int width,
    int height) {
    if (immediate == nullptr || context == nullptr || target == nullptr || width <= 0 || height <= 0) {
        return;
    }
    auto const start = std::chrono::steady_clock::now();
    {
        StateGuard const guard(immediate);
        float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        immediate->ClearRenderTargetView(target, clear);
        SetViewport(width, height);
        BeginFrame();
        context->Render();
        EndFrame(target);
    }
    lastMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace mevr::menu

// SPDX-License-Identifier: MIT
#pragma once

#include <d3d11.h>

// Saves every piece of the immediate context's state the menu's passes may change (RmlUi's renderer, the backdrop's
// passes) and puts it back when it goes out of scope; while alive, the geometry, hull and domain stages are off.
// The game's immediate context is shared with the game's renderer and Halcyon's: UE3 rebinds most state per draw,
// but not all of it.
namespace mevr::menu {

inline constexpr UINT kGuardSlots = 2;  // constant buffers b0-b1, textures t0-t1, sampler s0
inline constexpr UINT kGuardViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

class StateGuard {
public:
    explicit StateGuard(ID3D11DeviceContext* c) : c_(c) {
        c_->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_, &dsv_);
        c_->OMGetBlendState(&blend_, blendFactor_, &sampleMask_);
        c_->OMGetDepthStencilState(&depth_, &stencilRef_);
        c_->RSGetState(&raster_);
        viewportCount_ = kGuardViewports;
        c_->RSGetViewports(&viewportCount_, viewports_);
        scissorCount_ = kGuardViewports;
        c_->RSGetScissorRects(&scissorCount_, scissors_);
        c_->IAGetInputLayout(&layout_);
        c_->IAGetPrimitiveTopology(&topology_);
        c_->IAGetVertexBuffers(0, 1, &vb_, &vbStride_, &vbOffset_);
        c_->IAGetIndexBuffer(&ib_, &ibFormat_, &ibOffset_);
        c_->VSGetShader(&vs_, nullptr, nullptr);
        c_->VSGetConstantBuffers(0, kGuardSlots, vsCb_);
        c_->PSGetShader(&ps_, nullptr, nullptr);
        c_->PSGetConstantBuffers(0, kGuardSlots, psCb_);
        c_->PSGetShaderResources(0, kGuardSlots, psSrv_);
        c_->PSGetSamplers(0, 1, &sampler_);
        c_->GSGetShader(&gs_, nullptr, nullptr);
        c_->HSGetShader(&hs_, nullptr, nullptr);
        c_->DSGetShader(&ds_, nullptr, nullptr);
        c_->GSSetShader(nullptr, nullptr, 0);
        c_->HSSetShader(nullptr, nullptr, 0);
        c_->DSSetShader(nullptr, nullptr, 0);
    }
    ~StateGuard() {
        // Our textures off the inputs first, so a target of ours still bound there can't block one of the game's.
        ID3D11ShaderResourceView* const none[kGuardSlots] = {};
        c_->PSSetShaderResources(0, kGuardSlots, none);
        c_->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_, dsv_);
        c_->OMSetBlendState(blend_, blendFactor_, sampleMask_);
        c_->OMSetDepthStencilState(depth_, stencilRef_);
        c_->RSSetState(raster_);
        c_->RSSetViewports(viewportCount_, viewports_);
        c_->RSSetScissorRects(scissorCount_, scissors_);
        c_->IASetInputLayout(layout_);
        c_->IASetPrimitiveTopology(topology_);
        c_->IASetVertexBuffers(0, 1, &vb_, &vbStride_, &vbOffset_);
        c_->IASetIndexBuffer(ib_, ibFormat_, ibOffset_);
        c_->VSSetShader(vs_, nullptr, 0);
        c_->VSSetConstantBuffers(0, kGuardSlots, vsCb_);
        c_->PSSetShader(ps_, nullptr, 0);
        c_->PSSetConstantBuffers(0, kGuardSlots, psCb_);
        c_->PSSetShaderResources(0, kGuardSlots, psSrv_);
        c_->PSSetSamplers(0, 1, &sampler_);
        c_->GSSetShader(gs_, nullptr, 0);
        c_->HSSetShader(hs_, nullptr, 0);
        c_->DSSetShader(ds_, nullptr, 0);
        for (auto*& r : rtvs_) {
            Release(r);
        }
        Release(dsv_);
        Release(blend_);
        Release(depth_);
        Release(raster_);
        Release(layout_);
        Release(vb_);
        Release(ib_);
        Release(vs_);
        Release(ps_);
        Release(gs_);
        Release(hs_);
        Release(ds_);
        Release(sampler_);
        for (UINT i = 0; i < kGuardSlots; ++i) {
            Release(vsCb_[i]);
            Release(psCb_[i]);
            Release(psSrv_[i]);
        }
    }
    StateGuard(StateGuard const&) = delete;
    StateGuard& operator=(StateGuard const&) = delete;

private:
    template <typename T>
    static void Release(T*& p) {
        if (p != nullptr) {
            p->Release();
            p = nullptr;
        }
    }
    ID3D11DeviceContext* c_;
    ID3D11RenderTargetView* rtvs_[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* dsv_ = nullptr;
    ID3D11BlendState* blend_ = nullptr;
    FLOAT blendFactor_[4] = {};
    UINT sampleMask_ = 0;
    ID3D11DepthStencilState* depth_ = nullptr;
    UINT stencilRef_ = 0;
    ID3D11RasterizerState* raster_ = nullptr;
    UINT viewportCount_ = 0;
    D3D11_VIEWPORT viewports_[kGuardViewports] = {};
    UINT scissorCount_ = 0;
    D3D11_RECT scissors_[kGuardViewports] = {};
    ID3D11InputLayout* layout_ = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology_ = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer* vb_ = nullptr;
    UINT vbStride_ = 0;
    UINT vbOffset_ = 0;
    ID3D11Buffer* ib_ = nullptr;
    DXGI_FORMAT ibFormat_ = DXGI_FORMAT_UNKNOWN;
    UINT ibOffset_ = 0;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11Buffer* vsCb_[kGuardSlots] = {};
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11Buffer* psCb_[kGuardSlots] = {};
    ID3D11ShaderResourceView* psSrv_[kGuardSlots] = {};
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11GeometryShader* gs_ = nullptr;
    ID3D11HullShader* hs_ = nullptr;
    ID3D11DomainShader* ds_ = nullptr;
};

} // namespace mevr::menu

// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/GalaxyHologram.hpp"

#include <openxr/openxr.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Draws the galaxy hologram's draw list (core/vr/GalaxyHologram Build) for one eye with D3D11: the host into its projection
// layer (plugin/GalaxyHologram), tools/galaxy-preview into PNGs. Light is additive (alpha 0) except the labels and their
// plates, so the target is premultiplied: clear it to transparent for a layer, or to a background for a picture. The target
// should be sRGB (the shader writes linear light). Labels use a TrueType font baked with stb_truetype.
namespace mevr::host::galaxy_draw {

struct Eye {
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrFovf fov{-0.785f, 0.785f, 0.785f, -0.785f};
};

struct Stats {
    unsigned vertices = 0;
    unsigned glyphs = 0;
};

class Renderer {
public:
    // Compiles the shaders and bakes the font (`fontFile`, a TTF; empty or missing: the labels are skipped).
    bool Init(ID3D11Device* device, std::filesystem::path const& fontFile, std::string* error);
    bool Ready() const { return ready_; }
    bool HasFont() const { return fontReady_; }

    // Builds the frame's geometry once for both eyes: billboards and ribbons face `head` (the eyes' centre), so both eyes see
    // the same shapes. Call before Draw each frame.
    Stats Prepare(vr::galaxy_holo::DrawList const& list, XrPosef const& head);
    // Draws the prepared frame into `target` (width x height pixels) as seen by `eye`.
    void Draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, int width, int height, Eye const& eye, float timeS);

    // The eye's view-projection, rows of the matrix that takes a room-space point (x, y, z, 1) to clip space (D3D, z 0..1).
    static void ViewProjection(Eye const& eye, float nearM, float farM, float rows[4][4]);

private:
    struct Vertex {
        float position[3];
        float uv[2];
        float colour[4];
        float extra[4];  // x: style; yzw: per style
    };
    struct Glyph {
        float x0, y0, x1, y1;  // quad in font pixels, y down from the baseline
        float u0, v0, u1, v1;
        float advance;
    };

    bool BakeFont(ID3D11Device* device, std::filesystem::path const& fontFile, std::string* error);
    void AddQuad(XrVector3f const& c, XrVector3f const& right, XrVector3f const& up, float halfW, float halfH, float const colour[4],
                 float style, float e1 = 0.0f, float e2 = 0.0f, float e3 = 0.0f);
    void AddRibbon(std::vector<XrVector3f> const& points, bool closed, float widthM, float const colour[4], XrVector3f const& eye);
    float TextWidth(std::string const& text) const;
    unsigned AddText(std::string const& text, XrVector3f const& anchor, XrVector3f const& right, XrVector3f const& up, float heightM,
                     float const colour[4]);

    bool ready_ = false;
    bool fontReady_ = false;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer_;
    unsigned vertexCapacity_ = 0;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> atlas_;
    std::vector<Glyph> glyphs_;  // code points 32..255
    float fontPixels_ = 0.0f;    // the baked size: the line's ascent to descent
    float fontAscent_ = 0.0f;
    std::vector<Vertex> vertices_;
};

} // namespace mevr::host::galaxy_draw

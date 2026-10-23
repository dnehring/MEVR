// SPDX-License-Identifier: MIT
#pragma once

#include <wrl/client.h>

#include <cstdint>

struct ID3D11Buffer;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11PixelShader;
struct ID3D11RenderTargetView;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct ID3D11VertexShader;

// The menu's frosted glass (research/28): what's behind the panel, blurred, darkened and desaturated, as the
// texture "mevr:backdrop" the document's root draws (decorator: backdrop(...)). Not a true backdrop filter: the
// world is sampled from one eye's image a few times a second, which at the cost of a few small passes looks the
// same for a panel fixed in the room.
//   Capture: an eye image (its swapchain image, right before Halcyon releases it) downsampled into a copy of our
//            own, at most kCaptureMax pixels across.
//   Compose: each backdrop pixel is a point on the panel; where that point is in the captured eye image (the eye's
//            pose and FOV as that image was rendered), blurred (two separable Gaussian passes), then graded.
// Render thread (the game's immediate context); all state it touches is restored. No OpenXR here.
namespace mevr::menu {

inline constexpr unsigned kBackdropWidth = 480;   // a third of the menu's 1440 x 900
inline constexpr unsigned kBackdropHeight = 300;
inline constexpr unsigned kCaptureMax = 960;

// The panel in the captured eye's view space (meters; OpenXR axes: +x right, +y up, -z ahead), and that eye's FOV.
struct BackdropView {
    float center[3] = {0.0f, 0.0f, -1.2f};
    float right[3] = {1.0f, 0.0f, 0.0f};   // half the panel's width along its +x
    float up[3] = {0.0f, 1.0f, 0.0f};      // half its height along its +y
    float tanLeft = -1.0f;                 // tan(angleLeft), negative
    float tanRight = 1.0f;
    float tanUp = 1.0f;
    float tanDown = -1.0f;                 // negative
};

class Backdrop {
public:
    Backdrop();
    ~Backdrop();
    Backdrop(Backdrop const&) = delete;
    Backdrop& operator=(Backdrop const&) = delete;

    // Shaders and targets. The backdrop starts as a dark gradient (no world yet). False: failed (logged by the caller).
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context);
    bool Ready() const { return ready_; }

    // `source` (a view of an eye image), its `rect` (x, y, width, height pixels) in a `width` x `height` texture.
    // `decodes`: the view returns linear values (an _SRGB view): they're encoded again.
    bool Capture(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, int const rect[4], unsigned width,
        unsigned height, bool decodes);
    // The panel's backdrop from the last capture. False: nothing captured yet.
    bool Compose(ID3D11DeviceContext* context, BackdropView const& view);
    // Fill with the plain dark gradient (no world to show: a flat screen, a loading screen).
    void Clear(ID3D11DeviceContext* context);

    ID3D11ShaderResourceView* View() const;
    bool Captured() const { return captured_; }

private:
    bool ready_ = false;
    bool captured_ = false;
    unsigned captureW_ = 0;
    unsigned captureH_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> capturePs_, projectPs_, blurPs_, gradePs_, clearPs_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> capture_, work_[2], final_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> captureRtv_, workRtv_[2], finalRtv_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> captureSrv_, workSrv_[2], finalSrv_;
};

} // namespace mevr::menu

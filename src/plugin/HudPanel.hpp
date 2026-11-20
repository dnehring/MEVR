// SPDX-License-Identifier: MIT
#pragma once

// The body-locked HUD panel's core, game-independent (both Halcyon hosts): the panel texture the renderer's UI draws go
// into instead of the eye images (the colour of Scaleform's "over" draws, and a tint texture for its other blend modes),
// the blit of a window of it into one of our quad swapchains (box-filtered, colour-corrected, masked), the panel's own
// swapchain, and its placement in front of the body (core/vr/OverlayPlacement). Moved out of plugin/HalcyonOverlays.cpp
// unchanged (research/le2-renderer-integration.md "Step 4"): LE1's HalcyonOverlays builds its wheels, labels and HUD
// pieces on top of it; LE2's bridge shows the panel itself (Layer below).
//
// The panel texture holds premultiplied colour and coverage from the UI's "over" draws; Scaleform's other blend modes
// (multiply, screen) add a tint texture (white = the world shows unchanged) that the panel's alpha is made from.

#include "core/vr/OverlayPlacement.hpp"
#include "plugin/QuadSwapchain.hpp"

#include <cstdint>

namespace mevr::host::hud_panel {

// ---- Render thread: the renderer's UI draw seams ----------------------------------------------------------------------
// The next UI draws go to the panel (set at the end of each frame by the Present thread).
bool Armed() noexcept;
// The panel's render target for a UI draw into a width x height target of `format` (made, or remade for a new size;
// nullptr if it can't be, or if this frame already drew into a panel of another kind).
ID3D11RenderTargetView* Target(ID3D11DeviceContext* context, unsigned width, unsigned height, int format) noexcept;
// The game's "over" blend state with alpha accumulated too (the panel's coverage).
ID3D11BlendState* Blend(ID3D11DeviceContext* context, ID3D11BlendState* game) noexcept;
// For Scaleform's other blend modes: the colour pass into the panel and the tint pass (nullptr: no effect behind).
void TintBlends(ID3D11DeviceContext* context, ID3D11BlendState* game, ID3D11BlendState** colour, ID3D11BlendState** tint) noexcept;
ID3D11RenderTargetView* TintTarget() noexcept;  // call after Target
ID3D11DepthStencilState* NoWrites(ID3D11DeviceContext* context, ID3D11DepthStencilState* game) noexcept;
void NoteDraw(bool beforeEyeCopy, bool textBatch) noexcept;
void NoteTint() noexcept;
bool IsFloat() noexcept;

// One UI draw the renderer has classified as UI (a blended full-frame draw into its UI target, the immediate context):
// "over" draws (DestBlend INV_SRC_ALPHA) and Scaleform's blend modes (DestBlendAlpha DEST_ALPHA) go into the panel
// (`draw(user)` is called with the panel bound, once more into the tint for a blend mode), anything else is left alone.
// True if the draw went to the panel (the caller then skips its own). For a renderer without LE1's draw seam (LE2's
// d3d_capture.cpp: MevrBridge::HudPanelDraw). `afterLeftEye` false: a UI draw before the renderer captured its left eye
// (not the frame's end): counted and left to the renderer, as LE1's draws before its eye copy.
bool Redirect(ID3D11DeviceContext* context, void (*draw)(void*), void* user, bool textBatch, bool afterLeftEye = true) noexcept;

// ---- Present thread ---------------------------------------------------------------------------------------------------
void SetArmed(bool armed) noexcept;
struct Draws {
    unsigned draws = 0, text = 0, early = 0, earlyText = 0;
};
Draws TakeDraws() noexcept;                 // this frame's counts, reset
unsigned TakeTintDraws() noexcept;          // this frame's tint passes, reset
void NoteDrawsLastFrame(unsigned draws) noexcept;
unsigned DrawsLastFrame() noexcept;

unsigned Width() noexcept;
unsigned Height() noexcept;
bool HasTexture() noexcept;                 // made (UI drew into it)
bool Ready() noexcept;                      // made with its tint: windows of it can be blitted
bool HasTarget() noexcept;                  // its render target exists (cleared at a frame's end)
bool Usable() noexcept;                     // its texture, swapchain and pipeline haven't failed
ID3D11Texture2D* Texture() noexcept;        // for a readback probe

// The colour correction of the game's UI (LE1's plugin/Colour, research/27): its shader prelude (compiled in front of
// the panel shader, once, at the first blit: set before it) and the begin/end around each blit. Unset: uncorrected.
struct ColourHooks {
    char const* prelude = nullptr;
    bool (*begin)(ID3D11DeviceContext*, std::uint32_t seed) = nullptr;
    void (*end)(ID3D11DeviceContext*) = nullptr;
};
void SetColour(ColourHooks const& hooks) noexcept;

// A window of the panel texture (source pixels).
struct Window {
    UINT x = 0;
    UINT y = 0;
    UINT width = 0;
    UINT height = 0;
};
struct Mask {
    INT x0 = 0;
    INT y0 = 0;
    INT x1 = 0;  // x1 <= x0: none
    INT y1 = 0;
};
// Up to three rectangles cut out of a blit (a wheel or the interaction prompt, the wrist piece, the gun piece).
struct Masks {
    static constexpr unsigned kMax = 3;
    Mask rects[kMax];
    unsigned count = 0;
    void Add(UINT x, UINT y, UINT width, UINT height) {
        if (count < kMax && width > 0 && height > 0) {
            rects[count++] = {static_cast<INT>(x), static_cast<INT>(y), static_cast<INT>(x + width), static_cast<INT>(y + height)};
        }
    }
};
struct BlitOptions {
    float backdrop = 0.0f;                          // a screen standing in the room: its dark parts' opacity (0: as it is)
    ID3D11ShaderResourceView* figure = nullptr;     // a picture behind the UI (LE1's UI world, research/57 section 7)
    float figureRect[4] = {};                       // its origin (source pixels), 1/width, 1/height
    float figureOpacity = 0.0f;
    float figureGamma = 1.0f;
    std::uint32_t colourSeed = 3;                   // the colour correction's dither pattern (2 the HUD panel, 3 the rest)
    // Called with the image written (before its release) and the panel texture: a readback probe ([MENUVIS]).
    void (*probe)(ID3D11DeviceContext*, ID3D11Texture2D* image, ID3D11Texture2D* panel) = nullptr;
};
// One blit of a window of the panel texture (and its tint) into `swapchain`, `factor` times smaller; pixels inside any of
// `masks` come out transparent. Returns the image written (released), or nullptr.
ID3D11Texture2D* Blit(ID3D11DeviceContext* context, ID3D11Device* device, QuadSwapchain& swapchain, Window const& window,
    UINT factor, Masks const& masks, float whiteScale, BlitOptions const& options) noexcept;
// The most opaque pixel in each cell of a columns x rows grid over the whole panel (core/vr/WheelCrop's measurement),
// drawn into `target`. False if it couldn't be.
bool DrawCoverageGrid(ID3D11DeviceContext* context, ID3D11Device* device, ID3D11RenderTargetView* target, UINT columns,
    UINT rows) noexcept;

// The panel's own swapchain ("hud" in snapshots) and its scale factor (1/factor of the panel texture).
QuadSwapchain& Swapchain() noexcept;
UINT Factor() noexcept;
// The whole panel into its swapchain (`options.probe` used once, on the first frame with UI draws). False if not drawn.
bool Render(ID3D11DeviceContext* context, ID3D11Device* device, XrSession session, float whiteScale, Masks const& masks,
    BlitOptions const& options) noexcept;

// After the frame's layers: the panel cleared for the next frame's UI (when it had draws, after an arm change, or a clear
// asked for), unless `keepUntilNextUi` (the galaxy map's window: the last complete image stays until the next UI starts,
// research/51 section 8). Logs the first frame with tint draws.
void EndFrame(ID3D11DeviceContext* context, unsigned draws, unsigned tintDraws, bool keepUntilNextUi) noexcept;
void NoteArmChange() noexcept;              // the next EndFrame clears
unsigned TakeClears() noexcept;             // clears made before a UI draw (keepUntilNextUi) since the last call

// Placement: the body-locked panel in front of the player, following the head past `followAfter`.
mevr::vr::overlay::Panel& Placement() noexcept;
void Recentered() noexcept;                 // any thread: placed straight ahead again at the next frame
void RequestReplace() noexcept;             // any thread: placed where the head looks at the next frame
// Updates the placement for this frame (recenter, re-place, follow), with its log lines.
void Place(XrQuaternionf const& head, bool headValid, mevr::vr::overlay::PanelSettings const& settings, float followDeg,
    float dt) noexcept;

// The session changed: the panel's swapchain went with the old one.
void ForgetSession() noexcept;

// ---- The plain body-locked panel, one call a frame (LE2's bridge) -----------------------------------------------------
struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    bool worldFrame = false;     // a head-tracked world frame (not a flat panel)
    bool gameplay = false;       // a game mode whose UI belongs on the panel
    bool stereo = false;         // the renderer's stereo mode leaves the frame HUD-free (LE1's Stereo 2, ME2's SFR)
    bool headValid = false;
    XrPosef head{};              // in appSpace
    float whiteScale = 1.0f;     // a float (scRGB) panel: the multiplier that maps SDR white to 1.0
    // The settings (renderer::Hud(): hudPanel, distance, drop, follow, width).
    bool enabled = true;
    float distanceM = 1.0f;
    float dropM = 0.0f;
    float followDeg = 0.0f;
    float widthM = 1.0f;
    Masks masks;                 // cut out of the panel: the HUD pieces shown on the body (plugin/HudPieceLayers)
    // A game screen of ours owns the panel (ME2's screens on the wrist, plugin/MenusLe2; LE1's HalcyonOverlays passes it to
    // Render itself): the panel's dark parts at this opacity (BlitOptions::backdrop). 0: the HUD as it is.
    float backdrop = 0.0f;
    // Called after the panel was drawn (`shown`: its quad goes out) and before it's cleared for the next frame's UI: where
    // windows of this frame's UI are blitted onto quads of their own (the HUD pieces). Null: nothing.
    void (*beforeClear)(void* user, bool shown) = nullptr;
    void* user = nullptr;
    // The 3D cinema screen is up and its UI stands on a layer of its own (research/52 "Cross-eyed", [Cinematics] uiLayer;
    // LE1's HalcyonOverlays screen branch): the game's UI (the dialogue wheel, subtitles, ME2's interrupt prompt) goes to the
    // panel in any frame of it (not only gameplay's world frames) and its part `screenPart` (u0, v0, u1, v1 of the panel: the
    // columns and rows the screen shows) is shown at `screenPose` in `screenSpace`, `screenSize` metres, unmasked.
    bool screenUi = false;
    XrSpace screenSpace = XR_NULL_HANDLE;
    XrPosef screenPose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrExtent2Df screenSize{0.0f, 0.0f};
    float screenPart[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    float screenForwardM = 0.0f;  // for the log: how far in front of the screen
    // A screen's UI that is masked all the same: a conversation held in the world, whose subtitle and wheel are cut out onto
    // quads of their own (plugin/ConversationUi) while the rest of the movie stays on the panel fixed in the room.
    bool screenMasks = false;
};
// Shows the panel for this frame (its quad, or nullptr), clears it and arms the next frame's UI redirect: exactly
// LE1's HalcyonOverlays for a frame with no wheel, label, world screen or cinema screen (its HUD pieces' masks given).
XrCompositionLayerQuad const* Layer(Frame const& frame) noexcept;

} // namespace mevr::host::hud_panel

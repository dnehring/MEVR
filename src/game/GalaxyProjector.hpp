// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/GalaxyHologram.hpp"
#include "core/vr/HostView.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/SceneCaptureRig.hpp"
#include "ue3/Types.hpp"

#include <cstdint>
#include <optional>

// The game's own galaxy map over the CIC's projector, every game's part of it (research/51 section 8.1, ME1's design;
// docs/galaxy-projector-handoff.md for the sequels): Shepard's view on foot remembered for the host's render-time hold, and
// the game's map camera's picture as a stereo window (two scene captures side by side, core/vr/CineScreen's parallel cameras).
// Each game's adapter finds its console or projector, its map camera and its map modes: ME1's game/GalaxyMap, ME3's
// game/Le3GalaxyCapture. Game thread.
struct HWND__;

namespace mevr::game::galaxy_projector {

// The game's window: this process's largest visible top-level window (the captures take its client area's aspect).
HWND__* GameWindow();

// Shepard's view on foot (the last update of the player's own camera in the on-foot mode), held while the map is open: the
// host swaps it into the player camera's POV for the main view's build only. The corrected on-foot base (floor lock) comes
// with it, so the renderer adds its common offsets once (vr::host_view::HoldBaseZ).
struct HeldView {
    bool haveView = false;
    ue3::Vector location{};
    ue3::Rotator rotation{};
    float fov = 90.0f;
    float rawZ = 0.0f;
    vr::host_view::State floorView;
    std::optional<engine::TrackingFrame> footFrame;  // the room as that view maps it, saved before the map's camera takes over
};
// Remembers the POV the player's camera just updated to (the caller has checked it is the on-foot view of this room).
void Remember(HeldView& held, ue3::Vector const& location, ue3::Rotator const& rotation, float fov);

// Where the player camera's POV lives, seen at its last update (the host writes the held view there around the main view's
// CalcSceneView and puts the game's camera back).
struct RenderHold {
    bool on = false;
    ue3::Vector* location = nullptr;
    ue3::Rotator* rotation = nullptr;
    float* fov = nullptr;
    std::uint64_t ms = 0;
};
// The scene's render-hold fields, while the POV's addresses are fresh (500 ms) and `writes` (`mevr galaxy hold 0`: off).
void PutRenderHold(HeldView const& held, RenderHold const& render, bool writes, vr::galaxy_holo::Scene& scene);

// The game's map camera after its own update: the window's captures look from it, and the host's label projection uses the
// exact POV last sent to them (the scene's camera).
struct MapPov {
    bool valid = false;
    ue3::Vector location{};
    ue3::Rotator rotation{};
    float fovDeg = 0.0f;
    std::uint64_t ms = 0;
};

struct WindowLook {
    float widthM = 2.0f;       // the window's width in the room (on the projector: its disc's diameter)
    float depth = 0.03f;       // [Galaxy] windowDepth: infinity's disparity, a share of the width
    bool round = true;         // [Galaxy] windowRound
    float backdrop = 0.8f;     // [Galaxy] windowBackdrop
    bool postProcess = true;   // the player's post-process chain on the captures (off: a diagnostic, research/le3-galaxy.md)
    ue3::Object* postProcessChain = nullptr;  // with postProcess: this chain instead of the player's (a diagnostic)
};

// Two captures from the map camera, side by side so the map's centre lands on the window's plane and infinity is `depth` of
// the width behind (core/vr/CineScreen). Their size: about half the game window's width at its aspect (the UI panel's
// resolution), both multiples of 8. A failed capture is retried after 5 s; the table's UI stays meanwhile.
class StereoWindow {
public:
    // Creates the captures if needed, points them from `pov` (fresh, its FOV sane: the caller's check), and fills the scene's
    // window fields and camera. `centre`: the map's centre object, for the convergence (else 5000 units). False: paused.
    bool Step(MapPov const& pov, std::optional<ue3::Vector> const& centre, int clientWidth, int clientHeight, WindowLook const& look,
        vr::galaxy_holo::Scene& scene);
    // Both captures stopped (kept for the next opening).
    void Pause();
    // The player or world changed: alive captures destroyed, the rest forgotten (they went with the old world).
    void Reset(char const* why);
    // The mod unloads: destroyed unless the process is exiting.
    void Shutdown(bool exiting);
    bool On() const { return on_; }
    float BaselineUU() const { return baselineUU_; }
    float ConvergeUU() const { return convergeUU_; }
    float Crop() const { return crop_; }
    capture_rig::Rig const& Eye(int e) const { return eyes_[e & 1]; }

private:
    capture_rig::Rig eyes_[2];
    bool on_ = false;
    std::uint64_t failedMs_ = 0;
    float baselineUU_ = 0.0f;
    float crop_ = 0.0f;
    float convergeUU_ = 0.0f;
    unsigned pointed_ = 0;
};

} // namespace mevr::game::galaxy_projector

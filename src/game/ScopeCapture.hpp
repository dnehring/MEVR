// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/AimRay.hpp"
#include "core/vr/ScopeCapture.hpp"
#include "core/vr/Sights.hpp"

#include <cstdint>
#include <string>

// The sniper scope's real zoomed render (research/39; what's shared with the host: core/vr/ScopeCapture.hpp). LE1,
// [Sights] scopeCapture = 1 (off by default until its frame cost is measured).
//
// The first time an eye is behind a scope, a SceneCapture2DActor is spawned (Actor.Spawn on the player controller,
// through ProcessEvent) with a TextureRenderTarget2D of its own (TextureRenderTarget2D.Create): never with the update
// skips that would stop it (its owner and its texture's users are never rendered), the player's post-process chain,
// FrameRate from scopeCaptureRate. While the aiming eye stays behind the scope (the host's sightView bits) it is enabled
// (SceneCaptureComponent.SetEnabled) and each tick pointed along the sight line from the sight's centre (SetView, then
// Actor.ForceUpdateComponents so the pose goes out now, before the next Draw), with a field of view that holds the
// picture's 1/zoom of the world (SetCaptureParameters when it changes), its near plane past the muzzle. It is disabled
// (no probe, no render: free) scopeCaptureHold seconds after the eye leaves. A new level forgets it (the old world's
// objects are gone); the mod's shutdown (hot reload) destroys it. Everything through our reflection layer (src/ue3);
// any piece missing stands it down with a `scope capture:` line, and the host keeps the eye copy. Game thread.
namespace mevr::game::scope_capture {

struct Link {
    // HostServices v20 setScopeCapture; null: an older host (the mod then never spawns anything).
    void (*publish)(vr::scope_capture::Capture const* capture) = nullptr;
};

// Each tick after the sight is published: `sight` as published this tick, `ray` the gun's aim ray it came from,
// `hostView` the host's vr::sights::View bits from its latest frame.
void Tick(float deltaSeconds, vr::sights::Sight const& sight, vr::aim_ray::Ray const& ray, std::uint32_t hostView, Link const& link);
// Snapshot JSON ("scopeCapture").
std::string StatusJson();
// Heartbeat line (while it exists).
void LogStats();
// Mod shutdown: disables and destroys the capture unless the process is exiting (then nothing is called).
void Shutdown(bool exiting);

} // namespace mevr::game::scope_capture

// SPDX-License-Identifier: MIT
#pragma once

#include "ue3/Types.hpp"

#include <cstdint>
#include <string>

// One scene capture of our own, as game/Spectator builds its one (research/39 section 2, research/47, research/49): a
// SceneCapture2DActor spawned on the player controller, its SceneCapture2DComponent writing a TextureRenderTarget2D of
// its own every frame it is enabled and pointed. The update skips that would stop it are off (nothing renders the texture,
// the actor is invisible), the matrices aren't taken from the actor (bUpdateMatrices off: SetView holds), the player's
// post-process chain on it if asked. Everything through our reflection layer (src/ue3); every class, field and function
// it uses has a row in engine/ReflectCheck (the spectator's rows). Game thread.
namespace mevr::game::capture_rig {

struct Options {
    int width = 1024;
    int height = 1024;
    bool floatTarget = false;  // FloatRGB (linear) instead of A8R8G8B8 (the game's display-ready SDR, post-processed)
    float fovDeg = 90.0f;      // horizontal
    float nearUnits = 10.0f;
    float farUnits = 0.0f;     // 0: the engine's
    float rate = 150.0f;       // SceneCaptureComponent.FrameRate (a re-pointed capture renders every frame anyway)
    bool postProcess = true;   // the player's post-process chain
};

class Rig {
public:
    // `tag` names it in the log ("galaxy left"). Spawns it near `at`; false (and `Failure()`) when anything is missing.
    bool Create(char const* tag, Options const& options, ue3::Vector const& at);
    // Alive in the current world (a level change takes its objects with it: then Forget).
    bool Alive() const;
    void Forget();
    // Disabled and destroyed (unless the process is exiting).
    void Destroy(char const* why);
    bool SetEnabled(bool on);
    bool Enabled() const { return enabled_; }
    // A new field of view (SetCaptureParameters, only when it changed).
    bool SetFov(float fovDeg);
    // Points it (SetView) and sends the view out now (ForceUpdateComponents), before the next Draw.
    bool Point(ue3::Vector const& location, ue3::Rotator const& rotation);

    // What the host needs to find the texture (UTexture.Resource: an FTextureResource* in engine memory).
    std::uint64_t Resource() const;
    std::uint64_t Generation() const { return generation_; }
    int Width() const { return options_.width; }
    int Height() const { return options_.height; }
    std::int32_t PixelFormat() const;
    float FovDeg() const { return options_.fovDeg; }
    std::uint32_t EnabledTicks() const { return enabledTicks_; }
    void Tick() { enabledTicks_ += enabled_ ? 1u : 0u; }
    std::string const& Failure() const { return failure_; }

private:
    bool Flush();

    std::string tag_;
    Options options_;
    void const* world_ = nullptr;
    ue3::Object* actor_ = nullptr;
    ue3::Object* component_ = nullptr;
    ue3::Object* target_ = nullptr;
    std::uint64_t generation_ = 0;
    bool enabled_ = false;
    bool reattach_ = false;
    std::uint32_t enabledTicks_ = 0;
    std::string failure_;
};

} // namespace mevr::game::capture_rig

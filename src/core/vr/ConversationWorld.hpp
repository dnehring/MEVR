// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/InteractLabel.hpp"

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>

// LE1 conversations played in the world, in first person (research/59, `[Cinematics] conversations = world`): during a
// conversation (game mode 5) the player keeps their own head-tracked view at Shepard's place instead of the director's camera.
// The game runs the conversation unchanged (its camera included); the mod holds the main view at Shepard's eyes at render time
// (as the galaxy map and the world screens do), and the host keeps the world in VR, puts the conversation movie's UI on the HUD
// panel and cuts its two parts out of it: the subtitle under the speaker's face, the dialogue wheel at the left hand (its stick
// steers it). Shared by the host (plugin/ConversationWorld, plugin/HalcyonOverlays) and the mod (game/ConversationWorld); the
// pure parts are unit-tested.
namespace mevr::vr::conversation_world {

// ---- What the mod publishes (HostServices v33 setConversationWorld) -----------------------------------------------------------
struct State {
    std::uint32_t size = sizeof(State);  // the publisher's struct size: host and mod must match
    std::uint8_t active = 0;      // a conversation the mod plays in the world: the host shows the world as on foot
    std::uint8_t wheel = 0;       // the dialogue wheel is up (BioSFHandler_Conversation.m_bDisplayingWheel)
    std::uint8_t subtitle = 0;    // subtitleAt holds where the speaker's line goes
    std::uint8_t wheelHand = 0;   // 0 the left hand, 1 the right, 2 in front of the head
    float subtitleAt[3] = {};     // tracking space, metres: the subtitle's centre (under the speaker's face)
    float subtitleHeightDeg = 0.0f;  // the subtitle band's angular height ([Cinematics] subtitleHeightDeg)
    float subtitleCrop[4] = {};   // x, y, width, height of the HUD frame; width 0: DefaultSubtitleCrop
    float wheelCrop[4] = {};      // the same for the wheel; width 0: DefaultWheelCrop
    float wheelWidthM = 0.0f;     // the wheel's quad width
    float wheelDistanceM = 0.0f;  // above the hand (the weapon wheel's placement)
    std::uint32_t speakerSerial = 0;  // changes when the speaker does (logs)
    std::uint32_t conversation = 0;   // counts the conversations: a new one is placed again
    // The view held at Shepard's at render time (the director's camera keeps serving the game): the host swaps these into the
    // player camera's POV around the main view's CalcSceneView (Halcyon's [MEVR] seam, as the galaxy map's hold does).
    std::uint8_t holdRender = 0;
    std::uint8_t holdPad[3] = {};
    float holdLocation[3] = {};
    std::int32_t holdRotation[3] = {};  // pitch, yaw, roll (Unreal units)
    float holdFov = 0.0f;
    std::uint64_t povLocationAddress = 0;  // FVector
    std::uint64_t povRotationAddress = 0;  // FRotator
    std::uint64_t povFovAddress = 0;       // float
    std::uint64_t sequence = 0;   // set by Publish
    std::uint64_t timeMs = 0;     // GetTickCount64 when published (set by Publish)
};

void Publish(State const& state);
// The latest state if active and published within `maxAgeMs`.
std::optional<State> Latest(std::uint64_t maxAgeMs = 300);
void Reset();

// ---- [Cinematics] conversations ----------------------------------------------------------------------------------------------
enum class Mode : std::uint8_t {
    Vr = 0,      // Halcyon's VR cine (the director's camera filling the view)
    Screen = 1,  // the 3D cinema screen (research/52)
    World = 2,   // in the world, first person (research/59)
};
// "world", "screen", "vr" or a number (0 = VR cine, 1 = the 3D screen, 2 = world); anything else: `fallback`.
Mode ParseMode(char const* text, Mode fallback);
char const* Name(Mode mode);

// ---- The conversation movie's parts on the HUD frame -------------------------------------------------------------------------
// The conversation movie (GUI_SF_Conversation) is a 1280x720 stage shown across the HUD frame's width; the frame is taller than
// the stage (2344x2392: 1306 stage pixels), and its subtitle and wheel are anchored to the stage's bottom edge, which lands on the
// frame's bottom (as the HUD's bottom widgets do, research/41) [verify]. Authored places (pchud.cpp, from the movie's SWF):
// SubtitleConversation at (639, 605.6), ConversationWheel at (639.7, 643.3), SubtitleBottom at (639, 703.1).
// The subtitle's band: the line (up to two rows, centred) above the wheel.
interact::Rect DefaultSubtitleCrop(std::uint32_t width, std::uint32_t height);
// The wheel's square around its centre (the six replies' text reaches about 300 stage px to each side).
interact::Rect DefaultWheelCrop(std::uint32_t width, std::uint32_t height);

// ---- Placement -----------------------------------------------------------------------------------------------------------------
// Where the subtitle goes for a speaker: `belowM` under the face (the head bone), moved `towardM` toward the eyes so it isn't
// inside the speaker's body, in tracking space. False if the speaker is at the eyes.
bool SubtitlePoint(XrVector3f const& face, XrVector3f const& eyes, float belowM, float towardM, XrVector3f& out);

// The held view's location: Shepard's on-foot view moved with his pawn since the conversation began (`follow`), so a stage that
// places him moves the player with him (the camera cuts hid such moves; research/59 measures them). Unreal units.
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};
Vec3 HeldLocation(Vec3 const& footView, Vec3 const& pawnAtStart, Vec3 const& pawnNow, bool follow);

} // namespace mevr::vr::conversation_world

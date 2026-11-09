// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/GameMenuRequest.hpp"
#include "core/vr/WorldUi.hpp"

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>
#include <string_view>

// The omni-tool's rail (research/57; the user, 2026-11-04: "the entire menu system available via omni-tool", "we do need a
// menu on the left side to allow selecting all of the different menus"): now a column on the selected side of the
// screen projected from the left wrist, or alone on the wrist when none is up; tapping the left wrist with the right index
// fingertip opens and closes it. This is the game-independent part (unit-tested in tests/Main.cpp): the entries, where the
// rail stands, the tap. The host draws it (plugin/OmniTool, assets/menu/omnitool.rml); the mod opens the screens
// (game/GameMenuRequest).
namespace mevr::vr::omni_rail {

// Top to bottom, as the rail lists them.
enum class Entry : std::uint8_t {
    Squad,      // the character record: talents (the concept art's "Abilities"), per squadmate
    Equipment,  // the inventory
    Map,
    Journal,    // missions and assignments
    Codex,
    Computer,   // the game's main wheel (options, save, load, quit): ShowMenuGui
    Close,      // every screen closed and the rail too
    Count
};
struct EntryInfo {
    char const* id;      // the document's name for it (omnitool.rml)
    char const* label;
    char const* icon;    // the menu's icon decorator (plugin/menu/MenuDecorators)
    game_menu::Action action;
};
EntryInfo const& Info(Entry entry);
std::optional<Entry> FromId(std::string_view id);
// The entry a screen up belongs to (Inventory: Equipment, CharacterRecord: Squad, the main wheel: Computer...); nullopt for a
// screen the rail doesn't open (the shop, the minigame).
std::optional<Entry> ForScreen(world_ui::Screen screen);

// ---- Where it stands ----
// Sized for a fingertip (the user, 2026-11-04: "too small for VR finger interaction"): the game's frame fills a 360 x 640 canvas,
// so beside a 0.31 m screen each tab is about 3 x 14.5 cm (it was 1.85 x 9.3 on 360 x 1000, the frame filling 62% of it).
inline constexpr float kRailAspect = 0.5625f;  // width over height (its canvas: 360 x 640)
inline constexpr float kGapM = 0.015f;         // between the rail and the screen beside it
inline constexpr float kAloneHeightM = 0.32f;  // alone on the wrist: its height (tabs about 3 cm)
struct Rail {
    XrPosef pose{};  // its centre; it faces +Z (toward the eyes), like the screen
    float widthM = 0.0f;
    float heightM = 0.0f;
};
// Beside a screen in its plane, as tall as it. +1: the eye's right, -1: left.
Rail Beside(world_ui::Panel const& screen, float side = 1.0f, float gapM = kGapM, float aspect = kRailAspect);
inline constexpr float kSlideS = 0.43f;
inline constexpr float kAloneFadeS = 0.10f;
float SlideEase(float seconds);
Rail Interpolate(Rail const& from, Rail const& to, float seconds);
// Tucked under the selected screen edge, slightly behind its face.
Rail Tucked(world_ui::Panel const& screen, float side);
struct Face {
    Rail rail;
    float u0 = 0.0f, u1 = 1.0f;  // horizontal canvas crop, for both drawing and pointer pixels
};
// OpenXR orders layers, not their depth. Clip the tucked rail to the part outside the screen's edge as it emerges.
std::optional<Face> Exposed(Rail const& rail, world_ui::Panel const& screen, float side);

// Wait kUnfoldS, fading the old standalone rail in place for kAloneFadeS, then reveal from Tucked for kSlideS.
// dt is real elapsed time (not capped at a frame budget); screenAgeS initializes the remaining unfold wait.
// A slide is tied to a stretch with a screen, not its tag/opening number. The caller keeps it through tab backouts.
class Slide {
public:
    Rail Step(Rail const& target, bool beside, float side, float dt, std::optional<Rail> const& tucked,
        float screenAgeS = 0.0f);
    void Reset();
    bool Started() const { return started_; }
    bool Moving() const { return beside_ && Age() < kSlideS; }
    float Age() const;
    bool FadingAlone() const { return beside_ && fadeAlone_ && ageS_ < kAloneFadeS; }
    float Opacity() const;
private:
    bool have_ = false, beside_ = false, started_ = false, fadeAlone_ = false;
    float side_ = 1.0f, ageS_ = world_ui::kUnfoldS+kSlideS;
    Rail drawn_{};
    Rail alone_{};
    Rail from_{};  // relative to the destination pose: the slide follows a moving wrist
};
// Alone: projected from the wrist as a screen would be (world_ui::WristPlace), `heightM` tall.
std::optional<Rail> Alone(XrVector3f const& eyes, XrVector3f const& wrist, float heightM, float aboveM, float towardM,
    float aspect = kRailAspect);

// ---- The tap: the right index fingertip on the omni-tool's display, the left wrist ----
// A tap is the fingertip coming within pressM of the wrist point from farther than releaseM (a fingertip resting there, or a
// hand that starts there, doesn't tap), at most once every cooldownS, and only while `allowed` (the host: no weapon drawn, no
// menu of ours up, both hands tracked).
struct TapSettings {
    float pressM = 0.04f;    // 0.035 until 2026-11-05 (the user's first try found nothing to aim at; with the target, a little wider)
    float releaseM = 0.08f;  // 0.07 until then
    float cooldownS = 0.6f;
};

// ---- The tap's target (the user, 2026-11-05: "we most certainly need that visible target"; nothing showed where to tap) ----
// A button kTargetBelowM below the wrist point (the HUD's wrist readout sits there; the button mustn't cover it), facing the
// eyes. The tap aims at its centre. It shows faintly while the left wrist is raised in front of the face, brighter while the
// right fingertip comes near, cyan once the tap is armed within releaseM; never while the tap isn't allowed.
inline constexpr float kTargetBelowM = 0.035f;
inline constexpr float kTargetSizeM = 0.04f;        // the quad's side
inline constexpr float kTargetNearM = 0.30f;        // the right fingertip this near: the target brightens
inline constexpr float kRaisedBelowEyesM = 0.45f;   // the wrist raised: the target within this below the eyes...
inline constexpr float kRaisedAcrossM = 0.60f;      // ...and this near them across the floor
XrVector3f TargetPoint(XrVector3f const& wrist);
// On Shepard's drawn arm (the user, 2026-11-05: "I want it to be the wrist target", on the body, not at the controller: the
// arm IK's reach scaling draws the arm up to 15 cm from the controller): the mod sends his bones in tracking space and the
// target sits on the back of his left forearm, kForearmAlong of the way from the wrist to the elbow (where the omni-tool's
// gauntlet is), lifted kForearmLiftM toward the eyes (onto the arm's surface); the tap is his right index fingertip.
inline constexpr float kForearmAlong = 0.30f;
inline constexpr float kForearmLiftM = 0.035f;
inline constexpr float kFingertipRatio = 1.0f;  // the tip beyond the index's second joint, as a share of its first bone
struct Body {
    XrVector3f leftWrist{};
    XrVector3f leftElbow{};
    XrVector3f rightIndex1{};  // the index's first and second joints (RightIndexFinger1, RightIndexFinger2)
    XrVector3f rightIndex2{};
};
// Nullopt for a degenerate arm (wrist on the elbow, eyes on the arm, not finite).
std::optional<XrVector3f> BodyTarget(Body const& body, XrVector3f const& eyes);
std::optional<XrVector3f> BodyFingertip(Body const& body);
enum class TargetLook : std::uint8_t { Hidden, Faint, Near, Armed };
// `tipTracked` false: only the raised wrist counts. `armed`: Tap::Armed() (the fingertip has been away since the last tap).
TargetLook TargetLookFor(bool allowed, XrVector3f const& eyes, XrVector3f const& target, bool tipTracked, XrVector3f const& tip,
    bool armed, float releaseM);
class Tap {
public:
    explicit Tap(TapSettings const& settings = TapSettings{}) : settings_(settings) {}
    bool Step(bool tracked, XrVector3f const& tip, XrVector3f const& wrist, bool allowed, float dt);
    bool Armed() const { return armed_; }

private:
    TapSettings settings_;
    bool armed_ = false;  // the fingertip has been away (past releaseM) since the last tap
    float cooldown_ = 0.0f;
};

// The fingertip going for a panel on the wrist (a game screen or the rail): within depthM in front of its face (or a little
// behind it, pressing), its place across the face inside the panel and marginM around it. No wrist tap then: seen from the eyes
// the wrist sits in front of the screen's lower part, and a replayed reach for the equipment screen's Equip button passed 3.2 cm
// from the wrist point and closed everything (research/57 section 6). A tap on the wrist itself is below the panel's bottom edge
// (10 cm above the wrist), outside it.
inline constexpr float kTapPanelDepthM = 0.15f;
inline constexpr float kTapPanelBehindM = 0.03f;
inline constexpr float kTapPanelMarginM = 0.02f;
bool GoingForPanel(XrVector3f const& tip, world_ui::Panel const& panel, float depthM = kTapPanelDepthM,
    float marginM = kTapPanelMarginM);

} // namespace mevr::vr::omni_rail

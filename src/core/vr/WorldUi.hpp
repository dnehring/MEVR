// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// The game's full-screen screens in the room (research/53). LE1's screens are Scaleform movies the game opens in its GUI mode
// (EGameModes 7), each a tag in its GUI library (BioSFManager.HandlerLibrary: "shop", "Inventory", "Loot"...); the open ones are
// SFXGameModeGUI.StackedGuis, top last. Halcyon shows mode 7 on its flat panel, the world gone. Instead, while one of these
// screens is up the mod (game/WorldUi) publishes which one and what it belongs to (the vendor's shop actor...), the host keeps
// the world in stereo as on foot (Halcyon's live-world-GUI branch) and shows the game's own UI on a panel standing in the room
// where it opened, between the player and that thing, its dark parts see-through like a hologram; a controller's ray on it is
// the game's mouse. This file: the shared, engine-agnostic part (unit-tested, tests/Main.cpp TestWorldUi).
namespace mevr::vr::world_ui {

// The screens we put in the room, by their library tag. None: a tag we leave to Halcyon (the front end: the main menu, character
// creation, credits, splash, intro text; the HUD's own movies; the galaxy map's, which has its own, research/51).
enum class Screen : std::uint8_t {
    None = 0,
    Shop,
    Loot,
    Inventory,
    XMods,
    CharacterRecord,
    Specialization,
    Journal,
    Codex,
    Map,
    PartySelect,
    MenuBrowser,
    Options,
    Save,
    Load,
    MessageBox,
    Achievement,
    AdditionalContent,
    SkillGame,   // the bypass minigame (game mode 10)
    DesignerUI,  // designer minigames over the world (Quasar, the Hanoi puzzle, the laser, the toxin cure, the bomb disarm)
    GameOver,
    Count
};
Screen FromTag(std::string_view tag);  // case-insensitive
char const* Name(Screen screen);       // its library tag ("None" for None)
inline constexpr std::uint32_t Bit(Screen screen) { return 1u << static_cast<unsigned>(screen); }

// ---- Settings ([WorldUi] in <GAME>MEVR.ini, parsed by the mod) --------------------------------------------------------
struct Settings {
    bool on = true;            // on: the game's screens stand in the room (0: Halcyon's flat panel, the world gone)
    float widthM = 1.20f;      // widthM: the screen's shown part, that wide
    float distanceM = 0.95f;   // distanceM: that far ahead of the eyes when it opens (toward what it belongs to)
    float belowM = 0.50f;      // belowM: its centre below the eyes (a counter's height: the vendor's face stays clear above it)
    float backdrop = 0.80f;    // backdrop: its dark parts' opacity (the room shows through the rest); 1 opaque
    // screens: which ones (bits of Screen; the ini lists their tags), all by default (the user, 2026-11-04: every flat screen in
    // the room). The equipment, XMods and character record lose their 3D model there (it lives in the game's UI world).
    std::uint32_t screens = 0xFFFFFFFFu;
    // wrist: the screens projected from the left wrist instead, Shepard's omni-tool (the bypass minigame by default; the user,
    // 2026-11-04; Fallout 4 VR's Pip-Boy, wrist or projected, and Half-Life: Alyx's multitool holograms set the pattern:
    // research/53 section 6). Without a tracked left controller when it opens, the screen stands in the room.
    std::uint32_t wristScreens = Bit(Screen::SkillGame);
    float wristWidthM = 0.30f;   // wristWidthM: its width
    float wristAboveM = 0.10f;   // wristAboveM: its bottom edge above the wrist as the eyes see it (the fan of light in the gap;
                                 // 0.10 clears the omni-tool's gauntlet, measured 2026-11-04, 0.06 covered its top)
    float wristTowardM = 0.0f;   // wristTowardM: moved that far from the wrist toward the eyes (level; < 0 away)
    bool wristFollow = true;     // wristFollow: it rides on the wrist (0: it stays where it was projected, the hand free)
    bool wristBeam = true;       // wristBeam: a faint fan of light from the wrist to its bottom edge
    // omniTool: while a wrist screen is up, Shepard's left forearm wears the game's own omni-tool (the orange holographic
    // gauntlet and the hand's display, BIOG_V_T_Omni_Tool_Z; the user, 2026-11-04: "core Mass Effect with the omni-tool").
    // Mod only (game/OmniTool).
    bool omniTool = true;
};

// Where a screen stands.
enum class Placement : std::uint8_t {
    Room = 0,       // in the room where it opened (Place)
    Wrist = 1,      // projected from the left wrist, riding on it (WristPlace each frame)
    WristOnce = 2,  // projected from the left wrist where it was when it opened, then fixed in the room
};

// ---- What the mod publishes (HostServices v27 setWorldUi) -------------------------------------------------------------
struct State {
    std::uint8_t active = 0;      // a screen of ours is up: the host shows it in the room
    std::uint8_t screen = 0;      // Screen
    std::uint8_t anchored = 0;    // anchor holds the place of what it belongs to
    std::uint8_t pad = 0;
    std::uint32_t opening = 0;    // counts the screens opened: a new one is placed again
    float anchor[3] = {};         // tracking space, metres (OpenXR axes)
    float widthM = 0.0f;
    float distanceM = 0.0f;
    float belowM = 0.0f;
    float backdrop = 0.0f;
    float stageCrop[4] = {0.0f, 0.0f, 1.0f, 1.0f};  // the part of the movie's stage shown: u0, v0, u1, v1 (stage UV)
    char tag[24] = {};            // the screen's library tag (logs)
    // The view held at Shepard's at render time (the shop's camera moves to a backdrop stage of its own, research/53): the host
    // swaps these into the player camera's POV around the main view's CalcSceneView (Halcyon's [MEVR] seam, as the galaxy map's
    // hold does), and the game keeps its own camera for everything else.
    std::uint8_t holdRender = 0;
    std::uint8_t holdPad[3] = {};
    float holdLocation[3] = {};
    std::int32_t holdRotation[3] = {};
    float holdFov = 0.0f;
    std::uint64_t povLocationAddress = 0;
    std::uint64_t povRotationAddress = 0;
    std::uint64_t povFovAddress = 0;
    std::uint64_t sequence = 0;   // set by Publish
    std::uint64_t timeMs = 0;     // GetTickCount64 when published (set by Publish)
    // Where it stands (the State grew 2026-11-04: the mod sends only to a host whose worldUiSize is this size).
    std::uint8_t placement = 0;   // Placement
    std::uint8_t beam = 0;        // the wrist's fan of light
    std::uint8_t placementPad[2] = {};
    float wristWidthM = 0.0f;
    float wristAboveM = 0.0f;
    float wristTowardM = 0.0f;
    // The projector (grew 2026-11-04 again): the omni-tool's display on Shepard's left arm as rendered (game/OmniTool), tracking
    // space; the host projects from it instead of the controller's wrist (the arm-length calibration can leave the hand short).
    float emitter[3] = {};
    std::uint8_t emitterValid = 0;
    std::uint8_t emitterPad[3] = {};
};

void Publish(State const& state);
void Forget();
inline constexpr std::uint8_t kGuiMode = 7;       // SFXGameModeManager.EGameModes: GameMode_GUI
inline constexpr std::uint8_t kMinigameMode = 10; // GameMode_Minigame (the bypass minigame)
inline constexpr bool ScreenMode(int mode) { return mode == kGuiMode || mode == kMinigameMode; }
// The latest state if published within maxAgeMs and active; else nullopt.
std::optional<State> Latest(std::uint64_t maxAgeMs);

// ---- The math ---------------------------------------------------------------------------------------------------------
struct Rect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};
inline constexpr float kStageAspect = 16.0f / 9.0f;  // LE1's movies are authored 16:9 (the shop's stage: rows 537..1855 of 2392)
// The movie's stage inside a window of that size, in window UV: the largest box of `aspect` centred in it (letterboxed or
// pillarboxed, as Scaleform's show-all scaling puts it).
Rect Stage(float windowWidth, float windowHeight, float aspect = kStageAspect);
// A part of the stage (stage UV) in window UV.
Rect InWindow(Rect const& stage, Rect const& part);
// The part of the stage each screen shows by default: its content without the empty sides (stage UV).
Rect DefaultCrop(Screen screen);
// The part shown when it's projected from the wrist: tighter where the screen is a small box on its stage (the minigame). The host
// picks it when the screen goes on the wrist; the mod's stageCrop is the room's.
Rect WristCrop(Screen screen);
// The width the screen's shown part stands at in the room: `widthM` ([WorldUi] widthM) is the whole stage's for a screen that
// keeps the stage's scale (loot: a narrow frame on an empty stage keeps its size, the empty sides gone), else its crop's.
float RoomWidth(Screen screen, float widthM);

struct Panel {
    XrPosef pose{};  // its centre; it faces +Z (toward the eyes)
    float widthM = 0.0f;
    float heightM = 0.0f;
};
// Where a screen stands when it opens: `distanceM` ahead of the eyes, toward the anchor when there is one (and then never
// closer to it than kNearAnchorM, nor nearer the eyes than kNearestM), else straight ahead (the head's flat forward); its centre
// `belowM` below the eyes; facing the eyes (its X level); `heightOverWidth` its shape. nullopt for a width that can't be.
inline constexpr float kNearAnchorM = 0.35f;
inline constexpr float kNearestM = 0.55f;
std::optional<Panel> Place(XrVector3f const& eyes, XrVector3f const& headForward, std::optional<XrVector3f> anchor, float widthM,
    float distanceM, float belowM, float heightOverWidth);
// The whole window on the shown panel's plane, for a panel that shows `part` of it (window UV): the quad the game pointer maps
// to the window's pixels.
Panel WholeWindow(Panel const& shown, Rect const& part);

// ---- Projected from the wrist (the omni-tool; research/53 section 6) ---------------------------------------------------
// Facing the eyes (its X level), its bottom edge's middle `aboveM` above the wrist (moved `towardM` toward the eyes, level) along
// its own up: across the line of sight, so the eyes see the gap between the wrist and the screen, never the wrist through the
// screen (a gap straight up hid the wrist behind a screen tilted to face eyes above it: snapshot 2026-11-04 11:00:52). Never
// nearer the eyes than kWristNearestM. nullopt for a width that can't be.
inline constexpr float kWristNearestM = 0.25f;
std::optional<Panel> WristPlace(XrVector3f const& eyes, XrVector3f const& wrist, float widthM, float aboveM, float towardM,
    float heightOverWidth);
// It unfolds out of the wrist when it opens: over kUnfoldS its height grows from nothing and its width from kUnfoldWidth, eased
// out, its bottom edge where it stays. `seconds` since it opened; past kUnfoldS the panel as it is.
inline constexpr float kUnfoldS = 0.18f;
inline constexpr float kUnfoldWidth = 0.6f;
Panel Unfold(Panel const& panel, float seconds);
// The fan of light from the emitter (the wrist) to the panel's bottom edge: a quad in their plane, its top edge on that edge, its
// bottom at the emitter (BeamPixels narrows to a point there). nullopt when the emitter is (nearly) on that edge or the fan
// would be seen edge on along the edge.
struct Beam {
    XrPosef pose{};
    float widthM = 0.0f;
    float heightM = 0.0f;
};
std::optional<Beam> BeamTo(XrVector3f const& emitter, Panel const& panel);
// The fan's picture, `size` x `size` RGBA8 (R in the low byte) for an sRGB texture, row 0 the panel's edge: the panel's width at the
// top narrowing to kBeamApex at the bottom, brightest at the emitter (alpha kBeamNear) fading to kBeamFar at the panel, soft sides.
// Premultiplied in linear light: each colour byte is the sRGB encoding of (the colour, linear) x alpha, alpha linear, so the GPU's
// decode gives what the compositor blends (premultiplying the encoded values darkened what was behind it: 2026-11-04 11:09:53).
// The colour is given sRGB-encoded.
inline constexpr float kBeamApex = 0.04f;  // of the width
inline constexpr float kBeamNear = 0.30f;
inline constexpr float kBeamFar = 0.05f;
std::vector<std::uint32_t> BeamPixels(int size, float red, float green, float blue);

} // namespace mevr::vr::world_ui

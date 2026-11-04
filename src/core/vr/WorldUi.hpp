// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>
#include <string_view>

// The game's full-screen screens in the room (research/53). LE1's screens are Scaleform movies the game opens in its GUI mode
// (EGameModes 7), each a tag in its GUI library (BioSFManager.HandlerLibrary: "shop", "Inventory", "Loot"...); the open ones are
// SFXGameModeGUI.StackedGuis, top last. Halcyon shows mode 7 on its flat panel, the world gone. Instead, while one of these
// screens is up the mod (game/WorldUi) publishes which one and what it belongs to (the vendor's shop actor...), the host keeps
// the world in stereo as on foot (Halcyon's live-world-GUI branch) and shows the game's own UI on a panel standing in the room
// where it opened, between the player and that thing, its dark parts see-through like a hologram; a controller's ray on it is
// the game's mouse. This file: the shared, engine-agnostic part (unit-tested, tests/Main.cpp TestWorldUi).
namespace mevr::vr::world_ui {

// The screens we put in the room, by their library tag. None: a tag we leave to Halcyon (the main menu, character creation,
// credits, the game-over screen, splash, intro text, the HUD's own movies, the galaxy map's, the minigame's).
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
    // screens: which ones (bits of Screen; the ini lists their tags). By default those measured in the room (research/53): not the
    // equipment, XMods and character record, whose 3D models live in the game's UI world.
    std::uint32_t screens = Bit(Screen::Shop) | Bit(Screen::MenuBrowser) | Bit(Screen::Journal) | Bit(Screen::Codex) | Bit(Screen::Map) |
                            Bit(Screen::MessageBox);
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
};

void Publish(State const& state);
void Forget();
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

} // namespace mevr::vr::world_ui

#pragma once

#include "imgui.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The guided calibration's look (research/25), apart from where it is shown (plugin/Calibration puts it in
// quads; tools/calibration-preview renders it to PNGs offline, so its look can be checked without a headset).
// Kept in this file alone so the coming main menu can re-skin it: the colours are the menu mockup's
// (2026-10-22: background #08101A, surfaces #0E1926 / #132235, text #E6F2FB / #9DB3C6, lines
// rgba(140,200,240,0.18), cyan #7CCBF2, orange #FF8F3F), chamfered corners with short orange strokes on two
// opposite corners; a "CALIBRATION" kicker and a step chip, a six-segment progress bar, a big uppercase
// instruction, one line of explanation, the target chips, a large hold-still ring, button hints at the
// bottom. ImGui draw lists only (a context whose display is the canvas size), plus the orbs and the check
// dot as small pictures made on the CPU.
namespace mevr::host::calibration_draw {

inline constexpr unsigned kPanelWidth = 1024;  // the instruction panel's canvas; the quad keeps its aspect
inline constexpr unsigned kPanelHeight = 640;
inline constexpr unsigned kBoardSize = 1024;   // the target board's canvas (square)
inline constexpr unsigned kOrbPixels = 256;    // an orb's picture (square)
inline constexpr unsigned kDotPixels = 128;    // the check dot's picture (square)
inline constexpr unsigned kMarkerPixels = 128; // the hand marker's picture (square)

enum ButtonId : int {
    kRowBreak = -1,  // in PanelView::buttons: the next button starts a new row
    kNoButton = 0,
    kSkip,
    kRestart,
    kCancel,
    kSave,
    kSwitchEye,
    kRedoBody,
    kRedoPistol,
    kRedoRifle,
    kCalibrateNow,
    kDismiss,
};
char const* ButtonLabel(int id);

struct Button {
    int id = kNoButton;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

// The sequence's screens and their words (shared by the host and the preview).
enum class Screen : int {
    Prompt,      // the first-run question
    Stand,       // 1 Height: stand tall, look ahead
    TPose,       // 2 Arms: out to the sides
    Forward,     // 2 Arms: straight ahead
    Crouch,      // 3 Crouch
    PistolGrab,  // 4 Pistol: take the orb
    PistolAim,   // 4 Pistol: aim it at the three targets
    RifleGrab,   // 5 Rifle: take both orbs
    RifleAim,    // 5 Rifle: shoulder it and aim
    Check,       // 6 Check: the red dot along the new sight line; save or redo
    Saved,
};
struct ScreenText {
    char const* title = "";   // the big uppercase instruction
    char const* body = "";    // one line (two at most) of explanation
    int pip = 0;              // the progress segment it lights (1..6; 0 none)
};
ScreenText TextFor(Screen screen);
inline constexpr int kSteps = 6;
char const* StepLabel(int pip);  // "Height", "Arms", "Crouch", "Pistol", "Rifle", "Check"

struct PanelView {
    bool prompt = false;          // the first-run question instead of a step
    std::string device;           // the controllers it is for
    int step = 0;                 // 1..steps: the progress bar (0: none)
    int steps = kSteps;
    std::string title;
    std::string body;
    std::vector<std::string> summary;  // result rows (the check step)
    int targets = 0;              // target chips to show (the aiming steps: 3)
    bool targetTaken[3] = {};     // each target's aim is in (a redo opens one again)
    int targetCurrent = -1;
    std::string status;           // what is missing (or "Hold still"): why nothing is being captured
    bool statusWarn = false;      // the status is a problem to fix (orange), not progress
    float progress = -1.0f;       // 0..1: the hold-still ring; negative: none
    std::string message;          // a result or a warning
    bool warn = false;
    bool hints = true;            // "B: back a step", "Hold both sticks: cancel"
    std::vector<int> buttons;     // ButtonId, in order (kRowBreak between rows)
    int hovered = kNoButton;
    bool pointer = false;         // a controller points at the panel: where (canvas pixels)
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    float time = 0.0f;            // seconds (pulses)
    float flash = 0.0f;           // 0..1: a capture was just taken
};

// The fonts: headings (Rajdhani if found, else Bahnschrift) and body text (Titillium Web if found, else
// Bahnschrift, else Segoe UI, else ImGui's own).
struct Fonts {
    ImFont* heading = nullptr;
    ImFont* body = nullptr;
};
// Adds them to `io.Fonts`, looking for Rajdhani-SemiBold.ttf / Rajdhani-Bold.ttf and TitilliumWeb-Regular.ttf /
// TitilliumWeb-SemiBold.ttf in `folders` first (SIL OFL fonts, not bundled: drop them in), then Windows' own.
// `chosen` names what was loaded.
Fonts LoadFonts(ImGuiIO& io, std::vector<std::filesystem::path> const& folders, std::string& chosen);

// Where the view's buttons are (canvas pixels), in rows from the bottom right.
std::vector<Button> LayoutButtons(PanelView const& view);
// Which button is at (x, y), or kNoButton.
int ButtonAt(PanelView const& view, float x, float y);
void DrawPanel(ImDrawList* dl, Fonts const& fonts, PanelView const& view);

struct BoardView {
    int current = -1;           // the target to aim at now (-1: none)
    bool done[3] = {};          // taken in this round
    std::string hint;           // one short line across the top (why the aim isn't captured), "" none
    bool hintWarn = false;      // orange (a problem) or cyan (hold still)
    float time = 0.0f;
    float flash = 0.0f;         // 0..1: a capture was just taken
};
// The board's canvas: kBoardSize square for vr::calibration::kBoardSize meters, the targets at kTargetX/Y.
void DrawBoard(ImDrawList* dl, Fonts const& fonts, BoardView const& view);

// Premultiplied sRGB RGBA8 (0xAABBGGRR), what the quads' SRGB swapchains take: an orb (`active`: the
// orange one to grab; else the cyan one in the hand), the check dot.
std::vector<std::uint32_t> OrbPicture(bool active);
std::vector<std::uint32_t> DotPicture();
// The ring drawn at each hand's grab point while the calibration has the controllers (Shepard's hands don't follow
// them then): where the calibration thinks the hand is, to judge the orbs against.
std::vector<std::uint32_t> HandMarkerPicture();

} // namespace mevr::host::calibration_draw

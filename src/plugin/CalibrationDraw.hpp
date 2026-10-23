#pragma once

#include "imgui.h"

#include <cstdint>
#include <string>
#include <vector>

// The guided calibration's look (research/25), apart from where it is shown (plugin/Calibration puts it in
// quads; tools/calibration-preview renders it to PNGs offline, so its look can be checked without a headset).
// ME1's holographic UI: pale blue-white lines with a soft glow over dark translucent blue, an orange accent
// for what to do now. ImGui draw lists only (a context whose display is the canvas size), plus the orbs and
// the check dot as small pictures made on the CPU.
namespace mevr::host::calibration_draw {

inline constexpr unsigned kPanelWidth = 1024;  // the instruction panel's canvas; the quad keeps its aspect
inline constexpr unsigned kPanelHeight = 640;
inline constexpr unsigned kBoardSize = 1024;   // the target board's canvas (square)
inline constexpr unsigned kOrbPixels = 256;    // an orb's picture (square)
inline constexpr unsigned kDotPixels = 128;    // the check dot's picture (square)

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
    Stand,       // 1: stand tall, look ahead
    TPose,       // 2: arms out to the sides
    Forward,     // 2: both arms straight ahead
    Crouch,      // 3
    PistolGrab,  // 4: take the pistol orb
    PistolAim,   // 4: aim it at the three targets
    RifleGrab,   // 5: take both rifle orbs
    RifleAim,    // 5: shoulder it and aim
    Check,       // 6: the red dot along the new sight line; save or redo
    Saved,
};
struct ScreenText {
    char const* title = "";
    char const* body = "";
    int pip = 0;  // the step pip it lights (1..6; 0 none)
};
ScreenText TextFor(Screen screen);
inline constexpr int kSteps = 6;

struct PanelView {
    bool prompt = false;          // the first-run question instead of a step
    std::string device;           // the controllers it is for
    int step = 0;                 // 1..steps: the pips (0: none)
    int steps = 6;
    std::string title;
    std::string body;             // wrapped to the panel; '\n' starts a new paragraph
    std::vector<std::string> summary;  // result lines (the check step)
    std::string status;           // what is missing, or "Hold still"
    float progress = -1.0f;       // 0..1: the hold-still ring; negative: none
    std::string message;          // a result or a warning, in the accent colour
    bool warn = false;
    std::vector<int> buttons;     // ButtonId, in order
    int hovered = kNoButton;
    bool pointer = false;         // a controller points at the panel: where (canvas pixels)
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    float time = 0.0f;            // seconds (pulses)
    float flash = 0.0f;           // 0..1: a capture was just taken
};
// Where the view's buttons are (canvas pixels), in rows from the bottom.
std::vector<Button> LayoutButtons(PanelView const& view);
// Which button is at (x, y), or kNoButton.
int ButtonAt(PanelView const& view, float x, float y);
void DrawPanel(ImDrawList* dl, ImFont* font, PanelView const& view);

struct BoardView {
    int current = -1;           // the target to aim at now (-1: none)
    bool done[3] = {};          // taken in this round
    float time = 0.0f;
    float flash = 0.0f;         // 0..1: a capture was just taken
};
// The board's canvas: kBoardSize square for vr::calibration::kBoardSize meters, the targets at kTargetX/Y.
void DrawBoard(ImDrawList* dl, ImFont* font, BoardView const& view);

// Premultiplied sRGB RGBA8 (0xAABBGGRR), what the quads' SRGB swapchains take: an orb (`active`: the
// orange one to grab, or held; else pale blue), the check dot.
std::vector<std::uint32_t> OrbPicture(bool active);
std::vector<std::uint32_t> DotPicture();

// Bahnschrift (Windows 10+), else Segoe UI, else ImGui's own, loaded large so big text stays crisp.
ImFont* LoadFont(ImGuiIO& io, std::string& chosen);

} // namespace mevr::host::calibration_draw

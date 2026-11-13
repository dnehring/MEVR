// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Thespian mode (docs/thespian-mode-plan.md, research/70 section 8 steps 4 and 5): the player speaks Shepard's lines.
// Two things live here, both without the engine:
//
// 1. The turn's rules: from what the matcher says about the words heard so far and how long the microphone has been
//    quiet, when a reply is taken. A line read to its end is taken after a short silence (endMs); a line the player
//    stopped part-way is taken after a longer one (pauseMs) if the matcher is sure at the end; nothing is ever taken on
//    a guess, nor while the other person still speaks.
// 2. The panel the mod sends the host every tick (HostServices setThespian): the reply rows, which words are lit, the
//    microphone's mark. It crosses the host/mod boundary: it only grows at the end and carries its size.
namespace mevr::vr::thespian {

// How much of a line must be said before it is taken.
enum class Strictness : std::uint8_t {
    Relaxed = 0,    // the matcher's decision is enough
    Performer = 1,  // decided, and half the line said or the label said (the default)
    Method = 2,     // decided, and nine tenths of the line said
};
Strictness ParseStrictness(std::string_view text, Strictness fallback = Strictness::Performer);
char const* Name(Strictness strictness);

enum class Phase : std::uint8_t {
    Off = 0,        // no wheel
    Armed = 1,      // the wheel is up, nothing heard
    Hearing = 2,    // words heard, no card decided
    Leading = 3,    // a card is decided: its row is lit
    Committed = 4,  // a reply was taken; nothing more until the wheel has gone down
};
// More: a row is lit but too little of it was said for the strictness ("Keep reading"). Doubt: a row would be taken but
// a word that tells it from another was only heard alike ("Not sure which"): the words are dropped for a fresh reading.
enum class Mic : std::uint8_t { Off = 0, Armed = 1, Hearing = 2, Understood = 3, Unsure = 4, More = 5, Doubt = 6 };

struct Settings {
    int endMs = 200;     // quiet after a line read to its end
    int pauseMs = 900;   // quiet after a line stopped part-way
    Strictness strictness = Strictness::Performer;
    int staleMs = 3500;  // quiet this long with words nobody could place: they are dropped ("Didn't catch that")
    float unsureS = 2.0f;        // how long the "Didn't catch that" mark shows
    float committedMaxS = 3.0f;  // a commit the game didn't act on (the wheel still up): listen again
};

// The matcher's answer for the words heard so far (core/voice/ScriptMatch's Result, reduced to what the rules need).
struct Match {
    bool words = false;       // anything was heard
    int leader = -1;          // the best card, -1 none
    bool decided = false;     // the matcher is sure of it
    bool reachedEnd = false;  // one of its line's last two words was said
    float coverage = 0.0f;    // the share of its line said, 0..1
    bool byLabel = false;     // matched by the wheel's label, not the line
    bool doubtful = false;    // it would be decided but for a word only heard alike (ScriptMatch's Result::doubtful)
};

struct Input {
    bool wheelUp = false;
    bool otherSpeaking = false;  // the other person's line has not ended
    std::uint32_t silentMs = 0;  // since the last sound at speech level
    Match now;                   // the matcher asked "while speaking" (final = false)
    Match atEnd;                 // the matcher asked "the player has stopped" (final = true)
    double dt = 0.0;
};

struct Output {
    Phase phase = Phase::Off;
    Mic mic = Mic::Off;
    int commit = -1;       // this tick: take this card
    int leader = -1;       // the row to light (-1 none)
    bool restart = false;  // this tick: drop the words heard so far and start the transcript again
};

struct State {
    Phase phase = Phase::Off;
    bool otherSpoke = false;   // the other person was speaking last tick
    float unsureLeft = 0.0f;   // the "Didn't catch that" mark's time left
    bool doubt = false;        // ... and it says "Not sure which" (Mic::Doubt)
    float committedS = 0.0f;   // since the commit
    int committed = -1;
};

// Whether the strictness lets this match be taken.
bool Allows(Strictness strictness, Match const& match);
Output Step(State& state, Settings const& settings, Input const& in);

// ---- The panel (host and mod deploy together) ----------------------------------------------------------------------------

inline constexpr std::size_t kMaxCards = 12;
inline constexpr std::size_t kLabelChars = 96;
inline constexpr std::size_t kLineChars = 512;
inline constexpr std::size_t kSpeakerChars = 64;

// A row's colour and icon, from the reply's category and GUI style.
enum class Tone : std::uint8_t { Default = 0, Agree = 1, Disagree = 2, Investigate = 3, Charm = 4, Intimidate = 5, Unavailable = 6 };
// EReplyCategory (0 default, 1 agree, 2 disagree, 3 friendly, 4 hostile, 5 investigate) and EConvGUIStyles (1 charm,
// 2 intimidate, 4 illegal) to a tone.
Tone ToneOf(int category, int guiStyle);
char const* ClassOf(Tone tone);  // the RCSS class: "default", "agree", ...

enum class Look : std::uint8_t { Idle = 0, Hover = 1, Leading = 2, Dim = 3, Committed = 4, Unavailable = 5 };

struct PanelCard {
    char label[kLabelChars]{};
    char line[kLineChars]{};
    std::uint8_t tone = 0;  // Tone
    std::uint8_t look = 0;  // Look
    std::int16_t next = -1; // the word after the last one said (the teleprompter's mark), -1 none
    std::uint64_t lit = 0;  // bit i: word i of the line (as ScriptMatch's DisplayWords splits it) was said
};

struct Panel {
    std::uint32_t size = sizeof(Panel);
    std::uint8_t shown = 0;      // the panel is up (the wheel is, the mode is on)
    std::uint8_t mic = 0;        // Mic
    std::uint8_t cardCount = 0;
    std::uint8_t mainCount = 0;  // the first mainCount cards are the wheel's replies, the rest investigate topics
    float level = 0.0f;          // the microphone's level, 0..1
    std::uint32_t serial = 0;    // changes when the cards' texts do (a new wheel): the host rebuilds its rows
    std::uint8_t anchored = 0;   // speakerHead is valid
    std::uint8_t gameWheel = 0;  // the game's own wheel is shown (the stick brought it back): the host draws it as ever
    std::uint8_t pad[2]{};
    float speakerHead[3]{};      // the other person's head, world (the panel stands under the line of sight to it)
    char speaker[kSpeakerChars]{};
    char heard[kLineChars]{};    // the other person's last line
    PanelCard cards[kMaxCards]{};
};

// ---- The panel's document and place (the host and the offline preview share them) ----------------------------------------

// The canvas the document is drawn on (dp = pixels) and the quad's width; its height follows the canvas.
inline constexpr int kCanvasWidth = 1280;
inline constexpr int kCanvasHeight = 880;
inline constexpr float kPanelWidthM = 0.90f;
inline constexpr float kPanelDistanceM = 1.0f;
inline constexpr float kBelowSpeakerDeg = 15.0f;  // the panel's top edge below the line from the eyes to the speaker's head
inline constexpr float kBelowHorizonDeg = 14.0f;  // ... or below the horizon without a speaker (the 3D screen)

// The rows' markup for the document's "rows" element: one `div.row` per card (classes: its tone, its look), holding the
// label (`div.tag`) and the line (`div.line`) with one `span` per word (`said`, `next` or none) and the punctuation between
// them as it is written; the investigate topics after a `div.divider`, as `row topic`. Text is escaped.
std::string RowsRml(Panel const& panel);
// The footer's prompt for the microphone's state ("Read a line aloud", "Listening...", the taken row's label, "Didn't catch
// that"), and the class of its mark ("armed", "hearing", "understood", "unsure"; "" off).
std::string PromptText(Panel const& panel);
char const* MicClass(Mic mic);

// Where the panel stands, in the space the head and the speaker are given in (metres, Y up): its centre kPanelDistanceM
// from the eyes toward the speaker (flat), or along the head's flat forward without one, its top edge kBelowSpeakerDeg
// under the line to the speaker's head (kBelowHorizonDeg under the horizon without one, and never higher than that), facing
// the eyes. `heightM` is the quad's height.
XrPosef PlacePanel(XrVector3f const& eyes, XrVector3f const& headForward, bool hasSpeaker, XrVector3f const& speakerHead, float heightM);

}  // namespace mevr::vr::thespian

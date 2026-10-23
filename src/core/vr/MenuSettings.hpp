#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The main menu's settings, apart from RmlUi and the game (research/28): every value a row shows or edits (the
// schema the data model binds), how a slider's value reads, and the small conversions between what a row shows
// and what a setting stores (the render resolution ladder, the gun sights, the desktop mirror, the colour
// sliders). Plus the menu's queue of settings-file edits: a value is written key by key (never the whole file),
// a slider's only once it has rested for a moment. Unit-tested in tests/Main.cpp.
namespace mevr::vr::menu_settings {

enum class Type {
    Flag,    // 0 / 1, a toggle
    Number,  // a slider (with a "<key>_text" companion: the value as the row reads it)
    Choice,  // a short string, one of a row's chips
    Text,    // a string the menu only shows
};

// How a number reads next to its slider.
enum class Format {
    None,
    SignedPercent,   // "+12%"
    Percent,         // "60%"
    SignedDegrees,   // "+4°"
    Degrees,         // "35°"
    DegreesOrNever,  // "35°", 0: "Never"
    DegreesPerSec,   // "120°/s"
    Fixed1,          // "3.4"
    Fixed2,          // "0.92"
    Fixed3,          // "0.012"
    SignedFixed3,    // "-0.010"
    Times1,          // "2.0×"
    Times2,          // "1.00×"
    Meters1,         // "1.6 m"
    Meters2,         // "1.20 m"
    SignedMeters2,   // "+0.12 m"
};
std::string FormatValue(Format format, float value);

struct Field {
    char const* key;
    Type type;
    float number = 0.0f;           // the default (Flag, Number)
    char const* text = "";         // the default (Choice, Text)
    float min = 0.0f;              // Number
    float max = 1.0f;
    Format format = Format::None;
    bool local = false;            // the menu's own state (the open section...), never written to a setting
};

// Every value the menu binds, in no particular order. Keys are unique, lower case, [a-z0-9_].
std::vector<Field> const& Schema();
Field const* Find(std::string_view key);

// A number into its field's range (non-finite: the default).
float Clamp(Field const& field, float value);

// ---- Render resolution (Stereo 2's near-square target; MELE-VR.bat's ladder) ----------------------------------
struct QualityTier {
    char const* key;    // "low", "performance", "balanced", "sharp", "max"
    int width;          // the target's width; the height is EyeHeight(width)
};
std::vector<QualityTier> const& QualityTiers();
// The tier nearest to `width`.
char const* QualityFor(int width);
int QualityWidth(std::string_view key);  // 0 if unknown
// Halcyon's near-square height (vr_config.h EyeHeightForWidth: width / 0.964, rounded, even).
int EyeHeight(int width);
// "3072 × 3188".
std::string QualityText(int width);

// ---- Gun sights ([Sights] pistol, shotgun, assaultRifle, sniperRifle: none | dot | holo | scope <zoom>) ----------
// The chip a value shows: "dot", "holo", "none" or "scope" (unknown text: the default kind given).
std::string SightKind(std::string_view value, std::string_view fallback);
// What a chip writes: "scope" keeps the zoom it had (4 if none).
std::string SightValue(std::string_view kind, std::string_view previous);

// ---- Desktop mirror (Halcyon's mirrorPresentEvery: present the flat mirror every Nth frame) ---------------------
std::string MirrorChoice(int presentEvery);   // 1: "every", 2: "smooth", else "normal"
int MirrorPresentEvery(std::string_view choice);

// ---- Colour (core/vr/Colour Settings <-> the menu's sliders) --------------------------------------------------
// Brightness, contrast and saturation read as a signed percentage of the identity (1.08 -> +8); mid-tones and
// the black level as they are.
float ColourPercent(float value);
float ColourFromPercent(float percent);

// ---- Settings-file edits -------------------------------------------------------------------------------------
// Edits on their way to an ini file: one per (section, key), the latest value wins, each written when it's due
// (a slider's 300 ms after it last moved; a toggle at once). Until then a re-read of the file shows the pending
// value instead of the stale one (Overlay), so a slider being dragged never jumps back.
class PendingWrites {
public:
    struct Edit {
        std::string section;
        std::string key;
        std::string value;
        std::uint64_t dueMs = 0;
    };
    void Put(std::string section, std::string key, std::string value, std::uint64_t dueMs);
    // The edits due at `nowMs` (all with `all`), removed from the queue, oldest first.
    std::vector<Edit> TakeDue(std::uint64_t nowMs, bool all = false);
    std::optional<std::string> Overlay(std::string_view section, std::string_view key) const;
    bool Empty() const { return edits_.empty(); }
    std::size_t Size() const { return edits_.size(); }

private:
    std::vector<Edit> edits_;
};

// Parses GetPrivateProfileSection's output (key=value lines, ';' comments) into lower-cased keys and trimmed values.
std::map<std::string, std::string> ParseSection(std::string_view lines);

} // namespace mevr::vr::menu_settings

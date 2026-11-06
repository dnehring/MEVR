// SPDX-License-Identifier: MIT
#include "install/Ini.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>

namespace mevr::install::ini {
namespace {

struct Line {
    std::string text;  // without its line ending
    std::string end;   // "\r\n", "\n" or "" (the last line)
};

std::vector<Line> Split(std::string_view text) {
    std::vector<Line> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t const nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            lines.push_back({std::string(text.substr(start)), ""});
            break;
        }
        bool const cr = nl > start && text[nl - 1] == '\r';
        lines.push_back({std::string(text.substr(start, nl - start - (cr ? 1 : 0))), cr ? "\r\n" : "\n"});
        start = nl + 1;
    }
    return lines;
}

std::string Join(std::vector<Line> const& lines) {
    std::string out;
    for (Line const& line : lines) {
        out += line.text;
        out += line.end;
    }
    return out;
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

bool EqualNoCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

// The section a header line opens, or nullopt.
std::optional<std::string_view> Header(std::string_view line) {
    std::string_view const t = Trim(line);
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
        return t.substr(1, t.size() - 2);
    }
    return std::nullopt;
}

struct KeyLine {
    std::string_view key;    // trimmed
    std::size_t valueStart;  // where the value begins in the line (after "=" and its spaces)
};

std::optional<KeyLine> ParseKey(std::string_view line) {
    std::string_view const t = Trim(line);
    if (t.empty() || t.front() == ';' || t.front() == '#' || t.front() == '[') {
        return std::nullopt;
    }
    std::size_t const eq = line.find('=');
    if (eq == std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t value = eq + 1;
    while (value < line.size() && (line[value] == ' ' || line[value] == '\t')) {
        ++value;
    }
    return KeyLine{Trim(line.substr(0, eq)), value};
}

// [begin, end) line ranges of every section named `section` (its body, not the header).
std::vector<std::pair<std::size_t, std::size_t>> Bodies(std::vector<Line> const& lines, std::string_view section) {
    std::vector<std::pair<std::size_t, std::size_t>> bodies;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto const name = Header(lines[i].text);
        if (!name || !EqualNoCase(*name, section)) {
            continue;
        }
        std::size_t end = i + 1;
        while (end < lines.size() && !Header(lines[end].text)) {
            ++end;
        }
        bodies.emplace_back(i + 1, end);
        i = end - 1;
    }
    return bodies;
}

std::string LineEnding(std::vector<Line> const& lines) {
    for (Line const& line : lines) {
        if (!line.end.empty()) {
            return line.end;
        }
    }
    return "\r\n";
}

} // namespace

namespace {

// Every line of `key` in `section` (all of the section's bodies), in file order.
std::vector<std::size_t> KeyLines(std::vector<Line> const& lines, std::string_view section, std::string_view key) {
    std::vector<std::size_t> found;
    for (auto const& [begin, end] : Bodies(lines, section)) {
        for (std::size_t i = begin; i < end; ++i) {
            auto const k = ParseKey(lines[i].text);
            if (k && EqualNoCase(k->key, key)) {
                found.push_back(i);
            }
        }
    }
    return found;
}

std::string ValueOf(Line const& line) {
    auto const k = ParseKey(line.text);
    return k ? std::string(Trim(std::string_view(line.text).substr(k->valueStart))) : std::string();
}

void SetValue(Line& line, std::string_view value) {
    if (auto const k = ParseKey(line.text)) {
        line.text = line.text.substr(0, k->valueStart) + std::string(value);
    }
}

// The `n`th line of `key` in `section` (0 = the first), trimmed; nullopt when there are fewer.
std::optional<std::string> GetNth(std::string_view text, std::string_view section, std::string_view key, int n) {
    std::vector<Line> const lines = Split(text);
    std::vector<std::size_t> const at = KeyLines(lines, section, key);
    if (n < 0 || static_cast<std::size_t>(n) >= at.size()) {
        return std::nullopt;
    }
    return ValueOf(lines[at[static_cast<std::size_t>(n)]]);
}

// Only the `n`th line of `key`: the others (a duplicate's own value) stay.
std::string SetNth(std::string_view text, std::string_view section, std::string_view key, int n, std::string_view value) {
    std::vector<Line> lines = Split(text);
    std::vector<std::size_t> const at = KeyLines(lines, section, key);
    if (n < 0 || static_cast<std::size_t>(n) >= at.size()) {
        return std::string(text);
    }
    SetValue(lines[at[static_cast<std::size_t>(n)]], value);
    return Join(lines);
}

std::string RemoveNth(std::string_view text, std::string_view section, std::string_view key, int n) {
    std::vector<Line> lines = Split(text);
    std::vector<std::size_t> const at = KeyLines(lines, section, key);
    if (n < 0 || static_cast<std::size_t>(n) >= at.size()) {
        return std::string(text);
    }
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(at[static_cast<std::size_t>(n)]));
    return Join(lines);
}

} // namespace

std::optional<std::string> Get(std::string_view text, std::string_view section, std::string_view key) {
    return GetNth(text, section, key, 0);
}

void Merge(std::vector<Edit>& edits, Edit const& edit) {
    for (Edit& e : edits) {
        if (EqualNoCase(e.section, edit.section) && EqualNoCase(e.key, edit.key) && e.occurrence == edit.occurrence) {
            e.after = edit.after;
            return;
        }
    }
    edits.push_back(edit);
}

std::string Set(std::string_view text, std::string_view section, std::string_view key, std::string_view value, std::vector<Edit>* edits) {
    std::vector<Line> lines = Split(text);
    std::string const eol = LineEnding(lines);
    auto bodies = Bodies(lines, section);
    std::vector<std::size_t> const at = KeyLines(lines, section, key);
    if (bodies.empty()) {
        // A new section at the end, after a blank line.
        if (!lines.empty()) {
            if (lines.back().end.empty()) {
                lines.back().end = eol;
            }
            if (!Trim(lines.back().text).empty()) {
                lines.push_back({"", eol});
            }
        }
        lines.push_back({"[" + std::string(section) + "]", eol});
        lines.push_back({std::string(key) + "=" + std::string(value), eol});
    } else if (!at.empty()) {
        // Every line of the key, each recorded with its own old value (a duplicate's second value isn't lost).
        for (std::size_t n = 0; n < at.size(); ++n) {
            std::string const before = ValueOf(lines[at[n]]);
            SetValue(lines[at[n]], value);
            if (edits != nullptr && before != value) {
                Merge(*edits, Edit{std::string(section), std::string(key), before, std::string(value), static_cast<int>(n)});
            }
        }
        return Join(lines);
    } else {
        bool spaced = false;
        for (auto const& [begin, end] : bodies) {
            for (std::size_t i = begin; i < end; ++i) {
                if (ParseKey(lines[i].text)) {
                    spaced = spaced || lines[i].text.find(" = ") != std::string::npos;
                }
            }
        }
        // After the last section body's last non-blank line.
        auto const [begin, end] = bodies.back();
        std::size_t pos = end;
        while (pos > begin && Trim(lines[pos - 1].text).empty()) {
            --pos;
        }
        if (pos > 0 && lines[pos - 1].end.empty()) {
            lines[pos - 1].end = eol;
        }
        std::string const line = std::string(key) + (spaced ? " = " : "=") + std::string(value);
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(pos), Line{line, eol});
    }
    if (edits != nullptr) {
        Merge(*edits, Edit{std::string(section), std::string(key), std::nullopt, std::string(value), 0});
    }
    return Join(lines);
}

std::string Remove(std::string_view text, std::string_view section, std::string_view key) {
    std::vector<Line> lines = Split(text);
    std::vector<bool> drop(lines.size(), false);
    for (auto const& [begin, end] : Bodies(lines, section)) {
        for (std::size_t i = begin; i < end; ++i) {
            auto const k = ParseKey(lines[i].text);
            if (k && EqualNoCase(k->key, key)) {
                drop[i] = true;
            }
        }
    }
    std::vector<Line> kept;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!drop[i]) {
            kept.push_back(lines[i]);
        }
    }
    return Join(kept);
}

namespace {

// Removes `section` if nothing but blank lines is left in it (a section an edit added), with the blank line before it.
std::string DropIfEmpty(std::string_view text, std::string_view section) {
    std::vector<Line> lines = Split(text);
    auto const bodies = Bodies(lines, section);
    if (bodies.size() != 1) {
        return std::string(text);
    }
    auto const [begin, end] = bodies.front();
    for (std::size_t i = begin; i < end; ++i) {
        if (!Trim(lines[i].text).empty()) {
            return std::string(text);
        }
    }
    std::size_t from = begin - 1;  // the header
    if (from > 0 && Trim(lines[from - 1].text).empty()) {
        --from;
    }
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(from), lines.begin() + static_cast<std::ptrdiff_t>(end));
    return Join(lines);
}

} // namespace

void Record(std::string_view text, std::string_view section, std::string_view key, std::vector<Edit>* edits) {
    if (edits == nullptr) {
        return;
    }
    for (Edit const& e : *edits) {
        if (EqualNoCase(e.section, section) && EqualNoCase(e.key, key) && e.occurrence == 0) {
            return;
        }
    }
    std::optional<std::string> const now = Get(text, section, key);
    edits->push_back(Edit{std::string(section), std::string(key), now, now.value_or(std::string()), 0});
}

std::string Revert(std::string_view text, std::vector<Edit> const& edits, Ours const& ours) {
    std::string out(text);
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        auto const now = GetNth(out, it->section, it->key, it->occurrence);
        if (!now || (*now != it->after && !(ours && ours(*it, *now)))) {
            continue;  // the player's own value since: theirs
        }
        if (it->before && *now == *it->before) {
            continue;  // already the player's (a recorded value the mod never changed)
        }
        if (it->before) {
            out = SetNth(out, it->section, it->key, it->occurrence, *it->before);
        } else {
            out = DropIfEmpty(RemoveNth(out, it->section, it->key, it->occurrence), it->section);
        }
    }
    return out;
}

std::vector<std::string> Sections(std::string_view text) {
    std::vector<std::string> names;
    for (Line const& line : Split(text)) {
        if (auto const name = Header(line.text)) {
            names.emplace_back(*name);
        }
    }
    return names;
}

std::string HalcyonReleaseIni() {
    // tools/deploy-asi.ps1's block, one per slot (Halcyon keeps every setting per profile slot, and a slot missing
    // from the file loads its code defaults: Insert, head aim on, third person).
    static constexpr char const* kSlot[] = {
        "vrMode = 2",
        "sfr2ResX = 4096", "sfr2ResY = 4250",
        "monoResX = 3840", "monoResY = 2160",
        "aerResX = 3072", "aerResY = 3072",
        "dibrResX = 3072", "dibrResY = 3072",
        "expectedResX = 0", "expectedResY = 0",
        "firstRunDone = 1",
        "menuKey = 220",
        "fpEnabled = 1",
        "combatHeadAim = 0", "makoHeadAim = 0",
        "mirrorPresentEvery = 2", "depthPopKeyEnabled = 0",
        "sfr2Metric = 1",
        // The owner's own profile (2026-11-05: the dev settings are the defaults): the right stick doesn't tilt the view
        // (only the head does), the view framed 7.8 cm right and 8.2 cm down, and the flat screen's dolly at 0.674 m.
        "decoupledPitch = 1",
        "cameraOffsetEnabled = 1", "cameraOffsetRight = 0.078", "cameraOffsetUp = -0.082",
        "screenDistanceEnabled = 1", "flatScreenDistance = 0.674",
    };
    std::string out = "; MELE VR config - written by the in-headset menu.\r\n\r\n[active]\r\nprofile=Default\r\n\r\n"
                      "[hotkeys]\r\nenabled=0\r\nprofile0=112\r\nprofile1=113\r\nprofile2=114\r\nprofile3=115\r\n\r\n";
    for (char const* name : {"Default", "Custom 1", "Custom 2", "Custom 3"}) {
        out += std::string("[profile:") + name + "]\r\n";
        for (char const* line : kSlot) {
            out += line;
            out += "\r\n";
        }
        out += "\r\n";
    }
    return out;
}

namespace {
// Profile keys only MEVR's copy of Halcyon reads and writes (its save writes every one; upstream's code has none of them).
constexpr char const* kMevrOnlyKeys[] = {"sfr2Metric", "sfr2MetricDepth", "sfr2MetricEyeFrustum", "worldLockedPanels", "recenterTiltDeg",
    "hdrPaperWhiteNits"};
} // namespace

std::string FixHalcyonIni(std::string_view text, std::vector<Edit>* edits) {
    std::string out(text);
    if (auto const v = Get(out, "hotkeys", "enabled"); v && *v == "1") {
        out = Set(out, "hotkeys", "enabled", "0", edits);
    }
    for (std::string const& section : Sections(text)) {
        if (section.rfind("profile:", 0) != 0) {
            continue;
        }
        // A slot this mod never wrote: none of the keys only MEVR's Halcyon knows ([MEVR] seams in vr_config.cpp; every
        // MEVR path writes sfr2Metric: the setup's file, tools/deploy-asi.ps1, the fix below; the in-game save writes them
        // all, so a slot saved in game stays recognised after an uninstall put back the sfr2Metric we added). So Halcyon's
        // upstream MELE-VR file, or an old starting file, copied in: its mode, first person, mirror rate and depth-pop key
        // are upstream's (AER or DIBR, third person, every frame, P on), not choices made in this mod's menu.
        bool const foreign = std::none_of(std::begin(kMevrOnlyKeys), std::end(kMevrOnlyKeys),
            [&](char const* key) { return Get(text, section, key).has_value(); });
        // A sparse slot loads Halcyon's code defaults for what it lacks (Insert, head aim on): written in when missing.
        if (auto const v = Get(out, section, "menuKey"); !v || *v != "220") {
            out = Set(out, section, "menuKey", "220", edits);
        }
        for (char const* key : {"combatHeadAim", "makoHeadAim"}) {
            if (auto const v = Get(out, section, key); !v || *v == "1") {
                out = Set(out, section, key, "0", edits);
            }
        }
        if (auto const v = Get(out, section, "hudPanelFollowDeg"); v && *v == "50.0") {
            out = Set(out, section, "hudPanelFollowDeg", "70.0", edits);
        }
        // Halcyon's welcome ("INSERT opens this menu") is never right in this mod: seen, in every slot.
        if (auto const v = Get(out, section, "firstRunDone"); !v || *v != "1") {
            out = Set(out, section, "firstRunDone", "1", edits);
        }
        // What a fresh install writes (HalcyonReleaseIni): in a foreign slot always, in ours only where missing (a missing
        // key loads Halcyon's code default: third person, the mirror every frame, the depth-pop key on; vrMode's default
        // is already Stereo). Where the slot is ours, the player's own choice from the menu stays.
        struct Fresh {
            char const* key;
            char const* value;
            bool whenMissing;
        };
        for (Fresh const& f : {Fresh{"vrMode", "2", false}, Fresh{"fpEnabled", "1", true}, Fresh{"mirrorPresentEvery", "2", true},
                 Fresh{"depthPopKeyEnabled", "0", true}}) {
            auto const v = Get(out, section, f.key);
            if ((!v && (foreign || f.whenMissing)) || (v && foreign && *v != f.value)) {
                out = Set(out, section, f.key, f.value, edits);
            }
        }
        if (!Get(out, section, "sfr2Metric")) {
            out = Set(out, section, "sfr2Metric", "1", edits);
        }
    }
    return out;
}

std::string SetHalcyonResolution(std::string_view text, int width, int height, std::vector<Edit>* edits) {
    std::string out(text);
    for (std::string const& section : Sections(text)) {
        if (section.rfind("profile:", 0) != 0) {
            continue;
        }
        for (auto const& [key, value] : {std::pair{"sfr2ResX", width}, std::pair{"sfr2ResY", height}, std::pair{"expectedResX", width},
                 std::pair{"expectedResY", height}}) {
            if (Get(out, section, key)) {
                out = Set(out, section, key, std::to_string(value), edits);
            }
        }
    }
    return out;
}

} // namespace mevr::install::ini

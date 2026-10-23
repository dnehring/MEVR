// SPDX-License-Identifier: MIT
#include "install/Ini.hpp"

#include <algorithm>
#include <cctype>

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

std::optional<std::string> Get(std::string_view text, std::string_view section, std::string_view key) {
    std::vector<Line> const lines = Split(text);
    for (auto const& [begin, end] : Bodies(lines, section)) {
        for (std::size_t i = begin; i < end; ++i) {
            auto const k = ParseKey(lines[i].text);
            if (k && EqualNoCase(k->key, key)) {
                return std::string(Trim(std::string_view(lines[i].text).substr(k->valueStart)));
            }
        }
    }
    return std::nullopt;
}

void Merge(std::vector<Edit>& edits, Edit const& edit) {
    for (Edit& e : edits) {
        if (EqualNoCase(e.section, edit.section) && EqualNoCase(e.key, edit.key)) {
            e.after = edit.after;
            return;
        }
    }
    edits.push_back(edit);
}

std::string Set(std::string_view text, std::string_view section, std::string_view key, std::string_view value, std::vector<Edit>* edits) {
    std::vector<Line> lines = Split(text);
    std::string const eol = LineEnding(lines);
    auto const before = Get(text, section, key);
    auto bodies = Bodies(lines, section);
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
    } else {
        bool found = false;
        bool spaced = false;
        for (auto const& [begin, end] : bodies) {
            for (std::size_t i = begin; i < end; ++i) {
                auto const k = ParseKey(lines[i].text);
                if (!k) {
                    continue;
                }
                spaced = spaced || lines[i].text.find(" = ") != std::string::npos;
                if (EqualNoCase(k->key, key)) {
                    lines[i].text = lines[i].text.substr(0, k->valueStart) + std::string(value);
                    found = true;
                }
            }
        }
        if (!found) {
            // After the last section body's last non-blank line.
            auto const [begin, end] = bodies.back();
            std::size_t at = end;
            while (at > begin && Trim(lines[at - 1].text).empty()) {
                --at;
            }
            if (at > 0 && lines[at - 1].end.empty()) {
                lines[at - 1].end = eol;
            }
            std::string const line = std::string(key) + (spaced ? " = " : "=") + std::string(value);
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), Line{line, eol});
        }
    }
    if (edits != nullptr && (!before || *before != value)) {
        Merge(*edits, Edit{std::string(section), std::string(key), before, std::string(value)});
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

std::string Revert(std::string_view text, std::vector<Edit> const& edits) {
    std::string out(text);
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        auto const now = Get(out, it->section, it->key);
        if (!now || *now != it->after) {
            continue;  // the player's own value since: theirs
        }
        if (it->before) {
            out = Set(out, it->section, it->key, *it->before);
        } else {
            out = DropIfEmpty(Remove(out, it->section, it->key), it->section);
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

std::string FixHalcyonIni(std::string_view text, std::vector<Edit>* edits) {
    std::string out(text);
    if (auto const v = Get(out, "hotkeys", "enabled"); v && *v == "1") {
        out = Set(out, "hotkeys", "enabled", "0", edits);
    }
    for (std::string const& section : Sections(text)) {
        if (section.rfind("profile:", 0) != 0) {
            continue;
        }
        if (auto const v = Get(out, section, "menuKey"); v && *v != "220") {
            out = Set(out, section, "menuKey", "220", edits);
        }
        for (char const* key : {"combatHeadAim", "makoHeadAim"}) {
            if (auto const v = Get(out, section, key); v && *v == "1") {
                out = Set(out, section, key, "0", edits);
            }
        }
        if (auto const v = Get(out, section, "hudPanelFollowDeg"); v && *v == "50.0") {
            out = Set(out, section, "hudPanelFollowDeg", "70.0", edits);
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

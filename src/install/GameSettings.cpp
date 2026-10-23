#include "install/GameSettings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace mevr::install::settings {
namespace {

// Halcyon's ladder (MELE-VR.bat; core/vr/MenuSettings' QualityTiers, kept apart so this library stands alone).
struct Tier {
    char const* key;
    int width;
};
constexpr Tier kTiers[] = {{"low", 2048}, {"performance", 3072}, {"balanced", 4096}, {"sharp", 5120}, {"max", 6144}};

// vr_config.h EyeHeightForWidth: width / 0.964, rounded, made even.
int EyeHeight(int width) {
    int h = static_cast<int>(static_cast<float>(width) / 0.964f + 0.5f);
    return (h & 1) != 0 ? h + 1 : h;
}

int Even(int v) {
    return (v & 1) != 0 ? v + 1 : v;
}

bool IsTrue(std::optional<std::string> const& v, bool fallback) {
    if (!v) {
        return fallback;
    }
    std::string s = *v;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s == "true" || s == "1";
}

} // namespace

Target TargetFor(std::string_view choice, int headsetWidth, int headsetHeight) {
    if (choice == "headset") {
        if (headsetWidth > 0 && headsetHeight > 0) {
            return {Even(headsetWidth), Even(headsetHeight)};
        }
        return {};
    }
    for (Tier const& tier : kTiers) {
        if (choice == tier.key) {
            return {tier.width, EyeHeight(tier.width)};
        }
    }
    return {};
}

std::string ApplyGamerSettings(std::string_view text, Choices const& c, std::vector<ini::Edit>* edits) {
    std::string out(text);
    auto set = [&](char const* section, char const* key, std::string const& value) { out = ini::Set(out, section, key, value, edits); };
    if (c.vsyncOff) {
        set("SystemSettings", "UseVSync", "False");
    }
    if (c.motionBlurOff) {
        set("SystemSettings", "MotionBlur", "False");
    }
    if (c.filmGrainOff) {
        set("SystemSettings", "FilmGrain", "False");
    }
    if (c.anisotropy16) {
        set("SystemSettings", "MaxAnisotropy", "16");
    }
    if (c.target.width > 0 && c.target.height > 0) {
        set("SystemSettings", "ResX", std::to_string(c.target.width));
        set("SystemSettings", "ResY", std::to_string(c.target.height));
    }
    set("SystemSettings", "BorderlessWindow", "True");
    set("SystemSettings", "Fullscreen", "False");
    set("SystemSettings", "DynamicShadows", "False");
    if (c.hdrOff) {
        set("HDR", "DesiredRange", "DynamicRange_SDR");
    }
    return out;
}

std::string ApplyEngineSettings(std::string_view text, std::vector<ini::Edit>* edits) {
    std::string out(text);
    for (std::string const& section : ini::Sections(text)) {
        auto const v = ini::Get(out, section, "SmoothFrameRate");
        if (v && IsTrue(v, false)) {
            out = ini::Set(out, section, "SmoothFrameRate", "FALSE", edits);
        }
    }
    return out;
}

Current Read(std::string_view text) {
    Current c;
    auto number = [&](char const* key) {
        auto const v = ini::Get(text, "SystemSettings", key);
        return v ? std::atoi(v->c_str()) : 0;
    };
    c.resX = number("ResX");
    c.resY = number("ResY");
    c.borderless = IsTrue(ini::Get(text, "SystemSettings", "BorderlessWindow"), false);
    c.vsync = IsTrue(ini::Get(text, "SystemSettings", "UseVSync"), true);
    c.motionBlur = IsTrue(ini::Get(text, "SystemSettings", "MotionBlur"), true);
    c.filmGrain = IsTrue(ini::Get(text, "SystemSettings", "FilmGrain"), true);
    auto const hdr = ini::Get(text, "HDR", "DesiredRange");
    c.hdr = hdr && *hdr != "DynamicRange_SDR";
    return c;
}

} // namespace mevr::install::settings

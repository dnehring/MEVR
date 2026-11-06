// SPDX-License-Identifier: MIT
// Offline tests for Display's Performance row (core/vr/PerformanceModes, research/60): the modes' bundles, which mode the
// files read as, the Custom line, and the edits a mode writes.

#include "core/vr/MenuSettings.hpp"
#include "core/vr/PerformanceModes.hpp"

#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/PerformanceModesTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace pm = mevr::vr::perf_modes;
namespace ms = mevr::vr::menu_settings;

// A settings file as (section, key) -> value, read the way the menu's getter reads one.
struct File {
    std::map<std::pair<std::string, std::string>, std::string> values;
    pm::Getter Getter() const {
        return [this](std::string_view section, std::string_view key) -> std::optional<std::string> {
            auto const it = values.find({std::string(section), std::string(key)});
            return it != values.end() ? std::optional<std::string>(it->second) : std::nullopt;
        };
    }
};

void TestModes() {
    CHECK(pm::Modes().size() == 3);
    CHECK(std::string_view(pm::Modes()[0].key) == "quality");
    CHECK(std::string_view(pm::Modes()[1].key) == "balanced");
    CHECK(std::string_view(pm::Modes()[2].key) == "performance");
    CHECK(pm::Find("balanced") != nullptr && pm::Find("custom") == nullptr && pm::Find("") == nullptr);
    for (pm::Mode const& mode : pm::Modes()) {
        // Each mode reads back as itself, has a description, and its size is one of the Render resolution row's chips.
        CHECK(pm::ModeFor(mode.levers) == mode.key);
        CHECK(pm::Description(mode.levers) == mode.description);
        CHECK(std::string_view(mode.description).size() > 20);
        CHECK(ms::HeadsetSize(mode.levers.resolution, 2824, 2880).width > 0);
        // No two modes are the same bundle.
        for (pm::Mode const& other : pm::Modes()) {
            CHECK(&other == &mode || !pm::Same(other.levers, mode.levers));
        }
    }
    // Balanced is the shipped defaults: what each module does with no key at all, at the first start's 0.85 of the eye.
    CHECK(pm::Same(pm::Find(pm::kDefault)->levers, pm::Levers{}));
    CHECK(pm::ModeFor(pm::Current(File{}.Getter(), "headset85")) == "balanced");
    // The menu's schema starts on the same mode.
    ms::Field const* field = ms::Find("perf_mode");
    CHECK(field != nullptr && std::string_view(field->text) == pm::kDefault);
    CHECK(ms::Find("cull_px") != nullptr && ms::Find("grass") != nullptr && ms::Find("dyn_shadows") != nullptr &&
          ms::Find("ambient_occl") != nullptr && ms::Find("fsr_sharpness") != nullptr && ms::Find("perf_desc") != nullptr &&
          ms::Find("perf_note") != nullptr);
    // Lighter to heavier: Performance renders no more pixels than Balanced, Balanced no more than Quality.
    auto pixels = [](char const* key) {
        ms::Size const s = ms::HeadsetSize(pm::Find(key)->levers.resolution, 2824, 2880);
        return s.width * s.height;
    };
    CHECK(pixels("performance") < pixels("balanced") && pixels("balanced") < pixels("quality"));
    CHECK(pm::Find("performance")->levers.minScreenRadiusPx >= pm::Find("balanced")->levers.minScreenRadiusPx);
    CHECK(pm::Find("quality")->levers.minScreenRadiusPx <= pm::Find("balanced")->levers.minScreenRadiusPx);
    CHECK(pm::Find("performance")->levers.foliage <= pm::Find("balanced")->levers.foliage);
    CHECK(pm::Find("quality")->levers.foliage >= pm::Find("balanced")->levers.foliage);
}

void TestCurrent() {
    // The file's spellings: booleans in any case or as 1/0, numbers with decimals, the upscale's aliases.
    File f;
    f.values[{"Upscale", "method"}] = "FSR1";
    f.values[{"Performance", "minScreenRadiusPx"}] = "24";
    f.values[{"SystemSettings", "FoliageDrawRadiusMultiplier"}] = "0.350000";
    f.values[{"SystemSettings", "DynamicShadows"}] = "false";
    f.values[{"SystemSettings", "AmbientOcclusion"}] = "0";
    pm::Levers const l = pm::Current(f.Getter(), "headset75");
    CHECK(l.fsr && l.minScreenRadiusPx == 24 && l.foliage > 0.349f && l.foliage < 0.351f && !l.dynamicShadows && !l.ambientOcclusion);
    CHECK(pm::ModeFor(l) == "performance");
    // plugin/RenderLevers reads minScreenRadiusTenths before minScreenRadiusPx; an empty one is the default.
    f.values[{"Performance", "minScreenRadiusTenths"}] = "80";
    CHECK(pm::Current(f.Getter(), "headset75").minScreenRadiusPx == 8);
    f.values[{"Performance", "minScreenRadiusTenths"}] = "";
    CHECK(pm::Current(f.Getter(), "headset75").minScreenRadiusPx == 24);
    // Off, any spelling core/vr/Upscale reads as off.
    f.values[{"Upscale", "method"}] = "bilinear";
    CHECK(!pm::Current(f.Getter(), "headset75").fsr);
    // An unreadable value keeps the module's default.
    File g;
    g.values[{"SystemSettings", "DynamicShadows"}] = "maybe";
    g.values[{"Performance", "minScreenRadiusPx"}] = "";
    CHECK(!pm::Current(g.Getter(), "headset85").dynamicShadows && pm::Current(g.Getter(), "headset85").minScreenRadiusPx == 8);
    // A fixed (ladder) size or a size no chip makes is no mode's.
    CHECK(pm::ModeFor(pm::Current(File{}.Getter(), "performance")) == "custom");
    CHECK(pm::ModeFor(pm::Current(File{}.Getter(), "")) == "custom");
}

void TestEdits() {
    for (pm::Mode const& mode : pm::Modes()) {
        File f;
        for (pm::Edit const& e : pm::Edits(mode.levers)) {
            f.values[{e.section, e.key}] = e.value;
            CHECK(e.key != "MotionBlur");  // motion blur stays off whatever the mode (gotchas/motion-blur-in-stereo)
        }
        // What a mode writes reads back as that mode, with the size the caller sets.
        CHECK(pm::ModeFor(pm::Current(f.Getter(), mode.levers.resolution)) == mode.key);
    }
    std::vector<pm::Edit> const edits = pm::Edits(pm::Levers{});
    CHECK(edits.size() == 5);
    CHECK(edits[0].section == "Upscale" && edits[0].key == "method" && edits[0].value == "fsr");
    CHECK(edits[1].section == "Performance" && edits[1].key == "minScreenRadiusPx" && edits[1].value == "8");
    CHECK(edits[2].section == "SystemSettings" && edits[2].key == "FoliageDrawRadiusMultiplier" && edits[2].value == "0.50");
    CHECK(edits[3].key == "DynamicShadows" && edits[3].value == "False");
    CHECK(edits[4].key == "AmbientOcclusion" && edits[4].value == "True");
}

void TestCustom() {
    pm::Levers mix = pm::Find("balanced")->levers;
    mix.foliage = 0.75f;
    CHECK(pm::ModeFor(mix) == "custom");
    CHECK(pm::Differences(mix, pm::Find("balanced")->levers) == "grass at 75%");
    CHECK(pm::Description(mix) == "Your own mix: Balanced with grass at 75%");
    mix.minScreenRadiusPx = 0;
    mix.dynamicShadows = true;
    CHECK(pm::Differences(mix, pm::Find("balanced")->levers) == "no small-object culling, grass at 75%, dynamic shadows on");
    // The nearest mode names the mix: Performance's size with Balanced's levers is "Balanced with render size ..."
    pm::Levers small = pm::Find("balanced")->levers;
    small.resolution = "headset75";
    CHECK(std::string_view(pm::Nearest(small).key) == "balanced");
    CHECK(pm::Description(small) == "Your own mix: Balanced with render size Headset \xC3\x97 0.75");
    // Within the menu's 5% step the grass is the same; a hair off it isn't Custom.
    pm::Levers close = pm::Find("balanced")->levers;
    close.foliage = 0.51f;
    CHECK(pm::ModeFor(close) == "balanced");
    CHECK(pm::Differences(pm::Levers{}, pm::Levers{}).empty());
    // A tie names the mix against Balanced: Balanced's size with Quality's cull and a 16 px cull differs from both by two.
    pm::Levers tie = pm::Find("balanced")->levers;
    tie.foliage = 0.75f;
    tie.minScreenRadiusPx = 16;
    CHECK(std::string_view(pm::Nearest(tie).key) == "balanced");
    CHECK(pm::Description(tie) == "Your own mix: Balanced with small objects culled under 16 px, grass at 75%");
}

void TestRows() {
    CHECK(pm::CullChoice(0) == "0" && pm::CullChoice(8) == "8" && pm::CullChoice(9) == "8" && pm::CullChoice(13) == "16");
    CHECK(pm::CullChoice(-5) == "0" && pm::CullChoice(100) == "32" && pm::CullChoice(20) == "16");
    CHECK(pm::FoliagePercent(0.5f) == 50.0f && pm::FoliagePercent(0.35f) == 35.0f && pm::FoliagePercent(1.0f) == 100.0f);
    CHECK(pm::FoliageFromPercent(75.0f) == 0.75f && pm::FoliageFromPercent(-1.0f) == 0.0f);
    CHECK(pm::RestartNote(true).find("restart") != std::string::npos);
    CHECK(pm::RestartNote(false).find("restart") == std::string::npos);
}

} // namespace

int RunPerformanceModesTests() {
    gFailures = 0;
    TestModes();
    TestCurrent();
    TestEdits();
    TestCustom();
    TestRows();
    return gFailures;
}

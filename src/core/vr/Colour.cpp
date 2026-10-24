// SPDX-License-Identifier: MIT
#include "core/vr/Colour.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace mevr::vr::colour {
namespace {

constexpr float kTolerance = 0.0005f;  // half a step of the menu's two-decimal sliders

bool Near(float a, float b) {
    return std::abs(a - b) <= kTolerance;
}

float Clamp(float v, float lo, float hi, float identity) {
    if (!std::isfinite(v)) {
        return identity;
    }
    return std::clamp(v, lo, hi);
}

std::string Lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char const c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

bool Contains(std::string_view haystack, std::string_view needle) {
    return Lower(haystack).find(Lower(needle)) != std::string::npos;
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

// As the guided calibration spells the system in its section keys (core/vr/Calibration.cpp).
std::string Sanitize(std::string_view text) {
    std::string out;
    bool lastUnderscore = false;
    for (char const raw : text) {
        unsigned char const c = static_cast<unsigned char>(raw);
        if (std::isalnum(c) != 0) {
            out.push_back(static_cast<char>(std::tolower(c)));
            lastUnderscore = false;
        } else if (!lastUnderscore && !out.empty()) {
            out.push_back('_');
            lastUnderscore = true;
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out;
}

std::optional<float> Number(Section const& section, char const* key) {
    auto const it = section.find(key);
    if (it == section.end() || it->second.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    double const v = std::strtod(it->second.c_str(), &end);
    if (end == it->second.c_str() || !std::isfinite(v)) {
        return std::nullopt;
    }
    return static_cast<float>(v);
}

// The headset's section first, then the common one.
std::optional<float> Number(Section const& headset, Section const& common, char const* key) {
    if (auto const v = Number(headset, key)) {
        return v;
    }
    return Number(common, key);
}

} // namespace

bool IsNeutral(Settings const& s) {
    return Near(s.brightness, 1.0f) && Near(s.contrast, 1.0f) && Near(s.saturation, 1.0f) && Near(s.gamma, 1.0f) &&
        Near(s.black, 0.0f) && Near(s.gain[0], 1.0f) && Near(s.gain[1], 1.0f) && Near(s.gain[2], 1.0f);
}

Settings Clamped(Settings s) {
    s.brightness = Clamp(s.brightness, 0.5f, 1.5f, 1.0f);
    s.contrast = Clamp(s.contrast, 0.5f, 1.5f, 1.0f);
    s.saturation = Clamp(s.saturation, 0.0f, 2.0f, 1.0f);
    s.gamma = Clamp(s.gamma, 0.5f, 2.0f, 1.0f);
    s.black = Clamp(s.black, -0.2f, 0.2f, 0.0f);
    for (float& g : s.gain) {
        g = Clamp(g, 0.5f, 1.5f, 1.0f);
    }
    return s;
}

char const* Name(Profile p) {
    switch (p) {
    case Profile::Auto: return "auto";
    case Profile::Neutral: return "neutral";
    case Profile::Quest: return "quest";
    case Profile::Vivid: return "vivid";
    case Profile::Custom: return "custom";
    }
    return "auto";
}

char const* Name(Tonemap t) {
    switch (t) {
    case Tonemap::Clip: return "clip";
    case Tonemap::Aces: return "aces";
    case Tonemap::Hable: return "hable";
    case Tonemap::Reinhard: return "reinhard";
    }
    return "clip";
}

std::optional<Tonemap> ParseTonemap(std::string_view text) {
    std::string const t = Lower(Trim(text));
    if (t == "clip" || t == "off" || t == "none" || t == "0") return Tonemap::Clip;
    if (t == "aces") return Tonemap::Aces;
    if (t == "hable" || t == "filmic" || t == "uncharted") return Tonemap::Hable;
    if (t == "reinhard") return Tonemap::Reinhard;
    return std::nullopt;
}

Hdr ClampedHdr(Hdr h) {
    h.exposure = Clamp(h.exposure, -3.0f, 3.0f, 0.0f);
    h.white = Clamp(h.white, 1.0f, 16.0f, 4.0f);
    return h;
}

bool TonemapActive(Hdr const& h) {
    return h.tonemap != Tonemap::Clip;
}

char const* Label(Profile p) {
    switch (p) {
    case Profile::Auto: return "Automatic";
    case Profile::Neutral: return "Neutral";
    case Profile::Quest: return "Quest streamed";
    case Profile::Vivid: return "Vivid";
    case Profile::Custom: return "Custom";
    }
    return "Automatic";
}

std::optional<Profile> ParseProfile(std::string_view text) {
    std::string const t = Lower(Trim(text));
    for (Profile const p : {Profile::Auto, Profile::Neutral, Profile::Quest, Profile::Vivid, Profile::Custom}) {
        if (t == Name(p)) {
            return p;
        }
    }
    if (t == "quest streamed" || t == "quest_streamed" || t == "queststreamed") {
        return Profile::Quest;
    }
    return std::nullopt;
}

Settings Preset(Profile p) {
    Settings s;
    switch (p) {
    case Profile::Quest:
        // research/27: a conservative step in the direction Quest owners correct the streamed picture
        // (Virtual Desktop gamma about 0.7, OpenXR Toolkit contrast and saturation up a little), small enough
        // that the shadow steps from about 20/255 up stay above the LCD's floor (Meta: below 13/255 can't be
        // told apart). Judgement calls, not measurements: no one has published measurements of the Quest 3's
        // streamed picture.
        s.contrast = 1.06f;
        s.saturation = 1.12f;
        s.gamma = 0.90f;
        break;
    case Profile::Vivid:
        s.contrast = 1.12f;
        s.saturation = 1.25f;
        s.gamma = 0.85f;
        break;
    case Profile::Auto:
    case Profile::Neutral:
    case Profile::Custom:
        break;
    }
    return s;
}

Detection Detect(std::string_view runtimeName, std::string_view systemName) {
    Detection d;
    // SteamVR: "SteamVR/OpenXR : <tracking system>", the headset driver's own name.
    std::string_view tracking;
    if (Contains(runtimeName, "steamvr")) {
        auto const colon = systemName.rfind(':');
        tracking = colon == std::string_view::npos ? std::string_view{} : Trim(systemName.substr(colon + 1));
    }
    auto streamed = [&](std::string why) {
        d.display = Display::QuestStreamed;
        d.automatic = Profile::Quest;
        d.why = std::move(why);
        return d;
    };
    if (Contains(systemName, "playstation_vr2") || Contains(systemName, "PlayStation VR2") ||
        Contains(systemName, "PS VR2") || Contains(systemName, "PSVR2")) {
        d.display = Display::Oled;
        d.why = "PS VR2 (OLED): neutral";
        return d;
    }
    if (Contains(systemName, "quest")) {
        return streamed(std::string("a Quest (") + std::string(systemName) + "): streamed LCD");
    }
    if (Contains(runtimeName, "VirtualDesktop")) {
        return streamed("Virtual Desktop's runtime (a streamed standalone headset)");
    }
    if (!tracking.empty()) {
        std::string const t = Lower(tracking);
        if (t == "oculus") {
            return streamed("a Meta headset through SteamVR (Link, Air Link, Virtual Desktop or ALVR): streamed LCD");
        }
        if (t == "vrlink" || t.find("steamlink") != std::string::npos) {
            return streamed("Steam Link (a streamed Quest): streamed LCD");
        }
        if (t.find("virtualdesktop") != std::string::npos || t.find("alvr") != std::string::npos) {
            return streamed("a streamed headset through SteamVR (" + std::string(tracking) + ")");
        }
        if (t == "lighthouse" || t == "holographic" || t == "aapvr") {
            d.display = Display::Wired;
            d.why = "a PC headset through SteamVR (" + std::string(tracking) + "): neutral";
            return d;
        }
        d.why = "an unknown SteamVR headset (" + std::string(tracking) + "): neutral";
        return d;
    }
    if (Contains(systemName, "rift")) {
        d.display = Display::Wired;
        d.why = "a Rift (not streamed): neutral";
        return d;
    }
    d.why = systemName.empty() ? std::string("headset not known yet: neutral")
                               : "unknown headset (" + std::string(systemName) + "): neutral";
    return d;
}

std::string HeadsetKey(std::string_view runtimeName, std::string_view systemName) {
    std::string const system = Sanitize(systemName);
    if (system.empty()) {
        return {};
    }
    std::string const runtime = Sanitize(runtimeName);
    std::string key = runtime.empty() || system.starts_with(runtime) ? system : runtime + "__" + system;
    if (key.size() > 90) {
        key.resize(90);
    }
    return key;
}

Section ParseSection(std::string_view lines) {
    Section out;
    std::size_t start = 0;
    while (start < lines.size()) {
        std::size_t end = lines.find_first_of("\n", start);
        if (end == std::string_view::npos) {
            end = lines.size();
        }
        std::string_view line = Trim(lines.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[') {
            continue;
        }
        auto const eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        std::string_view value = Trim(line.substr(eq + 1));
        if (auto const comment = value.find(';'); comment != std::string_view::npos) {
            value = Trim(value.substr(0, comment));
        }
        std::string const key = Lower(Trim(line.substr(0, eq)));
        if (!key.empty()) {
            out[key] = std::string(value);
        }
    }
    return out;
}

Resolved Resolve(Section const& common, Section const& headset, Detection const& detection) {
    Resolved r;
    auto profileOf = [](Section const& s) -> std::optional<Profile> {
        auto const it = s.find("profile");
        return it == s.end() ? std::nullopt : ParseProfile(it->second);
    };
    if (auto const p = profileOf(headset)) {
        r.chosen = *p;
    } else if (auto const q = profileOf(common)) {
        r.chosen = *q;
    }
    r.effective = r.chosen == Profile::Auto ? detection.automatic : r.chosen;
    if (r.effective == Profile::Auto) {
        r.effective = Profile::Neutral;
    }

    Settings custom;
    struct Key {
        char const* name;
        float* value;
    };
    Key const keys[] = {{"brightness", &custom.brightness}, {"contrast", &custom.contrast},
        {"saturation", &custom.saturation}, {"gamma", &custom.gamma}, {"black", &custom.black},
        {"gainr", &custom.gain[0]}, {"gaing", &custom.gain[1]}, {"gainb", &custom.gain[2]}};
    for (Key const& k : keys) {
        if (auto const v = Number(headset, common, k.name)) {
            *k.value = *v;
        }
    }
    r.custom = Clamped(custom);
    r.settings = r.effective == Profile::Custom ? r.custom : Preset(r.effective);

    if (auto const c = Number(common, "compare")) {
        r.compare = *c != 0.0f;
    }
    if (auto const d = Number(common, "dither")) {
        r.dither = std::clamp(*d, 0.0f, 2.0f);
    }
    auto tonemapOf = [](Section const& s) -> std::optional<Tonemap> {
        auto const it = s.find("tonemap");
        return it == s.end() ? std::nullopt : ParseTonemap(it->second);
    };
    if (auto const t = tonemapOf(headset)) {
        r.hdr.tonemap = *t;
    } else if (auto const u = tonemapOf(common)) {
        r.hdr.tonemap = *u;
    }
    if (auto const e = Number(headset, common, "exposure")) {
        r.hdr.exposure = *e;
    }
    if (auto const w = Number(headset, common, "white")) {
        r.hdr.white = *w;
    }
    r.hdr = ClampedHdr(r.hdr);
    return r;
}

Constants MakeConstants(Settings const& in, float ditherSteps, bool linearSource, std::uint32_t seed) {
    return MakeConstants(in, ditherSteps, linearSource, seed, Hdr{});
}

Constants MakeConstants(Settings const& in, float ditherSteps, bool linearSource, std::uint32_t seed, Hdr const& hdrIn) {
    Settings const s = Clamped(in);
    Hdr const h = ClampedHdr(hdrIn);
    Constants k{};
    k.tone[0] = s.black;
    k.tone[1] = 1.0f / s.gamma;
    k.tone[2] = 2.0f * (s.contrast - 1.0f);
    k.tone[3] = s.saturation;
    for (int c = 0; c < 3; ++c) {
        k.gain[c] = s.gain[c] * s.brightness;
    }
    k.misc[0] = std::clamp(ditherSteps, 0.0f, 2.0f) / 255.0f;
    k.misc[1] = linearSource ? 1.0f : 0.0f;
    k.misc[2] = std::exp2(h.exposure);
    k.misc[3] = h.white;
    k.seed[0] = seed;
    k.seed[1] = static_cast<std::uint32_t>(h.tonemap);
    return k;
}

// Keep in step with the CPU reference below (tools/colour-preview compares the two on the GPU).
char const* ShaderPrelude() {
    return R"(
cbuffer MevrColour : register(b1) {
    float4 mevrTone;
    float4 mevrGain;
    float4 mevrMisc;
    uint4 mevrSeed;
};
float3 MevrEncode(float3 l) {
    l = saturate(l);
    return (l <= 0.0031308) ? l * 12.92 : 1.055 * pow(l, 1.0 / 2.4) - 0.055;
}
float3 MevrDecode(float3 e) {
    e = saturate(e);
    return (e <= 0.04045) ? e / 12.92 : pow((e + 0.055) / 1.055, 2.4);
}
float3 MevrGrade(float3 x) {
    x = saturate((x + mevrTone.x) / (1.0 + mevrTone.x));
    x = saturate(x * mevrGain.rgb);
    x = pow(x, mevrTone.y);
    x = x + mevrTone.z * (x * x * (3.0 - 2.0 * x) - x);
    float y = dot(x, float3(0.2126, 0.7152, 0.0722));
    return saturate(y + (x - y) * mevrTone.w);
}
uint MevrHash(uint2 p, uint seed) {
    uint h = p.x * 1973u + p.y * 9277u + seed * 26699u;
    h = (h ^ 61u) ^ (h >> 16);
    h = h * 9u;
    h = h ^ (h >> 4);
    h = h * 0x27d4eb2du;
    h = h ^ (h >> 15);
    return h;
}
float MevrDither(float2 pixel) {
    uint h = MevrHash(uint2(pixel), mevrSeed.x);
    float u1 = (float(h & 0xFFFFu) + 0.5) / 65536.0;
    float u2 = (float(h >> 16) + 0.5) / 65536.0;
    return u1 + u2 - 1.0;
}
float4 MevrColourIn(float4 c, float2 pixel, bool linearIn) {
    float3 x = saturate(c.rgb);
    [branch] if (linearIn) { x = MevrEncode(c.rgb); }
    x = MevrGrade(x) + MevrDither(pixel) * mevrMisc.x;
    [branch] if (linearIn) { x = MevrDecode(x); } else { x = saturate(x); }
    return float4(x, c.a);
}
float3 MevrAces(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}
float3 MevrHableCurve(float3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
// ToEye's clip of a float (scRGB) source, or the tonemap: linear in (paper white at 1.0), linear [0, 1] out.
float3 MevrTone(float3 c) {
    c = max(c, 0.0);
    [branch] if (mevrMisc.y <= 0.5 || mevrSeed.y == 0u) { return c / max(1.0, max(c.r, max(c.g, c.b))); }
    c *= mevrMisc.z;
    [branch] if (mevrSeed.y == 1u) { return MevrAces(c); }
    [branch] if (mevrSeed.y == 2u) { return saturate(MevrHableCurve(c * 2.0) / MevrHableCurve(mevrMisc.www * 2.0)); }
    float w = mevrMisc.w;
    return saturate(c * (1.0 + c / (w * w)) / (1.0 + c));
}
#define MEVR_EYE_OUT(c, pos) MevrColourIn((c), (pos).xy, mevrMisc.y > 0.5)
#define MEVR_PANEL_OUT(c, pos) MevrColourIn(float4((c), 1.0), (pos).xy, true).rgb
#define MEVR_TONE(c) MevrTone(c)
)";
}

namespace {
float AcesFit(float x) {
    return std::clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f);
}
float HableCurve(float x) {
    constexpr float A = 0.15f, B = 0.50f, C = 0.10f, D = 0.20f, E = 0.02f, F = 0.30f;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
} // namespace

Rgb Tone(Constants const& k, Rgb c) {
    for (float& v : c) {
        v = std::max(v, 0.0f);
    }
    if (k.misc[1] <= 0.5f || k.seed[1] == 0u) {
        float const peak = std::max(1.0f, std::max(c[0], std::max(c[1], c[2])));
        for (float& v : c) {
            v /= peak;
        }
        return c;
    }
    float const w = k.misc[3];
    for (float& v : c) {
        float const x = v * k.misc[2];
        switch (k.seed[1]) {
        case 1u: v = AcesFit(x); break;
        case 2u: v = std::clamp(HableCurve(x * 2.0f) / HableCurve(w * 2.0f), 0.0f, 1.0f); break;  // the filmic bias of 2
        default: v = std::clamp(x * (1.0f + x / (w * w)) / (1.0f + x), 0.0f, 1.0f); break;
        }
    }
    return c;
}

float Encode(float linear) {
    float const l = std::clamp(linear, 0.0f, 1.0f);
    return l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
}

float Decode(float encoded) {
    float const e = std::clamp(encoded, 0.0f, 1.0f);
    return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f);
}

Rgb Grade(Constants const& k, Rgb x) {
    for (int c = 0; c < 3; ++c) {
        float v = std::clamp((x[c] + k.tone[0]) / (1.0f + k.tone[0]), 0.0f, 1.0f);
        v = std::clamp(v * k.gain[c], 0.0f, 1.0f);
        v = std::pow(v, k.tone[1]);
        x[c] = v + k.tone[2] * (v * v * (3.0f - 2.0f * v) - v);
    }
    float const y = 0.2126f * x[0] + 0.7152f * x[1] + 0.0722f * x[2];
    for (float& v : x) {
        v = std::clamp(y + (v - y) * k.tone[3], 0.0f, 1.0f);
    }
    return x;
}

namespace {
std::uint32_t Hash(std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
    std::uint32_t h = x * 1973u + y * 9277u + seed * 26699u;
    h = (h ^ 61u) ^ (h >> 16);
    h = h * 9u;
    h = h ^ (h >> 4);
    h = h * 0x27d4eb2du;
    h = h ^ (h >> 15);
    return h;
}
} // namespace

float Dither(std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
    std::uint32_t const h = Hash(x, y, seed);
    float const u1 = (static_cast<float>(h & 0xFFFFu) + 0.5f) / 65536.0f;
    float const u2 = (static_cast<float>(h >> 16) + 0.5f) / 65536.0f;
    return u1 + u2 - 1.0f;
}

Rgb Correct(Constants const& k, Rgb c, std::uint32_t x, std::uint32_t y, bool linear) {
    Rgb v;
    for (int i = 0; i < 3; ++i) {
        v[i] = linear ? Encode(c[i]) : std::clamp(c[i], 0.0f, 1.0f);
    }
    v = Grade(k, v);
    float const d = Dither(x, y, k.seed[0]) * k.misc[0];
    for (float& e : v) {
        e += d;
        e = linear ? Decode(e) : std::clamp(e, 0.0f, 1.0f);
    }
    return v;
}

std::uint8_t ToUnorm8(float v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

} // namespace mevr::vr::colour

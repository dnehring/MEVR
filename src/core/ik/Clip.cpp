// SPDX-License-Identifier: MIT
#include "core/ik/Clip.hpp"

#include "core/ik/TwoHand.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_map>

namespace mevr::ik::clip {
namespace {

bool Fail(std::string* error, std::string text) {
    if (error != nullptr) {
        *error = std::move(text);
    }
    return false;
}

bool ReadVec(json::Value const& v, Vec3& out) {
    auto const& items = v.Items();
    if (!v.IsArray() || items.size() != 3) {
        return false;
    }
    out = {items[0].AsNumber(), items[1].AsNumber(), items[2].AsNumber()};
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

bool ReadQuat(json::Value const& v, Quat& out) {
    auto const& items = v.Items();
    if (!v.IsArray() || items.size() != 4) {
        return false;
    }
    out = Normalize(Quat{items[0].AsNumber(), items[1].AsNumber(), items[2].AsNumber(), items[3].AsNumber()});
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z) && std::isfinite(out.w);
}

} // namespace

double Clip::TravelPerLoop() const {
    if (loopEnd <= loopStart || loopEnd >= static_cast<int>(clip.size())) {
        return 0.0;
    }
    Vec3 const d = clip[static_cast<std::size_t>(loopEnd)].root - clip[static_cast<std::size_t>(loopStart)].root;
    return std::hypot(d.x, d.y);
}

std::optional<Clip> Load(json::Value const& file, std::string* error) {
    if (file.String("format") != "mevr-clip" || file.Number("version") != 1.0) {
        Fail(error, "not a mevr-clip version 1");
        return std::nullopt;
    }
    Clip c;
    c.name = file.String("name");
    c.fps = file.Number("fps", 0.0);
    json::Value const& skeleton = file.Get("skeleton").Get("bones");
    json::Value const& map = file.Get("retarget").Get("map");
    json::Value const& frames = file.Get("clip");
    if (!(c.fps > 0.0) || !skeleton.IsArray() || !map.IsArray() || !frames.IsArray() || frames.Items().empty()) {
        Fail(error, "missing fps, skeleton bones, retarget map or frames");
        return std::nullopt;
    }
    std::unordered_map<std::string, int> index;
    for (std::size_t i = 0; i < skeleton.Items().size(); ++i) {
        index.emplace(skeleton.Items()[i].AsString(), static_cast<int>(i));
    }
    std::vector<int> mapped;
    for (json::Value const& row : map.Items()) {
        std::string const& bone = row.Items().empty() ? std::string() : row.Items()[0].AsString();
        auto const it = index.find(bone);
        if (it == index.end()) {
            Fail(error, std::format("the map's bone '{}' isn't in the clip's skeleton", bone));
            return std::nullopt;
        }
        c.bones.push_back(bone);
        mapped.push_back(it->second);
    }
    std::size_t const boneCount = skeleton.Items().size();
    for (json::Value const& f : frames.Items()) {
        Clip::Frame frame;
        json::Value const& locals = f.Get("local");
        if (!ReadVec(f.Get("root"), frame.root) || !locals.IsArray() || locals.Items().size() != boneCount) {
            Fail(error, std::format("frame {} is malformed", c.clip.size()));
            return std::nullopt;
        }
        for (int b : mapped) {
            Quat q;
            if (!ReadQuat(locals.Items()[static_cast<std::size_t>(b)], q)) {
                Fail(error, std::format("frame {} bone {} is malformed", c.clip.size(), b));
                return std::nullopt;
            }
            frame.locals.push_back(q);
        }
        c.clip.push_back(std::move(frame));
    }
    c.frames = static_cast<int>(c.clip.size());
    c.loopStart = 0;
    c.loopEnd = c.frames - 1;
    if (json::Value const& loop = file.Get("loop"); loop.IsObject()) {
        int const start = static_cast<int>(loop.Number("start", -1));
        int const end = static_cast<int>(loop.Number("end", -1));
        if (start >= 0 && end > start && end < c.frames) {
            c.loopStart = start;
            c.loopEnd = end;
        }
    }
    if (c.loopEnd <= c.loopStart) {
        Fail(error, "the clip is too short to play");
        return std::nullopt;
    }
    return c;
}

Sample At(Clip const& c, double seconds) {
    Sample s;
    int const span = c.loopEnd - c.loopStart;
    double phase = std::fmod(seconds * c.fps, static_cast<double>(span));
    if (phase < 0.0 || !std::isfinite(phase)) {
        phase = 0.0;
    }
    int const i = static_cast<int>(std::floor(phase));
    double const f = phase - i;
    Clip::Frame const& a = c.clip[static_cast<std::size_t>(c.loopStart + i)];
    Clip::Frame const& b = c.clip[static_cast<std::size_t>((std::min)(c.loopStart + i + 1, c.loopEnd))];
    Clip::Frame const& first = c.clip[static_cast<std::size_t>(c.loopStart)];
    Clip::Frame const& last = c.clip[static_cast<std::size_t>(c.loopEnd)];
    // The loop closed: a loop's last frame is never exactly its first (the loop finder takes the nearest pair), so the wrap
    // jumped by their difference, and the hair simulation felt every wrap as a kick (the owner, 2026-11-19: "it would almost
    // like re-trigger the animation for the hair ... needs to better end and start loop"). That difference is spread over
    // the whole loop instead: each bone turned by the share `phase / span` of the turn from the last frame to the first, the
    // height likewise, so the loop's end meets its start exactly.
    double const t = phase / span;
    s.locals.resize(a.locals.size());
    for (std::size_t k = 0; k < a.locals.size(); ++k) {
        Quat delta = Normalize(first.locals[k] * Conjugate(last.locals[k]));
        if (delta.w < 0.0) delta = {-delta.x, -delta.y, -delta.z, -delta.w};
        Quat const close = Slerp(Quat{0.0, 0.0, 0.0, 1.0}, delta, t);
        s.locals[k] = Normalize(close * Slerp(a.locals[k], b.locals[k], f));
    }
    // The root: the loop's straight horizontal travel taken out (the body walks on the spot; whoever plays the clip moves
    // the character), its bob and sway kept. Without it the end of each loop would jump back.
    Vec3 const drift{last.root.x - first.root.x, last.root.y - first.root.y, 0.0};
    Vec3 const root = a.root + (b.root - a.root) * f;
    Vec3 const line = first.root + drift * t;
    s.root = {root.x - line.x, root.y - line.y, root.z + (first.root.z - last.root.z) * t};
    return s;
}

} // namespace mevr::ik::clip

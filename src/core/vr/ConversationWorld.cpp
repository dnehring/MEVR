// SPDX-License-Identifier: MIT
#include "core/vr/ConversationWorld.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

namespace mevr::vr::conversation_world {
namespace {

std::mutex gMutex;
State gLatest{};
bool gHave = false;
std::uint64_t gSequence = 0;

constexpr float kStageWidth = 1280.0f;
constexpr float kStageHeight = 720.0f;

// A band of the stage (stage pixels) as fractions of the frame. The conversation movie shows its whole 1280x720 stage across the
// frame's width, centred top to bottom (measured 2026-11-06 on a 2344x2392 HUD frame: the wheel's centre at frame y 1715, stage y
// 643 at 1.83 frame pixels per stage pixel, 537 px of frame above and below the stage).
interact::Rect StageBand(std::uint32_t width, std::uint32_t height, float left, float right, float top, float bottom) {
    if (width == 0 || height == 0) {
        return {};
    }
    float const w = static_cast<float>(width);
    float const h = static_cast<float>(height);
    float const scale = w / kStageWidth;                     // frame pixels per stage pixel
    float const stageTop = (h - kStageHeight * scale) * 0.5f;  // centred (negative for a frame wider than 16:9)
    auto const y = [&](float stageY) { return stageTop + stageY * scale; };
    float const y0 = std::clamp(y(top), 0.0f, h);
    float const y1 = std::clamp(y(bottom), 0.0f, h);
    interact::Rect rect;
    rect.x = std::clamp(left / kStageWidth, 0.0f, 1.0f);
    rect.width = std::clamp((right - left) / kStageWidth, 0.0f, 1.0f - rect.x);
    rect.y = y0 / h;
    rect.height = std::max(0.0f, (y1 - y0) / h);
    return rect;
}

} // namespace

void Publish(State const& state) {
    std::lock_guard lock(gMutex);
    gLatest = state;
    gLatest.size = sizeof(State);
    gLatest.sequence = ++gSequence;
    gLatest.timeMs = GetTickCount64();
    gHave = true;
}

std::optional<State> Latest(std::uint64_t maxAgeMs) {
    std::lock_guard lock(gMutex);
    if (!gHave || gLatest.active == 0 || GetTickCount64() - gLatest.timeMs > maxAgeMs) {
        return std::nullopt;
    }
    return gLatest;
}

void Reset() {
    std::lock_guard lock(gMutex);
    gLatest = {};
    gHave = false;
}

Mode ParseMode(char const* text, Mode fallback) {
    if (text == nullptr) {
        return fallback;
    }
    std::string word;
    for (char const* p = text; *p != '\0'; ++p) {
        if (!std::isspace(static_cast<unsigned char>(*p))) {
            word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
        }
    }
    if (word == "world" || word == "2") {
        return Mode::World;
    }
    if (word == "screen" || word == "1") {
        return Mode::Screen;
    }
    if (word == "vr" || word == "0") {
        return Mode::Vr;
    }
    return fallback;
}

namespace {
double Wrap180(double deg) {
    while (deg > 180.0) deg -= 360.0;
    while (deg < -180.0) deg += 360.0;
    return deg;
}
double BearingDeg(Vec3 const& from, Vec3 const& to) {
    return std::atan2(to.y - from.y, to.x - from.x) * 180.0 / 3.14159265358979323846;
}
} // namespace

bool InView(Vec3 const& player, double facingYawDeg, Vec3 const& person, double halfAngleDeg, double minUnits, double maxUnits) {
    double const dx = person.x - player.x, dy = person.y - player.y;
    double const flat = std::hypot(dx, dy);
    if (flat < minUnits || flat > maxUnits) {
        return false;
    }
    return std::abs(Wrap180(BearingDeg(player, person) - facingYawDeg)) <= halfAngleDeg;
}

std::vector<ArcSeat> ArcSeats(Vec3 const& player, Vec3 const& owner, int count, double radiusUnits, double spacingDeg, double halfAngleDeg) {
    std::vector<ArcSeat> seats;
    if (count <= 0 || radiusUnits <= 0.0) {
        return seats;
    }
    double const centre = BearingDeg(player, owner);
    for (int i = 0; i < count; ++i) {
        // Right by one step, left by one, right by two... (the owner's own bearing is the centre).
        int const step = i / 2 + 1;
        double const side = (i % 2 == 0) ? -1.0 : 1.0;
        double const offset = std::min(step * spacingDeg, halfAngleDeg);
        double const bearing = (centre + side * offset) * 3.14159265358979323846 / 180.0;
        ArcSeat seat;
        seat.at = {player.x + std::cos(bearing) * radiusUnits, player.y + std::sin(bearing) * radiusUnits, player.z};
        seat.yawDeg = Wrap180(BearingDeg(seat.at, player));
        seats.push_back(seat);
    }
    return seats;
}

bool IsSetPiece(std::string_view name) {
    // tools/conversation-scan.py --list framed, over the game's packages (2026-11-06).
    // tools/conversation-scan.py --set-pieces (research/59 "Marking the rest", 2026-11-09): framed, or Shepard walked by the game.
    static constexpr std::string_view kSetPieces[] = {
        "end90_council_alive", "end90_council_dead", "end90_saren", "jug20_sal_captainkirrahe", "jug70_sovereign",
        "los10_trig_door00", "nor10_captain", "nor10_comm_room", "nor20_empty_locker", "nor20_ilos_approach",
        "nor20_ilos_open", "nor20_leaving_oculon", "nor20_start_kaidan", "nor20_start_liara", "prc1_cutscene_discovered",
        "prc1_human_console", "sta60_scenicview", "sta60_stripperseat", "sta60_wrex_at_fists", "sta90_council_saren",
        "war30_trig_01_entry",
    };
    std::string key;
    std::size_t const dot = name.rfind('.');
    std::size_t const space = name.rfind(' ');
    std::size_t start = 0;
    if (dot != std::string_view::npos) {
        start = dot + 1;
    } else if (space != std::string_view::npos) {
        start = space + 1;
    }
    for (char c : name.substr(start)) {
        key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    for (std::string_view suffix : {std::string_view("_dlg"), std::string_view("_d")}) {
        if (key.size() > suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
            key.resize(key.size() - suffix.size());
            break;
        }
    }
    return std::find(std::begin(kSetPieces), std::end(kSetPieces), key) != std::end(kSetPieces);
}

char const* Name(Mode mode) {
    switch (mode) {
    case Mode::Vr:
        return "vr";
    case Mode::Screen:
        return "screen";
    case Mode::World:
        return "world";
    }
    return "?";
}

interact::Rect DefaultSubtitleCrop(std::uint32_t width, std::uint32_t height) {
    // The line across the stage, its bottom row at stage y 564..575 (measured), up to three rows above it (a long line wraps
    // upward from SubtitleConversation's anchor at 605.6); the wheel's top row starts at 590.
    return StageBand(width, height, 80.0f, 1200.0f, 480.0f, 586.0f);
}

interact::Rect DefaultWheelCrop(std::uint32_t width, std::uint32_t height) {
    // The ring centred on (640, 643), its rows at stage y 590..680 (measured), the replies' text up to about 260 stage px to
    // each side of it.
    return StageBand(width, height, 330.0f, 950.0f, 586.0f, 712.0f);
}

bool SubtitlePoint(XrVector3f const& face, XrVector3f const& eyes, float belowM, float towardM, XrVector3f& out) {
    XrVector3f const to{eyes.x - face.x, 0.0f, eyes.z - face.z};  // level: the line stays upright under the face
    float const d = std::sqrt(to.x * to.x + to.z * to.z);
    if (!(d > 0.05f)) {
        return false;
    }
    float const toward = std::clamp(towardM, 0.0f, d * 0.5f);
    out = {face.x + to.x / d * toward, face.y - std::max(0.0f, belowM), face.z + to.z / d * toward};
    return true;
}

Follow FollowHead(Vec3 const& place, Vec3 const& anchor, Vec3 const& headNow, double threshold) {
    double const dx = headNow.x - anchor.x;
    double const dy = headNow.y - anchor.y;
    double const dz = headNow.z - anchor.z;
    if (!(std::sqrt(dx * dx + dy * dy + dz * dz) > threshold)) {
        return {place, anchor, false};
    }
    return {{place.x + dx, place.y + dy, place.z + dz}, headNow, true};
}

bool FollowHeadSettled(FollowState& state, Vec3 const& headNow, double threshold, int settle) {
    Follow const f = FollowHead(state.place, state.anchor, headNow, threshold);
    if (!f.jumped) {
        state.away = 0;
        return false;
    }
    if (++state.away < std::max(1, settle)) {
        return false;
    }
    state.place = f.place;
    state.anchor = f.anchor;
    state.away = 0;
    return true;
}

namespace {
double MarkDistance(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
} // namespace

void MarkWalk::Jump(Vec3 wanted, Vec3 standing, double nowS, double minUnits) {
    wanted_ = wanted;
    wantedAtS_ = nowS;
    phase_ = MarkDistance(wanted, standing) > std::max(0.0, minUnits) ? WalkPhase::Waiting : WalkPhase::Idle;
}

bool MarkWalk::StartIfSettled(Vec3 standing, double nowS, double settleS, double minUnits) {
    if (phase_ != WalkPhase::Waiting) {
        return false;
    }
    if (!(MarkDistance(wanted_, standing) > std::max(0.0, minUnits))) {
        End();
        return false;
    }
    if (nowS - wantedAtS_ < std::max(0.0, settleS)) {
        return false;
    }
    phase_ = WalkPhase::Walking;
    return true;
}

double BeyondWalk(Vec3 standing, Vec3 actual, Vec3 mark, double speedUnitsPerS, double elapsedS) {
    Vec3 const end = StepTowardMark(standing, mark, speedUnitsPerS, elapsedS).place;
    Vec3 const delta{end.x - standing.x, end.y - standing.y, end.z - standing.z};
    double const lengthSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    double const along = lengthSq > 1e-12 ? std::clamp(((actual.x - standing.x) * delta.x +
        (actual.y - standing.y) * delta.y + (actual.z - standing.z) * delta.z) / lengthSq, 0.0, 1.0) : 0.0;
    return MarkDistance(actual, {standing.x + along * delta.x, standing.y + along * delta.y, standing.z + along * delta.z});
}

WalkStep StepTowardMark(Vec3 standing, Vec3 mark, double speedUnitsPerS, double dtS) {
    double const distance = MarkDistance(standing, mark);
    if (distance <= 1e-6) {
        return {mark, {}, true};
    }
    if (!(dtS > 0.0) || !(speedUnitsPerS > 0.0)) {
        return {standing, {}, false};
    }
    double const fraction = std::min(1.0, speedUnitsPerS * dtS / distance);
    Vec3 const delta{(mark.x - standing.x) * fraction, (mark.y - standing.y) * fraction, (mark.z - standing.z) * fraction};
    return {{standing.x + delta.x, standing.y + delta.y, standing.z + delta.z},
            {delta.x / dtS, delta.y / dtS, delta.z / dtS}, fraction == 1.0};
}

} // namespace mevr::vr::conversation_world

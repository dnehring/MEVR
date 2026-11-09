// SPDX-License-Identifier: MIT
#include "core/vr/ConversationWorld.hpp"

#include <Windows.h>

#include <algorithm>
#include <span>
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
    std::string const key = ConversationKey(name);
    return std::find(std::begin(kSetPieces), std::end(kSetPieces), key) != std::end(kSetPieces);
}

std::string ConversationKey(std::string_view name) {
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
    return key;
}

namespace {
struct FlowRow {
    std::string_view name;
    int offset;  // into kFlowEntries
    int count;
};
// tools/conversation-scan.py --flow-list over the game's packages (2026-11-09; research/59 "Flowing conversations"): per
// conversation a level's script starts, the entries of its chain starts (plays that can never reach a wheel) that no
// conversation start reaches and that the game left ordinary. Set pieces, transmissions and the narration under the game's
// own screens (the character creation, the tutorial popups, the news terminals) are left out there. Generated, never typed.
constexpr int kFlowEntries[] = {
    67, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,  // cha00_asari_liara
    442, 443,  // cha00_humanmale_kaidan
    151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183,  // cha00_krogan_wrex
    154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172,  // cha00_quarian_tali
    169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198,  // cha00_turian_garrus
    1, 2, 4, 5,  // combat_banter_00
    152, 153, 154, 155, 156, 157, 158,  // ice20_anoleis
    2, 3, 131,  // ice20_giannaparasini
    1, 2, 39, 40, 41, 42,  // ice20_inamorda
    17, 18, 19,  // ice20_kairastirling
    56,  // ice20_mallenecalis
    13,  // ice20_trig11_si_guards
    1,  // ice50_amb_announcements
    10, 15,  // ice50_trig13_salarian_suicide
    0, 1,  // ice50_trig22_juleth_ambush
    2, 3,  // ice60_ambient_barricade_2
    2, 3,  // ice60_ambient_guard_1
    2, 3,  // ice60_ambient_guard_2
    5, 6,  // ice60_ambient_scientist_1
    5, 6,  // ice60_ambient_scientist_2
    6, 9, 10,  // ice60_ambient_scientist_3
    5, 6,  // ice60_ambient_scientist_4
    60,  // ice60_julethventralis
    0, 1,  // ice60_trig12_olar_execution
    50,  // ice70_tartakovsky
    8,  // jug20_amb_ashley
    7,  // jug20_amb_garrus
    1,  // jug20_amb_kaidan
    1,  // jug20_amb_liara
    8,  // jug20_amb_tali
    3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 15, 16, 17, 19, 20, 21, 22, 23, 25, 27, 33, 45,  // jug20_trigger
    0,  // jug20_wrex
    26,  // jug70_sal_prisoner
    21, 22, 23, 24,  // jug70_slv_fanatic01
    31, 32, 35, 38, 39, 42, 52, 55, 59,  // jug70_trigger
    0, 1, 2, 3, 4, 5, 6, 7, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,  // jug80_saren
    10, 15, 19, 20,  // jug80_trigger
    21, 22, 64,  // lav70_asariscientist
    11,  // lav70_trig03_krogan_boss
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,  // los30_security_system_done
    5, 6, 7, 8, 9,  // los30_trig_a
    6,  // los40_trig_c
    1, 2, 3,  // nor10_ambient_01
    0, 1, 2,  // nor10_engineer
    0, 25, 26, 27, 28, 29, 30, 31,  // nor10_joker
    9, 10, 11, 12,  // nor10_landing_bay
    41, 42, 43, 44,  // nor10_oculon_approach
    99, 100, 101, 102,  // nor20_caleston_done
    43,  // nor20_feros_done
    59, 60,  // nor20_virmire_done
    21,  // prc1_batarian_lieutenant
    24,  // prc1_human_contact
    1, 3, 4, 5, 6, 7, 8,  // prc1_human_radio
    72, 73,  // prc1_surveyor_wrapup
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,  // prc1_trig_batarians
    1, 2, 3,  // prc1_trig_main_facility_radio
    0,  // pro10_m_scientist02
    3,  // pro10_trigger_04_captain_dead
    0, 1,  // pro20_powell
    31,  // sp104_comatose
    42, 43,  // sp105_mariedurand
    0, 1,  // sp108_diplomat
    21,  // sp110_researcher
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,  // sp114_triggers
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,  // sp116_triggers
    16,  // sp119_major_kyle
    0, 1, 2, 3, 4, 5, 6, 7, 8,  // sp123_kahoku
    39,  // sp126_darius
    0,  // sta00_cab
    1, 4,  // sta20_asari_employee03
    31,  // sta20_chorban
    0,  // sta20_chorban_triggers
    35, 36, 37, 89, 90, 91, 92, 93, 94,  // sta20_human_ambassador_enter
    63,  // sta20_terra_firma
    3, 7,  // sta30_banter_01
    0, 1,  // sta30_chellik
    46,  // sta30_khalisah
    23, 24, 25, 26, 27, 28, 29,  // sta30_krogan_cutscene
    38,  // sta60_garrus_doctor
    15, 16, 17, 18,  // sta60_jax
    1, 4,  // sta60_schells
    61, 62, 63,  // sta60_septimus
    8,  // sta60_trig_02_jennas_contact
    0, 1,  // sta70_garrus
    44, 45, 46,  // sta90_council_spectre_induction
    1,  // unc_banter_00
    1, 9,  // war20_ambientbunker01
    1, 2, 5, 10, 11, 19, 24,  // war20_ambientmain02
    1, 2, 4, 5, 6,  // war20_arceliasilvamartinez
    1, 2, 3, 4, 6, 7, 9, 23, 24, 25, 26, 27, 29,  // war20_calanthablake
    2, 3, 4, 5, 7, 8, 9, 10, 52, 53, 54, 55,  // war20_davinandersson
    0, 1, 2, 3, 4, 64, 65, 66, 67, 68, 69,  // war20_faizhou
    37,  // war20_iannewstead
    28, 29, 30, 31,  // war20_inoste_merchant
    6, 23, 24, 25,  // war20_machadoyle
    10,  // war40_ambrefugee_female01
    10,  // war40_ambrefugee_male01
    10,  // war40_ambrefugee_male03
    65, 66, 67, 69, 70, 72, 73, 75, 76,  // war40_juliannabaynham
    12,  // war50_claw01
    4, 5, 6, 7, 8, 9,  // war50_krogan
    15, 17, 18, 20, 22, 72, 73, 74, 75, 76, 77, 78, 79, 80,  // war50_lizbeth
};
constexpr FlowRow kFlowRows[] = {
    {"cha00_asari_liara", 0, 24},
    {"cha00_humanmale_kaidan", 24, 2},
    {"cha00_krogan_wrex", 26, 33},
    {"cha00_quarian_tali", 59, 19},
    {"cha00_turian_garrus", 78, 30},
    {"combat_banter_00", 108, 4},
    {"ice20_anoleis", 112, 7},
    {"ice20_giannaparasini", 119, 3},
    {"ice20_inamorda", 122, 6},
    {"ice20_kairastirling", 128, 3},
    {"ice20_mallenecalis", 131, 1},
    {"ice20_trig11_si_guards", 132, 1},
    {"ice50_amb_announcements", 133, 1},
    {"ice50_trig13_salarian_suicide", 134, 2},
    {"ice50_trig22_juleth_ambush", 136, 2},
    {"ice60_ambient_barricade_2", 138, 2},
    {"ice60_ambient_guard_1", 140, 2},
    {"ice60_ambient_guard_2", 142, 2},
    {"ice60_ambient_scientist_1", 144, 2},
    {"ice60_ambient_scientist_2", 146, 2},
    {"ice60_ambient_scientist_3", 148, 3},
    {"ice60_ambient_scientist_4", 151, 2},
    {"ice60_julethventralis", 153, 1},
    {"ice60_trig12_olar_execution", 154, 2},
    {"ice70_tartakovsky", 156, 1},
    {"jug20_amb_ashley", 157, 1},
    {"jug20_amb_garrus", 158, 1},
    {"jug20_amb_kaidan", 159, 1},
    {"jug20_amb_liara", 160, 1},
    {"jug20_amb_tali", 161, 1},
    {"jug20_trigger", 162, 22},
    {"jug20_wrex", 184, 1},
    {"jug70_sal_prisoner", 185, 1},
    {"jug70_slv_fanatic01", 186, 4},
    {"jug70_trigger", 190, 9},
    {"jug80_saren", 199, 22},
    {"jug80_trigger", 221, 4},
    {"lav70_asariscientist", 225, 3},
    {"lav70_trig03_krogan_boss", 228, 1},
    {"los30_security_system_done", 229, 16},
    {"los30_trig_a", 245, 5},
    {"los40_trig_c", 250, 1},
    {"nor10_ambient_01", 251, 3},
    {"nor10_engineer", 254, 3},
    {"nor10_joker", 257, 8},
    {"nor10_landing_bay", 265, 4},
    {"nor10_oculon_approach", 269, 4},
    {"nor20_caleston_done", 273, 4},
    {"nor20_feros_done", 277, 1},
    {"nor20_virmire_done", 278, 2},
    {"prc1_batarian_lieutenant", 280, 1},
    {"prc1_human_contact", 281, 1},
    {"prc1_human_radio", 282, 7},
    {"prc1_surveyor_wrapup", 289, 2},
    {"prc1_trig_batarians", 291, 22},
    {"prc1_trig_main_facility_radio", 313, 3},
    {"pro10_m_scientist02", 316, 1},
    {"pro10_trigger_04_captain_dead", 317, 1},
    {"pro20_powell", 318, 2},
    {"sp104_comatose", 320, 1},
    {"sp105_mariedurand", 321, 2},
    {"sp108_diplomat", 323, 2},
    {"sp110_researcher", 325, 1},
    {"sp114_triggers", 326, 40},
    {"sp116_triggers", 366, 13},
    {"sp119_major_kyle", 379, 1},
    {"sp123_kahoku", 380, 9},
    {"sp126_darius", 389, 1},
    {"sta00_cab", 390, 1},
    {"sta20_asari_employee03", 391, 2},
    {"sta20_chorban", 393, 1},
    {"sta20_chorban_triggers", 394, 1},
    {"sta20_human_ambassador_enter", 395, 9},
    {"sta20_terra_firma", 404, 1},
    {"sta30_banter_01", 405, 2},
    {"sta30_chellik", 407, 2},
    {"sta30_khalisah", 409, 1},
    {"sta30_krogan_cutscene", 410, 7},
    {"sta60_garrus_doctor", 417, 1},
    {"sta60_jax", 418, 4},
    {"sta60_schells", 422, 2},
    {"sta60_septimus", 424, 3},
    {"sta60_trig_02_jennas_contact", 427, 1},
    {"sta70_garrus", 428, 2},
    {"sta90_council_spectre_induction", 430, 3},
    {"unc_banter_00", 433, 1},
    {"war20_ambientbunker01", 434, 2},
    {"war20_ambientmain02", 436, 7},
    {"war20_arceliasilvamartinez", 443, 5},
    {"war20_calanthablake", 448, 13},
    {"war20_davinandersson", 461, 12},
    {"war20_faizhou", 473, 11},
    {"war20_iannewstead", 484, 1},
    {"war20_inoste_merchant", 485, 4},
    {"war20_machadoyle", 489, 4},
    {"war40_ambrefugee_female01", 493, 1},
    {"war40_ambrefugee_male01", 494, 1},
    {"war40_ambrefugee_male03", 495, 1},
    {"war40_juliannabaynham", 496, 9},
    {"war50_claw01", 505, 1},
    {"war50_krogan", 506, 6},
    {"war50_lizbeth", 512, 14},
};
} // namespace

std::span<int const> FlowEntries(std::string_view name) {
    std::string const key = ConversationKey(name);
    for (FlowRow const& row : kFlowRows) {
        if (row.name == key) {
            return {kFlowEntries + row.offset, static_cast<std::size_t>(row.count)};
        }
    }
    return {};
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

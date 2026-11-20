// SPDX-License-Identifier: MIT
#include "game/AnimDriver.hpp"

#include "core/Log.hpp"
#include "core/Json.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/Clip.hpp"
#include "engine/ClipPose.hpp"
#include "engine/HairPose.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace mevr::game::anim_driver {
namespace {

ue3::ClassRef gStuntClass{"SFXGame.SFXStuntActor", ue3::kEveryGame};
ue3::FieldRef gStuntBody{"SFXGame.SFXStuntActor", "BodyMesh", ue3::kEveryGame};
ue3::FieldRef gStuntHair{"SFXGame.SFXStuntActor", "HairMesh", ue3::kEveryGame};
ue3::ClassRef gGesturesClass{"SFXGame.SFXModule_Gestures", ue3::kEveryGame};
ue3::FieldRef gInMatinee{"SFXGame.SFXModule_Gestures", "m_bInMatinee", ue3::kEveryGame};
ue3::ClassRef gSequenceClass{"Engine.AnimSequence", ue3::kEveryGame};
ue3::ClassRef gAnimSetClass{"Engine.AnimSet", ue3::kEveryGame};
ue3::FieldRef gSetSequences{"Engine.AnimSet", "Sequences", ue3::kEveryGame};
ue3::FieldRef gSequenceName{"Engine.AnimSequence", "SequenceName", ue3::kEveryGame};
ue3::FieldRef gSequenceLength{"Engine.AnimSequence", "SequenceLength", ue3::kEveryGame};
ue3::ClassRef gNodeSequenceClass{"Engine.AnimNodeSequence", ue3::kEveryGame};
ue3::FieldRef gNodeName{"Engine.AnimNode", "NodeName", ue3::kEveryGame};
ue3::FieldRef gNodeSkel{"Engine.AnimObject", "SkelComponent", ue3::kEveryGame};
ue3::FieldRef gNodeTime{"Engine.AnimNodeSequence", "CurrentTime", ue3::kEveryGame};
ue3::FieldRef gMeshAnimSets{"Engine.SkeletalMeshComponent", "AnimSets", ue3::kEveryGame};
ue3::FieldRef gMeshAnimSetUpdated{"Engine.SkeletalMeshComponent", "bAnimSetUpdated", ue3::kEveryGame};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gPawnList{"Engine.WorldInfo", "PawnList", ue3::kEveryGame};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn", ue3::kEveryGame};
ue3::FieldRef gPawnHair{"SFXGame.BioPawn", "m_oHairMesh", ue3::kEveryGame};
ue3::FunctionRef gSetAnim{"Engine.AnimNodeSequence.SetAnim", ue3::kEveryGame};
ue3::FunctionRef gPlayAnim{"Engine.AnimNodeSequence.PlayAnim", ue3::kEveryGame};
ue3::FunctionRef gStopAnim{"Engine.AnimNodeSequence.StopAnim", ue3::kEveryGame};
ue3::FunctionRef gSetPosition{"Engine.AnimNodeSequence.SetPosition", ue3::kEveryGame};
ue3::FunctionRef gSetLocation{"Engine.Actor.SetLocation", ue3::kEveryGame};
ue3::FunctionRef gSetRotation{"Engine.Actor.SetRotation", ue3::kEveryGame};

constexpr double kPi = 3.14159265358979323846;

// An engine TArray's header.
struct RawArray {
    void* data;
    std::int32_t count;
    std::int32_t max;
};

struct Driven {
    ue3::Object* actor = nullptr;
    ue3::Object* body = nullptr;
    ue3::Object* node = nullptr;       // the body's MatineeAnim AnimNodeSequence
    ue3::Object* gestures = nullptr;   // its gestures module (m_bInMatinee set while driven), or null
    bool gesturesWas = false;
    RawArray oldSets{};                // the body's own AnimSets, put back at the end
    void* ourSets = nullptr;           // the array we gave it (engine memory)
    std::string sequence;
    double rate = 1.0;
    double walkMS = 0.0;
    double rangeM = 3.0;
    ue3::Vector origin{};
    double yaw = 0.0;                  // radians, the walk's direction
    double along = 0.0;                // metres from the origin
    int direction = 1;
    bool paused = false;
    int turns = 0;
    // The facing shown: eased toward the wanted one (the turn, the walk's turnaround) at kTurnRadPerS, never at once (an
    // instant 90 or 180 degrees reset the hair simulation: the owner, 2026-11-19).
    double shownFacing = 0.0;
    std::shared_ptr<ik::clip::Clip const> clip;  // a generated clip played by engine/ClipPose (no node, no animation sets)
};
std::optional<Driven> gDriven;
constexpr double kTurnRadPerS = 240.0 * 3.14159265358979323846 / 180.0;
// The character's own facing when it was first driven: a clip's `turn` is from it, not from where the last clip left it (so
// 0, 90 and 180 are facing the player, side-on and facing away whatever played before).
ue3::Object* gBaseActor = nullptr;
double gBaseYaw = 0.0;
double gSlowmo = 1.0;  // `mevr anim slowmo`: our clip, hair and walk all at this share of real time (footage)
ue3::Object const* gWorld = nullptr;

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string PathOf(ue3::Object const* object) {
    return object != nullptr ? ue3::runtime::FullName(object) : std::string("none");
}

ue3::Object* PlayerWorld() {
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    return pawn != nullptr ? gWorldInfo.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
}

// The first stunt actor in the world whose name, hair or body mesh holds the text.
ue3::Object* FindActor(std::string const& what, ue3::Object* world) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gStuntClass.Get();
    if (cls == nullptr) {
        return nullptr;
    }
    std::string const key = Lower(what);
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls) || gWorldInfo.Read<ue3::Object*>(o).value_or(nullptr) != world) {
            continue;
        }
        ue3::Object* const body = gStuntBody.Read<ue3::Object*>(o).value_or(nullptr);
        ue3::Object* const hair = gStuntHair.Read<ue3::Object*>(o).value_or(nullptr);
        std::string const text = Lower(PathOf(o) + " " + (body != nullptr ? PathOf(player::SkeletalMesh(body)) : "") + " " +
                                       (hair != nullptr ? PathOf(player::SkeletalMesh(hair)) : ""));
        if (text.find(key) != std::string::npos) {
            return o;
        }
    }
    return nullptr;
}

// A character to play a clip on: an ME3 conversation character (its BodyMesh), else a pawn (its Mesh: ME2's Miranda, a
// squadmate, anyone in the world's pawn list but the player), by its name or its body or hair mesh.
struct Character {
    ue3::Object* actor = nullptr;
    ue3::Object* body = nullptr;
};
Character FindCharacter(std::string const& what, ue3::Object* world) {
    if (ue3::Object* const stunt = FindActor(what, world)) {
        return {stunt, gStuntBody.Read<ue3::Object*>(stunt).value_or(nullptr)};
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const playerPawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    std::string const key = Lower(what);
    int guard = 0;
    for (ue3::Object* p = gPawnList.Read<ue3::Object*>(world).value_or(nullptr); p != nullptr && guard < 512;
         p = gNextPawn.Read<ue3::Object*>(p).value_or(nullptr), ++guard) {
        if (!ue3::runtime::IsAlive(p)) {
            break;  // a level torn down under us leaves freed pawns in the list (CLAUDE.md, LivePawns)
        }
        if (p == playerPawn) {
            continue;
        }
        ue3::Object* const body = player::Mesh(p);
        ue3::Object* const hair = player::IsBioPawn(p) ? gPawnHair.Read<ue3::Object*>(p).value_or(nullptr) : nullptr;
        std::string const text = Lower(PathOf(p) + " " + (body != nullptr ? PathOf(player::SkeletalMesh(body)) : "") + " " +
                                       (hair != nullptr && ue3::runtime::IsAlive(hair) ? PathOf(player::SkeletalMesh(hair)) : ""));
        if (body != nullptr && text.find(key) != std::string::npos) {
            return {p, body};
        }
    }
    return {};
}

ue3::Object* FindSequence(std::string const& what) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gSequenceClass.Get();
    if (cls == nullptr) {
        return nullptr;
    }
    std::string const key = Lower(what);
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && r.IsAlive(o) && r.IsA(o, cls) && PathOf(o).find("Default__") == std::string::npos &&
            Lower(PathOf(o)).find(key) != std::string::npos) {
            return o;
        }
    }
    return nullptr;
}

// The body's AnimNodeSequence named MatineeAnim (what a cutscene drives).
ue3::Object* FindMatineeNode(ue3::Object* body) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gNodeSequenceClass.Get();
    if (cls == nullptr) {
        return nullptr;
    }
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls) || gNodeSkel.Read<ue3::Object*>(o).value_or(nullptr) != body) {
            continue;
        }
        if (auto const name = gNodeName.Read<ue3::Name>(o); name && ue3::runtime::NameText(*name) == "MatineeAnim") {
            return o;
        }
    }
    return nullptr;
}

ue3::Object* FindGestures(ue3::Object* actor) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gGesturesClass.Get();
    if (cls == nullptr) {
        return nullptr;
    }
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && r.IsAlive(o) && r.IsA(o, cls) && r.OuterOf(o) == actor) {
            return o;
        }
    }
    return nullptr;
}

// The AnimSet that holds a sequence: ME3 cooks a sequence as <package>.<set>_<sequence>, its outer the package, so the set
// is found by its Sequences list (putting the package in a body's AnimSets crashed SetAnim, 2026-11-19).
ue3::Object* SetOf(ue3::Object* sequence) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gAnimSetClass.Get();
    if (cls == nullptr) {
        return nullptr;
    }
    if (ue3::Object* const outer = r.OuterOf(sequence); outer != nullptr && r.IsA(outer, cls)) {
        return outer;
    }
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls)) {
            continue;
        }
        auto const list = gSetSequences.ElementsOf(o, 4096);
        if (!list) {
            continue;
        }
        for (std::int32_t k = 0; k < list->count; ++k) {
            if (*static_cast<ue3::Object**>(list->At(k)) == sequence) {
                return o;
            }
        }
    }
    return nullptr;
}

RawArray* SetsOf(ue3::Object* body) {
    return static_cast<RawArray*>(gMeshAnimSets.Address(body));
}

bool CallNode(ue3::FunctionRef& ref, ue3::Object* node, void (*fill)(ue3::Params&, void const*), void const* arg) {
    ue3::Function* const f = ref.Get();
    if (f == nullptr || node == nullptr) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), f);
    if (fill != nullptr) {
        fill(p, arg);
    }
    return ue3::runtime::Call(node, f, p);
}

void PutBack(char const* why) {
    if (!gDriven) {
        return;
    }
    Driven& d = *gDriven;
    ue3::Reflection& r = ue3::runtime::Get();
    if (d.clip != nullptr) {
        engine::clip_pose::Stop();
    }
    if (d.node != nullptr && r.IsAlive(d.node)) {
        CallNode(gStopAnim, d.node, nullptr, nullptr);
    }
    if (d.body != nullptr && r.IsAlive(d.body)) {
        if (RawArray* const sets = SetsOf(d.body); sets != nullptr && sets->data == d.ourSets) {
            *sets = d.oldSets;
            write_journal::Wrote("AnimDriver.animSets", sets, sizeof(RawArray));
            gMeshAnimSetUpdated.WriteBool(d.body, true);
            ue3::memory::Free(d.ourSets);
        }
    }
    if (d.gestures != nullptr && r.IsAlive(d.gestures)) {
        gInMatinee.WriteBool(d.gestures, d.gesturesWas);
    }
    log::Info("anim driver: {} stopped ({}); its own animation sets put back", PathOf(d.actor), why);
    gDriven.reset();
}

std::string List(std::string const& what) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gSequenceClass.Get();
    if (cls == nullptr) {
        return "Engine.AnimSequence not found";
    }
    std::string const key = Lower(what);
    std::string out;
    int total = 0;
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls)) {
            continue;
        }
        std::string const path = PathOf(o);
        if (path.find("Default__") != std::string::npos || Lower(path).find(key) == std::string::npos) {
            continue;
        }
        if (++total <= 80) {
            out += std::format("\n  {} ({:.2f} s)", path, gSequenceLength.Read<float>(o).value_or(0.0f));
        }
    }
    return std::format("{} loaded animation(s) hold '{}'{}{}", total, what, out, total > 80 ? "\n  ..." : "");
}

std::string Play(std::istringstream& words) {
    std::string who, what;
    double rate = 1.0, walk = 0.0, range = 3.0;
    words >> who >> what;
    if (!(words >> rate)) {
        rate = 1.0;
    }
    if (!(words >> walk)) {
        walk = 0.0;
    }
    if (!(words >> range)) {
        range = 3.0;
    }
    if (who.empty() || what.empty()) {
        return "mevr anim play <who> <sequence text> [rate] [walk m/s] [range m]";
    }
    if (!ue3::memory::Ready()) {
        return "the engine's allocator isn't known yet";
    }
    ue3::Object* const world = PlayerWorld();
    if (world == nullptr) {
        return "no player in a world";
    }
    PutBack("a new play");
    ue3::Object* const actor = FindActor(who, world);
    if (actor == nullptr) {
        return "no stunt actor in this world holds '" + who + "' (only ME3's conversation characters for now)";
    }
    ue3::Object* const body = gStuntBody.Read<ue3::Object*>(actor).value_or(nullptr);
    ue3::Object* const node = body != nullptr ? FindMatineeNode(body) : nullptr;
    if (node == nullptr) {
        return PathOf(actor) + " has no MatineeAnim node on its body";
    }
    ue3::Object* const sequence = FindSequence(what);
    if (sequence == nullptr) {
        return "no loaded animation holds '" + what + "' (`mevr anim list <text>`)";
    }
    ue3::Object* const set = SetOf(sequence);
    auto const name = gSequenceName.Read<ue3::Name>(sequence);
    RawArray* const sets = SetsOf(body);
    if (set == nullptr || !name || sets == nullptr) {
        return "no AnimSet holds " + PathOf(sequence) + ", or its name or the body's AnimSets can't be read";
    }
    // The body's sets plus the animation's, in an array of ours (theirs put back by stop).
    std::int32_t const count = std::clamp(sets->count, 0, 256);
    void* const ours = ue3::memory::Malloc(static_cast<std::size_t>(count + 1) * sizeof(void*));
    if (ours == nullptr) {
        return "couldn't allocate the animation sets";
    }
    if (count > 0 && sets->data != nullptr) {
        std::memcpy(ours, sets->data, static_cast<std::size_t>(count) * sizeof(void*));
    }
    static_cast<ue3::Object**>(ours)[count] = set;
    Driven d;
    d.actor = actor;
    d.body = body;
    d.node = node;
    d.oldSets = *sets;
    d.ourSets = ours;
    *sets = RawArray{ours, count + 1, count + 1};
    write_journal::Wrote("AnimDriver.animSets", sets, sizeof(RawArray));
    gMeshAnimSetUpdated.WriteBool(body, true);
    d.gestures = FindGestures(actor);
    if (d.gestures != nullptr) {
        d.gesturesWas = gInMatinee.ReadBool(d.gestures).value_or(false);
        gInMatinee.WriteBool(d.gestures, true);  // the gestures stand down, as in a cutscene
    }
    ue3::Name const seqName = *name;
    CallNode(gSetAnim, node, [](ue3::Params& p, void const* a) { p.Set("Sequence", *static_cast<ue3::Name const*>(a)); }, &seqName);
    struct PlayArgs {
        double rate;
    } playArgs{rate};
    CallNode(gPlayAnim, node, [](ue3::Params& p, void const* a) {
        p.SetBool("bLoop", true);
        p.Set("InRate", static_cast<float>(static_cast<PlayArgs const*>(a)->rate));
        p.Set("StartTime", 0.0f);
    }, &playArgs);
    d.sequence = PathOf(sequence);
    d.rate = rate;
    d.walkMS = std::max(0.0, walk);
    d.rangeM = std::max(0.5, range);
    d.origin = player::Location(actor).value_or(ue3::Vector{});
    d.yaw = static_cast<double>(player::Rotation(actor).value_or(ue3::Rotator{}).yaw) * (2.0 * kPi / 65536.0);
    d.shownFacing = d.yaw;
    gDriven = d;
    gWorld = world;
    log::Info("anim driver: {} plays {} (rate {:.2f}, walk {:.2f} m/s over {:.1f} m)", PathOf(actor), d.sequence, rate, d.walkMS, d.rangeM);
    return std::format("{} plays {} from {} (rate {:.2f}, looping){}", PathOf(actor), d.sequence, PathOf(set), rate,
        d.walkMS > 0.0 ? std::format(", walking {:.2f} m/s back and forth over {:.1f} m", d.walkMS, d.rangeM) : std::string());
}

// The clip files: tools/motion/ardy-clip.py writes <motion>/<name>/<name>.mevrclip.json (MEVR_MOTION_DIR, else
// %LOCALAPPDATA%\MEVR\motion); a path to a .mevrclip.json works too.
std::filesystem::path ClipFile(std::string const& name) {
    std::filesystem::path const direct(name);
    if (direct.extension() == ".json" && std::filesystem::exists(direct)) {
        return direct;
    }
    char buffer[MAX_PATH] = {};
    std::filesystem::path root;
    if (GetEnvironmentVariableA("MEVR_MOTION_DIR", buffer, MAX_PATH) > 0) {
        root = buffer;
    } else if (GetEnvironmentVariableA("LOCALAPPDATA", buffer, MAX_PATH) > 0) {
        root = std::filesystem::path(buffer) / "MEVR" / "motion";
    }
    return root / name / (name + ".mevrclip.json");
}

void Place(Driven& d);

std::string PlayClip(std::istringstream& words) {
    std::string who, name, walkText;
    double rate = 1.0, range = 3.0, turnDeg = 0.0;
    words >> who >> name;
    if (!(words >> rate)) {
        rate = 1.0;
    }
    words >> walkText;
    if (!(words >> range)) {
        range = 3.0;
    }
    if (!(words >> turnDeg)) {
        turnDeg = 0.0;  // turned from the character's own facing before walking: 90 walks across the player's view
    }
    if (who.empty() || name.empty()) {
        return "mevr anim clip <who> <clip name or .mevrclip.json> [rate] [walk m/s | auto] [range m] [turn deg]";
    }
    ue3::Object* const world = PlayerWorld();
    if (world == nullptr) {
        return "no player in a world";
    }
    PutBack("a new play");
    std::filesystem::path const file = ClipFile(name);
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return "no clip file " + file.string() + " (tools/motion/ardy-clip.py retarget writes it)";
    }
    std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    std::optional<json::Value> const parsed = json::Parse(text, &error);
    std::optional<ik::clip::Clip> loaded = parsed ? ik::clip::Load(*parsed, &error) : std::nullopt;
    if (!loaded) {
        return file.string() + ": " + error;
    }
    Character const character = FindCharacter(who, world);
    ue3::Object* const actor = character.actor;
    ue3::Object* const body = character.body;
    if (body == nullptr) {
        return "no conversation character or pawn in this world holds '" + who + "' in its name or its body or hair mesh";
    }
    auto const clip = std::make_shared<ik::clip::Clip const>(std::move(*loaded));
    double const pace = clip->LoopSeconds() > 0.0 ? clip->TravelPerLoop() / engine::kUnitsPerMeter / clip->LoopSeconds() : 0.0;
    double walk = 0.0;
    if (walkText == "auto") {
        walk = pace * rate;
    } else if (!walkText.empty()) {
        try {
            walk = std::stod(walkText);
        } catch (...) {
            walk = 0.0;
        }
    }
    engine::clip_pose::Play(body, clip, rate);
    Driven d;
    d.actor = actor;
    d.body = body;
    d.clip = clip;
    d.sequence = "clip " + clip->name;
    d.rate = rate;
    d.walkMS = std::max(0.0, walk);
    d.rangeM = std::max(0.5, range);
    d.origin = player::Location(actor).value_or(ue3::Vector{});
    double const nowYaw = static_cast<double>(player::Rotation(actor).value_or(ue3::Rotator{}).yaw) * (2.0 * kPi / 65536.0);
    if (actor != gBaseActor) {
        gBaseActor = actor;
        gBaseYaw = nowYaw;
    }
    d.yaw = gBaseYaw + turnDeg * kPi / 180.0;
    d.shownFacing = nowYaw;  // turns from where it stands now, eased (Tick)
    gDriven = d;
    gWorld = world;
    log::Info("anim driver: {} plays the clip {} ({} frames, loop {}..{}, {:.2f} m/s in the clip), rate {:.2f}, walk {:.2f} m/s",
        PathOf(actor), file.string(), clip->frames, clip->loopStart, clip->loopEnd, pace, rate, d.walkMS);
    return std::format("{} plays the clip {} (loop {}..{}, {:.2f} s; the clip walks {:.2f} m/s){}", PathOf(actor), clip->name,
        clip->loopStart, clip->loopEnd, clip->LoopSeconds(), pace,
        d.walkMS > 0.0 ? std::format(", walking {:.2f} m/s back and forth over {:.1f} m", d.walkMS, d.rangeM) : std::string());
}

void Place(Driven& d) {
    ue3::Function* const setLocation = gSetLocation.Get();
    ue3::Function* const setRotation = gSetRotation.Get();
    if (setLocation == nullptr || setRotation == nullptr) {
        return;
    }
    double const facing = d.shownFacing;
    ue3::Vector to = d.origin;
    to.x += static_cast<float>(std::cos(d.yaw) * d.along * engine::kUnitsPerMeter);
    to.y += static_cast<float>(std::sin(d.yaw) * d.along * engine::kUnitsPerMeter);
    ue3::Params move(ue3::runtime::Get(), setLocation);
    move.Set("NewLocation", to);
    ue3::runtime::Call(d.actor, setLocation, move);
    ue3::Rotator rot{};
    rot.yaw = static_cast<std::int32_t>(std::lround(facing * 65536.0 / (2.0 * kPi))) & 0xFFFF;
    ue3::Params turn(ue3::runtime::Get(), setRotation);
    turn.Set("NewRotation", rot);
    ue3::runtime::Call(d.actor, setRotation, turn);
}

} // namespace

std::string Command(std::string const& args) {
    std::istringstream words(args);
    std::string verb;
    words >> verb;
    if (!ue3::runtime::Up()) {
        return "anim driver: the reflection layer isn't up";
    }
    if (verb == "list") {
        std::string what;
        words >> what;
        return List(what);
    }
    if (verb == "play") {
        return Play(words);
    }
    if (verb == "clip") {
        return PlayClip(words);
    }
    if (verb == "slowmo") {
        double scale = 1.0;
        words >> scale;
        gSlowmo = std::clamp(scale, 0.0, 4.0);
        engine::clip_pose::SetTimeScale(gSlowmo);
        engine::hair_pose::SetTimeScale(gSlowmo);
        return std::format("our clip, hair and walk at {:.2f} of real time (the game's own animation isn't slowed)", gSlowmo);
    }
    if (verb == "stop") {
        if (!gDriven) {
            return "nothing is driven";
        }
        PutBack("mevr anim stop");
        return "stopped; the character's own animation sets are back";
    }
    if (!gDriven) {
        return verb.empty() || verb == "status" ? "nothing is driven"
                                                : "mevr anim [list <text> | play <who> <sequence> [rate] [walk m/s] [range m] | clip <who> <clip> [rate] [walk m/s | auto] [range m] [turn deg] | at <s> | pause | resume | stop | status]";
    }
    Driven& d = *gDriven;
    if (d.clip != nullptr && verb != "status" && !verb.empty()) {
        if (verb == "pause" || verb == "resume") {
            d.paused = verb == "pause";
            engine::clip_pose::Seek(engine::clip_pose::GetStats().seconds, d.paused ? 0.0 : d.rate);
            return d.paused ? "paused (the walk holds too)" : "resumed";
        }
        if (verb == "at") {
            double seconds = 0.0;
            words >> seconds;
            d.paused = true;
            engine::clip_pose::Seek(seconds, 0.0);
            return std::format("held at {:.2f} s into the loop (frame {:.1f}); `mevr anim resume` plays on", seconds,
                d.clip->loopStart + seconds * d.clip->fps);
        }
        return "a clip plays: mevr anim at <s> | pause | resume | stop | status";
    }
    if (d.clip != nullptr) {
        engine::clip_pose::Stats const st = engine::clip_pose::GetStats();
        return std::format("{} plays {} at {:.2f} s{}; {} pose updates, {} written, {} of the clip's bones found{}{}; walk {:.2f} m/s, "
                           "{:.2f} m along, {} turn(s)", PathOf(d.actor), d.sequence, st.seconds, d.paused ? " (paused)" : "", st.calls,
            st.written, st.mappedFound, st.lastReject != nullptr ? std::string(", last refusal: ") + st.lastReject : std::string(),
            st.broken ? ", the stage STOPPED after an access violation" : "", d.walkMS, d.along, d.turns) +
               std::format("\n  root before ({:.1f}, {:.1f}, {:.1f}), after ({:.1f}, {:.1f}, {:.1f}); pelvis before ({:.1f}, {:.1f}, {:.1f})",
                   st.rootBefore.x, st.rootBefore.y, st.rootBefore.z, st.rootAfter.x, st.rootAfter.y, st.rootAfter.z,
                   st.pelvisBefore.x, st.pelvisBefore.y, st.pelvisBefore.z);
    }
    if (verb == "pause" || verb == "resume") {
        d.paused = verb == "pause";
        float const rate = d.paused ? 0.0f : static_cast<float>(d.rate);
        struct A {
            float rate, time;
        } a{rate, gNodeTime.Read<float>(d.node).value_or(0.0f)};
        CallNode(gPlayAnim, d.node, [](ue3::Params& p, void const* x) {
            p.SetBool("bLoop", true);
            p.Set("InRate", static_cast<A const*>(x)->rate);
            p.Set("StartTime", static_cast<A const*>(x)->time);
        }, &a);
        return d.paused ? "paused (the walk holds too)" : "resumed";
    }
    if (verb == "at") {
        float seconds = 0.0f;
        words >> seconds;
        CallNode(gSetPosition, d.node, [](ue3::Params& p, void const* x) {
            p.Set("NewTime", *static_cast<float const*>(x));
            p.SetBool("bFireNotifies", false);
        }, &seconds);
        return std::format("at {:.2f} s", seconds);
    }
    if (verb.empty() || verb == "status") {
        return std::format("{} plays {} at {:.2f} s{}, rate {:.2f}; walk {:.2f} m/s, {:.2f} m along, {} turn(s)", PathOf(d.actor), d.sequence,
            gNodeTime.Read<float>(d.node).value_or(-1.0f), d.paused ? " (paused)" : "", d.rate, d.walkMS, d.along, d.turns);
    }
    return "mevr anim [list <text> | play <who> <sequence> [rate] [walk m/s] [range m] | clip <who> <clip> [rate] [walk m/s | auto] [range m] [turn deg] | at <s> | pause | resume | stop | status]";
}

void Tick(double deltaSeconds) {
    if (!gDriven) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    Driven& d = *gDriven;
    if (PlayerWorld() != gWorld || !r.IsAlive(d.actor) || !r.IsAlive(d.body) || (d.clip == nullptr && !r.IsAlive(d.node))) {
        if (d.clip != nullptr) {
            engine::clip_pose::Stop();
        }
        gDriven.reset();  // a new level: its objects are gone (nothing of ours to put back in them)
        log::Info("anim driver: the driven character is gone (a level change)");
        return;
    }
    // The facing eased toward the wanted one.
    bool turning = false;
    if (deltaSeconds > 0.0) {
        double const want = d.yaw + (d.direction < 0 ? kPi : 0.0);
        double diff = std::remainder(want - d.shownFacing, 2.0 * kPi);
        double const step = kTurnRadPerS * std::min(deltaSeconds, 0.1) * gSlowmo;
        if (std::abs(diff) > 1e-4) {
            d.shownFacing += std::clamp(diff, -step, step);
            turning = true;
        }
    }
    if (d.paused || d.walkMS <= 0.0 || !(deltaSeconds > 0.0)) {
        if (turning && !d.paused) Place(d);
        return;
    }
    d.along += d.direction * d.walkMS * std::min(deltaSeconds, 0.1) * gSlowmo;
    if (d.along > d.rangeM || d.along < 0.0) {
        d.along = std::clamp(d.along, 0.0, d.rangeM);
        d.direction = -d.direction;
        ++d.turns;
    }
    Place(d);
}

std::string StatusJson() {
    if (!gDriven) {
        return "{\"driving\": false}";
    }
    Driven const& d = *gDriven;
    return std::format("{{\"driving\": true, \"actor\": \"{}\", \"sequence\": \"{}\", \"rate\": {:.3f}, \"walkMS\": {:.3f}, \"alongM\": {:.3f}, \"turns\": {}}}",
        PathOf(d.actor), d.sequence, d.rate, d.walkMS, d.along, d.turns);
}

void Shutdown() {
    gSlowmo = 1.0;
    engine::clip_pose::SetTimeScale(1.0);
    engine::hair_pose::SetTimeScale(1.0);
    PutBack("shutdown");
}

} // namespace mevr::game::anim_driver

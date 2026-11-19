// SPDX-License-Identifier: MIT
#include "game/AnimDriver.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
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
};
std::optional<Driven> gDriven;
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
    gDriven = d;
    gWorld = world;
    log::Info("anim driver: {} plays {} (rate {:.2f}, walk {:.2f} m/s over {:.1f} m)", PathOf(actor), d.sequence, rate, d.walkMS, d.rangeM);
    return std::format("{} plays {} from {} (rate {:.2f}, looping){}", PathOf(actor), d.sequence, PathOf(set), rate,
        d.walkMS > 0.0 ? std::format(", walking {:.2f} m/s back and forth over {:.1f} m", d.walkMS, d.rangeM) : std::string());
}

void Place(Driven& d) {
    ue3::Function* const setLocation = gSetLocation.Get();
    ue3::Function* const setRotation = gSetRotation.Get();
    if (setLocation == nullptr || setRotation == nullptr) {
        return;
    }
    double const facing = d.yaw + (d.direction < 0 ? kPi : 0.0);
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
    if (verb == "stop") {
        if (!gDriven) {
            return "nothing is driven";
        }
        PutBack("mevr anim stop");
        return "stopped; the character's own animation sets are back";
    }
    if (!gDriven) {
        return verb.empty() || verb == "status" ? "nothing is driven"
                                                : "mevr anim [list <text> | play <who> <sequence> [rate] [walk m/s] [range m] | at <s> | pause | resume | stop | status]";
    }
    Driven& d = *gDriven;
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
    return "mevr anim [list <text> | play <who> <sequence> [rate] [walk m/s] [range m] | at <s> | pause | resume | stop | status]";
}

void Tick(double deltaSeconds) {
    if (!gDriven) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    Driven& d = *gDriven;
    if (PlayerWorld() != gWorld || !r.IsAlive(d.actor) || !r.IsAlive(d.body) || !r.IsAlive(d.node)) {
        gDriven.reset();  // a new level: its objects are gone (nothing of ours to put back in them)
        log::Info("anim driver: the driven character is gone (a level change)");
        return;
    }
    if (d.paused || d.walkMS <= 0.0 || !(deltaSeconds > 0.0)) {
        return;
    }
    d.along += d.direction * d.walkMS * std::min(deltaSeconds, 0.1);
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
    PutBack("shutdown");
}

} // namespace mevr::game::anim_driver

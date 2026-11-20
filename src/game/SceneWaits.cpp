// SPDX-License-Identifier: MIT
// A scene that waits for the player at a level trigger (game/SceneWaits.hpp).
#include "game/SceneWaits.hpp"

#include "core/Log.hpp"
#include "core/vr/ConversationWorld.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"

#include <Windows.h>

#include <cmath>
#include <format>
#include <vector>

namespace mevr::game::scene_waits {
namespace {

namespace cw = vr::conversation_world;

ue3::ClassRef gTouchClass{"Engine.SeqEvent_Touch", ue3::kEveryGame};
ue3::FieldRef gOriginator{"Engine.SequenceEvent", "Originator", ue3::kEveryGame};
ue3::FieldRef gTriggerCount{"Engine.SequenceEvent", "TriggerCount", ue3::kEveryGame};
ue3::FieldRef gMaxTriggerCount{"Engine.SequenceEvent", "MaxTriggerCount", ue3::kEveryGame};
ue3::FieldRef gEnabled{"Engine.SequenceEvent", "bEnabled", ue3::kEveryGame};
ue3::FieldRef gCollision{"Engine.Actor", "CollisionComponent", ue3::kEveryGame};
ue3::FunctionRef gPointCheck{"Engine.Actor.PointCheckComponent", ue3::kEveryGame};
ue3::FieldRef gCylinder{"Engine.Pawn", "CylinderComponent", ue3::kEveryGame};
ue3::FieldRef gCylinderRadius{"Engine.CylinderComponent", "CollisionRadius", ue3::kEveryGame};
ue3::FieldRef gCylinderHeight{"Engine.CylinderComponent", "CollisionHeight", ue3::kEveryGame};

constexpr double kNearUnits = 3000.0;  // touch events whose trigger stands this near (flat) where the scene wants Shepard

struct Candidate {
    ue3::Object* event = nullptr;
    ue3::Object* trigger = nullptr;
    ue3::Object* component = nullptr;
};
std::vector<Candidate> gCandidates;
ue3::Vector gGatheredFor{};
std::uint64_t gGatheredMs = 0;
std::uint64_t gJudgedMs = 0;
std::optional<Wait> gLast;
unsigned gGathers = 0, gFound = 0;

double Flat(ue3::Vector const& a, ue3::Vector const& b) {
    return std::hypot(static_cast<double>(a.x) - b.x, static_cast<double>(a.y) - b.y);
}

// Whether a box of `extent` at `at` touches the trigger's collision (the engine's own point check: ME1's trigger stop's Overlaps).
bool Inside(Candidate const& c, ue3::Vector const& at, ue3::Vector const& extent) {
    ue3::Function* const f = gPointCheck.Get();
    if (f == nullptr) return false;
    ue3::Params p(ue3::runtime::Get(), f);
    if (!p.Valid() || !p.Set("InComponent", c.component) || !p.Set("PointLocation", at) || !p.Set("PointExtent", extent)) return false;
    ue3::runtime::Call(c.trigger, f, p);
    return p.ReturnBool().value_or(false);
}

void Gather(ue3::Vector const& wanted) {
    gCandidates.clear();
    gGatheredFor = wanted;
    gGatheredMs = GetTickCount64();
    ++gGathers;
    if (gTouchClass.Get() == nullptr || gPointCheck.Get() == nullptr) return;
    ue3::Reflection& r = ue3::runtime::Get();
    std::int32_t const n = r.ObjectCount();
    for (std::int32_t i = 0; i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !ue3::IsA(o, gTouchClass) || r.NameText(o).rfind("Default__", 0) == 0) continue;
        ue3::Object* const trigger = gOriginator.Read<ue3::Object*>(o).value_or(nullptr);
        if (trigger == nullptr || !r.IsAlive(trigger)) continue;
        ue3::Object* const component = gCollision.Read<ue3::Object*>(trigger).value_or(nullptr);
        auto const at = player::Location(trigger);
        if (component == nullptr || !r.IsAlive(component) || !at || Flat(*at, wanted) > kNearUnits) continue;
        gCandidates.push_back({o, trigger, component});
    }
}

} // namespace

std::optional<Wait> Find(ue3::Vector const& wanted, ue3::Object* shepard, ue3::Vector const& playerAt) {
    if (!ue3::runtime::Up()) return std::nullopt;
    std::uint64_t const now = GetTickCount64();
    if (gGatheredMs == 0 || now - gGatheredMs > 3000 || Flat(wanted, gGatheredFor) > 500.0) Gather(wanted);
    if (gJudgedMs != 0 && now - gJudgedMs < 250) return gLast;
    gJudgedMs = now;
    ue3::Object* const cylinder = shepard != nullptr && ue3::runtime::IsAlive(shepard) ? gCylinder.Read<ue3::Object*>(shepard).value_or(nullptr) : nullptr;
    float const radius = cylinder != nullptr ? gCylinderRadius.Read<float>(cylinder).value_or(34.0f) : 34.0f;
    float const half = cylinder != nullptr ? gCylinderHeight.Read<float>(cylinder).value_or(90.0f) : 90.0f;
    ue3::Vector const extent{radius, radius, half};
    auto const shepardAt = shepard != nullptr && ue3::runtime::IsAlive(shepard) ? player::Location(shepard) : std::nullopt;
    std::vector<cw::WaitTrigger> facts;
    std::vector<Candidate const*> which;
    for (Candidate const& c : gCandidates) {
        if (!ue3::runtime::IsAlive(c.event) || !ue3::runtime::IsAlive(c.trigger) || !ue3::runtime::IsAlive(c.component)) continue;
        cw::WaitTrigger t;
        t.enabled = gEnabled.ReadBool(c.event).value_or(false);
        t.triggerCount = gTriggerCount.Read<std::int32_t>(c.event).value_or(0);
        t.maxTriggerCount = gMaxTriggerCount.Read<std::int32_t>(c.event).value_or(0);
        if (!cw::Armed(t)) continue;  // (the checks below are engine calls)
        t.wantedInside = Inside(c, wanted, extent);
        if (!t.wantedInside) continue;
        t.shepardInside = shepardAt && Inside(c, *shepardAt, extent);
        t.playerInside = Inside(c, playerAt, extent);
        facts.push_back(t);
        which.push_back(&c);
    }
    int const index = cw::WaitingTrigger(facts);
    std::optional<Wait> const before = gLast;
    gLast.reset();
    if (index >= 0) {
        gLast = Wait{wanted, ue3::runtime::FullName(which[static_cast<std::size_t>(index)]->trigger)};
        if (!before || before->trigger != gLast->trigger) {
            ++gFound;
            log::Info("scene waits: the script waits on {} (a one-shot touch, {} of {} fired) where the scene wants Shepard; neither he nor "
                      "the player stands in it", gLast->trigger, facts[static_cast<std::size_t>(index)].triggerCount,
                facts[static_cast<std::size_t>(index)].maxTriggerCount);
        }
    }
    return gLast;
}

void Forget() {
    gCandidates.clear();
    gGatheredMs = 0;
    gJudgedMs = 0;
    gLast.reset();
}

std::string Status() {
    return std::format("{{\"gathers\": {}, \"candidates\": {}, \"found\": {}, \"waiting\": {}}}", gGathers, gCandidates.size(), gFound,
        gLast ? "\"" + gLast->trigger + "\"" : std::string("null"));
}

} // namespace mevr::game::scene_waits

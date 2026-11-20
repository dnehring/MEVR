// SPDX-License-Identifier: MIT
// The conversational circle and the squad's formation on the sequels (included by game/SequelConversationWorld.cpp inside its
// anonymous namespace, after the records' helpers). ME1's worldCircle and worldSquadGather (research/69 item 28, "The
// conversational circle" and "The squad's formation, held"; the owner's design of 2026-11-11 and their answers of 2026-11-12) on
// ME1's core rules: cw::MakeCircle / JoinCircle / PlayerSoon / UpcomingPeople / StaggerSeconds, cw::SquadFormation / AssignSpots.
//   - Nobody moves at a scene's start. The circle forms round the player toward the conversation's owner once the player speaks
//     or is spoken to now or within the lines read ahead (people talking among themselves move nobody); the owner stays where
//     it stands (worldCircleOwnerStays), anyone within worldCircleInRangeM (3.5 m) of the player joins where it stands, and
//     whoever speaks or is spoken to now or within worldCircleLookahead (4) lines on any branch walks into the nearest free place
//     of the circle with the game's own AI (game/AiWalk: ME2's SFXAI_Core.MoveToGoal, ME3's BioAiController.MoveToSlot), the
//     walkers setting off worldCircleStaggerS (0.6 s) apart, facing the circle's middle on arrival.
//   - Non-walkers never walk: not on the ground (Physics not PHYS_Walking), seated (the head under 0.62 m above the place), no
//     AI (ME3's hub characters are SFXStuntActors: the authored composition stays), hidden, or a matinee's track moves them.
//   - The squad (worldSquadGather): after the opening each squadmate the conversation's start moved off where it stood with the
//     player is put back (ME1's PutBackSquad), then each walks to its own side of a relaxed formation beside and a little behind
//     the player (1.7 m out, 0.5 m back), facing the owner; under a Cut opening's dip they are put there at once. ME1 then holds
//     them with the squad's HOLD formation written quietly into BioBaseSquad.Members; the sequels have no such field (ME2's
//     FMemberData has no eFormation, its hold is an AI order the henchman answers aloud): after its walk a squadmate's AI is the
//     conversation's again (its enable counters back as they were), so it stands where it arrived [verify per scene].
// Logs `conversation world: circle:` and `conversation world: squad:`; the records' `walk` (with its `kind`) and `circle`.
// Test switches (the broken runs): `mevr convo circle ownerwalks on` walks the owner into the circle too, `mevr convo squad
// behind on` puts the formation's places behind the player; tools/scene_audit.py's `owner-walked` and `squad-behind` catch them.

struct CircleSettingsNow {
    bool circle = true;            // [Cinematics] worldCircle
    cw::CircleSettings s;
    int lookahead = 4;             // worldCircleLookahead
    bool squad = true;             // worldSquadGather
    double sideUnits = 170.0;      // worldSquadGatherSideM
    double backUnits = 50.0;       // worldSquadGatherBackM
    std::uint64_t ms = 0;
};
CircleSettingsNow gCircleSettings;
std::optional<bool> gCircleOverride;  // `mevr convo circle on|off|auto`
std::optional<bool> gSquadOverride;   // `mevr convo squad on|off|auto`
bool gCircleOwnerWalksTest = false;   // the broken run: the owner walks too
bool gSquadBehindTest = false;        // the broken run: the formation behind the player

void ReadCircleSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gCircleSettings.ms != 0 && now - gCircleSettings.ms < 2000) return;
    gCircleSettings.ms = now;
    std::wstring const path = engine::SettingsFile().wstring();
    auto const number = [&](wchar_t const* key, double fallback, double low, double high) {
        wchar_t text[32] = {};
        vr::settings_file::String(L"Cinematics", key, L"", text, static_cast<DWORD>(std::size(text)), path.c_str());
        std::string narrow;
        for (wchar_t const* c = text; *c != 0; ++c) narrow.push_back(static_cast<char>(*c & 0x7F));
        return std::clamp(ini::Number(narrow).value_or(fallback), low, high);
    };
    CircleSettingsNow& c = gCircleSettings;
    c.circle = number(L"worldCircle", 1.0, 0.0, 1.0) != 0.0;
    c.s.gapUnits = number(L"worldCircleGapM", 1.1, 0.5, 3.0) * 100.0;
    c.s.ownerNearUnits = number(L"worldCircleNearM", 2.5, 0.0, 20.0) * 100.0;
    c.s.maxWalkUnits = number(L"worldCircleMaxWalkM", 12.0, 0.0, 50.0) * 100.0;
    c.s.staggerSeconds = number(L"worldCircleStaggerS", 0.6, 0.0, 10.0);
    c.s.inRangeUnits = number(L"worldCircleInRangeM", 3.5, 0.0, 20.0) * 100.0;
    c.s.ownerAlwaysStays = number(L"worldCircleOwnerStays", 1.0, 0.0, 1.0) != 0.0;
    c.s.regather = false;  // ME1's default (worldCircleRegather 0 since the owner's second design): not built on the sequels
    c.lookahead = static_cast<int>(number(L"worldCircleLookahead", 4.0, 0.0, 12.0));
    c.squad = number(L"worldSquadGather", 1.0, 0.0, 1.0) != 0.0;
    c.sideUnits = number(L"worldSquadGatherSideM", 1.7, 0.5, 5.0) * 100.0;
    c.backUnits = number(L"worldSquadGatherBackM", 0.5, -2.0, 5.0) * 100.0;
}

bool CircleWanted() {
    return gCircleOverride.value_or(gCircleSettings.circle);
}
bool SquadWanted() {
    return gSquadOverride.value_or(gCircleSettings.squad);
}

// ---- What the game gives (each game's names) ------------------------------------------------------------------------------------
// The dialogue's graph (cw::DialogGraph) from the conversation's data: ME1's names on both sequels (ME2's BioConversation is ME1's
// object; ME3's data object, BioConversationController.m_pConvData, keeps the same lists).
ue3::FieldRef gGraphEntries{"SFXGame.BioConversation", "m_EntryList", ue3::kEveryGame};
ue3::FieldRef gGraphEntrySpeaker{gGraphEntries, "nSpeakerIndex"};
ue3::FieldRef gGraphEntryListener{gGraphEntries, "nListenerIndex"};
ue3::FieldRef gGraphEntryReplies{gGraphEntries, "ReplyListNew"};
ue3::FieldRef gGraphEntryReplyIndex{gGraphEntryReplies, "nIndex"};
ue3::FieldRef gGraphReplies{"SFXGame.BioConversation", "m_ReplyList", ue3::kEveryGame};
ue3::FieldRef gGraphReplyEntries{gGraphReplies, "EntryList"};
#if defined(MEVR_GAME_LE3)
// A speaker index names a tag of the data's m_aSpeakerList; the controller's speaker data and found actors pair tags with actors.
ue3::FieldRef gDataSpeakerTags{"SFXGame.BioConversation", "m_aSpeakerList", ue3::kEveryGame};
ue3::FieldRef gSpeakerDataTag{gSpeakers, "nmSpeakerTag"};
ue3::FieldRef gSpeakerDataActor{gSpeakers, "pSpeakerActor"};
ue3::FieldRef gFoundTag{gFoundActors, "nmSpeakerTag"};
#endif
ue3::FieldRef gWalkPhysics{"Engine.Actor", "Physics", ue3::kEveryGame};
ue3::FunctionRef gBioSetDesiredRotation{"SFXGame.BioPawn.BioSetDesiredRotation", ue3::kEveryGame};

cw::DialogGraph ReadDialogGraph(ue3::Object* data) {
    cw::DialogGraph graph;
    if (auto const entries = gGraphEntries.ElementsOf(data, 4096)) {
        for (std::int32_t i = 0; i < entries->count; ++i) {
            void const* const node = entries->At(i);
            cw::DialogGraph::Entry e;
            e.speaker = gGraphEntrySpeaker.Read<std::int32_t>(node).value_or(-1);
            e.listener = gGraphEntryListener.Read<std::int32_t>(node).value_or(-1);
            if (auto const replies = gGraphEntryReplies.ElementsOf(node, 64)) {
                for (std::int32_t r = 0; r < replies->count; ++r) {
                    if (auto const index = gGraphEntryReplyIndex.Read<std::int32_t>(replies->At(r))) e.replies.push_back(*index);
                }
            }
            graph.entries.push_back(std::move(e));
        }
    }
    if (auto const replies = gGraphReplies.ElementsOf(data, 4096)) {
        for (std::int32_t i = 0; i < replies->count; ++i) {
            std::vector<int> next;
            if (auto const list = gGraphReplyEntries.ElementsOf(replies->At(i), 64);
                list && list->stride == static_cast<std::int32_t>(sizeof(std::int32_t))) {
                for (std::int32_t k = 0; k < list->count; ++k) next.push_back(*static_cast<std::int32_t const*>(list->At(k)));
            }
            graph.replyEntries.push_back(std::move(next));
        }
    }
    return graph;
}

// The actor of a line's speaker index (-2 the player, -1 the owner), or null.
ue3::Object* PersonOfIndex(ue3::Object* conversation, ue3::Object* pawn, int index) {
    if (index == -2) return pawn;
    if (index == -1) return gOwner.Read<ue3::Object*>(conversation).value_or(nullptr);
    if (index < 0) return nullptr;
#if defined(MEVR_GAME_LE3)
    auto const tags = gDataSpeakerTags.ElementsOf(ConversationData(conversation), 64);
    if (!tags || index >= tags->count || tags->stride != static_cast<std::int32_t>(sizeof(ue3::Name))) return nullptr;
    std::uint64_t const tag = static_cast<ue3::Name const*>(tags->At(index))->Key();
    for (auto const& [list, tagField, actorField] : {std::tuple{&gSpeakers, &gSpeakerDataTag, &gSpeakerDataActor},
             std::tuple{&gFoundActors, &gFoundTag, &gFoundActor}}) {
        if (auto const found = list->ElementsOf(conversation, 64)) {
            for (std::int32_t i = 0; i < found->count; ++i) {
                auto const name = tagField->Read<ue3::Name>(found->At(i));
                if (name && name->Key() == tag) {
                    if (ue3::Object* const actor = actorField->Read<ue3::Object*>(found->At(i)).value_or(nullptr)) return actor;
                }
            }
        }
    }
    return nullptr;
#else
    auto const list = gSpeakerList.ElementsOf(conversation, 64);
    if (!list || index >= list->count) return nullptr;
    return gSpeakerActor.Read<ue3::Object*>(list->At(index)).value_or(nullptr);
#endif
}

// ---- The scene's state ------------------------------------------------------------------------------------------------------------
struct CircleWalker {
    ue3::Object* actor = nullptr;
    std::string name;
    char const* kind = "circle";   // "circle" or "squad" (the records' walk.kind)
    ue3::Vector to{};
    ue3::Vector from{};            // where it stood when the walk was asked (the records' walk.from)
    double faceYawDeg = 0.0;
    std::uint64_t startAtMs = 0;   // the stagger: it sets off then
    std::optional<ai_walk::Walk> walk;
    char const* phase = "waiting"; // waiting, walking, arrived, stalled, refused
    bool done = false;
};
struct CircleScene {
    std::uint32_t serial = 0;      // the conversation it belongs to (gC.serial)
    bool graphRead = false;
    cw::DialogGraph graph;
    cw::ConversationCircle circle;
    std::vector<ue3::Object*> ids;  // the circle's member ids
    std::vector<int> out;           // ids that stay out (too far, can't walk)
    std::vector<CircleWalker> walkers;
    bool squadDone = false;
    bool putBackDone = false;
    unsigned joined = 0, walked = 0, early = 0, still = 0, tooFar = 0, arrived = 0, stalled = 0, refused = 0;
    unsigned squadWalked = 0, squadPlaced = 0, squadPutBack = 0, squadStayed = 0;
};
CircleScene gCircle;

// Where each squadmate stood on foot, the last on-foot tick (ME1's Foot::squad): the conversation's start may move them.
struct FootSquadmate {
    ue3::Object* pawn = nullptr;
    ue3::Vector at{};
    ue3::Rotator rotation{};
    std::string name;
};
std::vector<FootSquadmate> gFootSquad;
std::uint64_t gFootSquadMs = 0;

std::string const* FootSquadName(ue3::Object const* actor) {
    for (FootSquadmate const& m : gFootSquad) {
        if (m.pawn == actor && ue3::runtime::IsAlive(m.pawn)) return &m.name;
    }
    return nullptr;
}

int CircleId(ue3::Object* actor) {
    auto const it = std::find(gCircle.ids.begin(), gCircle.ids.end(), actor);
    if (it != gCircle.ids.end()) return static_cast<int>(it - gCircle.ids.begin());
    gCircle.ids.push_back(actor);
    return static_cast<int>(gCircle.ids.size()) - 1;
}

CircleWalker* WalkerOf(ue3::Object const* actor) {
    for (CircleWalker& w : gCircle.walkers) {
        if (w.actor == actor && !w.done) return &w;
    }
    return nullptr;
}

// A person one of our walks moves now (the after-the-fact keep in place leaves its steps alone).
bool CircleWalking(void const* actor) {
    for (CircleWalker const& w : gCircle.walkers) {
        if (w.actor == actor && !w.done) return true;
    }
    return false;
}

// Someone we moved by SetLocation: the keep-in-place takes its new place as where it stands.
void NoteMovedByUs(ue3::Object* actor, ue3::Vector const& at) {
    for (Kept& k : gC.kept) {
        if (k.actor == actor) {
            k.at = at;
            cw::Calm(k.insist);
        }
    }
}

double FlatUnits(ue3::Vector const& a, ue3::Vector const& b) {
    return std::hypot(static_cast<double>(a.x) - b.x, static_cast<double>(a.y) - b.y);
}

// The floor under a place on the pawn's level (a trace down from its centre past its feet), or false.
bool FloorUnder(ue3::Object* tracer, ue3::Object* pawn, ue3::Vector const& at) {
    ue3::Object* const cylinder = pawn != nullptr ? gPawnCylinder.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
    float const half = cylinder != nullptr ? gCylinderHeight.Read<float>(cylinder).value_or(90.0f) : 90.0f;
    return !LineOfSight(tracer, at, ue3::Vector{at.x, at.y, at.z - half - 60.0f});  // something is hit: a floor
}

// Whether the game's AI can walk this person: a pawn (ME3's stunt actors have no AI), on the ground and standing.
bool CanCircleWalk(ue3::Object* actor, std::string& why) {
    if (!player::IsBioPawn(actor)) {
        why = "no AI to walk it (a stunt actor: the authored composition stays)";
        return false;
    }
    if (auto const physics = gWalkPhysics.Read<std::uint8_t>(actor); physics && *physics != 1) {
        why = std::format("not on the ground (its physics {})", static_cast<int>(*physics));
        return false;
    }
    auto const at = player::Location(actor);
    auto const head = HeadPoseOf(actor);
    if (at && head && head->at.z - at->z < 62.0) {
        why = std::format("seated (its head {:.2f} m above its place)", (head->at.z - at->z) / 100.0);
        return false;
    }
    if (TrackMovedLately(actor, 300)) {  // (a cutscene just before the conversation moved everyone a moment ago: not now)
        why = "a matinee's track moves it (an authored walk)";
        return false;
    }
    return true;
}

// Turned to `yawDeg` the way the game turns a pawn (its desired rotation, at its own rate).
void FaceToward(ue3::Object* actor, double yawDeg) {
    ue3::Function* const fn = gBioSetDesiredRotation.Get();
    auto const rot = player::Rotation(actor);
    if (fn == nullptr || !rot || !player::IsBioPawn(actor)) return;
    ue3::Rotator r = *rot;
    r.yaw = static_cast<std::int32_t>(yawDeg * 65536.0 / 360.0);
    ue3::Params p(ue3::runtime::Get(), fn);
    if (p.Valid() && p.Set("rDesiredRotation", r) && p.SetBool("bForce", false)) ue3::runtime::Call(actor, fn, p);
}

bool PutPersonAt(ue3::Object* actor, ue3::Vector const& at, double yawDeg) {
    ue3::Function* const setLocation = gSetLocation.Get();
    ue3::Function* const setRotation = gSetRotation.Get();
    if (setLocation == nullptr) return false;
    ue3::Params move(ue3::runtime::Get(), setLocation);
    if (!move.Valid() || !move.Set("NewLocation", at) || !ue3::runtime::Call(actor, setLocation, move) ||
        !move.ReturnBool().value_or(false)) {
        return false;
    }
    write_journal::Wrote("ConversationWorld.circlePut", actor, 0);
    NoteMovedByUs(actor, at);
    if (setRotation != nullptr) {
        if (auto rot = player::Rotation(actor)) {
            rot->yaw = static_cast<std::int32_t>(yawDeg * 65536.0 / 360.0);
            ue3::Params turn(ue3::runtime::Get(), setRotation);
            if (turn.Valid() && turn.Set("NewRotation", *rot)) ue3::runtime::Call(actor, setRotation, turn);
        }
    }
    FaceToward(actor, yawDeg);
    return true;
}

// A walk queued (it sets off at `startAtMs`); a walk of the same person under way ends first (a squadmate who speaks leaves its
// place in the formation for the circle).
void QueueWalk(ue3::Object* actor, std::string const& name, char const* kind, ue3::Vector const& to, double faceYawDeg,
    std::uint64_t startAtMs) {
    if (CircleWalker* const old = WalkerOf(actor)) {
        if (old->walk) ai_walk::End(*old->walk, "it walks somewhere else");
        old->done = true;
    }
    CircleWalker w;
    w.actor = actor;
    w.name = name;
    w.kind = kind;
    w.to = to;
    w.from = player::Location(actor).value_or(to);
    w.faceYawDeg = faceYawDeg;
    w.startAtMs = startAtMs;
    gCircle.walkers.push_back(std::move(w));
}

#if defined(MEVR_GAME_LE2)
// ME2's walk goes to an actor (SFXAI_Core.MoveToGoal): a goal of ours at each walker's place (a BioLookAtTarget, the eyes stand-in's
// class, spawned once and moved), so the walk ends on the place and not at the level's navigation point nearest it.
std::vector<ue3::Kept> gWalkGoals;
ue3::Object* WalkGoal(std::size_t index, ue3::Vector const& at) {
    while (gWalkGoals.size() <= index) gWalkGoals.emplace_back();
    ue3::Object* goal = gWalkGoals[index].Live();
    if (goal == nullptr) {
        ue3::Class* const cls = gEyesClass.Get();
        ue3::Function* const spawn = gActorSpawn.Get();
        ue3::Object* const controller = player::Controller();
        if (cls == nullptr || spawn == nullptr || controller == nullptr) return nullptr;
        ue3::Reflection& r = ue3::runtime::Get();
        ue3::Object* defaults = nullptr;
        for (std::int32_t i = 0, n = r.ObjectCount(); i < n && defaults == nullptr; ++i) {
            ue3::Object* const o = r.ObjectAt(i);
            if (o != nullptr && r.ClassOf(o) == cls && (r.FlagsOf(o) & 0x200) != 0) defaults = o;  // RF_ClassDefaultObject
        }
        bool const wasStatic = defaults != nullptr && gActorStatic.ReadBool(defaults).value_or(false);
        bool const wasNoDelete = defaults != nullptr && gActorNoDelete.ReadBool(defaults).value_or(false);
        if (wasStatic) gActorStatic.WriteBool(defaults, false);  // journalled by FieldRef; put back right after the spawn
        if (wasNoDelete) gActorNoDelete.WriteBool(defaults, false);
        ue3::Params p(r, spawn);
        p.Set("SpawnClass", cls);
        p.Set("SpawnOwner", controller);
        p.Set("SpawnLocation", at);
        p.SetBool("bNoCollisionFail", true);
        ue3::runtime::Call(controller, spawn, p);
        if (wasStatic) gActorStatic.WriteBool(defaults, true);
        if (wasNoDelete) gActorNoDelete.WriteBool(defaults, true);
        goal = p.Return<ue3::Object*>().value_or(nullptr);
        if (goal == nullptr || !ue3::IsA(goal, gEyesClass)) return nullptr;
        gWalkGoals[index] = ue3::Kept(goal);
        log::Info("conversation world: circle: a walk goal made ({})", ue3::runtime::FullName(goal));
    }
    return PutEyesAt(goal, ik::Vec3{at.x, at.y, at.z}) ? goal : nullptr;
}
#endif

void StepWalkers(std::uint64_t now) {
    for (CircleWalker& w : gCircle.walkers) {
        if (w.done) continue;
        if (!ue3::runtime::IsAlive(w.actor) || gActorHiddenRead.ReadBool(w.actor).value_or(false)) {
            if (w.walk) ai_walk::End(*w.walk, "gone or hidden");
            w.done = true;
            w.phase = "stalled";
            continue;
        }
        if (!w.walk) {
            if (now < w.startAtMs) continue;
            std::string why;
#if defined(MEVR_GAME_LE2)
            ue3::Object* const goal = WalkGoal(static_cast<std::size_t>(&w - gCircle.walkers.data()), w.to);  // one per walker
            w.walk = goal != nullptr ? ai_walk::StartToGoal(w.actor, w.to, goal, why) : ai_walk::Start(w.actor, w.to, why);
#else
            w.walk = ai_walk::Start(w.actor, w.to, why);
#endif
            if (!w.walk) {
                w.done = true;
                w.phase = "refused";
                ++gCircle.refused;
                log::Info("conversation world: {}: {}'s walk refused ({}): it takes part where it stands", w.kind, w.name, why);
                continue;
            }
            w.phase = "walking";
            continue;
        }
        ai_walk::Phase const phase = ai_walk::Step(*w.walk);
        if (phase == ai_walk::Phase::Walking) continue;
        auto const at = player::Location(w.actor);
        double const left = at ? FlatUnits(*at, w.to) : 0.0;
        // A walk the AI ended near its place after walking most of the way counts as arrived (it turns to the circle): ME2's
        // MoveToGoal gave Prazza's walk-up up 1.4 m short after 3.9 m (lane FPC6, Tali's reintroduction started without its
        // cutscene), and a speaker left facing where it walked looked unfinished.
        constexpr double kNearUnits = 160.0;
        double const walkedUnits = at ? FlatUnits(*at, w.from) : 0.0;
        bool const closeEnough = phase != ai_walk::Phase::Arrived && left <= kNearUnits && walkedUnits >= 100.0 && walkedUnits >= left;
        ai_walk::End(*w.walk, phase == ai_walk::Phase::Arrived ? "arrived" : closeEnough ? "near its place" : "stalled");
        w.done = true;
        if (closeEnough) {
            w.phase = "arrived";
            ++gCircle.arrived;
            FaceToward(w.actor, w.faceYawDeg);
            log::Info("conversation world: {}: {}'s walk ended {:.2f} m from its place after {:.2f} m (the AI's own stop): near "
                      "enough, it turns to {:.0f} deg", w.kind, w.name, left / 100.0, walkedUnits / 100.0, w.faceYawDeg);
        } else if (phase == ai_walk::Phase::Arrived) {
            w.phase = "arrived";
            ++gCircle.arrived;
            FaceToward(w.actor, w.faceYawDeg);
            log::Info("conversation world: {}: {} arrived {:.2f} m from its place and turns to {:.0f} deg", w.kind, w.name, left / 100.0,
                w.faceYawDeg);
        } else {
            w.phase = "stalled";
            ++gCircle.stalled;
            log::Info("conversation world: {}: {}'s walk stalled {:.2f} m from its place: it stays where it stands", w.kind, w.name,
                left / 100.0);
        }
        if (at) NoteMovedByUs(w.actor, *at);
    }
}

// ---- The squad --------------------------------------------------------------------------------------------------------------------
// On foot (mode 0): where each squadmate stands.
void CircleNoteFoot() {
    std::vector<squad_weapons::MemberView> const members = squad_weapons::Members();
    if (members.empty()) return;  // (the roster not read yet this level: the last one stands)
    gFootSquad.clear();
    for (squad_weapons::MemberView const& m : members) {
        if (m.pawn == nullptr || !ue3::runtime::IsAlive(m.pawn)) continue;
        auto const at = player::Location(m.pawn);
        auto const rot = player::Rotation(m.pawn);
        if (at && rot) gFootSquad.push_back({m.pawn, *at, *rot, m.name});
    }
    gFootSquadMs = GetTickCount64();
}

// ME1's PutBackSquad: a squadmate the conversation's start moved more than a metre is put back where it stood on foot, if it stood
// with the player (within 10 m of Shepard, 2 m of his height, on a floor). Not after a Cut (the player is at the mark).
void PutBackSquad(ue3::Object* pawn, ue3::Object* owner) {
    if (gC.opening == cw::Opening::Cut || gFootSquadMs == 0 || gC.startMs < gFootSquadMs || gC.startMs - gFootSquadMs > 5000) return;
    for (FootSquadmate const& m : gFootSquad) {
        if (m.pawn == pawn || m.pawn == owner || !ue3::runtime::IsAlive(m.pawn) || gActorHiddenRead.ReadBool(m.pawn).value_or(false) ||
            TrackMovedLately(m.pawn, 300)) {
            continue;
        }
        auto const now = player::Location(m.pawn);
        if (!now || Distance(*now, m.at) < 100.0) continue;
        double const fromShepard = FlatUnits(m.at, gFoot.pawn);
        double const below = std::abs(static_cast<double>(m.at.z) - gFoot.pawn.z);
        if (fromShepard > 1000.0 || below > 200.0 || !FloorUnder(pawn, m.pawn, m.at)) {
            log::Info("conversation world: squad: the conversation's start moved {} {:.1f} m; it was not with the player ({:.1f} m from "
                      "Shepard, {:.1f} m above or below): left where the start put it", DisplayName(m.pawn, pawn),
                Distance(*now, m.at) / 100.0, fromShepard / 100.0, below / 100.0);
            continue;
        }
        bool const put = PutPersonAt(m.pawn, m.at, YawDeg(m.rotation));
        if (put) ++gCircle.squadPutBack;
        log::Info("conversation world: squad: the conversation's start moved {} {:.1f} m: {}", DisplayName(m.pawn, pawn),
            Distance(*now, m.at) / 100.0, put ? "put back where it stood with the player" : "it could not be put back");
    }
}

// ME1's GatherSquad: once a conversation, after its opening, each squadmate walks to its own side of a relaxed formation beside the
// player, facing the owner; under a Cut's dip it is put there at once.
void GatherSquad(ue3::Object* pawn, ue3::Object* conversation, cw::Vec3 const& player, std::uint64_t now) {
    // A second after the opening (ME1's: once the opening is done): a cutscene's tracks just before have let go of the squad.
    if (gCircle.squadDone || !SquadWanted() || now < gC.placedMs + 1000) return;
    gCircle.squadDone = true;
    ue3::Object* const owner = gOwner.Read<ue3::Object*>(conversation).value_or(nullptr);
    // The squad as the roster has it, else as it stood with the player on foot (ME2's roster is empty in a conversation).
    std::vector<ue3::Object*> squad;
    for (squad_weapons::MemberView const& m : squad_weapons::Members()) squad.push_back(m.pawn);
    if (squad.empty()) {
        for (FootSquadmate const& m : gFootSquad) squad.push_back(m.pawn);
    }
    std::vector<ue3::Object*> members;
    for (ue3::Object* const m : squad) {
        if (m == nullptr || m == pawn || m == owner || !ue3::runtime::IsAlive(m) || gActorHiddenRead.ReadBool(m).value_or(false)) {
            continue;
        }
        members.push_back(m);
    }
    if (members.empty()) log::Info("conversation world: squad: no squadmate with the player: no formation");
    if (members.empty()) return;
    auto const ownerAt = owner != nullptr && owner != pawn && ue3::runtime::IsAlive(owner) ? player::Location(owner) : std::nullopt;
    engine::Basis const ahead = engine::ToBasis(engine::UnrealRotation{0, gC.facing.yaw, 0});
    cw::Vec3 const look = ownerAt ? cw::Vec3{ownerAt->x, ownerAt->y, ownerAt->z}
                                  : cw::Vec3{player.x + ahead.forward.x * 300.0, player.y + ahead.forward.y * 300.0, player.z};
    std::vector<cw::FormationSpot> spots = cw::SquadFormation(player, look, static_cast<int>(members.size()), gCircleSettings.sideUnits,
        gCircleSettings.backUnits, gC.serial);
    if (gSquadBehindTest) {
        // The broken run: the places straight behind the player (squad-behind must catch it).
        double const dx = player.x - look.x, dy = player.y - look.y, len = std::max(1.0, std::hypot(dx, dy));
        for (std::size_t i = 0; i < spots.size(); ++i) {
            double const back = 150.0 + 60.0 * static_cast<double>(i);
            spots[i].at = {player.x + dx / len * back, player.y + dy / len * back, player.z};
        }
        log::Info("conversation world: squad: the broken run: the formation's places straight behind the player");
    }
    std::vector<cw::Vec3> from;
    for (ue3::Object* const m : members) {
        auto const at = player::Location(m).value_or(ue3::Vector{});
        from.push_back({at.x, at.y, at.z});
    }
    std::vector<int> const assigned = cw::AssignSpots(from, spots);
    bool const underDip = gC.opening == cw::Opening::Cut;
    for (std::size_t i = 0; i < members.size() && i < assigned.size(); ++i) {
        ue3::Object* const m = members[i];
        std::string const name = DisplayName(m, pawn);
        cw::FormationSpot const& spot = spots[static_cast<std::size_t>(assigned[i])];
        auto const at = player::Location(m);
        if (!at) continue;
        double const away = std::hypot(spot.at.x - at->x, spot.at.y - at->y);
        std::string why;
        if (away < 100.0) {
            log::Info("conversation world: squad: {} is already beside you ({:.1f} m from its place in the formation)", name, away / 100.0);
            continue;
        }
        if (away > 1500.0) {
            ++gCircle.squadStayed;
            log::Info("conversation world: squad: {} is {:.1f} m from its place beside you: too far, it stays where it is", name, away / 100.0);
            continue;
        }
        if (!CanCircleWalk(m, why)) {
            ++gCircle.squadStayed;
            log::Info("conversation world: squad: {} stays where it is ({})", name, why);
            continue;
        }
        ue3::Vector const to{static_cast<float>(spot.at.x), static_cast<float>(spot.at.y), at->z};
        if (!FloorUnder(pawn, m, to)) {
            ++gCircle.squadStayed;
            log::Info("conversation world: squad: no floor at {}'s place beside you: it stays where it is", name);
            continue;
        }
        if (underDip && PutPersonAt(m, to, spot.faceYawDeg)) {
            ++gCircle.squadPlaced;
            log::Info("conversation world: squad: {} is put at its place beside you under the opening's dip ({:.1f} m; facing {:.0f} deg)",
                name, away / 100.0, spot.faceYawDeg);
            continue;
        }
        QueueWalk(m, name, "squad", to, spot.faceYawDeg, now + static_cast<std::uint64_t>(spot.delaySeconds * 1000.0));
        ++gCircle.squadWalked;
        log::Info("conversation world: squad: {} walks {:.1f} m to its place beside you after {:.1f} s (facing {:.0f} deg)", name,
            away / 100.0, spot.delaySeconds, spot.faceYawDeg);
    }
}

// ---- The circle -------------------------------------------------------------------------------------------------------------------
// Each tick of a conversation held in the world, after its opening.
void StepCircle(ue3::Object* controller, ue3::Object* pawn, std::uint64_t now) {
    (void)controller;
    ReadCircleSettings();
    if (!gC.on || !gC.placed || gC.skipped || !gSettings.stay) return;
    if (gCircle.serial != gC.serial) {
        for (CircleWalker& w : gCircle.walkers) {
            if (w.walk && !w.done) ai_walk::End(*w.walk, "another conversation", true);
        }
        gCircle = {};
        gCircle.serial = gC.serial;
    }
    ue3::Object* const conversation = gC.controller;
    if (conversation == nullptr || !ue3::runtime::IsAlive(conversation) || pawn == nullptr) return;
    ue3::Vector const eyes = gDrawnEyes ? ue3::Vector{static_cast<float>(gDrawnEyes->x), static_cast<float>(gDrawnEyes->y), gC.pin.z}
                                        : ue3::Vector{gC.held.x, gC.held.y, gC.pin.z};
    cw::Vec3 const player{eyes.x, eyes.y, eyes.z};  // the player: the drawn eyes (ME1's PlayerEyes), at the feet's height
    if (!gCircle.putBackDone && SquadWanted()) {
        gCircle.putBackDone = true;  // at the opening, before the formation: nothing of the start's places stays drawn
        PutBackSquad(pawn, gOwner.Read<ue3::Object*>(conversation).value_or(nullptr));
    }
    GatherSquad(pawn, conversation, player, now);
    StepWalkers(now);
    if (!CircleWanted()) return;
    cw::CircleSettings const& cs = gCircleSettings.s;
    if (!gCircle.graphRead) {
        gCircle.graphRead = true;
        gCircle.graph = ReadDialogGraph(ConversationData(conversation));
        log::Info("conversation world: circle: the dialogue read for the look ahead: {} lines of others', {} of the player's ({} lines "
                  "ahead)", gCircle.graph.entries.size(), gCircle.graph.replyEntries.size(), gCircleSettings.lookahead);
    }
    ue3::Object* const playerPawn = ConversationPlayer(conversation) != nullptr ? ConversationPlayer(conversation) : pawn;
    ue3::Object* const speaker = gSpeakerNow.Read<ue3::Object*>(conversation).value_or(nullptr);
    ue3::Object* const listener = gListenerNow.Read<ue3::Object*>(conversation).value_or(nullptr);
    int const entry = gEntryNow.Read<std::int32_t>(conversation).value_or(-1);
    bool const involved = speaker == playerPawn || speaker == pawn || listener == playerPawn || listener == pawn ||
        cw::PlayerSoon(gCircle.graph, entry, gCircleSettings.lookahead);
    std::vector<ue3::Object*> const people = Speakers(conversation, playerPawn);
    if (!gCircle.circle.made) {
        if (!involved) return;  // people talking among themselves move for nobody
        ue3::Object* const owner = gOwner.Read<ue3::Object*>(conversation).value_or(nullptr);
        ue3::Object* const anchor = owner != nullptr && owner != playerPawn && owner != pawn && ue3::runtime::IsAlive(owner) ? owner
            : speaker != playerPawn && speaker != pawn ? speaker : listener;
        auto const anchorAt = anchor != nullptr && ue3::runtime::IsAlive(anchor) ? player::Location(anchor) : std::nullopt;
        if (!anchorAt) return;
        int count = 1;  // the player's place
        for (ue3::Object* const p : people) {
            if (p != nullptr && ue3::runtime::IsAlive(p) && !gActorHiddenRead.ReadBool(p).value_or(false)) ++count;
        }
        gCircle.circle = cw::MakeCircle(player, {anchorAt->x, anchorAt->y, anchorAt->z}, std::max(2, count), cs);
        cw::JoinCircle(gCircle.circle, CircleId(anchor), {anchorAt->x, anchorAt->y, anchorAt->z}, !gCircleOwnerWalksTest, cs);
        ++gCircle.joined;
        log::Info("conversation world: circle: conversation {}: formed round you toward {} ({:.1f} m away, {}) at {:.1f} s: {} places, "
                  "{:.1f} m across", gC.serial, DisplayName(anchor, pawn), std::hypot(anchorAt->x - player.x, anchorAt->y - player.y) / 100.0,
            gCircleOwnerWalksTest ? "the broken run: it walks too" : "who stays where it is",
            static_cast<double>(now - gC.startMs) / 1000.0, gCircle.circle.people, 2.0 * gCircle.circle.radius / 100.0);
        if (gCircleOwnerWalksTest) {
            // The broken run (owner-walked must catch it): the owner, who stays by the rule, walks 2 m to its side instead.
            double const dx = anchorAt->x - player.x, dy = anchorAt->y - player.y, len = std::max(1.0, std::hypot(dx, dy));
            ue3::Vector const to{static_cast<float>(anchorAt->x - dy / len * 200.0), static_cast<float>(anchorAt->y + dx / len * 200.0),
                anchorAt->z};
            double const face = std::atan2(player.y - to.y, player.x - to.x) * (180.0 / 3.14159265358979323846);
            if (FloorUnder(pawn, anchor, to)) {
                QueueWalk(anchor, DisplayName(anchor, pawn), "circle", to, face, now);
                ++gCircle.walked;
            }
        }
    }
    // Who joins now: who speaks or is spoken to, and who will within the lines read ahead (they set off early).
    struct Wanted {
        ue3::Object* actor;
        std::string why;
        bool early;
    };
    std::vector<Wanted> wanted{{speaker, "speaks", false}, {listener, "is spoken to", false}};
    for (cw::Upcoming const& u : cw::UpcomingPeople(gCircle.graph, entry, gCircleSettings.lookahead)) {
        wanted.push_back({PersonOfIndex(conversation, playerPawn, u.index),
            std::format("takes part {} line{} from now", u.linesAway, u.linesAway == 1 ? "" : "s"), true});
    }
    int order = 0;
    for (Wanted const& w : wanted) {
        ue3::Object* const a = w.actor;
        if (a == nullptr || a == playerPawn || a == pawn || !ue3::runtime::IsAlive(a) || gActorHiddenRead.ReadBool(a).value_or(false)) continue;
        auto const at = player::Location(a);
        if (!at) continue;
        int const id = CircleId(a);
        if (std::find(gCircle.out.begin(), gCircle.out.end(), id) != gCircle.out.end()) continue;
        std::string const name = DisplayName(a, pawn);
        cw::ConversationCircle trial = gCircle.circle;
        cw::CircleSpot const preview = cw::JoinCircle(trial, id, {at->x, at->y, at->z}, false, cs);
        if (preview.already) continue;
        std::string notWalker;
        if (preview.walk && !CanCircleWalk(a, notWalker)) {
            gCircle.out.push_back(id);
            ++gCircle.still;
            log::Info("conversation world: circle: {} {}, but {}: it takes part where it is", name, w.why, notWalker);
            continue;
        }
        cw::CircleSpot const spot = cw::JoinCircle(gCircle.circle, id, {at->x, at->y, at->z}, false, cs);
        if (spot.tooFar) {
            gCircle.out.push_back(id);
            ++gCircle.tooFar;
            log::Info("conversation world: circle: {} {} {:.1f} m from its place in the circle: too far to walk in, it stays where it is",
                name, w.why, spot.walkUnits / 100.0);
            continue;
        }
        ++gCircle.joined;
        if (!spot.walk) {
            log::Info("conversation world: circle: {} {} and joins where it stands ({:.1f} m from you)", name, w.why,
                std::hypot(at->x - player.x, at->y - player.y) / 100.0);
            continue;
        }
        ue3::Vector const to{static_cast<float>(spot.at.x), static_cast<float>(spot.at.y), at->z};
        if (!FloorUnder(pawn, a, to)) {
            log::Info("conversation world: circle: {} {}, but there is no floor at its place in the circle: it stays where it is", name, w.why);
            continue;
        }
        if (w.early) ++gCircle.early;
        double const delay = cw::StaggerSeconds(order++, cs);
        QueueWalk(a, name, "circle", to, spot.faceYawDeg, now + static_cast<std::uint64_t>(delay * 1000.0));
        ++gCircle.walked;
        log::Info("conversation world: circle: {} {} and walks {:.1f} m into the circle{} (its place at {:.0f} deg, facing {:.0f} deg)", name,
            w.why, spot.walkUnits / 100.0, delay > 0.0 ? std::format(" after {:.1f} s", delay) : std::string(), spot.angleDeg,
            spot.faceYawDeg);
    }
}

// The conversation ended: every walk ends where it is (the scene's own teardown follows), the counts logged.
void CircleEnd(char const* why) {
    if (gCircle.serial == 0) return;
    for (CircleWalker& w : gCircle.walkers) {
        if (w.walk && !w.done) ai_walk::End(*w.walk, why, true);
        w.done = true;
    }
    if (gCircle.circle.made || gCircle.squadWalked + gCircle.squadPlaced + gCircle.squadPutBack > 0) {
        log::Info("conversation world: circle: conversation {} ({}): {} joined, {} walked ({} early), {} arrived, {} stalled, {} refused, "
                  "{} took part where they were, {} too far; squad: {} put back, {} walked, {} placed under the dip, {} stayed",
            gCircle.serial, why, gCircle.joined, gCircle.walked, gCircle.early, gCircle.arrived, gCircle.stalled, gCircle.refused,
            gCircle.still, gCircle.tooFar, gCircle.squadPutBack, gCircle.squadWalked, gCircle.squadPlaced, gCircle.squadStayed);
    }
    gCircle = {};
}

// The records' `walk` of a person (ME1's: {"phase", "to"}, with the walk's kind), else "null".
std::string CircleWalkJson(ue3::Object const* actor) {
    for (auto it = gCircle.walkers.rbegin(); it != gCircle.walkers.rend(); ++it) {
        if (it->actor == actor) {
            return std::format("{{\"phase\": \"{}\", \"to\": [{:.0f}, {:.0f}, {:.0f}], \"from\": [{:.0f}, {:.0f}, {:.0f}], \"kind\": \"{}\"}}",
                it->phase, it->to.x, it->to.y, it->to.z, it->from.x, it->from.y, it->from.z, it->kind);
        }
    }
    return "null";
}

// The squad's formation is still being composed (not asked yet, or a squadmate on its way): the records' opening waits for it.
bool CircleComposing() {
    if (!SquadWanted() || gCircle.serial != gC.serial) return false;
    if (!gCircle.squadDone) return true;
    return std::any_of(gCircle.walkers.begin(), gCircle.walkers.end(),
        [](CircleWalker const& w) { return !w.done && std::string_view(w.kind) == "squad"; });
}

// The records' `walks`: whether the game's AI could walk this person.
bool CircleCanWalk(ue3::Object* actor) {
    std::string why;
    return actor != nullptr && CanCircleWalk(actor, why);
}

// The records' `circle` (ME1's: centre, radius, places, members at their places), else "null".
std::string CircleJson(ue3::Object* pawn) {
    if (!gCircle.circle.made) return "null";
    std::string members;
    for (cw::ConversationCircle::Member const& m : gCircle.circle.members) {
        double const a = m.angleDeg * (3.14159265358979323846 / 180.0);
        ik::Vec3 const at{gCircle.circle.centre.x + std::cos(a) * gCircle.circle.radius,
            gCircle.circle.centre.y + std::sin(a) * gCircle.circle.radius, gCircle.circle.centre.z};
        ue3::Object* const actor = m.id >= 0 && m.id < static_cast<int>(gCircle.ids.size()) ? gCircle.ids[static_cast<std::size_t>(m.id)] : nullptr;
        members += std::format("{}{{\"name\": \"{}\", \"at\": {}}}", members.empty() ? "" : ", ", JsonText(DisplayName(actor, pawn)), P3(at));
    }
    return std::format("{{\"centre\": {}, \"radius\": {:.0f}, \"people\": {}, \"members\": [{}]}}",
        P3({gCircle.circle.centre.x, gCircle.circle.centre.y, gCircle.circle.centre.z}), gCircle.circle.radius, gCircle.circle.people, members);
}

std::string CircleConsole(std::string const& what, std::string v) {
    v.erase(0, v.find_first_not_of(' '));
    ReadCircleSettings();
    if (what == "circle") {
        if (v == "on" || v == "off") gCircleOverride = v == "on";
        if (v == "auto") gCircleOverride.reset();
        if (v.rfind("ownerwalks", 0) == 0) gCircleOwnerWalksTest = v.find("on") != std::string::npos;
        return std::format("conversation world: circle {} ({}){}; this scene: {} joined, {} walked, {} arrived", CircleWanted() ? "on" : "off",
            gCircleOverride ? "mevr convo circle" : "[Cinematics] worldCircle", gCircleOwnerWalksTest ? "; the broken run: the owner walks" : "",
            gCircle.joined, gCircle.walked, gCircle.arrived);
    }
    if (v == "on" || v == "off") gSquadOverride = v == "on";
    if (v == "auto") gSquadOverride.reset();
    if (v.rfind("behind", 0) == 0) gSquadBehindTest = v.find("on") != std::string::npos;
    return std::format("conversation world: squad formation {} ({}){}; this scene: {} put back, {} walked, {} placed, {} stayed",
        SquadWanted() ? "on" : "off", gSquadOverride ? "mevr convo squad" : "[Cinematics] worldSquadGather",
        gSquadBehindTest ? "; the broken run: the places behind the player" : "", gCircle.squadPutBack, gCircle.squadWalked,
        gCircle.squadPlaced, gCircle.squadStayed);
}

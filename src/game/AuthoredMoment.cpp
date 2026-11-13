// SPDX-License-Identifier: MIT
// LE1 authored moments: what the level's script does to the player, measured (game/AuthoredMoment.hpp, research/69).
#include "game/AuthoredMoment.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/ConversationWorld.hpp"
#include "engine/ConversationHooks.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/SettingsFile.hpp"
#include "game/ConversationWorld.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace mevr::game::authored_moment {

namespace {

namespace cw = vr::conversation_world;

constexpr double kUnitsPerMeter = 100.0;

ue3::FunctionRef gControllerTeleport{"Engine.Controller.OnTeleport"};
ue3::FunctionRef gPawnTeleport{"SFXGame.BioPawn.OnTeleport"};
ue3::FunctionRef gBlackScreen{"SFXGame.BioSeqAct_BlackScreen.Activated"};
ue3::FunctionRef gCinematicToggle{"SFXGame.BioSeqAct_BioToggleCinematicMode.Activated"};
ue3::FunctionRef gFastTrace{"Engine.Actor.FastTrace"};
ue3::FieldRef gActorHidden{"Engine.Actor", "bHidden"};
ue3::FieldRef gCinematicMode{"Engine.PlayerController", "bCinematicMode"};
ue3::FieldRef gGuiManager{"Engine.PlayerController", "m_oCachedScaleFormManager"};
ue3::FunctionRef gHideBlackScreen{"SFXGame.MassEffectGuiManager.HideBlackScreen"};
// EBlackScreenActionSet: 0 turn black on, 1 off, 2 fade to black, 3 fade from black.
ue3::FieldRef gBlackScreenAction{"SFXGame.BioSeqAct_BlackScreen", "m_eBlackScreenAction"};

// ---- Settings ----------------------------------------------------------------------------------------------------------------
struct Settings {
    bool world = false;  // conversations = world
    bool log = true;     // worldMoments = log | off
    bool Active() const { return world && log; }
};
Settings gSettings;
std::uint64_t gSettingsMs = 0;

void RefreshSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gSettingsMs != 0 && now - gSettingsMs < 1000) {
        return;
    }
    gSettingsMs = now;
    ini::Loaded const loaded = ini::Read(engine::SettingsFile());
    Settings s;
    if (auto const text = ini::Get(loaded.file, "Cinematics", "conversations")) {
        s.world = cw::ParseMode(text->c_str(), cw::Mode::Screen) == cw::Mode::World;
    }
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldMoments")) {
        std::string const w = ini::Lower(ini::Trim(*text));
        s.log = !(w == "off" || w == "0" || w == "none");
    }
    if (s.Active() != gSettings.Active()) {
        log::Info("moment: [Cinematics] worldMoments = {} ({})", s.log ? "log" : "off", s.world ? "conversations = world: watching" : "not the world mode: idle");
    }
    gSettings = s;
}

// ---- The move track's question (the engine's physics step asks it) ------------------------------------------------------------
std::atomic<void const*> gPawnForHook{nullptr};  // the player's pawn, set each tick
std::atomic<unsigned> gTrackCalls{0};            // calls for the player's pawn since the hook went in
std::atomic<void const*> gTrackSeen{nullptr};    // the last track that moved him
std::atomic<void const*> gTrackInstSeen{nullptr};  // its instance (inside the SeqAct_Interp that plays it: names the matinee)

std::atomic<std::uint64_t> gTrackMs{0};          // when the last call for him came

// The actors a move track moved lately (game thread: the engine's physics step writes, the mod's tick reads).
struct TrackMove {
    void const* actor = nullptr;
    std::uint64_t ms = 0;
};
TrackMove gTrackMoves[24];
std::atomic<unsigned> gTrackWithheld{0};
// Where the track has the player at its last call (the script's place for him), when it could be asked.
std::atomic<float> gTrackPlace[3];
std::atomic<std::uint64_t> gTrackPlaceMs{0};

bool OnMoveTrack(void const* actor, void const* track, void const* instance, float time) {
    if (actor != nullptr) {
        // Remembered per actor: an authored walk of someone in a conversation is his own move, not a placement to undo.
        std::uint64_t const now = GetTickCount64();
        TrackMove* slot = &gTrackMoves[0];
        for (TrackMove& m : gTrackMoves) {
            if (m.actor == actor) {
                slot = &m;
                break;
            }
            if (m.ms < slot->ms) {
                slot = &m;  // the oldest, to reuse
            }
        }
        slot->actor = actor;
        slot->ms = now;
    }
    if (actor != nullptr && actor == gPawnForHook.load(std::memory_order_relaxed)) {
        gTrackCalls.fetch_add(1, std::memory_order_relaxed);
        gTrackSeen.store(track, std::memory_order_relaxed);
        gTrackInstSeen.store(instance, std::memory_order_relaxed);
        gTrackMs.store(GetTickCount64(), std::memory_order_relaxed);
        // worldMoments = walk: the player walks it himself (game/ConversationWorld holds the view and moves him). Where
        // the track has him is kept: the script counts on him being there when the matinee ends.
        if (conversation_world::MomentWithholds()) {
            float place[3];
            if (engine::conversation_hooks::MoveTrackPlace(track, instance, time, place)) {
                for (int i = 0; i < 3; ++i) {
                    gTrackPlace[i].store(place[i], std::memory_order_relaxed);
                }
                gTrackPlaceMs.store(GetTickCount64(), std::memory_order_relaxed);
            }
            gTrackWithheld.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }
    return false;  // the track moves its actor as authored
}

// ---- A moment: the stretch in which a track moves the player -------------------------------------------------------------------
struct Vec {
    double x = 0.0, y = 0.0, z = 0.0;
};
Vec V(ue3::Vector const& v) {
    return {v.x, v.y, v.z};
}
double Dist(Vec const& a, Vec const& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
std::string F(Vec const& v) {
    return std::format("({:.0f}, {:.0f}, {:.0f})", v.x, v.y, v.z);
}

struct Moment {
    bool on = false;
    std::uint64_t startMs = 0;
    std::uint64_t lastCallMs = 0;
    unsigned callsAtStart = 0;
    Vec from;
    Vec last;
    double path = 0.0;       // the length of the way the track took him
    std::string modes;       // the game modes passed through, in order
    int lastMode = -100;
    bool hidden = false;     // his pawn was hidden at some point
    bool cinematic = false;  // the controller's cinematic mode was on at some point
    int teleports = 0;
    int blackScreens = 0;
    int number = 0;
};
Moment gM;
unsigned gCallsSeen = 0;
Vec gPawnAt;             // the pawn's place at the last tick (where a teleport took him from)
bool gHavePawnAt = false;
int gMoments = 0;
int gTeleports = 0;
int gBlackScreens = 0;
int gToggles = 0;
std::string gLast;       // the last summary

bool LineOfSight(ue3::Object* from, Vec const& start, Vec const& end) {
    if (from == nullptr || gFastTrace.Get() == nullptr) {
        return true;
    }
    ue3::Params trace(ue3::runtime::Get(), gFastTrace.Get());
    trace.Set("TraceEnd", ue3::Vector{static_cast<float>(end.x), static_cast<float>(end.y), static_cast<float>(end.z)});
    trace.Set("TraceStart", ue3::Vector{static_cast<float>(start.x), static_cast<float>(start.y), static_cast<float>(start.z)});
    ue3::runtime::Call(from, gFastTrace.Get(), trace);
    return trace.ReturnBool().value_or(true);
}

void NoteMode(ue3::Object* controller) {
    auto const mode = player::GameMode(controller);
    int const m = mode ? static_cast<int>(*mode) : -1;
    if (m != gM.lastMode) {
        gM.lastMode = m;
        gM.modes += std::format("{}{}", gM.modes.empty() ? "" : ", ", m);
    }
}

void EndMoment(char const* why) {
    double const seconds = static_cast<double>(gM.lastCallMs - gM.startMs) / 1000.0;
    gLast = std::format("{{\"number\": {}, \"why\": \"{}\", \"seconds\": {:.1f}, \"pathM\": {:.2f}, \"fromToM\": {:.2f}, \"riseM\": {:.2f}, "
                        "\"modes\": [{}], \"hidden\": {}, \"cinematic\": {}, \"teleports\": {}, \"blackScreens\": {}}}",
        gM.number, why, seconds, gM.path / kUnitsPerMeter, Dist(gM.from, gM.last) / kUnitsPerMeter, (gM.last.z - gM.from.z) / kUnitsPerMeter,
        gM.modes, gM.hidden ? "true" : "false", gM.cinematic ? "true" : "false", gM.teleports, gM.blackScreens);
    log::Info("moment: a track moved Shepard for {:.1f} s: {:.2f} m along its way, {:.2f} m from {} to {}; game modes {}; his pawn {}, "
              "cinematic mode {}", seconds, gM.path / kUnitsPerMeter, Dist(gM.from, gM.last) / kUnitsPerMeter, F(gM.from), F(gM.last), gM.modes,
        gM.hidden ? "was hidden" : "stayed shown", gM.cinematic ? "was on" : "stayed off");
    log::Info("moment: summary {}", gLast);
    gM = {};
}

// ---- The script's teleport, black screen and cinematic switch (ProcessEvent post-handlers, game thread) -------------------------
void OnTeleport(ue3::Object* context, ue3::Function*, void*, void*) {
    if (!gSettings.Active()) {
        return;
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = player::Pawn(controller);
    if (context == nullptr || (context != controller && context != pawn) || pawn == nullptr || !gHavePawnAt) {
        return;
    }
    auto const now = player::Location(pawn);
    if (!now) {
        return;
    }
    Vec const to = V(*now);
    double const flat = std::hypot(to.x - gPawnAt.x, to.y - gPawnAt.y);
    if (Dist(to, gPawnAt) < 5.0) {
        return;  // the controller's handler runs the pawn's: the second report of one teleport
    }
    // The same space, as the opening's rule has it: in sight from where his eyes were, within a walk, not another deck.
    Vec const eyes{gPawnAt.x, gPawnAt.y, gPawnAt.z + 60.0};
    bool const sight = LineOfSight(pawn, eyes, {to.x, to.y, to.z + 60.0});
    cw::OpeningFacts facts;
    facts.known = true;
    facts.markUnits = flat;
    facts.riseUnits = to.z - gPawnAt.z;
    facts.sightToMark = sight;
    cw::Opening const rule = cw::DecideOpening(facts, cw::OpeningRule::Auto);
    ++gTeleports;
    if (gM.on) {
        ++gM.teleports;
    }
    log::Info("moment: the script teleported Shepard {:.2f} m ({:+.2f} m up) from {} to {}: {}, so {} ({})", flat / kUnitsPerMeter,
        (to.z - gPawnAt.z) / kUnitsPerMeter, F(gPawnAt), F(to), sight ? "in sight" : "out of sight",
        rule == cw::Opening::Cut ? "another composition: a cut" : "the same space: it could be walked", cw::Name(rule));
    gPawnAt = to;
}

void OnBlackScreen(ue3::Object* context, ue3::Function*, void*, void*) {
    if (!gSettings.Active()) {
        return;
    }
    ++gBlackScreens;
    if (gM.on) {
        ++gM.blackScreens;
    }
    // While the moment is held in first person nothing is hidden behind a black screen: what it covered in the original (the
    // teleport, the swap of a character) either does not happen or goes under the mod's own dip.
    bool hidden = false;
    if (conversation_world::MomentViewOn() && gHideBlackScreen.Get() != nullptr) {
        ue3::Object* const controller = player::Controller();
        ue3::Object* const manager = controller != nullptr ? gGuiManager.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
        if (manager != nullptr && ue3::runtime::IsAlive(manager)) {
            ue3::Params p(ue3::runtime::Get(), gHideBlackScreen.Get());
            if (p.Valid()) {
                p.SetBool("bFade", false);
                hidden = ue3::runtime::Call(manager, gHideBlackScreen.Get(), p);
            }
        }
    }
    // What the script covers by going black (a character switched off or swapped: Anderson vanished from the stairs in
    // plain sight when his walk ended, 2026-11-10) still wants a cover: the mod's own short dip from black, the view where
    // it is (the owner: a forced placement needs a fade). Only where the script turns black on, not where it comes back.
    int const action = context != nullptr ? static_cast<int>(gBlackScreenAction.Read<std::uint8_t>(context).value_or(255)) : 255;
    bool const dipped = hidden && (action == 0 || action == 2) && conversation_world::MomentDip();
    conversation_world::ScriptBlackScreen(action);  // a staged conversation's cut to a new composition
    log::Info("moment: the script's black screen ({}, action {}){}{}{}", context != nullptr ? ue3::runtime::FullName(context) : std::string("?"),
        action, gM.on ? " during a track's move of Shepard" : "", hidden ? ": taken down again (the moment is held in first person)" : "",
        dipped ? "; a dip from black in its place if it covers a change in the player's view" : "");
}

void OnCinematicToggle(ue3::Object* context, ue3::Function*, void*, void*) {
    if (!gSettings.Active()) {
        return;
    }
    ++gToggles;
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = player::Pawn(controller);
    auto const mode = player::GameMode(controller);
    log::Info("moment: the script switched the cinematic mode ({}): the controller's cinematic mode is now {}, game mode {}, Shepard's pawn {}",
        context != nullptr ? ue3::runtime::FullName(context) : std::string("?"),
        controller != nullptr && gCinematicMode.ReadBool(controller).value_or(false) ? "on" : "off", mode ? static_cast<int>(*mode) : -1,
        pawn != nullptr && gActorHidden.ReadBool(pawn).value_or(false) ? "hidden" : "shown");
    if (controller != nullptr && gCinematicMode.ReadBool(controller).value_or(false) && mode && *mode == 6) {
        conversation_world::MomentMayStart();  // a walk of Shepard's may begin: the view is held from this instant
    }
}

bool gHandlers = false;

void EnsureHandlers(ue3::Object* controller, ue3::Object* pawn) {
    if (gHandlers || controller == nullptr || !player::IsBioPawn(pawn)) {
        return;
    }
    // The teleport action calls its target's OnTeleport by name: the handler goes on whatever function that name is for the
    // player's own pawn and controller classes (an override in a subclass would never pass a handler on the base's).
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Function* const pawnTeleport = r.FindFunction(r.ClassOf(pawn), "OnTeleport");
    ue3::Function* const controllerTeleport = r.FindFunction(r.ClassOf(controller), "OnTeleport");
    if (pawnTeleport == nullptr) {
        return;
    }
    gHandlers = true;
    engine::process_event::AddPostHandler(pawnTeleport, &OnTeleport);
    if (controllerTeleport != nullptr && controllerTeleport != pawnTeleport) {
        engine::process_event::AddPostHandler(controllerTeleport, &OnTeleport);
    }
    if (pawnTeleport != gPawnTeleport.Get() || controllerTeleport != gControllerTeleport.Get()) {
        log::Info("moment: the player's OnTeleport handlers are {} and {}", r.FullName(pawnTeleport),
            controllerTeleport != nullptr ? r.FullName(controllerTeleport) : std::string("none"));
    }
    if (ue3::Function* const f = gBlackScreen.Get()) {
        engine::process_event::AddPostHandler(f, &OnBlackScreen);
    }
    if (ue3::Function* const f = gCinematicToggle.Get()) {
        engine::process_event::AddPostHandler(f, &OnCinematicToggle);
    }
    log::Info("moment: watching the script's teleports{}{} of the player", gBlackScreen.Get() != nullptr ? ", black screens" : "",
        gCinematicToggle.Get() != nullptr ? " and cinematic mode switches" : "");
}

} // namespace

void Tick() {
    RefreshSettings();
    if (!ue3::runtime::Ready()) {
        return;
    }
    engine::conversation_hooks::SetMoveTrackProvider(gSettings.Active() ? &OnMoveTrack : nullptr);
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = player::Pawn(controller);
    gPawnForHook.store(pawn, std::memory_order_relaxed);
    if (!gSettings.Active() || pawn == nullptr) {
        gHavePawnAt = false;
        if (gM.on) {
            EndMoment(pawn == nullptr ? "no pawn" : "the setting is off");
        }
        return;
    }
    EnsureHandlers(controller, pawn);
    auto const at = player::Location(pawn);
    std::uint64_t const now = GetTickCount64();
    unsigned const calls = gTrackCalls.load(std::memory_order_relaxed);
    bool const moved = calls != gCallsSeen;
    gCallsSeen = calls;
    if (moved && at) {
        if (!gM.on) {
            gM = {};
            gM.on = true;
            gM.number = ++gMoments;
            gM.startMs = now;
            gM.from = gHavePawnAt ? gPawnAt : V(*at);
            gM.last = gM.from;
            auto const mode = player::GameMode(controller);
            // The track instance's path names the matinee (its group instance lives in the SeqAct_Interp that plays it): which
            // of research/69's 21 moments this is (item 11).
            auto* const inst = static_cast<ue3::Object*>(const_cast<void*>(gTrackInstSeen.load(std::memory_order_relaxed)));
            std::string const matinee = inst != nullptr && ue3::runtime::IsAlive(inst) ? ue3::runtime::FullName(inst) : std::string("?");
            log::Info("moment: a matinee's move track takes Shepard from {} (game mode {}, his pawn {}): {}", F(gM.from), mode ? static_cast<int>(*mode) : -1,
                gActorHidden.ReadBool(pawn).value_or(false) ? "hidden" : "shown", matinee);
        }
        gM.lastCallMs = now;
        Vec const here = V(*at);
        double const step = Dist(here, gM.last);
        if (step < 600.0) {
            gM.path += step;  // a longer step in one tick is a cut inside the track, not a walk
        }
        gM.last = here;
    }
    if (gM.on) {
        NoteMode(controller);
        gM.hidden = gM.hidden || gActorHidden.ReadBool(pawn).value_or(false);
        gM.cinematic = gM.cinematic || (controller != nullptr && gCinematicMode.ReadBool(controller).value_or(false));
        if (now - gM.lastCallMs > 500) {
            EndMoment("the track let him go");
        }
    }
    if (at) {
        gPawnAt = V(*at);
        gHavePawnAt = true;
    }
}

bool TrackMoves(void const* actor, std::uint64_t withinMs) {
    std::uint64_t const now = GetTickCount64();
    for (TrackMove const& m : gTrackMoves) {
        if (m.actor == actor && actor != nullptr) {
            return now - m.ms <= withinMs;
        }
    }
    return false;
}

bool PlayerTrackPlace(float place[3], std::uint64_t* ms) {
    std::uint64_t const at = gTrackPlaceMs.load(std::memory_order_relaxed);
    if (at == 0) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        place[i] = gTrackPlace[i].load(std::memory_order_relaxed);
    }
    if (ms != nullptr) {
        *ms = at;
    }
    return true;
}

std::uint64_t LastPlayerTrackMs() {
    return gTrackMs.load(std::memory_order_relaxed);
}

std::string Command(std::string const&) {
    return std::format("moment: worldMoments = {} ({}); {} moments, {} teleports of the player, {} black screens, {} cinematic switches seen; "
                       "the move track's hook was asked about Shepard {} times ({} withheld){}{}", gSettings.log ? "log" : "off", gSettings.Active() ? "watching" : "idle",
        gMoments, gTeleports, gBlackScreens, gToggles, gTrackCalls.load(), gTrackWithheld.load(), gM.on ? "; a moment is running" : "",
        gLast.empty() ? std::string() : "\n  the last: " + gLast);
}

void LogStats() {
    if (gMoments > 0 || gTeleports > 0 || gBlackScreens > 0 || gToggles > 0) {
        log::Info("moment: {} moments (a track moving Shepard), {} teleports of the player, {} black screens, {} cinematic switches", gMoments,
            gTeleports, gBlackScreens, gToggles);
    }
}

void Shutdown() {
    engine::conversation_hooks::SetMoveTrackProvider(nullptr);
    gPawnForHook.store(nullptr, std::memory_order_relaxed);
    gHandlers = false;  // the handlers are cleared with every other module's (engine::process_event::ClearHandlers)
    gM = {};
}

} // namespace mevr::game::authored_moment

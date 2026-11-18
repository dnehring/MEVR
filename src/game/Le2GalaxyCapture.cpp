// SPDX-License-Identifier: MIT
// ME2's galaxy map over the CIC's projector: ME2's adapter of the sequels' flow (game/Le2GalaxyCapture.hpp).
#include "game/Le2GalaxyCapture.hpp"

#if defined(MEVR_GAME_LE2)  // ME2's adapter only (ME1 and ME3 compile nothing of it)
#include "game/GalaxyProjectorFlow.hpp"
#include "core/WriteJournal.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <sstream>

namespace mevr::game::le2_galaxy_capture {

namespace {

ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gStreamingLevels{"Engine.WorldInfo", "StreamingLevels", ue3::kEveryGame};
ue3::FieldRef gPackageName{"Engine.LevelStreaming", "PackageName", ue3::kEveryGame};
ue3::FieldRef gLoadedLevel{"Engine.LevelStreaming", "LoadedLevel", ue3::kEveryGame};
ue3::FieldRef gIsVisible{"Engine.LevelStreaming", "bIsVisible", ue3::kEveryGame};

ue3::FunctionRef gClientSetLocation{"Engine.Controller.ClientSetLocation", ue3::kEveryGame};
ue3::ClassRef gGalaxyHandler{"SFXGame.BioSFHandler_GalaxyMap", ue3::kEveryGame};
ue3::FunctionRef gCloseGalaxyMap{"SFXGame.BioSFHandler_GalaxyMap.CloseGalaxyMap", ue3::kEveryGame};

// `mevr galaxy exit`: the map's own close (BioSFHandler_GalaxyMap.CloseGalaxyMap, what its Exit button does) on every live
// handler: a test's way out of the map, as the pad's B is the player's (a replayed B reaches the map's movie only with the game
// window in front).
std::string Exit() {
    ue3::Function* const f = gCloseGalaxyMap.Get();
    if (f == nullptr || gGalaxyHandler.Get() == nullptr) return "mevr galaxy exit: BioSFHandler_GalaxyMap.CloseGalaxyMap not found";
    ue3::Reflection& r = ue3::runtime::Get();
    int called = 0;
    for (int i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !ue3::IsA(o, gGalaxyHandler) || r.NameText(o).starts_with("Default__")) continue;
        ue3::Params p(r, f);
        if (p.Valid() && ue3::runtime::Call(o, f, p)) ++called;
    }
    return std::format("mevr galaxy exit: CloseGalaxyMap called on {} handler(s)", called);
}

constexpr char kCicArt[] = "bioa_nor_110";  // the CIC's art level (its GalaxyMapProxy projector)

// `mevr galaxy place X Y Z yawDeg`: Shepard put there facing yawDeg (the controller's own ClientSetLocation: the pawn's
// SetLocation and both rotations), a test fixture's way to the CIC's console from a Normandy save on another deck.
std::string Place(ue3::Object* pc, std::istringstream& words) {
    ue3::Vector at{};
    float yawDeg = 0.0f;
    if (!(words >> at.x >> at.y >> at.z >> yawDeg)) return "usage: mevr galaxy place X Y Z yawDeg";
    ue3::Function* const f = gClientSetLocation.Get();
    if (f == nullptr || pc == nullptr) return "mevr galaxy place: Engine.Controller.ClientSetLocation not found";
    ue3::Params p(ue3::runtime::Get(), f);
    ue3::Rotator const rotation{0, static_cast<std::int32_t>(yawDeg * 65536.0f / 360.0f), 0};
    if (!p.Valid() || !p.Set("NewLocation", at) || !p.Set("NewRotation", rotation)) return "mevr galaxy place: the call's parameters";
    bool const called = ue3::runtime::Call(pc, f, p);
    write_journal::Wrote("Le2Galaxy.place", pc, 0);
    auto const now = player::Location(player::Pawn(pc));
    return std::format("mevr galaxy place: ClientSetLocation called {}: Shepard at {:.0f} {:.0f} {:.0f}", called, now ? now->x : 0.0f,
        now ? now->y : 0.0f, now ? now->z : 0.0f);
}

// The CIC's art level loaded and visible in the player's world: on another deck the projector isn't there.
bool CicArtVisible(ue3::Object* pc) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const world = pc != nullptr ? gWorldInfo.Read<ue3::Object*>(pc).value_or(nullptr) : nullptr;
    auto const list = world != nullptr && r.IsAlive(world) ? gStreamingLevels.ElementsOf(world, 4096) : std::nullopt;
    if (!list || list->stride != sizeof(ue3::Object*)) return false;
    for (int i = 0; i < list->count; ++i) {
        ue3::Object* const level = *static_cast<ue3::Object**>(list->At(i));
        if (level == nullptr || !r.IsAlive(level)) continue;
        auto const package = gPackageName.Read<ue3::Name>(level);
        if (!package) continue;
        std::string name = r.ToString(*package);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name != kCicArt) continue;
        ue3::Object* const loaded = gLoadedLevel.Read<ue3::Object*>(level).value_or(nullptr);
        return loaded != nullptr && r.IsAlive(loaded) && gIsVisible.ReadBool(level).value_or(false);
    }
    return false;
}

} // namespace

void TryStartup() {
    galaxy_projector::flow::Adapter adapter;
    adapter.name = "LE2 galaxy projector";
    adapter.roomVisible = &CicArtVisible;
    adapter.mapLevel = "bioa_nor_103agalaxymap";  // loaded but hidden on foot; stream_inGalaxyMap shows it at the console's use
    adapter.depthOfFieldOff = true;  // the map's entry with ME2's depth of field on faulted the rendering thread (2026-11-17)
    galaxy_projector::flow::TryStartup(adapter);
}

void Tick(Publish publish, std::optional<vr::floor::State> const& floor) {
    galaxy_projector::flow::Tick(publish, floor);
}

void Shutdown(bool exiting) {
    galaxy_projector::flow::Shutdown(exiting);
}

std::string StatusJson() {
    return galaxy_projector::flow::StatusJson();
}

std::string Command(ue3::Object* pc, std::string const& line) {
    std::istringstream words(line);
    std::string sub, option;
    words >> sub >> option;
    if (sub == "projector") {
        return "mevr galaxy projector: " + galaxy_projector::flow::ProjectorJson(pc);
    }
    if (sub == "capture") {
        return galaxy_projector::flow::Command(pc, option);
    }
    if (sub == "exit") {
        return Exit();
    }
    if (sub == "place") {
        std::istringstream rest(line);
        std::string skip;
        rest >> skip;
        return Place(pc, rest);
    }
    if (sub == "state" || sub.empty()) {
        return "mevr galaxy: " + galaxy_projector::flow::StatusJson() + " cic art visible " + (CicArtVisible(pc) ? "1" : "0") +
            ", game mode " + std::to_string(static_cast<int>(player::GameMode(pc).value_or(255)));
    }
    return "usage: mevr galaxy state | projector | capture <action> (on|on-post|on-default|on-ui|hold-only|off|opening-keep|opening-cut|ship|flares|state) | place X Y Z yawDeg | exit";
}

} // namespace mevr::game::le2_galaxy_capture
#endif

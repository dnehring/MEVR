// SPDX-License-Identifier: MIT
// ME3's galaxy map over the CIC's projector: ME3's adapter of the sequels' flow (game/Le3GalaxyCapture.hpp).
#include "game/Le3GalaxyCapture.hpp"
#if defined(MEVR_GAME_LE3)
#include "game/GalaxyProjectorFlow.hpp"
#include "game/Le3GalaxyRetention.hpp"

namespace mevr::game::le3_galaxy_capture {
namespace {
std::string ArmRetention(ue3::Object* pc) {
    return le3_galaxy_retention::Arm(pc, true);  // the projector's own lease: as long as the map once it opened
}
} // namespace

void TryStartup() {
    galaxy_projector::flow::Adapter adapter;
    adapter.name = "LE3 galaxy projector";
    adapter.roomVisible = &le3_galaxy_retention::CicArtVisible;  // BioA_Nor_210CIC loaded and visible
    // The CIC's art unloads on the map's entry (research/le3-galaxy.md "Bounded CIC retention candidate"): kept through it.
    adapter.armRetention = &ArmRetention;
    adapter.retentionArmed = &le3_galaxy_retention::Armed;
    adapter.retentionActive = &le3_galaxy_retention::Active;
    adapter.retentionKept = &le3_galaxy_retention::Kept;
    adapter.refreshRetention = &le3_galaxy_retention::Refresh;
    adapter.armRetentionInMap = &le3_galaxy_retention::ArmInMap;
    adapter.mapLevel = "bioa_nor_203agalaxymap";
    galaxy_projector::flow::TryStartup(adapter);
}

void Tick(Publish publish, std::optional<vr::floor::State> const& floor) {
    galaxy_projector::flow::Tick(publish, floor);
}

void Shutdown(bool exiting) {
    galaxy_projector::flow::Shutdown(exiting);
}

std::string Command(ue3::Object* pc, std::string const& action) {
    return galaxy_projector::flow::Command(pc, action);
}

std::string StatusJson() {
    return galaxy_projector::flow::StatusJson();
}

} // namespace mevr::game::le3_galaxy_capture
#else
namespace mevr::game::le3_galaxy_capture {
void TryStartup() {}
void Tick(Publish, std::optional<vr::floor::State> const&) {}
void Shutdown(bool) {}
std::string Command(ue3::Object*, std::string const&) { return "galaxy capture: LE3 only"; }
std::string StatusJson() { return "{}"; }
}
#endif

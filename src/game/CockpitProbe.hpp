// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Rigid.hpp"
#include "core/vr/CockpitPanel.hpp"
#include "core/vr/HudPieces.hpp"
#include "core/vr/MakoControls.hpp"
#include "engine/XrToUnreal.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace mevr::ue3 {
struct Object;
}

// An experiment, not a feature (research/47): can the mod, at run time and with nothing in the game's install changed, put
// a static mesh the game has already loaded into the world, move it every tick with the player or the Mako, and show a
// live SceneCapture2D render target on it, drawn with correct depth in both eyes? The answer decides whether the Mako
// cockpit is a real mesh in the world or something the host draws.
//
// Completely inert until a `mevr cockpit ...` console line (tools/console.ps1) asks for something: one early return per
// tick and no resolved handle before that. LE1 only (a message elsewhere). Everything through our reflection layer on the
// game thread; a failed lookup answers with a readable line and changes nothing. A new level forgets what it spawned (the
// old world's objects are gone), `mevr cockpit clear` and the mod's shutdown destroy it. Log prefix `cockpit probe:`.
namespace mevr::game::cockpit_probe {

// One `mevr cockpit ...` line without the two first words; what it answers goes to console.result. Game thread.
std::string Command(std::string const& line);
// Read-only attribution for `mevr reticles`: the capture-only mesh is separate from Scaleform.
std::string ReticleReport();
// `mevr vehicle [enter]`: game mode, map, the pawn, the nearest vehicle (what a console `open <map>` produced); `enter`
// calls BioEnterVehicle on the closest one. Game thread.
std::string VehicleCommand(std::string const& line);
// Registers the camera update's post-handler `ride` uses (inert until asked), before the ProcessEvent hook goes in.
bool TryStartup();
// The cockpit is riding in the Mako this tick (its camera moved): the chase camera's own rotation no longer turns the view.
bool Riding();
// While riding, with a `hud` layout: where the cockpit's displays are, into the HUD pieces the host draws (research/47).
void FillHud(vr::hud_pieces::Pieces& pieces);
// Each mod tick, late (the view anchor current). Also the cockpit by itself ([Cockpit]): in on entering the Mako, out on leaving.
// `recenters`: the host's recenter count (core/vr/Floor), if known: a change while riding takes a new seat.
void Tick(std::optional<std::uint32_t> recenters);
// Shepard in the cockpit's seat (docs/mako-cockpit-goal.md item 7): while riding with [Cockpit] seated and a `seat` layout, the
// driver is shown to the eyes and placed on the chair. Where game/arm_ik puts the legs and hands, in the world (this frame's
// cockpit). Nullopt: not seated.
struct Seat {
    ue3::Object* driver = nullptr;
    ue3::Object* mesh = nullptr;
    engine::UnrealVector knee[2]{};  // 0 left, 1 right
    engine::UnrealVector heel[2]{};
    bool held[2]{};                  // the left hand holds the lever, the right the stick; either the extraction handle (research/48 27)
    engine::UnrealVector grip[2]{};  // where those grips are now
    bool haveAxes = false;
    engine::UnrealVector axis[2]{};  // each handle's grip axis now (world, unit): a held fist wraps it
    bool pointing[2]{};              // the hand presses the repair button: its index straight out, the other fingers closed
    engine::UnrealVector eye{};      // the cockpit's eye (where the seated player's head is, before leaning)
};
std::optional<Seat> CurrentSeat();
// While riding with a `seat` layout: the cockpit model's grips (the lever at neutral, the stick upright) and the vehicle's flat
// forward, in tracking space this tick: where game/MakoControls lets the hands grab (the models are the controls). Nullopt: no
// cockpit, no view.
std::optional<vr::mako_controls::Seat> ControlSeat();
// While riding with a `panel` layout: the repair button and the extraction handle in tracking space this tick (game/CockpitPanel
// works them). Nullopt: no cockpit, no view, no layout.
std::optional<vr::cockpit_panel::Layout> PanelLayout();
// While riding: the Mako's hull health (0..1), as the hazard alarm reads it every 0.1 s.
std::optional<float> HullHealth();
// The repair button's lamps (the ring's health lamps, then the plate's squares, as `panel ... lamps` numbers them) shown in
// these colours: only what changed is swapped on Repair_Base. False: no Repair_Base part, no layout, or an instance missing.
bool ShowPanelLamps(vr::cockpit_panel::Colour const* colours, int count);
// game/arm_ik: the hips' midpoint in the driver mesh's component space (as animated), which the placement puts on the H-point.
// `head`: the head bone there too, if the skeleton has one: the placement slides the body back until it is behind the
// cockpit's eye as a head is behind its eyes (the eye was inside the back of Shepard's head, research/47).
void NoteSeatHips(ue3::Object const* mesh, ik::Vec3 const& hips, std::optional<ik::Vec3> const& head = std::nullopt);
// Snapshot JSON ("cockpitProbe").
std::string StatusJson();
// Heartbeat line (while something is up).
void LogStats();
// Mod shutdown: destroys what it spawned unless the process is exiting (then nothing is called).
void Shutdown(bool exiting);

} // namespace mevr::game::cockpit_probe

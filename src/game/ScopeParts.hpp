// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// Parts of the held gun's model hidden while its scope's picture is up (research/21 section 14): the sniper rifle's
// glowing lens ("a blue light that shines on the model itself", 2026-10-26) sits where the scope's picture is drawn.
// `[Sights] <type>Hide` names the bones (sniper default `Scope_Lens` [verify]: which bone carries the glow is a guess;
// the material log below tells). SkeletalMeshComponent.HideBone / UnHideBone / IsBoneHidden through ProcessEvent on the
// game thread (our reflection layer, never a generated wrapper). A bone the game had hidden itself is left alone; the
// sight's own bone and its ancestors are never hidden (a hidden bone collapses its children onto it [verify]).
// Everything hidden comes back when the picture goes (after 0.15 s), on a weapon switch or holster, on a settings change
// and when the mod unloads.
//
// Also logs once per weapon model its materials and, for material instances, their parameter names and values, so a
// headset snapshot can tell which one glows. LE1 only. Game thread.
namespace mevr::game::scope_parts {

// Each tick, after game/sights has the host's view: `up` the held gun's scope picture is shown, `wanted` the bones to
// hide on it ([Sights] <type>Hide), `sightBone` the bone the sight rides on (index into the model's bones), `weaponType`
// the held gun's type (vr::sights numbering) for the logs.
void Update(bool up, std::vector<std::string> const& wanted, std::optional<std::size_t> sightBone, int weaponType);
// Unhides whatever this module hid (game thread), logged with `why`.
void Restore(char const* why);
// For snapshots.
std::string StatusJson();
// Unhides (unless the process is exiting) and forgets.
void Shutdown(bool exiting);

} // namespace mevr::game::scope_parts

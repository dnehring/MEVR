// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// A dev probe for our own touch inventory (research/56 section 6; the user, 2026-11-04: "Own touch UI"): reads the game's
// inventory model through the open equipment screen's handler (BioSFHandler_Inventory.m_oInvInterface, a
// BioInventoryGuiInterface: the layer between the game's items and its Flash screen) and calls its functions, so the touch UI
// can be a new view over the game's own rules.
//   mevr inv              the handler's squad member and slot, the equipped item, and the slot's items (name, level, stats,
//                         category and manufacturer texts, new, equippable)
//   mevr inv slot <n>     SelectItemSlot(n), BuildEquippableItemList, then the list
//   mevr inv slots        every slot of lstEquipSlotOrder with its item count (the slot it was on is put back)
//   mevr inv equip <i>    Equip(i, false): the list's item i on the current squad member
// LE1, game thread; needs the equipment screen up (mevr omni equipment).
namespace mevr::game::inventory_probe {

std::string Command(std::string const& args);

} // namespace mevr::game::inventory_probe

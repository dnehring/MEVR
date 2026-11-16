// SPDX-License-Identifier: MIT
// ME2's and ME3's native side of the squad's weapon menu (docs/trilogy-parity-ledger.md Forks 5), included inside
// SquadWeapons' private namespace: the `native` adapter that SquadWeapons.cpp's one menu tick (TickCustom) calls. The tick,
// the pointing, the cards, the haptics and the logs are ME1's, shared; what ME2 and ME3 force is here: a squadmate's
// inventory is a list of weapon objects (four tiles a page, a "more weapons" tile for the rest: ReadLe2Members), the order is
// BioPlayerController.OrderWeaponSwitch with the weapon object, its outcome is the pawn's own Pawn.Weapon, and the pause is
// the main menu's owner (game/MenuPause). Until 2026-11-16 this file held a second tick (TickLe2) beside ME1's.
std::uintptr_t gLe2PendingWeapon = 0;
std::uint64_t gLe2PendingGeneration = 0;
void const* gLe2PendingWorld = nullptr;

void EquipLe2(int member, int slot) {
    gLastResult = "weapon order refused: unavailable recipient, inventory or game mode";
    if (!le2_squad::CanOrder() || member < 0 || member >= gCount || slot < 0 || slot >= kWeapons) return;
    auto const& m = gMembers[member];
    auto* pawn = reinterpret_cast<ue3::Object*>(m.key);
    auto* weapon = reinterpret_cast<ue3::Object*>(m.weapons[slot]);
    auto inventory = le2_squad::Weapons(pawn);
    auto* fn = le2_squad::orderWeapon.Get();
    if (!le2_squad::RecipientReady(pawn) || fn == nullptr || std::find(inventory.begin(), inventory.end(), weapon) == inventory.end()) return;
    ue3::Params args(ue3::runtime::Get(), fn);
    if (!args.Set("oOrderedPawn", pawn) || !args.Set("Wpn", weapon) || !le2_squad::BindInputMethod(args) || !ue3::runtime::Call(player::Controller(), fn, args)) return;
    gLastUsed = m.key;
    gPending = {m.key, slot, 0.0, m.name};
    gLe2PendingWeapon = m.weapons[slot];
    gLe2PendingGeneration = squad_context::Generation();
    gLe2PendingWorld = engine::World();
    gLastResult = std::format("{}: {} order dispatched; waiting for equipped weapon", m.name, le2_squad::WeaponLabel(m.typeOfSlot[slot]));
    log::Info("squad: {}", gLastResult);
}

namespace native {
struct Squad {};  // the roster is gMembers itself: nothing else to carry from Read to Equip

std::optional<Squad> Read(double, bool busy, bool newPull, std::string& why) {
    // Tick has aged and, every half second, re-read the roster already (other modules read it through Members()); while the
    // menu is busy, on a new pull or with an order's outcome pending it is read afresh every tick, as ME1's.
    if (busy || newPull || gPending.key != 0) {
        gDetailsAge = 1e9;
        ReadLe2Members(0.0, head_tracking::GameMode());
    }
    if (!le2_squad::CanOrder()) {
        why = "the game takes no squad orders now (the layer, the player's pawn, or combat is off here)";
        return std::nullopt;
    }
    return Squad{};
}

void Pause(Squad const&) {
    // The main menu's pause owner (game/MenuPause); Mod.cpp's link keeps it while the menu is open.
    menu_pause::SetSquadPause(true);
    gPausedByUs = true;
}

void Unpause(Squad const&) {
    // Always, before the order goes out: closing restores the prior state before the game's own equip admission.
    menu_pause::SetSquadPause(false);
    gPausedByUs = false;
}

void Stop(char const*) {
    menu_pause::SetSquadPause(false);
    gPausedByUs = false;
    gPending = {};
    gLe2PendingWeapon = 0;
}

void Equip(Squad const&, int member, int tile) { EquipLe2(member, tile); }

// The order's outcome: the pawn's own equipped weapon, while the scene and the recipient stay the same.
void Follow(double deltaSeconds) {
    if (gPending.key != 0 && (gLe2PendingWorld != engine::World() ||
        gLe2PendingGeneration != squad_context::Generation() || head_tracking::GameMode() != 0 ||
        std::none_of(gMembers.begin(), gMembers.begin() + gCount, [](auto const& m) { return m.key == gPending.key && m.name == gPending.who; }))) {
        gLastResult = "pending weapon observation cancelled: scene or recipient changed";
        gPending = {}; gLe2PendingWeapon = 0;
    }
    if (gPending.key != 0) {
        auto* pawn = reinterpret_cast<ue3::Object*>(gPending.key);
        auto* held = ue3::runtime::IsAlive(pawn) ? le2_squad::equippedWeapon.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
        gPending.age += deltaSeconds;
        if (reinterpret_cast<std::uintptr_t>(held) == gLe2PendingWeapon) {
            gLastResult = std::format("{}: requested weapon observed equipped", gPending.who);
            log::Info("squad: {}", gLastResult); gPending = {};
        } else if (gPending.age > kResultSeconds || !ue3::runtime::IsAlive(pawn)) {
            gLastResult = "weapon order was dispatched, but requested weapon was not observed equipped";
            log::Warn("squad: {}", gLastResult); gPending = {};
        }
    }
}

// A tile is a slot of the member's page: the weapon's type in words, or the navigation tile.
std::string TileName(MemberInfo const& member, int tile) {
    if (tile < 0 || tile >= kWeapons) return "none";
    if (tile == member.page.more) return std::format("more weapons (page {} of {})", member.page.index + 1, member.page.pages);
    std::string label = le2_squad::WeaponLabel(member.typeOfSlot[tile]);
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return label;
}

void NextPage(int member) {
    if (member < 0 || member >= gCount) return;
    auto const& m = gMembers[member];
    gLe2Pages[m.key] = (m.page.index + 1) % m.page.pages;
    gDetailsAge = 1e9;
    ReadLe2Members(0.0, head_tracking::GameMode());
}
}  // namespace native

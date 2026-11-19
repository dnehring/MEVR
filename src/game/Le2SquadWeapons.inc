// SPDX-License-Identifier: MIT
// ME2's and ME3's side of the squad's weapons (docs/trilogy-parity-ledger.md G30), included inside SquadWeapons' private
// namespace. On the sequels the left trigger is the game's own LB whatever the left hand does: its hold opens the game's
// weapon wheel, which switches the squad's weapons by the game's own rules (the owner, 2026-11-18: our drawn menu at the hand,
// this file's `native` adapter until then, is gone). What is left is the squad orders' switch (GiveWeapon: "Miranda, your
// pistol"): a squadmate's inventory is a list of weapon objects (ReadLe2Members), the order is
// BioPlayerController.OrderWeaponSwitch with the weapon object, its outcome is the pawn's own Pawn.Weapon.
std::uintptr_t gLe2PendingWeapon = 0;
std::uint64_t gLe2PendingGeneration = 0;
void const* gLe2PendingWorld = nullptr;

void EquipLe2(int member, ue3::Object* weapon) {
    gLastResult = "weapon order refused: unavailable recipient, inventory or game mode";
    if (!le2_squad::CanOrder() || member < 0 || member >= gCount || weapon == nullptr) return;
    auto const& m = gMembers[member];
    auto* pawn = reinterpret_cast<ue3::Object*>(m.key);
    auto inventory = le2_squad::Weapons(pawn);
    auto* fn = le2_squad::orderWeapon.Get();
    if (!le2_squad::RecipientReady(pawn) || fn == nullptr || std::find(inventory.begin(), inventory.end(), weapon) == inventory.end()) return;
    ue3::Params args(ue3::runtime::Get(), fn);
    if (!args.Set("oOrderedPawn", pawn) || !args.Set("Wpn", weapon) || !le2_squad::BindInputMethod(args) || !ue3::runtime::Call(player::Controller(), fn, args)) return;
    int const type = le2_squad::WeaponType(weapon);
    gLastUsed = m.key;
    gPending = {m.key, type, 0.0, m.name};
    gLe2PendingWeapon = reinterpret_cast<std::uintptr_t>(weapon);
    gLe2PendingGeneration = squad_context::Generation();
    gLe2PendingWorld = engine::World();
    gLastResult = std::format("{}: {} order dispatched; waiting for equipped weapon", m.name, le2_squad::WeaponLabel(type));
    log::Info("squad: {}", gLastResult);
}

// The order's outcome: the pawn's own equipped weapon, while the scene and the recipient stay the same.
void FollowLe2(double deltaSeconds) {
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

// SPDX-License-Identifier: MIT
// Included inside SquadWeapons' private namespace; reuses ME1's menu machine and host view.
std::array<std::array<std::uintptr_t, kWeapons>, kMaxMembers> gLe2OpenWeapons{};
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
    if (!le2_squad::Living(pawn) || fn == nullptr || std::find(inventory.begin(), inventory.end(), weapon) == inventory.end()) return;
    ue3::Params args(ue3::runtime::Get(), fn);
    if (!args.Set("oOrderedPawn", pawn) || !args.Set("Wpn", weapon) || !ue3::runtime::Call(player::Controller(), fn, args)) return;
    gLastUsed = m.key;
    gPending = {m.key, slot, 0.0, m.name};
    gLe2PendingWeapon = m.weapons[slot];
    gLe2PendingGeneration = squad_context::Generation();
    gLe2PendingWorld = engine::World();
    gLastResult = std::format("{}: {} order dispatched; waiting for equipped weapon", m.name, le2_squad::WeaponLabel(m.typeOfSlot[slot]));
    log::Info("squad: {}", gLastResult);
}

void TickLe2(double dt, Link const& link, hand_calibration::Squad const& settings,
    vr::controllers::State const& controllers, bool newPull) {
    gStyleNative = false; // ME2's native wheel is a different UI; reuse our custom hand menu.
    auto const frame = view_anchor::Current();
    bool const busy = gMachine.IsOpen();
    if (busy || newPull || gPending.key != 0) {
        gDetailsAge = 1e9;
        ReadLe2Members(0.0, head_tracking::GameMode());
    }
    bool same = !busy || gCount == gOpenCount;
    for (int i = 0; busy && same && i < gCount; ++i)
        same = gMembers[i].key == gOpenKeys[i] && gMembers[i].weapons == gLe2OpenWeapons[i];
    gUsable = gStarted && same && le2_squad::CanOrder() && frame.has_value() && vr::host_view::Latest().valid &&
        controllers.active && controllers.left.aimValid && !link.menuOpen && !power_wheel::OwnsLeftHand() && !support_grip::Held();
    int pointed = -1, preferred = 0;
    if (frame && controllers.left.aimValid) {
        // Do the angular test in world coordinates, avoiding a second quaternion implementation.
        auto const origin = engine::ToWorld(*frame, controllers.left.aim.position);
        auto const direction = engine::OrientationToWorld(*frame, controllers.left.aim.orientation).forward;
        float best = static_cast<float>(settings.pointConeDeg) * kDeg;
        for (int i = 0; i < gCount; ++i) {
            if (gMembers[i].key == gLastUsed) preferred = i;
            auto const p = player::Location(reinterpret_cast<ue3::Object*>(gMembers[i].key));
            if (!p) continue;
            double const dx = p->x - origin.x, dy = p->y - origin.y, dz = p->z + kChestUnits - origin.z;
            double const distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            double const upm = frame->unitsPerMeter > 0.0 ? frame->unitsPerMeter : engine::kUnitsPerMeter;
            if (distance < 1e-3 || distance > settings.pointMaxM * upm) continue;
            float const angle = static_cast<float>(std::acos(std::clamp((dx * direction.x + dy * direction.y + dz * direction.z) / distance, -1.0, 1.0)));
            if (angle < best) { best = angle; pointed = i; }
        }
    }
    gCardOrder = {0, 1};
    menu::Input in;
    in.dt = static_cast<float>(dt);
    in.usable = gUsable;
    in.triggerDown = gTrigger;
    in.newPull = newPull;
    in.pointed = pointed;
    in.preferred = preferred;
    in.memberCount = gCount;
    for (int i = 0; i < gCount; ++i) in.members[i] = {gMembers[i].carried, gMembers[i].current};
    in.leftX = controllers.left.stickX; in.leftY = controllers.left.stickY;
    in.rightX = controllers.right.stickX; in.rightY = controllers.right.stickY;
    in.a = controllers.right.primary; in.b = controllers.right.secondary;
    in.confirmOnRelease = settings.confirmOnRelease;
    auto const step = gMachine.Update(in);
    // Use the main menu's pause owner; closing restores its prior state before native equip admission.
    menu_pause::SetSquadPause(gMachine.IsOpen() && settings.pause);
    if (step.opened) {
        ++gSerial; ++gOpens; gOpenCount = gCount;
        for (int i = 0; i < gCount; ++i) { gOpenKeys[i] = gMembers[i].key; gLe2OpenWeapons[i] = gMembers[i].weapons; }
        gPointedCard = pointed;
    }
    if (step.refused || step.tileRefused) ++gRefusals;
    if (step.closed == menu::Close::Confirmed) {
        ++gConfirms;
        EquipLe2(step.member, step.weapon);
    } else if (step.closed != menu::Close::None) ++gCancels;
    if (link.haptic != nullptr && (step.opened || step.cardChanged || step.tileChanged || step.closed != menu::Close::None || step.refused || step.tileRefused))
        link.haptic(step.tileChanged ? 1 : 0, step.refused || step.tileRefused ? 0.08f : 0.02f,
            static_cast<float>(settings.hapticAmplitude));
    if (gPending.key != 0 && (gLe2PendingWorld != engine::World() ||
        gLe2PendingGeneration != squad_context::Generation() || head_tracking::GameMode() != 0 ||
        std::none_of(gMembers.begin(), gMembers.begin() + gCount, [](auto const& m) { return m.key == gPending.key && m.name == gPending.who; }))) {
        gLastResult = "pending weapon observation cancelled: scene or recipient changed";
        gPending = {}; gLe2PendingWeapon = 0;
    }
    if (gPending.key != 0) {
        auto* pawn = reinterpret_cast<ue3::Object*>(gPending.key);
        auto* held = ue3::runtime::IsAlive(pawn) ? le2_squad::equippedWeapon.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
        gPending.age += dt;
        if (reinterpret_cast<std::uintptr_t>(held) == gLe2PendingWeapon) {
            gLastResult = std::format("{}: requested weapon observed equipped", gPending.who);
            log::Info("squad: {}", gLastResult); gPending = {};
        } else if (gPending.age > kResultSeconds || !ue3::runtime::IsAlive(pawn)) {
            gLastResult = "weapon order was dispatched, but requested weapon was not observed equipped";
            log::Warn("squad: {}", gLastResult); gPending = {};
        }
    }
    SetWhy(gMachine.IsOpen() ? "ME2 custom menu open" : gUsable ? "ME2 custom menu ready" : "ME2 menu unavailable (mode, tracking, input owner or roster)");
    Publish(link, gMachine.IsOpen());
}

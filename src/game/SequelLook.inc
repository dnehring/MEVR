// SPDX-License-Identifier: MIT
// The sequels' look-at chain, each game's adapter for game/SequelConversationWorld (included there, inside its anonymous
// namespace): whom a person looks at, at which rank, where its chain aims, and the game's own call to give it another target.
// ME2's chain is ME1's (BioPawn.m_oLookAtData, a BioLookAt with m_oTarget and m_nRank; BioPawn.SetLookAtTarget). ME3 has neither:
// its look-at is a module on the actor (Engine.Actor.Modules holds an SFXGame.SFXModule_LookAt: m_pTarget, m_nRank, and the native
// ChangeTarget(Target, a_eTransition, nRank); research/le3-conversations-in-world.md 2.4). The chains' last link aims at
// SkelControlLookAt.TargetLocation on both (BioPawnLookAtController and BioActorLookAtController derive from it).

struct LookNow {
    bool known = false;                 // the person has a look-at chain we can read
    ue3::Object* target = nullptr;      // its target (nullptr: none)
    std::int32_t rank = 0;
    std::optional<ue3::Vector> aim;     // where the chain's last link aims, in the world
};

ue3::FieldRef gLookTargetLocation{"Engine.SkelControlLookAt", "TargetLocation", ue3::kEveryGame};

#if defined(MEVR_GAME_LE3)
ue3::FieldRef gActorModules{"Engine.Actor", "Modules", ue3::kEveryGame};
ue3::ClassRef gLookModuleClass{"SFXGame.SFXModule_LookAt", ue3::kEveryGame};
ue3::FieldRef gLookModuleTarget{"SFXGame.SFXModule_LookAt", "m_pTarget", ue3::kEveryGame};
ue3::FieldRef gLookModuleRank{"SFXGame.SFXModule_LookAt", "m_nRank", ue3::kEveryGame};
ue3::FieldRef gLookModuleControllers{"SFXGame.SFXModule_LookAt", "m_Controllers", ue3::kEveryGame};
ue3::FunctionRef gLookChangeTarget{"SFXGame.SFXModule_LookAt.ChangeTarget", ue3::kEveryGame};

// The actor's look-at module (its Modules array holds at most a few), or nullptr.
ue3::Object* LookHolder(ue3::Object* actor) {
    if (actor == nullptr || !ue3::runtime::IsAlive(actor)) return nullptr;
    auto const modules = gActorModules.ElementsOf(actor, 32);
    if (!modules || modules->stride != static_cast<std::int32_t>(sizeof(void*))) return nullptr;
    for (std::int32_t i = 0; i < modules->count; ++i) {
        ue3::Object* const m = *static_cast<ue3::Object**>(modules->At(i));
        if (m != nullptr && ue3::runtime::IsAlive(m) && ue3::IsA(m, gLookModuleClass)) return m;
    }
    return nullptr;
}
ue3::FieldRef& LookTargetField() { return gLookModuleTarget; }
ue3::FieldRef& LookRankField() { return gLookModuleRank; }
ue3::FieldRef& LookControllersField() { return gLookModuleControllers; }

// The game's own call: the module's ChangeTarget at a rank (its transition 0, the game's default).
bool SetLookRanked(ue3::Object* actor, ue3::Object* target, std::int32_t rank) {
    ue3::Object* const holder = LookHolder(actor);
    ue3::Function* const fn = gLookChangeTarget.Get();
    if (holder == nullptr || fn == nullptr) return false;
    ue3::Params p(ue3::runtime::Get(), fn);
    if (!p.Valid() || !p.Set("Target", target) || !p.Set("a_eTransition", std::uint8_t{0}) || !p.Set("nRank", rank)) return false;
    return ue3::runtime::Call(holder, fn, p);
}
#else
ue3::FieldRef gPawnLookData{"SFXGame.BioPawn", "m_oLookAtData", ue3::kEveryGame};
ue3::FieldRef gLookDataTarget{"SFXGame.BioLookAt", "m_oTarget", ue3::kEveryGame};
ue3::FieldRef gLookDataRank{"SFXGame.BioLookAt", "m_nRank", ue3::kEveryGame};
ue3::FieldRef gLookDataControllers{"SFXGame.BioLookAt", "m_Controllers", ue3::kEveryGame};
ue3::FunctionRef gSetLookAt{"SFXGame.BioPawn.SetLookAtTarget", ue3::kEveryGame};

// The pawn's BioLookAt (ME1's), or nullptr.
ue3::Object* LookHolder(ue3::Object* actor) {
    if (actor == nullptr || !ue3::runtime::IsAlive(actor) || !player::IsBioPawn(actor)) return nullptr;
    ue3::Object* const data = gPawnLookData.Read<ue3::Object*>(actor).value_or(nullptr);
    return data != nullptr && ue3::runtime::IsAlive(data) ? data : nullptr;
}
ue3::FieldRef& LookTargetField() { return gLookDataTarget; }
ue3::FieldRef& LookRankField() { return gLookDataRank; }
ue3::FieldRef& LookControllersField() { return gLookDataControllers; }

// The game's own call (ME1's): BioPawn.SetLookAtTarget at a rank.
bool SetLookRanked(ue3::Object* actor, ue3::Object* target, std::int32_t rank) {
    ue3::Function* const fn = gSetLookAt.Get();
    if (actor == nullptr || !ue3::runtime::IsAlive(actor) || !player::IsBioPawn(actor) || fn == nullptr) return false;
    ue3::Params p(ue3::runtime::Get(), fn);
    if (!p.Valid() || !p.Set("oTarget", target) || !p.Set("a_eTransition", std::uint8_t{0}) || !p.Set("nRank", rank)) return false;
    return ue3::runtime::Call(actor, fn, p);
}
#endif

// What a person looks at now (both games).
LookNow LookOf(ue3::Object* actor) {
    LookNow out;
    ue3::Object* const holder = LookHolder(actor);
    if (holder == nullptr) return out;
    out.known = true;
    out.target = LookTargetField().Read<ue3::Object*>(holder).value_or(nullptr);
    out.rank = LookRankField().Read<std::int32_t>(holder).value_or(0);
    if (auto const links = LookControllersField().ElementsOf(holder, 32);
        links && links->count > 0 && links->stride == static_cast<std::int32_t>(sizeof(void*))) {
        ue3::Object* const last = *static_cast<ue3::Object**>(links->At(links->count - 1));
        if (last != nullptr && ue3::runtime::IsAlive(last)) out.aim = gLookTargetLocation.Read<ue3::Vector>(last);
    }
    return out;
}

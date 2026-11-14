// SPDX-License-Identifier: MIT
// Included once to reuse existing guarded roots, membership and class checks.
namespace {
static_assert(offsetof(APawn, Weapon)==0x480);
static_assert(offsetof(ASFXWeapon, MagSize)==0x400 && offsetof(FScaledFloat, Value)==0x20);
static_assert(offsetof(ASFXWeapon, CurrentSpareAmmo)==0x908 && offsetof(ASFXWeapon, AmmoUsedCount)==0x90C);
struct WeaponSample {
    ResourceContext roots;
    ObjectSample weapon;
    std::uintptr_t objectTable{}, pending{};
    unsigned objectCount{}, objectCapacity{};
    std::uint64_t nameBits{};
    int nameNumber{};
    char name[kNameBytes]{};
    bool available{}, heavy{};
    char const* reason{"selected-weapon-unavailable"};
};
struct WeaponValue { std::uintptr_t weapon{}; unsigned field{}; std::uint32_t bits{}; };
constexpr unsigned kWeaponFields=9;
#if defined(MEVR_LE2_OBSERVATION_TEST)
void (*gWeaponSampleHook)(){};
#endif
void ReadWeapon(void* context) {
    auto& s=*static_cast<WeaponSample*>(context); ReadResourceContext(&s.roots);
    if(!s.roots.available) { s.reason=s.roots.reason; return; }
    auto const* objects=UObject::GObjObjects;
    s.reason="weapon-object-table-invalid";
    if(!objects || !BoundedArray(*objects,kMaxObjects) || objects->Count()!=s.roots.root.objectCount) return;
    s.objectTable=reinterpret_cast<std::uintptr_t>(objects->GetData()); s.objectCount=objects->Count(); s.objectCapacity=objects->Capacity();
    Table table{objects->GetData(),objects->Count()};
    auto const* pawn=reinterpret_cast<APawn const*>(s.roots.root.pawn.address);
    auto const* inventory=reinterpret_cast<ASFXInventoryManager const*>(s.roots.inventory.address);
    auto const* weapon=static_cast<ASFXWeapon const*>(pawn->Weapon);
    s.reason="selected-pawn-weapon-not-live-SFXWeapon";
    if(!ReadObject(table,weapon,"SFXGame","SFXWeapon",s.weapon)) return;
    s.reason="weapon-instigator-inventory-or-world-disagrees";
    if(weapon->Instigator!=pawn || weapon->InvManager!=inventory || weapon->WorldInfo!=pawn->WorldInfo) return;
    // Runtime instances can legitimately have numbered FNames. Read only the
    // base spelling through the existing bounded reader; retain exact identity.
    auto baseName=weapon->Name; std::memcpy(&s.nameBits,&baseName,sizeof(baseName));
    s.nameNumber=baseName.Number; baseName.Number=0;
    s.reason="weapon-name-unavailable"; if(!NameText(baseName,s.name)) return;
    s.heavy=IsA(table,weapon,"SFXGame","SFXHeavyWeapon");
    s.pending=reinterpret_cast<std::uintptr_t>(inventory->PendingWeapon);
    s.available=true; s.reason="validated-pawn-selected-weapon";
}
void ReadWeaponValue(void* context) {
    auto& v=*static_cast<WeaponValue*>(context); auto const* w=reinterpret_cast<ASFXWeapon const*>(v.weapon);
    switch(v.field) {
    case 0: v.bits=std::bit_cast<std::uint32_t>(w->MagSize.Value); break;
    case 1: v.bits=std::bit_cast<std::uint32_t>(w->AmmoUsedCount); break;
    case 2: v.bits=std::bit_cast<std::uint32_t>(w->CurrentSpareAmmo); break;
    case 3: v.bits=w->bInfiniteAmmo; break;
    case 4: v.bits=w->bWeaponCanBeReloaded; break;
    case 5: v.bits=w->bEarlyReload; break;
    case 6: v.bits=w->bDrawingWeapon; break;
    case 7: v.bits=w->bHolsteringWeapon; break;
    case 8: v.bits=w->bPlayerUsable; break;
    }
}
bool SameWeapon(WeaponSample const& a,WeaponSample const& b) {
    return a.available && b.available && SameResourceContext(a.roots,b.roots) && SameObjectSample(a.weapon,b.weapon) &&
        a.objectTable==b.objectTable && a.objectCount==b.objectCount && a.objectCapacity==b.objectCapacity &&
        a.pending==b.pending && a.heavy==b.heavy && a.nameBits==b.nameBits && std::strcmp(a.name,b.name)==0;
}
json::Value WeaponIdentity(ObjectSample const& sample) {
    auto out=json::Value::Object(); out["class"]=sample.className; out["index"]=sample.index; return out;
}
}
#if defined(MEVR_LE2_OBSERVATION_TEST)
void testing::SetWeaponSampleHook(void (*hook)()) { gWeaponSampleHook=hook; }
#endif
json::Value ObserveWeapon(std::uint32_t thread) {
    auto out=json::Value::Object(); out["schemaVersion"]=1; out["game"]="LE2"; out["available"]=false;
    out["dispatchCapability"]=false; out["reloadEligibilityVerified"]=false; out["reloadCompletionVerified"]=false;
    out["persistenceVerified"]=false; out["durableObjectIdentity"]=false; out["coherentSimulationSnapshot"]=false;
    auto refuse=[&](char const* reason) { out["reason"]=reason; return out; };
    if(thread==0 || thread!=GetCurrentThreadId()) return refuse("not-on-verified-game-thread");
    WeaponSample first{},after{};
    if(!ue3::runtime::Guarded(ReadWeapon,&first)) return refuse("guarded-weapon-context-read-fault");
    if(!first.available) return refuse(first.reason);
    WeaponValue values[2][kWeaponFields]{}; bool read[2][kWeaponFields]{};
    for(unsigned pass=0;pass<2;++pass) {
#if defined(MEVR_LE2_OBSERVATION_TEST)
        if(pass==1 && gWeaponSampleHook) gWeaponSampleHook();
#endif
        for(unsigned i=0;i<kWeaponFields;++i) { values[pass][i]={first.weapon.address,i}; read[pass][i]=ue3::runtime::Guarded(ReadWeaponValue,&values[pass][i]); }
    }
    if(!ue3::runtime::Guarded(ReadWeapon,&after)) return refuse("guarded-weapon-context-reread-fault");
    if(!SameWeapon(first,after)) return refuse("weapon-context-changed");
    out["available"]=true; out["reason"]=first.reason;
    auto context=json::Value::Object(); context["world"]=WeaponIdentity(first.roots.root.world);
    context["controller"]=WeaponIdentity(first.roots.root.player); context["pawn"]=WeaponIdentity(first.roots.root.pawn);
    context["inventory"]=WeaponIdentity(first.roots.inventory); context["pendingWeaponPresent"]=first.pending!=0;
    context["source"]="current pawn.Weapon; reciprocal Instigator/InvManager/WorldInfo"; out["context"]=std::move(context);
    auto weapon=WeaponIdentity(first.weapon); weapon["name"]=first.name; weapon["nameNumber"]=first.nameNumber;
    weapon["heavyWeaponClass"]=first.heavy; out["selectedWeapon"]=std::move(weapon);
    char const* keys[kWeaponFields]={"magazineCapacity","magazineUsed","reserveStored","infiniteAmmo","weaponCanBeReloaded","earlyReload","drawingWeapon","holsteringWeapon","playerUsable"};
    char const* sources[kWeaponFields]={"MagSize.Value","AmmoUsedCount","CurrentSpareAmmo","bInfiniteAmmo","bWeaponCanBeReloaded","bEarlyReload","bDrawingWeapon","bHolsteringWeapon","bPlayerUsable"};
    bool valid[kWeaponFields]{}; auto fields=json::Value::Object();
    for(unsigned i=0;i<kWeaponFields;++i) {
        auto f=json::Value::Object(); f["source"]=sources[i];
        char const* why=nullptr;
        if(!read[0][i] || !read[1][i]) why="guarded-weapon-field-read-fault";
        else if(values[0][i].bits!=values[1][i].bits) why="weapon-field-changed";
        else if(i<2 && !std::isfinite(std::bit_cast<float>(values[0][i].bits))) why="nonfinite-weapon-field";
        valid[i]=why==nullptr; f["available"]=valid[i]; f["reason"]=why ? why : "stable-raw-field";
        if(valid[i]) {
            if(i<2) f["value"]=static_cast<double>(std::bit_cast<float>(values[0][i].bits));
            else if(i==2) f["value"]=std::bit_cast<std::int32_t>(values[0][i].bits);
            else f["value"]=values[0][i].bits!=0;
        }
        fields[keys[i]]=std::move(f);
    }
    auto loaded=json::Value::Object(); loaded["available"]=false; loaded["source"]="final SFXWeapon.GetAmmoCountInMagazine: int(MagSize.Value-AmmoUsedCount)";
    loaded["reason"]="capacity-or-used-unavailable-or-out-of-range";
    float const capacity=std::bit_cast<float>(values[0][0].bits), used=std::bit_cast<float>(values[0][1].bits);
    if(valid[0] && valid[1] && capacity>=0 && used>=0 && used<=capacity) {
        float const remaining=capacity-used;
        if(std::isfinite(remaining) && static_cast<double>(remaining)<2147483648.0) {
            loaded["available"]=true; loaded["reason"]="source-derived-truncation-toward-zero"; loaded["value"]=static_cast<std::int32_t>(remaining);
        }
    }
    fields["loadedCount"]=std::move(loaded);
    auto reserve=json::Value::Object(); reserve["available"]=false;
    reserve["reason"]="effective-GetCurrentSpareAmmo-override-not-resolved; use-raw-reserveStored-only";
    fields["effectiveReserve"]=std::move(reserve); out["fields"]=std::move(fields);
    return out;
}

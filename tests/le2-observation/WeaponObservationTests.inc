// SPDX-License-Identifier: MIT
// Synthetic SDK layout tests, not live game evidence.
{
    using mevr::engine::le2_diagnostic::ObserveWeapon;
    using mevr::engine::le2_diagnostic::testing::SetWeaponSampleHook;
    Raw<UClass> weaponType, rifleType, heavyType;
    add(weaponType.get(),"SFXWeapon",meta.get(),sfxPackage.get());
    add(rifleType.get(),"TestRifle",meta.get(),sfxPackage.get()); rifleType.get()->SuperField=weaponType.get();
    add(heavyType.get(),"SFXHeavyWeapon",meta.get(),sfxPackage.get()); heavyType.get()->SuperField=weaponType.get();
    Raw<ASFXWeapon> gun, other;
    add(gun.get(),"SelectedRifle",rifleType.get(),nullptr); add(other.get(),"OtherRifle",rifleType.get(),nullptr);
    auto resetGun=[&] {
        resourcePawn.get()->Weapon=gun.get(); gun.get()->Class=rifleType.get(); gun.get()->ObjectFlags=0;
        gun.get()->Instigator=resourcePawn.get(); gun.get()->InvManager=inventory.get(); gun.get()->WorldInfo=menuWorldInfo.get();
        gun.get()->MagSize.Value=12; gun.get()->AmmoUsedCount=4; gun.get()->CurrentSpareAmmo=23;
        gun.get()->bInfiniteAmmo=0; gun.get()->bWeaponCanBeReloaded=1; gun.get()->bEarlyReload=0;
        gun.get()->bDrawingWeapon=0; gun.get()->bHolsteringWeapon=0; gun.get()->bPlayerUsable=1;
        inventory.get()->PendingWeapon=nullptr;
    };
    resetGun();
    auto observe=[&] {return ObserveWeapon(thread);};
    auto weaponFields=[&] {return observe().Get("fields");};
    auto weaponResult=observe();
    Check(weaponResult.Bool("available") && weaponResult.Get("selectedWeapon").String("name")=="SelectedRifle", "selected current pawn weapon identity");
    Check(!weaponResult.Get("selectedWeapon").Has("address") && !weaponResult.Bool("durableObjectIdentity"), "weapon diagnostic identity has no wire pointer or generation promise");
    gun.get()->Name.Number=23;
    Check(observe().Bool("available") && observe().Get("selectedWeapon").Number("nameNumber")==23,"numbered runtime object name accepted without guessing rendered suffix");
    gun.get()->Name.Number=0;
    Check(weaponFields().Get("magazineCapacity").Number("value")==12 && weaponFields().Get("magazineUsed").Number("value")==4 &&
        weaponFields().Get("loadedCount").Number("value")==8 && weaponFields().Get("reserveStored").Number("value")==23,"used, loaded and reserve remain distinct");
    Check(!weaponResult.Bool("dispatchCapability") && !weaponResult.Bool("reloadEligibilityVerified") && !weaponResult.Bool("reloadCompletionVerified") &&
        !weaponResult.Bool("persistenceVerified") && !weaponResult.Bool("coherentSimulationSnapshot"),"sampling does not invent gameplay proof");
    gun.get()->MagSize.Value=12.75f; gun.get()->AmmoUsedCount=4.5f;
    Check(weaponFields().Get("loadedCount").Number("value")==8,"fractional positive difference truncates, never rounds");
    gun.get()->AmmoUsedCount=12.75f; Check(weaponFields().Get("loadedCount").Number("value")==0,"empty magazine is a real zero");
    gun.get()->AmmoUsedCount=13; Check(!weaponFields().Get("loadedCount").Bool("available") && weaponFields().Get("magazineUsed").Number("value")==13,"raw overused count preserved, derived negative loaded unavailable");
    gun.get()->AmmoUsedCount=-1; Check(!weaponFields().Get("loadedCount").Has("value") && weaponFields().Get("magazineUsed").Number("value")==-1,"negative raw count is not silently clamped");
    resetGun(); gun.get()->MagSize.Value=2147483648.0f; gun.get()->AmmoUsedCount=0;
    Check(!weaponFields().Get("loadedCount").Has("value"),"float-to-int upper overflow refused");
    gun.get()->MagSize.Value=2147483520.0f;
    Check(weaponFields().Get("loadedCount").Number("value")==2147483520.0,"largest representable float below int limit converts safely");
    gun.get()->MagSize.Value=std::numeric_limits<float>::infinity();
    Check(!weaponFields().Get("magazineCapacity").Has("value") && !weaponFields().Get("loadedCount").Has("value"),"infinite capacity omitted from JSON and derivation");
    resetGun(); gun.get()->AmmoUsedCount=std::numeric_limits<float>::quiet_NaN();
    Check(!weaponFields().Get("magazineUsed").Has("value") && weaponFields().Get("reserveStored").Bool("available"),"nonfinite used unavailable independently");
    resetGun(); gun.get()->CurrentSpareAmmo=-7;
    Check(weaponFields().Get("reserveStored").Number("value")==-7 && !weaponFields().Get("effectiveReserve").Has("value"),"signed raw reserve retained, effective semantics not guessed");
    gun.get()->Class=heavyType.get(); gun.get()->bInfiniteAmmo=1; gun.get()->bEarlyReload=1;
    Check(observe().Get("selectedWeapon").Bool("heavyWeaponClass") && !weaponFields().Get("effectiveReserve").Bool("available") &&
        weaponFields().Get("infiniteAmmo").Bool("value") && weaponFields().Get("earlyReload").Bool("value"),"heavy reserve override and infinite/early flags do not become transfer proof");
    resetGun(); resourcePawn.get()->Weapon=nullptr; Check(!observe().Bool("available"),"missing selected weapon refused");
    resetGun(); resourcePawn.get()->Weapon=reinterpret_cast<AWeapon*>(1); Check(!observe().Bool("available"),"bad selected pointer fails safely inside guarded reader");
    resetGun(); gun.get()->Class=pawnType.get(); Check(!observe().Bool("available"),"wrong selected class refused");
    resetGun(); gun.get()->ObjectFlags=0x200; Check(!observe().Bool("available"),"template weapon refused");
    resetGun(); gun.get()->Instigator=pawn.get(); Check(!observe().Bool("available"),"foreign weapon instigator refused");
    resetGun(); gun.get()->InvManager=nullptr; Check(!observe().Bool("available"),"weapon inventory reciprocal required");
    resetGun(); gun.get()->WorldInfo=nullptr; Check(!observe().Bool("available"),"weapon world reciprocal required");
    resetGun();
    static ASFXWeapon* sampledGun{}; static APawn* sampledPawn{}; static ASFXInventoryManager* sampledInventory{}; static unsigned mutation{};
    sampledGun=gun.get(); sampledPawn=resourcePawn.get(); sampledInventory=inventory.get();
    SetWeaponSampleHook(+[] {
        if(mutation==1) sampledGun->AmmoUsedCount+=1;
        if(mutation==2) sampledPawn->Weapon=nullptr;
        if(mutation==3) sampledGun->bInfiniteAmmo=1;
        if(mutation==4) sampledInventory->PendingWeapon=sampledGun;
    });
    mutation=1; weaponResult=observe();
    Check(weaponResult.Bool("available") && !weaponResult.Get("fields").Get("magazineUsed").Has("value") && !weaponResult.Get("fields").Get("loadedCount").Has("value") &&
        weaponResult.Get("fields").Get("reserveStored").Number("value")==23,"changing used invalidates derived count without hiding stable reserve");
    resetGun(); mutation=2; Check(observe().String("reason")=="weapon-context-changed","selected weapon switch invalidates entire observation");
    resetGun(); mutation=3; Check(!weaponFields().Get("infiniteAmmo").Has("value"),"changing flag unavailable independently");
    resetGun(); mutation=4; Check(!observe().Bool("available"),"pending selection change invalidates sample");
    SetWeaponSampleHook(nullptr); resetGun();
    // Readable identity/capacity with unreadable ammo/flags: a field fault must
    // not be turned into zero or invalidate unrelated readable scalar values.
    SYSTEM_INFO weaponSystemInfo{}; GetSystemInfo(&weaponSystemInfo);
    auto* weaponPages=static_cast<unsigned char*>(VirtualAlloc(nullptr,weaponSystemInfo.dwPageSize*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(weaponPages!=nullptr,"allocate weapon field-fault fixture");
    auto* partialGun=reinterpret_cast<ASFXWeapon*>(weaponPages+weaponSystemInfo.dwPageSize-offsetof(ASFXWeapon,CurrentSpareAmmo));
    std::memcpy(partialGun,gun.get(),sizeof(ASFXWeapon));
    auto gunIndex=gun.get()->ObjectInternalInteger; slots[gunIndex]=partialGun; resourcePawn.get()->Weapon=partialGun;
    DWORD weaponOldProtection{};
    Check(VirtualProtect(weaponPages+weaponSystemInfo.dwPageSize,weaponSystemInfo.dwPageSize,PAGE_NOACCESS,&weaponOldProtection)!=0,"protect weapon ammo page");
    auto partialWeaponResult=observe();
    Check(partialWeaponResult.Bool("available") && partialWeaponResult.Get("fields").Get("magazineCapacity").Number("value")==12 &&
        partialWeaponResult.Get("fields").Get("reserveStored").String("reason")=="guarded-weapon-field-read-fault" &&
        !partialWeaponResult.Get("fields").Get("loadedCount").Has("value"),"field read fault preserves only readable proven values");
    slots[gunIndex]=gun.get(); resourcePawn.get()->Weapon=gun.get(); VirtualFree(weaponPages,0,MEM_RELEASE);
    auto readsBeforeWeapon=engineReadCalls;
    Check(ObserveWeapon(0).String("reason")=="not-on-verified-game-thread" && engineReadCalls==readsBeforeWeapon,"wrong thread performs no weapon reads");
    Check(mevr::json::Write(observe(),0).size()<14*1024,"weapon payload fits control envelope budget");
    resourcePawn.get()->Weapon=nullptr;
}

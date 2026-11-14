// SPDX-License-Identifier: MIT
// Original synthetic objects; uses the existing SDK harness's validated roots.
{
    using mevr::engine::le2_diagnostic::ObserveOrbital;
    using mevr::engine::le2_diagnostic::testing::SetOrbitalSampleHook;
    Raw<UClass> omType, ogType, mgrType, camType, latType, actType, planetTypeO, galaxyTypeO, clusterTypeO, systemTypeO, featureTypeO, interpType;
    auto cls=[&](Raw<UClass>& value,char const* name,UObject* package) { add(value.get(),name,meta.get(),package); };
    cls(omType,"SFXGameModeOrbital",sfxPackage.get()); cls(ogType,"SFXGameModeGalaxy",sfxPackage.get());
    omType.get()->SuperField=ogType.get(); cls(mgrType,"SFXGameModeManager",sfxPackage.get());
    cls(camType,"BioCameraBehaviorGalaxy",sfxPackage.get()); cls(latType,"SeqAct_Latent",enginePackage.get());
    cls(actType,"BioSeqAct_OrbitalGame",sfxPackage.get()); actType.get()->SuperField=latType.get();
    cls(planetTypeO,"BioPlanet",sfxPackage.get()); cls(galaxyTypeO,"SFXGalaxy",sfxPackage.get());
    cls(clusterTypeO,"SFXCluster",sfxPackage.get()); cls(systemTypeO,"SFXSystem",sfxPackage.get());
    cls(featureTypeO,"SFXPlanetFeature",sfxPackage.get()); cls(interpType,"InterpActor",enginePackage.get());
    Raw<UClass> orbitalPcType; cls(orbitalPcType,"BioPlayerController",sfxPackage.get()); orbitalPcType.get()->SuperField=controllerType.get();
    Raw<ABioPlayerController> orbitalPc; std::memcpy(orbitalPc.get(),controller.get(),sizeof(APlayerController));
    add(orbitalPc.get(),"OrbitalController",orbitalPcType.get(),nullptr);
    local.get()->Actor=orbitalPc.get(); resourcePawn.get()->Controller=orbitalPc.get(); menuWorldInfo.get()->LocalPlayerController=orbitalPc.get();
    Raw<USFXGameModeOrbital> om; Raw<USFXGameModeGalaxy> gm; Raw<USFXGameModeManager> mgr;
    Raw<UBioCameraBehaviorGalaxy> cameraO; Raw<UBioSeqAct_OrbitalGame> actionO;
    Raw<UBioPlanet> planetO; Raw<USFXGalaxy> galaxyO; Raw<USFXCluster> clusterO; Raw<USFXSystem> systemO;
    Raw<USFXPlanetFeature> featureO; Raw<AInterpActor> actorO;
    add(om.get(),"OrbitalMode",omType.get(),orbitalPc.get()); add(gm.get(),"GalaxyMode",ogType.get(),orbitalPc.get());
    add(mgr.get(),"OrbitalManager",mgrType.get(),orbitalPc.get()); add(cameraO.get(),"GalaxyCamera",camType.get(),nullptr);
    add(actionO.get(),"OrbitalAction",actType.get(),nullptr); add(planetO.get(),"TestPlanet",planetTypeO.get(),nullptr);
    add(galaxyO.get(),"TestGalaxy",galaxyTypeO.get(),nullptr); add(clusterO.get(),"TestCluster",clusterTypeO.get(),galaxyO.get());
    add(systemO.get(),"TestSystem",systemTypeO.get(),clusterO.get()); add(featureO.get(),"TestFeature",featureTypeO.get(),planetO.get());
    add(actorO.get(),"PlanetActor",interpType.get(),nullptr);
    auto* pcO=orbitalPc.get();
    pcO->GameModeManager2=mgr.get();
    USFXGameModeBase* modePointers[11]{}; modePointers[9]=gm.get(); modePointers[10]=om.get();
    USeqAct_Latent* latentPointers[2]{actionO.get(),actionO.get()};
    USFXCluster* clusterPointers[21]{}; clusterPointers[20]=clusterO.get();
    USFXSystem* systemPointers[2]{nullptr,systemO.get()}; UBioPlanet* planetPointers[1]{planetO.get()};
    USFXPlanetFeature* featurePointers[2]{featureO.get(),featureO.get()};
    auto arrayHeader=[](auto& array,auto* data,unsigned count,unsigned capacity) {
        struct Header { void* data; unsigned count,capacity; } h{data,count,capacity}; std::memcpy(&array,&h,sizeof(h));
    };
    arrayHeader(mgr.get()->GameModes,modePointers,11,11); mgr.get()->CurrentMode=10; om.get()->bIsActive=1;
    gm.get()->GalaxyCam=cameraO.get(); om.get()->GalaxyCam=nullptr; // no invented inherited-camera equality
    arrayHeader(resourcePawn.get()->LatentActions,latentPointers,1,2);
    actionO.get()->CurrentPlanet=planetO.get(); actionO.get()->Planet=actorO.get(); actorO.get()->WorldInfo=menuWorldInfo.get();
    cameraO.get()->m_nActiveWorld=200100; cameraO.get()->m_pGalaxyMap=galaxyO.get();
    arrayHeader(galaxyO.get()->Clusters,clusterPointers,21,21); arrayHeader(clusterO.get()->Systems,systemPointers,2,2);
    arrayHeader(systemO.get()->Planets,planetPointers,1,1); arrayHeader(planetO.get()->Features,featurePointers,1,2);
    std::wstring orbitalMap=L"SyntheticPlanetMap"; setText(planetO.get()->MapName,orbitalMap);
    planetO.get()->DisplayName=123; planetO.get()->PlanetLandCondition=456; planetO.get()->PlanetLevelType=1;
    featureO.get()->FeatureTransition=789; featureO.get()->VisibleConditional=321; featureO.get()->ScaledAmount=42;
    om.get()->Scanning=1; om.get()->ScanDist=10.5f;
    auto orbit=[&]{return ObserveOrbital(thread);}; auto sample=orbit();
    Check(sample.Bool("available") && sample.Get("context").Number("activeWorld")==200100 &&
          sample.Get("planet").String("mapName")=="SyntheticPlanetMap" && sample.Get("planet").Bool("featuresComplete"),"rooted orbital planet and active-world hierarchy");
    Check(sample.Get("state").Bool("scanning") && sample.Get("planet").Get("features").Items()[0].Number("transition")==789 &&
          sample.Get("resources").Get("fuel").Number("value")==1500.25 && !sample.Bool("savedPlanetIdVerified") &&
          !sample.Bool("discoveryConditionalEvaluated") && !sample.Bool("dispatchCapability"),"orbital state resources and source IDs without calls or saved-ID claims");
    auto reads=engineReadCalls; Check(ObserveOrbital(0).String("reason")=="not-on-verified-game-thread" && reads==engineReadCalls,"wrong-thread orbital performs no reads");
    mgr.get()->CurrentMode=9; Check(!orbit().Bool("available"),"nonorbital mode unavailable"); mgr.get()->CurrentMode=10;
    arrayHeader(resourcePawn.get()->LatentActions,latentPointers,2,2); Check(!orbit().Bool("available"),"duplicate orbital latent rejected");
    arrayHeader(resourcePawn.get()->LatentActions,latentPointers,65,65); Check(!orbit().Bool("available"),"latent bound checked before read");
    arrayHeader(resourcePawn.get()->LatentActions,latentPointers,1,2);
    cameraO.get()->m_nActiveWorld=200101; Check(!orbit().Bool("available"),"active-world planet index bounds"); cameraO.get()->m_nActiveWorld=200100;
    planetPointers[0]=nullptr; Check(!orbit().Bool("available"),"hierarchy and latent planet must agree"); planetPointers[0]=planetO.get();
    featureO.get()->Outer=nullptr; Check(!orbit().Bool("available"),"foreign feature refused"); featureO.get()->Outer=planetO.get();
    arrayHeader(planetO.get()->Features,featurePointers,2,2); Check(!orbit().Bool("available"),"duplicate feature refused");
    arrayHeader(planetO.get()->Features,featurePointers,65,65); Check(!orbit().Bool("available"),"feature bound checked before traversal");
    arrayHeader(planetO.get()->Features,featurePointers,1,2);
    om.get()->ScanDist=std::numeric_limits<float>::quiet_NaN(); Check(!orbit().Bool("available"),"nonfinite orbital sample rejected"); om.get()->ScanDist=10.5f;
    static USFXGameModeOrbital* changedOrbital; changedOrbital=om.get();
    SetOrbitalSampleHook(+[]{changedOrbital->ProbeActive=1;});
    Check(orbit().String("reason")=="orbital-context-or-state-changed","orbital flag change invalidates same-snapshot report");
    SetOrbitalSampleHook(nullptr); om.get()->ProbeActive=0;
    static wchar_t* changedMap; changedMap=orbitalMap.data(); SetOrbitalSampleHook(+[]{changedMap[0]=L'X';});
    Check(!orbit().Bool("available"),"map text body resampled"); SetOrbitalSampleHook(nullptr); orbitalMap[0]=L'S';
    inventory.get()->CurrentFuel=std::numeric_limits<float>::infinity(); sample=orbit();
    Check(sample.Bool("available") && !sample.Get("resources").Get("fuel").Bool("available") && !sample.Get("resources").Get("fuel").Has("value"),"invalid fuel independently unavailable"); inventory.get()->CurrentFuel=1500.25f;
    latentPointers[0]=reinterpret_cast<USeqAct_Latent*>(1); Check(!orbit().Bool("available"),"bad latent pointer guarded"); latentPointers[0]=actionO.get();
    sample=orbit(); Check(sample.Bool("available") && mevr::json::Write(sample,0).size()<14*1024,"orbital report recovers within payload cap");
    std::cout << "Representative orbital observation bytes: " << mevr::json::Write(sample,0).size() << '\n';
    auto oldTableCount=table.count;
    auto many=std::make_unique<std::array<Raw<USFXPlanetFeature>,64>>(); USFXPlanetFeature* manyPointers[64]{};
    Check(static_cast<unsigned>(table.count)+64<=slots.size(),"synthetic feature table capacity");
    for(unsigned i=0;i<64;++i) { auto* f=(*many)[i].get(); std::memcpy(f,featureO.get(),sizeof(*f));
        f->ObjectInternalInteger=table.count; slots[table.count++]=f; manyPointers[i]=f; }
    arrayHeader(planetO.get()->Features,manyPointers,64,64);
    Check(orbit().String("reason")=="orbital-payload-budget-exceeded","complete valid feature list over payload budget is unavailable not truncated");
    table.count=oldTableCount; arrayHeader(planetO.get()->Features,featurePointers,1,2);
    orbitalPc.get()->Class=controllerType.get(); Check(orbit().String("reason")=="orbital-controller-is-not-BioPlayerController","base controller rejected before subclass field read"); orbitalPc.get()->Class=orbitalPcType.get();
    local.get()->Actor=controller.get(); resourcePawn.get()->Controller=controller.get(); menuWorldInfo.get()->LocalPlayerController=reinterpret_cast<ABioPlayerController*>(controller.get()); arrayHeader(resourcePawn.get()->LatentActions,latentPointers,0,0);
}

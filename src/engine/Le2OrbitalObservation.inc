// SPDX-License-Identifier: MIT
// Included once in Le2DiagnosticObservation.cpp to reuse its validated reader.
namespace {
constexpr unsigned kOrbitalLatents = 64, kOrbitalFeatures = 64;
static_assert(offsetof(AActor, LatentActions) == 0x100);
static_assert(offsetof(UBioSeqAct_OrbitalGame, CurrentPlanet) == 0x250);
static_assert(offsetof(USFXGameModeGalaxy, GalaxyCam) == 0xD0);
static_assert(offsetof(UBioCameraBehaviorGalaxy, m_nActiveWorld) == 0x560);
static_assert(offsetof(UBioCameraBehaviorGalaxy, m_pGalaxyMap) == 0x408);
static_assert(offsetof(UBioPlanet, Features) == 0xB0 && offsetof(UBioPlanet, MapName) == 0xC0);
struct OrbitalFeature {
    ObjectSample object;
    int tableId{}, visible{}, visibleParameter{}, usable{}, usableParameter{}, transition{}, transitionParameter{}, scaledAmount{};
    unsigned type{}, mineral{};
};
struct OrbitalData {
    ObjectSample modes, orbital, galaxyMode, camera, galaxy, cluster, system, action, planet, planetActor;
    std::uintptr_t modesData{}, latentData{}, featureData{}, clustersData{}, systemsData{}, planetsData{}, mapName{}, orbitalCamera{};
    unsigned modeCount{}, modeCapacity{}, latentCount{}, latentCapacity{}, featureCount{}, featureCapacity{};
    unsigned clusterCount{}, clusterCapacity{}, systemCount{}, systemCapacity{}, planetCount{}, planetCapacity{};
    std::uintptr_t latents[kOrbitalLatents]{};
    unsigned currentMode{}, planetType{};
    int activeWorld{}, planetTableId{}, displayName{}, landConditional{};
    bool active{}, scanning{}, probeActive{}, impactActive{}, rescaling{}, exiting{};
    float probeTime{}, impactTime{}, scanDist{};
    OrbitalFeature features[kOrbitalFeatures]{};
};
struct OrbitalSample {
    ResourceContext resource;
    OrbitalData data;
    bool available{};
    char const* reason{"current-player-resource-context-unavailable"};
};
static_assert(std::is_trivially_destructible_v<OrbitalSample>);
#if defined(MEVR_LE2_OBSERVATION_TEST)
void (*gOrbitalSampleHook)(){};
#endif
void ReadOrbital(void* context) {
    auto& s = *static_cast<OrbitalSample*>(context); auto& d = s.data;
    ReadResourceContext(&s.resource);
    if (!s.resource.available) { s.reason = s.resource.reason; return; }
    auto const* objects = UObject::GObjObjects;
    s.reason = "orbital-object-table-invalid";
    if (objects == nullptr || !BoundedArray(*objects,kMaxObjects) || objects->Count()!=s.resource.root.objectCount) return;
    Table table{objects->GetData(),objects->Count()};
    auto const* pc = reinterpret_cast<ABioPlayerController const*>(s.resource.root.player.address);
    auto const* pawn = reinterpret_cast<APawn const*>(s.resource.root.pawn.address);
    s.reason = "orbital-controller-is-not-BioPlayerController";
    if (!IsA(table,pc,"SFXGame","BioPlayerController")) return;
    auto const* modes = pc->GameModeManager2;
    s.reason = "orbital-mode-manager-unavailable";
    if (!ReadObject(table,modes,"SFXGame","SFXGameModeManager",d.modes)) return;
    s.reason = "orbital-mode-array-invalid";
    if (!BoundedArray(modes->GameModes,64)) return;
    d.currentMode=modes->CurrentMode; d.modeCount=modes->GameModes.Count(); d.modeCapacity=modes->GameModes.Capacity();
    d.modesData=reinterpret_cast<std::uintptr_t>(modes->GameModes.GetData());
    s.reason = "current-mode-is-not-orbital-10";
    if (d.currentMode!=10 || d.modeCount<=10) return;
    auto const* orbital = static_cast<USFXGameModeOrbital const*>(modes->GameModes.GetData()[10]);
    auto const* galaxyMode = static_cast<USFXGameModeGalaxy const*>(modes->GameModes.GetData()[9]);
    s.reason = "orbital-or-galaxy-mode-identity-invalid";
    if (!ReadObject(table,orbital,"SFXGame","SFXGameModeOrbital",d.orbital) || orbital->Outer!=pc ||
        !ReadObject(table,galaxyMode,"SFXGame","SFXGameModeGalaxy",d.galaxyMode) || galaxyMode->Outer!=pc) return;
    d.active=orbital->bIsActive; s.reason="orbital-mode-inactive"; if (!d.active) return;
    d.orbitalCamera=reinterpret_cast<std::uintptr_t>(orbital->GalaxyCam);
    // GetGalaxyCamera calls native HACK_GetCameraMode(9); its body is opaque.
    // This is the SDK mode9 field path, cross-checked against CurrentPlanet,
    // not proof of that accessor's return. Do not assume mode10 equality.
    auto const* camera=galaxyMode->GalaxyCam;
    s.reason="galaxy-camera-unavailable";
    if (!ReadObject(table,camera,"SFXGame","BioCameraBehaviorGalaxy",d.camera)) return;
    d.activeWorld=camera->m_nActiveWorld;
    s.reason="orbital-latent-array-invalid";
    if (!BoundedArray(pawn->LatentActions,kOrbitalLatents)) return;
    d.latentCount=pawn->LatentActions.Count(); d.latentCapacity=pawn->LatentActions.Capacity();
    d.latentData=reinterpret_cast<std::uintptr_t>(pawn->LatentActions.GetData());
    UBioSeqAct_OrbitalGame const* action=nullptr;
    for (unsigned i=0;i<d.latentCount;++i) {
        auto const* latent=pawn->LatentActions.GetData()[i]; d.latents[i]=reinterpret_cast<std::uintptr_t>(latent);
        s.reason="invalid-or-ambiguous-orbital-latent-action";
        if (!table.Listed(latent,true) || !IsA(table,latent,"Engine","SeqAct_Latent")) return;
        if (IsA(table,latent,"SFXGame","BioSeqAct_OrbitalGame")) {
            if (action!=nullptr || !ReadObject(table,latent,"SFXGame","BioSeqAct_OrbitalGame",d.action)) return;
            action=static_cast<UBioSeqAct_OrbitalGame const*>(latent);
        }
    }
    s.reason="orbital-latent-action-missing"; if (!action) return;
    auto const* planet=action->CurrentPlanet;
    s.reason="orbital-current-planet-unavailable";
    if (!ReadObject(table,planet,"SFXGame","BioPlanet",d.planet) ||
        !ReadObject(table,action->Planet,"Engine","InterpActor",d.planetActor)) return;
    s.reason="orbital-planet-actor-world-disagrees";
    if (reinterpret_cast<std::uintptr_t>(action->Planet->WorldInfo)!=s.resource.worldInfo.address) return;
    auto const* galaxy=camera->m_pGalaxyMap;
    s.reason="active-world-galaxy-path-invalid";
    if (d.activeWorld<0 || !ReadObject(table,galaxy,"SFXGame","SFXGalaxy",d.galaxy) || !BoundedArray(galaxy->Clusters,128)) return;
    unsigned const ci=static_cast<unsigned>(d.activeWorld/10000), si=static_cast<unsigned>(d.activeWorld/100%100), pi=static_cast<unsigned>(d.activeWorld%10);
    d.clusterCount=galaxy->Clusters.Count(); d.clusterCapacity=galaxy->Clusters.Capacity(); d.clustersData=reinterpret_cast<std::uintptr_t>(galaxy->Clusters.GetData());
    if (ci>=d.clusterCount) return;
    auto const* cluster=galaxy->Clusters.GetData()[ci];
    if (!ReadObject(table,cluster,"SFXGame","SFXCluster",d.cluster) || !BoundedArray(cluster->Systems,100)) return;
    d.systemCount=cluster->Systems.Count(); d.systemCapacity=cluster->Systems.Capacity(); d.systemsData=reinterpret_cast<std::uintptr_t>(cluster->Systems.GetData());
    if (si>=d.systemCount) return;
    auto const* system=cluster->Systems.GetData()[si];
    if (!ReadObject(table,system,"SFXGame","SFXSystem",d.system) || !BoundedArray(system->Planets,100)) return;
    d.planetCount=system->Planets.Count(); d.planetCapacity=system->Planets.Capacity(); d.planetsData=reinterpret_cast<std::uintptr_t>(system->Planets.GetData());
    s.reason="active-world-selected-planet-disagrees";
    if (pi>=d.planetCount || system->Planets.GetData()[pi]!=planet) return;
    d.planetTableId=planet->TableID; d.displayName=planet->DisplayName; d.planetType=planet->PlanetLevelType;
    d.landConditional=planet->PlanetLandCondition;
    d.mapName=reinterpret_cast<std::uintptr_t>(&planet->MapName);
    d.scanning=orbital->Scanning; d.probeActive=orbital->ProbeActive; d.impactActive=orbital->ImpactActive;
    d.rescaling=orbital->bRescaling; d.exiting=orbital->bExiting;
    d.probeTime=orbital->ProbeLaunchTime; d.impactTime=orbital->ImpactTime; d.scanDist=orbital->ScanDist;
    s.reason="nonfinite-orbital-state";
    if (!std::isfinite(d.probeTime) || !std::isfinite(d.impactTime) || !std::isfinite(d.scanDist)) return;
    s.reason="planet-features-invalid-or-over-limit";
    if (!BoundedArray(planet->Features,kOrbitalFeatures)) return;
    d.featureCount=planet->Features.Count(); d.featureCapacity=planet->Features.Capacity(); d.featureData=reinterpret_cast<std::uintptr_t>(planet->Features.GetData());
    for(unsigned i=0;i<d.featureCount;++i) {
        auto const* f=planet->Features.GetData()[i]; auto& out=d.features[i];
        s.reason="invalid-duplicate-or-foreign-planet-feature";
        if (!ReadObject(table,f,"SFXGame","SFXPlanetFeature",out.object) || f->Outer!=planet) return;
        for(unsigned j=0;j<i;++j) if(d.features[j].object.address==out.object.address) return;
        out.tableId=f->TableID; out.visible=f->VisibleConditional; out.visibleParameter=f->VisibleParameter;
        out.usable=f->UsableConditional; out.usableParameter=f->UsableParameter;
        out.transition=f->FeatureTransition; out.transitionParameter=f->FeatureTransitionParameter;
        out.scaledAmount=f->ScaledAmount; out.type=f->FeatureType; out.mineral=f->MineralType;
    }
    s.available=true; s.reason="stable-current-orbital-structural-sample";
}
bool SameOrbitalData(OrbitalData const& a, OrbitalData const& b) {
    if (!SameObjectSample(a.modes,b.modes) ||
        !SameObjectSample(a.orbital,b.orbital) ||
        !SameObjectSample(a.galaxyMode,b.galaxyMode) ||
        !SameObjectSample(a.camera,b.camera) ||
        !SameObjectSample(a.galaxy,b.galaxy) ||
        !SameObjectSample(a.cluster,b.cluster) ||
        !SameObjectSample(a.system,b.system) ||
        !SameObjectSample(a.action,b.action) ||
        !SameObjectSample(a.planet,b.planet) ||
        !SameObjectSample(a.planetActor,b.planetActor)) return false;
    if (a.modesData!=b.modesData ||
        a.latentData!=b.latentData ||
        a.featureData!=b.featureData ||
        a.clustersData!=b.clustersData ||
        a.systemsData!=b.systemsData ||
        a.planetsData!=b.planetsData ||
        a.mapName!=b.mapName ||
        a.orbitalCamera!=b.orbitalCamera ||
        a.modeCount!=b.modeCount ||
        a.modeCapacity!=b.modeCapacity ||
        a.latentCount!=b.latentCount ||
        a.latentCapacity!=b.latentCapacity ||
        a.featureCount!=b.featureCount ||
        a.featureCapacity!=b.featureCapacity ||
        a.clusterCount!=b.clusterCount ||
        a.clusterCapacity!=b.clusterCapacity ||
        a.systemCount!=b.systemCount ||
        a.systemCapacity!=b.systemCapacity ||
        a.planetCount!=b.planetCount ||
        a.planetCapacity!=b.planetCapacity ||
        a.currentMode!=b.currentMode ||
        a.planetType!=b.planetType ||
        a.activeWorld!=b.activeWorld ||
        a.planetTableId!=b.planetTableId ||
        a.displayName!=b.displayName ||
        a.landConditional!=b.landConditional ||
        a.active!=b.active ||
        a.scanning!=b.scanning ||
        a.probeActive!=b.probeActive ||
        a.impactActive!=b.impactActive ||
        a.rescaling!=b.rescaling ||
        a.exiting!=b.exiting ||
        a.probeTime!=b.probeTime ||
        a.impactTime!=b.impactTime ||
        a.scanDist!=b.scanDist) return false;
    for(unsigned i=0;i<a.latentCount;++i) if(a.latents[i]!=b.latents[i]) return false;
    for(unsigned i=0;i<a.featureCount;++i) {
        auto const& x=a.features[i]; auto const& y=b.features[i];
        if (!SameObjectSample(x.object,y.object) || x.tableId!=y.tableId || x.visible!=y.visible || x.visibleParameter!=y.visibleParameter || x.usable!=y.usable || x.usableParameter!=y.usableParameter || x.transition!=y.transition || x.transitionParameter!=y.transitionParameter || x.scaledAmount!=y.scaledAmount || x.type!=y.type || x.mineral!=y.mineral) return false;
    }
    return true;
}
struct OrbitalTextCheck { OrbitalSample* sample; CapturedText const* text; bool matches{}; };
void CheckOrbitalText(void* context) {
    auto& c=*static_cast<OrbitalTextCheck*>(context); ReadOrbital(c.sample);
    c.matches=c.sample->available && TextStillMatches(c.sample->data.mapName,*c.text);
}
} // namespace
#if defined(MEVR_LE2_OBSERVATION_TEST)
void testing::SetOrbitalSampleHook(void (*hook)()) { gOrbitalSampleHook=hook; }
#endif
json::Value ObserveOrbital(std::uint32_t thread) {
    auto out=json::Value::Object(); out["schemaVersion"]=1; out["game"]="LE2";
    out["available"]=false; out["dispatchCapability"]=false; out["savedPlanetIdVerified"]=false;
    out["discoveryConditionalEvaluated"]=false; out["persistenceVerified"]=false; out["durableObjectIdentity"]=false;
    auto refuse=[&](char const* reason) { out["reason"]=reason; return out; };
    if(thread==0 || GetCurrentThreadId()!=thread) return refuse("not-on-verified-game-thread");
    auto first=std::make_unique<OrbitalSample>(); auto after=std::make_unique<OrbitalSample>();
    if(!ue3::runtime::Guarded(ReadOrbital,first.get())) return refuse("guarded-orbital-read-fault");
    if(!first->available) return refuse(first->reason);
    CapturedText mapText{}; auto map=CaptureText(first->data.mapName,mapText);
    if(!map.Available() || !mapText.headerCopied || (mapText.header.count!=0 && !mapText.bodyCopied)) return refuse("planet-map-name-unavailable");
    ResourceFieldRead values[2][kResourceCount]{}; bool readable[2][kResourceCount]{};
    for(unsigned pass=0;pass<2;++pass) {
#if defined(MEVR_LE2_OBSERVATION_TEST)
        if(pass==1 && gOrbitalSampleHook) gOrbitalSampleHook();
#endif
        for(unsigned i=0;i<kResourceCount;++i) { values[pass][i]={first->resource.inventory.address,i}; readable[pass][i]=ue3::runtime::Guarded(ReadResourceField,&values[pass][i]); }
    }
    OrbitalTextCheck check{after.get(),&mapText};
    if(!ue3::runtime::Guarded(CheckOrbitalText,&check)) return refuse("guarded-orbital-reread-fault");
    if(!check.matches || !SameResourceContext(first->resource,after->resource) ||
        !SameOrbitalData(first->data,after->data)) return refuse("orbital-context-or-state-changed");
    auto const& d=first->data;
    out["available"]=true; out["reason"]=first->reason;
    auto context=json::Value::Object(); context["currentMode"]=static_cast<int>(d.currentMode); context["activeWorld"]=d.activeWorld;
    context["controller"]=ObjectJson(first->resource.root.player,nullptr); context["pawn"]=ObjectJson(first->resource.root.pawn,nullptr);
    context["world"]=ObjectJson(first->resource.root.world,nullptr); context["worldInfo"]=ObjectJson(first->resource.worldInfo,nullptr);
    context["inventory"]=ObjectJson(first->resource.inventory,nullptr); context["mode"]=ObjectJson(d.orbital,nullptr);
    context["camera"]=ObjectJson(d.camera,nullptr); context["latentAction"]=ObjectJson(d.action,nullptr);
    context["cameraSource"]="GameModeManager2.GameModes[9].GalaxyCam"; out["context"]=std::move(context);
    auto planet=json::Value::Object(); planet["object"]=ObjectJson(d.planet,nullptr); planet["mapName"]=map.utf8;
    planet["tableId"]=d.planetTableId; planet["displayNameRef"]=d.displayName; planet["levelType"]=static_cast<int>(d.planetType);
    planet["landConditional"]=d.landConditional;
    planet["source"]="unique pawn.LatentActions BioSeqAct_OrbitalGame.CurrentPlanet; camera hierarchy cross-check";
    auto features=json::Value::Array();
    for(unsigned i=0;i<d.featureCount;++i) { auto const& f=d.features[i]; auto row=json::Value::Object();
        row["index"]=static_cast<int>(i); row["tableId"]=f.tableId; row["type"]=static_cast<int>(f.type); row["mineralType"]=static_cast<int>(f.mineral);
        row["scaledAmount"]=f.scaledAmount; row["visibleConditional"]=f.visible; row["visibleParameter"]=f.visibleParameter;
        row["usableConditional"]=f.usable; row["usableParameter"]=f.usableParameter;
        row["transition"]=f.transition; row["transitionParameter"]=f.transitionParameter; features.Push(std::move(row)); }
    planet["featuresComplete"]=true; planet["features"]=std::move(features); out["planet"]=std::move(planet);
    auto state=json::Value::Object(); state["source"]="current SFXGameModeOrbital fields";
    state["scanning"]=d.scanning; state["probeActive"]=d.probeActive; state["impactActive"]=d.impactActive;
    state["rescaling"]=d.rescaling; state["exiting"]=d.exiting; state["probeLaunchTime"]=d.probeTime; state["impactTime"]=d.impactTime; state["scanDist"]=d.scanDist;
    out["state"]=std::move(state); auto resources=json::Value::Object();
    for(unsigned i=0;i<kResourceCount;++i) { auto item=json::Value::Object(); auto bits=values[0][i].bits;
        bool valid=readable[0][i] && readable[1][i] && bits==values[1][i].bits;
        auto scalar=i==7?static_cast<double>(std::bit_cast<float>(bits)):static_cast<double>(std::bit_cast<int>(bits));
        valid=valid && std::isfinite(scalar) && scalar>=0;
        item["available"]=valid; item["reason"]=valid?"stable-direct-live-inventory-field":"unreadable-changing-or-invalid-resource";
        item["sourceField"]=std::string("SFXGame.SFXInventoryManager.")+kResourceFields[i]; item["scalarType"]=i==7?"float32":"int32";
        if(readable[0][i]) item["sampleBits"]=std::format("0x{:08X}",bits);
        if(valid) item["value"]=scalar; resources[kResourceNames[i]]=std::move(item); }
    out["resources"]=std::move(resources);
    if(json::Write(out,0).size()>14*1024) { auto unavailable=json::Value::Object(); unavailable["schemaVersion"]=1; unavailable["game"]="LE2"; unavailable["available"]=false; unavailable["reason"]="orbital-payload-budget-exceeded"; unavailable["dispatchCapability"]=false; return unavailable; }
    return out;
}

// SPDX-License-Identifier: MIT
// Included once after the existing diagnostic reader/flow helpers.
namespace input_context {
namespace {
static_assert(offsetof(APlayerController, PlayerInput)==0x6F8);
static_assert(offsetof(APlayerController, bIgnoreMoveInput)==0x7A5);
static_assert(offsetof(APlayerController, bIgnoreLookInput)==0x7A6);
static_assert(sizeof(State::panelStates)/sizeof(Panel)==kMaxMenuPanels);
static_assert(kNameBytes==128);
static_assert(std::is_trivially_destructible_v<Context>);
struct ReadContext { SaveFlowSample flow; Context result; };
#if defined(MEVR_LE2_OBSERVATION_TEST)
void (*gSampleHook)(){};
#endif
Identity CopyIdentity(ObjectSample const& s) { return {s.address,s.classAddress,s.index}; }
template<class T> ArrayIdentity CopyArray(TArray<T> const& a) {
    return {reinterpret_cast<std::uintptr_t>(a.GetData()),a.Count(),a.Capacity()};
}
void ReadInputContext(void* raw) {
    auto& sample=*static_cast<ReadContext*>(raw); auto& f=sample.flow; auto& c=sample.result;
    ReadSaveFlow(&f);
    c.reason=f.reason;
    if (!f.available || !f.menu.root.mapAvailable) return;
    auto const* objects=UObject::GObjObjects;
    c.reason="input-object-table-unavailable";
    if (!objects || !BoundedArray(*objects,kMaxObjects) || objects->Count()!=f.menu.root.objectCount) return;
    Table table{objects->GetData(),objects->Count()};
    auto const* pc=reinterpret_cast<ABioPlayerController const*>(f.menu.root.player.address);
    auto const* manager=reinterpret_cast<UMassEffectGuiManager const*>(f.menu.manager.address);
    auto const* modes=reinterpret_cast<USFXGameModeManager const*>(f.modes.address);
    auto const* mode=reinterpret_cast<USFXGameModeBase const*>(f.mode.address);
    ObjectSample input;
    c.reason="player-input-missing-foreign-or-invalid";
    if (!ReadObject(table,pc->PlayerInput,"Engine","PlayerInput",input) || pc->PlayerInput->Outer!=pc) return;
    c.engine=CopyIdentity(f.menu.root.engine); c.world=CopyIdentity(f.menu.root.world);
    c.localPlayer=CopyIdentity(f.menu.root.localPlayer); c.controller=CopyIdentity(f.menu.root.player);
    c.pawn=CopyIdentity(f.menu.root.pawn); c.playerInput=CopyIdentity(input);
    c.viewport=CopyIdentity(f.menu.viewport); c.guiManager=CopyIdentity(f.menu.manager);
    c.worldInfo=CopyIdentity(f.menu.worldInfo); c.modeManager=CopyIdentity(f.modes); c.mode=CopyIdentity(f.mode);
    c.focusPanel=CopyIdentity(f.focusedPanel); c.currentMode=f.currentMode;
    c.modes=CopyArray(modes->GameModes); c.panels=CopyArray(manager->m_lstPanels);
    c.reason="input-array-header-changed";
    if (c.panels.count!=f.menu.panelCount || c.panels.count>kMaxMenuPanels ||
        c.panels.data!=f.menu.panelsData || c.panels.capacity<c.panels.count || c.panels.capacity>4096 ||
        c.modes.count!=f.modeCount || c.modes.capacity!=f.modeCapacity || c.modes.data!=f.modesData) return;
    std::memcpy(c.map.data(),f.menu.root.map,kNameBytes); std::memcpy(c.focusedTag.data(),f.focusedTag,kNameBytes);
    c.modeActive=f.modeActive; c.allowMovement=mode->bAllowMovement; c.allowCamera=mode->bAllowCamera;
    c.cinematic=pc->bCinematicMode; c.ignoreMove=pc->bIgnoreMoveInput; c.ignoreLook=pc->bIgnoreLookInput;
    c.playersOnly=f.playersOnly; c.playersOnlyPending=manager->oBioWorldInfo->bPlayersOnlyPending;
    c.worldPaused=manager->oBioWorldInfo->Pauser!=nullptr;
    // ReadMenu already validated this bounded complete panel list and every
    // panel/manager reciprocal. Keep each identity and interactive flag so a
    // menu change invalidates an admitted pulse even if focus is unchanged.
    for (unsigned i=0;i<c.panels.count;++i) {
        auto const* panel=manager->m_lstPanels.GetData()[i]; auto& p=c.panelStates[i];
        ObjectSample identity, handler; char tag[kNameBytes]{};
        c.reason="input-panel-or-handler-invalid";
        if (!ReadObject(table,panel,"Engine","BioSFPanel",identity) || panel->oParentManager!=manager ||
            !NameText(panel->nmTag,tag)) return;
        std::memcpy(p.tag.data(),tag,kNameBytes);
        p.object=CopyIdentity(identity);
        if (panel->m_DefaultHandler) {
            if (!ReadObject(table,panel->m_DefaultHandler,"Engine","BioSFHandler",handler) ||
                panel->m_DefaultHandler->oPanel!=panel) return;
            p.handler=CopyIdentity(handler);
        }
        p.visible=panel->IsVisible; p.initialized=panel->bInitialized; p.removing=panel->bToBeRemoved;
        p.inputDisabled=panel->bInputDisabled; p.eventsDisabled=panel->bEventsDisabled; p.focused=manager->m_FocusPanel==panel;
        if (p.focused) {
            if (!BoundedArray(panel->ScriptHandlers,kMaxPanelHandlers)) return;
            c.focusScripts=CopyArray(panel->ScriptHandlers);
            if (c.focusScripts.count>kMaxPanelHandlers || c.focusScripts.capacity<c.focusScripts.count ||
                c.focusScripts.capacity>4096 || (c.focusScripts.count && !c.focusScripts.data)) return;
            for (unsigned j=0;j<c.focusScripts.count;++j) {
                auto const* script=panel->ScriptHandlers.GetData()[j];
                if (!script) continue; // PanelContext permits an empty optional slot.
                ObjectSample scriptIdentity;
                if (!ReadObject(table,script,"Engine","BioSFHandler",scriptIdentity) || script->oPanel!=panel) return;
                c.focusScriptHandlers[j]=CopyIdentity(scriptIdentity);
            }
        }
    }
    c.reason="unsupported-input-context";
    if (f.currentMode==0 && std::strcmp(f.mode.className,"SFXGame.SFXGameModeDefault")==0) {
        c.closedWheel=IdleWheelFocus(f); c.wheelHandler=CopyIdentity(f.idleWheel.handler);
        c.wheelVisible=f.wheelVisible; c.wheelTransition=f.wheelTransition; c.wheelMode=f.wheelMode;
        c.wheelCache=f.wheelCache; c.wheelPlayer=f.wheelPlayer; c.wheelPawn=f.wheelPawn;
        if (c.closedWheel) c.focusHandler=c.wheelHandler;
        c.reason="default-input-context-not-controllable";
        if (!f.modeActive || !c.allowMovement || !c.allowCamera || c.cinematic || c.ignoreMove || c.ignoreLook ||
            c.playersOnly || c.playersOnlyPending || c.worldPaused || (f.menu.focus!=0 && !c.closedWheel) ||
            f.browser.panel.available || f.save.panel.available) return;
        c.category=Category::Default;
    } else if (f.currentMode==7 && std::strcmp(f.mode.className,"SFXGame.SFXGameModeGUI")==0 &&
               std::strcmp(f.focusedTag,"PartySelect")==0) {
        MenuPanel party;
        auto const* panel=reinterpret_cast<UBioSFPanel const*>(f.focusedPanel.address);
        c.reason="party-select-panel-or-handler-not-interactive";
        if (!f.modeActive || !PanelContext(table,panel,manager,"BioSFHandler_PCPartySelection",party,true) ||
            std::strcmp(party.handler.className,"SFXGame.BioSFHandler_PCPartySelection")!=0 || !PanelInteractive(party)) return;
        auto const* handler=reinterpret_cast<UBioSFHandler_PCPartySelection const*>(party.handler.address);
        c.focusHandler=CopyIdentity(party.handler); c.partyInitialized=handler->m_bSFScreenInitialized;
        c.partyNoSelectMode=handler->m_bNoSelectMode; c.partyMember1=handler->m_nPartyMember1; c.partyMember2=handler->m_nPartyMember2;
        c.reason="party-select-handler-not-initialized";
        if (!c.partyInitialized) return;
        // Both initialization flags are structural prerequisites, not proof of
        // GFx completion or enabled controls. Selected members and NoSelectMode
        // are copied without inventing a selection-stage interpretation.
        c.category=Category::PartySelect;
    } else return;
    c.available=true; c.reason="stable-read-only-input-context";
}
} // namespace
bool SameContext(Context const& a, Context const& b) {
    return a.available && b.available && a.category!=Category::Unavailable &&
        static_cast<State const&>(a)==static_cast<State const&>(b);
}
Context CaptureInputContext(std::uint32_t thread) {
    Context unavailable;
    if (!thread || GetCurrentThreadId()!=thread) { unavailable.reason="not-on-verified-game-thread"; return unavailable; }
    auto first=std::make_unique<ReadContext>(); auto second=std::make_unique<ReadContext>();
    if (!ue3::runtime::Guarded(ReadInputContext,first.get())) { unavailable.reason="guarded-input-context-read-fault"; return unavailable; }
    if (!first->result.available) return first->result;
#if defined(MEVR_LE2_OBSERVATION_TEST)
    if (gSampleHook) gSampleHook();
#endif
    if (!ue3::runtime::Guarded(ReadInputContext,second.get())) { unavailable.reason="guarded-input-context-reread-fault"; return unavailable; }
    if (!SameContext(first->result,second->result)) { unavailable.reason="input-context-changed-during-sample"; return unavailable; }
    return first->result;
}
json::Value ContextJson(Context const& c) {
    auto out=json::Value::Object(); out["available"]=c.available; out["reason"]=c.reason;
    out["dispatchCapability"]=false; out["bindingsVerified"]=false; out["gameplayEffectProven"]=false;
    out["durableObjectIdentity"]=false;
    out["category"]=c.available?(c.category==Category::Default?"default":"party-select"):"unavailable";
    if (!c.available) return out;
    out["currentMode"]=static_cast<int>(c.currentMode); out["map"]=c.map.data(); out["focusedPanelTag"]=c.focusedTag.data();
    out["modeActive"]=c.modeActive; out["allowMovement"]=c.allowMovement; out["allowCamera"]=c.allowCamera;
    out["cinematic"]=c.cinematic; out["ignoreMove"]=static_cast<int>(c.ignoreMove); out["ignoreLook"]=static_cast<int>(c.ignoreLook);
    out["playersOnly"]=c.playersOnly; out["playersOnlyPending"]=c.playersOnlyPending; out["worldPaused"]=c.worldPaused;
    out["closedPowerWheel"]=c.closedWheel; out["partyHandlerInitialized"]=c.partyInitialized;
    out["partyNoSelectMode"]=c.partyNoSelectMode; out["partyMember1"]=c.partyMember1; out["partyMember2"]=c.partyMember2;
    auto identities=json::Value::Object();
    identities["worldIndex"]=c.world.index; identities["controllerIndex"]=c.controller.index;
    identities["pawnIndex"]=c.pawn.index; identities["playerInputIndex"]=c.playerInput.index;
    identities["modeManagerIndex"]=c.modeManager.index; identities["modeIndex"]=c.mode.index;
    if (c.focusPanel.address) identities["focusedPanelIndex"]=c.focusPanel.index;
    if (c.focusHandler.address) identities["focusedHandlerIndex"]=c.focusHandler.index;
    out["identities"]=std::move(identities); return out;
}
#if defined(MEVR_LE2_OBSERVATION_TEST)
void testing::SetSampleHook(void (*hook)()) { gSampleHook=hook; }
#endif
} // namespace input_context

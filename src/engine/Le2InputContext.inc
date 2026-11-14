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
static_assert(offsetof(UBioSFHandler_MessageBox,__InputCallback__Delegate)==0x9C);
static_assert(offsetof(UBioSFHandler_MessageBox,nInputCallbackContext)==0xD4);
static_assert(offsetof(UBioSFHandler_MessageBox,m_nHiddenDepth)==0xD8);
static_assert(offsetof(UBioSFHandler_PartySelection,lstMemberInfo)==0x9C);
static_assert(offsetof(UBioSFHandler_PartySelection,__ExternalCallback_OnComplete__Delegate)==0xCC);
static_assert(offsetof(FPartySelectMemberInfo,InPartyLabel)==0x20);
static_assert(sizeof(FScriptDelegate)==16 && sizeof(SFXName)==8);
struct ReadContext { SaveFlowSample flow; Context result; };
#if defined(MEVR_LE2_OBSERVATION_TEST)
void (*gSampleHook)(){};
#endif
Identity CopyIdentity(ObjectSample const& s) { return {s.address,s.classAddress,s.index}; }
template<class T> ArrayIdentity CopyArray(TArray<T> const& a) {
    return {reinterpret_cast<std::uintptr_t>(a.GetData()),a.Count(),a.Capacity()};
}
bool CopyPartyName(SFXName const& name, std::array<std::uint32_t,2>& raw, std::array<char,128>& text) {
    char label[kNameBytes]{};
    if (!NameText(name,label)) return false;
    std::memcpy(raw.data(),&name,sizeof(name)); std::memcpy(text.data(),label,kNameBytes);
    return true;
}
bool ReadPartyConfirmation(Table const& table, UMassEffectGuiManager const* manager, Context& c) {
    MenuPanel message, party;
    auto const* focus=reinterpret_cast<UBioSFPanel const*>(c.focusPanel.address);
    c.reason="party-confirm-message-panel-or-handler-invalid";
    if (!c.modeActive || !PanelContext(table,focus,manager,"BioSFHandler_PCMessageBox",message,true) ||
        std::strcmp(message.handler.className,"SFXGame.BioSFHandler_PCMessageBox")!=0 || !PanelInteractive(message)) return false;
    unsigned partyCount=0, messageCount=0;
    for (unsigned i=0;i<c.panels.count;++i) {
        auto const* panel=manager->m_lstPanels.GetData()[i];
        auto const& p=c.panelStates[i];
        if (std::strcmp(p.tag.data(),"MessageBox")==0) ++messageCount;
        if (std::strcmp(p.tag.data(),"PartySelect")!=0) continue;
        c.reason="party-confirm-party-panel-ambiguous-or-invalid";
        if (++partyCount!=1 || !PanelContext(table,panel,manager,"BioSFHandler_PCPartySelection",party,true) ||
            std::strcmp(party.handler.className,"SFXGame.BioSFHandler_PCPartySelection")!=0 ||
            !party.initialized || party.removing) return false;
        // A modal child owns focus and may disable/hide the parent; do not
        // incorrectly require the underlying party panel to be interactive.
    }
    c.reason="party-confirm-reachable-panels-not-unique";
    if (partyCount!=1 || messageCount!=1) return false;
    auto const* msg=reinterpret_cast<UBioSFHandler_PCMessageBox const*>(message.handler.address);
    auto const* selected=reinterpret_cast<UBioSFHandler_PCPartySelection const*>(party.handler.address);
    c.focusHandler=CopyIdentity(message.handler); c.partyPanel=CopyIdentity(party.panel); c.partyHandler=CopyIdentity(party.handler);
    c.partyInitialized=selected->m_bSFScreenInitialized; c.partyNoSelectMode=selected->m_bNoSelectMode;
    c.partyMember1=selected->m_nPartyMember1; c.partyMember2=selected->m_nPartyMember2;
    c.confirmationContext=msg->nInputCallbackContext; c.messageHiddenDepth=msg->m_nHiddenDepth;
    c.messageHandleInput=msg->bHandleInput; c.messageModal=msg->m_bIsModal;
    c.messageCached=msg->m_bCached; c.messageDisplayingHint=msg->m_bDisplayingHint;
    c.messageCache=reinterpret_cast<std::uintptr_t>(manager->m_oMessageBox);
    c.reason="party-confirm-callback-does-not-target-current-party";
    if (msg->__InputCallback__Delegate.Object!=selected || !c.partyInitialized ||
        !CopyPartyName(msg->__InputCallback__Delegate.FunctionName,c.partyNames[0],c.confirmationFunction) ||
        std::strcmp(c.confirmationFunction.data(),"FinalizeParty")!=0 || c.confirmationContext!=0) return false;
    c.confirmationCallback=c.partyHandler;
    c.reason="party-confirm-selected-member-array-or-index-invalid";
    if (!BoundedArray(selected->lstMemberInfo,32)) return false;
    c.partyMembers=CopyArray(selected->lstMemberInfo);
    int const indices[]{c.partyMember1,c.partyMember2};
    for (unsigned i=0;i<2;++i) {
        if (indices[i]==-1) continue; // FinalizeParty explicitly permits absent members.
        if (indices[i]<0 || static_cast<unsigned>(indices[i])>=c.partyMembers.count) return false;
        auto& label=i==0?c.partyLabel1:c.partyLabel2;
        if (!CopyPartyName(selected->lstMemberInfo.GetData()[indices[i]].InPartyLabel,c.partyNames[i+2],label)) return false;
    }
    // ExitScreen may call an external completion delegate instead of simply
    // hiding PartySelect. Preserve that identity; never infer its destination.
    auto const& completion=selected->__ExternalCallback_OnComplete__Delegate;
    std::memcpy(c.partyNames[1].data(),&completion.FunctionName,sizeof(SFXName));
    if (completion.Object) {
        ObjectSample target;
        c.reason="party-confirm-completion-delegate-invalid";
        if (!ReadObject(table,completion.Object,"Core","Object",target) ||
            !CopyPartyName(completion.FunctionName,c.partyNames[1],c.completionFunction)) return false;
        c.partyCompletionCallback=CopyIdentity(target);
        static_assert(sizeof(target.className)==sizeof(c.partyCompletionClass));
        std::memcpy(c.partyCompletionClass.data(),target.className,sizeof(target.className));
    }
    c.category=Category::PartyConfirm;
    return true;
}
static_assert(offsetof(UBioSFHandler_ChoiceGUI,__InputCallback__Delegate)==0xBC);
static_assert(offsetof(UBioSFHandler_ChoiceGUI,m_ChoiceData)==0xEC);
static_assert(offsetof(USFXSeqAct_ChooseLoadoutBase,TopMenu)==0x15C);
static_assert(offsetof(USFXSeqAct_ChooseLoadoutBase,SubMenu)==0x164);
static_assert(offsetof(USFXSeqAct_ChooseLoadoutBase,CurrentPawn)==0x1C0);
static_assert(offsetof(USequenceObject,ParentSequence)==0x70);
static_assert(offsetof(USequence,SequenceObjects)==0xDC);
static_assert(offsetof(USequence,ActiveSequenceOps)==0xEC);
bool ReadNameKey(SFXName const& name, NameKey& out) { return CopyPartyName(name,out.raw,out.text); }
bool ReadNameBits(SFXName const& name, std::array<std::uint32_t,2>& out) {
    char text[kNameBytes]{};
    if (!NameText(name,text)) return false;
    std::memcpy(out.data(),&name,sizeof(name)); return true;
}
bool LoadoutWeaponClass(Table const& table, UClass const* type) {
    for (unsigned depth=0;type && depth<kMaxClassDepth;++depth) {
        if (!ClassMetadata(table,type)) return false;
        if (Qualified(table,type,"SFXGame","SFXWeapon")) return true;
        type=static_cast<UClass const*>(type->SuperField);
    }
    return false;
}
bool ReadLoadoutContext(Table const& table, UMassEffectGuiManager const* manager, Context& c) {
    auto& s=c.loadout; MenuPanel choice;
    auto const* focus=reinterpret_cast<UBioSFPanel const*>(c.focusPanel.address);
    c.reason="loadout-choice-panel-or-handler-invalid";
    if (!c.modeActive || !PanelContext(table,focus,manager,"BioSFHandler_PCChoiceGUI",choice,true) ||
        std::strcmp(choice.handler.className,"SFXGame.BioSFHandler_PCChoiceGUI")!=0 || !PanelInteractive(choice)) return false;
    unsigned matchingPanels=0;
    for (unsigned i=0;i<c.panels.count;++i) if (c.panelStates[i].object.address==c.focusPanel.address) ++matchingPanels;
    c.reason="loadout-choice-panel-ambiguous";
    if (matchingPanels!=1) return false;
    c.focusHandler=CopyIdentity(choice.handler);
    auto const* handler=reinterpret_cast<UBioSFHandler_PCChoiceGUI const*>(choice.handler.address);
    ObjectSample action, sequence, data;
    c.reason="loadout-callback-action-invalid";
    if (!ReadObject(table,handler->__InputCallback__Delegate.Object,"SFXGame","SFXSeqAct_ChooseLoadoutBase",action) ||
        (std::strcmp(action.className,"SFXGame.SFXSeqAct_ChooseLoadoutBase")!=0 &&
         std::strcmp(action.className,"SFXGameContent.SFXSeqAct_ChooseLoadout")!=0) ||
        !ReadNameKey(handler->__InputCallback__Delegate.FunctionName,s.callback)) return false;
    auto const* owner=reinterpret_cast<USFXSeqAct_ChooseLoadoutBase const*>(action.address);
    s.action=CopyIdentity(action); s.topMenu=reinterpret_cast<std::uintptr_t>(owner->TopMenu);
    s.subMenu=reinterpret_cast<std::uintptr_t>(owner->SubMenu);
    s.callbackContext=handler->nInputCallbackContext; s.currentPawn=owner->CurrentPawn;
    s.subWeaponIndex=owner->SubMenuWeaponIdx; s.active=owner->bActive;
    s.finished=owner->m_bFinished; s.latent=owner->bLatentExecution;
    s.activationCount=owner->ActivateCount; s.allHenchmen=owner->m_bAllHenchmen;
    c.reason="loadout-callback-stage-or-menu-reciprocal-invalid";
    if (std::strcmp(s.callback.text.data(),"CharacterMenuHandler")==0 && owner->TopMenu==handler) s.stage=1;
    else if (std::strcmp(s.callback.text.data(),"TopMenuHandler")==0 && owner->TopMenu==handler) s.stage=2;
    else if (std::strcmp(s.callback.text.data(),"SubMenuHandler")==0 && owner->SubMenu==handler) s.stage=3;
    else return false;
    c.reason="loadout-action-not-active-or-finished";
    if (!s.active || s.finished) return false;
    auto const* parent=owner->ParentSequence;
    c.reason="loadout-parent-sequence-invalid";
    if (!ReadObject(table,parent,"Engine","Sequence",sequence) || owner->Outer!=parent ||
        !BoundedArray(parent->SequenceObjects,4096) || !BoundedArray(parent->ActiveSequenceOps,4096)) return false;
    s.sequence=CopyIdentity(sequence); s.sequenceEnabled=parent->bEnabled;
    s.objects=CopyArray(parent->SequenceObjects); s.activeOps=CopyArray(parent->ActiveSequenceOps);
    unsigned members=0, activeMembers=0;
    // Only compare pointers in these bounded owner arrays. Never dereference
    // unrelated sequence members or search the global object table.
    for (unsigned i=0;i<s.objects.count;++i) if (parent->SequenceObjects.GetData()[i]==owner) { ++members; s.sequenceSlot=i; }
    for (unsigned i=0;i<s.activeOps.count;++i) if (parent->ActiveSequenceOps.GetData()[i]==owner) { ++activeMembers; s.activeSlot=i; }
    c.reason="loadout-action-not-unique-active-sequence-member";
    if (!s.sequenceEnabled || members!=1 || activeMembers!=1) return false;
    c.reason="loadout-choice-data-invalid-or-foreign";
    if (!ReadObject(table,handler->m_ChoiceData,"SFXGame","SFXGameChoiceGUIData",data) ||
        std::strcmp(data.className,"SFXGame.SFXGameChoiceGUIData")!=0 || handler->m_ChoiceData->Outer!=handler) return false;
    s.data=CopyIdentity(data);
    c.reason="loadout-array-invalid-or-over-limit";
    if (!BoundedArray(owner->AvailableHenchmen,32) || !BoundedArray(owner->LoadoutWeapons,6) ||
        !BoundedArray(owner->WeaponMenuEnabledWeaponIDs,6) || !BoundedArray(owner->SubMenuEnabledWeapons,64) ||
        !BoundedArray(handler->m_ChoiceData->lstChoices,64)) return false;
    s.characters=CopyArray(owner->AvailableHenchmen); s.weapons=CopyArray(owner->LoadoutWeapons);
    s.weaponIds=CopyArray(owner->WeaponMenuEnabledWeaponIDs); s.subWeapons=CopyArray(owner->SubMenuEnabledWeapons);
    s.choices=CopyArray(handler->m_ChoiceData->lstChoices);
    c.reason="loadout-current-character-or-choice-count-invalid";
    if (s.characters.count==0 || s.currentPawn<0 || static_cast<unsigned>(s.currentPawn)>=s.characters.count ||
        s.choices.count==0 || s.choices.count!=(s.stage==1?s.characters.count:s.stage==2?s.weaponIds.count:s.subWeapons.count)) return false;
    for (unsigned i=0;i<s.characters.count;++i) if (!ReadNameBits(owner->AvailableHenchmen.GetData()[i],s.characterNames[i])) return false;
    // Activated explicitly reserves index zero for Shepard with the None name.
    char firstCharacter[kNameBytes]{};
    if (!NameText(owner->AvailableHenchmen.GetData()[0],firstCharacter) || std::strcmp(firstCharacter,"None")!=0) return false;
    c.reason="loadout-weapon-class-or-index-invalid";
    for (unsigned i=0;i<s.weapons.count;++i) {
        auto const* type=owner->LoadoutWeapons.GetData()[i];
        if (!type) continue; // unavailable groups may be null; selectable groups below may not.
        if (!LoadoutWeaponClass(table,type)) return false;
        s.weaponClasses[i]={reinterpret_cast<std::uintptr_t>(type),reinterpret_cast<std::uintptr_t>(type->Class),type->ObjectInternalInteger};
    }
    for (unsigned i=0;i<s.weaponIds.count;++i) {
        int const id=owner->WeaponMenuEnabledWeaponIDs.GetData()[i];
        if (id<0 || id>=6 || static_cast<unsigned>(id)>=s.weapons.count || !s.weaponClasses[id].address) return false;
        for (unsigned j=0;j<i;++j) if (s.enabledWeaponIds[j]==id) return false;
        s.enabledWeaponIds[i]=id;
    }
    if (s.stage!=1 && s.weapons.count!=6) return false;
    if (s.stage==3 && (s.subWeaponIndex<0 || s.subWeaponIndex>=6 || !s.weaponClasses[s.subWeaponIndex].address)) return false;
    if (s.stage==3) {
        bool enabled=false;
        for (unsigned i=0;i<s.weaponIds.count;++i) enabled=enabled || s.enabledWeaponIds[i]==s.subWeaponIndex;
        if (!enabled) return false;
    }
    for (unsigned i=0;i<s.subWeapons.count;++i) {
        auto const& row=owner->SubMenuEnabledWeapons.GetData()[i];
        if (!ReadNameBits(row.ClassName,s.subWeaponNames[i]) || !ReadNameBits(row.UnlockPlotId,s.subWeaponUnlocks[i])) return false;
        s.subWeaponRatings[i]=row.Rating; s.subWeaponSpecial[i]=row.bNotRegularWeaponGUI;
    }
    for (unsigned i=0;i<s.choices.count;++i) {
        auto const& row=handler->m_ChoiceData->lstChoices.GetData()[i];
        s.choiceStates[i]={row.nChoiceID,row.bDisabled!=0,row.bNested!=0};
    }
    c.category=Category::Loadout;
    return true;
}
#include "engine/Le2CharacterRecordContext.inc"
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
    } else if (f.currentMode==7 && std::strcmp(f.mode.className,"SFXGame.SFXGameModeGUI")==0 &&
               std::strcmp(f.focusedTag,"MessageBox")==0) {
        if (!ReadPartyConfirmation(table,manager,c)) return;
    } else if (f.currentMode==7 && std::strcmp(f.mode.className,"SFXGame.SFXGameModeGUI")==0 &&
               std::strcmp(f.focusedTag,"ChoiceGUI")==0) {
        if (!ReadLoadoutContext(table,manager,c)) return;
    } else if (f.currentMode==7 && std::strcmp(f.mode.className,"SFXGame.SFXGameModeGUI")==0 &&
               std::strcmp(f.focusedTag,"CharacterRecord")==0) {
        if (!ReadCharacterRecord(table,manager,c)) return;
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
    out["category"]=c.available?(c.category==Category::Default?"default":c.category==Category::PartySelect?"party-select":
        c.category==Category::PartyConfirm?"party-confirm":c.category==Category::CharacterRecord?"character-record":"loadout"):"unavailable";
    if (!c.available) return out;
    out["currentMode"]=static_cast<int>(c.currentMode); out["map"]=c.map.data(); out["focusedPanelTag"]=c.focusedTag.data();
    out["modeActive"]=c.modeActive; out["allowMovement"]=c.allowMovement; out["allowCamera"]=c.allowCamera;
    out["cinematic"]=c.cinematic; out["ignoreMove"]=static_cast<int>(c.ignoreMove); out["ignoreLook"]=static_cast<int>(c.ignoreLook);
    out["playersOnly"]=c.playersOnly; out["playersOnlyPending"]=c.playersOnlyPending; out["worldPaused"]=c.worldPaused;
    out["closedPowerWheel"]=c.closedWheel; out["partyHandlerInitialized"]=c.partyInitialized;
    out["partyNoSelectMode"]=c.partyNoSelectMode; out["partyMember1"]=c.partyMember1; out["partyMember2"]=c.partyMember2;
    if (c.category==Category::PartyConfirm) {
        auto confirmation=json::Value::Object();
        confirmation["callback"]=c.confirmationFunction.data(); confirmation["callbackContext"]=c.confirmationContext;
        confirmation["currentPartyRelationVerified"]=true; confirmation["memberCount"]=static_cast<int>(c.partyMembers.count);
        confirmation["selectedLabel1"]=c.partyLabel1.data(); confirmation["selectedLabel2"]=c.partyLabel2.data();
        confirmation["externalCompletionPresent"]=c.partyCompletionCallback.address!=0;
        confirmation["externalCompletionFunction"]=c.completionFunction.data();
        if (c.partyCompletionCallback.address) {
            auto owner=json::Value::Object();
            owner["class"]=c.partyCompletionClass.data(); owner["index"]=c.partyCompletionCallback.index;
            confirmation["externalCompletionOwner"]=std::move(owner);
            auto functionName=json::Value::Object();
            functionName["index"]=static_cast<double>(c.partyNames[1][0]);
            functionName["number"]=std::bit_cast<std::int32_t>(c.partyNames[1][1]);
            confirmation["externalCompletionFunctionName"]=std::move(functionName);
        }
        confirmation["externalCompletionFunctionResolved"]=false;
        confirmation["externalCompletionObserved"]=false;
        confirmation["completionDestinationProven"]=false;
        confirmation["handleInput"]=c.messageHandleInput; confirmation["modal"]=c.messageModal;
        confirmation["hiddenDepth"]=c.messageHiddenDepth; confirmation["cached"]=c.messageCached;
        confirmation["displayingHint"]=c.messageDisplayingHint;
        confirmation["managerMessageCacheMatches"]=c.messageCache==c.focusHandler.address;
        out["partyConfirmation"]=std::move(confirmation);
    }
    if (c.category==Category::Loadout) {
        auto loadout=json::Value::Object(); auto const& s=c.loadout;
        loadout["callback"]=s.callback.text.data();
        loadout["stage"]=s.stage==1?"characters":s.stage==2?"weapon-groups":"weapons";
        loadout["currentPawnIndex"]=s.currentPawn; loadout["subWeaponIndex"]=s.subWeaponIndex;
        loadout["callbackContext"]=s.callbackContext; loadout["callbackContextIsObservedSelection"]=false;
        loadout["active"]=s.active; loadout["finished"]=s.finished; loadout["latent"]=s.latent;
        loadout["activationCount"]=s.activationCount; loadout["allHenchmen"]=s.allHenchmen;
        loadout["characterCount"]=static_cast<int>(s.characters.count); loadout["weaponGroupCount"]=static_cast<int>(s.weaponIds.count);
        loadout["weaponCount"]=static_cast<int>(s.subWeapons.count); loadout["choiceCount"]=static_cast<int>(s.choices.count);
        loadout["actionIndex"]=s.action.index; loadout["sequenceIndex"]=s.sequence.index; loadout["choiceDataIndex"]=s.data.index;
        loadout["currentWorldHandlerRelationVerified"]=true; loadout["activeSequenceReciprocalVerified"]=true;
        loadout["gworldLevelContainmentProven"]=false; loadout["nativeChoiceIndexMappingProven"]=false;
        out["loadout"]=std::move(loadout);
    }
    if (c.category==Category::CharacterRecord) {
        auto record=json::Value::Object(); auto const& s=c.characterRecord;
        record["handlerClass"]="SFXGame.BioSFHandler_PCCharacterRecord2";
        record["helperIndex"]=s.helper.index; record["characterIndex"]=s.characterIndex; record["characterObjectIndex"]=s.character.index;
        record["sourceCharacterCount"]=static_cast<int>(s.sourcePawns.count);
        record["pointsSpent"]=s.pointsSpent; record["powerSnapshotCount"]=static_cast<int>(s.powerSnapshot.count);
        record["closing"]=s.closing; record["initialHidePending"]=s.initialHidePending;
        record["currentSourceCharacterVerified"]=true; record["worldReciprocalsVerified"]=true;
        record["closeCallbackPresent"]=s.closeOwner.address!=0;
        if (s.closeOwner.address) {
            auto owner=json::Value::Object(); owner["class"]=s.closeOwnerClass.data(); owner["index"]=s.closeOwner.index;
            record["closeCallbackOwner"]=std::move(owner); record["closeCallbackFunction"]=s.closeCallback.text.data();
            record["closeCallbackNameIndex"]=static_cast<double>(s.closeCallback.raw[0]);
            record["closeCallbackNameNumber"]=std::bit_cast<std::int32_t>(s.closeCallback.raw[1]);
        }
        record["closeCallbackResolved"]=false; record["closeCallbackExecuted"]=false;
        record["gfxInitializationProven"]=false; record["talentChangesAudited"]=false; record["exitProven"]=false;
        out["characterRecord"]=std::move(record);
    }
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

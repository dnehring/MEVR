// SPDX-License-Identifier: MIT
// Included inside the existing synthetic input fixture, no engine calls.
{
    Raw<UClass> choiceType, pcChoiceType, choiceDataType, loadoutType, sequenceType, loadoutWeaponType;
    Raw<UBioSFHandler_PCChoiceGUI> choiceHandler;
    Raw<UBioSFPanel> choicePanel, hiddenChoicePanel;
    Raw<USFXGameChoiceGUIData> choiceData;
    Raw<USFXSeqAct_ChooseLoadoutBase> loadoutAction;
    Raw<USequence> loadoutSequence;
    add(choiceType.get(),"BioSFHandler_ChoiceGUI",meta.get(),sfxPackage.get()); choiceType.get()->SuperField=handlerType.get();
    add(pcChoiceType.get(),"BioSFHandler_PCChoiceGUI",meta.get(),sfxPackage.get()); pcChoiceType.get()->SuperField=choiceType.get();
    add(choiceDataType.get(),"SFXGameChoiceGUIData",meta.get(),sfxPackage.get()); choiceDataType.get()->SuperField=objectRoot.get();
    add(loadoutType.get(),"SFXSeqAct_ChooseLoadoutBase",meta.get(),sfxPackage.get()); loadoutType.get()->SuperField=objectRoot.get();
    add(sequenceType.get(),"Sequence",meta.get(),enginePackage.get()); sequenceType.get()->SuperField=objectRoot.get();
    add(loadoutWeaponType.get(),"SFXWeapon",meta.get(),sfxPackage.get()); loadoutWeaponType.get()->SuperField=objectRoot.get();
    add(choiceHandler.get(),"CurrentLoadoutHandler",pcChoiceType.get(),nullptr);
    add(choicePanel.get(),"CurrentLoadoutPanel",panelType.get(),nullptr); tag(choicePanel.get(),"ChoiceGUI");
    add(hiddenChoicePanel.get(),"HiddenParentLoadoutPanel",panelType.get(),nullptr); tag(hiddenChoicePanel.get(),"ChoiceGUI");
    add(choiceData.get(),"CurrentChoiceData",choiceDataType.get(),choiceHandler.get());
    add(loadoutSequence.get(),"CurrentLoadoutSequence",sequenceType.get(),nullptr);
    add(loadoutAction.get(),"CurrentLoadoutAction",loadoutType.get(),loadoutSequence.get());
    std::array<SFXName,3> characters{};
    setName(characters[0],"None"); setName(characters[1],"SyntheticHenchOne"); setName(characters[2],"SyntheticHenchTwo");
    UClass* weaponClasses[6]{}; for(auto& type:weaponClasses) type=loadoutWeaponType.get();
    int weaponIds[]{0,1,2};
    std::array<FLoadoutWeaponInfo,3> subWeapons{};
    for(auto& loadoutRow:subWeapons) { setName(loadoutRow.ClassName,"SyntheticWeapon"); setName(loadoutRow.UnlockPlotId,"None"); }
    Raw<std::array<FSFXChoiceEntry,3>> choiceStorage;
    auto& choices=*choiceStorage.get(); // No SDK string/array allocation or destruction.
    USequenceObject* sequenceObjects[]{loadoutAction.get(),nullptr};
    USequenceOp* activeOps[]{loadoutAction.get(),nullptr};
    UBioSFPanel* loadoutPanels[]{choicePanel.get(),hiddenChoicePanel.get()};
    auto arrayHeader=[]<class T>(TArray<T>& dst,T* data,unsigned count,unsigned capacity) {
        mevr::ue3::TArray<T> header{data,static_cast<int>(count),static_cast<int>(capacity)}; std::memcpy(&dst,&header,16);
    };
    auto loadoutInput=[&](unsigned stage=1) {
        beginInput(); modes.get()->CurrentMode=7;
        choiceHandler.get()->Class=pcChoiceType.get(); choiceHandler.get()->oPanel=choicePanel.get(); choiceHandler.get()->oWorldInfo=menuWorldInfo.get();
        choiceHandler.get()->__InputCallback__Delegate.Object=loadoutAction.get();
        setName(choiceHandler.get()->__InputCallback__Delegate.FunctionName,stage==1?"CharacterMenuHandler":stage==2?"TopMenuHandler":"SubMenuHandler");
        choiceHandler.get()->m_ChoiceData=choiceData.get(); choiceHandler.get()->nInputCallbackContext=0;
        choicePanel.get()->oParentManager=manager.get(); choicePanel.get()->m_DefaultHandler=choiceHandler.get();
        choicePanel.get()->IsVisible=choicePanel.get()->bInitialized=1;
        choicePanel.get()->bInputDisabled=choicePanel.get()->bEventsDisabled=choicePanel.get()->bToBeRemoved=0;
        manager.get()->m_FocusPanel=choicePanel.get(); arrayHeader(manager.get()->m_lstPanels,loadoutPanels,1,2);
        choiceData.get()->Outer=choiceHandler.get(); choiceData.get()->Class=choiceDataType.get(); arrayHeader(choiceData.get()->lstChoices,choices.data(),3,3);
        loadoutAction.get()->Class=loadoutType.get(); loadoutAction.get()->Outer=loadoutSequence.get(); loadoutAction.get()->ParentSequence=loadoutSequence.get();
        loadoutAction.get()->bActive=loadoutAction.get()->bLatentExecution=1; loadoutAction.get()->m_bFinished=0;
        loadoutAction.get()->TopMenu=stage==3?nullptr:choiceHandler.get(); loadoutAction.get()->SubMenu=stage==3?choiceHandler.get():nullptr;
        loadoutAction.get()->CurrentPawn=0; loadoutAction.get()->SubMenuWeaponIdx=1; loadoutAction.get()->ActivateCount=1;
        loadoutSequence.get()->bEnabled=1; sequenceObjects[0]=loadoutAction.get(); sequenceObjects[1]=nullptr;
        activeOps[0]=loadoutAction.get(); activeOps[1]=nullptr;
        arrayHeader(loadoutSequence.get()->SequenceObjects,sequenceObjects,1,2); arrayHeader(loadoutSequence.get()->ActiveSequenceOps,activeOps,1,2);
        arrayHeader(loadoutAction.get()->AvailableHenchmen,characters.data(),3,3);
        arrayHeader(loadoutAction.get()->LoadoutWeapons,weaponClasses,stage==1?0:6,6);
        arrayHeader(loadoutAction.get()->WeaponMenuEnabledWeaponIDs,weaponIds,stage==1?0:3,3);
        arrayHeader(loadoutAction.get()->SubMenuEnabledWeapons,subWeapons.data(),stage==3?3:0,3);
        weaponIds[0]=0; weaponIds[1]=1; weaponIds[2]=2;
        for(auto& type:weaponClasses) type=loadoutWeaponType.get();
        for(auto& loadoutRow:choices) loadoutRow.bDisabled=loadoutRow.bNested=0;
    };
    for(unsigned stage=1;stage<=3;++stage) {
        loadoutInput(stage); auto observedLoadout=captureInput();
        Check(observedLoadout.available && observedLoadout.category==ic::Category::Loadout && observedLoadout.loadout.stage==stage,
              "source-bound loadout character group and weapon stages admitted");
        auto report=ic::ContextJson(observedLoadout).Get("loadout");
        Check(report.Bool("activeSequenceReciprocalVerified") && !report.Bool("gworldLevelContainmentProven",true) &&
              !report.Bool("nativeChoiceIndexMappingProven",true),"loadout reports precise structural provenance boundary");
    }
    loadoutInput(2); choices[0].bDisabled=1;
    Check(captureInput().available,"legitimately disabled weapon group does not reject entire loadout UI");
    loadoutInput(); auto baselineLoadout=captureInput();
    hiddenChoicePanel.get()->oParentManager=manager.get(); hiddenChoicePanel.get()->IsVisible=0;
    arrayHeader(manager.get()->m_lstPanels,loadoutPanels,2,2);
    Check(captureInput().available && !ic::SameContext(baselineLoadout,captureInput()),"nonfocused same-tag parent allowed and panel-list change invalidates prior pulse");
    manager.get()->m_FocusPanel=hiddenChoicePanel.get();
    Check(!captureInput().available,"focus switch to unvalidated hidden sibling is unavailable");
    loadoutInput(); tag(choicePanel.get(),"MissionCompletion");
    Check(!captureInput().available,"MissionCompletion class reuse cannot authorize loadout input"); tag(choicePanel.get(),"ChoiceGUI");
    for(unsigned bad=0;bad<24;++bad) {
        loadoutInput(3);
        if(bad==0) choiceHandler.get()->__InputCallback__Delegate.Object=completionTarget.get();
        if(bad==1) setName(choiceHandler.get()->__InputCallback__Delegate.FunctionName,"OtherCallback");
        if(bad==2) loadoutAction.get()->SubMenu=nullptr;
        if(bad==3) loadoutAction.get()->bActive=0;
        if(bad==4) loadoutAction.get()->m_bFinished=1;
        if(bad==5) loadoutAction.get()->ParentSequence=nullptr;
        if(bad==6) loadoutAction.get()->Outer=nullptr;
        if(bad==7) sequenceObjects[0]=nullptr;
        if(bad==8) activeOps[0]=nullptr;
        if(bad==9) { sequenceObjects[1]=loadoutAction.get(); arrayHeader(loadoutSequence.get()->SequenceObjects,sequenceObjects,2,2); }
        if(bad==10) { activeOps[1]=loadoutAction.get(); arrayHeader(loadoutSequence.get()->ActiveSequenceOps,activeOps,2,2); }
        if(bad==11) loadoutSequence.get()->bEnabled=0;
        if(bad==12) choiceData.get()->Outer=nullptr;
        if(bad==13) choiceHandler.get()->oWorldInfo=nullptr;
        if(bad==14) loadoutAction.get()->CurrentPawn=-1;
        if(bad==15) loadoutAction.get()->CurrentPawn=3;
        if(bad==16) loadoutAction.get()->SubMenuWeaponIdx=6;
        if(bad==17) weaponIds[1]=6;
        if(bad==18) weaponIds[1]=0;
        if(bad==19) arrayHeader(choiceData.get()->lstChoices,choices.data(),2,3);
        if(bad==20) arrayHeader(loadoutAction.get()->AvailableHenchmen,characters.data(),33,33);
        if(bad==21) arrayHeader(loadoutSequence.get()->ActiveSequenceOps,reinterpret_cast<USequenceOp**>(1),1,1);
        if(bad==22) weaponClasses[1]=objectRoot.get();
        if(bad==23) choicePanel.get()->bInputDisabled=1;
        Check(!captureInput().available,"unrelated inactive foreign ambiguous or malformed loadout context rejected");
    }
    loadoutInput(3); auto fullLoadout=captureInput();
    loadoutAction.get()->CurrentPawn=1;
    auto changedCharacter=captureInput();
    Check(changedCharacter.available && !ic::SameContext(fullLoadout,changedCharacter),"valid integer character switch invalidates prior pulse");
    loadoutInput(3); static FSFXChoiceEntry* changingChoices; changingChoices=choices.data();
    ic::testing::SetSampleHook(+[]{changingChoices[0].bDisabled=1;});
    Check(!captureInput().available,"choice body change between samples rejects context"); ic::testing::SetSampleHook(nullptr);
    loadoutInput(3); static USFXSeqAct_ChooseLoadoutBase* changingLoadout; changingLoadout=loadoutAction.get();
    ic::testing::SetSampleHook(+[]{++changingLoadout->ActivateCount;});
    Check(!captureInput().available,"new activation between samples rejects context"); ic::testing::SetSampleHook(nullptr);
    loadoutInput(3); static UBioSFHandler_PCChoiceGUI* changingChoice; changingChoice=choiceHandler.get();
    ic::testing::SetSampleHook(+[]{changingChoice->__InputCallback__Delegate.Object=nullptr;});
    Check(!captureInput().available,"loadout callback change between samples rejects context"); ic::testing::SetSampleHook(nullptr);
    loadoutInput(3); static FLoadoutWeaponInfo* changingSubWeapons; changingSubWeapons=subWeapons.data();
    ic::testing::SetSampleHook(+[]{++changingSubWeapons[0].Rating;});
    Check(!captureInput().available,"submenu routing row body change between samples rejects context"); ic::testing::SetSampleHook(nullptr);
}

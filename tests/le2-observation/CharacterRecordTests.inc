// SPDX-License-Identifier: MIT
// Original synthetic SDK objects; no UI function or allocator invocation.
{
    Raw<UClass> recordType, pcRecordType, helperType, recordPawnType;
    Raw<UBioSFHandler_PCCharacterRecord2> record;
    Raw<USFXPowerLevelUpHelper> helper, replacementHelper;
    Raw<UBioSFPanel> recordPanel, duplicateRecordPanel;
    Raw<ABioPawn> firstCharacter, otherCharacter;
    Raw<ABioWorldInfo> foreignRecordWorld;
    Raw<UObject> closeOwner, replacementCloseOwner;
    add(recordType.get(),"BioSFHandler_CharacterRecord2",meta.get(),sfxPackage.get()); recordType.get()->SuperField=handlerType.get();
    add(pcRecordType.get(),"BioSFHandler_PCCharacterRecord2",meta.get(),sfxPackage.get()); pcRecordType.get()->SuperField=recordType.get();
    add(helperType.get(),"SFXPowerLevelUpHelper",meta.get(),sfxPackage.get()); helperType.get()->SuperField=objectRoot.get();
    add(recordPawnType.get(),"BioPawn",meta.get(),sfxPackage.get()); recordPawnType.get()->SuperField=pawnType.get();
    add(record.get(),"CurrentRecordHandler",pcRecordType.get(),nullptr);
    add(helper.get(),"CurrentLevelUpHelper",helperType.get(),record.get());
    add(replacementHelper.get(),"ReplacementLevelUpHelper",helperType.get(),record.get());
    add(recordPanel.get(),"CurrentRecordPanel",panelType.get(),nullptr); tag(recordPanel.get(),"CharacterRecord");
    add(duplicateRecordPanel.get(),"DuplicateRecordPanel",panelType.get(),nullptr); tag(duplicateRecordPanel.get(),"CharacterRecord");
    add(firstCharacter.get(),"FirstCharacter",recordPawnType.get(),nullptr); add(otherCharacter.get(),"OtherCharacter",recordPawnType.get(),nullptr);
    add(foreignRecordWorld.get(),"ForeignRecordWorld",bioWorldInfoType.get(),nullptr);
    add(closeOwner.get(),"CloseOwner",objectRoot.get(),nullptr); add(replacementCloseOwner.get(),"ReplacementCloseOwner",objectRoot.get(),nullptr);
    ABioPawn* sources[]{firstCharacter.get(),otherCharacter.get()};
    UBioSFPanel* recordPanels[]{recordPanel.get(),messagePanel.get(),duplicateRecordPanel.get()};
    FNewPowerInfo powerSnapshots[2]{};
    auto header=[]<class T>(TArray<T>& a,T* data,unsigned count,unsigned capacity) {
        mevr::ue3::TArray<T> raw{data,static_cast<int>(count),static_cast<int>(capacity)}; std::memcpy(&a,&raw,16);
    };
    auto recordInput=[&] {
        beginInput(); modes.get()->CurrentMode=7; guiMode.get()->bIsActive=1;
        guiMode.get()->bAllowMovement=guiMode.get()->bAllowCamera=0;
        manager.get()->m_FocusPanel=recordPanel.get(); header(manager.get()->m_lstPanels,recordPanels,1,3);
        recordPanel.get()->oParentManager=manager.get(); recordPanel.get()->m_DefaultHandler=record.get();
        recordPanel.get()->IsVisible=recordPanel.get()->bInitialized=1;
        recordPanel.get()->bInputDisabled=recordPanel.get()->bEventsDisabled=recordPanel.get()->bToBeRemoved=0;
        record.get()->Class=pcRecordType.get(); record.get()->oPanel=recordPanel.get(); record.get()->oWorldInfo=menuWorldInfo.get();
        record.get()->m_WorldInfo=menuWorldInfo.get(); record.get()->m_Helper=helper.get();
        record.get()->m_bCloseGUIButtonPressed=0; record.get()->m_InitialHiddenTimer=0;
        record.get()->m_CurrentPawnIndex=0;
        record.get()->__OnCloseCallback__Delegate.Object=nullptr; setName(record.get()->__OnCloseCallback__Delegate.FunctionName,"None");
        for(auto* h:{helper.get(),replacementHelper.get()}) {
            h->Class=helperType.get(); h->Outer=record.get(); h->m_WorldInfo=menuWorldInfo.get(); h->m_Pawn=firstCharacter.get(); h->m_nPointsSpent=0;
            header(h->m_UpdatedPowers,powerSnapshots,2,2);
        }
        sources[0]=firstCharacter.get(); sources[1]=otherCharacter.get();
        for(auto* p:sources) {p->WorldInfo=menuWorldInfo.get(); p->Class=recordPawnType.get();}
        header(record.get()->m_SpawnedSquadMembersSource,sources,2,2);
        messagePanel.get()->oParentManager=manager.get(); messagePanel.get()->IsVisible=0;
        duplicateRecordPanel.get()->oParentManager=manager.get();
    };
    recordInput(); auto recordContext=std::make_unique<ic::Context>(captureInput());
    Check(recordContext->available && recordContext->category==ic::Category::CharacterRecord,"exact initialized character record with optional null callback admitted structurally");
    auto report=ic::ContextJson(*recordContext);
    Check(report.String("category")=="character-record" && report.Get("characterRecord").Number("powerSnapshotCount")==2 &&
        !report.Get("characterRecord").Bool("closeCallbackPresent",true) && !report.Get("characterRecord").Bool("talentChangesAudited",true) &&
        !report.Get("characterRecord").Bool("exitProven",true),"record serializer distinguishes initialized power snapshots from audited talent changes or exit");
    helper.get()->m_Pawn=otherCharacter.get(); record.get()->m_CurrentPawnIndex=1;
    Check(captureInput().available && captureInput().characterRecord.character.address!=captureInput().pawn.address &&
        !ic::SameContext(*recordContext,captureInput()),"valid different squad character permitted and character switch cancels prior context");
    recordInput(); record.get()->__OnCloseCallback__Delegate.Object=closeOwner.get();
    setName(record.get()->__OnCloseCallback__Delegate.FunctionName,"SyntheticCloseCallback");
    auto bound=std::make_unique<ic::Context>(captureInput()); auto callback=ic::ContextJson(*bound).Get("characterRecord");
    Check(bound->available && callback.String("closeCallbackFunction")=="SyntheticCloseCallback" &&
        callback.Get("closeCallbackOwner").Number("index")==closeOwner.get()->ObjectInternalInteger &&
        !callback.Bool("closeCallbackResolved",true) && !callback.Bool("closeCallbackExecuted",true),"optional valid close callback observed without requiring a sequence opening");
    record.get()->__OnCloseCallback__Delegate.Object=replacementCloseOwner.get();
    Check(captureInput().available && !ic::SameContext(*bound,captureInput()),"valid callback owner replacement cancels continuity");
    for(unsigned bad=0;bad<27;++bad) {
        recordInput();
        if(bad==0) record.get()->m_bCloseGUIButtonPressed=1;
        if(bad==1) recordPanel.get()->bInitialized=0;
        if(bad==2) recordPanel.get()->bInputDisabled=1;
        if(bad==3) recordPanel.get()->bEventsDisabled=1;
        if(bad==4) recordPanel.get()->bToBeRemoved=1;
        if(bad==5) recordPanel.get()->IsVisible=0;
        if(bad==6) record.get()->Class=recordType.get();
        if(bad==7) record.get()->oPanel=nullptr;
        if(bad==8) record.get()->oWorldInfo=foreignRecordWorld.get();
        if(bad==9) record.get()->m_WorldInfo=foreignRecordWorld.get();
        if(bad==10) record.get()->m_Helper=nullptr;
        if(bad==11) helper.get()->Outer=nullptr;
        if(bad==12) helper.get()->m_WorldInfo=foreignRecordWorld.get();
        if(bad==13) helper.get()->m_Pawn=nullptr;
        if(bad==14) firstCharacter.get()->WorldInfo=foreignRecordWorld.get();
        if(bad==15) record.get()->m_CurrentPawnIndex=-1;
        if(bad==16) record.get()->m_CurrentPawnIndex=2;
        if(bad==17) header(record.get()->m_SpawnedSquadMembersSource,sources,33,33);
        if(bad==18) sources[1]=sources[0];
        if(bad==19) record.get()->__OnCloseCallback__Delegate.Object=reinterpret_cast<UObject*>(1);
        if(bad==20) {record.get()->__OnCloseCallback__Delegate.Object=closeOwner.get(); record.get()->__OnCloseCallback__Delegate.FunctionName.Number=1;}
        if(bad==21) {header(manager.get()->m_lstPanels,recordPanels,2,3); messagePanel.get()->IsVisible=1;}
        if(bad==22) header(manager.get()->m_lstPanels,recordPanels,3,3);
        if(bad==23) record.get()->m_InitialHiddenTimer=0.1f;
        if(bad==24) record.get()->m_InitialHiddenTimer=std::numeric_limits<float>::quiet_NaN();
        if(bad==25) flowPc.get()->PlayerInput=nullptr;
        if(bad==26) header(helper.get()->m_UpdatedPowers,powerSnapshots,129,129);
        Check(!captureInput().available,"invalid or closing character record roots metadata bounds modal and callback refuse admission");
    }
    recordInput(); header(record.get()->m_SpawnedSquadMembersSource,reinterpret_cast<ABioPawn**>(1),2,2);
    Check(!captureInput().available,"unreadable character source array is guarded");
    recordInput(); header(manager.get()->m_lstPanels,recordPanels,2,3);
    Check(captureInput().available,"hidden cached message panel is not an active overlay");
    manager.get()->m_FocusPanel=messagePanel.get(); messagePanel.get()->IsVisible=1;
    Check(!captureInput().available || captureInput().category!=ic::Category::CharacterRecord,"modal focus cannot borrow character-record admission");
    recordInput(); static UBioSFHandler_PCCharacterRecord2* changingRecord; changingRecord=record.get();
    ic::testing::SetSampleHook(+[]{changingRecord->m_bCloseGUIButtonPressed=1;});
    Check(!captureInput().available,"closing transition between samples rejected"); ic::testing::SetSampleHook(nullptr);
    recordInput(); record.get()->__OnCloseCallback__Delegate.Object=closeOwner.get(); setName(record.get()->__OnCloseCallback__Delegate.FunctionName,"SyntheticCloseCallback");
    ic::testing::SetSampleHook(+[]{changingRecord->__OnCloseCallback__Delegate.Object=nullptr;});
    Check(!captureInput().available,"callback removal between otherwise valid samples rejected"); ic::testing::SetSampleHook(nullptr);
    recordInput(); static USFXPowerLevelUpHelper* changingRecordHelper; changingRecordHelper=helper.get();
    ic::testing::SetSampleHook(+[]{++changingRecordHelper->m_nPointsSpent;});
    Check(!captureInput().available,"points state changed between samples rejected"); ic::testing::SetSampleHook(nullptr);
    recordInput(); auto stable=std::make_unique<ic::Context>(captureInput()); record.get()->m_Helper=replacementHelper.get();
    Check(captureInput().available && !ic::SameContext(*stable,captureInput()),"valid helper replacement cancels continuity");
    beginInput();
}

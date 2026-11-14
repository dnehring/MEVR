// SPDX-License-Identifier: MIT
// Synthetic reflected content class, never a reconstructed game class layout.
{
    using mevr::engine::le2_diagnostic::ObservePartyCompletion;
    namespace party_completion_testing=mevr::engine::le2_diagnostic::party_completion_testing;
    auto arrayHeader=[]<class T>(TArray<T>& array,T* data,unsigned count,unsigned capacity) {
        mevr::ue3::TArray<T> header{data,static_cast<int>(count),static_cast<int>(capacity)}; std::memcpy(&array,&header,sizeof(header));
    };
    Raw<UObject> contentPackage;
    Raw<UClass> seqActionType, completionType, completionSequenceType;
    Raw<UBoolProperty> completionProperty, duplicateProperty;
    Raw<USequence> completionSequence;
    alignas(USequenceAction) std::array<unsigned char,1024> completionStorage{};
    auto* action=reinterpret_cast<USequenceAction*>(completionStorage.data());
    add(contentPackage.get(),"SFXGameContent",objectRoot.get(),nullptr);
    add(seqActionType.get(),"SequenceAction",meta.get(),enginePackage.get()); seqActionType.get()->SuperField=objectRoot.get();
    add(completionType.get(),"BioSeqAct_ShowPartySelectionGUI",meta.get(),contentPackage.get()); completionType.get()->SuperField=seqActionType.get();
    add(completionSequenceType.get(),"Sequence",meta.get(),enginePackage.get()); completionSequenceType.get()->SuperField=objectRoot.get();
    add(completionSequence.get(),"CurrentCompletionSequence",completionSequenceType.get(),nullptr);
    add(action,"CurrentCompletionAction",completionType.get(),completionSequence.get());
    add(completionProperty.get(),"m_bPartySelectionComplete",boolType.get(),completionType.get());
    add(duplicateProperty.get(),"m_bPartySelectionComplete",boolType.get(),completionType.get());
    USequenceObject* completionObjects[]{action,nullptr}; USequenceOp* completionActive[]{action,nullptr};
    auto completionInput=[&] {
        confirmationInput();
        partyHandler.get()->__ExternalCallback_OnComplete__Delegate.Object=action;
        setName(partyHandler.get()->__ExternalCallback_OnComplete__Delegate.FunctionName,"OnPartySelectionComplete");
        action->Class=completionType.get(); action->Outer=completionSequence.get(); action->ParentSequence=completionSequence.get();
        action->bActive=1; action->bLatentExecution=1; action->ActivateCount=3;
        completionType.get()->PropertySize=1024; seqActionType.get()->PropertySize=sizeof(USequenceAction);
        completionType.get()->Children=completionProperty.get();
        auto* p=completionProperty.get(); p->Outer=completionType.get(); p->Class=boolType.get(); p->Next=nullptr;
        p->Offset=512; p->ElementSize=4; p->ArrayDim=1; p->BitMask=8; p->PropertyFlags=0;
        std::uint32_t value=8; std::memcpy(completionStorage.data()+512,&value,4);
        completionSequence.get()->bEnabled=1;
        completionObjects[0]=action; completionObjects[1]=nullptr; completionActive[0]=action; completionActive[1]=nullptr;
        arrayHeader(completionSequence.get()->SequenceObjects,completionObjects,1,2);
        arrayHeader(completionSequence.get()->ActiveSequenceOps,completionActive,1,2);
    };
    auto observeCompletion=[&] { return ObservePartyCompletion(thread,captureInput()); };
    completionInput(); auto report=observeCompletion();
    Check(report.Bool("available") && report.Get("completionFlag").Bool("value") && report.Bool("active") &&
          report.Number("activationCount")==3 && report.Get("parentSequence").Number("ownerActiveMembershipCount")==1,
          "exact reflected flag and inherited scheduler state observed with parent membership");
    Check(report.Get("completionFlag").Number("offset")==512 && report.Get("completionFlag").Number("mask")==8 &&
          !report.Bool("affectsInputAdmission",true) && !report.Bool("callbackExecutionProven",true) &&
          !report.Bool("schedulerProgressProven",true),"metadata is explicit without callback or scheduler inference");
    std::uint32_t zero=0; std::memcpy(completionStorage.data()+512,&zero,4);
    action->bActive=0; arrayHeader(completionSequence.get()->ActiveSequenceOps,completionActive,0,2);
    report=observeCompletion();
    Check(report.Bool("available") && !report.Get("completionFlag").Bool("value",true) && !report.Bool("active",true) &&
          report.Get("parentSequence").Number("ownerActiveMembershipCount")==0,"inactive action and false completion remain observable");
    for (unsigned bad=0;bad<17;++bad) {
        completionInput(); auto* p=completionProperty.get();
        if(bad==0) completionType.get()->Children=nullptr;
        if(bad==1) p->Class=intType.get();
        if(bad==2) p->Outer=seqActionType.get();
        if(bad==3) p->Next=p;
        if(bad==4) p->Next=duplicateProperty.get();
        if(bad==5) p->ArrayDim=2;
        if(bad==6) p->ElementSize=1;
        if(bad==7) p->BitMask=0;
        if(bad==8) p->BitMask=3;
        if(bad==9) p->Offset=-1;
        if(bad==10) p->Offset=1022;
        if(bad==11) p->Offset=sizeof(USequenceAction)-4;
        if(bad==12) completionType.get()->PropertySize=65537;
        if(bad==13) p->PropertyFlags=0x80;
        if(bad==14) p->PropertyFlags=0x400;
        if(bad==15) setName(partyHandler.get()->__ExternalCallback_OnComplete__Delegate.FunctionName,"OtherCallback");
        if(bad==16) action->Class=seqActionType.get();
        Check(!observeCompletion().Bool("available"),"unknown malformed duplicate cyclic or unsupported reflected property fails closed");
        Check(captureInput().available,"completion diagnostic failure does not modify valid pulse context admission");
    }
    completionInput(); completionObjects[1]=action; arrayHeader(completionSequence.get()->SequenceObjects,completionObjects,2,2);
    Check(observeCompletion().Get("parentSequence").Number("ownerMembershipCount")==2,"duplicate parent membership reported without readiness claim");
    completionInput(); action->ParentSequence=nullptr;
    report=observeCompletion(); Check(report.Bool("available") && !report.Get("parentSequence").Bool("available"),"missing parent preserves independently read flag");
    completionInput(); arrayHeader(completionSequence.get()->ActiveSequenceOps,completionActive,4097,4097);
    Check(!observeCompletion().Get("parentSequence").Bool("available"),"oversize sequence arrays not scanned");
    completionInput(); arrayHeader(completionSequence.get()->ActiveSequenceOps,reinterpret_cast<USequenceOp**>(1),1,1);
    Check(!observeCompletion().Bool("available"),"unreadable parent array contained by read guard");
    completionInput(); auto savedContext=std::make_unique<ic::Context>(captureInput());
    Check(!ObservePartyCompletion(0,*savedContext).Bool("available"),"wrong thread refuses all completion reads");
    action->ObjectFlags|=0x200;
    Check(!ObservePartyCompletion(thread,*savedContext).Bool("available"),"stale input owner template flag refuses diagnostic"); action->ObjectFlags&=~0x200ull;
    static unsigned char* changingCompletionBytes; changingCompletionBytes=completionStorage.data();
    completionInput(); party_completion_testing::SetSampleHook(+[]{changingCompletionBytes[512]^=8;});
    Check(!observeCompletion().Bool("available"),"completion flag changed between samples unavailable"); party_completion_testing::SetSampleHook(nullptr);
    static UBoolProperty* changingCompletionProperty; changingCompletionProperty=completionProperty.get();
    completionInput(); party_completion_testing::SetSampleHook(+[]{changingCompletionProperty->BitMask=16;});
    Check(!observeCompletion().Bool("available"),"metadata changed between samples unavailable"); party_completion_testing::SetSampleHook(nullptr);
    static USequenceAction* changingCompletionAction; changingCompletionAction=action;
    completionInput(); party_completion_testing::SetSampleHook(+[]{changingCompletionAction->ActivateCount++;});
    Check(!observeCompletion().Bool("available"),"scheduler counter changed between samples unavailable"); party_completion_testing::SetSampleHook(nullptr);
    completionInput(); party_completion_testing::SetSampleHook(+[]{changingCompletionAction->ParentSequence=nullptr;});
    Check(!observeCompletion().Bool("available"),"parent relation changed between samples unavailable"); party_completion_testing::SetSampleHook(nullptr);
    static UBioSFHandler_PCMessageBox* changedFocusHandler; changedFocusHandler=messageHandler.get();
    completionInput(); party_completion_testing::SetSampleHook(+[]{changedFocusHandler->nInputCallbackContext=1;});
    Check(!observeCompletion().Bool("available"),"input context changed during completion read unavailable"); party_completion_testing::SetSampleHook(nullptr);
    completionInput(); auto savedName=completionProperty.get()->Name; completionProperty.get()->Name.Number=1;
    Check(!observeCompletion().Bool("available"),"numbered declaration name refused"); completionProperty.get()->Name=savedName;
    confirmationInput();
}

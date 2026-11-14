// SPDX-License-Identifier: MIT
// Synthetic SDK objects only; no native input, engine allocation or dispatch.
{
    namespace ic=mevr::engine::le2_diagnostic::input_context;
    Raw<UClass> inputClass, partyClass, pcPartyClass;
    Raw<UPlayerInput> playerInput, replacementInput;
    Raw<UBioSFHandler_PCPartySelection> partyHandler;
    Raw<UBioSFPanel> partyPanel;
    add(inputClass.get(),"PlayerInput",meta.get(),enginePackage.get());
    add(partyClass.get(),"BioSFHandler_PartySelection",meta.get(),sfxPackage.get()); partyClass.get()->SuperField=handlerType.get();
    add(pcPartyClass.get(),"BioSFHandler_PCPartySelection",meta.get(),sfxPackage.get()); pcPartyClass.get()->SuperField=partyClass.get();
    add(playerInput.get(),"CurrentPlayerInput",inputClass.get(),flowPc.get());
    add(replacementInput.get(),"ReplacementPlayerInput",inputClass.get(),flowPc.get());
    add(partyHandler.get(),"CurrentPartyHandler",pcPartyClass.get(),nullptr);
    add(partyPanel.get(),"CurrentPartyPanel",panelType.get(),nullptr); tag(partyPanel.get(),"PartySelect");
    auto beginInput=[&] {
        beginFlow(); flowPc.get()->PlayerInput=playerInput.get(); playerInput.get()->Outer=flowPc.get(); playerInput.get()->Class=inputClass.get();
        flowPc.get()->bCinematicMode=0; flowPc.get()->bIgnoreMoveInput=flowPc.get()->bIgnoreLookInput=0;
        defaultMode.get()->bAllowMovement=defaultMode.get()->bAllowCamera=1; defaultMode.get()->bIsActive=1;
        menuWorldInfo.get()->Pauser=nullptr; menuWorldInfo.get()->bPlayersOnlyPending=0;
    };
    auto captureInput=[&]{return ic::CaptureInputContext(thread);};
    beginInput(); auto initial=captureInput();
    Check(initial.available && initial.category==ic::Category::Default && initial.playerInput.index==playerInput.get()->ObjectInternalInteger,
          "default input context roots current PlayerInput without calls");
    Check(ic::SameContext(initial,captureInput()),"fresh unchanged context compares equal");
    auto oldPosition=resourcePawn.get()->Location; resourcePawn.get()->Location.X+=1;
    Check(ic::SameContext(initial,captureInput()),"ordinary pawn movement does not cancel same-context pulse"); resourcePawn.get()->Location=oldPosition;
    flowPc.get()->PlayerInput=replacementInput.get();
    Check(captureInput().available && !ic::SameContext(initial,captureInput()),"valid replacement PlayerInput invalidates previous context"); flowPc.get()->PlayerInput=playerInput.get();
    auto output=ic::ContextJson(initial);
    Check(output.Bool("available") && output.String("category")=="default" && !output.Bool("dispatchCapability") &&
          !output.Bool("bindingsVerified") && !output.Bool("gameplayEffectProven") && !output.Has("address"),"input JSON is structural evidence only");
    auto reads=engineReadCalls;
    Check(!ic::CaptureInputContext(0).available && engineReadCalls==reads,"wrong input thread never reads engine");
    for(unsigned bad=0;bad<10;++bad) {
        beginInput();
        if(bad==0) flowPc.get()->PlayerInput=nullptr;
        if(bad==1) playerInput.get()->Outer=nullptr;
        if(bad==2) playerInput.get()->Class=controllerType.get();
        if(bad==3) flowPc.get()->bCinematicMode=1;
        if(bad==4) flowPc.get()->bIgnoreMoveInput=1;
        if(bad==5) flowPc.get()->bIgnoreLookInput=2;
        if(bad==6) defaultMode.get()->bAllowMovement=0;
        if(bad==7) defaultMode.get()->bAllowCamera=0;
        if(bad==8) menuWorldInfo.get()->Pauser=reinterpret_cast<APlayerReplicationInfo*>(1);
        if(bad==9) menuWorldInfo.get()->bPlayersOnlyPending=1;
        auto rejected=captureInput(); Check(!rejected.available && !ic::SameContext(initial,rejected),"uncontrollable or invalid input chain rejected");
    }
    beginInput(); focusedWheel(); auto wheelInput=captureInput();
    Check(wheelInput.available && wheelInput.closedWheel && wheelInput.focusHandler.address==reinterpret_cast<std::uintptr_t>(idleWheel.get()),"proved idle HUD wheel reused for input context");
    for(unsigned bad=0;bad<4;++bad) {
        focusedWheel();
        if(bad==0) idleWheel.get()->m_bVisible=1;
        if(bad==1) idleWheel.get()->m_bInOutTransition=1;
        if(bad==2) idleWheel.get()->m_ePowerWheelMode=1;
        if(bad==3) manager.get()->m_oSFPowerWheelHandler=nullptr;
        Check(!captureInput().available,"active transitional or foreign wheel refuses default input");
    }
    auto partyInput=[&] {
        beginInput(); modes.get()->CurrentMode=7; menuWorldInfo.get()->bPlayersOnly=1;
        partyHandler.get()->Class=pcPartyClass.get(); partyHandler.get()->oPanel=partyPanel.get(); partyHandler.get()->oWorldInfo=menuWorldInfo.get();
        partyHandler.get()->m_bSFScreenInitialized=1; partyHandler.get()->m_bNoSelectMode=0;
        partyHandler.get()->m_nPartyMember1=1; partyHandler.get()->m_nPartyMember2=2;
        partyPanel.get()->oParentManager=manager.get(); partyPanel.get()->m_DefaultHandler=partyHandler.get();
        partyPanel.get()->IsVisible=partyPanel.get()->bInitialized=1;
        partyPanel.get()->bToBeRemoved=partyPanel.get()->bInputDisabled=partyPanel.get()->bEventsDisabled=0;
        flowPanels[0]=partyPanel.get(); mevr::ue3::TArray<UBioSFPanel*> h{flowPanels,1,2}; std::memcpy(&manager.get()->m_lstPanels,&h,sizeof(h));
        manager.get()->m_FocusPanel=partyPanel.get();
    };
    partyInput(); auto party=captureInput();
    Check(party.available && party.category==ic::Category::PartySelect && !ic::SameContext(initial,party),"focused interactive exact PCPartySelection admitted structurally");
    Check(ic::ContextJson(party).Number("partyMember1")==1 && party.partyInitialized,"party phase facts copied without calling selection");
    for(unsigned bad=0;bad<9;++bad) {
        partyInput();
        if(bad==0) partyPanel.get()->bInitialized=0;
        if(bad==1) partyPanel.get()->bInputDisabled=1;
        if(bad==2) partyPanel.get()->bEventsDisabled=1;
        if(bad==3) partyPanel.get()->bToBeRemoved=1;
        if(bad==4) partyPanel.get()->IsVisible=0;
        if(bad==5) manager.get()->m_FocusPanel=nullptr;
        if(bad==6) partyHandler.get()->oWorldInfo=nullptr;
        if(bad==7) partyHandler.get()->oPanel=nullptr;
        if(bad==8) partyHandler.get()->Class=partyClass.get();
        Check(!captureInput().available,"inactive malformed foreign or wrong concrete party panel rejected");
    }
    partyInput(); auto savedTag=partyPanel.get()->nmTag; partyPanel.get()->nmTag=otherPanel.get()->nmTag;
    Check(!captureInput().available,"matching handler never authorizes arbitrary panel"); partyPanel.get()->nmTag=savedTag;
    partyInput(); partyHandler.get()->m_nPartyMember1=3;
    Check(!ic::SameContext(party,captureInput()),"party selection transition invalidates prior pulse context");
    partyInput(); UBioSFHandler* scriptHandlers[]{partyHandler.get()};
    mevr::ue3::TArray<UBioSFHandler*> scriptHeader{scriptHandlers,1,1}; std::memcpy(&partyPanel.get()->ScriptHandlers,&scriptHeader,sizeof(scriptHeader));
    Check(captureInput().available && !ic::SameContext(party,captureInput()),"focused handler-list change invalidates context even with same selected handler");
    scriptHeader={nullptr,0,0}; std::memcpy(&partyPanel.get()->ScriptHandlers,&scriptHeader,sizeof(scriptHeader));
    partyInput(); partyHandler.get()->m_bSFScreenInitialized=0;
    auto loadingParty=captureInput();
    Check(!loadingParty.available && !loadingParty.partyInitialized && !ic::SameContext(party,loadingParty),"handler initialization independently required, never GFx completion proof");
    beginInput(); modes.get()->CurrentMode=9;
    Check(!captureInput().available,"galaxy and unsupported mode indices not admitted");
    beginInput(); flowPc.get()->PlayerInput=reinterpret_cast<UPlayerInput*>(1);
    Check(!captureInput().available,"invalid PlayerInput pointer fails closed");
    partyInput(); mevr::ue3::TArray<UBioSFPanel*> oversized{flowPanels,33,33}; std::memcpy(&manager.get()->m_lstPanels,&oversized,sizeof(oversized));
    Check(!captureInput().available,"panel bound checked before traversal");
    beginInput(); static ABioPlayerController* changingPc; changingPc=flowPc.get();
    ic::testing::SetSampleHook(+[]{changingPc->bIgnoreMoveInput=1;});
    Check(captureInput().reason==std::string("input-context-changed-during-sample"),"second read rejects changed controllability");
    ic::testing::SetSampleHook(nullptr);
    partyInput(); static UBioSFHandler_PCPartySelection* changingParty; changingParty=partyHandler.get();
    ic::testing::SetSampleHook(+[]{changingParty->m_nPartyMember2=3;});
    Check(!captureInput().available,"second read rejects party state change"); ic::testing::SetSampleHook(nullptr);

    Raw<UClass> messageClass, pcMessageClass, objectRoot;
    Raw<UBioSFHandler_PCMessageBox> messageHandler;
    Raw<UBioSFPanel> messagePanel, duplicatePartyPanel;
    Raw<UObject> completionTarget;
    add(objectRoot.get(),"Object",meta.get(),corePackage.get());
    add(messageClass.get(),"BioSFHandler_MessageBox",meta.get(),sfxPackage.get()); messageClass.get()->SuperField=handlerType.get();
    add(pcMessageClass.get(),"BioSFHandler_PCMessageBox",meta.get(),sfxPackage.get()); pcMessageClass.get()->SuperField=messageClass.get();
    add(messageHandler.get(),"CurrentMessageHandler",pcMessageClass.get(),nullptr);
    add(messagePanel.get(),"CurrentMessagePanel",panelType.get(),nullptr); tag(messagePanel.get(),"MessageBox");
    add(duplicatePartyPanel.get(),"DuplicatePartyPanel",panelType.get(),nullptr); tag(duplicatePartyPanel.get(),"PartySelect");
    add(completionTarget.get(),"CompletionTarget",objectRoot.get(),nullptr);
    std::array<FPartySelectMemberInfo,3> members{};
    auto setName=[&](SFXName& target,char const* name) { Raw<UObject> named; Name(named.get(),name); target=named.get()->Name; };
    setName(members[1].InPartyLabel,"SyntheticPartyLabelOne"); setName(members[2].InPartyLabel,"SyntheticPartyLabelTwo");
    UBioSFPanel* confirmationPanels[]{partyPanel.get(),messagePanel.get(),duplicatePartyPanel.get()};
    auto confirmationInput=[&] {
        partyInput();
        messageHandler.get()->Class=pcMessageClass.get(); messageHandler.get()->oPanel=messagePanel.get();
        messageHandler.get()->oWorldInfo=menuWorldInfo.get();
        messageHandler.get()->__InputCallback__Delegate.Object=partyHandler.get();
        setName(messageHandler.get()->__InputCallback__Delegate.FunctionName,"FinalizeParty");
        messageHandler.get()->nInputCallbackContext=0; messageHandler.get()->m_nHiddenDepth=0;
        messageHandler.get()->bHandleInput=1; messageHandler.get()->m_bIsModal=1;
        messageHandler.get()->m_bCached=messageHandler.get()->m_bDisplayingHint=0;
        messagePanel.get()->oParentManager=manager.get(); messagePanel.get()->m_DefaultHandler=messageHandler.get();
        messagePanel.get()->IsVisible=messagePanel.get()->bInitialized=1;
        messagePanel.get()->bToBeRemoved=messagePanel.get()->bInputDisabled=messagePanel.get()->bEventsDisabled=0;
        manager.get()->m_FocusPanel=messagePanel.get(); manager.get()->m_oMessageBox=messageHandler.get();
        mevr::ue3::TArray<UBioSFPanel*> panelsHeader{confirmationPanels,2,3};
        std::memcpy(&manager.get()->m_lstPanels,&panelsHeader,sizeof(panelsHeader));
        mevr::ue3::TArray<FPartySelectMemberInfo> memberHeader{members.data(),3,3};
        std::memcpy(&partyHandler.get()->lstMemberInfo,&memberHeader,sizeof(memberHeader));
        partyHandler.get()->__ExternalCallback_OnComplete__Delegate.Object=nullptr;
        setName(partyHandler.get()->__ExternalCallback_OnComplete__Delegate.FunctionName,"None");
    };
    confirmationInput(); auto confirmation=captureInput();
    Check(confirmation.available && confirmation.category==ic::Category::PartyConfirm &&
        confirmation.confirmationCallback.address==reinterpret_cast<std::uintptr_t>(partyHandler.get()),"exact party-bound message callback admitted without calls");
    auto confirmJson=ic::ContextJson(confirmation);
    Check(confirmJson.String("category")=="party-confirm" && confirmJson.Get("partyConfirmation").String("callback")=="FinalizeParty" &&
        !confirmJson.Get("partyConfirmation").Bool("completionDestinationProven",true),"confirmation output exposes source relation without destination claim");
    Check(!ic::SameContext(party,confirmation),"party to modal confirmation invalidates previous pulse");
    partyPanel.get()->bInputDisabled=1; partyPanel.get()->IsVisible=0;
    Check(captureInput().available,"modal parent need not be focused or interactive");
    for(unsigned bad=0;bad<21;++bad) {
        confirmationInput();
        if(bad==0) messageHandler.get()->__InputCallback__Delegate.Object=nullptr;
        if(bad==1) messageHandler.get()->__InputCallback__Delegate.Object=partyPanel.get();
        if(bad==2) setName(messageHandler.get()->__InputCallback__Delegate.FunctionName,"UnrelatedConfirmation");
        if(bad==3) messageHandler.get()->__InputCallback__Delegate.FunctionName.Number=1;
        if(bad==4) messageHandler.get()->nInputCallbackContext=1;
        if(bad==5) messageHandler.get()->oWorldInfo=nullptr;
        if(bad==6) messageHandler.get()->oPanel=nullptr;
        if(bad==7) messageHandler.get()->Class=messageClass.get();
        if(bad==8) messagePanel.get()->bInputDisabled=1;
        if(bad==9) messagePanel.get()->bEventsDisabled=1;
        if(bad==10) messagePanel.get()->bInitialized=0;
        if(bad==11) messagePanel.get()->bToBeRemoved=1;
        if(bad==12) messagePanel.get()->IsVisible=0;
        if(bad==13) partyPanel.get()->bInitialized=0;
        if(bad==14) partyPanel.get()->bToBeRemoved=1;
        if(bad==15) partyHandler.get()->m_bSFScreenInitialized=0;
        if(bad==16) partyHandler.get()->m_nPartyMember1=3;
        if(bad==17) partyHandler.get()->m_nPartyMember2=-2;
        if(bad==18) { mevr::ue3::TArray<FPartySelectMemberInfo> invalidMembers{members.data(),33,33}; std::memcpy(&partyHandler.get()->lstMemberInfo,&invalidMembers,16); }
        if(bad==19) { mevr::ue3::TArray<FPartySelectMemberInfo> invalidMembers{reinterpret_cast<FPartySelectMemberInfo*>(1),3,3}; std::memcpy(&partyHandler.get()->lstMemberInfo,&invalidMembers,16); }
        if(bad==20) partyHandler.get()->__ExternalCallback_OnComplete__Delegate.Object=reinterpret_cast<UObject*>(1);
        Check(!captureInput().available,"unrelated unsafe or malformed confirmation context rejected");
    }
    confirmationInput(); auto messageTag=messagePanel.get()->nmTag; tag(messagePanel.get(),"Other");
    Check(!captureInput().available,"party callback on arbitrary tagged panel not admitted"); messagePanel.get()->nmTag=messageTag;
    confirmationInput();
    duplicatePartyPanel.get()->oParentManager=manager.get();
    mevr::ue3::TArray<UBioSFPanel*> duplicateHeader{confirmationPanels,3,3}; std::memcpy(&manager.get()->m_lstPanels,&duplicateHeader,16);
    Check(!captureInput().available,"duplicate party panel rejected before delegate interpretation");
    confirmationInput(); tag(duplicatePartyPanel.get(),"MessageBox"); std::memcpy(&manager.get()->m_lstPanels,&duplicateHeader,16);
    Check(!captureInput().available,"duplicate message panel rejected");
    confirmationInput(); partyHandler.get()->m_nPartyMember1=partyHandler.get()->m_nPartyMember2=-1;
    Check(captureInput().available,"source-permitted absent selected members retained without invented minimum");
    confirmationInput(); partyHandler.get()->__ExternalCallback_OnComplete__Delegate.Object=completionTarget.get();
    setName(partyHandler.get()->__ExternalCallback_OnComplete__Delegate.FunctionName,"SyntheticCompletion");
    auto completion=captureInput();
    Check(completion.available && completion.partyCompletionCallback.index==completionTarget.get()->ObjectInternalInteger &&
        !ic::SameContext(confirmation,completion),"valid external completion identity retained without claiming destination");
    confirmationInput(); messageHandler.get()->m_bCached=1;
    Check(captureInput().available && !ic::SameContext(confirmation,captureInput()),"message state change invalidates context");
    confirmationInput(); static FPartySelectMemberInfo* changingMembers; changingMembers=members.data();
    auto oldLabel=members[1].InPartyLabel;
    ic::testing::SetSampleHook(+[]{changingMembers[1].InPartyLabel=changingMembers[2].InPartyLabel;});
    Check(!captureInput().available,"selected plot label replacement between reads rejects context");
    ic::testing::SetSampleHook(nullptr); members[1].InPartyLabel=oldLabel;
    confirmationInput(); static UBioSFHandler_PCMessageBox* changingMessage; changingMessage=messageHandler.get();
    ic::testing::SetSampleHook(+[]{changingMessage->__InputCallback__Delegate.Object=nullptr;});
    Check(!captureInput().available,"callback replacement between reads rejects context"); ic::testing::SetSampleHook(nullptr);
    beginInput(); Check(captureInput().available && flowDispatchCalls==0,"read-only context recovers without dispatch or save-flow receipts");
}

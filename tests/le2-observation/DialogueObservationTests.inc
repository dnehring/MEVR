// SPDX-License-Identifier: MIT
// Synthetic SDK objects, never game extracts. Snapshot only; no dispatch.
{
    using mevr::engine::le2_diagnostic::ObserveDialogue;
    using mevr::engine::le2_diagnostic::testing::SetDialogueSampleHook;
    Raw<UClass> convType, convHandlerType;
    Raw<UBioConversation> conv;
    Raw<UBioSFHandler_Conversation> convHandler;
    Raw<UBioSFPanel> convPanel;
    add(convType.get(),"BioConversation",meta.get(),sfxPackage.get());
    add(convHandlerType.get(),"BioSFHandler_Conversation",meta.get(),sfxPackage.get());
    convHandlerType.get()->SuperField=handlerType.get();
    add(conv.get(),"SyntheticConversation",convType.get(),mapPackage.get());
    add(convHandler.get(),"SyntheticConversationHandler",convHandlerType.get(),nullptr);
    add(convPanel.get(),"SyntheticConversationPanel",panelType.get(),nullptr);
    convPanel.get()->nmTag=convPanel.get()->Name;
    Raw<FBioDialogEntryNode> entry; Raw<FBioDialogReplyNode> reply;
    entry.get()->nExportID=101; reply.get()->nExportID=202; reply.get()->nStateTransition=303;
    struct Header { void* data; unsigned count,capacity; };
    Header eh{entry.get(),1,1}, rh{reply.get(),1,1};
    std::memcpy(&conv.get()->m_EntryList,&eh,sizeof(eh)); std::memcpy(&conv.get()->m_ReplyList,&rh,sizeof(rh));
    conv.get()->m_nCurrentEntry=0; conv.get()->m_nCurrentReply=-1; conv.get()->m_nSelectedReply=-1;
    conv.get()->m_tInterruptInfo.nReplyListIndex=0; conv.get()->m_tInterruptInfo.eInterruptType=1;
    conv.get()->m_tInterruptInfo.bEnabled=1; conv.get()->m_tInterruptInfo.fWindowTimeRemaining=2.5f;
    menuWorldInfo.get()->m_oCurrentConversation=conv.get();
    Header savedPanels{}; std::memcpy(&savedPanels,&manager.get()->m_lstPanels,sizeof(savedPanels));
    auto* savedFocus=manager.get()->m_FocusPanel;
    UBioSFPanel* dialoguePanels[2]{convPanel.get(),convPanel.get()}; Header ph{dialoguePanels,1,2};
    std::memcpy(&manager.get()->m_lstPanels,&ph,sizeof(ph)); manager.get()->m_FocusPanel=convPanel.get();
    manager.get()->m_oConversationHandler=convHandler.get();
    convPanel.get()->oParentManager=manager.get(); convPanel.get()->m_DefaultHandler=convHandler.get();
    convPanel.get()->IsVisible=convPanel.get()->bInitialized=convPanel.get()->bUsingGamepad=1;
    convHandler.get()->oPanel=convPanel.get(); convHandler.get()->oWorldInfo=menuWorldInfo.get();
    convHandler.get()->m_bDisplayingParagonInterrupt=convHandler.get()->m_bSFScreenInitialized=1;
    auto observeDialogue=[&]{return ObserveDialogue(thread);};
    auto initial=observeDialogue();
    if(!initial.Bool("available") || !initial.Get("gui").Bool("available")) std::cerr<<mevr::json::Write(initial,0)<<"\n";
    Check(initial.Bool("available") && initial.Get("gui").Bool("available"),"dialogue validated conversation and GUI snapshot");
    Check(initial.Get("conversation").Get("currentEntry").Number("exportId")==101 &&
        !initial.Get("conversation").Get("selectedReply").Bool("available") && initial.Get("interrupt").Get("reply").Number("stateTransition")==303,
        "dialogue selected node IDs distinguished from absent replies and array indices");
    Check(!initial.Bool("dispatchCapability") && !initial.Bool("legalityEvaluated") && !initial.Bool("acceptedInterruptProven") && !initial.Bool("historyComplete"),"dialogue snapshot never claims legality acceptance history or calls");
    Check(initial.Get("conversation").String("name")=="SyntheticConversation" && initial.Get("conversation").Get("package").Bool("available"),"dialogue bounded name and direct package provenance");
    conv.get()->Outer=nullptr;
    Check(observeDialogue().Bool("available") && !observeDialogue().Get("conversation").Get("package").Bool("available"),"dialogue missing direct package stays independently unavailable");
    conv.get()->Outer=mapPackage.get();
    conv.get()->Name.Number=7;
    Check(observeDialogue().Bool("available") && observeDialogue().Get("conversation").Number("nameNumber")==7 &&
        observeDialogue().Get("conversation").String("name")=="SyntheticConversation","dialogue numbered name retains base and raw instance number");
    conv.get()->Name.Number=0;
    conv.get()->Class=worldType.get(); Check(!observeDialogue().Bool("available"),"dialogue foreign object type refused"); conv.get()->Class=convType.get();
    auto* previousPawn=controller.get()->Pawn; controller.get()->Pawn=nullptr;
    Check(observeDialogue().Bool("available") && !observeDialogue().Get("context").Get("pawn").Bool("available"),"dialogue optional absent pawn preserves controller conversation"); controller.get()->Pawn=previousPawn;
    conv.get()->m_nSelectedReply=1; Check(!observeDialogue().Bool("available"),"dialogue selected reply bounds enforced"); conv.get()->m_nSelectedReply=-1;
    conv.get()->m_tInterruptInfo.nReplyListIndex=-2; Check(!observeDialogue().Bool("available"),"dialogue interrupt reply bounds enforced"); conv.get()->m_tInterruptInfo.nReplyListIndex=0;
    convHandler.get()->m_bWasTriggered=1;
    Check(observeDialogue().Get("gui").Bool("wasTriggered") && !observeDialogue().Bool("acceptedInterruptProven"),"GUI trigger not accepted reply proof");
    convHandler.get()->m_bWasTriggered=0;
    manager.get()->m_oConversationHandler=nullptr;
    Check(observeDialogue().Bool("available") && !observeDialogue().Get("gui").Bool("available"),"missing GUI cache preserves current conversation");
    manager.get()->m_oConversationHandler=convHandler.get();
    convHandler.get()->oWorldInfo=nullptr;
    Check(!observeDialogue().Get("gui").Bool("available"),"foreign GUI world unavailable"); convHandler.get()->oWorldInfo=menuWorldInfo.get();
    ph.count=2; std::memcpy(&manager.get()->m_lstPanels,&ph,sizeof(ph));
    Check(!observeDialogue().Get("gui").Bool("available"),"duplicate reachable panel refused");
    ph.count=1; std::memcpy(&manager.get()->m_lstPanels,&ph,sizeof(ph));
    for(int index : {-2,1,INT_MAX}) {
        conv.get()->m_nCurrentEntry=index; Check(!observeDialogue().Bool("available"),"dialogue invalid current node index refused");
    }
    conv.get()->m_nCurrentEntry=-1;
    Check(observeDialogue().Bool("available") && !observeDialogue().Get("conversation").Get("currentEntry").Bool("available"),"dialogue minus-one node explicitly absent");
    conv.get()->m_nCurrentEntry=0;
    eh.capacity=0; std::memcpy(&conv.get()->m_EntryList,&eh,sizeof(eh)); Check(!observeDialogue().Bool("available"),"dialogue malformed node array refused");
    eh.capacity=eh.count=4097; std::memcpy(&conv.get()->m_EntryList,&eh,sizeof(eh)); Check(!observeDialogue().Bool("available"),"dialogue node bound enforced before body access");
    eh.count=eh.capacity=1; eh.data=reinterpret_cast<void*>(1); std::memcpy(&conv.get()->m_EntryList,&eh,sizeof(eh));
    Check(observeDialogue().String("reason")=="guarded-dialogue-read-fault","dialogue unreadable selected row contained by SEH");
    eh.data=entry.get(); std::memcpy(&conv.get()->m_EntryList,&eh,sizeof(eh));
    conv.get()->m_tInterruptInfo.fWindowTimeRemaining=std::numeric_limits<float>::quiet_NaN();
    Check(!observeDialogue().Bool("available"),"dialogue nonfinite timer refused");
    conv.get()->m_tInterruptInfo.fWindowTimeRemaining=-1;
    Check(observeDialogue().Bool("available") && !observeDialogue().Bool("legalityEvaluated"),"negative timer reported without invented eligibility");
    conv.get()->m_tInterruptInfo.eInterruptType=255;
    Check(observeDialogue().Get("interrupt").String("typeName")=="unknown","unknown interrupt enum not invented polarity"); conv.get()->m_tInterruptInfo.eInterruptType=1;
    dialogueMutationTarget=conv.get(); dialogueMutationNode=entry.get(); SetDialogueSampleHook(MutateDialogueSample);
    auto convIndex=conv.get()->ObjectInternalInteger;
    for(int mutation : {1,2,3,4,5}) {
        dialogueMutationKind=mutation;
        Check(observeDialogue().String("reason")=="dialogue-context-or-state-changed","dialogue reread catches node body index identity and activation changes");
        conv.get()->m_nCurrentEntry=0; conv.get()->Name.Number=0; conv.get()->ObjectInternalInteger=convIndex; conv.get()->m_tInterruptInfo.bActivated=0; entry.get()->nExportID=101;
    }
    SetDialogueSampleHook(nullptr);
    menuWorldInfo.get()->m_oCurrentConversation=nullptr;
    Check(observeDialogue().String("reason")=="no-current-conversation","missing conversation explicit");
    menuWorldInfo.get()->m_oCurrentConversation=conv.get();
    auto reads=engineReadCalls;
    Check(ObserveDialogue(0).String("reason")=="not-on-verified-game-thread" && engineReadCalls==reads,"dialogue zero thread performs no reads");
    Check(ObserveDialogue(thread+1).String("reason")=="not-on-verified-game-thread" && engineReadCalls==reads,"dialogue wrong thread performs no reads");
    auto bytes=mevr::json::Write(observeDialogue(),0).size(); Check(bytes<14*1024,"dialogue compact snapshot fits host envelope budget");
    std::cout<<"Representative dialogue observation bytes: "<<bytes<<'\n';
    #include "DialogueHistoryTests.inc"
    menuWorldInfo.get()->m_oCurrentConversation=nullptr; manager.get()->m_oConversationHandler=nullptr;
    std::memcpy(&manager.get()->m_lstPanels,&savedPanels,sizeof(savedPanels)); manager.get()->m_FocusPanel=savedFocus;
}

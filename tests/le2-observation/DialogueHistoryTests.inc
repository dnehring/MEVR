// SPDX-License-Identifier: MIT
// Runs inside the synthetic conversation fixture; no native process or input.
{
    using mevr::engine::le2_diagnostic::DialogueHistory;
    auto history=std::make_unique<DialogueHistory>();
    auto pre=engineReadCalls;
    auto empty=history->Snapshot();
    Check(!empty.Bool("started") && empty.String("reason")=="never-started" && empty.Get("rows").Items().empty(),"history never-started explicit");
    Check(!history->Start(0,0,0) && !history->Start(0,0,thread+1) && !history->Start(UINT64_MAX,0,thread) &&
        !history->Start(0,UINT64_MAX,thread) && engineReadCalls==pre,"history rejects wrong thread and clock overflow without reads");
    Check(history->Start(100,1000,thread) && history->Active() && engineReadCalls==pre,"history Start opts in without reading engine");
    auto started=mevr::json::Write(history->Snapshot(),0);
    Check(!history->Start(100,1000,thread) && mevr::json::Write(history->Snapshot(),0)==started,"active history cannot rearm or clear evidence");
    history->Tick(100,1001,thread); // intentional first post-Poll call in same game tick
    Check(history->Snapshot().Number("sampledRows")==1,"first same-frame history Tick samples");
    auto historyRow=history->Snapshot().Get("rows").Items().front();
    Check(historyRow.Items()[0].AsNumber()==0 && historyRow.Items()[1].AsNumber()==1 && historyRow.Items()[2].AsNumber()==0 &&
        historyRow.Items()[4].AsNumber()==0 && historyRow.Items()[5].AsNumber()==101 && historyRow.Items()[9].IsNull(),"history compact schema preserves node indices and absent export null");
    pre=engineReadCalls; history->Tick(101,1050,thread);
    Check(engineReadCalls==pre && history->Snapshot().Number("cadenceSkippedTicks")==1,"history cadence skips engine traversal");
    history->Tick(102,1501,thread);
    Check(history->Snapshot().Number("missedCadenceSlots")==3 && history->Snapshot().Number("gapCount")==1 &&
        history->Snapshot().Number("sampledRows")==2,"delayed tick records gap without catch-up fabricated samples");
    pre=engineReadCalls; history->Tick(103,1600,thread+1);
    Check(history->Active() && engineReadCalls==pre && history->Snapshot().Number("sampledRows")==2,"wrong-thread history Tick leaves owner state untouched");
    menuWorldInfo.get()->m_oCurrentConversation=nullptr; history->Tick(103,1626,thread);
    historyRow=history->Snapshot().Get("rows").Items().back();
    Check(historyRow.Items()[2].AsNumber()==1 && historyRow.Items()[3].AsNumber()==0 && historyRow.Items()[4].IsNull() && historyRow.Items()[17].IsNull(),"missing conversation historyRow has no invented state");
    menuWorldInfo.get()->m_oCurrentConversation=conv.get();
    conv.get()->m_fNodePlayTimer=std::numeric_limits<float>::quiet_NaN(); history->Tick(104,1751,thread);
    Check(history->Snapshot().Get("rows").Items().back().Items()[2].AsNumber()==3,"history unavailable malformed state distinct from no conversation"); conv.get()->m_fNodePlayTimer=0;
    menuWorldInfo.get()->m_oCurrentConversation=reinterpret_cast<UBioConversation*>(1); history->Tick(105,1876,thread);
    Check(history->Snapshot().Get("rows").Items().back().Items()[2].AsNumber()==2,"history guarded read fault recorded"); menuWorldInfo.get()->m_oCurrentConversation=conv.get();
    dialogueMutationKind=4; SetDialogueSampleHook(MutateDialogueSample); history->Tick(106,2001,thread);
    Check(history->Snapshot().Get("rows").Items().back().Items()[2].AsNumber()==4,"history unstable sample explicitly unavailable");
    SetDialogueSampleHook(nullptr); conv.get()->m_tInterruptInfo.bActivated=0;
    convHandler.get()->m_bWasTriggered=1; history->Tick(107,2126,thread);
    auto snapshot=history->Snapshot();
    Check(snapshot.Number("unavailableRows")==4 && (static_cast<int>(snapshot.Get("rows").Items().back().Items()[14].AsNumber())&2048)!=0 &&
        !snapshot.Bool("acceptedInterruptProven") && !snapshot.Bool("legalityEvaluated") && !snapshot.Bool("historyComplete"),"history structural trigger sample does not imply accepted input");
    convHandler.get()->m_bWasTriggered=0;
    pre=engineReadCalls; history->Stop(); auto frozenHistory=mevr::json::Write(history->Snapshot(),0);
    history->Tick(108,2251,thread); history->Stop();
    Check(engineReadCalls==pre && mevr::json::Write(history->Snapshot(),0)==frozenHistory && history->Snapshot().String("endMonoMs")=="2126", "Stop freezes history at last valid tick clock without reads");
    Check(history->Start(200,5000,thread) && history->Snapshot().Get("rows").Items().empty(),"explicit completed restart clears old run evidence");
    history->Tick(201,5001,thread); pre=engineReadCalls; history->Tick(202,5000,thread);
    Check(!history->Active() && history->Snapshot().String("reason")=="clock-invalid" && engineReadCalls==pre,"monotonic rollback stops without reads");
    Check(history->Start(300,6000,thread),"restart after clock refusal"); history->Tick(300,6001,thread); pre=engineReadCalls; history->Tick(300,6126,thread);
    Check(history->Snapshot().String("reason")=="clock-invalid" && engineReadCalls==pre,"second same game tick refused");
    Check(history->Start(400,7000,thread),"start expiration case"); pre=engineReadCalls; history->Tick(401,17000,thread);
    Check(history->Snapshot().String("reason")=="expired" && history->Snapshot().Number("sampledRows")==0 && engineReadCalls==pre,"duration expiry never samples outside window");
    Check(history->Start(500,20000,thread),"start context epoch case");
    for(unsigned i=0;i<5;++i) { conv.get()->Name.Number=i; history->Tick(500+i,20000+i*125,thread); }
    Check(history->Snapshot().String("reason")=="context-limit" && history->Snapshot().Get("epochs").Items().size()==4 &&
        history->Snapshot().Number("sampledRows")==4,"identity epoch dictionary bounded without silently relabeling context");
    conv.get()->Name.Number=0;
    Check(history->Start(600,30000,thread),"start full bounded window");
    for(unsigned i=0;i<80;++i) history->Tick(600+i,30000+i*125,thread);
    auto full=history->Snapshot();
    Check(full.Number("sampledRows")==80 && full.Number("bufferDroppedRows")==0 && full.Get("rows").Items().front().Items()[1].AsNumber()==0 &&
        full.Get("rows").Items().back().Items()[1].AsNumber()==9875,"bounded window preserves first and latest samples");
    Check(full.Get("rows").Items().size()+static_cast<unsigned>(full.Number("serializationOmittedRows"))==80 && mevr::json::Write(full,0).size()<=10*1024,"history budget omissions precisely accounted");
    history->Tick(680,40000,thread); Check(!history->Active() && history->Snapshot().String("reason")=="expired","full window closes on next boundary tick");
    // Extreme node integers and escaped bounded identity labels force size policy.
    auto originalName=conv.get()->Name; auto* originalClass=conv.get()->Class;
    Raw<UClass> longConvType; std::string longLabel(120,'"');
    add(longConvType.get(),longLabel.c_str(),meta.get(),sfxPackage.get()); longConvType.get()->SuperField=convType.get();
    conv.get()->Class=longConvType.get(); std::string longName(127,'"'); Name(conv.get(),longName.c_str());
    entry.get()->nExportID=INT_MIN; reply.get()->nExportID=INT_MAX;
    conv.get()->m_nCurrentReply=conv.get()->m_nSelectedReply=0;
    conv.get()->m_fNodePlayTimer=std::numeric_limits<float>::max(); conv.get()->m_tInterruptInfo.fWindowStartTimeRemaining=-std::numeric_limits<float>::max();
    Check(history->Start(700,50000,thread),"start extreme budget case");
    for(unsigned i=0;i<80;++i) { conv.get()->Name.Number=i/20; history->Tick(700+i,50000+i*125,thread); }
    auto extreme=history->Snapshot();
    Check(extreme.Number("sampledRows")==80 && extreme.Get("epochs").Items().size()==4 &&
        extreme.Number("serializationOmittedRows")>0 && mevr::json::Write(extreme,0).size()<=10*1024,
        "worst labels and numbers enforce compact budget with counted omissions");
    Check(extreme.Get("rows").Items().front().Items()[1].AsNumber()==0 && extreme.Get("rows").Items().back().Items()[1].AsNumber()==9875,
        "budget trimming preserves first and latest endpoints");
    std::cout<<"Dialogue history full bytes: "<<mevr::json::Write(full,0).size()<<"; extreme bytes: "<<mevr::json::Write(extreme,0).size()<<'\n';
    history->Stop();
    std::string oversizedLabel(121,'"'); Name(longConvType.get(),oversizedLabel.c_str());
    Check(history->Start(800,60000,thread),"start label limit case"); history->Tick(800,60000,thread);
    Check(history->Snapshot().String("reason")=="history-label-limit" && history->Snapshot().Get("epochs").Items().empty(),"history rejects oversized dictionary identity before storing it");
    conv.get()->Class=originalClass; conv.get()->Name=originalName; entry.get()->nExportID=101; reply.get()->nExportID=202;
    conv.get()->m_nCurrentReply=conv.get()->m_nSelectedReply=-1; conv.get()->m_fNodePlayTimer=0; conv.get()->m_tInterruptInfo.fWindowStartTimeRemaining=0;
}

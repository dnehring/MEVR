// SPDX-License-Identifier: MIT
// Included once, reusing the established SDK/object-table/SEH read boundary.
namespace {
static_assert(offsetof(ABioWorldInfo,m_oCurrentConversation)==0xA4C);
static_assert(offsetof(UBioConversation,m_tInterruptInfo)==0x2A0);
static_assert(offsetof(UBioConversation,m_nCurrentEntry)==0x324);
static_assert(offsetof(UBioConversation,m_nSelectedReply)==0x32C);
static_assert(offsetof(UBioConversation,m_nCurrentReply)==0x330);
static_assert(sizeof(FBioInterruptReplyInfo)==0x14);
static_assert(offsetof(FBioInterruptReplyInfo,eInterruptType)==0x10);
static_assert(offsetof(FBioDialogNode,nExportID)==0x2C);
constexpr unsigned kDialogueNodes=4096;
// The generated SDK emits EInterruptionType as a comment. The standalone SDK
// configuration checks its declaration; unknown runtime bytes remain unknown.
constexpr unsigned kInterruptRenegade=0, kInterruptParagon=1;
struct DialogueNode {
    int index{-1}, exportId{}, transition{}, parameter{};
    bool available{};
    bool operator==(DialogueNode const&) const = default;
};
struct DialogueData {
    std::uintptr_t entries{}, replies{}, objectTable{};
    unsigned objectCount{}, objectCapacity{};
    std::uint64_t nameBits{};
    int nameNumber{};
    unsigned entryCount{}, entryCapacity{}, replyCount{}, replyCapacity{};
    DialogueNode entry, reply, selected, interruptReply;
    float nodeTimer{}, startRemaining{}, windowRemaining{};
    unsigned type{};
    bool enabled{}, activated{};
    bool operator==(DialogueData const&) const = default;
};
struct DialogueSample {
    MenuSample menu;
    ObjectSample worldInfo, conversation, package;
    DialogueData data;
    char conversationName[kNameBytes]{}, packageName[kNameBytes]{};
    MenuPanel gui;
    bool available{}, guiAvailable{}, wheel{}, paragon{}, renegade{}, initialized{}, triggered{}, usingGamepad{};
    char const* reason{"dialogue-player-context-unavailable"};
    char const* guiReason{"dialogue-gui-context-unavailable"};
};
static_assert(std::is_trivially_destructible_v<DialogueSample>);
#if defined(MEVR_LE2_OBSERVATION_TEST)
void (*gDialogueSampleHook)(){};
#endif
// -1 is absence, not index zero. Other invalid values fail the snapshot.
template<class T> bool DialogueNodeRead(TArray<T> const& nodes,int index,DialogueNode& out) {
    out.index=index;
    if(index==-1) return true;
    if(index<0 || static_cast<unsigned>(index)>=nodes.Count()) return false;
    auto const& node=nodes.GetData()[index];
    out.available=true; out.exportId=node.nExportID;
    out.transition=node.nStateTransition; out.parameter=node.nStateTransitionParam;
    return true;
}
void ReadDialogue(void* context) {
    auto& s=*static_cast<DialogueSample*>(context);
    ReadMenu(&s.menu);
    auto const& root=s.menu.root;
    if(!root.world.available || !root.localPlayer.available || !root.player.available) return;
    auto const* objects=UObject::GObjObjects;
    s.reason="dialogue-object-table-invalid";
    if(!objects || !BoundedArray(*objects,kMaxObjects) || objects->Count()!=root.objectCount) return;
    Table table{objects->GetData(),objects->Count()};
    auto const* controller=reinterpret_cast<APlayerController const*>(root.player.address);
    s.reason="dialogue-world-info-unavailable";
    if(!ReadObject(table,controller->WorldInfo,"SFXGame","BioWorldInfo",s.worldInfo)) return;
    if(root.pawn.available) {
        auto const* pawn=reinterpret_cast<APawn const*>(root.pawn.address);
        s.reason="dialogue-pawn-world-or-controller-disagrees";
        if(pawn->WorldInfo!=controller->WorldInfo || pawn->Controller!=controller) return;
    }
    auto const* world=static_cast<ABioWorldInfo const*>(controller->WorldInfo);
    s.reason="no-current-conversation";
    auto const* conv=world->m_oCurrentConversation;
    if(!conv) return;
    s.reason="dialogue-conversation-identity-invalid";
    if(!ReadObject(table,conv,"SFXGame","BioConversation",s.conversation)) return;
    s.reason="dialogue-conversation-name-unavailable";
    // Reuse weapon observer's numbered-instance pattern: label base name and
    // raw number separately, without constructing a name or allocating in SDK.
    auto baseName=conv->Name; std::memcpy(&s.data.nameBits,&baseName,sizeof(baseName));
    s.data.nameNumber=baseName.Number; baseName.Number=0;
    if(!NameText(baseName,s.conversationName)) return;
    // Optional exact direct package only; no invented outer-chain containment.
    auto const* package=conv->Outer;
    if(package && table.Listed(package,true) && IsA(table,package,"Core","Package") && package->Outer==nullptr &&
       NameText(package->Name,s.packageName)) ReadObject(table,package,"Core","Package",s.package);
    s.reason="dialogue-node-array-invalid-or-limit";
    if(!BoundedArray(conv->m_EntryList,kDialogueNodes) || !BoundedArray(conv->m_ReplyList,kDialogueNodes)) return;
    auto& d=s.data;
    d.objectTable=reinterpret_cast<std::uintptr_t>(objects->GetData());
    d.objectCount=objects->Count(); d.objectCapacity=objects->Capacity();
    d.entries=reinterpret_cast<std::uintptr_t>(conv->m_EntryList.GetData());
    d.replies=reinterpret_cast<std::uintptr_t>(conv->m_ReplyList.GetData());
    d.entryCount=conv->m_EntryList.Count(); d.entryCapacity=conv->m_EntryList.Capacity();
    d.replyCount=conv->m_ReplyList.Count(); d.replyCapacity=conv->m_ReplyList.Capacity();
    s.reason="dialogue-node-index-invalid";
    if(!DialogueNodeRead(conv->m_EntryList,conv->m_nCurrentEntry,d.entry) ||
       !DialogueNodeRead(conv->m_ReplyList,conv->m_nCurrentReply,d.reply) ||
       !DialogueNodeRead(conv->m_ReplyList,conv->m_nSelectedReply,d.selected) ||
       !DialogueNodeRead(conv->m_ReplyList,conv->m_tInterruptInfo.nReplyListIndex,d.interruptReply)) return;
    d.nodeTimer=conv->m_fNodePlayTimer;
    d.startRemaining=conv->m_tInterruptInfo.fWindowStartTimeRemaining;
    d.windowRemaining=conv->m_tInterruptInfo.fWindowTimeRemaining;
    d.enabled=conv->m_tInterruptInfo.bEnabled; d.activated=conv->m_tInterruptInfo.bActivated;
    d.type=conv->m_tInterruptInfo.eInterruptType;
    s.reason="dialogue-nonfinite-timer";
    if(!std::isfinite(d.nodeTimer) || !std::isfinite(d.startRemaining) || !std::isfinite(d.windowRemaining)) return;
    s.available=true; s.reason="stable-current-conversation-fields-only";
    // GUI is independent: a valid conversation can exist before its panel/cache.
    if(!s.menu.contextAvailable) { s.guiReason=s.menu.reason; return; }
    s.guiReason="dialogue-manager-world-disagrees";
    if(s.menu.worldInfo.address!=s.worldInfo.address) return;
    auto const* manager=reinterpret_cast<UMassEffectGuiManager const*>(s.menu.manager.address);
    auto const* handler=manager->m_oConversationHandler;
    s.guiReason="dialogue-cached-handler-unavailable";
    ObjectSample h;
    if(!ReadObject(table,handler,"SFXGame","BioSFHandler_Conversation",h)) return;
    auto const* panel=handler->oPanel;
    unsigned matches=0;
    for(unsigned i=0;i<manager->m_lstPanels.Count();++i) if(manager->m_lstPanels.GetData()[i]==panel) ++matches;
    s.guiReason="dialogue-panel-not-uniquely-reachable";
    if(matches!=1) return;
    s.guiReason="dialogue-handler-panel-or-world-disagrees";
    if(!PanelContext(table,panel,manager,"BioSFHandler_Conversation",s.gui,false) ||
       s.gui.handler.address!=h.address || s.gui.worldInfo.address!=s.worldInfo.address) return;
    s.wheel=handler->m_bDisplayingWheel; s.paragon=handler->m_bDisplayingParagonInterrupt;
    s.renegade=handler->m_bDIsplayingRenegadeInterrupt; s.initialized=handler->m_bSFScreenInitialized;
    s.triggered=handler->m_bWasTriggered; s.usingGamepad=panel->bUsingGamepad;
    s.guiAvailable=true; s.guiReason="reachable-cached-handler-display-state-only";
}
bool SameDialogueObject(ObjectSample const& a,ObjectSample const& b) {
    return SameObjectSample(a,b) && std::strcmp(a.className,b.className)==0 && std::strcmp(a.reason,b.reason)==0;
}
bool SameDialogue(DialogueSample const& a,DialogueSample const& b) {
    auto const& x=a.menu.root; auto const& y=b.menu.root;
    if(!b.available || !(a.data==b.data) || a.guiAvailable!=b.guiAvailable ||
       std::strcmp(a.conversationName,b.conversationName)!=0 || std::strcmp(a.packageName,b.packageName)!=0 ||
       std::strcmp(a.guiReason,b.guiReason)!=0 ||
       !SameDialogueObject(x.world,y.world) || !SameDialogueObject(x.engine,y.engine) ||
       !SameDialogueObject(x.localPlayer,y.localPlayer) || !SameDialogueObject(x.player,y.player) ||
       !SameDialogueObject(x.pawn,y.pawn) || !SameDialogueObject(a.worldInfo,b.worldInfo) ||
       !SameDialogueObject(a.conversation,b.conversation) || !SameDialogueObject(a.package,b.package)) return false;
    if(a.menu.panelCount!=b.menu.panelCount || a.menu.panelsData!=b.menu.panelsData || a.menu.focus!=b.menu.focus) return false;
    if(!a.guiAvailable) return true;
    return SameDialogueObject(a.menu.manager,b.menu.manager) && SameDialogueObject(a.gui.panel,b.gui.panel) &&
        SameDialogueObject(a.gui.handler,b.gui.handler) && SameDialogueObject(a.gui.worldInfo,b.gui.worldInfo) &&
        a.gui.visible==b.gui.visible && a.gui.initialized==b.gui.initialized && a.gui.removing==b.gui.removing &&
        a.gui.inputDisabled==b.gui.inputDisabled && a.gui.eventsDisabled==b.gui.eventsDisabled && a.gui.focused==b.gui.focused &&
        a.wheel==b.wheel && a.paragon==b.paragon && a.renegade==b.renegade && a.initialized==b.initialized &&
        a.triggered==b.triggered && a.usingGamepad==b.usingGamepad;
}
json::Value DialogueNodeJson(DialogueNode const& n) {
    auto out=json::Value::Object(); out["available"]=n.available; out["index"]=n.index;
    out["reason"]=n.available?"bounded-selected-node-fields":"no-node-minus-one";
    if(n.available) { out["exportId"]=n.exportId; out["stateTransition"]=n.transition; out["stateTransitionParameter"]=n.parameter; }
    return out;
}
json::Value DialogueJson(DialogueSample const& s) {
    auto out=json::Value::Object(); auto const& d=s.data;
    auto c=json::Value::Object();
    c["world"]=ObjectJson(s.menu.root.world,nullptr); c["worldInfo"]=ObjectJson(s.worldInfo,nullptr);
    c["controller"]=ObjectJson(s.menu.root.player,nullptr); c["localPlayer"]=ObjectJson(s.menu.root.localPlayer,nullptr);
    c["engine"]=ObjectJson(s.menu.root.engine,nullptr); c["pawn"]=ObjectJson(s.menu.root.pawn,nullptr);
    c["mapContainmentProven"]=false; out["context"]=std::move(c);
    auto conv=ObjectJson(s.conversation,nullptr); conv["name"]=s.conversationName; conv["nameNumber"]=d.nameNumber;
    conv["package"]=ObjectJson(s.package,nullptr);
    if(s.package.available) conv["package"]["name"]=s.packageName;
    conv["currentEntry"]=DialogueNodeJson(d.entry); conv["currentReply"]=DialogueNodeJson(d.reply);
    conv["selectedReply"]=DialogueNodeJson(d.selected); conv["nodePlayTimer"]=d.nodeTimer;
    conv["entryCount"]=static_cast<int>(d.entryCount); conv["replyCount"]=static_cast<int>(d.replyCount);
    out["conversation"]=std::move(conv);
    auto interrupt=json::Value::Object(); interrupt["replyListIndex"]=d.interruptReply.index;
    interrupt["reply"]=DialogueNodeJson(d.interruptReply); interrupt["startTimeRemaining"]=d.startRemaining;
    interrupt["windowTimeRemaining"]=d.windowRemaining; interrupt["enabled"]=d.enabled;
    interrupt["activated"]=d.activated; interrupt["type"]=static_cast<int>(d.type);
    interrupt["typeName"]=d.type==kInterruptRenegade?"renegade":d.type==kInterruptParagon?"paragon":"unknown";
    out["interrupt"]=std::move(interrupt);
    auto gui=json::Value::Object(); gui["available"]=s.guiAvailable; gui["reason"]=s.guiReason;
    if(s.guiAvailable) {
        gui["manager"]=ObjectJson(s.menu.manager,nullptr); gui["panel"]=ObjectJson(s.gui.panel,nullptr);
        gui["handler"]=ObjectJson(s.gui.handler,nullptr); gui["visible"]=s.gui.visible;
        gui["focused"]=s.gui.focused; gui["panelInitialized"]=s.gui.initialized; gui["removing"]=s.gui.removing;
        gui["inputDisabled"]=s.gui.inputDisabled; gui["eventsDisabled"]=s.gui.eventsDisabled;
        gui["wheelDisplayed"]=s.wheel; gui["paragonDisplayed"]=s.paragon; gui["renegadeDisplayed"]=s.renegade;
        gui["screenInitialized"]=s.initialized; gui["wasTriggered"]=s.triggered; gui["usingGamepad"]=s.usingGamepad;
    }
    out["gui"]=std::move(gui); return out;
}
} // namespace
#if defined(MEVR_LE2_OBSERVATION_TEST)
void testing::SetDialogueSampleHook(void (*hook)()) { gDialogueSampleHook=hook; }
#endif
json::Value ObserveDialogue(std::uint32_t thread) {
    auto out=json::Value::Object(); out["schemaVersion"]=1; out["game"]="LE2";
    out["available"]=false; out["dispatchCapability"]=false; out["legalityEvaluated"]=false;
    out["acceptedInterruptProven"]=false; out["historyComplete"]=false; out["durableObjectIdentity"]=false;
    auto refuse=[&](char const* reason) { out["reason"]=reason; return out; };
    if(thread==0 || thread!=GetCurrentThreadId()) return refuse("not-on-verified-game-thread");
    auto first=std::make_unique<DialogueSample>(); auto second=std::make_unique<DialogueSample>();
    if(!ue3::runtime::Guarded(ReadDialogue,first.get())) return refuse("guarded-dialogue-read-fault");
    if(!first->available) return refuse(first->reason);
#if defined(MEVR_LE2_OBSERVATION_TEST)
    if(gDialogueSampleHook) gDialogueSampleHook();
#endif
    if(!ue3::runtime::Guarded(ReadDialogue,second.get())) return refuse("guarded-dialogue-reread-fault");
    if(!SameDialogue(*first,*second)) return refuse("dialogue-context-or-state-changed");
    auto snapshot=DialogueJson(*first);
    out["context"]=snapshot.Get("context"); out["conversation"]=snapshot.Get("conversation");
    out["interrupt"]=snapshot.Get("interrupt"); out["gui"]=snapshot.Get("gui");
    out["available"]=true; out["reason"]=first->reason; return out;
}

// Fixed duration/cadence and POD storage: no file I/O, callbacks or dispatch.
namespace {
constexpr std::uint64_t kHistoryDuration=10000, kHistoryCadence=125;
constexpr unsigned kHistoryCapacity=81, kHistoryEpochs=4, kHistoryColumns=18;
constexpr std::size_t kHistoryBytes=10*1024;
struct DialogueHistoryEpoch {
    ObjectSample world,engine,localPlayer,controller,pawn,worldInfo,conversation;
    std::uint64_t nameBits{};
    int nameNumber{};
    char name[kNameBytes]{};
};
bool SameHistoryEpoch(DialogueHistoryEpoch const& e,DialogueSample const& s) {
    auto const& r=s.menu.root;
    return SameDialogueObject(e.world,r.world) && SameDialogueObject(e.engine,r.engine) &&
        SameDialogueObject(e.localPlayer,r.localPlayer) && SameDialogueObject(e.controller,r.player) &&
        SameDialogueObject(e.pawn,r.pawn) && SameDialogueObject(e.worldInfo,s.worldInfo) &&
        SameDialogueObject(e.conversation,s.conversation) && e.nameBits==s.data.nameBits &&
        std::strcmp(e.name,s.conversationName)==0;
}
DialogueHistoryEpoch CaptureHistoryEpoch(DialogueSample const& s) {
    DialogueHistoryEpoch e; auto const& r=s.menu.root;
    e.world=r.world; e.engine=r.engine; e.localPlayer=r.localPlayer; e.controller=r.player;
    e.pawn=r.pawn; e.worldInfo=s.worldInfo; e.conversation=s.conversation;
    e.nameBits=s.data.nameBits; e.nameNumber=s.data.nameNumber;
    std::memcpy(e.name,s.conversationName,sizeof(e.name)); return e;
}
}
struct DialogueHistory::Storage {
    bool active{},seenTick{};
    std::uint32_t thread{};
    char const* reason{"recording"};
    char const* lastUnavailableReason{"none"};
    std::uint64_t startTick{},startMono{},lastTick{},lastMono{},lastSample{},endTick{},endMono{};
    unsigned count{},epochCount{},sampled{},unavailable{},skipped{},dropped{},missed{},gaps{};
    std::int64_t rows[kHistoryCapacity][kHistoryColumns]{};
    DialogueHistoryEpoch epochs[kHistoryEpochs]{};
    DialogueSample first,second;
    void Finish(char const* why) noexcept { active=false; reason=why; endTick=lastTick; endMono=lastMono; }
};
DialogueHistory::DialogueHistory() noexcept = default;
DialogueHistory::~DialogueHistory() = default;
bool DialogueHistory::Active() const noexcept { return storage_ && storage_->active; }
bool DialogueHistory::Start(std::uint64_t tick,std::uint64_t mono,std::uint32_t thread) noexcept {
    if(thread==0 || thread!=GetCurrentThreadId() || Active() || tick==UINT64_MAX || mono>UINT64_MAX-kHistoryDuration) return false;
    try {
        auto fresh=std::make_unique<Storage>();
        fresh->thread=thread; fresh->active=true; fresh->startTick=fresh->lastTick=tick;
        fresh->startMono=fresh->lastMono=mono;
        storage_=std::move(fresh); return true;
    } catch(...) { return false; } // preserves the preceding completed record
}
void DialogueHistory::Stop() noexcept {
    if(storage_ && storage_->active && storage_->thread==GetCurrentThreadId()) storage_->Finish("stopped");
}
void DialogueHistory::Tick(std::uint64_t tick,std::uint64_t mono,std::uint32_t thread) noexcept {
    if(!Active()) return;
    auto& s=*storage_;
    if(thread!=s.thread || thread!=GetCurrentThreadId()) return; // never read/mutate from foreign thread
    if(tick<s.lastTick || (s.seenTick && tick==s.lastTick) || mono<s.lastMono || tick-s.startTick>UINT32_MAX) { s.Finish("clock-invalid"); return; }
    s.lastTick=tick; s.lastMono=mono; s.seenTick=true;
    if(mono-s.startMono>=kHistoryDuration) { s.Finish("expired"); return; }
    if(s.sampled && mono-s.lastSample<kHistoryCadence) {
        if(s.skipped==UINT32_MAX) s.Finish("counter-limit"); else ++s.skipped;
        return;
    }
    // Scratch lives in the bounded host-owned allocation, not the game stack.
    std::destroy_at(&s.first); std::construct_at(&s.first);
    std::destroy_at(&s.second); std::construct_at(&s.second);
    unsigned status=0; char const* why="none";
    if(!ue3::runtime::Guarded(ReadDialogue,&s.first)) { status=2; why="guarded-dialogue-read-fault"; }
    else if(!s.first.available) { status=std::strcmp(s.first.reason,"no-current-conversation")==0?1:3; why=s.first.reason; }
    else {
#if defined(MEVR_LE2_OBSERVATION_TEST)
        if(gDialogueSampleHook) gDialogueSampleHook();
#endif
        if(!ue3::runtime::Guarded(ReadDialogue,&s.second)) { status=2; why="guarded-dialogue-reread-fault"; }
        else if(!SameDialogue(s.first,s.second)) { status=4; why="dialogue-context-or-state-changed"; }
    }
    if(status==0 && (std::strlen(s.first.menu.root.player.className)>128 ||
        std::strlen(s.first.worldInfo.className)>128 || std::strlen(s.first.conversation.className)>128)) {
        s.Finish("history-label-limit"); return;
    }
    if(status==0 && (!s.epochCount || !SameHistoryEpoch(s.epochs[s.epochCount-1],s.first))) {
        if(s.epochCount==kHistoryEpochs) { s.Finish("context-limit"); return; }
        s.epochs[s.epochCount++]=CaptureHistoryEpoch(s.first);
    }
    auto missed=(mono-(s.sampled?s.lastSample:s.startMono))/kHistoryCadence;
    if(s.sampled && missed) --missed;
    if(missed) { s.missed+=static_cast<unsigned>(missed); ++s.gaps; }
    if(s.count==kHistoryCapacity) {
        // Defensive fallback; ordinary bounded cadence cannot reach this limit.
        std::memmove(s.rows,s.rows+1,sizeof(s.rows[0])*(kHistoryCapacity-1)); --s.count; ++s.dropped;
    }
    auto& row=s.rows[s.count++];
    row[0]=static_cast<std::int64_t>(tick-s.startTick); row[1]=static_cast<std::int64_t>(mono-s.startMono);
    row[2]=status; row[3]=status?0:s.epochCount;
    if(status==0) {
        auto const& d=s.first.data;
        row[4]=d.entry.index; row[5]=d.entry.exportId; row[6]=d.reply.index; row[7]=d.reply.exportId;
        row[8]=d.selected.index; row[9]=d.selected.exportId; row[10]=d.interruptReply.index; row[11]=d.interruptReply.exportId;
        row[12]=d.type; row[13]=(d.enabled?1:0)|(d.activated?2:0);
        auto const& g=s.first;
        row[14]=g.guiAvailable?(1|(g.gui.visible?2:0)|(g.gui.focused?4:0)|(g.gui.initialized?8:0)|
            (g.gui.removing?16:0)|(g.gui.inputDisabled?32:0)|(g.gui.eventsDisabled?64:0)|(g.wheel?128:0)|
            (g.paragon?256:0)|(g.renegade?512:0)|(g.initialized?1024:0)|(g.triggered?2048:0)|(g.usingGamepad?4096:0)):0;
        row[15]=std::bit_cast<std::uint32_t>(d.nodeTimer); row[16]=std::bit_cast<std::uint32_t>(d.startRemaining);
        row[17]=std::bit_cast<std::uint32_t>(d.windowRemaining);
    } else { ++s.unavailable; s.lastUnavailableReason=why; }
    ++s.sampled; s.lastSample=mono;
}
json::Value DialogueHistory::Snapshot() const {
    auto out=json::Value::Object(); out["schemaVersion"]=1; out["game"]="LE2";
    out["started"]=storage_!=nullptr; out["active"]=Active(); out["reason"]=storage_?storage_->reason:"never-started";
    out["durationMs"]=static_cast<int>(kHistoryDuration); out["cadenceMs"]=static_cast<int>(kHistoryCadence);
    out["capacity"]=static_cast<int>(kHistoryCapacity); out["maxEpochs"]=static_cast<int>(kHistoryEpochs);
    out["dispatchCapability"]=false; out["legalityEvaluated"]=false; out["acceptedInterruptProven"]=false; out["historyComplete"]=false;
    out["startTick"]=std::to_string(storage_?storage_->startTick:0); out["startMonoMs"]=std::to_string(storage_?storage_->startMono:0);
    out["endTick"]=std::to_string(storage_?storage_->endTick:0); out["endMonoMs"]=std::to_string(storage_?storage_->endMono:0);
    out["endTimeSource"]="last-valid-tick-call-or-start";
    out["sampledRows"]=static_cast<int>(storage_?storage_->sampled:0); out["unavailableRows"]=static_cast<int>(storage_?storage_->unavailable:0);
    out["cadenceSkippedTicks"]=static_cast<std::uint64_t>(storage_?storage_->skipped:0);
    out["bufferDroppedRows"]=static_cast<int>(storage_?storage_->dropped:0); out["serializationOmittedRows"]=0;
    out["missedCadenceSlots"]=static_cast<int>(storage_?storage_->missed:0); out["gapCount"]=static_cast<int>(storage_?storage_->gaps:0);
    out["lastUnavailableReason"]=storage_?storage_->lastUnavailableReason:"none";
    out["dictionaryOmitted"]=false;
    auto columns=json::Value::Array();
    for(auto* name:{"tickOffset","msOffset","status","epoch","currentEntryIndex","currentEntryExportId","currentReplyIndex","currentReplyExportId",
        "selectedReplyIndex","selectedReplyExportId","interruptReplyIndex","interruptReplyExportId","interruptType","interruptFlags","guiFlags",
        "nodeTimerBits","startTimeRemainingBits","windowTimeRemainingBits"}) columns.Push(name);
    out["columns"]=std::move(columns);
    auto epochs=json::Value::Array(); auto rows=json::Value::Array();
    if(storage_) {
        auto const& s=*storage_;
        for(unsigned i=0;i<s.epochCount;++i) {
            auto const& e=s.epochs[i]; auto record=json::Value::Object(); record["epoch"]=static_cast<int>(i+1);
            record["controller"]=ObjectJson(e.controller,nullptr); record["worldInfo"]=ObjectJson(e.worldInfo,nullptr);
            auto conv=ObjectJson(e.conversation,nullptr); conv["name"]=e.name; conv["nameNumber"]=e.nameNumber;
            record["conversation"]=std::move(conv); epochs.Push(std::move(record));
        }
        for(unsigned i=0;i<s.count;++i) {
            auto row=json::Value::Array(); auto const* data=s.rows[i];
            for(unsigned j=0;j<kHistoryColumns;++j) {
                bool const absent=data[2]!=0 && j>=4;
                bool const noNode=j>=5 && j<=11 && (j%2)==1 && data[j-1]==-1;
                if(absent || noNode) row.Push(nullptr); else row.Push(data[j]);
            }
            rows.Push(std::move(row));
        }
    }
    out["epochs"]=std::move(epochs); out["rows"]=std::move(rows);
    unsigned omitted=0;
    // Preserve the first and latest samples; explicitly omit intervening old rows
    // if numeric extremes/long names would exceed the host's bounded wire budget.
    while(json::Write(out,0).size()>kHistoryBytes && out["rows"].Items().size()>2) {
        out["rows"].Items().erase(out["rows"].Items().begin()+1);
        out["serializationOmittedRows"]=static_cast<int>(++omitted);
    }
    // Defensive final fallback if a future dictionary change breaks the proven
    // current label bounds. Keep capture/counter provenance, never oversized wire.
    if(json::Write(out,0).size()>kHistoryBytes) {
        out["rows"]=json::Value::Array(); out["epochs"]=json::Value::Array();
        out["serializationOmittedRows"]=static_cast<int>(storage_?storage_->count:0);
        out["dictionaryOmitted"]=true;
    }
    return out;
}

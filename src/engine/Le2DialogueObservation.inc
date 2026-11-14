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

// SPDX-License-Identifier: MIT
// Included after the existing guarded readers and input context implementation.
namespace party_completion_detail {
using input_context::Identity;
using input_context::CopyIdentity;
using input_context::CopyArray;
struct State {
    Identity action{}, property{}, superclass{}, sequence{};
    int instanceSize{}, superclassSize{}, offset{}, elementSize{}, arrayDim{};
    std::uint64_t flags{};
    std::uint32_t mask{}, rawWord{};
    bool active{}, latent{}, sequenceAvailable{}, sequenceEnabled{};
    int activationCount{};
    input_context::ArrayIdentity objects{}, activeOps{};
    unsigned members{}, activeMembers{};
    bool operator==(State const&) const = default;
};
struct Sample {
    input_context::Context const* context{};
    State state{};
    bool available{};
    char const* reason{"not-sampled"};
    char const* sequenceReason{"not-sampled"};
};
static_assert(std::is_trivially_destructible_v<Sample>);
static_assert(offsetof(USequenceOp,ActivateCount)==0xCC);
#if defined(MEVR_LE2_OBSERVATION_TEST)
void (*gSampleHook)(){};
#endif
void Read(void* raw) {
    auto& out=*static_cast<Sample*>(raw); auto& s=out.state;
    auto const& context=*out.context;
    auto const* objects=UObject::GObjObjects;
    out.reason="completion-object-table-unavailable";
    if (!objects || !BoundedArray(*objects,kMaxObjects)) return;
    Table table{objects->GetData(),objects->Count()};
    auto const* action=reinterpret_cast<USequenceAction const*>(context.partyCompletionCallback.address);
    ObjectSample identity;
    out.reason="completion-owner-not-exact-source-candidate";
    if (!ReadObject(table,action,"Engine","SequenceAction",identity) ||
        CopyIdentity(identity)!=context.partyCompletionCallback ||
        !Qualified(table,action->Class,"SFXGameContent","BioSeqAct_ShowPartySelectionGUI")) return;
    s.action=CopyIdentity(identity);
    auto const* owner=action->Class;
    auto const* super=static_cast<UClass const*>(owner->SuperField);
    ObjectSample superIdentity;
    out.reason="completion-superclass-or-instance-size-unavailable";
    if (!ClassMetadata(table,super) || !ReadObject(table,super,"Core","Class",superIdentity)) return;
    s.superclass=CopyIdentity(superIdentity);
    s.instanceSize=owner->PropertySize; s.superclassSize=super->PropertySize;
    if (s.instanceSize<static_cast<int>(sizeof(USequenceAction)) || s.instanceSize>65536 ||
        s.superclassSize<static_cast<int>(sizeof(USequenceAction)) || s.superclassSize>s.instanceSize) return;
    UBoolProperty const* completion=nullptr;
    unsigned fields=0;
    for (auto const* field=owner->Children; field; field=field->Next) {
        out.reason="completion-invalid-owner-field-or-chain-limit";
        if (++fields>kMaxOwnerFields || !table.Listed(field,true) ||
            !IsA(table,field,"Core","Field") || field->Outer!=owner) return;
        char name[kNameBytes]{};
        if (!NameText(field->Name,name)) return;
        if (std::strcmp(name,"m_bPartySelectionComplete")!=0) continue;
        out.reason="completion-duplicate-or-non-bool-declaration";
        if (completion || !ClassMetadata(table,field->Class) || !Qualified(table,field->Class,"Core","BoolProperty")) return;
        completion=static_cast<UBoolProperty const*>(field);
    }
    out.reason="completion-property-missing";
    if (!completion) return;
    ObjectSample propertyIdentity;
    if (!ReadObject(table,completion,"Core","Property",propertyIdentity)) return;
    s.property=CopyIdentity(propertyIdentity); s.offset=completion->Offset;
    s.elementSize=completion->ElementSize; s.arrayDim=completion->ArrayDim;
    s.flags=completion->PropertyFlags; s.mask=completion->BitMask;
    out.reason="completion-property-storage-or-mask-unsupported";
    if (s.elementSize!=4 || s.arrayDim!=1 || (s.flags&(0x80ull|0x400ull))!=0 ||
        !s.mask || (s.mask&(s.mask-1))!=0 || s.offset<0 || s.offset>s.instanceSize-4) return;
    out.reason="completion-layout-overlaps-superclass-unsupported";
    if (s.offset<s.superclassSize) return; // An unsupported packing layout, not a universal invalidity claim.
    std::memcpy(&s.rawWord,reinterpret_cast<unsigned char const*>(action)+s.offset,4);
    s.active=action->bActive; s.latent=action->bLatentExecution; s.activationCount=action->ActivateCount;
    out.available=true; out.reason="stable-reflected-completion-and-sdk-sequence-state";
    auto const* parent=action->ParentSequence; ObjectSample sequence;
    out.sequenceReason="parent-sequence-unavailable-or-foreign";
    if (!ReadObject(table,parent,"Engine","Sequence",sequence) || action->Outer!=parent) return;
    out.sequenceReason="parent-sequence-arrays-invalid-or-over-limit";
    if (!BoundedArray(parent->SequenceObjects,4096) || !BoundedArray(parent->ActiveSequenceOps,4096)) return;
    s.sequence=CopyIdentity(sequence); s.objects=CopyArray(parent->SequenceObjects); s.activeOps=CopyArray(parent->ActiveSequenceOps);
    s.sequenceEnabled=parent->bEnabled;
    // Pointer comparison only; unrelated sequence objects are never dereferenced.
    for (unsigned i=0;i<s.objects.count;++i) if (parent->SequenceObjects.GetData()[i]==action) ++s.members;
    for (unsigned i=0;i<s.activeOps.count;++i) if (parent->ActiveSequenceOps.GetData()[i]==action) ++s.activeMembers;
    s.sequenceAvailable=true; out.sequenceReason="bounded-parent-array-membership-sampled";
}
}
json::Value ObservePartyCompletion(std::uint32_t thread, input_context::Context const& context) {
    auto out=json::Value::Object(); out["available"]=false;
    out["sourceCandidate"]="SFXGameContent.BioSeqAct_ShowPartySelectionGUI";
    out["dispatchCapability"]=false; out["affectsInputAdmission"]=false;
    out["callbackExecutionProven"]=false; out["schedulerProgressProven"]=false;
    out["durableObjectIdentity"]=false; out["mountedScriptBodyProven"]=false;
    if (!thread || GetCurrentThreadId()!=thread) { out["reason"]="not-on-verified-game-thread"; return out; }
    if (!context.available || context.category!=input_context::Category::PartyConfirm ||
        std::strcmp(context.completionFunction.data(),"OnPartySelectionComplete")!=0 ||
        context.partyNames[1][1]!=0 || !context.partyCompletionCallback.address) {
        out["reason"]="exact-current-party-confirmation-callback-unavailable"; return out;
    }
    auto current=input_context::CaptureInputContext(thread);
    if (!input_context::SameContext(context,current)) { out["reason"]="input-context-changed-before-completion-sample"; return out; }
    party_completion_detail::Sample first{&context}, second{&context};
    if (!ue3::runtime::Guarded(party_completion_detail::Read,&first)) { out["reason"]="guarded-completion-read-fault"; return out; }
    if (!first.available) { out["reason"]=first.reason; return out; }
#if defined(MEVR_LE2_OBSERVATION_TEST)
    if (party_completion_detail::gSampleHook) party_completion_detail::gSampleHook();
#endif
    if (!ue3::runtime::Guarded(party_completion_detail::Read,&second)) { out["reason"]="guarded-completion-reread-fault"; return out; }
    current=input_context::CaptureInputContext(thread);
    if (!second.available || !(first.state==second.state) || !input_context::SameContext(context,current)) {
        out["reason"]="completion-or-input-context-changed-during-sample"; return out;
    }
    auto const& s=first.state; out["available"]=true; out["reason"]=first.reason;
    out["ownerIndex"]=s.action.index;
    auto flag=json::Value::Object(); flag["name"]="m_bPartySelectionComplete"; flag["type"]="Core.BoolProperty";
    flag["declaringOwner"]="SFXGameContent.BioSeqAct_ShowPartySelectionGUI";
    flag["propertyIndex"]=s.property.index; flag["offset"]=s.offset; flag["elementSize"]=s.elementSize;
    flag["arrayDim"]=s.arrayDim; flag["mask"]=static_cast<double>(s.mask);
    flag["instanceSize"]=s.instanceSize; flag["superclassSize"]=s.superclassSize;
    flag["value"]=(s.rawWord&s.mask)!=0; out["completionFlag"]=std::move(flag);
    out["active"]=s.active; out["latentExecution"]=s.latent; out["activationCount"]=s.activationCount;
    out["inheritedStateSource"]="pinned-SDK-Engine.SequenceOp";
    auto sequence=json::Value::Object(); sequence["available"]=s.sequenceAvailable; sequence["reason"]=first.sequenceReason;
    sequence["currentWorldContainmentProven"]=false;
    if (s.sequenceAvailable) {
        sequence["index"]=s.sequence.index; sequence["enabled"]=s.sequenceEnabled;
        sequence["objectCount"]=static_cast<int>(s.objects.count); sequence["activeOpCount"]=static_cast<int>(s.activeOps.count);
        sequence["ownerMembershipCount"]=static_cast<int>(s.members); sequence["ownerActiveMembershipCount"]=static_cast<int>(s.activeMembers);
    }
    out["parentSequence"]=std::move(sequence); return out;
}
#if defined(MEVR_LE2_OBSERVATION_TEST)
void party_completion_testing::SetSampleHook(void (*hook)()) { party_completion_detail::gSampleHook=hook; }
#endif

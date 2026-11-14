// SPDX-License-Identifier: MIT
// Included in input_context's existing guarded-reader namespace.
static_assert(offsetof(UBioSFHandler_CharacterRecord2,__OnCloseCallback__Delegate)==0xEC);
static_assert(offsetof(UBioSFHandler_CharacterRecord2,m_WorldInfo)==0xFC);
static_assert(offsetof(UBioSFHandler_CharacterRecord2,m_Helper)==0x104);
static_assert(offsetof(UBioSFHandler_CharacterRecord2,m_CurrentPawnIndex)==0x114);
static_assert(offsetof(USFXPowerLevelUpHelper,m_WorldInfo)==0x80);
static_assert(offsetof(USFXPowerLevelUpHelper,m_Pawn)==0x88);
static_assert(offsetof(USFXPowerLevelUpHelper,m_nPointsSpent)==0x90);
bool ReadCharacterRecord(Table const& table, UMassEffectGuiManager const* manager, Context& c) {
    auto& s=c.characterRecord; MenuPanel record;
    auto const* panel=reinterpret_cast<UBioSFPanel const*>(c.focusPanel.address);
    c.reason="character-record-panel-or-handler-not-interactive";
    if (!c.modeActive || !PanelContext(table,panel,manager,"BioSFHandler_PCCharacterRecord2",record,true) ||
        std::strcmp(record.handler.className,"SFXGame.BioSFHandler_PCCharacterRecord2")!=0 || !PanelInteractive(record)) return false;
    c.reason="character-record-panel-ambiguous-or-message-overlay";
    unsigned matches=0;
    for (unsigned i=0;i<c.panels.count;++i) {
        auto const& other=c.panelStates[i];
        if (std::strcmp(other.tag.data(),"CharacterRecord")==0) ++matches;
        // A visible message overlay may be awaiting its focus transition. Never
        // borrow CharacterRecord admission to dismiss that separate screen.
        if (std::strcmp(other.tag.data(),"MessageBox")==0 && other.visible && !other.removing) return false;
    }
    if (matches!=1) return false;
    auto const* handler=reinterpret_cast<UBioSFHandler_PCCharacterRecord2 const*>(record.handler.address);
    c.focusHandler=CopyIdentity(record.handler); s.closing=handler->m_bCloseGUIButtonPressed;
    c.reason="character-record-closing-or-initial-hide-pending";
    if (!std::isfinite(handler->m_InitialHiddenTimer)) return false;
    s.initialHidePending=handler->m_InitialHiddenTimer>0;
    if (s.closing || s.initialHidePending) return false;
    c.reason="character-record-handler-world-mismatch";
    if (!record.worldInfo.available || CopyIdentity(record.worldInfo)!=c.worldInfo ||
        handler->m_WorldInfo!=manager->oBioWorldInfo || handler->m_WorldInfo!=handler->oWorldInfo) return false;
    ObjectSample helper, character;
    c.reason="character-record-helper-invalid-or-foreign";
    if (!ReadObject(table,handler->m_Helper,"SFXGame","SFXPowerLevelUpHelper",helper) ||
        std::strcmp(helper.className,"SFXGame.SFXPowerLevelUpHelper")!=0 || handler->m_Helper->Outer!=handler ||
        handler->m_Helper->m_WorldInfo!=handler->m_WorldInfo) return false;
    s.helper=CopyIdentity(helper);
    c.reason="character-record-character-invalid-or-foreign";
    if (!ReadObject(table,handler->m_Helper->m_Pawn,"SFXGame","BioPawn",character) ||
        handler->m_Helper->m_Pawn->WorldInfo!=handler->m_WorldInfo) return false;
    s.character=CopyIdentity(character); s.characterIndex=handler->m_CurrentPawnIndex;
    s.pointsSpent=handler->m_Helper->m_nPointsSpent;
    c.reason="character-record-source-array-or-current-index-invalid";
    if (!BoundedArray(handler->m_SpawnedSquadMembersSource,32) ||
        !BoundedArray(handler->m_Helper->m_UpdatedPowers,128)) return false;
    s.sourcePawns=CopyArray(handler->m_SpawnedSquadMembersSource); s.powerSnapshot=CopyArray(handler->m_Helper->m_UpdatedPowers);
    if (s.sourcePawns.count>32 || s.powerSnapshot.count>128 || s.characterIndex<0 || static_cast<unsigned>(s.characterIndex)>=s.sourcePawns.count) return false;
    unsigned selected=0;
    for (unsigned i=0;i<s.sourcePawns.count;++i) {
        auto const* pawn=handler->m_SpawnedSquadMembersSource.GetData()[i]; ObjectSample source;
        if (!ReadObject(table,pawn,"SFXGame","BioPawn",source) || pawn->WorldInfo!=handler->m_WorldInfo) return false;
        s.sources[i]=CopyIdentity(source);
        if (s.sources[i]==s.character) ++selected;
    }
    if (selected!=1 || s.sources[s.characterIndex]!=s.character) return false;
    // Source permits a null callback for ordinary menu openings. A present
    // callback is observed, not resolved/called or required to name a sequence.
    auto const& callback=handler->__OnCloseCallback__Delegate;
    std::memcpy(s.closeCallback.raw.data(),&callback.FunctionName,sizeof(callback.FunctionName));
    if (callback.Object) {
        ObjectSample owner; c.reason="character-record-close-callback-invalid";
        if (!ReadObject(table,callback.Object,"Core","Object",owner) ||
            !CopyPartyName(callback.FunctionName,s.closeCallback.raw,s.closeCallback.text) ||
            std::strcmp(s.closeCallback.text.data(),"None")==0) return false;
        s.closeOwner=CopyIdentity(owner);
        std::memcpy(s.closeOwnerClass.data(),owner.className,sizeof(owner.className));
    }
    c.category=Category::CharacterRecord; return true;
}

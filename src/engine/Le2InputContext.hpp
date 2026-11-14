// SPDX-License-Identifier: MIT
#pragma once
#include "core/Json.hpp"
#include <array>
#include <cstdint>

namespace mevr::engine::le2_diagnostic::input_context {
#if defined(MEVR_GAME_LE2)
enum class Category { Unavailable, Default, PartySelect, PartyConfirm, Loadout };
// Comparison-only process-local identities, never wire handles or durable IDs.
struct Identity {
    std::uintptr_t address{}, classAddress{};
    std::int32_t index{};
    bool operator==(Identity const&) const = default;
};
struct ArrayIdentity {
    std::uintptr_t data{};
    std::uint32_t count{}, capacity{};
    bool operator==(ArrayIdentity const&) const = default;
};
struct Panel {
    Identity object, handler;
    std::array<char,128> tag{};
    bool visible{}, initialized{}, removing{}, inputDisabled{}, eventsDisabled{}, focused{};
    bool operator==(Panel const&) const = default;
};
struct NameKey {
    std::array<std::uint32_t,2> raw{};
    std::array<char,128> text{};
    bool operator==(NameKey const&) const = default;
};
struct ChoiceKey {
    std::int32_t id{};
    bool disabled{}, nested{};
    bool operator==(ChoiceKey const&) const = default;
};
struct LoadoutState {
    Identity action, sequence, data;
    std::uintptr_t topMenu{}, subMenu{};
    NameKey callback;
    ArrayIdentity objects, activeOps, characters, weapons, weaponIds, subWeapons, choices;
    std::array<std::array<std::uint32_t,2>,32> characterNames{};
    std::array<Identity,6> weaponClasses{};
    std::array<std::int32_t,6> enabledWeaponIds{};
    std::array<std::array<std::uint32_t,2>,64> subWeaponNames{}, subWeaponUnlocks{};
    std::array<std::int32_t,64> subWeaponRatings{};
    std::array<bool,64> subWeaponSpecial{};
    std::array<ChoiceKey,64> choiceStates{};
    std::int32_t currentPawn{}, subWeaponIndex{}, callbackContext{}, activationCount{};
    std::uint32_t stage{}, sequenceSlot{}, activeSlot{}; // 1 characters, 2 groups, 3 weapons
    bool active{}, finished{}, sequenceEnabled{}, latent{}, allHenchmen{};
    bool operator==(LoadoutState const&) const = default;
};
struct State {
    Category category{Category::Unavailable};
    Identity engine, world, localPlayer, controller, pawn, playerInput, viewport,
        guiManager, worldInfo, modeManager, mode, focusPanel, focusHandler;
    ArrayIdentity modes, panels, focusScripts;
    std::array<Identity,32> focusScriptHandlers{};
    std::array<Panel,32> panelStates{};
    std::array<char,128> map{}, focusedTag{};
    std::uint32_t currentMode{}, ignoreMove{}, ignoreLook{}, wheelMode{};
    bool modeActive{}, allowMovement{}, allowCamera{}, cinematic{}, playersOnly{},
        playersOnlyPending{}, worldPaused{}, closedWheel{}, wheelVisible{}, wheelTransition{},
        partyInitialized{}, partyNoSelectMode{};
    std::int32_t partyMember1{}, partyMember2{};
    Identity wheelHandler;
    std::uintptr_t wheelCache{}, wheelPlayer{}, wheelPawn{};
    Identity partyPanel, partyHandler, confirmationCallback, partyCompletionCallback;
    ArrayIdentity partyMembers;
    std::array<std::array<std::uint32_t,2>,4> partyNames{}; // callback, completion, selected labels
    std::array<char,128> confirmationFunction{}, completionFunction{}, partyLabel1{}, partyLabel2{};
    std::uintptr_t messageCache{};
    std::int32_t confirmationContext{}, messageHiddenDepth{};
    bool messageHandleInput{}, messageModal{}, messageCached{}, messageDisplayingHint{};
    LoadoutState loadout;
    bool operator==(State const&) const = default;
};
struct Context : State {
    bool available{};
    char const* reason{"not-sampled"};
};
// Single verified game-thread owner only. Two guarded fresh samples, no engine
// calls, no input mutation. Keep Context on that owner; polling threads receive
// only the separately admitted primitive pad pulse, never these identities.
Context CaptureInputContext(std::uint32_t gameThreadId);
// Pure comparison. Caller must Capture again every tick and permanently cancel
// the pulse on false; restoring old values must never rearm that pulse.
// Cannot detect a transition that occurs entirely between sampled ticks.
bool SameContext(Context const&, Context const&);
json::Value ContextJson(Context const&); // excludes addresses and pointer values
#if defined(MEVR_LE2_OBSERVATION_TEST)
namespace testing { void SetSampleHook(void (*hook)()); }
#endif
#endif
}

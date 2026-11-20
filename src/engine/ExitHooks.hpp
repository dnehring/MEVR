// SPDX-License-Identifier: MIT
#pragma once

namespace mevr::hook {
class Context;
}

// The game's removal of a person, held back while the player can see them (game/ConversationExit, core/vr/ConversationExit;
// research/conversation-exits.md). LE1 only (read with Ghidra, 2.0.0.48602):
//
// - UBioActorBehavior::execSetActive (0xBAAB60; the native of SFXGame.BioActorBehavior.SetActive, folded with
//   BioArtPlaceableBehavior's) reads its bool and calls the behaviour's C++ SetActive (vtable slot 0x4D0). A level script's
//   BioSeqAct_ModifyPropertyPawn with its "Active" row (Udina's "Deactivate the ambassador" at sta90_captain_dlg's end) reaches
//   it through UObject::ProcessEvent: the op's Activated (0xA7CED0, the class's vtable slot 0x408) finds the behaviour function
//   the row maps to (aBehaviorFunctions) and calls it with its value (0xA818E0: the behaviour's vtable slot 0x250, ProcessEvent).
//   ProcessEvent's frame for a native is its own: Node the function, Locals the parameters. The pre-hook asks the provider
//   about such a call (the frame's Node is SetActive): withheld, the native is not run (ProcessEvent returns as after it) and
//   the op goes on to its outputs as the game's would; the provider applies the same call later through ProcessEvent. Calls
//   from script (another frame's Node) always run.
//
// The hook runs on the game thread inside the level script's update; the provider reads the mod's own per-tick state.
namespace mevr::engine::exit_hooks {

// True: withhold this SetActive call (`parms` the call's parameters: bSetActive at its reflected place).
using SetActiveProvider = bool (*)(void const* behavior, void const* parms);

// The hook goes in once (lazily); false if it could not (another game, not a function start).
bool Install(hook::Context& init);
// False: still live (or a call still inside it after waiting): this module must stay loaded.
bool Uninstall(hook::Context& init, bool waitForCalls);
bool Installed();
// `function`: SFXGame.BioActorBehavior.SetActive (the frame's Node a ProcessEvent call has); null provider: none.
void SetProvider(SetActiveProvider provider, void const* function);

struct Stats {
    unsigned calls = 0;     // execSetActive calls since the hook went in
    unsigned fromEvent = 0; // ... through ProcessEvent (the frame's Node SetActive)
    unsigned withheld = 0;  // ... withheld by the provider
};
Stats Totals();

} // namespace mevr::engine::exit_hooks

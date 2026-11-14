<!-- SPDX-License-Identifier: MIT -->
# Party confirmation and subsequent screens

This extends the existing read-only input context sampler with `PartyConfirm`
(`party-confirm`). It adds no engine calls, hooks, synthetic input source, host
admission, or transport. SDK compilation and synthetic checks pass. The later
integrated host has native structural context and bounded pulse evidence, but
repeatability remains unresolved. The latest run reached Powers after confirmation;
the earlier stalled run is retained below rather than reclassified as a pass.

## Latest native result: Powers reached

Run `370f621d-8fa9-4b73-bd7c-c81935f64f0a`, candidate `7eee6b6f`, reached the
Miranda/Grunt confirmation and sampled its actual external owner as
`SFXGameContent.BioSeqAct_ShowPartySelectionGUI`. Its reflected completion flag
was false; the active latent sequence action appeared once in its enabled
parent's active-operation list. After one confirmation pulse, reviewed captures
showed the squad Powers screen with Shepard level 28 and Miranda/Grunt.

No true completion-flag sample or callback trace was captured. The observer
stopped reporting the completion owner when the current confirmation relation
disappeared, and correctly rejected the new screen as an unsupported input
context. The Powers screen is not evidence of weapon loadout or combat readiness.
Reuse the scanner and existing context adapter to establish its ordinary exit.
All four protected roots restored; see the [full result](le2-validation-evidence.md#squad-confirmation-advances-to-powers-combat-acquisition-remains-pending).

## Earlier native checkpoint, 2026-11-14 UTC

Run `86d06e42-ad8a-466d-8770-76f46fe5397e`, candidate `460bf676`, visibly selected
Miranda and Grunt and reached the confirmation modal. The observer admitted the
exact current `FinalizeParty` relation, selected indices 6/7 and labels
`InSquadVixen`/`InSquadGrunt`. External completion was present and named
`OnPartySelectionComplete`; its destination was not proven. A bounded A pulse
produced 32 positive output polls and 1222 neutral writes over the completed
250 ms release window. The modal then disappeared visually while the dim squad
screen remained. Two later observations still reported the same confirmation
context and a passive capture showed no progression. No loadout pulse followed.

Windows refused explicit foreground activation (foreground HWND 0), so focus is
an unresolved condition. The run closed and all protected roots restored.
See [full run evidence](le2-validation-evidence.md). Stable structural flags do
not prove the native/GFx modal lifecycle completed; resolve the actual external
callback before treating this fixture as a route to loadout or combat.

The next observer revision exposes the already sampled completion owner's class
and process-local object index, plus the copied function-name index/number.
The previous archive omitted these fields and cannot identify the owner after
the fact. This adds no traversal or native call: function resolution and callback
execution remain explicitly false in the report. A fresh native sample is needed
to bind source candidates to the actual owner.

### External callback source candidate

The existing LegendaryExplorer-backed scanner found
`SFXGameContent.BioSeqAct_ShowPartySelectionGUI.OnPartySelectionComplete` in
`BioH_SelectGUI.pcc` copies under `DLC_00_Shared`, `DLC_CON_Pack01`,
`DLC_CON_Pack02`, `DLC_HEN_MT` and `DLC_HEN_VT`. Their inspected activation and
callback bodies match. This is a source candidate, not proof of mounted
precedence or of the live owner in run 1749.

The callback marks `m_bPartySelectionComplete`. In the shared-package copy,
`UpdateOp` reports that flag and `Deactivated` closes PartySelect. The inspected
sequence's finished branch performs squad cleanup and reaches the remote event
`re_PartySelectFinished`; the aborted/cancelled branch reaches
`re_PartySelectCancelled`. The receiver determines subsequent progression.
There is no loadout action in this inspected chain, so loadout is not an
established immediate destination.

The later run bound the live owner to this candidate and sampled its flag and
action state. The SDK has no declaration for this content action, so the helper
uses a validated runtime BoolProperty offset/mask rather than a guessed layout.
It reuses existing reflection and bounded sequence inspection. Neither that
sample nor these source findings establish that the callback ran in the earlier
failed transition; callback execution and sequence scheduling remain distinct.

The implemented read-only `partyCompletion` sample now resolves that exact
candidate's scalar BoolProperty through the existing guarded object/field
reader, including owner, name, mask, storage and class-size checks. It reports
the sampled flag, inherited activation count/active/latent fields, and bounded
parent membership. Inactive or absent active membership remains useful data.
Two complete samples and fresh input contexts must agree. General cached
reflection lookup was audited but does not supply these exact guarded checks;
the helper reuses the current observer's readers rather than introducing a
second reflection system. It runs only for explicit `observe-input`, outside
input admission and the post-publication path. SDK tests and source review pass.
The latest native result above adds field availability and visible transition
evidence; it does not capture a true flag or establish callback execution.

Private receipt `party-completion-source/inspection-receipt.json` SHA256
`861fba32f60988b5efb68181789c5aa45beb0f1e4b495f22499a1d68effaa015`
pins all five packages and outputs. Shared package SHA256:
`752586488a42c5f76f713ae0b0003bcbdf7a7d994d51fbeab12493eddd3bac5a`.
Scanner and SDK pins are the same as below. Raw extracted code remains private;
the metadata scan does not prove a unique owner or exhaustive mounted content.

The subsequent source-grounded loadout implementation is documented separately
in [the loadout context contract](le2-loadout-input-context.md). The audit below
records the boundary at the original party-confirmation change.

The archived `run-20261114T1641Z-input` capture after Grunt selection shows the
squad confirmation box. Its correlated save-flow response reports GUI mode 7
and focused tag `MessageBox`. Those artifacts establish the visible stop point,
not the callback pointer/class relationships introduced below. Localized caption
text is never an admission condition.

## Source and declaration evidence

The existing read-only conversation scanner inspected installed LE2
`SFXGame.pcc`. No game was launched or modified. Private outputs are under this
worktree's ignored `local-tools/party-source`; only these original findings are
committed.

| Input | Pin |
|---|---|
| LExSDKv2 declarations | `6bd05809cccb1a099a520371a95779d4a56bb246` |
| Existing scanner SHA-256 | `918a095735f00998e5ff99871902253c47c66069a6cce3e6d6b0d474addd6f01` |
| Installed SFXGame.pcc SHA-256 | `681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77` |
| Previously extracted BIOUI.ini SHA-256 | `2e556b5c880f02ed7ba318e4bc543e4480ad405c8b08bad44a3b4ae1e9cd724c` |

The configuration extraction is corroborating static evidence, not a new mounted
configuration or runtime-class completeness claim.

`BioSFHandler_PartySelection.ConfirmParty` creates a message box through its own
panel's manager, assigns its input delegate to the same party handler's
`FinalizeParty`, and displays the party confirmation. The inspected native
declaration of `MessageBox.SetInputDelegate` defaults its context argument to 0.
`FinalizeParty` uses each selected index other than -1 to read the corresponding
`lstMemberInfo.InPartyLabel`. Accept marks those members and calls `ExitScreen`;
cancel returns to the selection UI. `ExitScreen` either hides PartySelect or
calls `ExternalCallback_OnComplete`. That external destination is not inferred.

SDK offsets checked by compilation include message input delegate `0x9C`,
callback context `0xD4`, hidden depth `0xD8`, party member array `0x9C`, party
completion delegate `0xCC`, and member-row `InPartyLabel` offset `0x20`.
`FScriptDelegate` contains an object pointer and `SFXName`, totaling 16 bytes.

## Implemented gate

The existing validated engine/world/local-player/controller/pawn/input/GUI roots
and bounded complete panel list remain prerequisites. The new branch requires:

- Active exact `SFXGameModeGUI` in slot 7 and a unique focused, interactive
  `MessageBox` panel with exact `BioSFHandler_PCMessageBox` handler.
- One reachable initialized, nonremoving `PartySelect` panel with exact,
  initialized `BioSFHandler_PCPartySelection` handler. Both panels and handlers
  retain the existing manager, panel and world reciprocals. A modal child may
  hide or disable its parent; parent focus/input are not required.
- Message input delegate object equal to that party handler, function name
  exactly `FinalizeParty`, and callback context 0.
- Party member array count at most 32, valid capacity/storage, and both selected
  indices either -1 or within the array. The selected plot-label names and raw
  name identities are sampled. This imposes no invented minimum party size.
- Any external completion target must be a listed live object with valid class
  ancestry and a readable function name. Its identity is retained for change
  detection; no destination or callable signature is asserted.

Two guarded fresh samples must match. Callback identities, selected labels,
member array header, panel state, message input/modal/cache/hint/hidden-depth
facts and manager message-cache pointer participate in comparison. An admitted
pulse therefore cancels when these change. There is no global object search.

`CreateMessageBox`, `DisplayMessageBox` and `SetInputDelegate` have opaque native
bodies. The sampler reports actual input/modal/cache/hidden-depth fields and
whether the manager cache matches; it does not claim their full native lifecycle
or invent extra flag combinations as universal readiness requirements. It also
does not read GFx highlight state or prove game consumption, progression,
persistence, or VR behavior.

## Subsequent weapon/loadout source audit

The configuration maps `ChoiceGUI` to `BioSFHandler_PCChoiceGUI`, but uses that
same handler class for `MissionCompletion`. Generic class-only admission would
therefore include unrelated screens and remains unsupported.

The inspected `SFXSeqAct_ChooseLoadoutBase` script provides a narrower path:

| Stage | Concrete callback/ownership | Ordinary completion behavior |
|---|---|---|
| Character list | `TopMenu.InputCallback` targets this action's `CharacterMenuHandler` | Accept prepares that character's loadout and opens weapon groups; cancel sets `m_bFinished`. |
| Weapon groups | `TopMenu.InputCallback` targets `TopMenuHandler` | Accept opens the selected weapon group; cancel applies the current loadout and returns to characters. |
| Individual weapons | `SubMenu.InputCallback` targets `SubMenuHandler` | Accept changes the selected weapon; either response closes this submenu and returns to weapon groups. |

`Activated` enables GUI mode 7 with owner name `ChoiceGUI`, initializes the
available character list, and opens the character menu. After `m_bFinished`,
`Update` hides/nulls `TopMenu` and emits the Kismet output. `Deactivated` disables
that named GUI mode. The implementation behind `CreateChoiceGUI`, `Initialize`
and `ShowChoiceGUI` is native and opaque in these inspections.

The next bounded gate should require the focused exact `ChoiceGUI` panel and
same-world PCChoiceGUI handler, a live typed callback action belonging to the
current world/sequence, the exact corresponding callback above, and reciprocal
`TopMenu`/`SubMenu` identity. Snapshot the action's `m_bFinished`, `CurrentPawn`,
`SubMenuWeaponIdx`, bounded character/weapon arrays and choice-data relationship
before allowing that specific category. Relevant SDK fields include handler
input delegate `0xBC`, choice data `0xEC`, callback context `0xF4`; action TopMenu
`0x15C`, SubMenu `0x164`, CurrentPawn `0x1C0`, and state flags `0x1C4`.

The concrete sequence edge from this fixture's party completion to that action,
current-world action ownership, native menu initialization, and live callback
indices still need observation. This audit does not claim that every party
confirmation opens a loadout screen. No ChoiceGUI category is enabled here.

## Checks and next native handoff

The actual SDK observation test and production observer translation unit compile
under MSVC `/W4 /WX` in Release and Debug. The full existing observation suite
passes both. New fixtures cover callback target/name/context mismatch, parent
modal behavior, wrong concrete handlers, panel ambiguity, malformed arrays,
out-of-range selections, external completion identity, and label/callback
replacement between samples. Existing default/party-select behavior remains
covered by the same suite.

After separately integrating host admission for the new category, first archive
an `observe-input` showing the precise callback relationship in the actual
confirmation. Only then issue an explicit bounded confirmation pulse, reconcile
neutral output and capture the resulting screen/context. A returned pulse or
read-only available context is not acceptance of the next screen or scenario.

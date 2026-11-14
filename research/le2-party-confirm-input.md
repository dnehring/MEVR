<!-- SPDX-License-Identifier: MIT -->
# Party confirmation and the following loadout flow

This extends the existing read-only input context sampler with `PartyConfirm`
(`party-confirm`). It adds no engine calls, hooks, synthetic input source, host
admission, or transport. SDK compilation and synthetic checks pass; native
acceptance of this new category remains pending.

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

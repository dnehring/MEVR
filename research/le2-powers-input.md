<!-- SPDX-License-Identifier: MIT -->
# LE2 Powers screen: normal exit and existing input seams

The next bounded exit trial should reuse `pad-pulse` with B after validating the
exact **CharacterRecord** context. This screen is not the existing ChoiceGUI
weapon-loadout context. No implementation, native action or successful exit is
claimed by this audit.

## Evidence and normal route

Run `370f621d-8fa9-4b73-bd7c-c81935f64f0a` (candidate `7eee6b6f`) visibly advanced
from squad confirmation to the Powers screen: John Shepard, level 28 Soldier,
with Miranda and Grunt listed. The subsequent `observe-input` responses were
`unsupported-input-context`; they did **not** expose the live focused handler,
panel tag or close callback. The independently sealed native receipt is
`be4c6e02abeaf69c47b349aa835ffdc8e6801763daf906dacbedff0efca09c44`.

The run's archived `BIOUI.ini` maps `CharacterRecord` to
`SFXGame.BioSFHandler_PCCharacterRecord2` and
`GUI_SF_RecordScreen.RecordScreen`. The PC handler inherits
`BioSFHandler_CharacterRecord2`; its SDK declaration adds no fields. This is a
configuration/source identification, pending a fresh live structural sample.

Bounded exact-function inspection of installed `SFXGame.pcc` and `Engine.pcc`
through the existing LegendaryExplorer-backed scanner found:

| Stage | Exact source | What the source establishes |
|---|---|---|
| Normal input | `BioSFHandler_CharacterRecord2.HandleInputEvent` | B or Start obtains an input lock and sets `m_bCloseGUIButtonPressed`, then continues through inherited handling. Other events are refused while closing or when input permission is absent. |
| Inherited input | `BioSFHandler.HandleInputEvent`; `SFXGameModeBase.GuiKey` | The handler forwards to the ScaleForm input route; the active mode forwards GUI events to the manager. Native/GFx processing is not reconstructed here. |
| UI initialization | `BioSFHandler_CharacterRecord2.InitializeCharacterRecord` | The handler records its world, creates a level-up helper owned by itself, populates squad sources and switches to a character. The displayed character need not be the controller's pawn. |
| Closing callback | `AdditionalShutdownHandling`; `OnPanelRemoved` | Shutdown invokes an optional `OnCloseCallback`. Removal performs UI cleanup and clears helper/callback/closing state. |
| Sequence opening | `SFXSeqAct_ShowCharacterRecordGUI.Activated` | Creates `CharacterRecord` and binds the handler's close callback to `onScreenClosed`. |
| Sequence completion | `onScreenClosed`; `UpdateOp`; `Deactivated` | The callback marks `m_bIsFinished`; update selects success/aborted outputs and returns that flag; deactivation removes the panel. The destination after this action is not established. |

`HandleEvent` also contains talent purchases, auto-level and apply/cancel routes.
These are not exit shortcuts. The exact native/GFx close processing, live
callback owner and subsequent sequence destination remain unobserved. Directly
calling shutdown, the close delegate, `RemovePanel` or a sequence callback would
bypass the normal input path and is outside the proposed trial.

## Reuse before adding anything

| Existing implementation | Actual support | Smallest remaining seam |
|---|---|---|
| [GameMenuRequest.cpp](../src/game/GameMenuRequest.cpp) `StepSwitch` | B through active-mode `GuiKey`, with a focused-handler fallback, already exists. This implementation and its tick are **LE1-only** compile guards. | Reuse its source understanding; do not describe it as an available LE2 backout. Its repeated backouts are inappropriate for a single controlled trial. |
| [GamepadMapping.hpp](../src/game/GamepadMapping.hpp) and [GamepadBridge.cpp](../src/game/GamepadBridge.cpp) | Existing VR pad mapping and diagnostic XInput bridge already represent B. Diagnostic pulses retain identity, ownership, context cancellation and a neutral-output window. | No new hook, direct engine call or input runner. |
| [Le2InputContext.inc](../src/engine/Le2InputContext.inc) | Validates default, PartySelect, party confirmation and exact ChoiceGUI loadout contexts; rejects CharacterRecord. | Add one exact rooted CharacterRecord category and copied continuity state, using current guarded panel/handler/world checks. |
| [Le2InputControl.inc](../src/engine/Le2InputControl.inc), [ControlProtocol.cpp](../src/core/qa/ControlProtocol.cpp), [control.py](../tools/qa-le2/control.py) | Existing pulse transport, bounded schema, explicit permission and category matching. | Recognize the same new category consistently. Keep the existing channel and semantics; no new verb or console route. |

For the first trial, restrict the new context to a neutral-otherwise B pulse
(`buttons=8192`), without Start, purchase or navigation inputs. B is GUI event 13
in the pinned SDK; Start is event 21, but supporting both is unnecessary here.

## Minimal validation contract before that trial

Reuse current engine/world/controller/input/mode and complete panel-list checks.
Require the exact focused `CharacterRecord` panel with interactive state and
exact `BioSFHandler_PCCharacterRecord2`, reciprocal manager/panel/world links,
and a stable GUI-mode sample. Refuse modals, unknown panels and closing state.
Observe the already declared world/helper/close-delegate fields; validate the
helper's owner/world and character identity without equating its character to
the controller pawn. Keep two-read consistency and cancellation on changes.

Useful pinned SDK fields are the close delegate at `0xEC`, handler world at
`0xFC`, helper at `0x104`, current character index at `0x114`, and closing bit at
`0x150`. `SFXPowerLevelUpHelper` supplies world, pawn and points-spent fields.
Do not infer native/GFx readiness from a nonnull helper alone. Capture the
initialized screen and live close-callback identity before authorizing the one
B trial; then separately observe/capture its result. If the next screen is
unsupported, stop there rather than sending another B.

Source inspection receipt: private `local-tools/le2-powers-exit-source/inspection-receipt.json`,
SHA-256 `af6256b975c8183732ebb4ae0d850d29a5254199a65a2bd01f3091e6c3bb4529`.
It pins the scanner, installed packages, archived config, extracted exact
functions and SDK commit `6bd05809cccb1a099a520371a95779d4a56bb246`.
One negative lookup (`SFXGameModeGUI.GuiKey`) is retained as zero exports; it is
not evidence that the inherited GUI route is absent. Extracted game code stays
private. No runtime files were modified or executed for this audit.

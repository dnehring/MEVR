<!-- SPDX-License-Identifier: MIT -->
# LE2 Powers screen: normal exit and existing input seams

The implemented adapter reuses `pad-pulse` with B after validating the exact
**CharacterRecord** context. This screen is not the existing ChoiceGUI
weapon-loadout context. Offline checks pass; live context availability, input
consumption and successful exit remain pending.

Candidate `ad31adeb` passed the three builds and ordinary 53/53 plus diagnostic
14/14 tests. Its protected `1848Z-powers` trial stopped earlier in chapter
loading: the game reported BlackScreen with playersOnly set, and captures were
black. No pad pulse was sent, so that run neither validates nor exercises this
new category. All four protected roots restored. See the
[native evidence](le2-validation-evidence.md#powers-back-adapter-built-next-run-stopped-at-blackscreen-before-input).

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

## Reuse audit before implementation

This table records the gaps found before the adapter was added. The implemented
checks and current proof limits are recorded below.

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

## Implemented structural context

`Category::CharacterRecord` now serializes as `character-record` in the existing
input context sampler. The focused panel must be a unique CharacterRecord with
the exact PC handler, active GUI mode and existing interactive/reciprocal checks.
A visible MessageBox overlay is refused even before focus transfers. Closing,
initial hiding, malformed arrays, foreign helper/world/pawn links and invalid
present callback identities are unavailable. The client and C++ protocol accept
the new category only for B with all triggers and sticks neutral; the host
independently repeats that restriction before installing input hooks. Existing
request identity, explicit opt-in, one-shot dispatch and neutral draining remain
unchanged. No new input verb, hook or transport was introduced.

The helper must be the exact `SFXPowerLevelUpHelper`, owned by the handler, with
the same world. The current character must be a unique listed source pawn in
that world at the handler's current index. `InitializeCharacterRecord` populates
the source-pawn array in squad `GetMember(index)` order, without sorting or
filtering. `ChangeToCharacter` obtains `m_CurrentPawnIndex` from the same squad's
`GetMemberIndex`, and `SFXPowerLevelUpHelper.SetPawn` retains the supplied pawn.
This supports another squad member without equating it to the controller pawn.
The game can append a null source pawn; this sampler deliberately supports only
bounded lists of valid same-world source pawns. That is a supported subset, not
a claim that every engine-created CharacterRecord list satisfies it.

The source initializes `m_InitialHiddenTimer` to a short positive duration and
unhides the UI pawn when it reaches zero. Positive or nonfinite values are
refused; an expired timer is not proof of completed native/GFx initialization.
The helper's `m_UpdatedPowers` is populated with every current power by SetPawn,
so its bounded count is named `powerSnapshotCount`, not pending purchases.
`pointsSpent` is a copied diagnostic integer. Power entry contents are not
audited, and `talentChangesAudited` remains false.

The context copies helper/current character identities, source-pawn header and
identities, power-snapshot header, current index, points-spent field, closing
state and optional close delegate. A missing delegate is allowed; a present
delegate requires a live object and readable non-None function name, with no
requirement that every opening came from a sequence action. JSON reports its
class/index/name without pointer handles or claiming function resolution or
execution. All copied state participates in the existing two-read and per-tick
continuity comparison. Changes cancel existing pulses; no callback is invoked.

Actual-SDK synthetic tests cover alternate squad characters, absent and arbitrary
valid callbacks, invalid/foreign roots, modal/closing states, array bounds and
unreadable storage, and callback/helper/points/closing changes. Production and
test translation units compile under the existing `/W4 /WX` target. Native
CharacterRecord context, B consumption and exit remain separate pending checks.

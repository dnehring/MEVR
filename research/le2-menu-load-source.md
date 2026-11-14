<!-- SPDX-License-Identifier: MIT -->
# LE2 normal menu load route

Read-only source investigation from `8072df34`. No native calls, deployment,
profile changes or game-derived code are included. This narrows the
[save/load entry-point handoff](le2-save-load-entrypoints.md): open the actual
initialized menu, observe its current list, then select its verified record.
It does not authorize a load or establish live menu readiness.

## Exact candidate calls

These are game functions, not console commands. Their parameters contain no
caller-owned FString. The game itself still performs normal UI/string allocation.

| Receiver / effective function | SDK flags | Declared parameters | Source role |
|---|---|---|---|
| `BioSFHandler_PCSplash.AdvanceScreen` | `0x00020802` (Event) | None; no return | PC splash's ordinary advance event; overrides `BioSFHandler_Splash.AdvanceScreen`. |
| `BioSFHandler_MainMenu.MoveToLoadGui` | `0x00020002` | `LoadMode`: byte at `0x0`, size 1, property flags `0x80` (Parm); no return | Main-menu HandleEvent case 6 calls with Default mode 0. |
| `SFXSFHandler_Load.LoadCareer` | `0x00020003` (Final) | `SaveIdx`: int at `0x0`; `GuiMan`: object pointer at packed `0x4`; no return | Checks current `CareerList` bounds, then initializes that career's load list. |
| `SFXSFHandler_Load.LoadGame` | `0x00820003` (Final) | `SaveIdx`: int at `0x0`; `GuiMan`: object pointer at packed `0x4`; no return | Checks current `SaveList` bounds and follows the ordinary confirmation/load flow. |

Both index/pointer parameter blocks have declared input extent `0x0c`, with
`CPF_Parm` (`0x80`) on each field. Generated wrapper names containing `exec` do
not establish `FUNC_Exec`. Script locals commented in generated parameter structs
are not input fields. The live `NumParms`, `ParmsSize`, `ReturnValueOffset`,
function owner and properties must be observed before calling. In particular,
the exact **void ReturnValueOffset is not proven** by these declarations; do not
invent zero or `0xffff`. An empty C++ struct's size is not the runtime parameter
size. The PC splash override must not be replaced by its base implementation.

## Splash is not a ready main menu

`MassEffectGuiManager.IsInSplashScreen` identifies panel tag `Splash`, a
`BioSFHandler_Splash` subtype in its handlers, and `bPressedStart == false`.
This is useful source identity, not a full readiness check.

The PC splash's `HandleEvent` case 2 enables input; case 3 invokes its effective
`AdvanceScreen`. The base non-axis input route calls `CheckLogin`; on PC that
immediately advances. The PC override disables direct GFx input forwarding before
the base path sets `bPressedStart`, disables input, requests the transition and
fires the existing `startpressed` event. `EntryMenu.pcc` contains that authored
event and the splash/transition/main-menu initialization sequence, including
`SFXSeqAct_InitMainMenu`. Do not manufacture a MainMenu panel to skip it.

Only consider this candidate when the actual reachable Splash panel is focused,
visible, initialized, not being removed, and input/events are enabled, with
`bPressedStart` false and its legal/transition state recorded. The source does
not justify advancing an uninitialized or input-disabled legal screen. After one
advance, observe the normal transition to a ready MainMenu; a timeout does not
justify repeating it. A controller in `EntryMenu` alone proves neither state.

## MainMenu to populated Load list

`MoveToLoadGui(0)` creates panel tag `Load` through the current main menu's
`oPanel.oParentManager`, obtains its default handler as `SFXSFHandler_Load`, sets
`bFromMainMenu=true`, `GuiMode=MainMenu (1)` and `LoadMode=Default (0)`, then hides
and disables the main menu. `SFXSFHandler_PCLoad` is an available PC subclass;
the concrete live handler must be observed rather than assumed from platform.

The opener does **not** synchronously establish a populated save list. The Load
handler's UI event case 1 calls `Initialize`. It can start in career selection
when the current descriptor's career is not cached. Otherwise it invokes
`InitializeLoadList` with default `CareerIdx=-1`; this is a legitimate default,
not automatically a corrupt index. `BeginInitializeLoadList`,
`EndInitializeLoadList`, `BeginInitializeCareers` and `EndInitializeCareers` are
native declarations whose implementation was not recovered. Do not manually
invoke completion callbacks or assume their ordering from a nonempty array.

If career selection is required, normal event case 7 calls `LoadCareer` with a
position in `CareerList`. Observe the resulting list again before choosing a
save. Normal event case 3 calls `LoadGame` with a position in `SaveList` and the
same `MassEffectGuiManager` derived from `oPanel.oParentManager`.

In main-menu/default-load mode, `LoadGame` invokes its confirmation callback
without an in-game lose-progress prompt. Other modes differ: in-game loading
prompts; NG+ and legacy load modes enter import paths. Require the intended mode,
not just a matching function name. The callback queues the selected descriptor
through the existing engine save system. Returning from `LoadGame` is not proof
that the world or selected fixture finished loading.

## Bounded observation roots and state

Use existing object-table membership, class, lifetime, thread and guarded-read
checks. The following offsets are pinned SDK layout facts, not validated memory
handles or permission to dereference arbitrary addresses:

- Start at the already validated `ULocalPlayer`: `ViewportClient` at `0x594`,
  then `UGameViewportClient.m_pScaleFormManager` at `0xb8`. The controller's
  `m_oCachedScaleFormManager` at `0x730` is a separate cache; report disagreement
  instead of choosing whichever pointer is nonnull. Native
  `GetScaleFormManager()` is not needed for a read-only observation.
- `UBioSFManager.m_lstPanels` at `0xa8` and `m_FocusPanel` at `0x108` provide a
  bounded reachable panel set and current focus. Require a unique target tag
  and consistent membership. A hidden background panel or modal overlay must
  not be treated as an active action target.
- `UBioSFPanel`: `ScriptHandlers` at `0xa4`, `nmTag` at `0x13c`,
  `m_DefaultHandler` at `0x144`, `oParentManager` at `0x14c`. The handler's
  reciprocal `oPanel` is at `0x80`; its `oWorldInfo` is at `0x88`.
  `MassEffectGuiManager.oBioWorldInfo` at `0x164` supplies another consistency
  check. Search only these bounded lists and deduplicate the same pointer;
  reject competing typed handlers instead of searching all globals for one.
- Panel flags at `0x170`: visible `0x1`, initialized `0x4`, to-be-removed `0x8`,
  input-disabled `0x20`, events-disabled `0x200`. Splash `bPressedStart` is
  `0xb0` mask `0x1`, with legal timeout at `0x9c`.
- MainMenu flags at `0x148`: connecting `0x4`, has-saves `0x8`, current-career
  has-saves `0x20`, waiting-for-DLC `0x80`, waiting-for-saves `0x100`,
  checking-data-complete `0x200`. Its setup forwards save availability to the
  UI; an opener should not bypass a disabled/unfinished menu.
- Load inherits `SaveList` at `0xc4`, flags at `0x158` (waiting-on-message `0x1`,
  enumerating-saves `0x2`, checking-data-complete `0x4`) and `GuiMode` at `0x15c`.
  Its `CareerList` is at `0x160`, `CurrentCareerIndex` at `0x1a8`,
  `bFromMainMenu` at `0x1ac` mask `0x1`, `LoadMode` at `0x1b0`.

These fields support a conservative current-state snapshot, not a universal
readiness theorem. Reject malformed arrays, ambiguous ownership, incomplete
enumeration and changed snapshots. Retain build/process/lease and observation
identity; never export raw addresses as reusable client handles.

## List-to-file identity: what is and is not established

`FSaveGUIRecord` has stride `0x70`. Its `SaveDescriptor` is at `0x0`, size `0x28`:
`Career` FString at `0x0`, `SaveName` FString at `0x10`, `Index` int at `0x20`,
and `Type` byte at `0x24`. The record also has `FriendlyName` at `0x28`,
`SaveGame` object pointer at `0x48` and timestamp components at `0x58..0x6c`.
Types are Manual=0, Quick=1, Auto=2, Chapter=3. `CareerList` entries contain
`CareerName`, player/class information, creation date and active-career state.

The **UI list position is not the descriptor Index**, and neither is an
established filename formatter. The inspected script passes the full descriptor
to the native queue. It does not show how `Career`, `SaveName`, `Index` and `Type`
resolve to a disk path. `DebugName` used by the cheat `LoadGame` command is yet
another identity. Do not infer `Save_0001.pcsav`, `Save0001.pcsav`, or a quick-slot
path from a UI number. Existing test/template paths do not prove native naming.

Next proof: capture the complete bounded descriptor of the intended visible
record, correlate it with the staged immutable file and career in the protected
profile, and preserve that mapping with the file hash. If `SaveName` is blank,
ambiguous or not directly the observed disk filename, leave the mapping pending
until native enumeration or independently observed selection resolves it. Do not
load a guessed match. Recheck the same initialized handler, mode and list entry
immediately before calling `LoadGame(index, sameGuiMan)`, then verify the actual
loaded state separately. Native path construction remains an explicit gap.

## Source anchors and reproduction

SDK pin `6bd05809cccb1a099a520371a95779d4a56bb246`: exact identifiers above in
[SFXGame function declarations](../external/LExSDKv2/Src/LESDK/LE2/SFXGame_f_structs.hpp),
[SFXGame classes](../external/LExSDKv2/Src/LESDK/LE2/SFXGame_classes.hpp),
[record layouts](../external/LExSDKv2/Src/LESDK/LE2/SFXGame_structs.hpp), and
[Engine classes](../external/LExSDKv2/Src/LESDK/LE2/Engine_classes.hpp).
Use the existing [read-only scanner](../tools/conversation-scan/README.md) with
`--game LE2 --only SFXGame --script Class.Function`; the three manager/panel
getter declarations use `--only Engine`. Use `--sequence EntryMenu` for the
authored entry flow. Keep all output private.

Inspected package SHA-256 values: SFXGame
`681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77`;
Engine `d5e80354534935c674e901a6a7a889bfffa78d04bfaf15d32885d88630c44202`;
EntryMenu `0de6222db084fc5b88b487de94e2efa7491aea78c661dfb0884c7839ef129c89`.
Scanner/Core DLL pins match the preceding save/load handoff. This inspection
does not prove mounted override precedence, exact live void-function metadata,
filename mapping, native call safety or a completed fixture load.

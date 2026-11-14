<!-- SPDX-License-Identifier: MIT -->
# LE2 normal new-save source handoff

Offline investigation from `e67f3cd5`, following the proven frozen-fixture load.
No native calls, launches, deployment or save changes. This identifies reuse
points for the next save/reload gate; it does not make a save action available.
Read the [entry-point audit](le2-save-load-entrypoints.md),
[menu object-chain handoff](le2-menu-load-source.md) and
[latest evidence](le2-validation-evidence.md).

## Normal route and exact candidate interfaces

| Effective declaration | SDK flags | Packed input/return extent | Role |
|---|---|---|---|
| `SFXGameModeBase.ShowMenu` | `0x00020202` (Exec) | No parameters | Current gameplay mode opens the pause menu when permitted. |
| `MassEffectGuiManager.ShowBrowserWheel` | `0x00024803` (Final/Event) | Optional bool DWORD at 0, 4 bytes; source default true | Manager's normal pause/menu implementation; not an eligibility bypass. |
| `BioSFHandler_BrowserWheel.ShowSelectedPanel` | `0x00824803` (Final/Event) | int `nPanel` at 0; optional `BioSFManager* oManager` at 4; 12 bytes | Opens the configured page of the current initialized pause menu. |
| `SFXSFHandler_Save.HandleEvent` | `0x00C20002` | byte `Command` at 0; const/out `TArray<FString> lstArguments` at 4; 20 bytes | Command **11** is normal New Save, with an empty argument array. |
| `SFXSFHandler_Save.PrepareSaveGame` | `0x00020003` (Final) | No parameters | Internal continuation; unsuitable as a standalone New Save request. |
| `SFXSFHandler_Save.GetNewSaveIdx` | `0x00020003` (Final) | int return at 0, 4 bytes | Game-owned next-index calculation, invoked by the save flow. |
| `BioPlayerController.CanSave` | `0x00420802` (Event) | out FString `Reason` at 0; bool DWORD return at `0x10`; 20 bytes | Eligibility query with engine string ownership, not a scalar-only shortcut. |

These are declarations, not live metadata. Verify effective owner/overrides,
function flags, parameter identities/types/flags and actual `ParmsSize` before
entry. Void ReturnValueOffset remains an observed value, not a guessed constant;
empty C++ struct size is not the native block size. `ShowSelectedPanel` property
flags are `0x80` / `0x90`; Save HandleEvent fields are `0x80` / `0x400182`.
The latter's array inner property must be FString, with an actually constructed
empty SDK array at the packed offset; do not substitute a dangling pointer or
assume an inactive zeroed union supplies C++ lifetime. Ordinary string arguments
are unnecessary for command 11. This inspection did not compile a caller.

`ShowMenu` on the base checks `bAllowPauseMenu`, clears sprint intent and calls
the controller's manager. Command, power/weapon wheel, vehicle and photo modes
have overrides with additional behavior. Select the current effective mode;
never call the base to bypass an override. An initial implementation can restrict
support to a verified ordinary on-foot default mode. `ShowBrowserWheel` also
checks the world's browser permission, cancels squad leader activity, records
prior pause state, enables GUI mode and creates the normal browser. Its native
internal calls and game-owned allocations remain part of normal initialization.

## Current object chain and readiness

Reuse the validated local-player/controller/viewport/Scaleform-manager chain and
bounded reachable panel list from the existing observer. Check reciprocal
manager/panel/handler/world references, exact classes, membership, lifecycle,
focus, visibility, initialization, input/events and no modal/removal state.
Additional pinned SDK anchors:

- Controller `GameModeManager2` at `0xa68`; manager `GameModes` at `0x70` and
  `CurrentMode` at `0xb8` (Default enum 0, GUI 7). Mode flags at `0xc8` include
  active `0x2`, allow-save `0x400`, allow-pause-menu `0x1000`. Their presence is
  not proof that native `AllowsSaving()` is just one bit: that function is opaque.
- `ReturnToBrowserWheel` creates panel tag **`MenuBrowser`**. Manager
  `m_oBWHandler` at `0x154` is a cache to cross-check, not a global lookup shortcut.
  PC handler `BioSFHandler_PCPauseMenu.bWaitForNextFrame` at `0xc4` mask 1 is set
  on addition and cleared by its Update; HandleEvent refuses while set.
- Browser handler `lstPages` at `0x9c` contains `BWPageStruct`: Tag at 0,
  handler pointer at 8, label at `0x10`, Type at `0x14`. The save **Type is 1**;
  this is not necessarily UI position 1. Derive a unique bounded current row and
  inspect its configured Tag. This investigation did not establish the effective
  config's literal save-panel Tag; a class-dump search returned no instances.
- `ShowSelectedPanel` creates that Tag, removes the browser, attaches the shared
  in-game GUI handler, marks fullscreen and invokes normal additional processing.
  It checks only an upper bound in source: the adapter must reject negative and
  out-of-range rows. It does **not** recheck the page's disabled state or CanSave.
- Validate the resulting `SFXSFHandler_PCSave` / Save handler, not the Load
  subclass, through the current panel. PC OnPanelAdded exposes the mouse then
  calls the base. The UI's Save HandleEvent command 1 initializes save mode and
  calls native `BeginInitializeSaveList`; neither command 11 nor panel existence
  is proof initialization finished.
- Save fields: list at `0xc4`, MaxSaves `0xec`, PendingOverwriteIndex `0xf0`,
  PreparedSaveSize `0xf4`; flags at `0x158`: waiting-message 1, enumerating 2,
  checking-complete 4, checking-message-visible 8; GuiMode at `0x15c`, browser
  mode 0. Use complete bounded enumeration, no pending message/operation, and
  observed normal initialized UI; native Begin/End enumeration internals remain
  opaque. A flag alone is not a completion theorem or disk inventory proof.

## Eligibility must not be bypassed

`BioSFHandler_BrowserWheel.InitializeBrowser` calls the current controller's
`CanSave(out Reason)` and records Save's alert as normal 0 or disabled 2 in
`BioWorldInfo.m_lstBrowserAlerts[1]` (array at `0xb10`). It then applies
`SubPageStateOverrides` (array at `0x8d0`) when forming the UI state; an override
cannot enable Save when CanSave was false, but can disable an otherwise allowed
page. Preserve that effective-state logic and freshness, not just the raw alert.

CanSave refuses editor play, scripted no-save, combat/forced combat, vehicle,
mode-manager refusal and dead pawn; console builds also check login/storage.
Its reason may allocate an engine FString. The menu owns this invocation and
string internally. Calling it independently requires the remaining output-string
ownership proof; inspecting a readable return header would not establish that.

Neither ShowSelectedPanel nor command 11 independently repeats CanSave. The
normal UI uses a sampled decision; an adapter need not invent a stricter global
save theorem or independently allocate CanSave's output string. Preserve the
same delegated decision and menu lifetime:

1. Open the ordinary pause menu and establish that this new browser completed
   its normal initialization with Save effectively enabled. OnPanelAdded alone
   does not establish this: UI HandleEvent command 3 invokes InitializeBrowser,
   and a zero-initialized or old alert value could look normal. Current rendered
   enabled UI plus its fresh initialized panel/context is useful evidence; a
   narrowly scoped witness of the game's own initialization is an optional
   implementation aid, not a required new global hook framework.
2. Retain world/controller/pawn/manager identity and the effective Save page
   decision, including overrides. ShowBrowserWheel sets `bPlayersOnly=true`
   and enables GUI mode 7. ShowSelectedPanel removes the browser but does not
   unpause or disable that mode; carry the admission sample into the resulting
   Save panel only while that paused GUI context remains continuous.
3. Require fresh initialized Save UI with complete enumeration and no modal or
   operation in progress, then submit its normal New Save event. Invalidate the
   admission sample if the user exits/reopens, identity changes, relevant
   eligibility/override fields change or the GUI pause context is lost.

`HideBrowserWheel` disables GUI 7 and restores the previous `bPlayersOnly` value;
shared in-game GUI input can invoke it, so panel continuity must be observed.
`bPlayersOnly` is source-backed pause state, not a claim that every engine field
is immutable. The Save path itself handles preparation/free-space failures.
Current mode's allow-save bit alone must not replace native AllowsSaving. If
fresh normal enabled-UI evidence is unavailable, report that specific gap and
retain the ordinary UI/operator boundary. No host-owned CanSave FString call,
rewritten save flags or fabricated eligibility callback is needed for this route.

## New slot and asynchronous completion

Command 11 sets PendingOverwriteIndex to **-1** before PrepareSaveGame. Command
2 is overwrite and opens a confirmation; its callback sets an existing-row
index. Thus calling PrepareSaveGame alone could retain overwrite state.

The normal continuation disables input through GFx `loadSave.SetInputEnabled`,
not by directly setting the panel input-disabled bit. That bit alone cannot
identify an in-flight save; preserve the one-shot request/operation lifecycle.
It queues PrepareSave (action 9), requires successful preparation plus a supplied
size, queues QueryFreeSpace (8), then
requires success plus supplied free bytes. On sufficient space it enables normal
storage writing and calls controller SaveGame with the game's GetNewSaveIdx.
The overwrite branch instead calls OverwriteGame, which can delete an existing
save on PC. Preserve the new-save event and its callbacks; do not call the
overwrite branch, free-space callback or queue directly.

GetNewSaveIdx returns 1 for an empty list; otherwise it increments the maximum
observed descriptor Index, starting from -1. It does not search for a hole or
accept a caller filename. The current list must be complete and stable, with
bounded non-overflowing indices and room under the game's actual save limit.
Native enumeration/max-save UI behavior is not recovered, so this formula alone
cannot prove a destination is absent on disk. Preserve a before-inventory and
refuse unsupported/ambiguous state; let the game select its index and archive
actual new files rather than assuming the next file's name.

Controller SaveGame creates a descriptor with that Index and delegates to engine
SaveGameEx, which queues Save (2) with `SFXEngine.SaveGameCallback`. The UI returns
to the browser/normal game and displays a saving widget before persistence is
established. The free-space handler subsequently queues action 10. Native queue
scheduling and file-name generation remain opaque; a closed panel or visible
saving icon is not proof of success.

Callback metadata: `QueryFreeSpace` / `CheckFreeSpace` flags `0x20003`,
`SFXEngine.SaveGameCallback` `0x40003`, native `EndInitializeSaveList` `0x20400`.
Each has one `SFXSaveGameCommandEventArgs` input at 0, size `0x5c`, flags
`0x400080`. The record contains descriptor at 0 (size `0x28`), arrays at `0x28`
and `0x38`, free-byte value at `0x4c`, prepared size at `0x50`, flag DWORD at
`0x54` and Action byte at `0x58`. Flag masks: success 1, retry 2, pause 4,
needs-space 8, free-bytes-set `0x10`, prepared-size-set `0x20`.
On successful engine SaveGameCallback the game updates its current descriptor,
career cache and pending profile state. Observe the real correlated callback if
implemented; never invoke it to fabricate completion. A returned New Save event
only proves submission. Native `IsPerformingSaveAction` is another declaration,
not an established read-only query or completion proof.

For persistence acceptance, retain a successful correlated completion if
available, wait for a stable new file, preserve prior-save hashes, archive/decode
the new save independently, and compare resources/story/location to live and
fixture expectations. Normal close plus protected restoration must preserve the
new output privately. A second protected run may stage that generated file and
reuse the proven normal main-menu descriptor load; record fresh scene/state and
compare values again. Keep native save, reload/persistence and VR gates separate.

## Reproduction and limitations

SDK pin `6bd05809cccb1a099a520371a95779d4a56bb246`: exact identifiers above in
[SFXGame functions](../external/LExSDKv2/Src/LESDK/LE2/SFXGame_f_structs.hpp),
[classes](../external/LExSDKv2/Src/LESDK/LE2/SFXGame_classes.hpp) and
[records](../external/LExSDKv2/Src/LESDK/LE2/SFXGame_structs.hpp).
Installed SFXGame package SHA256
`681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77`;
scanner SHA256 `918a095735f00998e5ff99871902253c47c66069a6cce3e6d6b0d474addd6f01`,
Core DLL pin as in the entry-point audit. Use the existing read-only scanner with
`--game LE2 --only SFXGame --script Class.Function`. `grep:ShowMenu` finds the
mode overrides; grep searches script text, not full qualified identifiers.
Private extraction is under `local-tools/le2-normal-save-research/`, with prior
save callback bodies in `local-tools/le2-save-load-research/save-functions.txt`.
No extracted game code is included in this original summary. Mounted overrides,
actual live layouts/targets, effective config, save availability and completion
correlation still require independent implementation and protected native proof.

<!-- SPDX-License-Identifier: MIT -->
# LE2 save, load and exit entry points

Offline investigation at `1725a3bf`; no game calls, save changes or deployment.
Reuse the game's save queue and menus after the existing native-call proof gates
pass. A function declaration, console return or queue submission is not proof
that a save finished or the intended fixture loaded.

## Sources and scope

- LExSDKv2 pin `6bd05809cccb1a099a520371a95779d4a56bb246`:
  [LE2 function parameter declarations](../external/LExSDKv2/Src/LESDK/LE2/SFXGame_f_structs.hpp),
  [save descriptors and callback records](../external/LExSDKv2/Src/LESDK/LE2/SFXGame_structs.hpp),
  [action/type enums](../external/LExSDKv2/Src/LESDK/LE2/SFXGame_classes.hpp).
  Identifiers below are exact lookup anchors. Live reflection must still confirm
  the effective function, parameter layout, object and game-thread context.
- Installed LE2 `SFXGame.pcc`, SHA-256
  `681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77`,
  inspected read-only using [the existing scanner](../tools/conversation-scan/README.md).
  This establishes the inspected base-package script behavior, not complete
  mounted override precedence or the implementation of native functions.
- Scanner executable SHA-256
  `918a095735f00998e5ff99871902253c47c66069a6cce3e6d6b0d474addd6f01`;
  LegendaryExplorerCore SHA-256
  `d980a622e225cbc0dd1d1c5719a74ea1cb59284abe0e7855f354afbc6baf7d74`.
  Raw game-derived output remains private under ignored `local-tools/`.
- Licensed Halcyon ME2 source at
  `reference/MassEffect2-VR`, clean pin
  `4784e90649a0398c6812b89fc09f4a0dbcce6441`, was searched under `src/` for
  `QuickSave`, `QuickLoad`, `SaveGame`, `LoadGame`, `QuitGame`, and `ConsoleCommand`.
  No implementations of these routes were found. Its renderer/exit hooks are
  not evidence for save selection or a normal game quit command.

## Console routes versus reflected functions

`exec` in a generated C++ wrapper name alone does **not** mean console-callable.
Check `FUNC_Exec` and the package declaration. The following command spellings
come from actual exec declarations, except `ReturnToLauncher`, which is an
exact string used by the normal menu. Parser quoting, cheat-manager availability
and successful console dispatch remain untested.

| Route | Source-proven input and behavior | Automation limit |
|---|---|---|
| `QuickSave` | `BioPlayerController.QuickSave`, `FUNC_Exec`, no parameters. Requires an engine and successful `CanSave(Reason)`, enables save-to-storage, and submits a Quick-type descriptor through `SaveGameEx`. | Uses the quick-save mechanism, potentially replacing its existing slot. A rejected `CanSave` can produce no console text; do not equate an empty return with success. |
| `QuickLoad` | `BioPlayerController.QuickLoad`, `FUNC_Exec`, no parameters; calls controller `ResumeGame`, then engine `ResumeGame`. | Engine starts with `GetCurrentSaveDescriptor()` only when `TravelURL` is empty. On failed fast resume, `FastResumeGameCallback` can submit another Load with an omitted descriptor. It is neither a filename selector nor a guarantee of the quick slot or intended fixture. |
| `SaveGame <SaveName>` | `BioCheatManager.SaveGame(string SaveName)`, `FUNC_Exec`; writes `CurrentSaveGame.DebugName` and queues Save with an omitted descriptor. | Debug naming, not a verified file naming API. This script does not perform the controller's `CanSave` check and assumes current engine/save objects. Do not use it as the normal safe-save route. |
| `LoadGame <LoadName>` | `BioCheatManager.LoadGame(string LoadName)`, `FUNC_Exec`; enumerates saves, then its callback matches a save's `DebugName` and calls `SFXEngine.LoadSaveGame` with the save object. | The argument is **not** a `.pcsav` path or numbered slot. The first matching enumerated debug name wins; uniqueness and enumeration scope are unproven. |
| `LoadExternalSave <SavePath>` | `BioCheatManager.LoadExternalSave(string SavePath)`, `FUNC_Exec | FUNC_Native`; one `FString` parameter. | Native implementation is opaque here. Accepted path syntax, validation, profile association and side effects are not established. It is a candidate to investigate, not the selected normal-load implementation. |
| `ReturnToLauncher` | `BioSFHandler_PCPauseMenu.ExitConfirm(true, context)` sends this exact command through `WorldInfo.ConsoleCommand`. The main-menu confirmation schedules its `ReturnToLauncher` method, which sends the same command. | This is the normal return-to-launcher route, not proof that the launcher also exits. Do not substitute an assumed `quit`/`exit` command or a process kill. The current player-controller console route still needs its own dispatch proof. |

Parameter anchors: `SFXGame_f_structs.hpp` contains controller `QuickLoad`,
`QuickSave`, `CanSave`, `SaveGame`, `SaveGameEx`, `ResumeGame`; cheat-manager
`LoadExternalSave`, `LoadGame`, `SaveGame`; and both menu `ExitConfirm` functions.
The cheat string parameters occupy `0x10` bytes. The no-parameter script wrappers
must not be mistaken for a one-byte native parameter block merely because an
empty C++ struct has nonzero `sizeof`.

## Normal menu and queue route

For a selected fixture, prefer investigating the current menu's descriptor
selection rather than inventing map travel or assuming a filename command.

- `SFXSFHandler_Load.LoadGame(int SaveIdx, MassEffectGuiManager GuiMan)` is a
  non-exec function. Its arguments are `SaveIdx` at `0x0` and an object pointer
  at packed offset `0x4`. It checks `0 <= SaveIdx < SaveList.Length`.
  `SaveIdx` is a position in that live list, not `Save0036`'s numeric suffix.
  Main-menu/game-over modes proceed directly; an in-game request presents the
  normal lose-progress confirmation. The handler needs its real initialized
  panel, manager, load mode and save list.
- `Callback_ConfirmLoadGame(bool bAPressed, int context)` uses the selected
  descriptor. It distinguishes ordinary load from ME1 import and NG+ modes,
  clears import state, disables panel input, and queues Load with its completion
  callback. Calling this callback without the preceding menu state would bypass
  its list bounds check and confirmation; it is not a standalone load API.
- `SFXSFHandler_Save.PrepareSaveGame()` runs prepare-save and free-space checks.
  `CheckFreeSpace` enables storage writes and uses controller `SaveGame` for a
  new manual index, or `OverwriteGame` for the selected existing record.
  `GetNewSaveIdx()` derives an index from the enumerated save list. Do not guess
  an unused index or directly replace these checks with `SaveGame(int)`.
- `BioPlayerController.SaveGame(int SaveIndex)` and
  `SaveGameEx(SFXSaveDescriptor)` are non-exec helpers. The latter forwards to
  `SFXEngine.SaveGameEx`, which queues Save with `SaveGameCallback`.
  On callback success, the engine updates the current save descriptor/cache.
  This is asynchronous persistence, not completion at ProcessEvent return.

`SFXEngine.QueueSaveGameCommand` is native, non-exec, with action byte at `0x0`,
optional descriptor at `0x4` (size `0x28`) and optional delegate at `0x2c`
(size `0x10`). The descriptor contains `Career`, `SaveName`, `Index`, and `Type`;
it is not interchangeable with a bare path. Source enums identify Load=1,
Save=2, EnumerateSaves=7, QueryFreeSpace=8 and PrepareSave=9; save types are
Manual=0, Quick=1, Auto=2, Chapter=3. These values are references, not permission
to bypass menu initialization or call the native queue directly.

`CanSave(out FString Reason)` returns a boolean at `0x10` after its string at
`0x0`. The inspected script rejects editor mode, Kismet no-save, combat/forced
combat, vehicle pawns, game modes that disallow saving, and dead pawns, with
additional console-platform login/device checks. Its dependencies include
`WorldInfo`, a live pawn and `GameModeManager2`; an entry-menu controller alone
is insufficient. `SFXEngine.IsSaving()` and
`IsPerformingSaveAction(ESFXSaveGameAction)` are native query candidates, not
already proven readiness indicators.

## Smallest next validation sequence

1. Keep native dispatch unavailable until the existing target/allocator/output
   proof in [the console reuse audit](le2-native-console-reuse.md) is satisfied.
   Identify the actual controller, cheat manager if used, menu handler and
   effective function metadata on the owning game thread.
2. Use the protected profile and normal Load Game to establish one immutable
   fixture. Read the menu's populated descriptor/list identity and associate it
   with the staged file hash. Reject ambiguous or changing selection rather
   than assigning a UI index from a filesystem name.
3. Prove one ordinary allowed save and its completion: successful native callback
   plus a newly completed, parseable save with the expected resource/story state.
   Preserve the old slot and original fixtures. A dispatch receipt is separate.
4. Prove selecting that exact save through the normal load route, then compare
   actual map/player/start evidence and parsed resulting state. Treat quick
   resume's fallback as a source of ambiguity; no blind retry on a timeout.
5. Prove normal `ReturnToLauncher` only after persistence is settled. A departing
   process may prevent a final transport reply. Reconcile using the owned
   process identity and restored session state, without fabricating a completed
   response or killing another session's game.

## Bounded returned-string follow-up

The pinned SDK exposes `FString::Chars()`, `Length()` and `Capacity()` in
[FString.hpp](../external/LExSDKv2/Src/LESDK/Common/FString.hpp). Its protected
storage is a `TArrayBase<WCHAR,true>` with `Data`, unsigned `CountItems`, and
`CountMax` in [TArray.hpp](../external/LExSDKv2/Src/LESDK/Common/TArray.hpp).
Length/capacity subtract one when nonzero, accounting for the terminator;
`Chars()` returns a static empty string for zero count, which hides the actual
stored pointer. These convenience getters do not validate a corrupted header.
Destruction clears/shrinks the array and calls `sdkFree`.

The smallest future adaptation should snapshot and validate the actual bounded
16-byte header, preserve the packed ABI, cap count/capacity and copy only a
validated UTF-16 extent into owned host memory with a guarded read. Require a
final terminator and distinguish successful empty output from malformed headers,
unreadable storage and strict UTF-16 conversion failure. Use explicit lengths
and checked `WideCharToMultiByte` results, not the current unbounded `-1` scan.
Readable memory and plausible counts still do **not** establish ownership.

Input allocation also needs an always-active null/overflow check before copy:
the SDK string append path can reach `TArrayBase::Reserve`, whose initial
allocation result is not independently checked before subsequent use, and
`LESDK_CHECK` becomes a condition-only expression in Release. Preserve
`sdkMalloc`/`sdkFree` from
[Common.cpp](../external/LExSDKv2/Src/LESDK/Common/Common.cpp), which call the SDK's
`GMalloc`; do not mix heaps or patch the SDK submodule casually. A tiny adapter
would need an explicitly reviewed way to install a checked allocation/header,
not an aliasing cast to a private member or unchecked `FString` constructor.

The current ordinary C++ lifetime guards are not native-fault recovery. After a
native fault or corrupted returned header, do not call the output destructor or
free an untrusted pointer simply because it is readable. Any future diagnostic
path must retain uncertain ownership, disable further dispatch, and distinguish
intentional quarantine from normal cleanup. Even a valid output header requires
proof that the return allocation belongs to the SDK engine allocator. No such
adaptation or ownership claim is implemented by this research.

To reproduce package inspection, use the scanner README's private-output
workflow with `--game LE2 --only SFXGame`, `--script grep:SaveGame`,
`--script grep:LoadGame`, and individual `Class.Function` identifiers above.
`--script grep:quit` returned no matches in this package; that negative search
does not prove the engine lacks a native quit command. Never commit the raw
package-derived output.

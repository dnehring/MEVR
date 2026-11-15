<!-- SPDX-License-Identifier: MIT -->
# LE3 custom menu pause

2026-11-15. Source-derived native adapter and offline ownership tests. Live pause, native
admission, scene changes and restore acceptance remain pending; compilation is not runtime proof.

## Existing systems and installed contracts

Keep `game/MenuPause`, its shared main/squad requests and `core/vr/PauseOwner`. Do not open
a second native wheel or invent a separate pause state. LE1's existing call and LE2's existing
captured-bit implementation retain their behavior.

The existing private Legendary Explorer scanner inspected these installed LE3 exports:

- `Engine.pcc`: `WorldInfo.PauseGame` sets the world's player-only bit.
- `SFXGame.pcc`: `BioWorldInfo.PauseGame` overrides that function. It permits the change only
  when its replication info is not an `SFXGRI`, or that object's `bPauseForCommand` permits it.
- `SFXGameModeWeaponWheel.Activated/Deactivated` and the corresponding power-wheel functions
  call the world's pause function. Their other UI, input, tactical-order and sound work
  belongs to those native wheels and is not part of our custom-menu pause.
- `SFXGUIInteraction.ShowBrowserWheel/HideBrowserWheel` capture the old player-only bit and
  restore it through the world's pause function when the browser closes.

The override is why simply enabling the LE2 direct field write would be wrong. The LE3
adapter resolves the live world's method through the bounded `ue3/UnambiguousMethod` helper
and accepts only the audited `BioWorldInfo.PauseGame` function. Unknown subclass or state
overrides are refused. `Params` supplies the named boolean; the player-only bit is read
back after dispatch. A successful ProcessEvent invocation alone does not mean pause was accepted.
SDK comparison rows cover the bit, replication pointer/policy and exact function parameter.
Private script output stays ignored; no game script is included here.

## Ownership and transitions

Acquisition captures the previous bit only after observing the requested state. A refused
native pause acquires nothing. An existing owner cannot be overwritten by another acquisition.
Restoration targets the captured world and saved value, never a new world's current state;
the existing liveness/name check rejects a vanished owner. A failed native restore retains
the owner for retry. Closing either menu cannot release pause while the other still requests it.

LE3 uses existing `GameAdapter` semantic modes: acquire only on foot, in a vehicle or Atlas.
Native wheels, conversation, cinematics, GUI, injured Shepard and galaxy/orbital contexts do
not acquire custom pause. This does not grant traversal in authored seated scenes.
The menu's existing once-per-opening refusal policy remains; it does not repeatedly force a
native policy that says no. The status records native refusal or pending restoration.

## Verification and native acceptance

Offline tests extend the existing PauseOwner cases with native-refusal acquisition, invalid
identity, attempted owner replacement and failed-restore retention. Existing tests cover
previously paused worlds, world changes and disappeared owners. Compile MenuPause for LE1,
LE2 and LE3, and compare the LE3 metadata against its SDK. No new test runner is needed.

In the existing protected game session, first validate the new metadata. With an ordinary
gameplay fixture, observe `bPlayersOnly` and `SFXGRI.bPauseForCommand` before opening the main
menu, while open and after close. Repeat for the squad menu, menu overlap/hand-off, toggling
pause off, and an already-paused world. Native policy refusal must leave the original bit
untouched and report no acquired pause. Repeat after normal save load/world replacement;
the new world must never receive the old world's restoration.

Check a native-menu transition and module unload while paused. If policy changes after
acquisition and refuses restoration, the adapter deliberately does not bypass it. A pending
restore is not successful cleanup: the coordinator must resolve it in the owned session
before unload/restore acceptance. This rare transition needs native evidence rather than
an unconditional field-write fallback.

## Hot-reload restoration gate

`MevrModPrepareUnload` is an optional exported preflight. LE3 attempts the same captured
pause restoration before teardown. If it fails, `ModLoader::Unload` returns before touching
callbacks, renderer state, the DLL or its pending source stamp. The normal half-second poll
retries, and the current module keeps ticking. The host does not load a replacement beside
an unresolved owner. This is distinct from `keepLoaded`, which only pins code after teardown.

Success is tentative: no permanent shutdown flag is set. `MenuPause::Shutdown(false)` resets
the pause request latch on success, so if normal ticks continue without teardown, an open
main/squad menu can acquire again from its live request. A failed attempt retains the owner.
Process exit bypasses preflight and keeps the existing no-wait shutdown path. Older modules
without the optional export keep their prior behavior. An older host cannot honor this new
gate: native hot-reload acceptance requires deploying both the updated host and module.

`tests/ModReloadTests.cpp` includes the real loader implementation with callback/log stand-ins,
a private invalid-image candidate and an explicit Windows system-library reference. It
checks refusal without callback detach, continued ticks and retained pending stamp; repeated
refusal with a bounded log; later success with one shutdown/load attempt; missing-export
compatibility; and process-exit bypass. It never loads a game or changes installed runtime files.

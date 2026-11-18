---
type: Audit
title: ME3 audio against ME1's contracts
description: None of ME1's audio work runs on ME3; six contracts are n/a because ME3 is taken to play through Wwise (never verified from the exe), fourteen are missing, and unlike ME2 the mod's sound modules are compiled out of ME3's tick, so not even their stand-down or their console exists there.
tags: [audit, me3, parity, audio, wwise, voices]
status: draft
timestamp: 2026-11-18T10:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:00:00Z }
game: LE3
against: ../../me1/audio.md
commit: 583591c6
---

# Verdict

Audited against [the ME1 catalog file](../../me1/audio.md) by reading code and notes at 583591c6; no game was run and no
audio was recorded. Every claim below rests on reading alone.

- **ME3 gets nothing of this category from shared code in effect.** The game-independent rules (`cw::PlaceVoice`,
  `core/vr/SoundPlace`) and `engine/VoicePlace` compile into the LE3 mod, but `Mod.cpp` compiles their calls out
  (`#if !defined(MEVR_GAME_LE3)`), the ISACT hook has no ME3 address, and the host's OpenAL Soft switch is LE1's.
- **Finding 1: "ME3 is Wwise" is still an assumption on record.** The ledger's cell says "[verify] the exe's
  imports" for both sequels; no ME3 note names a loaded audio library. ME1's `[Audio] trace` (AUD-17), the tool that
  would log it, is not in the LE3 host.
- **Finding 2: nobody has measured whether ME3 has the problem ME1 solved.** `mevr-audio-loopback` and
  `tools/audio-balance.py` are game-independent and were never pointed at ME3. Until they are, AUD-07 to AUD-12 are
  "missing" by code and "unknown" by need; on the 3D screen (ME3's only conversation presentation) a centred voice is
  the right mix anyway.
- **Finding 3: ME3 has less than ME2 here, by a deliberate guard.** ME2 ticks `sound_place` and logs its stand-down;
  ME3 never calls it, so `[Audio]` keys are not read, nothing is logged, and `mevr voices` is not a command
  (`RunOursLe3` has no `voices`, `convo`, `moment` or `stream` verb). A lane that looks for audio on ME3 finds no
  trace that anything stood down.
- The menu is honest: the Sound page shows on ME3 with both groups greyed, "Not available in Mass Effect 3 yet" and
  "Now: Mass Effect 3 native audio (Wwise)". Thespian on ME3 keeps Shepard's recording (`kSkipSpokenReply = false`).

| rating | count | ids |
|---|---|---|
| same | 0 | |
| same core, other inputs | 0 | |
| adapter | 0 | |
| reinvented | 0 | |
| partial | 1 | AUD-16 |
| missing | 14 | AUD-07 to AUD-15, AUD-17 to AUD-21 |
| n/a | 6 | AUD-01 to AUD-06 |
| unknown | 0 | (the n/a rows carry an open verification, see "What to do" 1) |

Against the ledger: G37 "n/a: same" and G38 "missing" are right as far as code goes. G38's next step ("Wwise needs a
placement hook of its own") skips the question of whether placement is needed.

# Contract by contract

| id | rating | how ME3 gets it | where it differs | evidence |
|---|---|---|---|---|
| AUD-01 | n/a | ME3 is taken to play through Wwise, with no OpenAL router to detour. `openal_soft::Attach` is called only under `MEVR_WITH_HALCYON` and `MEVR_GAME_LE1`; `plugin/OpenAlSoft.cpp` is in LE1's host list only. What ME3 has instead: Wwise's own mixer and the spatialization its banks were authored with (not read). | Not verified from `MassEffect3.exe`'s imports or loaded modules. | `src/plugin/Entry.cpp:260-264`; `CMakeLists.txt:883-884` (inside the LE1 list 728-976), `:1143-1192`; [ledger](../../../trilogy-parity-ledger.md) G37 |
| AUD-02 | n/a | As AUD-01: no OpenAL device or context to wrap. Whether Wwise's output on ME3 is panned or binaural is not known. | The run that settles it: a loopback recording of a placed sound to one side, read for interaural delay as ME1's was. | as AUD-01 |
| AUD-03 | n/a | `[Audio] spatial` is read by no ME3 code. | The menu shows the three choices greyed. | `assets/menu/main.rml:323-327` |
| AUD-04 | n/a | No switch, so no fail-safe; the game's own audio always plays. | | as AUD-01 |
| AUD-05 | n/a | `[Audio] gainDb` is read by no ME3 code; no `ALSOFT_CONF` is written. | | as AUD-01 |
| AUD-06 | n/a | `GameSpec::audio` is false for LE3 (and LE2), so Setup installs no OpenAL Soft payload; `tools/deploy-asi.ps1` copies it for LE1 only. | This closes the ME2 audit's open check for both sequels. | `src/install/Game.cpp:25-29`; `src/install/Game.hpp:42-45`; `src/install/Installer.cpp:319`; `tools/deploy-asi.ps1:167` |
| AUD-07 | missing | Needs a first-person conversation (not on ME3, see [the conversations audit](conversations-and-scenes.md)) and the source hook. `cw::PlaceVoice` is compiled and unit-tested, game-independent; `conversation_world::Tick` and `EnsureVoicePlaceHook` are compiled out on LE3. | Whether ME3's dialogue is centred as ME1's was is unmeasured. | `src/mod/Mod.cpp:1915-1921`; `src/engine/Addresses.hpp:495` |
| AUD-08 | missing | No rule runs; every ME3 voice is the game's own mix at any distance. | | as AUD-07 |
| AUD-09 | missing | As AUD-07. | ME1's own falloff exists because ISACT only pans; Wwise applies authored attenuation, so the 2 m, 3 dB, 6 dB numbers may be unnecessary or wrong. Measure before copying. | as AUD-07 |
| AUD-10 | missing | `sound_place::Tick` is not called on LE3 at all. | ME2 calls it and logs "the sound source hook is not in"; ME3 is silent. The need is unmeasured. | `src/mod/Mod.cpp:1923-1927` |
| AUD-11 | missing | The rule (`vr::sound_place::Overheard`) compiles and is unit-tested; nothing feeds it. | | `src/core/vr/SoundPlace.hpp`; as AUD-10 |
| AUD-12 | missing | As AUD-10. | ME3's weapon sounds are Wwise events; which game object they play on is the open question ME1 had about owners. | as AUD-10 |
| AUD-13 | missing | The publisher lists exist in the LE3 build and no source is ever placed. Thespian on ME3 does not publish its mute (`kSkipSpokenReply` false) and only clears its list. | | `src/engine/VoicePlace.hpp:24`; `src/game/Thespian.cpp:41-43`, `:489` |
| AUD-14 | missing | `voice_place::Install` is never called on LE3 (its caller is compiled out); called, it would return false at once ("another game: no address"). | | `src/mod/Mod.cpp:1915-1919`; `src/engine/VoicePlace.cpp:370-376` |
| AUD-15 | missing | The stand-down lives in `sound_place::Tick`, which ME3 never calls: no list is cleared (none is filled) and no reason is logged. | The contract's outcome ("the game's own mix plays") is true because nothing else can happen. ME2's audit rated this "same" (inert); on ME3 the code does not run. | `src/mod/Mod.cpp:1923-1927`; `src/game/SoundPlace.cpp:341-350` |
| AUD-16 | partial | The same Sound page from `assets/menu/main.rml`; `le2` is set on ME3 too, so both groups carry `disabled` and "Not available in Mass Effect 3 yet"; the "Now:" line reads "Mass Effect 3 native audio (Wwise)". | No row does anything. `audio_*` writes are not in ME3's refused-key list (`UnavailableInLe3Presentation`), so whether a greyed row can still write `[Audio]` keys into `LE3MEVR.ini` was not settled (harmless: nothing reads them). | `assets/menu/main.rml:323-337`; `src/plugin/MainMenuSettings.cpp:301-303`, `:659-663`; `src/core/vr/MenuSettings.cpp:900-903` |
| AUD-17 | missing | The trace lives in `plugin/OpenAlSoft.cpp`, not in the LE3 host. | Nothing forces this: a library-load trace is game-independent and is the evidence AUD-01's n/a lacks. | `CMakeLists.txt:883-884`, `:1143-1192` |
| AUD-18 | missing | No hook; ISACT's axes and fields have no meaning in Wwise. | A Wwise route would set a game object's position through Wwise's own call, in its axes and units. | `src/engine/VoicePlace.cpp` |
| AUD-19 | missing | `sound_place::RefreshSettings` runs only from `Tick` and `WantsHook`, neither called on LE3. | ME2 reads the keys each tick (inert); ME3 does not read them. | `src/game/SoundPlace.cpp:336-342`; `src/mod/Mod.cpp:1915-1927` |
| AUD-20 | missing | Never reached. | | as AUD-10 |
| AUD-21 | missing | `mevr voices census` is not routed on ME3: `RunOursLe3` has no `voices` verb, so the line falls to the game's own console. | ME2 accepts the command and lists nothing; ME3 does not know it. No stand-in lists ME3's sound sources and owners. | `src/game/ConsoleRequest.cpp:1749-1760`, `:2126-2129`, `:1063` |

# Against ME2

| ME2 finding | on ME3 | note |
|---|---|---|
| "The game is Wwise" is unverified from the exe | same defect | one check closes both (ledger G37). |
| Nobody measured whether the sequel centres voices | same defect | the same two tools, another save. |
| The menu is honest | same (fine) | the `le2` flag greys both groups on ME3; the title text is ME3's. |
| `GameSpec::audio` not read ("check `Game.cpp`") | fine | false for LE2 and LE3 (`Game.cpp:21-29`). |
| No library-load trace in the host (AUD-17) | same defect | |
| `voice_place::Install` fails without a log line | different | on ME3 it is never called; the silence is complete. |
| AUD-15 and AUD-19 run inert | different | ME3 compiles the tick out, so they are missing, not inert. |
| `mevr voices census` accepted and empty | different | not a command on ME3. |
| Thespian cannot mute or skip Shepard's line | same | `kSkipSpokenReply` false for both sequels (`Thespian.cpp:41-43`); the menu's "A reply picked by hand" row is hidden. |
| Squad acknowledgement not ported (ledger G34) | same | `SquadAck.cpp:142` gates on `runtime::Ready()`, false off LE1. |

Of the summary's five mechanisms, only the second touches this category, and only at its edge: the LE3 host compiles
`plugin/Voice` and `plugin/Thespian` (with the main menu) and the mod's Thespian adapter is enabled, while Setup's
`GameSpec::voice` is false for LE3 and the menu refuses `voice_*`, `mic*` and `thespian_*` writes. A players' install
would have the code and neither the DLL nor a switch. That is the voice audit's row, noted here because it is the
only audio-adjacent code ME3 runs.

# Reinvented

Nothing. No ME3-specific audio code exists in the repository: no Wwise hook, no sequel sound module, no second copy of
the placement rule. The category stopped short on both sequels; it did not fork.

One thing to keep that way, as on ME2: the rules a Wwise route would need are game-independent
([core/vr/SoundPlace.hpp](../../../../src/core/vr/SoundPlace.hpp), `cw::PlaceVoice`) and the publisher lists
([engine/VoicePlace.hpp](../../../../src/engine/VoicePlace.hpp)) do not mention ISACT. A port adds a second back end
under `voice_place` once, for ME2 and ME3 together, not a `game/SoundPlaceLe3`.

# Rediscovered problems

None recorded: no ME3 note or commit shows an audio problem met a second time (searched `git log` for ME3 and LE3
commits naming audio, sound or voice, and `research/le3-*.md`; the only hits are the voice recognizer's host wiring and
Thespian's adapter).

The two entries of the catalog's "already solved" list a Wwise lane would otherwise meet again are the ME2 audit's:
measure what the PC plays instead of judging by ear, and take a census of sources and owners before deciding what to
place.

# Forced by the game

| difference | evidence |
|---|---|
| Another audio engine: ISACT over Creative's OpenAL on ME1, Wwise on ME3 (as recorded; unverified from the exe). The OpenAL Soft switch, its gain and its fail-safes have nothing to attach to. | [research/69](../../../../research/69-conversations-everywhere.md) "The voices"; [ledger](../../../trilogy-parity-ledger.md) G37; [trilogy plan](../../../trilogy-plan.md) T50 |
| ME1's hook address and field offsets (`FISACTSoundSource::Update` 0x7BA90, the wave's +0x78) are ISACT's. | `src/engine/Addresses.hpp:72`, `:150`, `:495` |
| Thespian's silent reply needs a mute and a skip through the audio route; ME3 keeps Shepard's recording. | `src/game/Thespian.cpp:41-43`; [le3-dialogue-cinema](../../../../research/le3-dialogue-cinema.md) |
| Conversation voices in the world (AUD-07 to AUD-09) need first-person conversations, which ME3 does not have. | [conversations audit](conversations-and-scenes.md) CNV-07 |

# Not forced

| difference | note |
|---|---|
| No measurement of ME3's mix | `mevr-audio-loopback` and `tools/audio-balance.py` record the PC's output; they work for any game. |
| No library-load trace on ME3 (AUD-17) | The `LoadLibraryExW` trace could be compiled into every host without the OpenAL switch. |
| The sound modules compiled out of ME3's tick, not standing down with a reason | ME1's convention is one log line per stand-down; ME2 keeps it, ME3 has `#if`s in `Mod.cpp` (commit 3a7bffff). The `LogStats`, `StatusJson` and `Shutdown` calls of the same modules still run on ME3. |
| `mevr voices` not routed on ME3 | `RunOursLe3` is a hand-kept list beside `RunOurs` and `RunOursLe2`; a verb is on ME3 only when someone adds it. |
| On-foot placement (AUD-10 to AUD-12) not attempted | No plan covers it (T50); the need was never measured. |

# What to do

Parity work only, ordered by what a player hears. Steps 1 to 3 are the ME2 audit's, done once for both sequels.

1. **Settle what ME3's audio engine is** (closes ledger G37's open step): the loaded modules of `MassEffect3.exe` in a
   running session (the crash breadcrumbs' module list, or the trace of step 5), looking for `OpenAL32.dll` and Wwise.
   Closing check: one line in [research/69](../../../../research/69-conversations-everywhere.md) or a sequel audio note
   naming the libraries, and the ledger cell without "[verify]".
2. **Measure before building** (decides whether AUD-10 to AUD-12 are gaps): on Save 31's Normandy with the fake
   headset, a talking crew member 90 degrees to one side, `build\Release\mevr-audio-loopback.exe out.wav 20`, then
   `python tools/audio-balance.py out.wav`; again with the head turned round, and for a gunshot on a combat save. A
   balance near 0 dB that does not mirror means ME3 centres as ME1 did; a mirrored balance means Wwise places them and
   the rows become n/a. Read the interaural delay for AUD-02.
3. **If step 2 shows centred voices:** one Wwise back end under `engine/VoicePlace` for ME2 and ME3 (publishers and
   `game/SoundPlace` unchanged), its addresses with patterns in `engine/Addresses.hpp`, a census first. Then remove the
   `#if !defined(MEVR_GAME_LE3)` around `sound_place::Tick` in `src/mod/Mod.cpp`. Closing checks: `mevr voices census`
   lists owners on ME3; step 2's recording mirrors; `tests/SoundPlaceTests.cpp` untouched and passing.
4. **Thespian's mute on ME3** follows from step 3's route (`Publisher::Thespian` with gain 0, then the skip).
5. **Small honesty fixes:** compile the library-load trace into every host; give `RunOursLe3` the `voices` verb (it can
   answer "no sound hook on ME3" until step 3); replace the three `#if`s in `Mod.cpp` with the modules' own logged
   stand-down, as ME2 has, so the three games differ in one place (`kModulesOnLayer`) instead of two.

# Citations

- Catalog: [ME1 audio](../../me1/audio.md); [how to catalog](../../how-to-catalog.md); the sibling audit
  [ME3 conversations and scenes](conversations-and-scenes.md).
- ME2's audits: [audio](../me2/audio.md), [summary](../me2/summary.md).
- Ledger and plans: [trilogy parity ledger](../../../trilogy-parity-ledger.md) rows G34, G35, G37, G38;
  [trilogy plan](../../../trilogy-plan.md) T50.
- Research: [69 conversations everywhere](../../../../research/69-conversations-everywhere.md) ("The voices", "The voices,
  placed", "The audio list"), [70 Thespian mode](../../../../research/70-thespian-mode.md),
  [le3-dialogue-cinema](../../../../research/le3-dialogue-cinema.md).
- Host: [Entry.cpp](../../../../src/plugin/Entry.cpp), [OpenAlSoft.cpp](../../../../src/plugin/OpenAlSoft.cpp),
  [MainMenuSettings.cpp](../../../../src/plugin/MainMenuSettings.cpp), [main.rml](../../../../assets/menu/main.rml),
  [MenuSettings.cpp](../../../../src/core/vr/MenuSettings.cpp).
- Mod and engine: [VoicePlace.cpp](../../../../src/engine/VoicePlace.cpp), [VoicePlace.hpp](../../../../src/engine/VoicePlace.hpp),
  [Addresses.hpp](../../../../src/engine/Addresses.hpp), [SoundPlace.cpp](../../../../src/game/SoundPlace.cpp),
  [core SoundPlace.hpp](../../../../src/core/vr/SoundPlace.hpp), [Thespian.cpp](../../../../src/game/Thespian.cpp),
  [SquadAck.cpp](../../../../src/game/SquadAck.cpp), [Mod.cpp](../../../../src/mod/Mod.cpp),
  [ConsoleRequest.cpp](../../../../src/game/ConsoleRequest.cpp).
- Installer and tools: [Game.cpp](../../../../src/install/Game.cpp), [Game.hpp](../../../../src/install/Game.hpp),
  [Installer.cpp](../../../../src/install/Installer.cpp), [deploy-asi.ps1](../../../../tools/deploy-asi.ps1),
  [audio-balance.py](../../../../tools/audio-balance.py).
- Build lists: [CMakeLists.txt](../../../../CMakeLists.txt).

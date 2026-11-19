---
type: Audit
title: ME2 audio against ME1's contracts
description: None of ME1's audio work runs on ME2; six contracts are n/a because ME2 is taken to play through Wwise (never verified from the exe), twelve are missing for want of a Wwise placement hook, and nobody has measured whether ME2 has ME1's centred-voice problem at all.
tags: [audit, me2, parity, audio, wwise, voices]
status: draft
timestamp: 2026-11-18T08:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:00:00Z }
game: LE2
against: ../../me1/audio.md
commit: 940a75c6
---

# Verdict

Audited against [the ME1 catalog file](../../me1/audio.md) by reading code and notes at 940a75c6; no game was run and no
audio was recorded. Every claim below rests on reading alone.

- **ME2 gets nothing of this category from shared code in effect.** The game-independent rules (`cw::PlaceVoice`,
  `core/vr/SoundPlace`) compile into the LE2 mod, but their only door to the engine, `engine/VoicePlace`'s hook on ISACT's
  source update, has no ME2 address, and the host's OpenAL Soft switch is compiled for LE1 only.
- **Finding 1: "ME2 is Wwise" is an assumption on record, not a measurement.** The ledger's own cell says
  "[verify] the exe's imports"; the evidence in the repository is one sentence in research/69 and a Wwise event name seen
  in ME2's packages (research/le2-menus). ME1's `[Audio] trace` (AUD-17), the tool that would log which audio libraries
  ME2 loads, is not in the LE2 host.
- **Finding 2: nobody has asked whether ME2 has the problem ME1 solved.** ME1's work began from a measurement (82% of
  dialogue cues centred; a voice 77 degrees to the right at 0 dB balance). The same measurement
  (`mevr-audio-loopback`, `tools/audio-balance.py`) is game-independent and was never made on ME2. Until it is, AUD-07
  to AUD-12 are "missing" by code and "unknown" by need.
- **Finding 3: the menu is honest.** The Sound page shows on ME2 with both groups greyed, "Not available in Mass Effect
  2 yet" and "Now: ... native audio (Wwise)"; nothing claims a feature that is not there.
- The two places where ME2 already paid for the missing audio route: Thespian cannot silence or skip Shepard's
  recording (`kSkipSpokenReply = false`), and squad acknowledgement is not ported (ledger G34).

| rating | count | ids |
|---|---|---|
| same | 2 | AUD-15, AUD-19 (both inert: the stand-down and the settings read are all that runs) |
| same core, other inputs | 0 | |
| adapter | 0 | |
| reinvented | 0 | |
| partial | 1 | AUD-16 |
| missing | 12 | AUD-07 to AUD-14, AUD-17, AUD-18, AUD-20, AUD-21 |
| n/a | 6 | AUD-01 to AUD-06 |
| unknown | 0 | (the n/a rows carry an open verification, see "What to do" 1) |

Against the ledger: G37 "n/a" and G38 "missing" are right as far as code goes. G38's next step ("Wwise needs a placement
hook of its own") skips the question of whether placement is needed.

# Contract by contract

| id | rating | how ME2 gets it | where it differs | evidence |
|---|---|---|---|---|
| AUD-01 | n/a | ME2 is taken to play through Wwise, with no OpenAL router to detour. `openal_soft::Attach` is called only under `MEVR_WITH_HALCYON` and `MEVR_GAME_LE1`; `plugin/OpenAlSoft.cpp` is in LE1's host list only. What ME2 has instead: Wwise's own mixer and whatever spatialization its sound bank was authored with (not read). | Not verified from `MassEffect2.exe`'s imports or loaded modules. | `src/plugin/Entry.cpp:260-264`; `CMakeLists.txt:905-906`; [research/69](../../../../research/69-conversations-everywhere.md) "The voices" ("LE1 has no Wwise (that is LE2 and LE3)"); [ledger](../../../trilogy-parity-ledger.md) G37 |
| AUD-02 | n/a | As AUD-01: no OpenAL device or context to wrap. Whether Wwise's output to headphones is plain panning or binaural on ME2 is not known. | The run that settles it: a loopback recording of a placed sound to one side, read for interaural delay as ME1's was (0.9 ms at 77 degrees with HRTF, none with panning). | as AUD-01 |
| AUD-03 | n/a | `[Audio] spatial` is read by no ME2 code. | The menu still shows the three choices, greyed. | `assets/menu/main.rml:323-327` |
| AUD-04 | n/a | No switch, so no fail-safe needed; the game's own audio always plays. | | as AUD-01 |
| AUD-05 | n/a | `[Audio] gainDb` is read by no ME2 code; no `ALSOFT_CONF` is written. | | as AUD-01 |
| AUD-06 | n/a | The installer installs the OpenAL Soft payload only for a game whose `GameSpec::audio` is set. | The LE2 value of that flag was not read for this audit; if it is set, a DLL nothing loads would be installed. Check `src/install/Game.cpp`. | `src/install/Game.hpp:44`; `src/install/Installer.cpp:319` |
| AUD-07 | missing | Needs a first-person conversation (not on ME2, see the conversations audit) and the source hook (no address). `cw::PlaceVoice` itself is compiled and unit-tested, game-independent. | Whether ME2's dialogue is centred as ME1's was is unmeasured. On the 3D screen a centred voice is the right mix. | `src/engine/Addresses.hpp:271`; `src/engine/VoicePlace.cpp:374-376`; `src/core/vr/ConversationWorld.hpp` |
| AUD-08 | missing | No rule runs; every ME2 voice is the game's own mix at any distance. | | as AUD-07 |
| AUD-09 | missing | As AUD-07. | ME1's own falloff exists because ISACT only pans; Wwise applies authored attenuation curves, so the 2 m, 3 dB, 6 dB numbers may be unnecessary or wrong on ME2. Measure before copying. | as AUD-07 |
| AUD-10 | missing | `game/SoundPlace::Tick` runs on LE2 and stands down every tick with "the sound source hook is not in". | The need is unmeasured: `tools/audio-balance.py` on a bark from one side on foot. | `src/game/SoundPlace.cpp:347-350`; `src/mod/Mod.cpp:1926` |
| AUD-11 | missing | The rule (`vr::sound_place::Overheard`) compiles and is unit-tested; nothing feeds it. | | `src/core/vr/SoundPlace.hpp`; as AUD-10 |
| AUD-12 | missing | As AUD-10. | ME2's weapon sounds are Wwise events; which game object they play on is the same open question ME1 had about owners. | as AUD-10 |
| AUD-13 | missing | The publisher lists exist in the LE2 build (`engine/VoicePlace` compiles) and can be filled, but no source is ever placed. Thespian on the sequels does not publish its mute. | | `src/engine/VoicePlace.hpp:24`; `src/game/Thespian.cpp:41-43` |
| AUD-14 | missing | `voice_place::Install` returns false at once on LE2 ("another game: no address"), without a log line. | | `src/engine/VoicePlace.cpp:370-376`; `src/mod/Mod.cpp:485-490` |
| AUD-15 | same | ME1's own stand-down code runs with its reason logged; on ME2 it is the only state there is. | Inert parity: the contract's "game's own mix plays" is true because nothing else can happen. | `src/game/SoundPlace.cpp:341-354` |
| AUD-16 | partial | The same Sound page from `assets/menu/main.rml`; on the sequels both groups carry `disabled` and a hint "Not available in (game) yet"; the "Now:" line reads "(game) native audio (Wwise)". | No row does anything. Whether a greyed row still takes a click and writes `[Audio]` keys into `LE2MEVR.ini` was not checked (harmless: nothing reads them). | `assets/menu/main.rml:323-337`; `src/plugin/MainMenuSettings.cpp:658-663`; [le2-menus](../../../../research/le2-menus.md) ("Voice, Sound pages: greyed") |
| AUD-17 | missing | The trace lives in `plugin/OpenAlSoft.cpp`, not in the LE2 host. | Nothing forces this: a library-load trace is game-independent and is exactly the evidence AUD-01's n/a lacks. | `CMakeLists.txt:905-906`, `:1086-1156` |
| AUD-18 | missing | No hook; ISACT's axes and fields (`kWaveVolume`, `kEntityPosition`) have no meaning in Wwise. | A Wwise route would set a game object's position through Wwise's own call, in its axes and units. | `src/engine/VoicePlace.cpp` |
| AUD-19 | same | `sound_place::RefreshSettings` runs each tick on every game and reads `[Audio] overheard` and `gun` from that game's ini. | Inert: the values change nothing on ME2. | `src/game/SoundPlace.cpp:336-342` |
| AUD-20 | missing | Never reached (the tick returns before `TickOverheard`). | | `src/game/SoundPlace.cpp:347-350` |
| AUD-21 | missing | `mevr voices census` is accepted by the console on every game, but the census is filled by the hook, so it lists nothing on ME2. | ME2 has no stand-in to list its sound sources and owners. | `src/game/ConsoleRequest.cpp:1063-1064`; `src/engine/VoicePlace.hpp` |

# Reinvented

Nothing. No ME2-specific audio code exists in the repository: no Wwise hook, no sequel sound module, no second copy of
the placement rule. The category stopped short; it did not fork.

One thing to keep that way: the rules a Wwise route would need are already game-independent
([core/vr/SoundPlace.hpp](../../../../src/core/vr/SoundPlace.hpp), `cw::PlaceVoice`) and the publisher lists
([engine/VoicePlace.hpp](../../../../src/engine/VoicePlace.hpp)) do not mention ISACT. A port should add a second
back end under `voice_place` (the ISACT post-hook is one, a Wwise position call another), not a `game/SoundPlaceLe2`.

# Rediscovered problems

None recorded: no ME2 note or commit shows an audio problem met a second time (searched `git log` for ME2, LE2 and
sequel commits naming audio, sound or voice, and `research/le2-*.md`).

Two entries from the catalog's "already solved" list are the ones a Wwise lane would otherwise meet again:

| ME1's lesson | why it applies to ME2 |
|---|---|
| "Audio judged by ear gave no answer": record what the PC plays and measure each channel. | The first ME2 step is this measurement, before any hook. |
| "Which actor owns a sound could not be told from the packages": the census. | A Wwise route needs the same census (game object to actor) before deciding what to place. |

# Forced by the game

| difference | evidence |
|---|---|
| Another audio engine: ISACT over Creative's OpenAL on ME1, Wwise on ME2 (as recorded; unverified from the exe). The OpenAL Soft switch, its gain and its fail-safes have nothing to attach to. | [research/69](../../../../research/69-conversations-everywhere.md) "The voices"; [le2-menus](../../../../research/le2-menus.md) (a `Wwise_VFX_Tech` event in ME2's omni-tool effects); [ledger](../../../trilogy-parity-ledger.md) G37 |
| ME1's hook address and field offsets (`FISACTSoundSource::Update` 0x7BA90, the wave's +0x78) are ISACT's. | `src/engine/Addresses.hpp:72`, `:150`, `:271` |
| Thespian's silent reply needs a mute and a skip through the audio route; ME2 keeps Shepard's recording. | `src/game/Thespian.cpp:43`; [research/70](../../../../research/70-thespian-mode.md) "ME2 cinema adapter" |
| Conversation voices in the world (AUD-07 to AUD-09) need first-person conversations, which ME2 does not have. | [conversations audit](conversations-and-scenes.md) CNV-07 |

# Not forced

| difference | note |
|---|---|
| No measurement of ME2's mix | `mevr-audio-loopback` and `tools/audio-balance.py` record the PC's output; they work for any game. |
| No library-load trace on ME2 (AUD-17) | The `LoadLibraryExW` trace could be compiled into every host without the OpenAL switch. |
| `voice_place::Install` fails silently on ME2 | ME1's convention is one log line per stand-down; the mod's `sound place:` line covers it only once a second module asks. |
| On-foot placement (AUD-10 to AUD-12) not attempted | No plan covers it (trilogy-plan T50: "No plan covers that hook"); the need was never measured, so the gap may be real or empty. |

# What to do

Parity work only, ordered by what a player hears.

1. **Settle what ME2's audio engine is** (closes ledger G37's open step): the import table and loaded modules of
   `MassEffect2.exe` in a running session (the host's module list at attach, or the crash breadcrumbs' module list),
   looking for `OpenAL32.dll` and Wwise's exports. Closing check: one line in
   [research/69](../../../../research/69-conversations-everywhere.md) or a new ME2 audio note naming the libraries, and the
   ledger cell without "[verify]".
2. **Measure before building** (decides whether AUD-10 to AUD-12 are gaps at all): on Save 4's spaceport with the fake
   headset, stand so a talking squadmate is 90 degrees to one side, record with
   `build\Release\mevr-audio-loopback.exe out.wav 20`, then `python tools/audio-balance.py out.wav`; repeat with the
   head turned round, and for a gunshot. A balance near 0 dB that does not mirror means ME2 centres as ME1 did; a clear
   mirrored balance means Wwise already places them and the rows become n/a. Also read the interaural delay to answer
   AUD-02 (panned or binaural).
3. **If step 2 shows centred voices:** a Wwise back end under `engine/VoicePlace` (the publishers and
   `game/SoundPlace` unchanged), found with Ghidra from the engine's Wwise position call, with its address and pattern
   in `engine/Addresses.hpp` and a census (AUD-21) first. Closing checks: `mevr voices census` lists owners on ME2;
   step 2's recording mirrors; `tests/SoundPlaceTests.cpp` untouched and passing.
4. **Thespian's mute on ME2** follows from step 3's route (`Publisher::Thespian` with gain 0, then the skip): closes
   the "Shepard still speaks" gap (trilogy-plan question 8). Check: research/70's ME2 run with a spoken reply, Shepard's
   line silent.
5. **Small honesty fixes:** a log line when `voice_place::Install` has no address (`src/engine/VoicePlace.cpp:374`);
   confirm `GameSpec::audio` is false for LE2 and LE3 (`src/install/Game.cpp`) so Setup never installs `soft_oal.dll`
   for a game that cannot load it (check: `tools/setup/test-fake-game.ps1 -Games LE2` leaves no `ASI\audio`).

# Citations

- Catalog: [ME1 audio](../../me1/audio.md); [how to catalog](../../how-to-catalog.md); the sibling audit
  [ME2 conversations and scenes](conversations-and-scenes.md).
- Ledger and plans: [trilogy parity ledger](../../../trilogy-parity-ledger.md) rows G34, G35, G37, G38;
  [trilogy plan](../../../trilogy-plan.md) T50 and question 8.
- Research: [69 conversations everywhere](../../../../research/69-conversations-everywhere.md) ("The voices", "The voices,
  placed", "The audio list"), [70 Thespian mode](../../../../research/70-thespian-mode.md),
  [le2-menus](../../../../research/le2-menus.md).
- Host: [Entry.cpp](../../../../src/plugin/Entry.cpp), [OpenAlSoft.cpp](../../../../src/plugin/OpenAlSoft.cpp),
  [MainMenuSettings.cpp](../../../../src/plugin/MainMenuSettings.cpp), [main.rml](../../../../assets/menu/main.rml).
- Mod and engine: [VoicePlace.cpp](../../../../src/engine/VoicePlace.cpp), [VoicePlace.hpp](../../../../src/engine/VoicePlace.hpp),
  [Addresses.hpp](../../../../src/engine/Addresses.hpp), [SoundPlace.cpp](../../../../src/game/SoundPlace.cpp),
  [core SoundPlace.hpp](../../../../src/core/vr/SoundPlace.hpp), [Thespian.cpp](../../../../src/game/Thespian.cpp),
  [Mod.cpp](../../../../src/mod/Mod.cpp), [ConsoleRequest.cpp](../../../../src/game/ConsoleRequest.cpp).
- Installer: [Game.hpp](../../../../src/install/Game.hpp), [Installer.cpp](../../../../src/install/Installer.cpp).
- Tools: [audio-balance.py](../../../../tools/audio-balance.py).
- Build lists: [CMakeLists.txt](../../../../CMakeLists.txt).

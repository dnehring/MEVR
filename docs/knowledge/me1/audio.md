---
type: System
title: Audio (ME1)
description: How ME1's sound reaches two ears in VR, as numbered contracts: the game's OpenAL sent to OpenAL Soft with HRTF, the mix's gain, voices placed at speakers' heads in first-person conversations, people overheard on foot, the gun at its muzzle, and the menu's Sound page.
tags: [me1, audio, hrtf, openal, voices, catalog]
status: stable
timestamp: 2026-11-18T07:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T07:00:00Z }
game: LE1
---

# What the player gets

- Every sound the game places reaches each ear as it would in the room (above and behind too), not only panned left
  and right. This is on by default and replaces no game file.
- The 3D mix is as loud as the game's own.
- In a first-person conversation a speaker within a few metres is heard from where their head is, and gets quieter
  with distance. A speaker far away (the Council across its chamber) stays in the centre like an intercom.
- On foot, people talking near the player are heard from where they stand instead of inside the player's head.
- The player's own weapon sounds from the gun in the hand.
- The menu's Sound page chooses headphones 3D, speakers or the game's own sound, the loudness, and the two "where
  sounds come from" switches.

# Contracts

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| AUD-01 | At the very start of attach, before the game opens its audio device, the host loads `ASI\audio\soft_oal.dll` and detours every function of the game's OpenAL router that OpenAL Soft also has to it. All detours are enabled at once. | `[Audio] spatial = hrtf` (default); read once at attach | LE1 host with Halcyon; needs the DLL, the game's `OpenAL32.dll` loaded and `wrap_oal.dll` not loaded yet | `plugin/OpenAlSoft.cpp` `Attach`; `plugin/Entry.cpp` | fake headset 2026-11-10 |
| AUD-02 | The game's device and context go through wrappers that ask for HRTF on top of the game's own attributes. A context refused with the HRTF attribute is made without it, so the game keeps its sound. | `ALC_HRTF_SOFT` (0x1992) | `spatial = hrtf` | `plugin/OpenAlSoft.cpp` (the `alcOpenDevice` and `alcCreateContext` wrappers) | fake headset (a voice 77 degrees to the right reaches the left ear 0.9 ms later) |
| AUD-03 | `spatial = soft` plays through OpenAL Soft without HRTF (panned for speakers). `spatial = game` leaves the game's own OpenAL untouched: nothing is loaded or hooked. | soft also for `pan`, `speakers`; game also for `off`, `0`, `creative`; anything else is hrtf | read once at attach | `plugin/OpenAlSoft.cpp` `ReadMode`; `vr::menu_settings::AudioSpatialChoice` | unit test (the chip); fake headset |
| AUD-04 | Fail safe: with no DLL, a DLL that does not load or lacks `alcOpenDevice` / `alcCreateContext`, the game's router not loaded, Creative's driver already loaded, or any detour failing, everything is put back and the game's own OpenAL plays. Each case is one log line and the menu's "Now:" text. | n/a | n/a | `plugin/OpenAlSoft.cpp` `Attach`, `SetDescribe` | fake headset [verify] each branch |
| AUD-05 | The whole 3D mix is raised to the game's level through OpenAL Soft's `volume-adjust`, written by the host to a config of its own that it names in `ALSOFT_CONF` before the library loads. A player's own `alsoft.ini` is read first; only the volume is ours. | `[Audio] gainDb` 3 (host clamps -12..12; the menu's range 0..9); OpenAL Soft's mix measured about 3 dB under the game's (-29.4 against -26.2 dB a channel); file `%LOCALAPPDATA%\MEVR\openal-soft.ini` | with OpenAL Soft; read once at attach | `plugin/OpenAlSoft.cpp` `Attach` | fake headset 2026-11-10 (same level, no clipping) |
| AUD-06 | No game file is replaced. The DLL ships in the release's `payload\audio` with its licences and source tarball (LGPL); Setup installs, repairs and uninstalls it on its manifest. | n/a | `package-release.ps1 -NoAudio` leaves it out | `tools/fetch-openal-soft.ps1`; `src/install` | unit test (`tests/InstallTests.cpp`) |
| AUD-07 | In a first-person conversation, a speaker near the player is heard from their head. The game plays 82% of dialogue cues in the centre wherever the speaker stands; for a source whose audio component belongs to a named speaker, the hook sends the head's place and "in the world" again every update, so it follows a moving head and a walking player mid-line. | `[Cinematics] worldVoices = spatial`; placed within `worldVoiceWithinM` 8, kept to `worldVoiceKeepM` 10 once placed; hook at `0x7BA90` | a conversation shown in the world; every speaker but the player | `cw::PlaceVoice`; `game/ConversationWorld.cpp`; `engine/VoicePlace.cpp` (`Publisher::Conversation`) | fake headset 2026-11-10 (Anderson 77 degrees right: +8 dB right, 0 in the game's mix; mirrored with the head turned round) |
| AUD-08 | A speaker further away stays the game's centred, full-level mix (the Council like an intercom). | beyond 8 m (10 m once placed) | as AUD-07 | `cw::PlaceVoice` (`placed` false) | unit test; fake headset |
| AUD-09 | The audio engine only pans, so loudness with distance is the mod's: full near the player, quieter per doubling of the distance, down to a floor. | `worldVoiceNearM` 2; `worldVoiceFalloffDb` 3 a doubling; `worldVoiceFloorDb` 6 at most; `worldVoiceGain` 1 | as AUD-07 | `cw::PlaceVoice` (gain); `engine/VoicePlace.cpp` (the volume sent again times the gain) | unit test; fake headset |
| AUD-10 | On foot, people near the player are named at their heads under the same rule, and only their sources the game left in the centre are placed. A bark with a falloff of its own or a footstep keeps the game's mix. | `[Audio] overheard = 1`; `overheardWithinM` 8, `overheardKeepM` 10, `overheardNearM` 2, `overheardFalloffDb` 3, `overheardFloorDb` 6, `overheardGain` 1 | game mode 0 with the view the player's; every visible BioPawn but the player's | `vr::sound_place::Overheard::Step`; `game/SoundPlace.cpp`; `engine/VoicePlace.cpp` (`centredOnly`, the wave's +0x78) | offline 2026-11-11; [verify] in the game |
| AUD-11 | At most a fixed number of people are named at once: those heard speaking recently first, then the nearest. Each keeps its hysteresis; those no longer near are forgotten. | `overheardMax` 16 (1..32); speaking = a source placed within 2.0 s | as AUD-10 | `vr::sound_place::Limits`, `Overheard::Heard`; `voice_place::TakeHeard`, `TakeHeardUnnamed` | unit test (`tests/SoundPlaceTests.cpp`) |
| AUD-12 | The player's held weapon is named at its drawn muzzle, so its sounds come from the gun in the hand. | `[Audio] gun = 1` | on foot with a weapon whose drawn place is known. Which actor owns the shot's source is [verify] (`mevr voices census`) | `game/SoundPlace.cpp` (`weapon::CurrentAsDrawn`, `Publisher::Gun`) | offline; [verify] in the game |
| AUD-13 | Lists have an order: a source whose owner more than one list names is placed by the first of conversation, overheard, gun, Thespian. Each publisher replaces its own list whole. | 4 publishers; 16 voices for the conversation's list, 32 at most per list | n/a | `engine/VoicePlace.hpp` `Publisher`, `Set` | read from the code |
| AUD-14 | The source hook is in for every player on foot when either switch is on, and for a first-person conversation. It places only on the thread that names the actors; a call from another thread places nothing and is counted. After three access violations in its own part it places nothing more. | n/a | LE1 only | `engine/VoicePlace.cpp`; `game/SoundPlace.cpp` (`EnsureVoicePlaceHook`) | fake headset (conversation); [verify] on foot |
| AUD-15 | Off foot, or with both switches off, the on-foot lists are cleared and the game's own mix plays; the stand-down is logged with its reason. | n/a | n/a | `game/SoundPlace.cpp` `StandDown`; `Overheard::Clear` | unit test |
| AUD-16 | The menu's Sound page: "Sound for" (Headphones 3D, Speakers, The game's own), Loudness, a "Now:" line saying what plays, a note when the file's choice differs from what plays, and the toggles "People around you" and "Your gun". | Loudness 0..9 dB in steps of 1, default 3 | the first two rows apply when the game next starts; the two toggles are live (the mod re-reads them) | `plugin/MainMenuSettings.cpp`; `assets/menu/main.rml`; `vr::menu_settings::AudioNote` | offline (`mevr-menu-preview`); unit test |
| AUD-17 | `[Audio] trace = 1` logs every OpenAL or DirectSound library load in the process and who asked. | off by default | read at attach | `plugin/OpenAlSoft.cpp` (`LoadLibraryExWTrace`) | fake headset |
| AUD-18 | A placed source's position is sent in the audio engine's own axes and units, and its volume is the game's own volume for the source times the gain, clamped as the game clamps it. A source of nobody named is left exactly as the game sent it. | ISACT axes X, Z, -Y, in metres (100 units a metre); volume 0..1 | per source update, once a frame per playing source | `engine/VoicePlace.cpp` (the post-hook; `kWaveVolume`, `kEntityPosition`, `kStreamPosition`) | fake headset 2026-11-10 |
| AUD-19 | The mod's `[Audio]` keys are re-read every second, so the menu's two toggles act during play; a console override lasts the session. | 1 s | n/a | `game/SoundPlace.cpp` `RefreshSettings`, `gOverheardOverride` | read from the code |
| AUD-20 | People are looked at for a voice only when their place is within the rule's keep radius plus a margin for the head above the feet; hidden actors are skipped; the head bone is read only for those. | keep radius 10 m plus 250 units (`CandidateReachUnits`) | as AUD-10 | `vr::sound_place::CandidateReachUnits`; `game/SoundPlace.cpp` (`Actor.bHidden`) | unit test |
| AUD-21 | A census on request lists, for some seconds, every distinct owner and audio component a source belongs to, its cue, whether the game placed it and how often and by whose list the mod did. | at most 64 entries | game thread | `engine/VoicePlace.hpp` `StartCensus`, `TakeCensus`; `mevr voices census [seconds]` | offline; [verify] in the game |

# Arbitration

| what | who wins | what the loser sees |
|---|---|---|
| one sound source named by several lists | the first of conversation, overheard, gun, Thespian | the later lists never place it |
| a named actor's source the game placed itself (`centredOnly` lists) | the game | left exactly as the game sent it; counted as `leftPlaced` |
| the player's own voice during a spoken Thespian reply | Thespian names the player's pawn with gain 0 | Shepard's recording is silent until it is skipped |
| the file's `spatial` or `gainDb` changed in the menu during play | what was read at attach | the note says it applies when the game next starts |

# Problems ME1 already solved

| problem | fix | where |
|---|---|---|
| The game plays 82% of dialogue cues in the centre wherever the speaker stands (no attenuation node). | Post-hook the ISACT source's update and send place, "in the world" and volume again for named actors. | [research/69](../../../research/69-conversations-everywhere.md) "The voices, placed" |
| ISACT only pans: a placed voice has no distance loudness. | The mod's own falloff (full within 2 m, 3 dB a doubling, at most 6 dB). | [research/69](../../../research/69-conversations-everywhere.md) "The voices, placed" |
| The game's OpenAL (Creative's router and software device) only pans left and right. | The router's functions detoured to OpenAL Soft with HRTF asked for. | [research/69](../../../research/69-conversations-everywhere.md) "Real 3D audio" |
| Hooks enabled one by one lost a race: the game opens its device during attach. | All detours queued and enabled at once, at the very start of attach. | [research/69](../../../research/69-conversations-everywhere.md) "OpenAL Soft as the mod ships it" |
| The game's import slots alone are not its route to OpenAL (GetProcAddress too). | Detour the router's functions themselves. | [research/69](../../../research/69-conversations-everywhere.md) "OpenAL Soft as the mod ships it" |
| OpenAL Soft's mix is about 3 dB quieter than the game's. | `volume-adjust` in a config the host writes, named in `ALSOFT_CONF`; the library's limiter on. | [research/69](../../../research/69-conversations-everywhere.md) "OpenAL Soft as the mod ships it" |
| Placing every source of a nearby person would override barks and footsteps the game already places. | "Centred sources only": the wave's flag after the game's update decides. | [research/69](../../../research/69-conversations-everywhere.md) "The audio list" |
| Audio judged by ear gave no answer. | Record what the PC plays and measure each channel (`mevr-audio-loopback`, `tools/audio-balance.py`). | [research/69](../../../research/69-conversations-everywhere.md) "The voices, placed" |
| A hook that faults on the thread updating the audio device would end the game's sound or the game. | The hook's own part is guarded; after three access violations it places nothing more, and calls off the naming thread are only counted. | [engine/VoicePlace.hpp](../../../src/engine/VoicePlace.hpp) |
| Which actor owns a sound (the gun's shot: the weapon actor or the pawn) could not be told from the packages. | `mevr voices census` lists every source's owner, component, cue and whether the game placed it. | [research/69](../../../research/69-conversations-everywhere.md) "The audio list" |

# Code map

| part | file | scope |
|---|---|---|
| OpenAL Soft switch, gain, trace | [plugin/OpenAlSoft.cpp](../../../src/plugin/OpenAlSoft.cpp), called from [plugin/Entry.cpp](../../../src/plugin/Entry.cpp) | host, LE1-only (`MEVR_WITH_HALCYON` and `MEVR_GAME_LE1`; ME2 and ME3 are Wwise, ledger G37 n/a) |
| Source hook, publishers, census | [engine/VoicePlace.hpp](../../../src/engine/VoicePlace.hpp), [engine/VoicePlace.cpp](../../../src/engine/VoicePlace.cpp), [engine/Addresses.hpp](../../../src/engine/Addresses.hpp) | mod, LE1-only (`kSoundSourceUpdate` is 0 on LE2 and LE3; ledger G38 missing) |
| Voice rule (reach, hysteresis, loudness) | [core/vr/ConversationWorld.hpp](../../../src/core/vr/ConversationWorld.hpp) `PlaceVoice`, `VoiceLimits` | game-independent, unit-tested |
| Conversation voices | [game/ConversationWorld.cpp](../../../src/game/ConversationWorld.cpp) | mod; LE1's first-person conversations |
| On-foot rule | [core/vr/SoundPlace.hpp](../../../src/core/vr/SoundPlace.hpp) | game-independent, unit-tested |
| On-foot driver | [game/SoundPlace.cpp](../../../src/game/SoundPlace.cpp) | mod; acts only where the hook installs (LE1) |
| Sound page | [plugin/MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp), [assets/menu/main.rml](../../../assets/menu/main.rml), [core/vr/MenuSettings.hpp](../../../src/core/vr/MenuSettings.hpp) | host; outside `MEVR_MENU_LE1` the "Now:" line reads the game's native audio (Wwise) |
| Build and packaging of the DLL | [tools/fetch-openal-soft.ps1](../../../tools/fetch-openal-soft.ps1) | tooling |

No HostServices call is involved: the host's part runs at attach, the mod's part hooks the engine itself.

# Settings and defaults

All in `ASI\LE1MEVR.ini`.

| section | key | default | read by | menu row |
|---|---|---|---|---|
| Audio | spatial | hrtf | host, once at attach | Sound > Sound for |
| Audio | gainDb | 3 | host, once at attach | Sound > Loudness |
| Audio | trace | 0 | host, at attach | none |
| Audio | overheard | 1 | mod, every second | Sound > People around you |
| Audio | gun | 1 | mod, every second | Sound > Your gun |
| Audio | overheardWithinM, overheardKeepM | 8, 10 | mod | none |
| Audio | overheardNearM, overheardFalloffDb, overheardFloorDb, overheardGain | 2, 3, 6, 1 | mod | none |
| Audio | overheardMax | 16 | mod | none |
| Cinematics | worldVoices | spatial | mod | none |
| Cinematics | worldVoiceWithinM, worldVoiceKeepM | 8, 10 | mod | none |
| Cinematics | worldVoiceNearM, worldVoiceFalloffDb, worldVoiceFloorDb, worldVoiceGain | 2, 3, 6, 1 | mod | none |

# Checks

| kind | what |
|---|---|
| log prefixes | `audio:`, `audio trace:`, `voice place:`, `convo: voices:`, `convo: ... voice comes from where it stands`, `sound place:`, `voice census:` |
| snapshot keys | `"mod"."soundPlace"` |
| console | `mevr voices census [seconds]` (every source's owner, cue and whether the game placed it), `mevr voices overheard on\|off`, `mevr voices gun on\|off` |
| measuring | `build\Release\mevr-audio-loopback.exe out.wav <seconds>`, then [tools/audio-balance.py](../../../tools/audio-balance.py) `out.wav --from s --to s` (each channel's level and the balance) |
| unit tests | [tests/SoundPlaceTests.cpp](../../../tests/SoundPlaceTests.cpp), [tests/ConversationWorldTests.cpp](../../../tests/ConversationWorldTests.cpp) (`PlaceVoice`), [tests/InstallTests.cpp](../../../tests/InstallTests.cpp) (the payload) |
| offline look | `mevr-menu-preview` renders the Sound page |

# Citations

- [research/69: conversations everywhere](../../../research/69-conversations-everywhere.md) ("The voices", "The voices,
  placed", "Real 3D audio", "OpenAL Soft as the mod ships it", "The audio list")
- [research/33: the installer](../../../research/33-installer.md)
- [docs/trilogy-parity-ledger.md](../../trilogy-parity-ledger.md) rows G37 and G38
- Source files: [plugin/OpenAlSoft.cpp](../../../src/plugin/OpenAlSoft.cpp), [engine/VoicePlace.cpp](../../../src/engine/VoicePlace.cpp),
  [game/SoundPlace.cpp](../../../src/game/SoundPlace.cpp), [core/vr/SoundPlace.hpp](../../../src/core/vr/SoundPlace.hpp)
- [How to catalog](../how-to-catalog.md)

---
type: Gotcha
title: BIOEngine.ini's SystemSettings win, the game rewrites them, and a level load re-arms them
description: DynamicShadows and MotionBlur set in GamerSettings.ini are overridden by BIOEngine.ini's [SystemSettings], which the game rewrites; a loaded level renders with the old values, and `scale set` to the value already stored changes nothing.
tags: [le1, system-settings, ini, console]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-23, 2026-10-28, 2026-11-18]
---

# Symptom

Shadows, motion blur or grass look like the flat game's although GamerSettings.ini (and `tools/vr-game-settings.ps1`)
say otherwise; or they are right at the title and wrong once a save has loaded; or shadows come back after a cinematic,
a conversation or a menu and differ between the eyes (the owner, 2026-11-18, ME2: "shadows in general seem to be
running ... I get the different image in both left and right eye"; ME1: "the cinematics re-enable it").

# Cause

- For `DynamicShadows` and `MotionBlur`, BIOEngine.ini's `[SystemSettings]` wins over GamerSettings.ini's (measured
  2026-10-23).
- The game rewrites BIOEngine.ini: after every `scale set`, and a launch put the defaults back once.
- A level load renders with the blur again although every file and the title's `scale set` said off (2026-10-28).
- `scale set MotionBlur False` with False already stored did nothing; `True` then `False` turned the blur off.
  Observed for MotionBlur; the mod treats every boolean the same.

# Check

The log line `system settings: scale set ...` once a local player exists, and `(a new level: applied again)` after
each level change. By hand: `tools/console.ps1 "scale set MotionBlur True" "scale set MotionBlur False"`. `scale set`
works live for these three keys, not for `DetailMode` or the LOD distance scales
([the console](le-console-quirks.md)).

# Fix in place

`game/SystemSettings` (the mod): the three values through the local player's console once a player exists and again on
each new `engine::World()`, booleans through their opposite first. `[SystemSettings]` in `ASI\LE1MEVR.ini` overrides
any key (`apply = 0`: off). `tools/vr-game-settings.ps1` and the installer write the same three into BIOEngine.ini too.
What they are worth: at a heavy spot, shadows off and the foliage radius took the render thread from 9.35 to 6.88 ms
and the GPU from 8.4 to 4.9 (the grass costs per instance, twice a frame in stereo; 0.25 pops it in too close).

2026-11-08 combined candidate: an installed native motion-blur guard with zero unknown effects
replaces the blanket menu/conversation-return reapply. Initial, new-level, settle and real settings
changes retain this application path. If guard coverage is lost, the next poll restores the fallback;
older hosts retain it. See [research/62](../../../research/62-motion-blur-and-menu-pacing.md).

2026-11-09: healthy native protection now also consumes the 3/15-second settling deadlines without
blanket resets. Initial/new-level applications, real edits and lost-guard fallback remain. Two controlled
startup repeats validate reset removal; loading itself still stalls. See [research/68](../../../research/68-culling-and-startup-followup.md).

2026-11-18: the guard keeps only the blur off, and a cinematic turns the shadows back on: with the whole menu-return reapply
skipped, ME2 kept them (LE2MEVR.log's "skipped menu-return reapply after mode 6/7: native motion-blur guard healthy").
A return to gameplay with the guard healthy now sends the shadow keys alone again (DynamicShadows, LightEnvironmentShadows,
through their opposites: 4 commands; ME2 once the level has been in gameplay 3 s), and the character shadows are off on
every game (engine::adapter::kVrCharacterShadowsOff).

Related: [motion blur in stereo](motion-blur-in-stereo.md).

# Citations

[1] [research/35, attempts 27, 31 and 33 (2026-10-23)](../../../research/35-performance-attempt-log.md): the precedence, the rewrite, the mod's apply.
[2] [research/35, attempt 49 (2026-10-28)](../../../research/35-performance-attempt-log.md): the level load, the same-value no-op.
[3] [src/game/SystemSettings.cpp](../../../src/game/SystemSettings.cpp).
[4] [tools/vr-game-settings.ps1](../../../tools/vr-game-settings.ps1).

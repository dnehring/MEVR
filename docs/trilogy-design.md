# One VR system for the whole trilogy

> **Historical plan (2026-10-07).** The current design is in [CLAUDE.md](../CLAUDE.md) and [research/](../research/README.md).

**Principle:** LE1, LE2 and LE3 share one VR interaction model. When a game's mechanics don't fit
VR, or differ from the other two in a way that would split the design, we **change the mechanic**
and don't fork the VR system. LE1 ships first; LE2 and LE3 should mostly need new data tables and
hook adapters, not new systems.

Where a change affects balance or feel, it gets a toggle (`VR-native` / `classic`) when that's
cheap to support.

## Code shape

```
src/
  core/           game-agnostic: OpenXR, D3D11 submission, input frame, UI layers,
                  IK math, interaction system (grab, holster, reload), comfort
  engine/         UE3 bridge shared by all three games via LExSDKv2: camera/view hooks,
                  ProcessEvent helpers, AnimTree IK effectors, GFx render-target redirect
  games/
    le1/ le2/ le3/  thin adapters: class/function names, hook signatures, data tables
                    (weapons.json, holsters.json, hud_map.json)
```

One source tree builds three ASIs (`LE1MEVR.asi`, `LE2MEVR.asi`, `LE3MEVR.asi`), using the same
model as LExASIs ([research/09](../research/09-me3tweaks-code-survey.md)): link LExSDKv2's
`LESDK-LE1/LE2/LE3` static lib, which defines `SDK_TARGET_LEn`, and branch with
`#if defined(SDK_TARGET_LE1)` only inside `games/` and `engine/`. Our CMake function is our own
code, since LExASIs is unlicensed.

## Architecture rules

These are adopted from Overlord's design docs (ideas only; [research/10](../research/10-vr-code-survey.md))
and the LExASIs survey ([research/09](../research/09-me3tweaks-code-survey.md)).

1. **One immutable input frame per tick.** Nothing else polls OpenXR. Button edges are computed
   once. After a focus or context change, input waits for a neutral release.
2. **Explicit contexts** (Menu, Gameplay, Conversation, Cinematic, Vehicle, Dead), each with its
   own bindings. ME's conversation state, detected the way ConvoSniffer does it, drives the
   Conversation context.
3. **The game stays authoritative** for inventory, ammo, damage and scripts. VR asks the game to
   act (via UnrealScript functions through ProcessEvent) and never keeps a writable second copy.
4. **Hooks fail closed.** Check the exe build (2.0.0.48602) and validate resolved addresses.
   On mismatch, disable VR features and log instead of calling the SDK's abort-on-failure
   checks.
5. **Cache `UFunction*` pointers** for ProcessEvent filtering. No per-call `GetFullName()` string
   compares.
6. **Combat:** shots follow the muzzle axis. Aiming down sights never moves the camera. Recoil
   moves the gun, never the head. (LE1's own recoil turns the player controller and so the camera:
   `game/view_level` holds it level and turns the kick into a hand pulse, research/24.)
7. **Hardware truth:** a desktop or offline test passing is never reported as working in the headset.

## First-person base per game

| Game | Script-side first-person base | Status |
|------|-------------------------------|--------|
| LE1 | MassFPS (LE1) 1.2.2.1, fully permissive | Absorb ([research/11](../research/11-massfps.md)) |
| LE2 | MassFPS (LE2) | Exists; **permissions not yet checked** |
| LE3 | MassFPS (LE3) "coming soon" as of 2023 | Unknown; may need our own |

## Mechanics: per-game differences and the unified VR treatment

Game-mechanic rows marked **[verify]** come from memory and should be checked in-game.

| Mechanic | LE1 | LE2 | LE3 | Unified VR treatment | Mechanic change? |
|----------|-----|-----|-----|----------------------|------------------|
| Camera | 3rd-person over-the-shoulder | same | same | First-person from Shepard's eyes, with a full body | Yes, in all three |
| Ammo | Overheat, unlimited ammo | Thermal clips | Thermal clips | **Physical heat-sink reload everywhere:** eject, grab from the belt, insert | **LE1:** add an optional "vent" (eject the heat sink to cool instantly). Classic mode keeps pure overheat |
| Weapon slots | Pistol, shotgun, AR, sniper | + SMG, heavy weapon | Weight-based loadout **[verify]** | One holster layout: pistol R hip, SMG L hip, long guns over the shoulders, heavy weapon on the lower back. Empty slots stay empty | No |
| Aiming | Reticle and stick | same | same | Controller aim, two-hand stabilization, sights where a weapon has a scope | Yes, in all three |
| Powers | Wheel plus hotkeys | same | same | Off-hand radial menu (slows time like the wheel) plus 3 quick-cast bindings. Gestures later | No |
| Melee | Basic | Basic | Heavy omni-blade melee | Physical swing above a velocity threshold triggers melee/heavy melee | Light: swing replaces the button |
| Grenades / thrown powers | Grenades **[verify]** | n/a **[verify]** | Some classes **[verify]** | Physical throw where the game has them | No |
| Cover | Contextual **[verify]** | Button | Button | Same binding in all three, plus physical lean and peek. No forced cover camera | Yes: the cover camera is removed |
| Combat roll / sprint | Sprint | Sprint | Sprint plus roll | Button-driven. Roll uses a brief comfort fade | No |
| Squad orders | Button plus reticle | same | same | **Point with the controller** and press to order | Input only |
| Dialogue | Wheel | Wheel plus **interrupts** | Wheel plus interrupts | Wheel on a world-anchored panel; interrupts pop up beside the matching hand (pull its trigger) | No |
| Conversations / cutscenes | Cinematic cameras | same | same | Cinema screen by default; in-world for static shots (stretch) | No |
| Bypass / hacking | Bypass minigame **[verify LE version]** | Circuit and code minigames | Minimal | Panel plus laser pointer | No |
| Galaxy map / scanning | Cursor map | Ship steering, fuel, probing | Map, scanning, Reaper chase | Panel to start; holographic tabletop is a stretch | No |
| Vehicles | Mako (core) | Hammerhead (DLC) | none **[verify]** | Cinema screen to start; cockpit view is a stretch | No |
| Menus / inventory | Heavy inventory | Lockers, research | Weapon bench | Cylinder layer plus pointer | No |

## Change log

Record each mechanic change here when it's implemented: what changed, why, the toggle name, and
which games it applies to.

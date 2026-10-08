# 01: Feasibility

## The original question

Could we build VR basics in the older Unreal Engine (UE3 / UDK) and port them into Mass Effect
Legendary Edition?

## Conclusion

**VR for ME LE is feasible. Porting it from UDK is not.** VR is a renderer- and engine-level feature
(stereo views, per-eye projection, head pose fed into the camera, frame submission to the XR
runtime). In ME LE that code is native C++ compiled into `MassEffect1.exe`, `MassEffect2.exe` and
`MassEffect3.exe`. Package content (meshes, lightmaps, UnrealScript) can move between UDK and ME
LE, but nothing in a package can change how the renderer draws a frame. UDK also never shipped
its C++ source (it was binary-only, with UnrealScript source), and BioWare's engine is a separate
fork either way.

## What *does* port from UDK

- Level geometry and lightmaps. The ME modding community builds geometry and bakes lighting in
  **UDK 2015**, then imports it with Legendary Explorer. The LEX wiki tutorial calls the method
  experimental, and says the public UDK is "close enough" to BioWare's internal build for this,
  since ME levels are "almost exclusively" static meshes. Suggested tools: current LEX, UDK 2015,
  and optionally uModel, an ActorX/FBX batch exporter and 3ds Max 2017+.
  [LEX wiki mirror]
- ME3Tweaks spent weeks debugging UDK so they could export whole maps into it and compute lighting
  textures. They needed close mesh approximations for bounce lighting and chose uncompressed
  lightmaps over DXT1. [ME3Tweaks blog]
- A Polycount user hit a material-slot loss (3 materials became 1) going XSI → ActorX → UDK. This
  is unrelated to VR, but recorded as a pitfall for any mesh work. [Polycount]

## What already exists for "Mass Effect VR"

- **No VR mod for Legendary Edition was found** (searched 2026-10-07: web, Nexus top-mods coverage,
  PC Gamer and GameRevolution mod roundups).
- **"MASS EFFECT VR" on itch.io (kawinfinity)** takes the opposite route: ME1's *original-trilogy*
  assets were extracted with modding tools, rebuilt in Blender, and assembled in **Unreal Engine 4**.
  It's a prototype where you can explore the Normandy, with known bugs. It requires SteamVR and
  works with any PC VR headset. A commenter asked about LE and Quest 2 support; no reply was
  visible. This shows that content moves *out* easily, but the result is a walkthrough without
  the game's dialogue, combat or saves, and it redistributes EA's assets. [itch.io]
- **vorpX:** no information found about a profile for ME LE. [searched, not found]

## Why there's no off-the-shelf shortcut

- **UEVR** supports UE **4.8–5.4** only. No evidence of UE3 support. Its method (forcing UE4/5's
  built-in stereo path and hooking the camera and player controller) depends on UE4+ features.
  See [05-vr-frameworks.md](05-vr-frameworks.md).
- No ME LE vorpX or geometry-3D profile was found.

## Path chosen

A native ASI plugin loaded by ME3Tweaks' LEBinkProxy, hooking the engine directly. See
[04-me-le-modding-ecosystem.md](04-me-le-modding-ecosystem.md) for the tooling and
[../docs/vr-features-plan.md](../docs/vr-features-plan.md) for the plan.

### Milestone ladder (rationale)

| # | Milestone | Why it's ordered here |
|---|-----------|-----------------------|
| M0 | ASI loads, logs, overlay | Proves the toolchain and loader |
| M1 | Plugin controls the camera | Proves we found the view-construction hook |
| M2 | Flat game on a virtual screen in the HMD | OpenXR plus D3D11 interop without touching the renderer. Usable on its own |
| M3 | Head tracking drives the camera (mono) | Camera hook plus pose; no stereo yet |
| M4 | True stereo, per-eye projection | The hard renderer work |
| M5 | HUD layers, conversation and cutscene handling | Needs M4's view control and UI capture |
| M6 | Motion-controlled weapons, body IK | Gameplay layer on top of a working VR view |

## Where AI-assisted work helps (and where it doesn't)

- **Helps:** naming functions in Ghidra decompilation; cross-referencing against UE3 UnrealScript
  (UDK's `Development/Src` and LEX-decompiled ME scripts); writing OpenXR, D3D11 and hook
  boilerplate.
- **Doesn't help:** in-headset testing, comfort tuning and feel. That's the human bottleneck.

## Sources

[LEX wiki mirror], [ME3Tweaks blog], [Polycount], [itch.io]; see [sources.md](sources.md).

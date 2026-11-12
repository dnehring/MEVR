---
type: Gotcha
title: The game's camera collision drops the first-person view to Shepard's knees
description: Each LE1 camera mode pulls the camera back toward its hook (the Camera bone at the pawn's centre) when anything lies between them; Halcyon's first-person camera sits 0.5 m ahead of that bone, so a wall, pillar or person just ahead of the face dropped the view 1.2 m, and Halcyon drew the eye behind Shepard's knees
tags: [first-person, camera, halcyon, mod]
status: stable
timestamp: 2026-11-11T17:20:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-11T17:20:00Z }
bitten: [2026-11-11]
---

# Symptom

On foot in first person the eyes stand behind Shepard's knees, looking at the backs of his legs, the game's camera about
0.62 m above the floor instead of 1.8 m; it lasts as long as he stands there and goes when he moves away. Earlier wordings:
"an opening that starts from a view at Shepard's hips" and "the view on foot at the pawn's place, 0.26 m below its centre"
(the conversation's opening took that view and cut, his mark "out of sight" from there); "the view inside his torso" (the
surveyor's scene); the judge's `body-strayed` 2.2 m. It looks like a crouch, a bad save or the floor lock; it is none of them.

# Cause

`SFXCameraMode.bCollisionEnabled`: with it set, `SFXPlayerCamera.UpdateCameraMode` calls the mode's `DoCameraCollision`,
which moves the camera back toward the mode's hook (`ActorCameraHook`: for the explore and combat modes the `Camera` bone,
which stands at the pawn's centre) when the line from the hook to the camera is blocked. For the game's third-person
camera that keeps walls out of the shot. Halcyon's first person writes the explore mode's Offset so that the camera stands
0.5 m ahead of the hook at head height, just ahead of the face; anything there (a wall faced up close, a pillar, a squadmate
crossing in front) pulls the camera to the hook, 0.26 m below the pawn's centre, and Halcyon draws the eye 0.43 m behind
the game's camera: behind the knees.

# Check

The mod's camera probe line (`camera: mode ... HookName Camera ... the formula from it puts the camera at X: 0.0 units from
the mode's m_pov, 129.8 from the game camera`): the game's camera far from the mode's own point is the collision. A snapshot's
`floorLock.rawCameraZ - pawnFloorZ` near 0.6 m on foot; moving Shepard a metre or two changes it back to 1.8 m.

# Fix in place

`game/CameraCollision` (mod, `[Game] cameraCollision`, default 0): while the view is head-tracked on foot, the current mode
(and an interpolation's target) of Halcyon's first-person classes (Explore, ExploreStorm, Combat, CombatStorm, TightAim;
never Vehicle, Sniper, HipAim) has `bCollisionEnabled` cleared, journalled, and put back when the view stops being
head-tracked or the mod unloads. Log `camera collision:`, snapshot `"mod"."cameraCollision"`.

# Citations

- research/69-conversations-everywhere.md, "The camera that fell to Shepard's knees" (item 18).
- `src/game/CameraCollision.cpp`, `src/game/CameraProbe.cpp`, `src/halcyon/le1/head_aim.cpp` (`FpOwnAndWrite`).

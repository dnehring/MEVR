---
type: Gotcha
title: The culling camera sits ahead of the first-person eye, so the player's own body is culled while in view
description: The engine culls with the game's camera; Halcyon draws the eye behind it by the render-side view offset (0.43 m with the shipped "view back" framing, plus the lean), so Umbra's frustum misses the body's tight box right under the eye at some head angles; the body, arms and hands vanish and come back with the pose
tags: [first-person, rendering, umbra, vrik, mod]
status: stable
timestamp: 2026-11-10T18:05:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-10T18:05:00Z }
bitten: [2026-11-06, 2026-11-10]
---

# Symptom

In first person the player's own body (arms, hands, chest) is missing although every hide flag is clear, and whether it
shows depends on where the head points and on the pose a moment earlier. At the Council's podium it was gone for a whole
conversation when facing the Council and looking down 50 degrees, there when turned 30 degrees aside or looking down 65,
and it stayed once it had been seen. Earlier wordings: "the arms and gun dropped out after a look up and back" (the
owner's QA, 2026-11-06, then put down to the one-Draw window); "no body is drawn in any scene in the world" (2026-11-10).
The mesh's `LastRenderTime` stands still; `UMBRA OCCLUSIONCULLING` and `TOGGLEOCCLUSION` change nothing.

# Cause

The engine culls with the game's camera: Umbra's camera is set in `BeginRenderingViewFamily` (`0x7655A0`) from the scene
view's CPU view and projection matrices. Halcyon draws the eye somewhere else: its render-side view offset (the camera
offset, the "view back" framing `flatScreenDistance` and the lean; `xr_session.cpp`, "Render-side view offset") goes into
the GPU matrices only. With the shipped framing the eye is 0.43 m behind and 0.18 m below the game's camera. A frustum
with its apex 0.43 m ahead of the eye misses what is directly under the eye: at the podium the body's box (the physics
bodies' box, 27 x 32 x 92 units around the pawn) lay 4 cm behind the culling frustum's lower plane, and 3 cm inside it
after a look back had changed the pose, which is why it came and went.

Umbra culls by its own box per object, which `UPrimitiveComponent`'s hand-over (`0x25CFE0`, the last call of
`UpdateBounds`) makes from the component's `Bounds`; the scene's copy of the bounds (scene info +0x54) is sent later with
the transform update and is not what Umbra tests.

# Check

`mevr convo body` (or `[Cinematics] worldBodyLog = 1`): flags clear and `rendered N s ago` growing while the body should
be in view is this. The line also prints the component's box and the renderer's copy. Turning the head 30 degrees aside
or looking further down brings the body back at once.

# Fix in place

`engine/BodyBounds` (mod): a post-hook on `USkeletalMeshComponent::UpdateBounds` (`0x58EEC0`) grows the box and sphere of
the player's body mesh and its pose followers by the length of the view offset plus 15 units (62 units with the shipped
settings; `game/ArmIk` `ProtectBodyBounds` names the meshes and the pad each tick from the host's view) and calls the
Umbra hand-over again, so Umbra's box holds the culling camera whenever the eye can see the body. Bounds still holding
our last result are not padded twice (the engine's function can return without computing). Log `body bounds: the body's
boxes are padded by N units`.

Padding `Bounds` alone is not enough (Umbra keeps the tight box), and forcing the component update does nothing (the
bounds were never stale).

# Citations

- research/69-conversations-everywhere.md, "The body that stayed culled: the culling camera is ahead of the eye".
- `src/engine/BodyBounds.cpp`, `src/game/ArmIk.cpp` (`ProtectBodyBounds`), `src/halcyon/le1/xr_session.cpp` ("Render-side view offset").

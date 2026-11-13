---
type: Gotcha
title: A held first-person view's place is the game's camera, not the player's eyes
description: In a conversation or moment held in the world, gC.hold is the game's camera; the host draws the eyes 0.43 m behind and 0.18 m below it (its view offset), so anything that takes the held place for the player aims, turns or places 0.43 m off; take PlayerEyes() (the tick frame's headWorld)
tags: [first-person, conversations, halcyon, mod]
status: stable
timestamp: 2026-11-12T19:40:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-12T19:40:00Z }
bitten: [2026-11-10, 2026-11-12]
---

# Symptom

In a first-person conversation people look just past the player, at where Shepard's hidden head is ("weird head behaviour at
Virmire's start", the owner, 2026-11-12; Kirrahe's face 8-18 degrees off the player's eyes, 3-13 off Shepard's head); the
right stick's turn swings the player round a point in front of them; the conversational circle forms half a metre ahead of the
player; at the Council's hearing the player's eyes stand behind Shepard's own head. Earlier wording of the same offset: the
player's own body culled under the eye (2026-11-10, [the culling camera](culling-camera-ahead-of-the-eye.md)).

# Cause

The held place (`gC.hold` in game/ConversationWorld, the moment's too) is written into the game's camera; Halcyon draws the eyes
at it moved by its render-side view offset (the camera offset, the "view back" framing `flatScreenDistance`, the lean), along the
held view's axes: 43 units back, 4 left and 18 down with the shipped settings (`vr::host_view::State` `offsetForward/Right/Up`).
`engine::FromCameraView` applies the same offset to give the tick's frame its `headWorld`, which arm IK and body follow put the
body under. So the held place is 0.43 m ahead of the eyes, and Shepard's head bone, with the body under the player, is 6-9 cm
from the drawn eyes, not from the held place.

# Check

`mevr convo stances on` (or `conversation-from-save.ps1 -Stances`), then `python tools/conversation-stance.py --audit`: the
column "Shepard's head from your eyes" is 0.06-0.17 m in a scene where the body is with the player. A record's `player.at` is
the held place, `player.eyes` the drawn eyes; they should be 43 units apart flat.

# Fix in place

`PlayerEyes()` (the tick frame's `headWorld`) is the player for the eyes stand-in (`StepEyes`), the re-face rule, the circle and
the squad's formation; a place meant for the eyes (an opening at Shepard's mark: his head bone plus `gFoot.eyeFromHead`, which
is arm IK's measure of the drawn eyes) is held at that place minus `FlatViewOffset()`; `TurnBy` shifts the held place so the
eyes stay where they are. Any new rule that means "where the player is" takes `PlayerEyes()`, not `gC.hold`.

# Citations

- research/69-conversations-everywhere.md, "Heads at the drawn eyes: the audit" and "The body that stayed culled".
- `src/game/ConversationWorld.cpp` (`PlayerEyes`, `FlatViewOffset`, `TurnBy`, `StepEyes`, `FinishOpening`).

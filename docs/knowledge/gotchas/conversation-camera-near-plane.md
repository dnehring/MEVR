---
type: Gotcha
title: A conversation's camera sets the near clip plane per shot, metres out
description: LE1's conversation camera writes LocalPlayer.NearClipPlane for every shot (0.3 to 2.8 m against 0.1 m on foot); a view drawn from the player's own eyes during a conversation loses everything within that distance, the player's body and the floor included, and it looks like culling
tags: [conversations, first-person, rendering, mod]
status: stable
timestamp: 2026-11-10T16:30:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-10T16:30:00Z }
bitten: [2026-11-06, 2026-11-10]
---

# Symptom

In a first-person (in the world) conversation or scripted walk, things near the player are missing while the scene around
is there, and it changes with who speaks. The owner's words: "assets missing or blurred from where the player stands"
(the comm room, 2026-11-06); "when the council members would talk, the assets near me would pretty much disappear, and
when it would be my turn to talk, we'd pretty much reappear" (2026-11-10). In snapshots: the player's own body gone,
and the floor under the player cut away along a straight line across the picture with the level below showing through
(a plane across the view, not the outline of a mesh). In the log every hide flag is clear and the body's
`LastRenderTime` stands still, which reads like a visibility or culling fault. It is not.

# Cause

`BioCameraBehaviorConversation` carries a near plane per shot (`m_fNearPlane`, from the stage camera:
`BioStage.GetStageCamNearPlane`) and the game writes it into `LocalPlayer.NearClipPlane` when the shot changes. The
values suit a long lens some metres from its subject: 279.5 units in Avina's first shot, 177.5 on Shepard's line, 67.5
after; 30 to 200 in the council chamber; 10 on foot. The main view's projection takes its near plane from there, so a
view held at the player's own eyes clips everything nearer than that: arms, chest, the floor at one's feet, a person
standing beside the player. (The body's `LastRenderTime` stops as well; why it is not even submitted was not read.)

# Check

`mevr convo body` (or `[Cinematics] worldBodyLog = 1`, then `convo: body:` once a second): the line ends with `near plane
N (LocalPlayer)` and each conversation camera's `near` and `fov`. Anything but the on-foot value while a world view is
held is this. `convo: the director's near plane is N units ... the on-foot plane is kept instead` is the fix at work.

# Fix in place

`game/ConversationWorld`'s `Camera.UpdateCamera` post-handler writes the on-foot plane back into
`LocalPlayer.NearClipPlane` after the camera's own update and before the draw, while a world view is held (a
conversation or a moment; `[Cinematics] worldNearPlane = 1`, default; journalled as `ConversationWorld.nearClipPlane`).
The game sets its own value again at the next shot and on foot.

The first attempt at the 2026-11-06 report held the game's whole camera at the player's place (`worldCamera = 1`),
on the theory that culling followed the director's camera. It did not cure the symptom, and together with the Umbra
eye-matrix lever it culled the player's body in every shot; it is off by default since 2026-11-10.

# Citations

- research/69-conversations-everywhere.md, "Phase A, run" and "The first shot: the director's near plane".
- `src/game/ConversationWorld.cpp` (`gNearHold`, `BodyLine`).

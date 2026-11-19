---
type: Gotcha
title: A held scene kept the floor on foot, so after a ride the scaled body floated
description: Arm IK scales the body about view_anchor's floor; a held view kept the floor captured when the hold began, so after a scripted ride (the council chamber's, 2.33 m) the body at scale 1.1 stood 0.25-0.28 m in the air; ConversationWorld passes the floor under the drawn body with every hold
tags: [first-person, conversations, body, mod]
status: stable
timestamp: 2026-11-18T21:00:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-18T21:00:00Z }
bitten: [2026-11-17, 2026-11-18]
---

# Symptom

In a first-person conversation at the Council (the hearing, after the ride up to the chamber) the player floats: "my height is
closer, but I'm still floating above where I should be" (the owner, 2026-11-17, at the podium); "it looks like I'm floating ...
my knees are kind of hanging" (2026-11-18, on the bridge, with `[Hands] crouchBody` on); looking down, the eyes inside Shepard's
chest, or his knees bent forward with the feet off the floor. Snapshots: `mod.torso.result.crouchGapM` 0.25 or more with the
player standing; the log's `torso: back to the game's height after a crouch with you: at most 69 cm down` in a scene.

# Cause

Arm IK scales the body to the player (research/58) about the floor `view_anchor::FloorReference()` gives. In a held view
(a conversation or a moment in the world) that is the reference passed to `view_anchor::Hold`, which took it only on the first
call of the hold: the floor on foot when the scene began. A scripted ride before the scene (the moment up to the council
chamber: `moment: summary {... "riseM": 2.33 ...}`) leaves that floor 2.3 m under the body, and scaling about it lifts the body
by (scale - 1) x 2.33 m: 0.24-0.28 m at 1.10-1.12. research/69 item 44 (2026-11-17) brought the eyes to the right height over
the floor under Shepard, which left the body as the one still floating; `crouchBody` then bent its knees to reach the eyes.

# Check

The log's `floor lock: IK hold start: ... floor Z <z>` against the floor under Shepard in the scene (his `Location.z -
CollisionHeight`); a snapshot's `mod.torso.result.crouchGapM` (near 0 standing) and `mod.bodyScale.scale`. After the fix the log
says `convo: at N s the body's size is taken about the floor under it, N m above the floor on foot when the scene began`.

# Fix in place

game/ConversationWorld's `HeldFloorView(pawn)`: the floor on foot's reference with `pawnFloorZ` set to the floor under the drawn
body (the pawn's cylinder floor, at `gC.detachedAt` while the view is detached: BodyAway's `riseUnits`), passed with every hold;
`view_anchor::Hold` takes a passed reference on every call. Any new hold that moves the view vertically passes the floor it
stands on.

# Citations

- research/29-body-follow.md, "The floor of a held scene (2026-11-18)".
- research/69-conversations-everywhere.md, item 44.
- `src/game/ConversationWorld.cpp` (`HeldFloorView`), `src/game/ViewAnchor.cpp` (`Hold`), `src/game/ArmIk.cpp` (the body scale's
  pivot).

---
type: Gotcha
title: Arm IK falls to its third-person anchor and stays there, and the body leaves the player
description: Arm IK places the body in the view's frame only while the view is within 45 units of Shepard's animated head (it leaves past 75); one bad tick (a scene's opening, a crouch with the body scaled) drops it to the third person, where the body stands where the game animates Shepard, and nothing brings it back
tags: [vrik, first-person, conversations, mod]
status: stable
timestamp: 2026-11-13T13:00:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-13T13:00:00Z }
bitten: [2026-11-09, 2026-11-13]
---

# Symptom

| wording | where |
|---|---|
| "weapons very low" after a crouch, until holstering | on foot with the body scaled to the player (the owner, 2026-11-09) |
| "I'm still not synced up with my body ... my body is where I was previously walking" | the council hearing after the walk with Anderson, first person (the owner, 2026-11-13: the body up to 5.7 m from the eyes for 225 of 227 s, and through the two conversations after it) |

# Cause

`game/HandTargets` chooses the frame the controllers are placed in by the view's distance to Shepard's animated head bone:
first person (the view's frame, the body under the player) within `kFirstPersonEnterUnits` (45), third person (tracking space
hung on Shepard's animated head) past `kFirstPersonLeaveUnits` (75), unless the caller says the view is the player's anyway
(`inView`). Once in the third person the body follower makes no slide (`third person: the hips turn`), so the body stays where
the game animates him, and the view may never come back within 45 units of that head: it is held elsewhere (a scene in the
world), or the scaled body puts it 44 units off at best (the crouch).

In the hearing the first bad tick came from reading our own writes: the scene's opening placed the player at Shepard's head bone
read from the drawn bones (`HeadOf`), which the body follower had slid to where the player walked during the scripted walk; the
follower then chased that head as the slide went away (4.84 m, 0.81 m), and arm IK saw the view 2.54 m from the animated head.

# Check

The log: `arm IK: body anchor: third person (tracking origin at Shepard's animated head), view N units ...` while the player is
in first person, and no later `body anchor: first person`. In a scene: `convo: audit: body-third-person`, the summary's
`audit.thirdPersonS`, and `convo: audit totals:`. Unattended runs only see it with tracked hands: the fake headset has no
controllers, so `tools/conversation-audit.ps1` replays a recording's hands in every scene.

# Fix in place

- Every conversation shown in the world counts as the view held away from the pawn (`conversation_world::BodyAway`), so arm IK is
  in the view's frame and the body follower puts the body where the player stands, however far.
- Whatever places the player by Shepard's head reads his animated head (`arm_ik::AnimatedHeadComponent`, `MarkHeadOf` in
  game/ConversationWorld), never the drawn one.
- The crouch (2026-11-09): the animated head is compared scaled as drawn (game/ArmIk's `headBone`).
- `tools/scene_audit.py`: `eyes-off-body` and a 0.3 m gap are never excused by a baseline.

# Citations

- research/69-conversations-everywhere.md, "The body that stayed at the mark".
- `src/game/HandTargets.cpp` (`kFirstPersonEnterUnits`), `src/game/ArmIk.cpp`, `src/game/ConversationWorld.cpp` (`BodyAway`,
  `MarkHeadOf`, `StepAudit`).

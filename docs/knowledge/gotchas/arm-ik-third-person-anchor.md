---
type: Gotcha
title: Arm IK falls to its third-person anchor and stays there, and the body leaves the player
description: Arm IK places the body in the view's frame only while the view is within 45 units of Shepard's animated head (it leaves past 75); one bad tick (a scene's opening, a crouch with the body scaled, the game's crouch under a floor-locked view) drops it to the third person, where the body stands where the game animates Shepard, and nothing brings it back
tags: [vrik, first-person, conversations, mod]
status: stable
timestamp: 2026-11-13T13:00:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-13T13:00:00Z }
bitten: [2026-11-09, 2026-11-13, 2026-11-16, 2026-11-18]
---

# Symptom

| wording | where |
|---|---|
| "weapons very low" after a crouch, until holstering | on foot with the body scaled to the player (the owner, 2026-11-09) |
| "I'm still not synced up with my body ... my body is where I was previously walking" | the council hearing after the walk with Anderson, first person (the owner, 2026-11-13: the body up to 5.7 m from the eyes for 225 of 227 s, and through the two conversations after it) |
| `vault.ps1`: the wrists up to 1.49 m from the controllers for 35 ticks of ME2's vault; `body anchor: third person ... view 104 units` | ME2 once the floor lock ran there (lane C3, 2026-11-16): the eye solved from the pawn's floor left the animated head; its own gotcha: floor-lock-through-a-traversal-move |
| "almost like clicks and moves Shepard to be behind cover, where it's almost magnetic"; `body anchor: third person ... view 75 units` 0.3-10 s into every crouch, back at 44 when the game stood him up | ME2 behind barriers (the owner, 2026-11-18; lanes CV2, M2W, ANC): the physical crouch crouched Shepard by the game, his animated head 45-60 units under the floor-locked view, and as the player rose before the game stood him the gap reached 75; the drawn body jumped 0.5-0.76 m back to the pawn and out again |

# Cause

`game/HandTargets` chooses the frame the controllers are placed in by the view's distance to Shepard's animated head bone:
first person (the view's frame, the body under the player) within `kFirstPersonEnterUnits` (45), third person (tracking space
hung on Shepard's animated head) past `kFirstPersonLeaveUnits` (75), unless the caller says the view is the player's anyway
(`inView`). Once in the third person the body follower makes no slide (`third person: the hips turn`), so the body stays where
the game animates him, and the view may never come back within 45 units of that head: it is held elsewhere (a scene in the
world), or the scaled body puts it 44 units off at best (the crouch).

The crouch (2026-11-18): the rule measured the whole distance, but with the floor lock on the view's height is the player's own
head, so its vertical part is only how the player stands against the animation (the game's crouch, a squat with the game
standing, the player rising before the game stands him), never where the camera is.

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
- The crouch (2026-11-18, every game): with the view's height the player's own (the host's floor lock, or a crouch on foot out
  of cover) only the horizontal distance decides (`ik::FirstPersonAnchor` in core/ik/BodyFollow, `viewHeightIsPlayers` from
  game/ArmIk through `hand_targets::Current`); the log line adds `(N across: ...)`. Third-person cameras stay a metre and
  more away across. Tests `tests/CrouchAnchorTests.cpp`; the run `tools/le2-halcyon-run/crouch-anchor.ps1`.
- `tools/scene_audit.py`: `eyes-off-body` and a 0.3 m gap are never excused by a baseline.

# Citations

- research/69-conversations-everywhere.md, "The body that stayed at the mark".
- research/29-body-follow.md, "The crouch that went third person"; research/23-cover.md sections 7.6 and 8.1.
- `src/core/ik/BodyFollow.cpp` (`FirstPersonAnchor`), `src/game/HandTargets.cpp`, `src/game/ArmIk.cpp`, `src/game/ConversationWorld.cpp` (`BodyAway`,
  `MarkHeadOf`, `StepAudit`).

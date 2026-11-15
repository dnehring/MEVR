<!-- SPDX-License-Identifier: MIT -->
# LE3 shared-menu body presentation

Audit and candidate implementation, 2026-11-15. The coordinator's protected preview
used integration `da486666`. This lane read the existing captures and source; it did
not launch a game, change VR/settings or deploy the candidate.

## Evidence and rejected hypothesis

The private snapshot receipts are identified by these file stems:

| Capture | Native mode / first person | Floor lock active | Correction | Eye centre Z | IK / torso |
| --- | --- | --- | --- | --- | --- |
| `2026-11-15_14-55-37-446` (`le3-preview-hands`) | 0 / on | false | 0 | 146.149979 | controller targets / driven |
| `2026-11-15_14-55-45-477` (`le3-preview-menu`) | 0 / on | false | 0 | 146.149979 | no targets / not driven |
| `2026-11-15_14-55-52-493` (`le3-preview-resume`) | 0 / on | false | 0 | 146.149979 | controller targets / driven |

All three report raw camera Z 146.149979, pawn floor Z 3.150002, final eye height
1.43 m, and a STAGE eye height of **zero**. The zero floor height already prevents
floor correction before the menu opens. Thus the suspected loss of floor correction
does **not** explain this run's menu silhouette. The menu's world image shows a large
nearby dark body surface. Its receipt reports no snapshot errors and a valid installed
first-person view hook. The capture still does not establish body identity by itself.

The consistent source and state change is:

- `plugin/MenusLe2.cpp::OnFrameBegun` publishes `calibration::Hidden(state)` while
  the shared main menu owns input, except for deliberate live adjustments.
- `plugin/Calibration.cpp::Hidden` clears poses and marks controllers inactive.
  Snapshot controller data is the earlier raw TestLoop sample, so its `active=true`
  does not contradict the mod seeing no controller targets.
- `game/HandTargets.cpp::Current` rejects inactive controllers.
- `game/ArmIk.cpp::Drive` calls `Stop` when there are no targets; `Stop` clears the
  bone-pose request, resets the body follower and stops applying body scale.
  The renderer's first-person body remains visible in native mode 0.
- `plugin/HalcyonBridge.cpp` uses the same hidden-controller policy for LE1. There
  is no existing general shared-menu torso hold to reuse. Its live-adjustment
  poses-only exception deliberately permits a live calibration preview.

Body-scale diagnostics also drift from 0.9342 to 0.9196 to 0.9097 using the legacy
view/lowest-bone fallback; the floor reference is unavailable. That is a separate
measurement concern, not proof of a floor-lock transition or a scale fix here.

## Narrow candidate

`game/MenuBodyHold.hpp` retains a sanitized copy of the existing bone-pose request;
it does not create a second body solver or expose gameplay controllers. `ArmIk`
captures only a fresh, first-person request that already moves the torso/body under
the view. During an ordinary shared-menu overlay it republishes that stationary
torso and body scale through the existing guarded bone-pose writer.

Arm targets, arm rotations, support grip, finger controls, gun shift, gun kick,
clavicle reach adjustment and leg targets are disabled in this copy. Shoulder-chain
metadata and protected camera-bone metadata remain because the torso solver and its
native write validation require them. `RightHandAimsGun` and support requests stay
false. The original request is not mutated.

The candidate is wired for **LE3 only**. LE1/LE2 retain their current menu behavior;
the shared value-only cache and tests are reusable after equivalent proof. No host
ABI field or raw-controller publication path is added. The normal live-adjustment
path still receives its existing poses-only input and does not freeze the torso.
Guided calibration remains excluded.

Every held tick requires a fresh host world view, the actual shared-menu owner,
normal module/runtime ownership, native mode 0, the existing camera-owner/no-cover
sample, no current LE3 custom action, movement not ignored, and a live pawn. World,
controller, pawn, mesh, skeletal asset, object names/classes, component transform,
native camera POV, floor-correction state and XR session/recenter epoch must still
match the captured values. Live objects are revalidated before making that identity;
the cache never dereferences retained pointer values. The existing bone-pose writer
continues checking owner/world, bone count and bone names at the actual write.

The first menu tick must be within 250 ms of the ordinary-body sample. Subsequent
renewals require a validated tick at least every 250 ms. A mismatch, load, recenter,
session change, native camera/actor motion, invalid sample, expired heartbeat,
menu close, shutdown or ordinary IK stop clears the hold; it cannot recover merely
because the old pointer values reappear. No cached pose is fabricated when the menu
opens before a valid first-person body placement exists.

Status explicitly reports `shared menu: stationary torso retained; hand targets
and gun control off`, or a refusal to retain. This is **not visible wrist/menu
interaction parity**. Hands continue to be hidden from gameplay input. Native arm
animation remains native under the held torso.

## Checks and remaining acceptance

The focused `MenuBodyHoldTests` exercise preserved torso/scale/camera-bone metadata,
removed hand/weapon controls, fresh entry and ongoing renewal, menu close, stale
and backwards time, actor/world/mesh/name/class replacement, native transform/POV
changes, floor-policy changes, XR epoch changes and denied eligibility. The actual
game files compile under strict warnings for all three targets.

No post-change game image has been captured. The next native comparison should
repeat the same hands/menu/resume sequence, inspect both eyes and world layers,
confirm the hold status and stable torso/body scale, and test camera/actor movement,
menu close, recenter, focus/session loss and calibration. A runtime refusal means
the candidate failed closed; it is not visual acceptance.

`plugin/FloorLock.cpp` still suppresses correction for the shared menu with a valid
floor sample. That separate source-level discontinuity needs a controlled valid-floor
comparison and a deliberate camera policy; this patch does not change it. A change
in floor-lock state invalidates the cached body placement so it cannot be reused in
a different eye-correction space. The present zero-height preview cannot validate
that case or physical-headset comfort.

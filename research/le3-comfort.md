<!-- SPDX-License-Identifier: MIT -->
# ME3 shared comfort adapter

The adapter reuses ME1/ME2's `game/Comfort`, core snap/smooth-turn rules, left-hand walking and vignette.
The pinned LE3 SDK declares the same native camera mode/input fields; the protected 259-row native probe
confirmed the added camera fields and rotation/movement-ignore parameter contracts. No SDK offsets are used
in the gameplay adapter. The existing per-camera allowlist remains restricted to ordinary explore/combat/aim modes.

LE3 adds a fresh native admission check: live player and BioPawn, ordinary gameplay, no current custom action,
resolved physics/cover fields and `IsMoveInputIgnored == false`. An unavailable return denies movement. This gates
turning, hand-directed walking and locomotion vignette independently of the player's physical seated calibration.
Existing menu, cover, ladder, controller-focus and one-tick pad-ownership rules still apply. Snap turning retains
its neutral-release latch. Traversal's persistent authored-yaw correction is separate work; comfort does not
take rotation from an active native traversal action.

Development builds for all three games and focused shared/core tests pass. Native snap/smooth turn, cover and
authored seating behavior, tracked eye orientation, controller-relative walking, and physical comfort remain
acceptance gates. Metadata agreement and compilation do not prove those outcomes.

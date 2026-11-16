<!-- SPDX-License-Identifier: MIT -->
# ME3 interaction selection from headset look

2026-11-15. Follow the [reuse audit](../docs/le3-port-reuse-audit.md).
Reuse `game/Interact` and `engine/SelectionGaze`; do not add another interaction
raycast, force a selected actor, expand native use range or turn combat into head aim.

## Evidence that changes the adapter

The protected Normandy approach `20261115-184731-505-LE3-map-approach` reached
the table but did not enter galaxy mode. Its 17 captures have no image errors;
selection is empty at the table and later identifies a door after walking past it.
Every sample reports gaze enabled but `gazeHooked = false`. All game/profile
files restored with zero differences, and the null-driver, SteamVR, chaperone and
LE1 ini hashes independently matched their restoration receipt.

This does not prove that the absent hook caused the failed table entry: the
interaction point and look direction also need validation. It does establish a
missing ME1 interaction feature that must be resolved before claiming parity.

The existing metadata probe, extended by two read-only rows, passed 511/511
comparisons in `20261115-185335-159-LE3-selection-probe`. Both independent object
readers identified `BioPlayerSelection.UpdateSelection` at native thunk RVA
`0xB75BC0`, with zero reflected parameter bytes. The protected desktop-only run
never enabled SteamVR, stopped normally and restored with zero differences.

Read-only inspection through the existing LE3 Ghidra project establishes:

| Native path | Contract and implication |
| --- | --- |
| `0xB75BC0` | No-argument script thunk, calls `0xAA55F0` with the selection object. |
| `0xAA55F0` | `void(this)` update body, called from the player-controller tick and the script thunk. Updates selection and the native prompt representation. |
| `0xA9BF90` | `void(this)` target search, called by the update body and separate thunk `0xB75B80`. This narrower function is the gaze-hook candidate. |
| Search origin | Selection's outer controller at `+0x40`, its `PlayerCamera` at `+0x684`, camera `CameraCache.POV.Location` at `+0x448`. |
| Search direction | The owning controller's `Actor.Rotation` at `+0x114`, converted to a direction/matrix by `0x80A20`. It does **not** read camera-cache rotation for this ray. |

The search still owns selectable candidates, range, angular ranking and native
admission checks. The adapter changes only the temporary origin/direction it
reads. LExSDKv2's LE3 declarations independently identify these fields. The
search entry is a `.pdata` function start with a unique captured-text pattern,
registered in the existing address table; raw game code stays private.

## Implementation and proof gates

ME1 retains its existing `int(this, deltaSeconds)` hook and camera-cache rotation.
ME3 uses `void(this)` at the narrower search and the controller's reflected
rotation field. Both reuse one provider, in-flight accounting, write journal,
fault guard and `__finally` restoration. The provider checks the selected player,
live camera/controller, on-foot context and existing head-world tracking frame.
No controller rotation remains changed after the call, including exception unwind.
LE2's unverified selection-hook address remains unavailable.

The direct hook tests compile the actual source separately for ME1 and ME3.
They exercise original-call arguments/return values where applicable, normal
swap/restoration, native exception unwind, absent provider, null selection,
invalid field pointer and a faulting provider that disables itself. Both pass;
these do not simulate native target eligibility or physical headset comfort.

## Bounded native result

Protected run `20261115-190610-153-LE3-gaze-selection` passes 517/517 metadata
comparisons, including both no-argument functions and the controller rotation
field. It normally loads unchanged Save 31 and captures five valid stereo pairs.
At the same player position, forward look selects nothing; yaw -30 degrees and
pitch -30 degrees selects the War Room's `SFXPointOfInterest_0`, about 1.35 m away.
The native **War Terminal / A Use** prompt is visible. A single replayed A press
opens the actual **War Assets** screen and changes native mode from 0 to 9.
This is observed native selection and Use, not merely successful hook installation.

This also corrects the fixture investigation's earlier visual identification:
the approached holographic table is the **War Room**, not the galaxy console.
Navigate from the verified starting location before claiming map access.

The original native prompt remains visible; `labelVisible = false` correctly
reports that the shared cropped replacement has no measured default yet.
No crop acceptance is implied. Logs show selection calls using the gaze provider;
the run stops after GUI entry, so native GUI return, reload, negative-mode tests,
combat aim and physical-headset behavior remain open. The exception-restoration
tests cover the hook lifetime offline, not those gameplay scenarios.

Both development and player variants build for all three games. All 71 registered
tests pass across the full run and targeted retries: six test binaries initially
needed building, and three installer/network tests required normal Windows temp
and localhost access outside the sandbox. No product test assertion was relaxed.
SPDX, knowledge links and diff checks pass. The native run restores game/profile
files with zero differences; independent null-driver, SteamVR, chaperone and LE1
ini hashes match the restoration receipt and the game lock is released.

The [combat fixture route](le3-combat-fixtures.md) remains unaccepted until normal
travel, range entry and checkpoint reload succeed.

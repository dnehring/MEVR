<!-- SPDX-License-Identifier: MIT -->
# Shared null-headset height

2026-11-15: the owner requested **5 ft 9 in (1.7526 m)** as the default null-headset
height for ME1, ME2 and ME3. This is the tracked headset/eye height above the floor,
not a stature estimate with an additional eye-to-crown subtraction.

All three test routes call `tools/fake-headset.ps1`. Its `-HeadHeightM` defaults to
1.7526 and defines the null driver's standing floor origin below its raw head at
zero. The seated/head-relative origin remains zero. Existing null universes are
updated too; unrelated universes and the original null calibration are preserved
for restoration. `-HeadHeightM` can explicitly override this for a bounded test.
The setup/restore guard now refuses while any trilogy game is running.

SteamVR chaperone relates raw tracking to standing and seated origins; collision
bounds remain in setup-standing coordinates. See Valve's
[driver documentation](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md#chaperone).

**Do not add 1.7526 to existing head-relative replay coordinates.** Walk/look
recordings used after recenter still put the head at local Y=0 and hands at offsets
from it. STAGE supplies floor-to-head height separately. Adding the standing height
to local replay coordinates would double-apply the translation. A recording's
explicit eye-height argument describes that recording's reference space; old
recordings are not silently rewritten.

This does not change real-headset calibration or move hands relative to the head.
The `--hands view` diagnostic still puts controllers 8 cm below and 25 cm ahead of
the eyes. Use `--hands chest` when checking a normal unobstructed view.

Offline validation: `pwsh -NoProfile -File tools/qa-hubs/test-fake-resolution.ps1`
exercises the actual setup script with native calls stubbed: default/custom height,
existing universe update, repeated setup, original universe restoration, other
universe preservation, the running-game guard and existing resolution settings.
The previous test incorrectly required byte-identical JSON after key-wise restore;
that assertion failed before this change. It now checks preserved values/absence
and byte-identical restoration of the copied null-driver settings file.

Native STAGE height verification is recorded here after the protected run.

<!-- SPDX-License-Identifier: MIT -->
# M6 native acceptance using existing machinery

Source audit, 2026-11-15; these are checks to run, not new runtime evidence. Use the exclusive
[`le2-halcyon-run`](../tools/le2-halcyon-run/README.md) session and its protected save loading/restoration.
Do not launch alongside another owner. Keep recordings and snapshots in the local run evidence directory.
Use a safe on-foot fixture for movement and a live hostile fixture for targeted powers. Authored seated
scenes, including the shuttle, must not permit traversal; first-person conversation work stays deferred.

## Shared script setup

`console.ps1`, `record.ps1`, `replay.ps1 -Inputs` and `snapshot.ps1` are already bridged into ME2.
Console commands below are present in `ConsoleRequest::RunOursLe2` and `PowerWheel::Console`.
The generators write the existing recording format; they do not launch the game. Set `$evidence` to
the current run's local output folder before these examples.

```powershell
tools/console.ps1 "mevr state" "mevr savedesc" "mevr powerwheel roster"
py -3.12 tools/make-walk-recording.py "$evidence/hands.mevrrec" --plan stand:30 --hands view --grip
tools/replay.ps1 -Path "$evidence/hands.mevrrec" -Inputs
tools/snapshot.ps1 -Delay 3 -Tag m6-hands
tools/replay.ps1 -Stop
py -3.12 tools/make-wrist-recording.py "$evidence/wrist.mevrrec" 30 none 0 wrist
tools/replay.ps1 -Path "$evidence/wrist.mevrrec" -Inputs
tools/snapshot.ps1 -Delay 3 -Tag m6-wrist
tools/replay.ps1 -Stop
```

The wrist generator's zero eye height matches the fake headset; use the actual calibrated height for a
different setup. Snapshots must include both eyes and their JSON. A good crop alone does not show that
the panel attaches correctly to the weapon or wrist.

## Gun and wrist HUD

Reuse `game/HudPieces`, the native ME2 crops and native ammo-effect observation. Repeat the hands
recording on a small and a large weapon using native weapon selection. Inspect the snapshot's
`hudPieces` record: `gunVisible`, `hostTakesIt`, `gunMount`, `anchor`, `up`, `ammoPower` and `body`.
Compare both eyes for overlap with the sight line, readability and wrist attachment. Change elemental
ammo through an actually learned power, then swap weapons and reload the fixture. Require the displayed
effect and `ammoPower` to follow the game's actual state, with no stale prior-weapon label.
`make-ads-recording.py` also supplies an existing scoped-weapon pose; its documentation's ME1
`mevr weapon sniper` command is not available through ME2's current console dispatch.

## Powers

```powershell
tools/console.ps1 "mevr powerwheel roster" "mevr powerwheel open"
tools/console.ps1 "mevr powerwheel"
tools/snapshot.ps1 -Tag m6-power-wheel
```

Read the returned roster first. Use `mevr powerwheel pick n` with the visible tile's current index,
then `mevr powerwheel equip`; do not assume fixed roster indices across saves. `mevr powerwheel lock`
reports the target and last cast. `mevr powerwheel selfcast` and `mevr powerwheel push` request the
equipped power's corresponding test activation. A request acknowledgement is not success: check
`lastCast`, cast/refusal counts, native cooldown and the actual effect on the hostile or player.
`mevr powerwheel close` cancels the diagnostic wheel. Leave `aimhead` off for hand-targeting validation.
ME2's `mevr powerwheel cast` deliberately refuses and directs the caller to `selfcast`/`push`.

Console selection validates roster and native activation only. Record actual controller selection and
each weapon-dependent gesture using `tools/record.ps1 -Seconds 20`, then replay the returned file with
`-Inputs` and capture before/after snapshots. Require one activation per gesture, correct hostile lock,
no activation on cooldown, correct evolved-power identity, and no gameplay input leaking while the wheel
owns the hand. Repeat chest/self, gun/ammo and targeted-push cases across weapon types. The native ME2
effects remain authoritative; do not copy ME1 projectile creation. `power-arena.ps1` uses ME1-only fixture
and spawning commands and is not an unchanged ME2 runner.

## Comfort and traversal exclusion

```powershell
py -3.12 tools/make-walk-recording.py "$evidence/walk.mevrrec" --plan stand:2,forward:2,stand:3 --hands view --stick 0.6
tools/replay.ps1 -Path "$evidence/walk.mevrrec" -Inputs
tools/snapshot.ps1 -Count 4 -Interval 1 -Tag m6-walk
tools/replay.ps1 -Stop
```

This supplies movement and neutral release in a safe on-foot area. For the head/hand-relative comparison,
record a real left hand pointed away from the head and replay it under both existing Comfort settings,
then restore them. `make-walk-recording.py --hand-yaw` rotates the right hand only, so it does not supply
that left-hand divergence. Snapshot `comfort.walk`/`handWalk`, heading and displacement must agree.
Check `comfort.vignette.target` and both-eye edge shading during movement and return to neutral.

For snap turning, record two right-stick X flicks above 0.7, each returned below 0.3 for at least 0.25 s.
Replay with `-Inputs`: require exactly two `comfort.snaps` increments and the configured yaw change,
including the body. For smooth turning, record a sustained right-stick X hold and neutral release;
compare heading over elapsed time with configured speed and confirm it stops on release. The existing
`make-look-recording.py` turns the headset, not the right stick, so it cannot prove either turning mode.

Record opening/closing the real menu while holding each stick, then release and reapply. Verify no
movement while the menu owns input, none on close until release, then normal response. Reuse these
recordings in an authored seated scene to verify traversal stays disabled, without enabling first-person
conversation conversion. Synthetic snapshots cannot establish physical-headset comfort or stutter.

## Galaxy presentation

Follow [the galaxy candidate's native checks](../research/le2-galaxy-table.md). Its first increment is a
native image on the shared fixed table with native picking/scanning, pending acceptance. Keeping the CIC
world around the table and giving the native map stereo depth remain separate unfinished work.

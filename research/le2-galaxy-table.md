<!-- SPDX-License-Identifier: MIT -->
# ME2 native galaxy and orbital image on the shared table

2026-11-15. M6 G's first presentation increment, based on `b74989a4`.

## Source audit and implementation

ME1's `game/GalaxyMap` reads LE1 galaxy tables, its camera behavior, a native crosshair and specific Normandy
projector actors. ME2's galaxy hierarchy, ship navigation and orbital mode need their own game adapter.
The existing [galaxy research](51-galaxy-map.md) and [orbital fixture work](le2-scanning-readiness.md)
remain the native-system references for the following increments.

Halcyon's ME2 renderer already presents modes 9 (galaxy) and 10 (orbital) using the complete native game
image. This increment places that image in app space using ME1's `galaxy_holo::PlaceTable` and `DiscPose`.
The complete image preserves native labels, travel questions and scanner readouts at their authored coordinates.
`NativeTablePanel` in `core/vr/GalaxyMap` keeps one placement through the galaxy/orbit transition, leaves
the original renderer placement in use while tracking or the copied image is unavailable, and resets on
other modes, disable, session replacement or recenter.
It preserves the native image's aspect ratio. The table's width is twice `tableRadiusM`.

`MevrBridge::PlaceGalaxyPanel` runs immediately before the submitted layers are assembled, using the head
located at the same predicted display time. It changes the native quad's pose and size. The original image
and swapchain remain owned by the renderer. The feature uses no game-thread camera writes and makes no
gameplay calls. It presents a fixed table against the existing flat-mode backdrop. Keeping the actual CIC
world around the table and capturing stereo depth from the native map remain future increments.

In mode 9 the existing game pointer receives exactly that submitted quad. Its native mouse route remains
subject to foreground-window and game input handling, which need a native run. In mode 10 no pointer is
registered, leaving the native analog scanner controls available. The shared pad merge now accepts an
orbital-native-trigger flag to retain LT/RT instead of the on-foot left-trigger squad remapping. The existing
close latch requires a trigger held across galaxy-to-orbit entry to be released before reaching the scanner.
The main menu retains its existing input filter. No new probe-launch command or resource write is introduced.

## Candidate configuration

The presentation is opt-in while native acceptance is pending. In `ASI/LE2MEVR.ini` use the existing
`[Galaxy]` section with `nativeTable = 1`. Existing settings `tableDistanceM`, `tableBelowM`, `tableRadiusM`
and `worldTiltDeg` tune the placement. Defaults for this candidate are 0.70 m ahead, 0.45 m below, 0.48 m
radius and 35 degrees tilt. Settings are read at most once per second. Reopen the map or recenter after
changing position or tilt. Set `nativeTable = 0` to use the renderer's ordinary panel placement.

The native-trigger mapping applies in orbital mode regardless of table presentation, so scan/probe controls
remain available on the ordinary panel too. Test those bindings through actual controller replay.

## Validation and next native run

Offline tests extend the existing galaxy tests for galaxy/orbit continuity, missing tracking, leave/reopen,
recenter, disabled presentation and invalid aspect. Pad-merge tests cover native LT/RT despite the on-foot
squad mapping and independent trigger release across entry. These tests establish deterministic rules only.

The isolated Release `MEVR-LE2` host/mod build passed, including a final incremental rebuild after the
session-reset and image-readiness guards. Existing CTest targets `mevr-tests` and `mevr-pad-merge` passed
(2/2). Whitespace and repository SPDX checks passed. No game was launched or deployed for this increment.
The companion [native acceptance checklist](../docs/le2-m6-native-checks.md) audits the existing HUD,
comfort and power commands/recordings; its generator examples were checked offline.

Use `tools/le2-halcyon-run` under exclusive ownership, load the protected Normandy fixture through normal
save loading, enter its map normally and enable the candidate. Capture both eyes with the hands visible.
Check table placement, native labels, pointer selection and travel confirmation, then enter orbit and verify
scan motion and a single launch from the prepared one-probe fixture. Compare same-planet resource/probe
state before and after. Repeat the zero-probe refusal and save/reload discovery baseline using the existing
[Zanethu investigation](le2-galaxy-fixture-source.md). The diagnostic orbital observer requires integration
or verification against the deployed Halcyon host before relying on its outputs.

Check a trigger held while entering orbit, both-stick main menu, tracking loss, recenter, map exit and a
load transition. Run the existing restoration verification. Table presentation, successful native picking,
resource accounting, discovery persistence and headset comfort are pending native acceptance.

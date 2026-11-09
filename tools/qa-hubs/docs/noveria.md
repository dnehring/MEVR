<!-- SPDX-License-Identifier: MIT -->
# Noveria regression routes

The initial targets are Port Hanshan's arrival/security approach and its populated public interior. Both
manifests are deliberately **draft**: they contain no invented starting pose or waypoints and must refuse native
execution until those values come from an owned, protected game session.

| Scenario | Intended workload | Current evidence |
|---|---|---|
| `noveria-port-hanshan-security` | Approach through the arrival/security area, cross a normal door/streaming boundary, then turn in place | Native acquisition reached the Normandy under the Noveria map; Port Hanshan fixture and route pending |
| `noveria-port-hanshan-plaza` | Walk a populated hub interior and turn across NPCs and occluding corners | Same acquisition result; exact public interior, fixture and route pending |

## What is established

[The earlier conversation survey](../../../research/59-conversations-in-world.md) used `BIOA_ICE00` for Noveria
and observed Port Hanshan conversations. Its survey moved Shepard onto streaming volumes and fired conversation
actions. That proves availability of those areas in the installed game, but is not evidence of normal walking,
reachable doors or a comparable populated hub state. These regression routes do not reuse those shortcuts.

A read-only local inventory of the current LE1 saves on 2026-11-08 decoded the UE3 chunked-zlib containers and
verified each chunk's expanded length. It found no obvious Noveria starting fixture among the first embedded map
strings. Map strings in a save also include historical state, so this is a discovery lead rather than an
authoritative current-level parser. The private inventory script and receipt are under
`scratch/qa-hubs/noveria/`; player files and their paths are not distributed.

## Native acquisition result, 2026-11-08

A protected SteamVR null-headset session ran the bounded `disconnect` / `open BIOA_ICE00` acquisition.
It reported on-foot gameplay (mode 0, `BioVehicleBase: no`) at `(-5169.9, 14705.6, -36047.9)`,
rotation `(0, -17312, 0)`. The world-eye snapshot clearly showed the **Normandy CIC and galaxy table**.
These are rejection evidence, not Port Hanshan starting coordinates. Map and gameplay-mode checks alone
would have admitted the wrong scene.

One normal `SFXGameModeDefault.Used` interaction opened the Noveria planet screen (mode 9). Its observed HUD
offered **Zoom Out**, with no Land action shown. This describes that acquisition state; it does not establish
that ordinary travel to Noveria is impossible. The attempt stopped without teleporting, forcing streamed levels,
activating plot actions, generating a fixture save or recording clean timing. An ordinary-play Port Hanshan save
with known quest/NPC/door state remains the missing input.

The private scripts retain the exact acquisition commands, scene/HUD snapshots and recovery receipts under
`scratch/qa-hubs/noveria/`. The High workload retained One Draw; no native walking route has been validated.
Recovery restored and hash-verified 42 protected files, archived nine test changes, and verified no remaining
game/SteamVR processes, pending test-loop requests or file mismatches. The installed ASI and global runtime
were preserved.
Both manifests pass schema validation and deliberately fail execution while `status` remains `draft`.

## Offline travel-state diagnosis

**The map-open succeeded; acquiring Port Hanshan did not.** Read-only inspection of the installed packages
found that `BIOA_ICE00` deliberately contains the Normandy and has its default `PlayerStart_3` at
`(-5169.885, 14705.606, -36046)`, yaw `-17312`. This matches the observed pawn's XY and yaw, with about two
units of vertical settling. ICE-owned pawn/sequence identities and the live streaming-volume inventory independently
support a real map change. The earlier visual rejection remains correct for the requested fixture; it is not
evidence that `open` was ignored or that a Noveria unlock flag must be injected.

The normal departure flow is present in the installed packages:

- The Normandy's inner airlock door checks **conditional 1042**, annotated as the docked-state test. That is a
  concrete gate to observe if normal Use is blocked; its truth was not measured during this acquisition.
- The ship-side departure console's normal Used event requests the exit streaming state and then signals the
  exterior airlock. Those transitions belong to ordinary gameplay. The QA driver should only walk and Use;
  it should not call those sequence actions itself.
- Noveria's exterior-airlock sequence updates departure state, performs squad selection/creation, waits for
  streaming and opens the exit. A no-companions branch explicitly supports the game's debug-playthrough case;
  absent companions alone are not established as a blocker.
- `BIOA_ICE00` already wires its Loaded-and-Visible event to **transition 5543**, annotated as marking Noveria
  landed. Static wiring does not prove runtime execution, but contradicts treating a missing landing flag as a
  demonstrated cause.

The numbers above are **conditional and transition IDs, not save boolean indices or file offsets**. No required
save patch has been identified. The generated SDK also separates saved maps, player/world state and desired
start-point state in `BioSaveGame`; changing a map string is not a complete fixture. The current save inventory
only extracted embedded map strings and cannot certify any save's current plot state.

For a later owned native session, close the planet screen through ordinary Back, walk from the CIC along the
cockpit-side walkway to the inner airlock, use its door control, then use the departure control inside the
airlock. Accept the game's normal squad-selection/transition flow and inspect the resulting Port Hanshan scene.
This path is **supported by package wiring, not yet controller-validated**. The authored door/control positions
are retained privately as discovery hints; they are not admitted as collision-checked route waypoints. If the
door refuses Use, record the docked predicate and actual control state read-only before considering a fixture
change. Do not force the door, streaming state or a plot transition.

The safest reusable fixture is an ordinary save after that departure and the intended security interaction,
with on-foot control restored, the expected public interior visibly populated and the normal route accessible.
Archive its hash privately with the chosen squad, quest/door/NPC state and measured pose. A save obtained after
debug map-open must retain that provenance and cannot silently stand in for a normally progressed story save.

### Sources and reproduction

These are local primary sources, inspected on 2026-11-08. Installed game packages and full property output stay
private. The repository's [read-only package inspector](../../cockpit/package/Verbs.cs) opens the package and
lists export properties; the private wrapper hashes each input before and after inspection.

| Local source | Relevant evidence |
|---|---|
| `BIOA_ICE00.pcc`, exports 1348, 1770, 142 and 532 | Authored default start, level-loaded landing transition, Normandy/Noveria streaming relationship |
| `BIOA_NOR10_00_PLC.pcc`, `ActivateAirlock`, exports 1206, 1266, 1233 and 1766 | Docked conditional, ordinary console Used event and normal exit sequence |
| `BIOA_ICE20_13_PLC.pcc`, `LeaveNormandyViaAirlock`, exports 832, 863, 254, 264 and 265 | Exterior departure path and squad-selection fallback |
| [LExSDKv2 LE1 declarations](../../../external/LExSDKv2/Src/LESDK/LE1/SFXGame_classes.hpp), `BioSaveGame`, `BioWorldInfo.CheckConditional` and `BioSeqAct_PMBase` subclasses | Distinct save/start state and conditional/transition APIs; declarations do not establish current values |
| [Galaxy-map research](../../../research/51-galaxy-map.md), sections 2 and 8 | Ordinary UI/input travel path and map presentation; planet-screen appearance alone does not prove landing availability |

The exact commands, package hashes and focused summaries are under ignored `scratch/qa-hubs/noveria/`:
`replay-offline-investigation.ps1`, `package-*.json`, `proof-default-start.txt`, `proof-normal-departure.txt`
and `proof-exterior-arrival.txt`. This reproduction is offline and does not launch the game, modify a save,
change a plot variable or require the native lease.

## Acquire and promote

1. Obtain exclusive game/SteamVR ownership and make the shared harness's hash-verified backup of the entire
   profile, game configuration, ASI settings/binary and affected SteamVR configuration.
2. Prefer an existing Port Hanshan gameplay save. If unavailable, a bounded `disconnect` / `open BIOA_ICE00`
   may be used **only as separately recorded fixture acquisition**. It does not count as controller traversal.
   If normal plot transitions cannot reach the intended start, stop and leave the route draft.
3. Record `mevr vehicle`, `mevr loc`, the initial pose and a world-eye snapshot. Confirm on-foot gameplay and
   the intended area visually; the persistent map name alone cannot distinguish the Normandy, Port Hanshan
   or Peak 15 under this planet map.
4. Walk using replayed controller inputs and normal door Use actions. Read location for checks. Do not write
   pawn location, enable ghost movement, force streamed chunks, or fire plot/conversation actions to bypass a gate.
5. Record start pose, measured waypoints, endpoint image, quest/door/crowd prerequisites and a private save
   identity. Promote to `ready` only when those values are known, and to `validated` only after a successful
   replay with route/restore receipts. Do not bundle the save, screenshots or game-derived package data.
6. For clean timing, load the head-turn clip before capture, settle, then record 90 seconds without replacing
   replay clips or taking snapshots. Retain One Draw. Route-time replay loads belong to traversal QA and are
   excluded from the clean stationary measurement.
7. Close only the owned game session, archive generated saves and restore/hash-check the original state.
   Record an incomplete attempt as incomplete; do not substitute a teleported endpoint.

## Acceptance

Require identical fixture/build/render settings across comparisons, successful collision-respecting waypoint
checks, no draw-replay faults, and verified restoration. Report frame p99, maxima and missed-budget counts plus
engine/host tick times. A fake headset exercises this rendering route; physical VDXR/SteamVR pacing remains a
separate acceptance check.

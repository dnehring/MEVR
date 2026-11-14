<!-- SPDX-License-Identifier: MIT -->
# LE2 orbital scanning observation and fixture acquisition

This is an offline source/SDK contract and a bounded observer, not a completed
scanning fixture or native acceptance. `planet-scanning.json` remains draft.
The helper makes no engine calls, evaluates no plot conditionals, writes no game
state, and is not an action capability. The diagnostic host registers it as the
empty, read-only `observe-orbital` request; native orbital sampling remains unproven.

## Immediate acquisition sequence

1. Use the existing protected session and normal menu loader with the reviewed
   Crew Deck resource fixture; verify its immutable hash and descriptor binding.
   The privately archived normal-save candidate inspected for this work has SHA256
   `b54bf7c65437bfa6d487019bd4328cb62fb86581f25237181a0629b04940e236`,
   map `Biop_Nor`, probes 29, fuel 1499. Its eight resources were read independently
   from the save; this note does not claim a fresh native reload of that file.
2. Acquire the galaxy-map scene through ordinary player navigation: reach the CIC
   galaxy console, interact normally, select the Hourglass Nebula, Ploitari, and
   Zanethu, then enter the scanner. Capture those steps and any prerequisite that
   blocks them. No verified arbitrary teleport, map-open command, reticle setter,
   or galaxy selection dispatcher is provided here. Do not force mode 10 or write
   `m_nActiveWorld` to bypass that sequence.
3. Through the empty read-only `observe-orbital` request, collect a
   correlated observation and scene capture. Require `available`, current mode 10,
   camera hierarchy agreement with the unique orbital latent action's planet,
   stable live resources, and the expected authored feature/planet data below.
   Record initial scanning/probe/impact/rescale/exit flags before input.
4. Use normal scan and probe input for the first baseline. Record before/after
   orbital observations and capture the actual effect; wait for the ordinary
   update/impact sequence to finish. Record probe consumption, mineral changes,
   the feature's remaining scaled amount and any landing-site reveal separately.
   Fuel is a travel resource: measure it before/after navigation; do not require
   an invented fixed cost or charge it to scanning without observation.
5. Leave the map normally and use the existing reviewed New Save flow plus
   `save-evidence.py` collection. Inspect the saved resource/galaxy/plot diff with
   `le2-save.py` and review event 4551 using `story-review.py` with the scoped
   state-event report. Reload that exact new save through the normal loader;
   revisit the same planet and capture persisted probe impacts/discovery state.
6. Only then bind a private immutable scanning fixture and its expected start
   state to the scenario. A second deliberate zero-probe baseline should use a
   separately reviewed preparation recipe or normal depletion; an unavailable
   resource must never be exported as zero. Native, VR, and headset gates remain
   separate. The current offline helper does not satisfy any of them.

The acquisition dependency is concrete: the Crew Deck fixture is usable for
normal navigation, but console reachability and the actual route to Zanethu still
need a native capture. Current save booleans 4419, 5678, 5679 and 2738 were all
false. That agrees with the authored *candidate* reveal prerequisite; it does not
establish access, conditional evaluation, or mission/story validity.

## Bounded API and wire contract

`engine/Le2OrbitalObservation.hpp` declares:

```cpp
json::Value ObserveOrbital(std::uint32_t gameThreadId);
```

The implementation is included once in `Le2DiagnosticObservation.cpp` to reuse
its existing guarded reader and exact object-table/class validation. It rejects
any caller outside the host's verified game thread. The host envelope is
`payload.orbitalObservation`, produced for empty `observe-orbital`; there are no
request pointers, indices, plot IDs, or mutable parameters.

| Output | Live source and meaning |
| --- | --- |
| `context.currentMode` | Current local BioPlayerController.GameModeManager2.CurrentMode, must be 10 |
| `context.activeWorld` | Mode 9 SDK GalaxyCam field's BioCameraBehaviorGalaxy.m_nActiveWorld; not a saved planet ID |
| `context.cameraSource` | `GameModeManager2.GameModes[9].GalaxyCam`, an explicit SDK field path; native accessor equivalence is unproved |
| `planet.object`, `mapName`, `tableId`, `displayNameRef`, `levelType` | Unique current pawn.LatentActions BioSeqAct_OrbitalGame.CurrentPlanet; BioPlanet fields |
| `planet.landConditional` | BioPlanet.PlanetLandCondition declaration only; there is no assumed land-parameter field |
| `planet.features` | Complete bounded BioPlanet.Features, each typed/live/unique and owned by that planet |
| Feature `visibleConditional`, `visibleParameter`, `usableConditional`, `usableParameter`, `transition`, `transitionParameter` | SFXPlanetFeature authored IDs/parameters; not evaluated outcomes |
| Feature `scaledAmount`, `type`, `mineralType`, `tableId` | SFXPlanetFeature live fields; amount can change during normal mining |
| `state.scanning`, `probeActive`, `impactActive`, `rescaling`, `exiting` | Corresponding SFXGameModeOrbital booleans |
| `state.probeLaunchTime`, `impactTime`, `scanDist` | Corresponding finite floats; not interpreted as absolute clock times |
| `resources.{credits,medigel,eezo,iridium,palladium,platinum,probes,fuel}` | Live rooted SFXInventoryManager fields; fuel maps to CurrentFuel |

Resource entries use the existing `available`, `reason`, `sourceField`,
`scalarType`, `sampleBits`, and optional `value` contract. Each scalar is sampled
and compared twice; invalid/unreadable/changing values are independently
unavailable. The inventory is the current pawn's validated owner/instigator/world
chain, never `CurrentSaveGame`. See [resource authority](le2-resource-observation.md).

The entire orbital chain, array headers/selected identities, state/feature
scalars and map FString header/body are resampled after resource reads. A changed
context yields an unavailable report without the earlier values. The camera's
hierarchy is indexed using the inspected Activated formula: cluster `n/10000`,
system `n/100%100`, planet `n%10`. The selected BioPlanet must match CurrentPlanet.
The inspected GetGalaxyCamera wrapper calls native HACK_GetCameraMode(9), whose
body is unavailable. Mode 9's SDK GalaxyCam field is therefore a structural
candidate cross-checked against CurrentPlanet, not a proved accessor result.
Mode 10's inherited GalaxyCam is recorded for consistency without assumed equality. The planet actor's WorldInfo must agree
with the current resource world. This does not establish GWorld level containment.

Bounds: game modes 64, latent actions 64, clusters 128, systems/planets 100 each,
features 64. Invalid headers, missing/ambiguous actions, foreign/duplicate features,
wrong classes, inconsistent world/planet identities, malformed text, nonfinite
orbital state and guarded faults give explicit unavailable reasons. No truncated
feature list is marked complete. Compact output exceeding 14 KiB becomes
`orbital-payload-budget-exceeded`, leaving space under the 16 KiB wire cap.
Object identities are point-in-time diagnostics only. Flags explicitly deny
savedPlanetIdVerified, discoveryConditionalEvaluated, persistenceVerified,
durableObjectIdentity and dispatchCapability.

## Source-backed next actions, not implemented

The inspected normal exec functions on SFXGameModeOrbital are:

| Function | SDK flags | Declared input/return parameters | Normal behavior |
| --- | --- | --- | --- |
| StartScanning | `0x00020202` | none | Enters scanning for supported planet types unless rescaling/exiting |
| StopScanning | `0x00020202` | none | Leaves scanning for supported planet types |
| LaunchProbe | `0x00820202` | none | Requires scanning, idle probe/impact, no rescale/exit; native resource branch consumes one probe when available |

Commented locals in SDK parameter structs are not call parameters. A future
hardcoded dispatcher still needs live effective UFunction metadata (ParmsSize 0,
NumParms 0, no return property, stable observed void ReturnValueOffset), exact
receiver/class/slot/owned hook route validation and immediate context resampling,
with explicit opt-in and uncertain-after-entry semantics. C++ `sizeof` an empty
SDK parameter struct does not establish UFunction.ParmsSize. No dispatcher is
included here. Reticle positioning and normal map navigation remain separate
input work. Zero probes is a native refusal case to observe, not a reason to fake
successful depletion or directly invoke impact handling.

Ordinary EndImpact handles proximity, visible conditional checks, authored feature
transition, mineral award/depletion and landing reveal. ShowProbeImpactVFX calls
BioPlanet.SaveProbeImpact; SaveProbeImpact/LoadProbeImpacts are native-only in the
inspected source. Do not invoke EndImpact, GiveResource or ExecuteStateTransition
to manufacture a scenario result. Do not identify serialized galaxy planets by
ActiveWorld alone: the inspected save has 80 planet records and no ID 200100;
the mapping remains unproved.

## Provenance and checks

Source is installed SFXGame UnrealScript inspected read-only with the existing
conversation scanner, plus pinned LExSDKv2 declarations
`6bd05809cccb1a099a520371a95779d4a56bb246`. Scanner SHA256:
`918a095735f00998e5ff99871902253c47c66069a6cce3e6d6b0d474addd6f01`.
SFXGame package SHA256:
`681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77`.
Private inspection output is ignored `local-tools/scanning-source/`; no game
scripts, dumps, binaries or original game data are included in this change.
Relevant inspected functions: BioSeqAct_OrbitalGame.Activated/GetGalaxyCamera;
SFXGameModeOrbital.GetGameData/Activated/StartScanning/StopScanning/LaunchProbe/
EndImpact/GiveResource/ShowProbeImpactVFX; BioPlanet.LoadPlanetData/
SaveProbeImpact/LoadProbeImpacts; BioCameraBehaviorGalaxy.BurnFuel.

The existing [effective galaxy source report](le2-galaxy-fixture-source.md)
identifies the mounted DLC_METR_Patch01 galaxy: Zanethu/Ploitari/Hourglass,
MapName BioP_N7Crsh, planet export 143, landing feature export 2709. Its source
indices predict ActiveWorld 200100, still requiring live observation. F1418 checks
4419 false; F1237 checks 4419 true and 2738 false. Event 4551 directly sets 4419 and
5679 true, not 5678. Preserve effective package hashes/context from that report;
these are authored dependencies, not a license to edit those flags into success.

Standalone actual-SDK checks are in `tests/le2-observation`, with orbital cases
in `OrbitalObservationTests.inc`. They exercise root/planet mismatch, wrong mode,
wrong controller class, duplicate/oversized arrays, invalid pointers, nonfinite
state, context/string changes, per-resource unavailability and payload refusal.
Synthetic successful state is not evidence that a native orbital scene has been
acquired. Integration requires the parent-owned host build and runtime observation.

Offline verification for this change: the standalone actual-SDK Release and Debug
builds both passed their complete observation suite (1/1 CTest per configuration),
including the production object compiled with `/W4 /WX`. Representative one-feature
orbital output was 4,041 compact bytes; a complete 64-feature synthetic list
exceeded the budget and was explicitly refused. SPDX and whitespace checks passed.
These checks do not establish a maximum for every valid native scene or native
acceptance; the runtime output cap remains enforced.

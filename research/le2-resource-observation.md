<!-- SPDX-License-Identifier: MIT -->
# LE2 live resource observation

This slice adds `ObserveResources(gameThreadId)` for the normal local player. It
performs bounded guarded reads only: no getter/ProcessEvent calls, engine writes,
request parsing, file reads or save-cache fallback. The host owns protocol/capture
binding and any export into scenario `state.resources`. It does not establish
resource persistence, successful save completion or VR gameplay acceptance.

## Source-backed authority

The pinned SDK declares all eight values on `ASFXInventoryManager`:

| Export name | SDK member | Offset | Representation |
|---|---|---:|---|
| credits | Credits | 0x304 | signed int32 |
| medigel | Medigel | 0x308 | signed int32 |
| eezo | Eezo | 0x30c | signed int32 |
| iridium | Iridium | 0x310 | signed int32 |
| palladium | Palladium | 0x314 | signed int32 |
| platinum | Platinum | 0x318 | signed int32 |
| probes | Probes | 0x31c | signed int32 |
| fuel | CurrentFuel | 0x320 | float32 |

Installed script inspection supports their live meaning. `SFXInventoryManager.GetResource`
returns the seven integer fields for the corresponding resource types;
`AdjustResource` updates those same fields. `BioCameraBehaviorGalaxy.GetRemainingFuel`
reads the local controller pawn's inventory `CurrentFuel`, and `BurnFuel` updates
it directly. Its credits/probes getters use that same current-pawn inventory.
`BioEpicPawnBehavior.GetInventoryResource` also resolves the current player pawn's
inventory. None of these calls is needed to observe the fields.

`SFXGame.GetPlayerResourcesManager` provides a second source-backed path through
`BioWorldInfo.m_playerSquad.m_playerPawn.InvManager`. The observer checks this path
when a squad reference exists and refuses disagreement. A missing squad is an
explicit unavailable auxiliary check, not another manager-selection strategy.
The primary player route remains the validated local controller and its pawn.

`SFXSaveGame.SavePlayer` resolves the selected local player and copies these eight
inventory fields into `PlayerRecord`; `LoadPlayer` copies them back after loading
weapons. There is no clamp in that assignment block. The existence of an
`SFXEngine.CurrentSaveGame` object does not demonstrate continuous synchronization
with gameplay. `UpdateCurrentSaveGame` also participates in descriptor/profile/
career-cache maintenance. Reading that cached save object would not substitute
for observing the live inventory.

## Bounded current-object checks

The existing `Read` POD sampler supplies the same current world, engine, local
player, controller, pawn, map and position used for this response. It is invoked
directly; the resource observer never calls general `Observe`, a host provider,
a generated getter wrapper or an engine allocator.

GWorld's map and the actors' WorldInfo are **separately sampled roots**. Agreement
between controller/pawn/inventory WorldInfo pointers does not prove exact actor
WorldInfo-to-GWorld containment. The pinned UWorld declaration has no typed
PersistentLevel/WorldInfo link; this slice neither invents an offset nor imposes
an unverified universal Outer-chain rule. Both identities remain in the response
for later native evaluation. Stable map availability means only a stable sampled
GWorld package name, not that containment or loaded-fixture identity was proved.

The current pawn must be a live `SFXPawn_Player` subtype (including Soldier).
Require controller-to-local-player and pawn-to-controller reciprocals, one typed
`BioWorldInfo` for controller/pawn/inventory, and inventory Owner and Instigator
both equal to that pawn. Engine `Pawn.PostBeginPlay` creates the manager for the
pawn, and `InventoryManager.SetupFor` sets those two ownership references. A
nonnull world controller cache must agree; a present typed player squad must
have the same player pawn/world. Wrong classes, templates, pending-kill objects,
invalid indices, foreign roots or guarded read faults remain unavailable.

Each scalar has its own guarded read, performed twice. Afterwards the complete
root chain is sampled again and compared by address, object-table index and class,
including optional cache/squad links. Changed roots invalidate every resource;
a single unreadable or changed field invalidates only that field. These are
point-in-time consistency checks, not a durable object generation or GC lock.

Zero is a valid observed resource. Negative integers, negative fuel and nonfinite
fuel are unavailable with explicit reasons. No value is clamped, rounded or filled
with zero. Configured Max* fields are not universal validity limits: save loading
copies the values directly, and the meaning of later gameplay caps is separate.
Fuel retains its float32 value and representation. Map and finite position must
also agree across the root samples; moving coordinates become unavailable while
otherwise stable resources remain independently observable.

## Response contract

The result has schemaVersion 1, game LE2, validationScope
`direct-live-inventory-POD-sample`, and false dispatchCapability, saveCacheUsed,
persistenceProven and durableObjectIdentity. `context.available` reports the
stable rooted source; its identity records are diagnostic addresses, never
client handles. `context.map` has available/reason/value; `context.position` has
available/reason/x/y/z. Unavailable values and coordinates are omitted.

`resources.<name>` contains available, reason, sourceField, scalarType and a value
only when valid. The exact sourceField prefix is `SFXGame.SFXInventoryManager.`
plus the SDK member in the table. scalarType is `int32` or `float32`. When the
first read succeeded, sampleBits is an uppercase eight-digit hexadecimal bit
pattern prefixed `0x`; it remains diagnostic even when availability is false.
Available fields have reason `stable-direct-live-inventory-field`.
`allResourcesAvailable` is the conjunction, not permission to mutate or proof of
native persistence. Exporters must inspect availability and correlated host
identity, never turn unavailable diagnostic bits into normalized state values.

## Evidence and reproduction

SDK revision `6bd05809cccb1a099a520371a95779d4a56bb246`; declarations are in
[Engine classes](../external/LExSDKv2/Src/LESDK/LE2/Engine_classes.hpp) and
[SFXGame classes](../external/LExSDKv2/Src/LESDK/LE2/SFXGame_classes.hpp).
The inspected installed SFXGame package SHA-256 is
`681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77`;
Engine is `d5e80354534935c674e901a6a7a889bfffa78d04bfaf15d32885d88630c44202`.
Use the existing [scanner](../tools/conversation-scan/README.md) with `--game LE2`,
`--only SFXGame` or Engine and one `--script Class.Function` selector per output.
Its pinned default LegendaryExplorer assembly path was used; a different local
DLL directory was incompatible and is not evidence. Keep extracted source/output
private. This document contains original summaries, not extracted implementations.

Synthetic tests compile actual SDK declarations and the production translation
unit under `/W4 /WX`. Cases cover all eight mappings, zero/fractional fuel,
large int32 independent of configured caps, negative/nonfinite values, ownership/
class/cache/squad inconsistencies, changed values/roots/location and wrong thread.
A guard-page inventory leaves Credits readable while all trailing resource fields
fault, proving independent unavailability without a fabricated zero. A mutation
seam exists only in the synthetic-test build. Release and Debug each passed the standalone SDK suite, including the production
translation-unit compile target. The representative compact response was 3,645
bytes, below the existing 16-KiB host frame budget.
The subsequent [protected resource-edited load](le2-validation-evidence.md#eight-edited-resources-observed-after-normal-loading)
observed all eight prepared values in live inventory and passed the correlated
exporter's starting checks. Normal save/reload persistence remains untested.

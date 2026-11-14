<!-- SPDX-License-Identifier: MIT -->
# LE2 validation coverage and evidence contract

Status: offline tooling and three draft definitions; no native, VR or headset acceptance is established by this document. The [goal](le2-validation-goal.md), [implementation packages](le2-port-implementation-plan.md) and [reuse map](le2-port-reuse-map.md) define the product requirements. This catalog makes missing evidence explicit rather than inferring campaign support from three scenarios.

## Initial scenario catalog

| Definition | Required variants | Fixture and implementation dependencies |
|---|---|---|
| [Reload](../tools/qa-le2/scenarios/reload.json) | Empty, partial, no reserve, interruption, weapon switch, repeated insertion, tutorial | Naturally obtained combat/tutorial fixtures; observed clip/reserve and native event order; P1/P3/P4/P6. Physical reload and game-owned ammo must be integrated before VR acceptance. |
| [Dialogue interrupt](../tools/qa-le2/scenarios/dialogue-interrupt.json) | Legal Paragon and Renegade windows, missed window, early and late input, return to combat | [Veetor source and snapshot workflow](../research/le2-interrupt-acquisition.md). Needs normal pre-conversation save/loading, matched native taken and missed trials, measured timing, and VR prompt/input routing. P1/P2/P7. |
| [Planet scanning](../tools/qa-le2/scenarios/planet-scanning.json) | Zero/one probe, scan hold without launch, anomaly unlock, insufficient/exact/surplus fuel | Natural galaxy/untouched-planet fixtures, observed discovery dependencies and baseline costs; P1/P2/P7/P8. Galaxy panel, scanning, launch and context routing must be implemented. |

All committed definitions are `draft`. Their missing map, save hash and recipe are intentional. Each acquisition task identifies how to replace uncertainty with evidence. `ready` means a private binding is sufficiently specified to attempt, never that it loaded or passed. A validated fixture is a ready binding **plus** evidence of normal loading, scene/start checks, progression and restoration. Do not use a `validated: true` field.

Acquire a separate private binding and immutable fixture for each starting condition when needed. One run identifies one `caseId`; an audit of `empty` does not cover `no-reserve`. Reuse a single binding across variants only when starting checks and the recipe are genuinely identical. Keep acquired save files, captures, extracted game data and local paths outside tracked content.

Offline helpers now prepare [resource boundary batches](../tools/qa-le2/variant-usage.md)
and review [source-bound story effects](../tools/qa-le2/story-review-usage.md).
One-probe zero and insufficient balances are the same preparation, not independent outcomes.
For reload, the [native source mapping](../research/le2-ammo-fixture-source.md) distinguishes
reserve from magazine usage and identifies an early-cancel phase that can transfer ammo.
The reload catalog includes resource boundaries, full-magazine refusal and cancellation
before/after that threshold and after transfer. All still require natural fixtures and native proof.
The [observer audit](../research/le2-runtime-observer.md) identifies missing LE2 producers;
an archived recording inspection cannot satisfy runtime-state or input-trace requirements.

## Fixture promotion and safe preparation

1. Capture a naturally reached save just before the action. Record private provenance, executable version/hash, DLC, character/class, difficulty, weapon/loadout, save slot identity and scene. Hash the untouched base.
2. Inspect using `tools/le2-save.py info BASE --json`. Prove unchanged roundtrip and bounded edit diff, independently reopen in Trilogy Save Editor, and load normally in LE2. An offline parser reopening its own output is not independent format proof.
3. Write a versioned private preparation recipe. The save adapter uses `schema_version: 1`, `id`, `expected_input_sha256`, `expect` and `set`; paths such as `player.probes` refer to inspected data. Every edited path must have an expected old value. Use an explicit no-op recipe for a natural fixture needing no changes. Recipe values are chosen after inspection, never guessed from this catalog.
4. Clone the draft into private storage. Set `status` to `ready`, `fixture.baseSaveSha256`, `recipe.path`, `recipe.sha256`, actual `expectedStart.map`, and nonempty starting checks. Record package/game/DLC prerequisites concretely. Hash the final scenario. Check it with `scenario.py validate PRIVATE_SCENARIO --for-run`.
5. Prepare a private output and receipt with `tools/le2-save.py prepare BASE OUTPUT --recipe RECIPE --receipt RECEIPT`. Check output hash, recipe hash, input hash and semantic edit list against the binding before staging. The protected session tool owns staging, exclusive session ownership, backups and restoration.
6. Load the selected save through the game's normal UI; retain evidence of the selected entry, rendered scene and actual player/resource/story state. Evaluate `check-start`, then perform the scene review. A map/position match does not prove a correct load. A map-open command or teleporter cannot replace this step.
7. Exercise actual controls, inspect the outcome, save/reload where applicable and restore protected state. Retain both audit and restoration reports. Each variant restarts from an immutable base, not a save mutated by an earlier run.

The bounded recipe contract supports resources, existing ammunition/power/squad progression scalars, position/facing and plot primitives; see the [adapter's exact allowlist](../research/le2-save-adapter.md). Format checks and independent source decoding verify their encoding, not native legality or coherent story effects. Weapon/squad record creation and class reassignment are unsupported. Prefer natural states until the edited field's game behavior is established. A resource grant is setup evidence, not proof that scanning/research gameplay grants or consumes the resource correctly.

For a named story recipe, record natural before/after save diffs and inspect conversation/package conditions with LegendaryExplorer. Document all dependent journal, squad, door, streaming and saved-script state. Each plot index needs provenance and ordinary progression proof. Missing dependencies keep a recipe experimental; one completion flag is insufficient. Do not encode unverified numeric plot IDs in committed examples.

## Scenario and observation contract

The executable validator is [scenario.py](../tools/qa-le2/scenario.py). It uses the Python standard library. `load(path, for_run=False, recipe_path=None)` returns a validated definition; `validate(object, for_run=False)` returns errors. Drafts validate structurally but fail `for_run`. With `for_run`, the recipe bytes and expected base hash must match the scenario. A frozen session may pass `--recipe RUN/recipe.json` because the unmodified scenario's original relative recipe path may not resolve from the run directory.

```text
python tools/qa-le2/scenario.py validate tools/qa-le2/scenarios/reload.json
python tools/qa-le2/scenario.py validate PRIVATE_SCENARIO --for-run
python tools/qa-le2/scenario.py check-start RUN_SCENARIO START_JSON --recipe RUN_RECIPE
python tools/qa-le2/scenario.py audit RUN_SCENARIO EVIDENCE_JSON --recipe RUN_RECIPE --session RUN_DIRECTORY
python tools/qa-le2/test-scenario.py
```

`check-start` consumes an observed document with `source: "runtime-observation"` and `state: {map: ..., ...}`. Checks use dotted paths within `state`, operators `eq`, `ge` or `le`, and scalar values. It does not inspect images, validate selected-save identity or establish acceptance. The session owner must match observed data to the active run. Audit additionally requires that the `start` artifact have the full run identity.

A check may name another declared `runtime-state` slot with `artifact`.
For reload, use `start` for the map and `start-weapon` for checks such as
`weapon.identity.class`, `weapon.magazineCapacity`, `weapon.loaded` and
`weapon.effectiveReserve`. Bind both artifacts to the same run and case.
With `check-start`, pass the additional sample using
`--observation start-weapon WEAPON_JSON`. With `audit`, include both slots
in the evidence manifest. The samples retain their own request and tick.
Review the loaded scene and verify that no action changed the weapon between
observations. A per-query export check can report missing starting fields that
the combined audit subsequently checks in the additional artifact.

The evidence JSON contains:

| Field | Contract |
|---|---|
| `schemaVersion` | Integer `1`. |
| `caseId` | One of the scenario's `variants`; also present in `identity`. |
| `mode`, `headset` | `native-baseline` or `vr`; `none`, `synthetic` or `physical`. These are declarations backed by the session log and review, not hardware detection. |
| `identity` | `runId`, `caseId`, `scenarioSha256`, `baseSaveSha256`, `recipeSha256`, `preparedSaveSha256`, `candidateCommit`, `gameVersion`, `gameExeSha256`; VR also requires `loadedModSha256`. Use the **loaded** binary's hash and preserve dirty-build provenance where applicable. |
| `artifacts` | Named slots from the scenario. Each entry has `path`, `kind`, `sha256`. Paths resolve inside the evidence directory; missing/changed files or path escapes cannot support an assertion. |
| `reviews` | Assertion ID to `{reviewer, observedAtUtc, notes, verdict, artifactHashes}`. `verdict` is `supported` or `failed`; `artifactHashes` must name exactly all artifacts required by that assertion, with lowercase SHA-256 values. A boolean or unbound review is pending. |

Structured `runtime-state`, `input-trace`, `save-inspection` and `timing` artifacts must repeat the **entire exact identity object**. This prevents accidental reuse of observations from another run/build/fixture. Runtime state requires `source: "runtime-observation"`. Input traces require `inputPath: "native"` or `"vr-controller"` and a nonempty event-object list; events should record timestamps, control/action, phase and source instrumentation. Direct debug calls must not be labeled player input. The auditor cannot authenticate the producer's truthfulness; preserve raw logs/video and implementation provenance for review.

`state.resources` now has a [source-backed live inventory observer](../research/le2-resource-observation.md)
and [correlated exporter](../tools/qa-le2/export-state.md) for all eight resources.
Protected resource-edited normal loads passed their starting checks, and one
normal New Save file independently retained all eight values. A fresh normal
reload of those exact bytes also matched all eight live values and the reviewed
scene, followed by verified restoration. Earlier deferred cleanup preserved
intervening ME1/shared-profile changes through qualified recovery. Gameplay
costs and the three end-to-end scenarios remain pending.
The read-only [weapon observer](../research/le2-weapon-observation.md) now exposes
selected-weapon identity, capacity, used/derived loaded count and stored reserve
through `observe-weapon`. Protected runs `1722Z-loadout` and `1749Z-neutral-focus`
sampled the selected MassCannon while in squad UI: capacity 1, used 0, derived
loaded 1 and raw stored reserve 10. The existing exporter now normalizes
`state.weapon.loaded` and `state.weapon.reserveStored` with request correlation.
These are partial observations, not reload or complete starting-state acceptance.
Effective `state.weapon.reserve` remains unavailable because virtual overrides
can differ from the stored field; audited source inheritance alone is not a
runtime getter observation. Save inspection cannot be relabeled runtime
observation. Manual observations need source captures, timestamps and reviewer
provenance; ambiguous values remain unresolved.

Machine assertions compare observed values, numeric deltas or sum conservation. They do not accept a producer's `passed` boolean. Review assertions bind an attributed observation to exact input/state/capture artifacts. Native baseline behavior, actual controller routing, stereo presentation and comfort require different evidence. Reload measurement windows must exclude firing/pickups and compare the same weapon; firing and switching have their own reviewed sequences. Use a separate record for post-load persistence.

The `settings` artifact records graphics, control bindings, difficulty, calibrated seated/standing profile, headset/runtime and hardware used. The `baseline` artifact identifies the equivalent native run and evidence hash, plus intended gameplay differences. `restoration` is the protected-session receipt; `session-log` records load, setup operations, candidate provenance, failures and exit. Both-eye images and a continuous scene/action video supplement state evidence. Do not substitute a desktop mirror for both eye captures.

Audits return `supported`, `pending` or `failed` per declared assertion and per gate. Missing coverage is `not-covered`. A missing/wrong starting observation keeps all assertions pending; native input cannot support VR routing, and synthetic headset mode cannot support physical-headset acceptance. Numeric comparisons reject booleans as numbers. The required CLI `--session` binds run/scenario/prepared-save/executable/recipe identity to the protected context and checks frozen artifacts and the preparation receipt. Exit `0` requires all declared assertions supported, recorded process binding and verified restoration; `2` means pending/failed assertions or lifecycle gates, and `1` means an invalid document/binding. A successful native baseline normally still exits `2` for pending VR/headset assertions: inspect `gates.native`. The Python audit API allows omitted session context for isolated validator tests and reports `sessionBinding.status: not-supplied`; that is not native acceptance. `fullPortAccepted` is always false: a scenario audit cannot establish campaign-wide acceptance.

## Coverage matrix beyond the initial slice

Every row starts **unproven**. Assign an owner and private fixture/result reference as work begins; no row inherits acceptance from another game, class, DLC or synthetic run. `P0` interface ownership and `P9` evidence/restoration apply throughout.

| Area | Minimum additional cases and observations | Packages / existing system to inspect |
|---|---|---|
| Save foundation | Representative real saves; unchanged byte roundtrip; constrained diff; invalid format/CRC/recipe refusal; TSE reopen; normal load and save/reload persistence; recovery after interrupted run | P9; Trilogy Save Editor LE2 format, current save adapter and protected session tool |
| Startup and lifecycle | Correct executable and loaded mod; one XR session; reflection validation; normal load/reload, death/retry, transition, pause/exit; tracking loss and safe failure | P1/P2; reflection validation, Halcyon bridge, existing launcher and QA lease |
| Stereo, body and hands | Both-eye scale/depth/UI; head/body independence; seated and standing; manual calibration; hand/weapon pose and one/two-hand transitions; lost tracking | P2/P3; ArmIk, HandAim, SupportGrip, calibration, renderer host |
| Movement and cover | Walk/turn/sprint/use/vault; mandatory traversal and doors; duck/peek from seated and standing; collision/view agreement; front/flank/above damage and difficulty variations | P5; Posture, Sprint, Comfort, VrCover plus native traversal and damage rules |
| Weapon handling | Every weapon family; muzzle-hit agreement; thermal clips, no/insufficient/exact/surplus reserves; interrupted reload at each phase; holster conflict; support release; drops/weapon switch; ammo pickup; forced loadout and tutorial | P3/P4/P6; native ammo/reload authority, shared hand arbitration, existing weapon/holster systems; licensed external concepts per reuse map |
| Powers and class mechanics | Legal/invalid targets, cooldown and resource preservation; menus/gesture selection; context cancellation; forced movement/camera cases | P6/P7; native power execution and existing power selection interfaces |
| Squad and combat contexts | Recruit/unavailable/dead squad members; commands, targeting and powers; class-specific loadout restrictions; melee and combat-to-dialogue transitions | P6/P7; native squad/loadout state and input ownership |
| Dialogue and cinematics | Paragon/Renegade interrupts; missed/early/held input; dialogue wheel; subtitle readability; return from cinematic with no fire/grip leakage; alternative plot branches | P7; conversation/cinema adapters and verified native conditions |
| Hacking and bypass | Success, failure/time expiration, cancellation, repeat attempt and return; pointer precision and both-eye readability | P7; native minigame and world UI panel/cursor |
| Research, stores and loadout | Zero/insufficient/exact/surplus credits/minerals; purchase/upgrade once; locked/unlocked research; weapons and squad selection; cancellation and persistence | P6/P7; native inventory/research costs; save recipe scalar resources |
| Galaxy and progression | Navigate/travel; zero/insufficient/exact/surplus fuel; probe purchase/capacity/zero/one/surplus; scan-hold; multiple mineral types; already-depleted/discovered planet; anomaly and normal mission entry | P7/P8; native galaxy/scanning/economy/discovery state |
| Hammerhead | Possession, movement/jump/fire, retrieval, damage/death/retry, scripted transitions and exit; both-eye HUD; motion comfort | P8; native vehicle controls, transferable Mako primitives. Physical cockpit remains a separate later milestone. |
| Scripted/forced sequences | Forced equipment, camera/input override, mounted/turret-like sequences where encountered, cinematics, transitions, disabled controls, squad restrictions and recovery | P1/P5/P6/P7; native sequence state, no bypass via map-open |
| Story recipes | Coherent journal/squad/streaming/door/script state; legal prerequisites, alternate branch, ordinary next mission and save/reload; reject mismatched base or preconditions | P1/P7/P9; natural paired saves, TSE plot databases, LegendaryExplorer condition/conversation inspection |
| Performance | Matched fixture/settings/hardware; cold load separately from steady state; repeatable combat/UI/galaxy/vehicle workloads; CPU/GPU/XR pacing, worst-frame distribution and stereo captures; no setup calls in measurement window | P2/P9; existing capture/profiling infrastructure; physical runtime and transport results separate from synthetic mode |
| Delivery and LE1 regression | Per-game install/repair/update/uninstall; clean shutdown and restoration; SDK-free release checks; LE1 calibration/combat/UI/vehicle smoke checks after shared changes | P0/P9; existing build, launcher, package and QA systems |

## Class, weapon and campaign expansion

Run baseline and VR coverage for all six LE2 classes. Record actual unlocked powers and equipped weapons instead of assuming a class has access to a loadout.

| Class | Additional high-risk observations |
|---|---|
| Soldier | Adrenaline Rush timing/camera and permitted weapons; input and reload behavior through effect exit |
| Adept | Biotic targeting, cooldowns and effect readability in both eyes |
| Engineer | Tech targeting and Combat Drone behavior; squad/control context |
| Vanguard | Charge target legality, movement/camera/collision transition and recovery to aiming/cover |
| Sentinel | Tech Armor activation, destruction/effects and subsequent power/reload state |
| Infiltrator | Tactical Cloak activation/exit and scope/aim readability with held/supported weapon |

Weapon rows must include heavy pistols, submachine guns, assault rifles, shotguns, sniper rifles and heavy weapons. Do not infer identical reload rules for heavy or unusual weapons. Inventory each supported model's native ammo rules, grip/sight geometry, sustained/charged fire where present, tutorial/forced-use dependencies and pose. Record explicit exceptions.

Campaign coverage is a separate completion ledger: ordinary critical-path progression, recruitment/loyalty/side missions, hubs, alternate morality/plot outcomes, import-dependent state, death/retry and final-mission branches. Include the installed DLC set explicitly: Zaeed, Kasumi, Firewalker, Overlord, Lair of the Shadow Broker, Arrival and Normandy Crash Site where present, plus any other included content/bonus equipment found in the installation. Track Genesis/import choices separately from playable mission completion. Installation inventory determines applicability; the list is not a claim that every package is present or tested.

For each campaign/DLC entry record fixture provenance, class/loadout, prerequisites, normal entry and exit, special mechanics, candidate/build, native result, VR result, both-eye result, seated/standing physical review, performance result and unresolved exceptions. Never promote a shared mechanic's unit pass into a mission-completion claim.

## Next smallest experiments

1. Independently reopen and normally load representative real save roundtrips, then perform a single constrained resource edit and normal save/reload. Retain hashes and restoration evidence.
2. Acquire natural pre-reload, interrupt and scan fixtures. Turn one private variant per scenario into a ready binding with actual starting checks; leave unacquired variants draft.
3. Implement or instrument verified native state/input observations and retain raw capture provenance. Prove the flat-game baseline and ordinary progression first.
4. Once each port feature is available, repeat with actual VR controller input and the same binding. Review both eyes; schedule physical seated/standing runs separately. Keep unavailable dependencies pending and continue independent offline coverage work.

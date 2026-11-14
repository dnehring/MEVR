<!-- SPDX-License-Identifier: MIT -->
# Mass Effect 2 VR validation tooling goal

Status: specification only; implementation and runtime acceptance remain pending.

Build and prove a repeatable validation system for the Mass Effect 2 Legendary Edition VR port. Agents must be able to quickly prepare, load, exercise, inspect, and reset specific gameplay scenarios without replaying hours of the campaign. Extend the existing ME1 testing machinery and use established Mass Effect modding tools wherever practical.

## Required context

Read these before implementation:

- [Repository instructions](../CLAUDE.md).
- [ME2 implementation plan](le2-port-implementation-plan.md).
- [Existing systems and reuse map](le2-port-reuse-map.md).
- [Trilogy feasibility assessment](le2-le3-port-feasibility.md).
- [LE1 save tool](../tools/le1-save.py) and [save dictionary](../tools/save-dictionary.py).
- [QA harness](../tools/qa-hubs/README.md) and [save fixture implementation](../tools/qa-hubs/save_fixture.py).
- [Conversation replay from saves](../tools/conversation-from-save.ps1).
- [Save overwrite failure history](knowledge/gotchas/test-runs-overwrite-saves.md).

Inspect current source, branch, working-tree changes and native-session ownership. Preserve unrelated work. Existing LE1 functionality is a starting point, not evidence that the same code works for LE2.

## Save editing foundation

Inspect and pin [Trilogy Save Editor](https://github.com/KarlitosVII/trilogy-save-editor), including its Legendary Edition ME2 implementation and plot databases. Use [LegendaryExplorerCore](https://github.com/ME3Tweaks/LegendaryExplorer) for package, conversation, condition and script inspection where appropriate.

Prefer a small automation adapter over duplicating existing functionality. Evaluate direct library reuse, a bounded command-line tool based on upstream, and an independent format adapter. Record the selected approach, dependencies, license obligations and attribution. Do not assume the existing editor provides a batch-editing API. Our LE1 tool is an original Python implementation based on upstream format knowledge; that does not require repeating the same implementation choice for LE2.

Support inspecting and deliberately changing:

- Credits, medi-gel, minerals, probes and fuel.
- Weapons, ammunition, powers and squad configuration where verified.
- Player location and facing where valid for the scenario.
- Named, validated story-state recipes.

Before trusting edited saves, prove unchanged round trips, constrained edit diffs, independent reopening in Trilogy Save Editor, successful ME2 loading, and in-game save/reload persistence. Preserve untouched data and reject unsupported formats or invalid edits. Synthetic tests alone are insufficient; include representative real LE2 saves privately.

## Reusable scenario fixtures

Each fixture must include an immutable private base save and hash, game/build/DLC prerequisites, a versioned preparation recipe, expected starting values, a load procedure, input actions, assertions, captures and reset/restoration steps.

Verify the actual rendered scene, player state and relevant story/resource state after loading. A reported map name or player position alone does not establish that the intended scenario loaded. Verify selected-save identity and normal load behavior before promoting a fixture from draft to validated.

Keep saves, extracted game data, recordings and machine-specific paths private. Commit scenario definitions, schemas, tools and original research summaries. Reference private artifacts by identifiers and hashes rather than embedding them.

## Coherent story manipulation

Prefer naturally obtained saves immediately before relevant beats. Compare saves before and after normal completion, and inspect native conditions with Legendary Explorer to identify dependencies among plot variables, journal entries, squad availability, streaming state, doors and saved script state.

Expose named recipes with explicit preconditions and expected outcomes. Do not assume one mission-complete flag recreates a coherent state. Validate each recipe through ordinary loading and progression. Preserve provenance for every flag or field used; unverified interpretations remain marked for investigation.

## Debugging and acceptance paths

Provide bounded development controls for fast setup where useful, using verified native game-thread operations. Record changes and reconcile them with saved state.

Acceptance tests must exercise actual VR controls. Directly invoking reload completion, granting a resource or triggering a conversation is setup or diagnostic evidence, not proof of the player interaction. Map-open and teleport tools are useful for inspection but cannot substitute for normal progression checks.

Do not use unlimited resources as the only test configuration. Include zero, insufficient, exact and surplus resources where those distinctions affect behavior.

## Three initial end-to-end scenarios

| Scenario | Preparation and actions | Required evidence |
|---|---|---|
| Reload encounter | Valid combat/tutorial save; empty and partially loaded weapons; controlled reserves; physical reload through VR controls | Correct ammo accounting and firing; no duplicate commits; interruption and weapon-switch recovery; relevant tutorial progression; save/reload persistence where applicable |
| Dialogue interrupt | Valid save immediately before a timed interrupt; exercise the controller response and a missed-window case | Prompt visible; input accepted only in the legal window; correct selected outcome and persistence; no combat-input leakage |
| Planet scanning | Controlled fuel, probes and minerals; valid untouched/discovery state; scan and launch through VR controls | Correct resource costs; deliberate single probe launch; discovery or mission unlock; correct save/reload state; zero-probe behavior |

If a required VR feature is unavailable, validate fixture preparation and native baseline behavior, record the exact implementation dependency, and leave VR acceptance pending. Continue independent tooling work. Do not replace missing VR evidence with simulated success or expand this tooling goal into an unbounded implementation of the entire port.

## Coverage and evidence

Expand the scenario matrix to movement/cover, powers, squad commands, hacking, research, Hammerhead, forced equipment, scripted sequences, all classes, weapon families and included DLC. Map each scenario to the implementation plan's work packages.

Report these gates separately:

1. Automated logic and save-format checks.
2. Native game behavior and progression.
3. Both-eye visual correctness.
4. Physical-headset comfort and usability, including seated and standing play.
5. Performance under repeatable workloads.
6. Campaign and DLC completion coverage.

Where appropriate, compare equivalent flat-game and VR scenarios. Record intended gameplay differences rather than treating every deviation as a defect. Use matching fixtures, settings and hardware for performance comparisons; keep setup and diagnostic activity outside measurement windows. Synthetic-headset results cannot establish physical comfort, real-runtime pacing or streaming transport quality.

Each result identifies the candidate commit, loaded mod hash, game build, fixture hash, preparation recipe, input sequence, settings, assertions, logs/captures, failures and restoration outcome. A screenshot alone does not establish correct ammo or plot state; a state assertion alone does not establish correct rendering.

## Execution boundaries

- Coordinate exclusive native-session ownership before launching or deploying. A clear process list alone does not establish ownership. Do not interrupt an occupied session.
- Back up saves, profiles, configuration and temporary binaries. Verify restoration, including files newly created during tests. Retain recovery information after interruption.
- Do not change the system OpenXR runtime as part of ordinary scenario testing.
- Parallelize independent offline tasks in isolated worktrees with explicit ownership. Serialize native sessions and shared-interface integration.
- Run focused tests and required repository checks appropriate to each change. Keep offline, native and physical-headset evidence distinct.
- Commit scoped, validated work to main; do not push unless requested. Preserve unrelated changes.

## Deliverables and completion

Deliver working tools, meaningful tests, agent-facing usage documentation, the three initial fixture definitions and available proof results, an expanded coverage matrix, and explicit remaining dependencies. Include a single repeatable prepare/load/verify/run/reset workflow or explain any step that still needs an operator.

Tooling success means another agent can reproduce preparation, normal loading, starting-state verification and reset for the initial scenarios with auditable evidence. End-to-end scenario acceptance additionally requires exercising the real VR interactions and observing the specified outcomes. If those depend on unfinished port features or physical-headset review, report tooling readiness and leave those gates open.

Neither harness completion nor three passing scenarios establishes full ME2 port acceptance. Campaign, DLC and hardware coverage remain explicit requirements in the broader implementation plan.

<!-- SPDX-License-Identifier: MIT -->
# Goal: prove the ME2 VR port and prepare ME3

Extend the Legendary Edition ME1 VR mod to ME2 first. Assess feasibility, implement a representative ME2 proof, and define independent agent work packages. Map ME3 differences; ME2 success is not ME3 acceptance.

Read `CLAUDE.md` and:
- [Feasibility and full feature inventory](le2-le3-port-feasibility.md).
- [Implementation packages, dependencies and gates](le2-port-implementation-plan.md).
- [Existing systems, native mechanics and source reuse](le2-port-reuse-map.md).
- [Detailed validation tooling goal](le2-validation-goal.md).

Inspect current code before assigning work. Update these documents with discoveries; distinguish confirmed, inferred, blocked and untested findings.

Map every porting area: engine access; stereo/camera; controller input; hands/body/calibration; aiming and weapon handling; physical unloading/reloading; holsters; movement, sprint, traversal and cover; powers, melee and squad commands; dialogue and timed interrupts; menus, HUD and minigames; inventory/research; galaxy travel, planet scanning, mining and discovery; vehicles; scripted sequences; saves, DLC, deployment and performance. Record ME1 reuse, ME2 changes, ME3 differences, dependencies and acceptance evidence for each.

Prefer existing MEVR code and native game mechanics. Implement Alyx-style thermal-clip handling while retaining native ammunition, reload events, tutorials and progression. Preserve native resource costs, inventory, powers and story rules. For external reuse, identify exact repository, revision, files, license, dependencies and adaptation boundary. Verify the identity and license of any downloaded Titanfall VR material before considering code reuse; follow the reuse map's distinctions between behavioral references and reusable source. Evaluate its licensed alternatives, including Overlord. Preserve attribution and required notices.

Build reusable ME2 validation tooling from the ME1 save/editor, loading and QA machinery. Inspect Trilogy Save Editor and Legendary Explorer before writing replacements; document the actual integration choice. Support verified resource, ammunition/loadout and coherent story-state recipes. Reject unsupported edits, preserve untouched fields, prove unchanged round trips and narrow diffs, independently reopen edited saves, and verify native load/save/reload persistence. Do not assume isolated plot flags recreate a playable mission state.

Provide one documented prepare/load/verify/run/reset workflow. Use immutable private base saves, hashes, versioned recipes, prerequisites, expected state, actions, assertions and recovery instructions. Verify the loaded scene and selected save. Protect the full affected save/profile/configuration state; verify restoration after normal and interrupted runs. Keep saves, game data and recordings private.

First prove three repeatable scenarios: physical reload in combat, timed dialogue interrupt including a missed window, and planet scanning/probe launch/discovery including zero probes. Test relevant empty, insufficient, exact and surplus resources. Setup shortcuts cannot establish VR interaction success: acceptance must exercise real controller actions and native outcomes.

Use isolated worktrees, explicit owners and shared interface contracts for parallel work. Serialize native sessions and integration; preserve unrelated changes and ME1 behavior. Record commit/build/mod/fixture hashes, input sequence, settings, captures, assertions and restoration. Separate automated, native, both-eye visual, physical-headset, performance and campaign/DLC gates. Missing VR features remain dependencies, never simulated passes.

Commit scoped work to main without pushing. Deliver updated plans, coverage, tested tools, proof and remaining tasks. Require reproducible ME2 evidence; three scenarios do not establish campaign or ME3 readiness.

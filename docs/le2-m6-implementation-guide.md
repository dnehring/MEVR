<!-- SPDX-License-Identifier: MIT -->
# M6 implementation guide — reuse ME1, adapt ME2

Source audit dated 2026-11-15, against local `main` at `87ebe0f4`. This is the detailed companion to
[M6 in the continuation plan](le2-continuation-plan.md#m6-the-rest-of-me1-one-module-at-a-time-l).
It describes remaining implementation and acceptance work, not additional completed features.
The Hammerhead is excluded from this pass at the owner's request.

**Current scope, 2026-11-15.** The owner authorized parallel implementation of ME2 parity and deferred package F,
first-person conversations in the world. Thespian reply selection may proceed using the existing cinema presentation.
For a future world-conversation port, authored seated scenes such as shuttle rides must disable player traversal.
The seated actor stays under the native scene's control. Head tracking and dialogue/interrupt controls must remain
available. Determine seated scene state explicitly before allowing movement and restore normal movement only when
the native scene releases the player. Physical seated play in ordinary gameplay is a separate setting.

### Active parallel assignments

| Owner | Scope | Worktree/branch | Native testing |
| --- | --- | --- | --- |
| `me2_squad_parity` | B and C, squad inventory/menu and native orders | `.claude/worktrees/me2-m6-squad`, `work/me2-m6-squad` | Coordinator schedules |
| `me2_interaction_parity` | A, selection adapter and floating labels | `.claude/worktrees/me2-interaction-parity`, `work/me2-interaction-parity` | Coordinator schedules |
| `me2_galaxy_parity` | G, native galaxy/table and scanning | Agent-owned isolated worktree from `b74989a4` | Coordinator schedules |
| Coordinator | D/E voice and Thespian dependencies, shared integration, regression and native validation | Existing integration worktree | Exclusive protected runs |

These assignments indicate active work, not completed implementation. Agents commit their own scoped candidates.
The coordinator reviews and integrates shared CMake, host services and renderer edits before landing on main.
Record resulting commits and acceptance evidence in the continuation plan as each package is verified.

## Scope and evidence

M6 has nine areas excluding the Hammerhead. Five have gameplay implementations on main, with uneven runtime
coverage. Four still need substantial implementation. Those four split into the seven packages below.
The earlier **5/9** figure counts areas with gameplay code; it is neither a completion estimate by effort nor
a claim that five areas passed final acceptance. Interaction target observation is useful partial work within
one of the remaining four areas.

| Package | Remaining area | Implement after | First deliverable |
| --- | --- | --- | --- |
| A | Interaction targeting and floating labels | Existing HUD and tracking seams | Correct native use target, then one localized prompt at that target |
| B | Squad weapons | Existing ME2 squad roster | Native inventory and confirmed equip through ME1's left-trigger menu |
| C | Squad attack and power orders | Roster; share B's recipient identity | One recipient attacks or casts on a legal target through native commands |
| D | Squad voice | C; B for spoken weapon requests | Existing radio gesture and recognizer dispatch the same verified commands |
| E | Thespian mode | Shared voice service from D; can audit replies earlier | Native reply cards and safe selection, then spoken selection |
| F (deferred) | First-person conversations in the world | Explicit seated-scene traversal policy and native observation | Later pass, with traversal disabled during authored seated scenes |
| G | Galaxy map and planet scanning | Existing wrist pointer/render services; separate Normandy fixture | Native map navigation on a table, then native scanning and discovery |

B, C and D are one continuation-plan row; E and F are another. A and G each have their own row.
Work can be prepared independently, but native runs are serialized. Suggested landing order is A, B, C, D,
E, G, with G's source/fixture investigation and E's read-only dialogue adapter prepared earlier when useful. F is deferred.
Finish a small observable behavior before widening it to the rest of a package.

## Common implementation rules

The existing architecture is the starting point. `src/core/vr` holds reusable interaction rules and published
views, `src/game` reads game state and dispatches native actions, and `src/plugin` owns the renderer, input
services and presentation. Keep the ME2 integration in those modules or small per-game adapters. Avoid a
second menu system, recognizer, launcher, recording format or save editor.

1. Audit the ME1 implementation, its latest research entries and current tests before changing a module.
   Older research headings can predate later implementations or deliberate reversions. Separate the shared
   algorithm from LE1 fields, enum values, UI coordinates, assets and executable addresses.
2. Verify each ME2 field/function and parameter layout through the existing SDK, reflection checks and,
   where necessary, narrowly scoped inspection of the original game with LegendaryExplorerCore or Ghidra.
   SDK presence establishes a candidate API, not its correct runtime call order. Mark unresolved calls
   **[verify]** until observed. Never transplant an LE1 native function pointer or address into LE2.
3. Use `ue3::FieldRef`, `FunctionRef`, `Params` and `runtime::Call` on the game thread. Add required names
   to `src/engine/ReflectCheck.cpp`. Audit `runtime::Ready()` gates carefully: some LE1 modules use readiness
   that is not the LE2 reflection-availability contract. Use the existing LE2 adapter pattern, including
   `runtime::Up()` where appropriate, rather than globally changing readiness semantics.
4. Extend the existing `src/mod/ModApi.hpp` services only if the existing view cannot represent the feature.
   `src/plugin/ModLoader.cpp::BuildServices` still keeps voice, interaction labels, squad weapon panels,
   Thespian, world-conversation publication and the galaxy-map services in LE1-specific wiring. Move each
   service only with its linked renderer, state reset and input integration. A non-null callback alone is
   not a port. Check CMake's LE2 host sources, `src/mod/Mod.cpp` lifecycle, hot reload and shutdown together.
5. Published host views contain copied data, not game objects that a render/audio thread dereferences.
   Revalidate actors, targets and requests after load, death, squad change and conversation transitions.
   Journal temporary engine writes, restore them on every exit, and clear pending requests on world change.
6. Preserve the existing input policy. The main-menu chord wins; an active panel owns its inputs; release
   latches prevent a closing menu from firing/casting. The left power wheel owns its hand while open.
   Dialogue LT/RT remain native Paragon/Renegade interrupts. Each new owner must release control on loss
   of tracking, invalid state, menu closure and mod unload.
7. Retain the ME2 amber/orange menu palette through existing theme parameters. Reuse the wrist rail and
   Omni-tool projection. Grey unavailable rows until their implementation can actually be shown.

## A. Interaction targeting and floating labels

**Reuse.** [ME1 interaction design](../research/40-interaction.md), `src/game/Interact.cpp`,
`src/engine/SelectionGaze.cpp`, `src/core/vr/InteractLabel.*`, and the label crop/submission code in
`src/plugin/HalcyonOverlays.cpp`. `HudPanel`, `HudPieceLayers` and `Swapchains` provide existing capture
and composition machinery. The ME2 adapter already observes `BioPlayerController.m_oPlayerSelection`,
`BioPlayerSelection.m_oCurrentSelectionTarget` and the combat-target flag and reports `gazeHooked`.

**ME2 differences.** LE1's selection detour is `int(this, float DeltaTime)`. The ME2 SDK declares
`void UpdateSelection()` and a separate `FindCurrentSelectionTarget()`. Its selection RVA is not supplied.
The existing detour therefore cannot be enabled by filling in an address alone. ME2 also has different
HUD layout and an existing selection flare, so LE1's crop must not be assumed to fit.

**Implementation sequence.**

1. Capture the native selected actor and unobstructed door/NPC prompts before changing selection. Record
   target identity, mode, head/body heading and the actor actually used by A.
2. Trace the ME2 selection body and its camera reads. **[verify]** Find the smallest native entry that
   covers the real selection updates. Implement an accurately typed LE2 detour only if needed. Reuse the
   provider and scoped camera-POV swap concept, including exception-safe restoration; do not alter the
   camera outside selection or enable head-based weapon aim.
3. Prove native selection follows the head and still respects range and occlusion. Publish the target via
   the existing `Interact` model; retain the native action dispatch.
4. Measure ME2's prompt rectangle at the actual captured HUD resolution. Reuse its rendered localized
   text, action glyph and availability rather than creating a separate string lookup. Add per-game crop
   defaults/settings and the shared `setInteractLabel` service and LE2 composition path.
5. Suppress the source prompt only after its replacement is ready for the same frame. On missing capture,
   invalid target or unsupported mode, show the native presentation and clear stale label state. Keep
   squad/power wheels and dialogue from displaying a second overlapping prompt.

**Acceptance.** Put two usable objects in different head/body directions and confirm the label and A agree.
Check NPC, door, inaccessible/occluded object, out-of-range object, no target and combat target. Change
language or use a long label if available. Inspect both eyes for crop/scale/clipping and verify cancellation
on load and menu entry. Log selection identity and hook activity alongside screenshots; a floating label
alone does not establish correct selection.

## B. Squad weapons through the existing menu

**Reuse.** `src/game/SquadWeapons.cpp` already supplies ME2's party via `BioWorldInfo.m_playerSquad`,
`BioBaseSquad.GetSquadSize/GetMember`, and living henchman entries. Reuse `src/core/vr/SquadMenu.*`,
`src/plugin/SquadPanel.*`, `SquadPanelDraw.*`, existing haptics, pointer preselection and close latches.
See [the weapon-menu design](squad-weapons-design.md) and
[the later squad research](../research/54-voice-squad-commands.md).

**Preserve the approved interaction.** ME1's left-trigger weapon menu is the baseline. The weapon row
inside the right-B placement wheel was built and deliberately reverted at the owner's request. Do not
restore it as presumed parity. Right B remains recipient selection and placement.

**ME2 differences.** `SquadMenu` encodes four LE1 quick-slot weapon types and fixed directions. LE2's
native inventory and weapon groups differ, including SMGs; its squadmates have restricted loadouts.
LE1's `BioEquipment.m_QuickSlotArray` and out-of-combat `ChangeToQuickSlot` route do not transfer.

**Implementation sequence.**

1. Read each live recipient's carried and equipped weapons through verified ME2 inventory fields. Audit
   `SFXSFHandler_PowerWheel` and the native inventory it consumes so the mod reflects actual available
   choices. Do not require opening or mutating the game's power wheel merely to enumerate weapons.
2. Introduce an explicit mapping between UI choices, weapon class/identity and ME2 native weapon group.
   Keep LE1's four-direction behavior unchanged. Prefer a small per-member list of carried weapons for
   LE2, using the existing panel placement and selection rules. Support an unexpected count without
   truncation or silently mapping an SMG to a pistol. Validate ordering and labels before changing layout.
3. Adapt `GiveWeapon` to the SDK candidate `BioPlayerController.SquadOrderEquipWeapon(nWeapon, pPawn)`.
   **[verify]** Confirm what `nWeapon` means and whether/when the native wheel applies tactical orders.
   Confirm both combat and holstered dispatch instead of copying LE1's fallback writes.
4. Publish the existing squad menu/wheel services in the LE2 host and include their draw modules. Connect
   the existing pause/ownership semantics to verified LE2 wheel entry/exit behavior. Preserve the player's
   prior holstered state where the native route draws it temporarily.
5. Record requested, refused, dispatched and observed-equipped outcomes separately. Re-read the actual
   weapon after dispatch with a bounded wait; cancel if the recipient changes or dies. Keep native
   inventory and ammo-power state authoritative.

**Acceptance.** With Miranda and Jacob, select every actually carried weapon and observe the intended
recipient's equipped class/mesh. Test unavailable choice, cancel, dead/absent recipient, combat versus
holstered state, load during a request, and a trigger held across dialogue entry. Player powers, right-B
placement and native dialogue interrupts must remain usable. Extend existing squad-menu tests for the
ME2 mapping/count boundary while retaining LE1 layout tests.

## C. Squad attack, power and standing orders

**Reuse.** `src/game/SquadOrders.cpp`, `SquadContext.cpp`, `SquadAck.cpp`, and
`src/core/vr/SquadGesture.*`, `SquadContext.*`, `SquadStanding.*`, `SquadRetreat.*` provide gesture
resolution, target groups, pose history, acknowledgements and order lifecycle. ME2's existing
`SquadPlace` adapter already dispatches hold/follow. Reuse `game/PowerWheel.cpp`'s ME2 power-reading
work for each squadmate rather than creating another native power classifier.

**ME2 differences.** The existing context uses LE1 class IDs, `BioEpicPawnBehavior.m_powers`, health
helpers and AI calls. A shared parser is only useful once its scene has accurate ME2 recipients, hostility,
power availability, names and cooldowns. LE1 standing-order reassertion could otherwise repeatedly
interrupt ME2 AI or queue the same power.

**Implementation sequence.**

1. Adapt the scene reader to the existing ME2 party identity and native hostility/alive checks. Read each
   member's `SFXPowerManager.Powers` with the eligibility/evolution/cooldown rules already used for
   player powers. Share a narrow read helper if useful; do not conflate player and henchman dispatch.
2. Trace native wheel/pad orders. SDK candidates include `HenchmanOrderAttackTarget(oHenchman,
   oTargetActor)`, `SquadOrderUsePower(nmPower, pPawn, nQueue, bShowIndicator, pDesiredTarget)` and
   `ApplyTacticalOrders()`. **[verify]** Establish queue values, target eligibility, dispatch timing and
   acknowledgements from a real native action before enabling gesture-driven orders.
3. Route console diagnostics, gestures and later voice through one validated dispatcher. Every request
   includes recipient identity, target/power identity and world generation. Refuse stale or unavailable
   commands with a reason. Use native power names, not display text, for dispatch.
4. Prove one-shot attack and one targeted power first, then self/squad powers where supported. Reuse
   ME2 hold/follow for regroup; add standing, retreat and compound orders only after their cancellation
   and native navigation are observed. Do not write LE1 formation bytes into ME2 objects.
5. Reconcile pending commands with actual AI target, power activation/cooldown and movement. Bound
   retries; cancel on target death, load, dialogue, recipient change and player override. A dispatched
   event must not be reported as an executed order.

**Acceptance.** Test each living member, whole squad, unreachable/occluded/dead targets, cooldown,
unavailable/evolved powers, repeat/cancel and combat-to-dialogue transitions. Require observed attacks
or power effects on the requested target. Test hold and follow physically, not just successful calls.
Add adapter/mapping and stale-request tests around the existing gesture/standing-order tests.

## D. Squad voice

**Reuse.** `src/plugin/Voice.cpp`, `VoiceModel.cpp`, `src/voice/Voice.cpp`, `src/game/Voice.cpp`,
`src/core/vr/VoiceGesture.*`, `SquadParser.*` and `src/core/voice` already implement the recognizer,
radio gesture, grammar, parsing and optional model tier. Use `tools/fetch-voice.ps1`, the existing
voice benchmarks and `tests/VoiceTests.cpp`, `VoiceMenuTests.cpp`, `SquadPhrasings.cpp`.
The host's v31 voice service wiring is currently in its LE1 branch.

**Implementation sequence.**

1. Share host recognition/services and deployment discovery with LE2. Verify workers start/stop once,
   missing optional models produce an actionable unavailable state, and microphone work never executes
   Unreal calls. Reuse existing installer/model provisions instead of packaging another recognizer.
2. Replace LE1-only roster aliases and power vocabulary with ME2 scene-derived terms. Preserve familiar
   names and disambiguation. Feed the parser only supported commands from B/C; reject commands whose
   game-side action is absent. Audit the LE1 readiness and throwables/venting assumptions in `game/Voice`.
3. Preserve the hand-to-ear radio gesture, haptic contact and push-to-talk ownership. Route transcripts
   through the same dispatcher as C. Resolve "there" or "that one" from the existing pose history at
   utterance time, then revalidate at dispatch. Reject late model answers after scene/recipient changes.
4. Keep partial/final duplicate suppression. Start with rule-parsed commands, then enable the existing
   optional model fallback after its ME2 vocabulary and constrained output are verified. Coordinate
   microphone ownership with E so a spoken reply cannot also issue a squad order.

**Acceptance.** First replay text through the existing parser/command channel to isolate dispatch. Then
test real microphone recognition, near-name ambiguity, silence, background speech, missing model,
late answer and tracking loss. Observe exactly one native action for a partial-plus-final transcript.
Measure recognition-to-dispatch latency and CPU cost separately from VR frame pacing. Fake-headset
timing cannot establish physical-headset smoothness or microphone ergonomics.

## E. Thespian mode

**Reuse.** [Thespian plan](thespian-mode-plan.md), [implementation research](../research/70-thespian-mode.md),
`src/core/voice/ScriptMatch.*`, `src/core/vr/Thespian.*`, `src/game/Thespian.cpp`,
`src/plugin/Thespian.cpp`, `tools/thespian-census.py` and the existing matcher/bench tests.
The matcher and panel are reusable; LE1 corpus accuracy is not evidence of ME2 reply correctness.

**ME2 differences.** The current game module obtains conversation state through LE1-oriented world
conversation code. Decouple a small read-only conversation/reply adapter so Thespian can work on the
existing ME2 cinema screen without waiting for F. ME2 has different text/audio storage and timed interrupts.
Do not reuse LE1's embedded TLK/ISACT extraction assumptions for ME2.

**Implementation sequence.**

1. Verify ME2's active conversation, `GetConversationHandler`, current entry, offered reply indices,
   wheel visibility, category, eligibility and submenu state. SDK candidates include `GetReplyText`,
   `GetReplyParaphraseText`, `QueueReply` and `SelectConversationEntry`. **[verify]** Match the native
   wheel's exact position-to-reply mapping, including Investigate and unavailable persuasion choices.
2. Publish read-only cards through the shared panel first. Use native localized strings. Add ME2 amber
   styling using the existing theme. Keep the original wheel until replacement rendering and navigation
   are proven, then hide only the replaced display while retaining native handler state and triggers.
3. Implement native reply selection and compare it with a normal wheel pick from the same protected
   checkpoint. Never implement selection by setting plot variables. Capture reply identity, subsequent
   entry and natural save changes; reject cards from an old conversation or changed wheel.
4. Connect the existing matcher and radio/listening lifecycle. Menu-only cards and near-identical lines
   retain explicit selection/disambiguation; uncertain speech does not commit. Preserve normal controller
   selection when voice is unavailable.
5. Separately verify Shepard voice suppression and line skip through ME2's audio/conversation path.
   Respect unskippable replies and restore audio on every exit. Do not enable LE1 voice placement hooks
   merely because matching works. Retain cinema presentation while these mechanics are proved.

**Acceptance.** Cover ordinary replies, Investigate/Return, menu-only cards, unavailable persuasion,
ambiguous speech, late transcript and an unskippable line. Check both timed interrupts inside and outside
their prompt windows. Compare native versus spoken selection for branch and saved-state equivalence.
Use LegendaryExplorerCore/ME2 TLK support if extending the census; keep text/audio and personal saves
private. Existing LE1 `--wheels`/VO commands need a format audit before use on ME2.

## F. First-person conversations in the world

**Reuse.** [World-conversation research](../research/59-conversations-in-world.md),
`src/game/ConversationWorld.cpp`, `ConversationFlow.cpp`, `AuthoredMoment.*`,
`src/engine/ConversationHooks.*`, `src/core/vr/ConversationWorld.*` and
`src/plugin/ConversationWorld.*`. Reuse their camera handoff, participant placement rules, published
state and audit vocabulary. This is a substantial adapter port; the large LE1 module contains scene-specific
exceptions and native assumptions that must be reviewed individually.

**Implementation sequence.**

1. Share E's live conversation observer. Survey ordinary dialogue, a walking conversation, a multi-actor
   scene and a cinematic/action sequence. Record camera mode, player/participant transforms, authored
   movement, reply wheel, interrupt window and exit state without overriding them.
2. Wire the existing world-conversation host service and LE2 camera/presentation seam for one simple
   supported scene. Preserve tracked head motion, calibrated body scale and one camera owner. Reuse
   the existing cinema screen for unsupported states and scene boundaries. Do not globally switch on
   world conversations or copy LE1 camera mode IDs/addresses without verification.
3. Adapt participant placement and authored movement only after the baseline scene is stable. Preserve
   story movement and sequence completion; avoid freezing or teleporting participants to make a picture
   look correct. Transfer only LE1 exceptions that a ME2 scene demonstrates it needs.
4. Reuse the world subtitle/reply placement and E's panel where enabled. Keep native trigger interrupts
   unclaimed. On conversation end, scripted walk completion, death, load or disable, restore camera,
   controls, mesh visibility and temporary state. Prevent duplicate dialogue overlays during handoff.
5. Adapt existing audit inputs to ME2 identities/fixtures. `conversation-from-save.ps1`,
   `conversation-audit.ps1`, `conversation-session.py`, `scene_audit.py` and `audit-report.py` contain
   reusable orchestration/reporting, but their LE1 launch and scene commands are not verified ME2 adapters.
   Extend them or their inputs after checking capability; do not fork another audit framework.

**Acceptance.** Paired eye captures and state traces must show correct scale, continuous head movement,
visible speakers and clean returns to gameplay. Include a chair/height case, a close interaction, a
scripted walk, dialogue interrupted by a menu, both morality interrupts and an unsupported scene's
cinema fallback. Inspect meshes and sight lines visually. Physical-headset review must assess motion and
comfort; passing transform or screenshot rules is insufficient.

## G. Galaxy map and planet scanning

**Reuse.** [ME1 galaxy-map implementation](../research/51-galaxy-map.md), `src/game/GalaxyMap.cpp`,
`src/core/vr/GalaxyMap.*`, `GalaxyHologram.*`, `src/plugin/GalaxyHologram.*`,
`GalaxyHologramDraw.*`, `WorldUi`, `GamePointer` and `tools/make-galaxy-pick-recording.py`.
The existing draw helper linked for squad placement does not mean the ME2 galaxy map is implemented.

**ME2 differences.** LE1's 2DA rows, CIC object names, table dimensions, mode IDs and map-opening delay
are specific to LE1. ME2 adds ship movement, fuel/probes, orbit scanning and native feature discovery.
Use its native galaxy hierarchy and visibility/usability conditions. Keep the native economy and plot
transitions authoritative.

**Implementation sequence.**

1. Normally load the existing protected Normandy fixture and establish the native console → galaxy →
   system → orbit → exit route with existing controls. Observe map/planet identity and resources. Keep
   Save 4 for combat; it is not a substitute for a reachable Normandy fixture.
2. Adapt ME1's table anchor, camera hold, hologram model and pointer to ME2's actual CIC geometry and
   native map objects. Start with the native map/UI projected on the table so selection/travel work
   before rebuilding spatial map elements. Preserve fuel, travel confirmation, back navigation and exit.
3. Port rendering and input ownership together. ME1's galaxy notes contain fixes for early UI draws,
   flicker and travel/arrival frame ownership; use their invariants but measure LE2's draw order. Keep
   labels and pointer on the same submitted geometry, and clear stale picks when the native map changes.
4. Add orbital scanning as a distinct input context. Initially reuse native scan axes/held controls and
   probe launch. **[verify]** Trace `SFXGameModeOrbital` scan and `LaunchProbe` behavior before translating
   controller rays or gestures into it. Define one launch edge and cancellation rules so a held trigger,
   menu closure or tracking recovery cannot spend another probe.
5. Reuse the [orbital observer and prepared resource fixtures](../research/le2-scanning-readiness.md).
   `observe-orbital` currently belongs to the diagnostic host; prove or integrate its observation seam
   in the Halcyon session before depending on it. Do not run two independent hosts/XR owners. Compare
   same-planet pre/post probe counts, resources, depletion and feature state with the visible outcome.
6. Acquire a natural discovery baseline using [the Zanethu investigation](../research/le2-galaxy-fixture-source.md).
   Its hierarchy and transition are source-derived candidates, not an accepted runtime fixture. Follow
   the native anomaly/probe/reveal flow, save, reload and compare actual journal/landing/planet state.
   Do not set discovery flags to simulate success or derive save IDs from object-name suffixes.

**Acceptance.** Exercise enter/exit, selection, travel/return, fuel refusal, scan movement and resource
readout. One available probe must become zero after exactly one successful launch; zero probes must
produce a visible native refusal with unchanged resources. Test a no-discovery probe separately from
an anomaly hit. Require natural save/reload persistence for discovery. Check table alignment, both-eye
labels, seated reach and scanning comfort in a headset.

## Acceptance still owed for the five implemented areas

| Area | Reuse/current implementation | Remaining proof |
| --- | --- | --- |
| Comfort | `game/Comfort`, shared pad merge/vignette, ME2 settings | Two discrete snap turns, held smooth turn, head/hand-relative movement, vignette, menu-held-stick release. The latest protected comfort run timed out before ME2 started. |
| Wrist and gun HUD | `HudPieces`, native ME2 crops and ammo effect | Small/large guns, elemental ammo changes, weapon swaps and both eyes. Inspect weapon-relative placement and physical-headset legibility. |
| Cover/crouch/sprint | `Posture`, `VrCover`, native Storm and existing traversal | Recheck current build with standing/seated calibration, protection, sprint release and vault interaction. Preserve the no-cover-snap decision. |
| Player powers | Shared wheel/coach/gestures and ME2 native roster/activation | Actual controller selection, successful targeted push at a hostile, evolved powers, other weapon types and cooldown refusals. Prior evidence covers chest activation and a drawn-rifle ammo gesture. |
| Squad placement | Existing B wheel/arc and ME2 hold/follow adapter | Selected recipient reaches and holds the marked floor, whole-squad case, double-tap follow, invalid landing, tracking loss and roster changes. Remains opt-in with `[SquadOrders] place = 1`. |

## Existing validation machinery and work receipts

Read [the scripted testing handoff](le2-scripted-testing.md) and the relevant repository skills before a
native run. Use [the protected Halcyon runner](../tools/le2-halcyon-run/README.md) for current VR tests.
`prep.ps1` owns the exclusive lock and backups, `launch.ps1` launches, `load.ps1` uses the native save-load
descriptor, and `restore.ps1` verifies restoration. Check current ownership/processes immediately before
launch. A free lock does not grant ownership of an already-running game. Never `streamlevelin` or return
the shared game slot without the owner's authorization.

Reuse `tools/console.ps1`, `snapshot.ps1`, `record.ps1`, `replay.ps1 -Inputs`, `perf-probe.ps1` and the
existing synthetic recording generators after checking their ME2 command support. Console dispatch
isolates the game adapter; an input replay is still needed to establish actual gesture/button routing.
Use [the existing save/scenario workflow](../tools/qa-le2/README.md), `prepare-variants.py`,
`save-evidence.py`, `check-save-oracle.py` and the Trilogy Save Editor oracle for controlled resource
variants and persistence checks. Do not build another binary save parser or treat arbitrary story-bit
edits as a coherent checkpoint. Diagnostic observer availability must be checked against the deployed host.

Each package lands with the following receipt, appended to its existing research page and linked from M6.

- Source commit, adapter/service changes and unresolved native assumptions.
- Focused offline tests for mappings, ownership, stale state and refusal; LE1 regression checks when
  shared behavior changes. Run the repository-required `build-trilogy`, `build`, `build-mono` and relevant
  CTest checks for implementation milestones; documentation-only changes need link/format/license checks.
- Protected run/build identity, normal-loaded fixture, action sequence and before/after native observations.
- Both-eye captures with hands/wrists visible where needed to judge projection, and actual observed outcome.
- Restoration verification and separately recorded physical-headset findings or pending checks.

Completion requires **implementation, native behavior and headset acceptance** to be tracked separately.
Mark a refused/failed launch as no native evidence. Keep saves, extracted scripts/text/audio, binaries and
raw captures in ignored local evidence storage; publish only original notes, tools and suitable references.
Land scoped commits using `tools/land-on-main.py`, preserving other sessions' changes. The owner pushes.

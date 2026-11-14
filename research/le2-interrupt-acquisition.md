<!-- SPDX-License-Identifier: MIT -->
# LE2 natural timed-interrupt acquisition follow-up

2026-11-14. Offline inspection only. **No replayable timed interrupt is established
for the current late-game Normandy corpus.** Several unset outcome flags belong
to encounters already passed. A useful earlier-progression alternative is the
Veetor conversation at Freedom's Progress: its installed conversation contains
both interrupt polarities and an authored nearby save sequence. It can share
the separate normal new-game progression already proposed for the ProCer reload
fixture. Neither candidate is promoted to a validated scenario.

This follows the [initial acquisition audit](le2-fixture-acquisition.md),
[validation goal](../docs/le2-validation-goal.md) and existing
[LE2 scanner](../tools/conversation-scan/README.md). It adds no save editor,
launcher, catalog framework or runtime action.

## Existing evidence and save state

Reused the private acquisition dumps before doing targeted package inspection.
Fresh strict `le2-save.py` reads covered archived Normandy base `ced4cb21...`,
the naturally persisted resource-test save `b54bf7c6...`, and original chapter
`2f4c441c...`. The second has the previously documented resource edits in its
history; it is not an untouched-resource baseline. All selected plot values
agree across these saves and with their existing independent TSE decodes. The
chapter is `BioP_EndGm2`, while both other saves are `Biop_Nor`.

| Lead | Observed state / source | Acquisition implication |
|---|---|---|
| Garrus mechanic | Boolean 2660 true; prior mechanic package/track inspection already exists. | Retain the known earlier-progression candidate; do not rescan or reset this one flag. |
| Liara introduction | 1550/1551 false, but met-Liara 1546 true, actual DLC completion 6815 true. | Unused interrupt outcomes do not imply an available introduction. The older base placeholders are not an effective DLC route. |
| Liara post-DLC visit | 6930 false, but date 6931 true, opening 6929 true, play-date 6940 false. | The Stronghold sequence branches on 6931. Its starter conversation also requires 6931 false to reach the date invitation branch. A missing closing flag is not a date replay recipe. |
| Citadel recovered credit chit | Completed 2496, found-chit 3016, talked-once 3018 and journal-complete 3068 true. | The LowerWing level-load branch tests 2496 and modifies `Active` on the associated pawns. Ambient entry is connected only to the incomplete branch; this is not an established current-save replay route. |
| Other bounded checks | Victim-mother talk 2027 and Samara end talk 2710 true; reporter-spoken 4428 true; Arrival completion 7436/7452 true; Overlord rescue 6561 true. | These are exclusions from the immediate shortlist, not a proof that every possible optional interrupt is exhausted. No full campaign catalog was repeated. |
| Zaeed label trap | 6272 false, but mission-done 1507, loyal 192, made-loyal 6258 and journal-complete 6270 true. | Do not infer an unfinished mission from one editor label. |

TSE labels are search anchors at revision
`6451f23d1fba62358328dce4e87733970f36ebf1`, not complete legal-state recipes.
The current saves also have Veetor autosave 2431 and Renegade outcome 5021 clear;
their late-game progression still does not make the prologue replayable.

## What the alternate conversations actually establish

`DLC_EXP_Part01/BioD_Exp1Lvl5_200Cabin_LOC_INT.pcc` contains the
`shdli5_relationship_02_h` interrupt visibility track, joined through
`InterpData_28` to node export 469323. It turns visibility on at approximately
3.383349 seconds. However, that node has only a default auto-continue edge,
and the scanned conversation has no explicit Paragon/Renegade interrupt reply
category. This is **not** enough to call it a usable timed morality response;
custom/native behavior or unused presentation remains unresolved. The preceding
date-state checks already prevent treating it as a repeatable current fixture.

The Citadel `cithub_rpplot_giver2_d` conversation is more concrete: node 318320
has an explicit Paragon interrupt reply (reply 9), and its matched track has
authored on/off keys at 1.2174913 and 3.0130534 seconds. The normal path requires
the recovered-chit branch (3016 true) while the encounter remains active.
The current completion state takes the actor-modification branch, not the normal
incomplete-quest ambient entry. A naturally obtained earlier save before returning
the chit would be a useful alternative, but none
was established in this inspected corpus.

## Preferred fallback: Veetor, sharing the reload acquisition playthrough

English `BioD_ProFre_501Veetor_LOC_INT.pcc` contains
`profre_veetor_door_d_dlg`. Both the base package and the installed
`DLC_METR_Patch01` copy were inspected separately. All 330 scanner records agree
after removing package path/hash provenance. The nonlocalized sequence outputs
are not wholly identical; the 52 relevant `Autosave` and `Veetor_Door` records
do agree. Preserve both package identities rather than claiming a general
mounted-catalog proof.

| Polarity | Conversation nodes / replies | Associated authored visibility |
|---|---|---|
| Paragon | Entry exports 210141 and 210142 have explicit Paragon interrupt reply 6. | `InterpData_107`: on at 0, no off key; `InterpData_108`: on at 0, off at 2.9975576. |
| Renegade | Entry exports 210145 and 210146 have explicit Renegade interrupt reply 11. | `InterpData_111`: on at 3.1459942, no off key; `InterpData_112`: on at 0, off at 5.0792027. |

These are per-node authored keys, **not measured native input windows**. Reply
transitions 4366 and 4370 have the direct serialized effects reviewed below;
transitive and native effects are not established. The sequence has separate
Paragon/Renegade tutorial events.
Its autosave route checks armor-tutorial state 1933, then the 2431 autosave
guard, executes transition 2425 and reaches normal `SFXSeqAct_SaveGame`.
The nearby conversation entry also toggles saving. Exact save placement and
reload entry point still need native verification.

Acquisition handoff, without changing existing story flags:

1. Use the existing protected session workflow with a separate normal new-game
   progression. Preserve its ProCer reload checkpoint first.
2. Continue normally through the opening and Freedom's Progress to the Veetor
   encounter. Keep combat/traversal and conversation choices game-owned; do not
   load the late Normandy save into this map.
3. Preserve the naturally written autosave when the authored route runs, plus
   a manual pre-conversation save if the normal Save menu allows it. Use the
   existing file collector, strict reader and TSE oracle to pin the actual
   bytes. An authored save action alone does not prove a usable checkpoint.
4. Normal-load that save in a fresh protected session and review the scene.
   Establish which dialogue branch produces each polarity; do not assume both
   occur on the same route. Keep separate taken/missed trials from identical
   starting bytes, then compare normal outcome saves.
5. Start the [bounded recorder](../tools/qa-le2/control.md) just before the
   expected prompt. Export the stopped capture and use `observe-dialogue`
   snapshots to supplement conversation, node and package identity. Correlate
   the sampled times with rendered prompt evidence, normal input and the
   selected branch. Conversation-specific synthetic input admission remains
   open. Keep direct interrupt-completion calls out of acceptance runs.

The Veetor draft requires a finished recorder export with archived start
provenance. The scenario checks that one stable row ties the Veetor name
to the authored entry, interrupt reply, polarity and GUI display flag.
The Paragon and Renegade variants each require their own polarity.
If the short prompt falls between samples, preserve that result and repeat
the trial from the same starting save. Review actual pixels, input timing
and the resulting branch independently. A display flag reports sampled
GUI state and cannot establish any of those outcomes.

No native navigation was attempted here. Reaching the Normandy galaxy map,
traveling to a hub and walking to an NPC remain normal-navigation prerequisites
for any future hub lead; none of those steps reopens a completed one-shot beat.

## Veetor direct transition and saved-outcome review

2026-11-14 offline follow-up against `270e5483`, using the existing state-event
scanner and pinned save reader/TSE outputs. The installed base and Patch01
English Veetor conversation copies agree on these reply records:

| Polarity | Reply index / node export ID | Transition | Direct serialized effect |
|---|---|---|---|
| Paragon | 6 / 210192 | 4366 | Increment LE2 `plot.integers[2]` by 5 |
| Renegade | 11 / 210197 | 4370 | Increment LE2 `plot.integers[3]` by 5 |

Each event has exactly one `BioStateEventElementInt`, with `Increment=true`,
`NewValue=5` and `UseParam=false`; the reply's transition parameter is -1 and is
not used by this element. The owning map is base `Startup_INT.pcc`, export 1742,
`PlotManagerAuto.StateTransitionMap`. Neither event contains a direct Boolean,
quest, substate or function element. This describes the inspected serialized
lists, not all subsequent conversation/native effects. TSE's pinned
`src/gui/mass_effect_2/general.rs` labels integer 2 Paragon and integer 3 Renegade;
these are LE2 plot totals, not imported `me1_plot` values.

The existing LegendaryExplorerCore mounted-file resolver selected the base
`Startup_INT.pcc` and the separately named Patch01 English startup; Patch01 has
mount priority 2000. Explicit inspection of `Startup_METR_Patch01_INT.pcc` found
**no `BioStateEventMap` exports**, hence neither requested event. Its scanner
exit 1 / `scopedInspectionComplete=false` reports missing IDs, with zero parsing
errors; it is not a successful duplicate-definition match. The base scan found
both events once, with no missing IDs, duplicates or errors. This is an offline
base/Patch01 comparison, not enumeration of every DLC/language event map or
proof of loaded runtime precedence.

Archived bytes were reread with `le2-save.py`; both target integers agree with
already pinned TSE decodes (valid CRC and byte-identical reserialization):

| Existing archive (SHA-256 prefix) | Map | Paragon / Renegade |
|---|---|---|
| Original Normandy `ced4cb214a636fbf` | `Biop_Nor` | 1209 / 302 |
| Resource-persistence Normandy `b54bf7c65437bfa6` | `Biop_Nor` | 1209 / 302 |
| Chapter acquisition `2f4c441c068f7906` | `BioP_EndGm2` | 1184 / 280 |

These are aggregate late-game totals, **not paired Veetor outcomes**. No taken
versus missed native trials exist in this evidence. A future outcome comparison
must bind identical starting bytes, the exact Veetor conversation/reply and
actual normal input, then establish what occurred between samples and normal
outcome saves. A +5 total alone is not unique to this interrupt; unrelated
morality rewards must be excluded or accounted for. Likewise, no delta cannot
by itself prove a miss if the save preceded the effect. Keep observed reply,
sampled routing/visual evidence and persisted total comparison separate. No
unique completion flag, reset recipe or plot-write fixture is inferred here.

Private evidence identifier: `le2-veetor-transitions/veetor-transition-source`.
Use the existing [story review control comparison](../tools/qa-le2/story-review-usage.md#taken-and-missed-interrupt-saves)
on the actual taken and missed outcome saves. Retain absolute deltas as well
as the difference for the selected polarity.
The offline integration check used the pinned base source report and the same
archived LE2 save for all three inputs. Both version-1 integer effects decoded,
and each reported a zero difference against its expected +5. The positive
comparison uses synthetic bytes in tests. No Veetor trial was run.
Reproduce with existing `--game LE2 --state-events 4366,4370`, selecting
`--only Startup_INT` in the base cooked directory and
`--only Startup_METR_Patch01_INT` in Patch01, each with a new private output.
Scanner and Veetor package pins are unchanged from the provenance below;
TSE revision is `6451f23d1fba62358328dce4e87733970f36ebf1`.

| Source/report | SHA-256 |
|---|---|
| Base `Startup_INT.pcc` | `047bba83d5595c4b83a8a5280d64dc1a187c74bc444fb6e67f200c1191f333f8` |
| Patch01 `Startup_METR_Patch01_INT.pcc` | `94fb30e7efb11920acebd8d68da46ca3455386aa187be21b720fa2d9bb9541b0` |
| Private `base.json` | `494802c191996de7943281b58449487829744b997d29f976a7c00881ec8fd0aa` |
| Private `patch.json` | `5e0edacdedc4beb3fa27e42e15ddf5c55437de3026c93d27d8faa1316c0bed7d` |
| Private `mounted.txt` | `8cff4de1156a3347c270118e1c5d5e54f6445e9db0e175c901291d170a826499` |
| Private `save-review.json` | `a41ebadb61bebc2143cc75d3b51d573b116a4a429d796f57b54b4708a054860d` |

## Native observer contract and trigger/acceptance boundary

2026-11-14 follow-up against source revision `587e25de`. The resulting
[observer](../tools/qa-le2/control.md) covers read-only point samples and a
bounded, opt-in recorder. Native sampling and paired trials remain pending.

### Input routing does not prove acceptance

Installed `BIOInput.ini` maps the conversation-mode left trigger to the Paragon
command and right trigger to the Renegade command. The corresponding
`SFXGameModeConversation.InterruptParagon` / `InterruptRenegade` script functions
obtain the conversation GUI handler. The inspected handler's `InterruptParagon`
function invokes a Flash method; it does not itself select the reply.
`BioSFHandler_Conversation.HandleEvent` receives commands 6 (Paragon) and 5
(Renegade), invokes `BioConversation.SelectInterruption`, ignores that Boolean
return, and sets `m_bWasTriggered` afterward. Therefore a command callback,
sound or triggered flag establishes routing evidence, **not successful acceptance**.
Do not call these functions directly to manufacture input acceptance.

`UpdateInterruptVisuals` obtains the native `NeedToDisplayInterrupt` result and
updates the two displayed-polarity flags. Its flags describe the handler's most
recent UI update; they do not independently prove currently legal input or actual
pixels. `NeedToDisplayInterrupt`, `SelectInterruption` and `TickInterruptionData`
are native-only declarations in this inspection. Their exact eligibility,
acceptance and timing predicates remain opaque. Do not reconstruct a legal-window
Boolean from timer signs or authored keys. The PC handler overrides axis handling;
its gamepad mode also matters. Preserve actual input configuration provenance.

### Minimum read-only sample

Reuse the validated roots, object-table/class checks and guarded reads in
[`Le2DiagnosticObservation.cpp`](../src/engine/Le2DiagnosticObservation.cpp).
Reach `BioWorldInfo` through the validated local actor's `WorldInfo`, then read
its current conversation. Independently traverse the existing local GUI manager
and reachable panel/handler path, requiring reciprocal panel/manager links and
agreement between handler and sampled world. Do not globally search objects or
assume a localized caption identifies the conversation.

The pinned LE2 SDK declares these fields; the offsets are discovery evidence,
not a substitute for actual SDK assertions and native validation:

| Object | Minimum fields |
|---|---|
| `BioWorldInfo` | `m_oCurrentConversation` at `0xA4C` |
| `BioConversation` | Entry/reply arrays at `0xB8`/`0xC8`; `m_tInterruptInfo` at `0x2A0`; owner/player at `0x2C0`/`0x2C8`; current entry at `0x324`, selected reply at `0x32C`, current reply at `0x330`, node timer at `0x340` |
| `BioInterruptReplyInfo` | Relative offsets: reply-list index `0`, start/window remaining `4`/`8`, enabled/activated bits `0xC`, polarity byte `0x10`; size `0x14` |
| Current entry/reply | Bound-check the typed array index, then report node `nExportID`, transition ID and parameter. An array index is not an export ID |
| `BioSFHandler_Conversation` | Flags at `0x12C`: wheel `0x2`, Paragon `0x4`, Renegade `0x8`, screen initialized `0x10`, triggered `0x20`; panel state and input mode separately |

Keep object address/index/class and sample epoch as process-local provenance,
never durable handles. Cap array sizes, inspect only selected records, reject
invalid indices/nonfinite scalars, and resample roots/headers/selected records
around a copy. Identity disagreement makes that sample unavailable. Preserve
raw timer/flag meanings and separately label visual, routing and outcome evidence.

The recorder uses the existing owned game-thread consumer behind
[`Le2DiagnosticHost`](../src/engine/Le2DiagnosticHost.cpp). A start request
arms up to 10 seconds of observation, with at least 125 ms between samples.
It records game tick, monotonic time, conversation identity, selected nodes,
interrupt fields and GUI flags with explicit missed, unavailable and omitted
sample counts. Stop or expiry freezes the record for retrieval through the
bound channel. These samples can miss short windows or selected replies.
Compare timestamps only when the scene and input evidence share the recorder's
clock or have a documented alignment. Timing across a gap remains unresolved.

### Existing trace and input machinery to reuse

- [`CallTrace`](../src/game/CallTrace.cpp) already combines script pre-call and
  ProcessEvent post-call observations with time and 600-line limits. Its startup
  requires reflection readiness and an installed CallFunction hook. Ordinary
  startup installs that hook when hand aim is enabled; the diagnostic branch
  returns through its own tick path. Do not claim the console trace is available
  merely because LE2 diagnostic startup succeeds. LE2 CallFunction's declared
  address alone is not an installed/validated trace.
- [`ProcessEvent`](../src/engine/ProcessEvent.cpp) has an observer/post-handler
  dispatch seam; [`ProcessEventHook`](../src/plugin/ProcessEventHook.cpp) owns the
  actual host detour. If routing evidence needs callback events, use this ownership
  arrangement, exact validated function/receiver identities and bounded primitive
  command capture. Do not add another detour, replace an active consumer, inspect
  arbitrary string arrays or assume all script calls traverse ProcessEvent.
  Callback coverage needs a native trial; pre/post timing must remain explicit.
- [`Le2InputControl.inc`](../src/engine/Le2InputControl.inc) already exposes bridge
  positive/neutral poll counts, first/last pulse times and physical-input state.
  These prove output writes, not game consumption. Existing input categories
  exclude conversations. Human normal input can supply the initial trial; any
  later synthetic conversation admission needs its own narrow context validation.
- [`ConversationHooks`](../src/engine/ConversationHooks.hpp) protects ambient
  conversations from cancellation. Its `InterruptConversation` counter is not a
  Paragon/Renegade interrupt counter and must not be reused as one.

First native evidence should correlate sampled Veetor node export IDs with the
pinned authored records above, visible prompts/captures, normal player input and
observed resulting reply/node. Compare taken and missed trials from identical
fixture bytes and inspect normal outcome saves using the existing save reader,
diff and source-review tools. Neither triggered flags nor a successful bridge
pulse closes the accepted-outcome gate. No new acceptance claim is made here.

### Follow-up source provenance

The existing scanner hash below was rechecked. Installed base `SFXGame.pcc`
SHA-256 is `681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77`.
SDK declarations were read at LExSDKv2 revision
`6bd05809cccb1a099a520371a95779d4a56bb246` (`LE2/SFXGame_classes.hpp` and
`SFXGame_structs.hpp`). The existing private extracted
`le2-validation-evidence/galaxy-source/coalesced/BIOInput.ini` has SHA-256
`fb65ed00e45ddd69b55dbd625a9f0b4e218e5832ca6cc52d1b62134e5a736a8c`;
its parent base `Coalesced_INT.bin` pin is recorded in
[galaxy source findings](le2-galaxy-fixture-source.md). This is base configuration,
not a live merged-binding measurement.

Private artifact identifier: `le2-interrupt-acquisition/interrupt-readiness`
(the isolated acquisition worktree's ignored `local-tools/interrupt-readiness`).
Each source file was produced with the existing scanner's `--game LE2 --only
SFXGame --script Class.Function` mode; no package or extracted script is published.
Key output hashes:

| Private `.txt` artifact | SHA-256 |
|---|---|
| `BioSFHandler_Conversation.HandleEvent` | `9909307f7f300a14682f860d08711f195f4d226a39c1c8f009ff0e2e8d2ca174` |
| `BioSFHandler_Conversation.UpdateInterruptVisuals` | `f25e27a1018fe3ce61788ca6e65c7ede2f88e420a0c4e4e2c0e0ecf9de8830a3` |
| `BioSFHandler_Conversation.InterruptParagon` | `667295ca562dc648c9a21d4083a54856475b3d8ba3a1c23e663c8b33c845bb8d` |
| `SFXGameModeConversation.InterruptParagon` | `8dfc5c15926bfe097a60593ab4a97d5879d12f2f5454f756cbc7bf7f8fd2f46f` |
| `SFXGameModeConversation.InterruptRenegade` | `b24aa014a878b6f5c291d52dcbadad8daa9eaa055502a887a11e9fbd7a0e185a` |
| `BioConversation.NeedToDisplayInterrupt` | `57c40742e96242b8fcd58837eea3b9071e89f07e0b7ddc514adde1335bd4b358` |
| `BioConversation.SelectInterruption` | `d1bbe4aba7bed6101368c6a6162e6e3c437e2388d197694aa610e5b9862d5998` |
| `BioConversation.TickInterruptionData` | `459712d50845039353c77b729f32ba6fa1006c7895c0691193909455e2ddd96c` |

## Reproduction and private provenance

Scanner executable SHA-256:
`918a095735f00998e5ff99871902253c47c66069a6cce3e6d6b0d474addd6f01`.
Veetor English base hash:
`a7efe171e9be6daa93d48a678bf797ce9b51d2a5e25def33f3fc959346a442f3`;
patch English hash:
`17acd9bd97f394ccd1998284a14fef697d0a913a9c3bec079e7fd13d1a42e57b`.

Private evidence identifier `le2-interrupt-acquisition/source/inspection-final.json`
has SHA-256 `fea9e9f6a518f87d49459bb1c24dafebf2ee9f22b0bb825ac28dd8cdd80b8d5c`.
It records complete save/package hashes, TSE comparisons and source artifacts.
Raw package output remains in the isolated worktree's ignored `local-tools/source`.
Use existing `--game LE2` scanner commands with new private outputs: exact
English `--only` package scans, targeted `--sequence`, and class dumps only to
find candidate tracks. No game-derived text, save, package or image is committed.

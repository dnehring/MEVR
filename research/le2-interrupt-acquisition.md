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
transitions 4366 and 4370 are source references; their full effects are not
interpreted here. The sequence has separate Paragon/Renegade tutorial events.
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
5. A conversation-specific LE2 observer/input admission path is still required:
   active conversation/node, visible/legal interruption state, actual input and
   selected branch must be correlated. Existing party/default pad contexts and
   authored keys do not supply that proof. Reuse the bound channel and input
   bridge; do not call interrupt-completion functions as acceptance evidence.

No native navigation was attempted here. Reaching the Normandy galaxy map,
traveling to a hub and walking to an NPC remain normal-navigation prerequisites
for any future hub lead; none of those steps reopens a completed one-shot beat.

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

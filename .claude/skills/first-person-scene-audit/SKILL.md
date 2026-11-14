---
name: first-person-scene-audit
description: Audits LE1's first-person conversations and scripted walks (moments) from above - plays one scene of each kind from validation saves on the fake headset with overhead stance records, and judges where people stand, face and look against the player's drawn eyes (who looks at the player, who at Shepard's hidden body, where the squad stands, who walks), then A/Bs a fix in one run. Use when the owner reports odd heads, facing, composition or movement in a first-person scene, when a change to game/ConversationWorld could move people or their looks, or to check every kind of scene for a regression.
---

# Auditing first-person scenes from above

The audits of 2026-11-12 (research/69 "Stances from above", "Heads that look past the player", "Heads at the drawn eyes",
"Auditing scenes by rule") are the templates. Runs go through the skill **`fake-headset-run`** (`tools/fake-headset-session.ps1`);
this skill is what to run and how to read it.

## 0. The quick way: the regression set and the rules

1. After a change to game/ConversationWorld (or anything near conversations): `tools\conversation-audit.ps1 -IgnoreSlot` (about
   12 minutes; `-Long` adds Virmire's 3-minute scene; `-Only <keys>` a few; `-Plan` prints what it would play). It plays the ten
   scenes of `tools/conversation-audit-set.json`, prints the rules' findings and compares with `tools/conversation-audit-baseline.json`
   (exit code 1 on a regression). A change judged good: `-WriteBaseline` makes its numbers the baseline (commit it); or
   `-Judge <since> -WriteBaseline` from a pass already played. A set entry's `before` is a console line before its scene (the
   scripted walks walk along: `mevr convo follow on`; with the player standing still they wait for him, 120 s). The baseline
   keeps the latest play of each scene since `<since>`, so a pass that only replays a few scenes can replace just those.
   **Read its first block before the findings** (`tools/audit-report.py`, research/69 "Seeing the body in the audit"): per scene,
   how long arm IK drove the body, how long Shepard's drawn hands were read, in view and off the controllers, the snapshots'
   verdicts, the scene's own expectations (`expect` in the set: a console line and a regex, at a time), and the lines starting
   `NOT MEASURED`: a scene whose body was not measured, or whose expectation failed, makes the run fail whatever the baseline.
   Then open `contact-sheet.png` in the session folder (`%LOCALAPPDATA%\MEVR\sessions\<stamp>-audit`): every snapshot of every
   scene, the replayed hands held up in front of the eyes, framed green when both drawn hands are in view and on the
   controllers, red when not, blue outside a scene. A red frame is a body not where the player is: look at that picture first.
   Since item 38 (research/69 "Obvious bugs, measured") the block also says per scene: `drawn:` (the body in view and the people
   in sight, seconds not drawn; the near plane), `the stick:` (replies picked with it held, right-stick holds and how far they
   turned), `conversations played:` (and `WRONG SCENE` when the save played another one or another part), `pictures:` (a flat eye,
   a black HUD panel, a wheel reply cut at its edge). Any rule of `FAIL_RULES`, picture check or wrong scene fails the run.
1b. **The audit's own check: broken runs** (`tools\conversation-audit.ps1 -Canaries -IgnoreSlot`, about 15 minutes, each in a game of
   its own; research/69 "Broken runs the audit must catch"). Each brings a fixed bug back (`mevr convo bodyaway off`, `mevr convo
   showearly off`; and in one shared game, `"game": "opold"`: `hide body on`, `hide person on`, `nearplane off`, `sticklatch off`,
   `stickturn off`) or takes away what the audit needs (no hands), and must be caught; a `BLIND` line means the audit would pass that
   bug. Run them after changing the audit, the runner or the set. **Every bug the owner finds that the audit missed becomes a broken
   run** (a test switch in the mod, an entry in the set's `canaries`) next to its fix.
2. Any records, any time, no game: `python tools/conversation-stance.py --findings --since <time>` (rules: `tools/scene_audit.py`),
   `--baseline-compare <file>`; `tools\conversation-audit.ps1 -Judge <time>` judges an earlier run again.
3. A headset session: `python tools/conversation-session.py` lists, per scene, the mod's live audit (`audit:<rule>`: the same rules,
   measured ten times a second in every scene in the world; the summary's `audit`; `convo: audit:` lines).
4. Only when a rule finds something new, read on: `--heads` for that scene, then its sheet, then (below) the by-eye procedure. A
   new kind of fault becomes a rule in `tools/scene_audit.py` with a case in tests/SceneAuditTests.py, and a number in `StepAudit`.

## 1. What the records are

`mevr convo stances on` (the runner's `-Stances`) makes the mod write a JSON per moment to `%LOCALAPPDATA%\MEVR\stances`:
`begin`, `after-dip`, `opening` (2 s), after each walk, whenever someone moved a metre or turned 40 degrees, `line-<entry>` (runner
`-Lines N`), `turned-90/180/0` (runner `-TurnAt N`), `settled`, `end`; in a scripted walk `moment-begin`, `moment` every 1.5 s,
`moment-end`. `mevr convo stance <tag>` writes one now.

| Field | Meaning |
|---|---|
| `player.at` | the held place: **the game's camera**, not the eyes |
| `player.eyes` | where the eyes are drawn: the held place plus the host's view offset (0.43 m back, 0.18 m down) and the head's tracking. "You" in every judgement |
| `player.bodyFromEyesM` | Shepard's head bone from the eyes, flat. 0.06-0.17 m when his body is under the player; more is a fault or a view that walks apart (`detached`) |
| `people[].face` | the head bone's first axis: `toYouDeg` (off the drawn eyes), `toShepardDeg` (off his head bone), `toHeldDeg` |
| `people[].gameLook` | the game's look-at target (`Shepard`, `you` = the eyes stand-in, a name), its rank (99999 = the game's or authored, 100 ours), `aim` (a point along the look, not on the target) |
| `people[].role` | speaker / listener / bystander this line; `owner`, `squad`, `hidden`, `walk`, `held` |
| `others`, `walls`, `circle`, `ahead` | bystanders within 20 m, ray scans at knee and chest, the conversational circle, what will still move |

**The rule the records taught: the player is `PlayerEyes()`, never `gC.hold`** (docs/knowledge/gotchas/held-place-is-not-the-eyes).

## 2. The tools

- `python tools/conversation-stance.py --audit --since <time>`: a row per scene: Shepard's head from the eyes (range), looks meant
  for the player (game target Shepard or the stand-in), how many faces point at the player / at his body / elsewhere, the face's
  median angle off the eyes, the aim's, and the worst. **Start here.**
- `--heads`: every moment, whom each face points at with both angles and the body gap. Then the sheets
  (`conversation-stance.py --since <time>`, PNGs beside the records; Read them): the player up the page, faces cyan, the game's
  aim dotted pink, Shepard's body grey when apart, walls and low things, walks and held places.
- `tools/conversation-from-save.ps1`: `-All -Career John02 -List %LOCALAPPDATA%\MEVR\save-dictionary-list-John02.json
  -FirstSlot 80 -Only 82,85 -Pace full -Seconds 90 -Stances -Lines 6 [-TurnAt 7] [-WanderAt 9] [-EndAfter 4] [-Before "mevr ..."]`.
  `-Before` runs a console line before each scene (an A/B switch). A static scene ends itself once its `settled` stance is written
  (after `-Lines` and `-TurnAt`).
- Console: `mevr convo stance|stances on|off|ahead|turn <deg>|goto [x y]|walk x y s|eyes on|off|follow on|off|pick n|skip|end`,
  `mevr moment`. In a scripted walk `follow on` is a player walking along (the simulated stick after Shepard's pawn); the
  moment records' `walkAlong` says whether the walk is paused for the player, the gap, who is beside Shepard, a line playing.

## 3. The scenes, one per kind (sample; never sweep everything)

| Kind | Save | Notes |
|---|---|---|
| plain stage, the mark a step away (stay) | John02 82 Opold | Opold is a hanar: his head bone's axis is no face (90 degrees always); judge by the game's aim |
| a mark across the room (walk-in) | John02 83 Anoleis | good for `-TurnAt` |
| an NPC's authored walk | John02 84 Wrex on the beach | did not start from its save (plot) |
| a trigger's scene, several people | John02 86 Benezia's death | her own dying animation turns her head |
| framed, in the world | John02 87 Wrex at Fist's | no looks meant for the player |
| staged, cut to Shepard's mark | John02 88, or `%LOCALAPPDATA%\MEVR\squad-saves\John02_70.pcsav` (Kirrahe, Virmire) | `-Save <path> -Action Plot_CH2_Virmire_Rendezvous_20.jug20_sal_captainkirrahe_dlg.BioSeqAct_StartConversation_1 -Class BioSeqAct_StartConversation -Seconds 175` (black screen at 135 s) |
| the baseline wheel conversation | John02 85 Chorban, 89 Avina | |
| the Council (staged; a moment, then the hearing) | John02 80, 81 | `-Pace skip` or `-EndAfter`; long |
| moments (scripted walks) | John02 80, 81 (walk-ins), 90 (Chellik on foot), 91-93 (touches) | records `moment-*` |
| the squad, a walk-in by the view | John00 squad saves 80 (Girard: the body stays on its mark), 84 Zabaleta, 81 Dr. Michel | `-Career John00 -List %LOCALAPPDATA%\MEVR\squad-saves\list.json -SaveDir %LOCALAPPDATA%\MEVR\squad-saves` |

## 4. The procedure

1. **Make the question a number** before running: "a face meant for the player points at his eyes within ~20 degrees, and never
   nearer Shepard's head bone when that is 0.3 m or more away"; "Shepard's head within 0.2 m of the eyes after a turn".
2. **Run once with the current build and the new measure** (add the field to the record first if the question needs one: a
   record field costs nothing and makes every later audit cheaper). One scene per kind; `-Lines` so heads are seen per line.
3. **Read**: `--audit`, then `--heads` for the rows that look wrong, then the sheet. Separate artefacts from faults:

   | Looks wrong, isn't | Why |
   |---|---|
   | a constant 90 degrees for one person | a skeleton whose head bone axis isn't the face (hanar); check the pink aim |
   | one line 50-85 degrees off | the line's own authored gesture (Girard's line 38 in every run) |
   | 0.4-0.6 m for half a second at a cut | the view settling after the opening's dip |
   | someone "at his body" with no look target on Shepard | facing someone who stands near the hidden mark (Garrus toward Girard, 2 m from it): check `gameLook.target`; the real fault is a target `Shepard` while `bodyFromEyesM` is 0.5 or more |
   | someone in their own animation | Benezia dying; seated people |
   | far people "at his body" | 10 m away the eyes and his head are the same bearing: look at the angles, not the label |
   | aim 20-25 degrees off in every scene | the aim is the last link's point along the look, not the target |

4. **Fix, then A/B in one run**: give the fix a console switch (as `mevr convo eyes on|off`) and run the scenes twice with
   `-Before`; or rerun only the scenes that failed. Compare the `--audit` rows of the two passes (split them by time).
5. **Write it down**: research/69 (an item in the status list, a section with the run tables, a log line), CLAUDE.md's
   first-person paragraph (names and rules), CHANGELOG (in the player's words), notes/headset-checklist.md (what the owner should
   see), and docs/knowledge/gotchas if the same cause bit twice. Then land and install (`fake-headset-run`).

## 5. Normal numbers (2026-11-12)

- Shepard's head bone 0.06-0.09 m from the drawn eyes when the player stays or walks in with the body, 0.09-0.14 m after a staged
  cut; the eyes do not move when the right stick turns.
- A face meant for the player: median 6-11 degrees off the eyes, up to about 22 (the look-at chain's reach and the game's
  nods); a body re-faces only past 80 degrees for a second.
- A walk-in by the view (the body on its mark, `detached`): the eyes stand-in is on (`his looks moved to your eyes`), the
  person spoken to faces the player (`facingsToEyes`), faces 7-14 degrees off the eyes.
- A moment: Shepard's hidden pawn walks the track (the gap grows to many metres while the player stands still); the people near
  have **no look-at target** (`gameLook` null): their faces are the matinee's animation, so "Looks meant for you" is 0 and a face
  "at his body" is the animation's, not a look. The stand-in has nothing to move there (A/B identical, 2026-11-12).
- The live audit in the summary (`audit`): `gapMaxM` 0.07-0.13, `faceOffMeanDeg` 7-12 in plain scenes; known findings, in the
  baseline (2026-11-12): Benezia's `face-off-you` (her dying animation, mean 46), the hearing's `eyes-off-body` (0.70 m for
  1.3 s, 2.5 s in), Ashley's `speaker-behind` at Kirrahe's camp (her stage mark, minor), Tali's `squad-behind` at Zabaleta's
  (at the rule's edge), Girard's `face-off-you` near the quarter line (his line 38's gesture); `faceAxisUnknown` 1 for Opold.
- A crash during a load after a scene: read `crash-breadcrumbs.txt` and symbolize it with the deployed PDB (`ASI\LE1MEVR.pdb`,
  copied before the next deploy overwrites it) by `python tools/resolve-native-symbols.py <copy of the ASI> <rva...>`.
- A gap of 0.3 m or more that stays after a cut is a placement fault: check the log for `the opening's eyes were drawn` and the
  record's `player.at` against `player.eyes` (they must be the view offset apart, about 44 units back and 18 down).

Not covered by the records: scenes on the 3D screen (no first-person eyes) and flowed scenes on foot (no conversation mode;
Shepard's body is the player's there).

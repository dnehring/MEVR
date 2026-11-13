<!-- SPDX-License-Identifier: MIT -->
# 70. Thespian mode: the player speaks Shepard's lines

The plan is [docs/thespian-mode-plan.md](../docs/thespian-mode-plan.md). This note is the working log **and the
implementation guide**: it is written so that someone who has not followed the work (a person or a smaller model) can
continue from any section. Rules for whoever continues:

- Do the steps of section 8 in order. Each step names its files, the engine names to use, what to log, and how to check it.
- Everything marked **[verify]** has not been seen in the running game. Verify it in the step that says so, write the
  result here under the step, and only then build on it.
- Follow CLAUDE.md's rules. The ones that matter most here: engine access only through `src/ue3/` (`ue3::FieldRef`,
  `ue3::FunctionRef`, `ue3::Params`, `ue3::runtime::Call`), every resolved name gets a row in `engine/ReflectCheck.cpp`,
  script calls only on the game thread, every write into engine memory followed by `write_journal::Wrote(...)`, host and
  mod deployed together when a shared struct changes.
- Game text and audio never go into the repository. Everything the tools below write goes under `scratch/thespian/`
  (ignored by git).

Status, 2026-11-12: **phase 0 (offline) is done except the tier-2 comparison** (section 7): reading a card's line
aloud picks the right card for 99.3% of 1,200 of Shepard's own recordings, with no wrong pick (section 6). Nothing runs in
the game yet; the next step is section 8 step 1.

## 1. Words used

| Word | Meaning |
|---|---|
| entry | A dialogue node spoken by someone other than the player (`BioConversation.m_EntryList[i]`). |
| reply | A dialogue node the player "says" (`m_ReplyList[i]`). |
| wheel | An entry that offers at least one reply of type `REPLY_STANDARD`: the game shows the dialogue wheel and waits. |
| card | One reply on a wheel, as Thespian mode shows it: a **label** (the wheel's short text, the game's "paraphrase") and a **line** (the full sentence Shepard says). |
| menu card | A card with a label and no line (the rapid transit's "Wards Locations"). |
| group | Cards of one wheel whose lines are the same or nearly the same. The matcher chooses between groups. |
| take | A recording of a line: `M` (the male player's voice for Shepard's lines) or `F` (the female player's). |
| transcript | What the speech recognizer wrote for what the player said (a partial while speaking, a final at the end). |
| decided | The matcher is sure enough of one group to show it as the choice and to commit it at an end. |

## 2. How the game stores a conversation (found offline, 2026-11-12)

Read with Legendary Explorer's library through `tools/conversation-scan` (C#; build with `dotnet build -c Release` in
that folder; needs the Legendary Explorer nightly in `mod-manager/`, as the existing scan does).

1. **Conversations** are `BioConversation` objects in the English dialogue packages
   `BioGame/CookedPCConsole/*_LOC_INT.pcc` (490 packages hold the 490 conversations).
2. **Entries** (`m_EntryList`, struct `BioDialogEntryNode`): `srText` (string id of the line), `nSpeakerIndex` (-1 the
   conversation's owner, -2 the player, 0.. an index into `m_SpeakerList`), `bSkippable`, and `ReplyListNew`: the replies
   this entry offers, each a struct with `nIndex` (index into `m_ReplyList`), `srParaphrase` (string id of the label) and
   `Category` (`REPLY_CATEGORY_DEFAULT`, `AGREE`, `DISAGREE`, `FRIENDLY`, `HOSTILE`, `INVESTIGATE`: where on the wheel
   the reply sits).
3. **Replies** (`m_ReplyList`, struct `BioDialogReplyNode`): `srText` (string id of what Shepard says), `ReplyType`
   (`REPLY_STANDARD`: picked on the wheel; `REPLY_AUTOCONTINUE`: taken by itself, no wheel; `REPLY_DIALOGEND`),
   `eGUIStyle` (`GUI_STYLE_NONE`, `CHARM`, `INTIMIDATE`...), `bUnskippable`, `EntryList` (the entries it leads to),
   `nStateTransition`, `nScriptIndex` (its plot effects).
4. **Text.** `srText` and `srParaphrase` are string ids. The strings are in `BioTlkFile` objects **inside the same
   packages**, named `tlk` (the female player's text) and `tlk_M` (the male player's); the other languages' are
   `tlk_FR`, `tlk_M_FR`... Read with `new ME1TalkFile(export).StringRefs` (`StringID`, `Data`). A string id with no
   `Data` is genuinely empty: 6,723 of 11,238 replies are **pass-through replies with no text** (`REPLY_AUTOCONTINUE`
   after someone's line) and have no label either. That is not a lookup failure.
5. **The male and female text never differ for Shepard's own lines** (0 of 4,493). Other people's lines sometimes do
   (he/she).
6. **Recordings.** Each line with audio has a `SoundNodeWave` named `<conversation>:VO_<string id>_M` in the package
   (property `Duration`, seconds; `BioStreamingData` names the bank). The audio is in
   `BioGame/Content/Packages/ISACT/<bank>.isb` (the English bank has the conversation's name; `_de`, `_fr`, `_it`,
   `_plpc`, `_ra` are other languages). In a bank a sample is titled `EN_<conversation>_<string id, 8 digits>_M.wav` or
   `..._F.wav`: every line has an `_M` take; Shepard's lines also have an `_F` take (the female player's voice). The
   samples are Ogg Vorbis, mono, 44.1 or 48 kHz. Read with `new ISACTBank(stream).GetAllBankChunks()
   .OfType<ISACTListBankChunk>()` (`TitleInfo.Value`, `SampleData`), decode with `NAudio.Vorbis.VorbisWaveReader`
   (in Legendary Explorer's folder).
7. **Investigate is a wheel grouping, not dialogue nodes.** The investigate topics are ordinary replies with
   `Category = REPLY_CATEGORY_INVESTIGATE` in the entry's `ReplyListNew`. The wheel's handler
   (`BioSFHandler_Conversation`) files them under its sub-menu (`m_aInvestigateLocations[6]`,
   `m_bDisplayInvestigateSubMenu`, `m_srTextInvestigate`, `m_srTextReturn`, `GetInvestigateReplyFromWheelLocation`).

### The tools

```bash
# 1. Every conversation's lines, labels, categories and recording lengths -> scratch/thespian/lines.jsonl (4 s)
tools/conversation-scan/bin/Release/net10.0-windows/conversation-scan.exe "<game>/Game/ME1/BioGame/CookedPCConsole" scratch/thespian/lines.jsonl --wheels
# 2. The census (section 3), the matcher's cases, and a list of recordings to extract
python tools/thespian-census.py scratch/thespian/lines.jsonl --cases scratch/thespian/cases.tsv --vo-list scratch/thespian/vo-list.tsv --vo-sample 600
# 3. Those recordings as WAV -> scratch/thespian/vo/ (7 s for 1,200)
tools/conversation-scan/bin/Release/net10.0-windows/conversation-scan.exe "<game>/Game/ME1/BioGame/Content/Packages/ISACT" scratch/thespian/vo --vo-extract scratch/thespian/vo-list.tsv
# 4. The matcher over every wheel, text only (5 s)
build/Release/mevr-thespian-bench.exe scratch/thespian/cases.tsv --noise 0.15 --drop 0.05
# 5. The recognizer over the recordings, then the matcher over what it heard (about 1 s per recording)
python tools/thespian-vo-bench.py --arch small
```

Files: `tools/conversation-scan/Wheels.cs` (`--wheels`), `VoExtract.cs` (`--vo-extract`), `tools/thespian-census.py`,
`tools/thespian-bench/Main.cpp` (target `mevr-thespian-bench`), `tools/thespian-vo-bench.py`. Each file's header
comment documents its formats.

## 3. The census (`tools/thespian-census.py`)

| What | Count |
|---|---|
| Conversations | 490 |
| Wheels | 3,072 |
| Cards (authored; the game shows fewer at a time, see below) | 12,643 |
| Cards with a spoken line | 12,616 (4,493 distinct lines) |
| Menu cards (label only) | 27 |
| Wheels with investigate cards | 1,662 (3,995 investigate cards) |
| Charm / intimidate cards | 112 / 114 |
| Unskippable cards | 65 |
| Shepard's lines: words | median 9, 10th percentile 4, 90th percentile 18, longest 38 |
| Shepard's lines: recorded length | median 2.4 s, 90th percentile 4.9 s, longest 14.3 s, 3.4 hours in all |
| One-word lines | 28 (11 of them "Goodbye.") |
| Lines of two words or fewer | 116 |
| Lines with tokens (`<...>`, `[...]`) | 11 |
| **Lines Shepard says without a wheel** | **0** |
| The same line twice on one wheel, same outcome | 746 |
| The same line twice on one wheel, different outcome | 3 (all in `ice70_tartakovsky`, same label too) |
| Cards told apart by their first word / two / three | 79% / 92% / 96% of those that can be |

What follows from it:

- **Shepard never speaks without the wheel in LE1's conversations** (no `REPLY_AUTOCONTINUE` reply has text, no entry is
  spoken by the player). The plan's section 3.7 (dub-along, hold until performed) is not needed. Every line Shepard says
  in a conversation is a picked card.
- **A wheel in the data holds every reply its entry offers, up to 13.** At run time plot conditions hide some and the
  wheel shows at most six plus six investigate topics. The benches use the full authored set, so they are harder than
  play.
- **One line under two labels is common** (746 times) and harmless: the outcome is the same. The matcher treats such
  cards as one group and reports the first; the three cases with different outcomes also share their label, so they are
  alternatives a condition chooses between, never both shown.
- **The 65 unskippable replies** are committed without the skip: Shepard's recording plays (muted or not is the
  setting's choice); phase 1 lists them.

## 4. The matcher (`src/core/voice/ScriptMatch.hpp/.cpp`)

Game-independent, deterministic, unit-tested (`tests/ScriptMatchTests.cpp`, run by `mevr-tests`). The header's comment
is the specification; this is the same in steps, with the constants (`script_match::Settings`).

**Words.** `Tokens(text)`: lower case; a word is a run of ASCII letters, digits, apostrophes (also the typographic
U+2019 / U+2018) and bytes of 0x80 or more; everything else separates, including the dashes and the ellipsis of the
U+20xx block. Apostrophes at a word's ends are dropped. `DisplayWords(text)` gives the same words as written (for the
teleprompter); same count, same order.

**A prepared word** (`Word`, made by `MakeWord`): `normal` (apostrophes dropped, small numbers as words: "2" is "two"),
`stem` ("-ing", "-ed", "-es", "-s" off, then a final "e" off, then a doubled last letter single), `sound` (`SoundKey`: a
simplified Metaphone with b/p, d/t, g/k, v/f, z/s merged and vowels kept only at the start: "saren" and "sarin" are both
`SRN`), `stop` (`IsStopWord`: articles, pronouns, auxiliaries, prepositions, fillers; **not** yes, no, not, the negative
contractions or the question words), `weight` (0.25 for a stop word, 1 otherwise).

**Two words are the same** (`Same`) when: the normal forms are equal; or the stems are equal and at least 3 letters;
or, only if neither is a stop word: both are 5 letters or longer and their spelling similarity (1 - edit distance /
longer length) is at least 0.75; or both are 4 letters or longer and their sound keys are equal and at least 2 long.

**Aligning a transcript with a script** (`Wheel::Align`): the heaviest common subsequence (dynamic programming over
said words x script words; a match adds the script word's weight). The walk back takes the earliest script word each
said word can have, so the lit words run from the line's start. Result: `matched` (weight), `penalty` (0.5 x the weight
of said words that matched nothing), `frontier` (index of the last script word matched), `lit` (per script word).

**A card's score:** `matched - penalty` against its line and against its label; the better one is the card's (a tie
goes to the script more of which was said, so the label "The geth?" beats the line that ends in "the geth"). When the
player has stopped (`final`), each script also loses `0.5 x min(weight not said, 2.0)`.

**Groups:** two cards are one group when their lines share at least 85% of the heavier line's weight in order (or, for
two menu cards, their labels do). A group's score is its best card's.

**Decided** (`Result::decided`) when all hold:

1. evidence: the leader's `matched` is at least `min(2.0, 0.6 x its script's whole weight)`;
2. its score is at least 0.5;
3. it leads the runner-up group by 1.2 while speaking, by 0.6 once the player has stopped (alone on the wheel: no lead
   needed);
4. if its script has two words or fewer: only when stopped, only if the whole script was said, and the transcript has
   at most one word more than the script.

`Result` also gives per card `coverage` (share of the line's weight said), `frontier`, `reachedEnd` (one of the line's
last two words was matched), `lit`, `byLabel`. `Wheel::Keyterms(n)` returns the cards' names (words with a capital
inside a sentence) for the recognizer's bias.

**The transcript's side only:** run-together words are expanded ("outta" is "out of"), and the last word may be the
start of a script word (section 6's three rules).

**What the caller adds** (`core/vr/Thespian`, section 8 step 4; not built): when to commit. The matcher only says who
leads and whether that is safe.

## 5. The text bench (`mevr-thespian-bench`, every wheel, no audio)

11,774 lines read (one per group) over 3,072 wheels; 2026-11-12, the constants above.

| Test | Right | Undecided | Wrong |
|---|---|---|---|
| The whole line, at its end | 100.0% (11,771) | 3 | **0** |
| The same through a simulated recognizer (15% of words wrong, 5% dropped) | 92.0% | 8.0% | 0.01% (1) |
| Stopped half-way through the line | 68.5% | 31.4% | 0.05% (6) |
| The label alone | 96.7% | 3.3% | 0 |
| Another wheel's line (should be refused) | n/a | 99.3% refused | 0.73% accepted |

While speaking, the right card is first decided at word 2 for 11% of lines, by word 3 for 36%, word 4 for 68%, word 5
for 92%, and for 96% before the line ends; a wrong card is never decided on the way with exact text (9 times in 11,771
with the simulated recognizer).

Reading the table:

- **Half-way stops are undecided a third of the time by design**: half of a short line is one or two content words, under
  the evidence rule. The player just keeps reading. This is the number the "Relaxed" strictness would trade against
  wrong picks; it stays a setting.
- **The accepted "other wheel's lines" are mostly near-synonyms** ("I should go." for the card "I have to go.", "We'll
  talk later, Williams." for "We'll talk later."): the game's own lines share far more than stray talk would. The test
  is kept as the upper bound.
- The three undecided whole lines are a one-word "Hey." and two pairs that differ only by "What's" / "What is".

History of the rules (why they are what they are): the first version (no score floor, no unsaid-words penalty, a
coverage tie-break) accepted 2.97% of other wheels' lines and left 19 whole lines undecided because near-duplicate lines
tied. The score floor of 0.5 and the capped unsaid-words penalty at the end brought the false accepts to 0.73%; grouping
near-identical lines removed the ties.

## 6. The speech bench (`tools/thespian-vo-bench.py`)

The recognizer (Moonshine's streaming models, one thread as in the game, partials every 250 ms) over 1,200 of Shepard's
own recordings: 600 wheels picked at random (`--vo-sample 600 --seed 1`), one card each, both takes; 50 minutes of
audio. Then the matcher over what it wrote, against the wheel's full authored set of cards.

| Recognizer | Words misheard (male / female take) | Lines heard word for word | Right card | Undecided | Wrong card |
|---|---|---|---|---|---|
| small, no key terms | 4.0% / 4.9% | 73% / 69% | 99.1% (1,189) | 0.9% (11) | **0** |
| small, each wheel's key terms | 3.0% / 4.0% | 80% / 74% | **99.3%** (1,192) | 0.7% (8) | **0** |
| tiny, no key terms | 6.7% / 7.1% | 62% / 60% | 98.5% (1,182) | 1.5% (18) | **0** |

- **A quarter of the lines are not heard word for word, and the right card is still chosen 99 times in 100 with no wrong
  pick in 3,600 trials.** This is the result the mode rests on. The exit criterion of the plan's phase 0 (at least 97%
  right on whole lines, under 0.5% wrong) is met with either model.
- **While speaking** (the transcript's words fed one by one): the right card is first decided at word 2 for 12% of the
  recordings, by word 3 for 37%, word 4 for 66%, word 5 for 91%, and for 96% before the line ends. No wrong card is ever
  decided on the way. The same as with exact text (section 5): the recognizer's errors cost almost nothing.
- **Both voices alike** (small without key terms: 98.8% male, 99.3% female).
- **Cost on one thread** (the files are fed faster than real time, so only the per-call costs count): a partial costs a
  median of 28 ms with the small model (12 ms tiny); the worst partial of a file is under 371 ms for 95% of files (166
  ms tiny); the final text comes a median of 185 ms after the audio's end (78 ms tiny).
- **Each wheel's own key terms** (`--keyterms wheel`; `Wheel::Keyterms(12)`: 2,145 of the 3,072 wheels have any): the 253
  recordings not heard word for word on wheels with names were run again with them. Their word error rate fell from
  13.0% to 9.2%, 65 became word for word ("Farewell, she allig" became "Farewell, Shiala"; "Gary on press list" became
  "Gary on Pressly"), and the matcher gained three cards. One side effect was seen: with key terms the recognizer
  sometimes ends a line inside a word ("My name's Shep", "Let's have a"); the cut-word rule below covers it.
- **The 8 still undecided** (small with key terms): names the recognizer cannot spell even with the bias ("How many
  rachni are in here?" as "how many racknigh are in here", twice; "What is Sha'ira's?" as "what is your ears";
  "Jenkins. Take point." as "chickens hit point"); a recording that says more than its text ("Go on, I'm listening." for
  the two-word line "I'm listening."); two lines of stop words whose last word was cut ("I'll just be on my [way]",
  "Let's have a [look]"); one silence (a shout with no words). In play each of these is "say it again" or the stick.
- **Tiny against small:** 7 more undecided in 1,200 at half the cost. The size stays the player's setting
  (`[Voice] size`; small is the default).

Three rules came out of these runs (all in `ScriptMatch.cpp`, each with a unit test):

1. **Run-together words** (`kRunTogether`): the recognizer writes "I'm outta here" for "I'm out of here." The
   transcript's outta, gonna, wanna, gotta, kinda, sorta, lemme, gimme, dunno and cannot are replaced by the two words
   they stand for; a script that itself says "gonna" still matches (`Same` takes a script's run-together word for the
   first of its words).
2. **A cut last word** (`Word::cut`): the transcript's last word counts when it is the first three or more letters of
   the script's word. Partials end inside words all the time, so this also lights the teleprompter a word earlier.
3. **Key terms** (`Wheel::Keyterms`): capitalised words inside sentences, without a possessive 's ("Saren's" is
   "Saren"), plus the setting's own lower-case words when a card has them (`kLore`: geth, rachni, krogan, turian, asari,
   salarian, quarian, volus, hanar, elcor, batarian, keeper, biotic, husk, spectre...).

## 7. Phase 0: what is left

Tier 1 is measured and passes. Left, in this order; none of it blocks section 8's steps 1 and 2:

1. **Tier 2 (the player rewords the line).** Not measured yet. Build the test set: for 300 sampled cards, three
   rewordings each (write them with a language model offline; keep them in `scratch/thespian/rewordings.tsv` as
   `wheel id <TAB> card index <TAB> text`), and run them through (a) the matcher alone
   (`mevr-thespian-bench cases.tsv --utterances rewordings.tsv`), (b) a sentence embedding (cosine similarity between
   the rewording and each card's "label. line"; pick the best if it leads by a margin), (c) the llama.cpp tier under a
   grammar that allows only a card number or `none` (`core/voice/Grammar`, `mevr-llm-bench` as the pattern). Report right /
   undecided / wrong for each, and the time per turn. Ship the best that has under 1% wrong; if none does, ship tier 1
   only and keep "say the label" (96.7% right, section 5) as the short way.
2. **Strictness numbers.** For Relaxed / Performer / Method (plan 3.4), compute from the recordings' transcripts at
   which word each would commit and the wrong picks of each (extend `Read` in `tools/thespian-bench/Main.cpp` to record
   the leader's `coverage` at the first decided word and at the end).
3. **A real microphone.** The recordings are studio takes. A player's reading through a headset microphone will be
   worse; the simulated recognizer of section 5 (15% of words wrong, 5% dropped: three to four times the studio error
   rate) still gave 92% right and 0.01% wrong, which is the margin there is. Record 50 lines with the owner's headset in
   phase 2 (`tools/record.ps1` does not record audio; use the voice DLL's capture or `mevr-audio-loopback`'s pattern)
   and run them through `--utterances`.

## 8. Implementation guide (phases 1 to 4)

Build in this order. After each step: `cmake --build build --config Release --target MEVR-LE1-mod` (mod only) or the
host too where the step says so, `build/Release/mevr-tests.exe`, and the in-game check the step names (the skill
`fake-headset-run` and `tools/conversation-from-save.ps1` play scenes without a person).

### Step 1. Read the live wheel (mod only; no behaviour change) - **built 2026-11-12, [verify] in the game**

Files: `src/game/Thespian.hpp/.cpp`, namespace `mevr::game::thespian`, in the mod's source list in `CMakeLists.txt`
after `src/game/Voice.hpp`. `thespian::Tick(deltaSeconds)` runs in `src/mod/Mod.cpp` right after
`game::conversation_world::Tick(...)`; `thespian::Shutdown()` beside `sound_place::Shutdown()`. The live conversation
and the wheel's handler come from two accessors added to `game/ConversationWorld.hpp`: `LiveConversation()`
(`BioWorldInfo.m_oCurrentConversation`) and `LiveWheelHandler()` (the conversation movie's `BioSFHandler_Conversation`).
The console verb is `thespian` in `src/game/ConsoleRequest.cpp`.

**What the wheel handler's script says** (`BioSFHandler_Conversation`, decompiled locally with Legendary Explorer and
read, 2026-11-12; nothing of it is in the repository). These settle questions (a) to (d) of the first draft of this
step without the game:

- (a) **Every reply function of `BioConversation` takes a POSITION in `m_lstCurrentReplyIndices`** (0 .. count-1), not
  the reply's index in `m_ReplyList`: `GetReplyText(nIndex, bParse)`, `GetReplyParaphraseText(nIndex)`,
  `GetReplyCategory(nIndex)`, `GetReplyGUIStyle(nIndex)`. `GetNumReplies()` is that array's length. The element's value
  is a place in the current entry's `ReplyListNew`, whose `nIndex` is the reply's index into `m_ReplyList`
  (**corrected 2026-11-13 in the game**: the hearing's wheel #10 lists 0, 1, 2 where its replies are 12, 11, 12; this
  note said the value was the index itself; `m_nSelectedReply` is a place too). `game/Thespian`'s `ReplyNode` maps a
  place to the reply. Never call them with a position the list does not have, nor while the conversation is being
  ended (section 10).
- (b) **The investigate topics are in the current list**, each with category 5 (`kCategoryInvestigate`). With two or
  more of them the main wheel shows one "Investigate" slot (the handler's `m_srTextInvestigate`) that opens the
  sub-menu; with exactly one the topic stands on the main wheel itself. For us they are ordinary cards.
- (c) **A wheel location holds a category, not a reply.** `m_aReplyLocations[location]` is a category;
  `SelectConversationEntry(location)` takes the FIRST current reply with that category (in the sub-menu:
  `GetInvestigateReplyFromWheelLocation`), refuses GUI style 4, handles the sub-menu switch
  (`ChangeConversationMenu`), and then calls **`BioConversation.QueueReply(position)`**. If that returns false it
  calls `BioWorldInfo.EndCurrentConversation()`. So a pick, whatever the slot, ends in `QueueReply(position)`: Thespian
  calls it directly and needs neither wheel locations nor the sub-menu. Consequence: a second non-investigate reply
  with a category an earlier one has can't be picked on the wheel; it is "shadowed" and is no card.
- (d) **An unavailable reply** (a charm or intimidate without the skill) is in the list with
  `GetReplyGUIStyle(position) == 4` (`GUI_STYLE_ILLEGAL`); the handler plays its error sound and takes nothing. It is
  no card.
- The handler's `Update` shows the wheel while the conversation's `NeedToDisplayReplies()` holds and hides it
  otherwise. Its GUI command 2 is `SelectConversationEntry` followed by `SkipNode()` on the conversation (a pick made
  while the other person still talks cuts the rest of that line); command 4 selects without the skip. The handler's
  `SkipConversation()` is `m_oCurrentConversation.SkipNode()`.

Names the module resolves (each has a `ReflectCheck` row, `engine/ReflectCheck.cpp` under "game/Thespian"):

```cpp
ue3::FieldRef    gCurrentReplies{"SFXGame.BioConversation", "m_lstCurrentReplyIndices"};   // TArray<int>
ue3::FieldRef    gReplyList{"SFXGame.BioConversation", "m_ReplyList"};  ue3::FieldRef gReplyUnskippable{gReplyList, "bUnskippable"};
ue3::FunctionRef gGetReplyText{"SFXGame.BioConversation.GetReplyText"};                    // (int nIndex, bool bParse) -> string
ue3::FunctionRef gGetReplyParaphrase{"SFXGame.BioConversation.GetReplyParaphraseText"};    // (int nIndex) -> string
ue3::FunctionRef gGetReplyCategory{"SFXGame.BioConversation.GetReplyCategory"};            // (int nIndex) -> int
ue3::FunctionRef gGetReplyGuiStyle{"SFXGame.BioConversation.GetReplyGUIStyle"};            // (int nIndex) -> byte
ue3::FunctionRef gQueueReply{"SFXGame.BioConversation.QueueReply"};                        // (int nReply) -> bool
ue3::FunctionRef gSkipNode{"SFXGame.BioConversation.SkipNode"};                            // () -> bool
ue3::FunctionRef gEndCurrentConversation{"SFXGame.BioWorldInfo.EndCurrentConversation"};
// state: BioConversation m_pSpeaker, m_pConversationPlayer, m_nCurrentEntry, m_nSelectedReply, m_nDelayedReplyChoice,
//        m_fRemainingVOLength, m_fShowRepliesOffset; BioSFHandler_Conversation m_bDisplayingWheel, m_bDisplayInvestigateSubMenu
```

A card (`ReadLive()`), one per position: `position`, `reply` (the element's value), `category`, `style`, `label`
(`GetReplyParaphraseText`), `line` (`GetReplyText(position, true)`: tokens resolved, as the subtitle shows it),
`unskippable`, and `candidate`: false for style 4 ("unavailable"), for a non-investigate reply whose category an
earlier position has ("shadowed"), and for a reply with neither label nor line ("nothing to say").

Commands (`tools/console.ps1 "mevr thespian ..."`):

| command | what it does |
|---|---|
| `mevr thespian` or `mevr thespian cards` | the live wheel: `thespian: wheel up, N replies; show-replies offset X s, the current line's recording has Y s left; investigate sub-menu on/off`, then a line per position: `[pos] reply R category C style S [unskippable] [NOT A CARD: why]: "label" -> "line"` |
| `mevr thespian watch on` / `off` | the same dump written to the log each time a wheel comes up (for unattended runs) |
| `mevr thespian say <words>` | step 2 |
| `mevr thespian pick <position>` | step 2, without the matcher |

**[verify] in the game** (the answers go in section 9):

- (a2) That the dump's labels and lines are the ones the wheel shows (confirms (a) live): compare `cards` with a
  snapshot of the wheel on a plain wheel, one with investigate topics and one with a charm option.
- (e) How long before the other person's line ends the wheel comes up: the `cards` line's two numbers
  (`m_fShowRepliesOffset`, `m_fRemainingVOLength`) with `watch on`.

### Step 2. Commit by typing (mod only) - **built 2026-11-12, [verify] in the game**

`mevr thespian say <words>`: a `script_match::Wheel` from the candidate cards (label and line), `Match(words, true)`
(final: the turn is over). Not decided: nothing is taken; the reply is
`thespian: "<words>" decides no card (leader [pos] "label", margin M; scores [pos] s, ...)`. Decided: `Commit`.

`Commit(live, position, how)` does what the handler's pick does, no more:

1. Only while `m_bDisplayingWheel` is set (a pick between lines crashed the game on 2026-11-06, research/59) and only
   a candidate card.
2. `BioConversation.QueueReply(position)` through `ue3::runtime::Call`.
3. If `m_bDisplayInvestigateSubMenu` was set, clear it (journalled `Thespian.investigateSubMenu`), so the next wheel
   opens on the main level.
4. `QueueReply` returned false: `BioWorldInfo.EndCurrentConversation()`, as the handler does; a warning is logged.
5. Else a pending commit starts: `thespian: <how> -> position P (reply R) "label" queued; its line is skipped when it
   starts` (or `; its line is unskippable and plays`).

The pending commit (`TickPending`, every tick until the next speaker, 15 s at most):

- Names the conversation's player actor (`m_pConversationPlayer`) to `engine/VoicePlace` with gain 0
  (`voice_place::Publisher::Thespian`, the fourth list; `kPublishers = 4`), so Shepard's recording is silent from the
  commit on. Needs the voice-place hook installed (it is for every player on foot and in first-person conversations).
- In the first tick in which `m_pSpeaker == m_pConversationPlayer` and the wheel is down: `SkipNode()` once, unless the
  reply's node is `bUnskippable`. Log `thespian: +N ms: Shepard's line started: SkipNode skipped it | was refused`.
- When the speaker is no longer the player: `thespian: +N ms: the next line began; the commit of position P is done
  (skipped|played; Shepard's recording was silenced | no source of Shepard's was seen playing)`, the voice list is
  cleared.
- Meanwhile every change of (wheel, speaker, entry, selected reply, delayed reply) is one log line
  `thespian: +N ms: wheel down, speaker the player, entry E, selected reply S, delayed reply D; recording left X s`:
  the trace that answers the [verify] items below.

`mevr thespian pick <position>` is the same commit without the matcher. `StatusJson()` counts wheels, commits, skips
and refusals (not yet in the snapshot's `"mod"` block).

**[verify] in the game** (the trace settles each; write the answers in section 9):

- (f) `m_pSpeaker == m_pConversationPlayer` while Shepard's reply line plays (the skip's trigger). If the trace never
  shows "speaker the player" after a commit, find what marks the reply's line (`m_nSelectedReply` or the entry
  changing) and trigger on that.
- (g) `SkipNode()` then returns true and the next entry starts at once (the time from "Shepard's line started" to "the
  next line began" should be a tick or two); nothing of the line's plot effects is lost (the plot check below).
- (h) Shepard's source is owned by the player's actor, so the gain-0 voice silences it: the "done" line says
  `Shepard's recording was silenced`, and `build/Release/mevr-audio-loopback.exe out.wav 6` over the commit with
  `python tools/audio-balance.py out.wav` shows no speech between the commit and the answer.
- (i) A wheel with investigate topics: `say` a topic's line from the main level takes it (no sub-menu step), and the
  next wheel is the main one.
- (j) The plot after a typed commit equals the plot after the same pick by stick: quicksave after each way and compare
  with `python tools/le1-save.py diff`. **Done 2026-11-13: equal** (section 9). The stick's side is
  `mevr thespian stick <position>`: `SelectConversationEntry` on the wheel location that holds the card's category.

Check: scenes from validation saves (`tools/conversation-from-save.ps1`); in each, `cards`, then one card's line, then
(next wheel) a label, then nonsense (must refuse).

### Step 3. The open microphone (voice DLL, host, mod) - **built 2026-11-13**

The recognizer's listen was already "open" in everything but name: it runs until told to stop and the mod joins its
lines. What Thespian mode needed more, and got (`MEVR_VOICE_API_VERSION 3`; host and `ASI\voice` deploy together):

- `MevrVoiceListen(2)` (`src/voice/Voice.cpp` `ApplyListen`): the same stream **without ducking the game's audio** (the
  other person is still talking when the wheel comes up). `1` (the radio's push-to-talk) wins over `2` and ducks; `0`
  ends either. The host passes the mode through (`plugin/Voice.cpp` `SetListen(int)`, `Apply(int)`: 0, 1 or 2); the mod
  asks with the existing `HostServices::setVoiceListen(2)`.
- `vr::voice::State` grew at its end (`core/vr/Voice.hpp`): `silentMs`, the time since the last chunk of sound at
  speech level, and `open`. The DLL's `NoteLevel(level)` keeps the clock: speech is a chunk whose RMS is at least
  `max(0.012, 3 x noise floor)`; the floor falls to the level at once and creeps up 0.4% a chunk. The clock starts at the
  listen's start.
- **A file fed during an open listen is heard in the microphone's place** (`MevrVoiceFeedFile`: the microphone's chunks
  are left out while it plays, its own chunks drive the level and the quiet clock, and the listen goes on after it). That
  is the whole test path without a voice: `mevr thespian feed <wav>`.

Mod (`game/Thespian.cpp`, "The spoken turn"): when a wheel comes up with the mode on and the recognizer ready,
`BeginTurn` reads the cards once, builds the matcher's `Wheel`, sends its `Keyterms(12)` to the recognizer and asks for
the open listen; `TakeLines` keeps the listen's lines by id (a partial replaces its line), joins them, and matches the
joined text twice whenever it changes (`final = false` and `final = true`). `game/Voice` stands down meanwhile
(`thespian::OwnsMicrophone()`: no line taken, no roster key terms; they are sent again after) and never reads a line of a
listen Thespian opened (`thespian::OwnsListen(listenStartMs)`), so Shepard's line is never parsed as a squad order.

### Step 4. The turn's rules (`src/core/vr/Thespian.hpp/.cpp`, `tests/ThespianTests.cpp`) - **built 2026-11-13**

`th::Step(State&, Settings, Input) -> Output`, a pure function called every tick while a wheel is up. Input: `wheelUp`,
`otherSpeaking` (`BioConversation.m_fRemainingVOLength > 0.05`: the wheel comes up about 2.5 s before the other person's
line ends, measured), `silentMs`, the matcher's two answers reduced to `Match {words, leader, decided, reachedEnd,
coverage, byLabel}`, `dt`. Output: `phase` (Off, Armed, Hearing, Leading, Committed), `mic` (Off, Armed, Hearing,
Understood, Unsure), `commit` (a card, this tick), `leader` (the row to light), `restart` (drop the words so far).

| Situation | What happens |
|---|---|
| The wheel comes up | Armed; `restart` (a fresh transcript) |
| Words, no card decided | Hearing |
| A card decided | Leading (its row lit); nothing taken while speech goes on |
| Decided, the line's end reached, quiet >= `endMs` (200), the strictness allows it | **commit** |
| Quiet >= `pauseMs` (900) and the matcher asked "the player has stopped" is decided and allowed | **commit** |
| Quiet >= `pauseMs`, nothing to take | the mark says "Didn't catch that" for 2 s; listening goes on |
| Quiet >= `staleMs` (3500), nothing to take | `restart`: the words are dropped |
| The other person still speaks | never a commit; a decided line stands and is taken once their line has ended |
| Their line ends with words heard and no card decided | `restart` (their voice in the microphone, an aside) |
| After a commit | Committed until the wheel goes down; still up after 3 s: Armed again with a fresh transcript |

Strictness (`Allows`): `relaxed` decided; `performer` decided and (coverage >= 0.5 or matched by the label); `method`
decided and coverage >= 0.9. A restart does not close the stream: the mod notes how many lines there are and how many
words the last one has (`Rebase`), and only what comes after counts (`Counted`).

### Step 5. The teleprompter (host; host and mod deploy together) - **built 2026-11-13**

The panel the mod sends each tick is `vr::thespian::Panel` (`core/vr/Thespian.hpp`; HostServices v39 `setThespian`,
`thespianPanelSize`): `shown`, `mic`, `level`, `serial` (a new wheel), `anchored` + `speakerHead` (the other person's head
bone **in app space, metres**, converted by the mod with `engine::ToTracking`; only in a first-person conversation),
`speaker`, `heard` (`BioConversation.GetEntryText(true)`), and up to 12 `PanelCard {label, line, tone, look, lit (a bit
per word of the line), next}`, the wheel's replies first (`mainCount`), then the investigate topics; an unavailable reply
is a greyed row, shadowed and empty ones are left out.

Shared by the host and the offline preview (`core/vr/Thespian.cpp`): `RowsRml(panel)` (the rows' markup: `div.row <tone>
<look> [topic]` > `div.tag` > `div.label`, `div.bar`, `div.line` with one `span` per word, `said` / `next` / none; what
stands before a word is inside its span, because the renderer drops a text node of spaces alone), `PromptText`,
`MicClass`, `PlacePanel(eyes, headForward, hasSpeaker, speakerHead, heightM)`.

Host: `src/plugin/Thespian.hpp/.cpp` (namespace `mevr::host::thespian`; the pattern is `plugin/OmniPad`): a third RmlUi
context on `assets/menu/thespian.rml` / `.rcss`, a 1280 x 880 canvas, one evictable swapchain ("thespian panel",
snapshot layer `thespian`), the quad cut to the panel's own height, 0.90 m wide, 1.0 m from the eyes, its top edge 15
degrees under the line to the speaker's head (14 under the horizon without one), facing the eyes, placed once per wheel.
It redraws only when something it shows changed (the level in steps of 4%). The taken row's flash stays 250 ms after the
mod's panel is gone. It takes no input. While it stands, the first-person mode's wheel quad at the hand is not drawn
(`HalcyonOverlays`: `convoWheelWanted ... && !thespianPanel`); on the 3D screen the game's wheel stays on the screen's UI
layer and the panel stands below it. Offline look: `build\Release\mevr-menu-preview.exe <folder>` writes
`thespian-idle|hover|leading|committed|unsure|topics.png`.

**The game's own wheel is hidden while the panel stands** (2026-11-13, the owner: "when we have our panel I think we need
to hide the game's dialogue display"). The wheel is the conversation movie's Flash. The handler (`BioSFHandler_Conversation.Update`,
read in the decompile) calls `UpdateConversationOptions(conversation)` once when replies are needed (its
`setConversationOptions`, which shows the wheel) and the movie's `HideWheel` once they no longer are, and in between only
moves the arrow. So `BeginTurn` calls `oPanel.InvokeMethod("HideWheel")` on the handler's panel (`Engine.BioSFHandler.oPanel`,
`Engine.BioSFPanel.InvokeMethod`) and never touches `m_bDisplayingWheel`: the handler goes on as if the wheel were up, takes
the stick as ever, and hides the wheel itself after a pick. The HideWheel also takes the last line's subtitle with it while
the wheel is up; the panel's header carries that line. **The left stick brings the game's wheel back** (its push past half
way: the handler's own `UpdateConversationOptions`), and the panel then folds away (`shown = 0`; one set of replies at a
time, and in first person the game's wheel quad stands where the panel would); the microphone still listens. A turn that
ends with the wheel still up and nothing taken (the mode off, the recognizer gone, the mod unloading) shows the game's wheel
again. `[Thespian] hideWheel = 0` keeps the game's wheel. `Panel::gameWheel` (a spare byte, no size change) tells the host;
`host::thespian::HidesGameWheel()` keeps the first-person wheel quad down only while the game's wheel is hidden. Console:
`mevr thespian wheel show | hide` (the stick's part without controllers). The panel's hint reads "or move the left stick
to choose by hand".

Not built: the hover look from gaze or a pointer (the `hover` class exists; nothing sets it), a curved panel.

### Step 6. The rest - **built 2026-11-13, except tier 2**

- Settings, `[Thespian]` in `ASI\LE1MEVR.ini`, read by the mod every second: `on` (0), `strictness` (performer),
  `endMs` (200), `pauseMs` (900), `staleMs` (3500), `handPick` (voice | silent), `keyterms` (12). The menu's Voice page
  has the group "Thespian mode (experimental)": Speak Shepard's lines, How much of the line, A reply picked by hand
  (`core/vr/MenuSettings` `thespian_on`, `thespian_strictness`, `thespian_hand`; `plugin/MainMenuSettings`).
- A pick by hand while the mode listens ends the turn; Shepard's recording plays as ever, or with `handPick = silent`
  it is silenced and skipped like a spoken one.
- Stand-downs, each with one log line: no recognizer (`thespian: the spoken mode is on but there is no recognizer`),
  a wheel with nothing to read or whose text isn't English (`LooksEnglish`: under one common English word in twelve).
- Console: `mevr thespian on | off | auto` (this session), `status`, `feed <wav>`.
- Tier 2 ("said in your own words") is not built: with five to ten differently worded replies the alignment decides
  (the owner, 2026-11-12), and the label is the short way.

## 9. Log

- **2026-11-12.** The plan. Phase 0: the line dump, the census, the matcher with its unit tests, the text bench over
  every wheel, the extractor for Shepard's recordings and the speech bench. Findings: no unchosen Shepard lines; investigate
  is a wheel grouping; 746 same-line pairs with one outcome; Shepard's recordings are per-conversation Ogg banks with
  both voices. Results: text, whole line 11,771 of 11,774 right, 0 wrong; speech, small model with per-wheel key terms
  1,192 of 1,200 right, 8 undecided, 0 wrong. Builds: `mevr-tests` passes, `build-trilogy` builds.
- **2026-11-12, later.** Steps 1 and 2 built, mod only (`src/game/Thespian`; console `mevr thespian`). The wheel
  handler's script, read offline, settled the first draft's questions (a) to (d): the reply functions take a position
  in the current list; investigate topics are in that list (category 5); a wheel location holds a category and a pick
  ends in `BioConversation.QueueReply(position)`; an unavailable reply has GUI style 4. So a commit is `QueueReply`
  directly, with no wheel location and no sub-menu step. Builds: `MEVR-LE1-mod`, `mevr-tests` pass, `build-trilogy`
  builds. Nothing run in the game yet: (a2) and (e) to (j) are open.
- **2026-11-12, night. Steps 1 and 2 in the game** (fake headset, `tools/fake-headset-session.ps1` with a driver script;
  John02's validation saves 89 Avina and 83 Anoleis; first-person conversations). The [verify] items of steps 1 and 2:
  - (a2) `cards` shows the wheel's labels and the lines Shepard says. Confirmed against the snapshot's wheel.
  - (e) **The wheel comes up 2.5 to 3.3 s before the other person's line ends** (`m_fShowRepliesOffset` 2.00 s; the
    recording had 2.47 to 3.34 s left at each wheel). A player who starts reading at once reads over them: the turn's
    rules wait for their line's end.
  - (f) `m_pSpeaker == m_pConversationPlayer` while the reply's line plays: yes. But a reply queued while the other
    person's last words still play (`m_nDelayedReplyChoice` holds it) has the player as the speaker one tick BEFORE its
    line begins (`m_nSelectedReply` still -1), and `SkipNode()` then returns false. Fixed: the skip waits for
    `m_nSelectedReply >= 0` and is tried up to six ticks.
  - (g) `SkipNode()` on the started line returns true; the next entry's line began 93 to 141 ms after the skip
    (125 to 172 ms after the commit).
  - (h) With the skip refused (the bug above) the line played 1.5 and 3.3 s and the log said `Shepard's recording was
    silenced`: the gain-0 voice on the player's actor does silence it. With the skip working no source of Shepard's is
    ever seen playing.
  - (i) An investigate topic's line said from the main wheel took the topic (Anoleis: the next wheel was the topics').
  - (j) The plot against a stick pick: **not run**. The commit is the call the wheel's own handler makes and the skip is
    the player's own skip; [verify] with two quicksaves and `python tools/le1-save.py diff` before the mode leaves
    "experimental".
  - Typed nonsense was refused on both wheels (scores under 0, no leader or margin 0.12).
- **2026-11-13. Steps 3 to 6 built; the spoken turn in the game** (the same scenes; a synthetic voice, Windows' own
  speech synthesizer at 16 kHz, fed into the open listen with `mevr thespian feed`; recognizer `small`):
  - A whole line: `leading` 1.3 to 1.4 s into the speech; taken at **quiet 203 and 219 ms** after the last word; the
    other person's line began **125 and 141 ms** after the commit. So the gap from the player's last word to the answer
    is about 0.35 s. The recognizer wrote "program" as "person" once; the line was still taken (71% of it).
  - A label alone ("Not interested.", "Goodbye."): taken at quiet 906 and 907 ms (the pause rule), the answer 172 ms
    later.
  - A pick by hand (`mevr convo pick`) while the mode listened: the turn ended, the game went on as ever.
  - Two things the run showed, fixed: the recognizer writes "you" for a quiet room now and then (a transcript of the
    commonest words alone no longer counts as words, unless it is a reply itself); a conversation's start stalls the
    game thread over half a second and the host took the panel for gone (its staleness limit is 1.5 s now).
  - The panel drew the real wheel (the speaker's name and line from the game, four rows with the investigate topic
    under the divider). Its place in that run was wrong (the mod sent the speaker's head in world units; it sends room
    metres now).
  - Builds: `MEVR-LE1`, `MEVR-LE1-mod`, `MEVR-Voice`, `build-mono`, `build-trilogy`; `mevr-tests` pass.
- **2026-11-13, the run after the fixes** (Anoleis; snapshot of the eyes): the panel stands under the line to Anoleis
  (5.5 m away), at (0.22, -0.52, -0.83) m from the room's origin, the row being read lit with its said words bright and
  the next one marked. A whole line: taken at quiet 219 ms, the answer 125 ms later. Talk that is no reply: dropped,
  nothing taken. Six of ten words of a line, with "an asari" heard as "in a seri": the row lit, **not taken** (just
  under half the line's weight for `performer`); the mark now says "Keep reading..." in that case (`Mic::More`) and the
  words are dropped after `staleMs`. The game's own wheel stays visible on the HUD panel behind the panel's top edge
  (the stick's pick needs it). Open for the headset: whether the panel reads as square to the eyes (in the snapshot's
  wide projection it looks skewed at the image's corner), its height, and a real microphone.
- **2026-11-13, (j) the plot against a stick pick: equal.** Avina (save 89) and Anoleis (save 83), each played twice
  from the same save to the conversation's end, a quicksave after each: once with `mevr thespian say <the line>` on
  every wheel (QueueReply, the line skipped and silenced), once with `mevr thespian stick <position>` for the same
  cards (the handler's `SelectConversationEntry(5)` / `(0)`, Shepard's line skipped by hand). `python tools/le1-save.py
  diff`: **0 booleans, 0 integers, 0 floats** differ in both; only the map's saved level bytes (which differ between
  any two saves). Two conversations, four replies; a reply whose line fires a plot effect at its END is the case a
  skip could touch, and the player's own skip has the same path.
- **2026-11-13, a commit of mine broke main for an hour**: 9d19da39 staged my hunks of files other sessions edit with a
  zero-context patch (`git apply --unidiff-zero` in a temporary index), which lands hunks lines off when the other
  session's uncommitted hunks stand above them: two source lines after CMakeLists.txt's last `endif`. Put right in
  b07cdc10. For next time: build each shared file's blob from the working copy with the others' hunks taken out by the
  working copy's own line numbers (never a zero-context apply onto HEAD), and compare its lines with the last commit's.
- **2026-11-13, on the 3D screen** (`mevr convo world screen`; Avina; a snapshot of the eyes): the panel stands
  squarely under the cinema screen, ahead of the head (`thespian panel: shown at (0.00, -0.40, -0.91) m ... ahead of the
  head (no speaker in the room)`), the game's own wheel on the screen above it. Both replies were taken by the synthetic
  voice (quiet 203 and 359 ms; the answer 141 ms after the commit). Seen there: "Give me the tour." heard as "Give me
  the." was taken as read to its end (one of its last two words was said, 60% of the line): fine for a four-word line.
  With this, everything that can be checked without a person has been; what is left is the owner's own voice and eyes.
- **2026-11-13, the game's wheel hidden under the panel** (the owner's request; fake headset, Anoleis in first person
  and Avina on the 3D screen, snapshots of the eyes): with the panel up neither the game's wheel nor its subtitle shows,
  in either mode; `wheel show` brings both back (the game's wheel quad in front in first person, the wheel on the 3D
  screen's UI layer), the panel folds away, and the stick's pick (`mevr thespian stick 0`) takes the reply. Spoken
  replies were taken as before with the wheel hidden (a whole line at quiet 219 ms, labels at the pause) and after it was
  shown again. Two things to know: the recognizer's stray "you" in a quiet room was taken together with a label ("you
  Goodbye.", fine), and how the left stick's first push feels in the headset is [verify].
- **2026-11-13, the owner's first session in the headset; misheard names** (section 10): the mode "working pretty well"
  but missing game words (Saren, Eden Prime) and one pick felt Renegade where Paragon was meant. Built: names to the
  recognizer, replies as choices, the doubt rule, sound-alike words (2a7768a8); and a crash guard (57e298c3) after two
  game crashes in another session's runs.

## 10. Misheard names (2026-11-13, after the owner's first headset session)

**What the owner reported**: the mode works but misses words specific to the game ("Saren", "Eden Prime"), and a
reply was taken as Renegade where Paragon was meant. Asked whether a training session (the player reads lines; the
misheard words are mapped to the right ones) is needed.

**What their session's log showed** (10:36 to 10:39: the Council's hearing, `sta90_council_saren_dlg`, then Anderson,
`sta90_captain_dlg`; the game log was overwritten by later runs, the lines are in this session's transcript). Six spoken
picks; each took the line that was read:

| Heard | Taken (wheel, position, reply) |
|---|---|
| "Saren despises humanity that's why he attacked Eden farm" | #10, 0, reply 12 ("Saren despises humanity. That's why he attacked Eden Prime!"), 87% |
| "Cairns hiding behind his position as a spectre, you need to open" | #14, 0, reply 16 ("Saren's hiding behind ..."), 76% |
| "You've made your decision. I won't waste" | #19, 0, reply 22, 79% |
| "Tell me about the sister between you and me." | captain #2, 0 ("Tell me about this history between you and Saren."), 57% |
| "What's our next step" | captain #4, 0, 100% |
| "I should go." | the line itself, 100% |

- On the hearing's wheels several labels lead to one reply: #10's positions 0 (AGREE "Insults? Typical.") and 2 (DEFAULT
  "See? He hates us!") are both reply 12; #19's three labels are all reply 22. Whatever label the panel showed lit, the
  game's outcome is the same. #14's AGREE (reply 16) carries transition 4029 (GlobalInt 47 +2; which counter int 47 is:
  [verify]). Which pick felt Renegade is open (asked the owner).
- **The cause found**: `Wheel::Keyterms` skipped a capitalised word at a sentence's start (to keep "Tell", "What" out),
  so "Saren" in "Saren despises humanity." and "Saren's hiding ..." never reached the recognizer on those wheels. Over
  the census, 1,374 of 3,072 wheels had a name only at a sentence's start (Benezia 50, Saren 37, Sovereign 31, Williams
  27, Anoleis 26, Feros 23 ...).

**Training (enrollment) is not the first lever.** Reading passages so a dictation model adapts to one voice is the old
approach; here the recognizer only has to tell 2 to 6 known lines apart, and a misheard word matters only when it is the
word that tells two cards apart. The order chosen: (1) the names to the recognizer, (2) never decide on a word only heard
alike, (3) match words by sound; later, if needed: (4) per-player aliases learned from the replies taken (a confusion
that keeps coming back, used only to match against the cards, never as the transcript), (5) score each card's line
against the audio in the recognizer's decoder (needs its scores; whether Moonshine's C API gives them: [verify]), (6) no
fine-tuning of the speech model on a player's voice.

### What was built (2a7768a8, 57e298c3)

1. **Names to the recognizer** (`Wheel::Keyterms(count, context)`, `core/voice/ScriptMatch.cpp`): a capitalised word at
   a sentence's start is a name when it is one elsewhere on the wheel (capitalised inside a sentence), in `context` (the
   other person's line, `gTurn.heard`) or in the game's names (`IsGameName`, `src/core/voice/GameNames.inc`: 274 names,
   generated by `python tools/thespian-census.py scratch/thespian/lines.jsonl --names src/core/voice/GameNames.inc`; a
   word written with a capital inside a sentence at least twice and four times as often as in lower case: "Prime" is in,
   once "prime condition"). A run of names with only spaces between is one term ("Eden Prime", "Matriarch Benezia",
   "Zhu's Hope": Moonshine's key terms take phrases, `moonshine-c-api.h`'s example "Anushka Sharma"). The hearing's
   wheels gave "Eden,Prime" and "Spectre,Council"; now "Saren,Eden Prime" and "Saren,Spectre,Council". Wheels with any
   key term: 2,145 before, 2,263 now. Not used: `moonshine_transcriber_set_context` (the library picks a passage's unusual
   words by its tokenizer; it would need the voice DLL's interface to grow).
2. **Replies are the choices** (`Card::reply`, the game's reply index; the bench passes the census's): cards with the
   same reply are one group; different replies are one group only when their lines are word for word the same (the
   census: identical lines on one wheel lead to the same reply 737 times, to different replies with one outcome 9 times,
   to different outcomes 3 times, Tartakovsky's plot-conditional twins); lines a word apart under different replies are
   two choices, so neither is decided by its line and the label tells them apart. The panel shows one row per reply, the
   labels joined with " / ", coloured only when they agree (`game/Thespian` `SendPanel`).
3. **How surely two words are one** (`Quality`, `Compare`, `WordQuality`): Exact (as written, a stem, a run-together
   spelling), Close (spellings 0.75 alike, five letters or more; equal sound keys, four letters or more; a cut last word),
   Loose, new: sound keys one edit apart (each three or more), spellings at least half alike (`Settings::looseSpelling`
   0.5), both four letters or more: "cairns" for "saren's", "sister" for "history"; not "farm" for "prime" (0.4).
4. **The doubt rule** (`Wheel::Doubtful`, run on a decided leader with a runner-up): if the lead holds with only Exact
   matches, it stands. Otherwise each word heard alike that the runner-up doesn't use must be Close and nearer the
   leader's word than any content word of the runner-up's by `clearGap` (0.2), with no Close or Loose match there; else
   `decided = false`, `doubtful = true`, `doubtHeard` / `doubtWord` name it. The turn's rules (`core/vr/Thespian`
   `Match::doubtful`): at the pause the mark says "Not sure which: read it again or say its label" (`Mic::Doubt`, RCSS
   class `unsure`) and the words are dropped (`out.restart`) so the second reading stands alone. Log `thespian: not sure
   which: "<heard>" would be [n] "<label>" but "<word>" was only heard like "<card's word>"`; `mevr thespian status`'s
   `not sure which` count, `StatusJson`'s `notSure`.
5. **A crash guard** (57e298c3): two game crashes in another session's runs (11:26 Benezia, 11:33 Liara; the owner's
   `[Thespian] on = 1`) were the mod calling `BioConversation.GetReplyText` 57 and 82 ms after `mevr convo end`, during a
   line: the ending conversation showed a wheel for a moment while its lists went away (`MassEffect1.exe+0xC80E72`
   reading 0x10, `+0xC80BF4` reading a freed block). `ReadLive` calls the reply natives only while the wheel is shown,
   the conversation has a current line (`m_nCurrentEntry >= 0`) and every current reply is in `m_ReplyList` (else
   `thespian: a wheel without its lists ...: not read`, five times at most); a turn begins only on a wheel that stood
   three ticks on the same conversation, line and replies (`gSteady`).

### Measured (2026-11-13)

Commands: `build/Release/mevr-thespian-bench.exe scratch/thespian/cases.tsv --noise 0.15 --drop 0.05` (`--replies 0`:
the old grouping), `... --foreign 0 --soundalike 0.3 --soundalike-out <file>` and the old binary `--utterances <file>` on
the same transcripts, `python tools/thespian-vo-bench.py --arch small --keyterms wheel --tag=-names`.

| Test | Before (6eb231a6) | After (2a7768a8) |
|---|---|---|
| Whole line, at its end | 11,771 of 11,774 right, 0 wrong | 11,777 of 11,784 right, 7 undecided, 0 wrong |
| Simulated recognizer (15% wrong, 5% dropped) | 92.0% right, 1 wrong | 91.5% right, 1 wrong (92.0% with `--replies 0`) |
| The label alone | 96.7% | 96.5% |
| Another wheel's line (should be refused) | 0.73% accepted | 0.68% accepted |
| **Sound-alike** (30% of content words heard as another game word that only sounds or looks alike; 7,607 lines) | 95.8% right, **1 wrong**, 2 wrong on the way | **98.0% right, 0 wrong**, 0 on the way, 152 "not sure which" |
| Shepard's recordings through the recognizer (1,200) | 1,192 right, 8 undecided, 0 wrong | 1,192 right, 8 undecided, 0 wrong |
| ... word errors, male / female | 3.0% / 4.0% | 2.9% / 3.9% |

- With replies as the choices there are 10 more groups (near-identical lines under different replies); four of them are
  the new undecided whole lines, by design (the label decides).
- The old matcher's wrong pick in the sound-alike test: "what spit of word do you do here" taken for "What do you do
  here?" (`ice60_palon_dlg#18`); the new one holds it. The 152 held back are mostly lines a word apart, the datalog's
  "Play the last entry." / "Play the first entry." with "list" or "laser" heard.
- The speech bench's re-run (lines not heard word for word on wheels with key terms): of the 253 re-run with both lists,
  65 were heard word for word with the old terms, 71 with the new; 11 recordings are on wheels only the new list gives
  terms, among them "Saren wants both of us dead." heard "Sarah wants ..." without a term and right with it.
- Offline look: `mevr-menu-preview`'s `thespian-doubt.png` (two labels of one reply in one row; the "Not sure which"
  footer).

### In the game (fake headset, 2026-11-13)

`%LOCALAPPDATA%\MEVR\sessions\2026-11-13_13-06-31-thespian-names` (build 57e298c3, players' build from a clean worktree;
John02's validation save 80: Anderson's walk, the hearing, Anderson after it; the 3D screen; Windows' speech synthesizer
reading each line with the names spelled to sound off, fed with `mevr thespian feed`):

| Wheel | Key terms given | Read (synthesizer) | Heard | Taken |
|---|---|---|---|---|
| the hearing's first | Eden Prime | the second card's line | word for word | position 1, right |
| #10 | Saren,Eden Prime | "Sarin despises humanity. That is why he attacked Eden Prim!" | "**Saren** despises humanity. That is why he attacked Eden Park." | position 0, right (87%) |
| #14 | Saren,Spectre,Council | "Sarren's hiding behind his position as a Spectre. ..." | "Sarin's hiding behind his position as a spectre." | position 0, right (61%, at the pause) |
| #19 | (none) | the line | "... I won't waste my time" | position 0 (all three are reply 22) |
| captain #2 | Council,Saren | "Tell me about this history between you and Sarren." | "... between you and me" | position 0, right (81%) |
| captain #4 | Council,Saren | "What's our next step?" | word for word | position 0, right |

- "Sarin" was written "Saren" with the key term; "Sarren's" came out "Sarin's" (a Close match, taken); the synthesizer's
  "Sarren" at a line's end was heard as "me" despite the term. No pick was wrong; no "not sure which" (no wheel there has
  two lines a misheard word apart).
- **The forced end** (`mevr convo end` during a line, the mode on; the crash case): the game ran on. It crashed 12 s later
  as the session tool quit it, at `ntdll.dll+0x1A12D` writing 0x24 from the exe's exit path: the same exit crash the other
  session's runs show at 12:59 and 13:02 today, not Thespian's.
- **Found here**: the reply numbers the mod read were places (0, 1, 2), so the panel never merged the rows of one reply:
  #19 showed one line three times in the agree (blue), disagree (orange) and neutral colours, and reading it took the
  first, though all three are one reply. That may be what read as a wrong alignment; the colours are the replies'
  categories, and the points come from a reply's plot transition (#14's agree reply carries 4029), so which pick felt
  Renegade stays the owner's to say. Fixed in b24c5d40 (`ReplyNode`).
- **The run again with b24c5d40** (`sessions\2026-11-13_13-16-28-thespian-merge`): the cards carry the replies the census
  has (the first wheel's 9, #10's 12, #14's 16, #19's 22); the panel shows #10 as two rows ("Insults? Typical. / See? He
  hates us!" in the neutral colour, "You'll pay!" orange) and #19 as one row with its three labels. A label said alone
  ("What's the point?") took its own position 1, reply 22. The forced end again left the game running; the exit crash at
  the session tool's quit again (13:21:08).

### Left

- The owner's voice in the headset: names at the hearing, a mumbled telling word ("Not sure which"), merged rows
  (notes/headset-checklist.md, Thespian mode).
- Which pick felt Renegade; what int 47 counts.
- (4) and (5) above, only if misheard names still decide or block picks in play.

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

### Step 1. Read the live wheel (mod only; no behaviour change)

New files `src/game/Thespian.hpp/.cpp`, namespace `mevr::game::thespian`. Add them to the mod's source list in
`CMakeLists.txt` next to `src/game/Voice.cpp`. Call `thespian::Tick(deltaSeconds)` from `src/mod/Mod.cpp` right after
`game::conversation_world::Tick(...)`.

Names to resolve (each needs a `ReflectCheck` row; the classes and the first two fields have rows already, see
`engine/ReflectCheck.cpp` near "BioSFHandler_Conversation"):

```cpp
ue3::FieldRef    gCurrentConversation{"SFXGame.BioWorldInfo", "m_oCurrentConversation"};          // in ConversationWorld.cpp too
ue3::FieldRef    gCurrentReplies{"SFXGame.BioConversation", "m_lstCurrentReplyIndices"};          // TArray<int>
ue3::FunctionRef gGetReplyText{"SFXGame.BioConversation.GetReplyText"};                // (int nIndex, bool bParse) -> FString
ue3::FunctionRef gGetReplyParaphrase{"SFXGame.BioConversation.GetReplyParaphraseText"}; // (int nIndex) -> FString
ue3::FunctionRef gGetReplyCategory{"SFXGame.BioConversation.GetReplyCategory"};        // (int nIndex) -> int
ue3::FunctionRef gGetReplyGuiStyle{"SFXGame.BioConversation.GetReplyGUIStyle"};        // (int nIndex) -> byte
ue3::FieldRef    gDisplayingWheel{"SFXGame.BioSFHandler_Conversation", "m_bDisplayingWheel"};
ue3::FieldRef    gSlotsUsed{"SFXGame.BioSFHandler_Conversation", "m_bSlotsUsed"};                 // byte[6]
ue3::FieldRef    gReplyLocations{"SFXGame.BioSFHandler_Conversation", "m_aReplyLocations"};       // int[6]
ue3::FieldRef    gInvestigateLocations{"SFXGame.BioSFHandler_Conversation", "m_aInvestigateLocations"};  // int[6]
ue3::FieldRef    gInvestigateShown{"SFXGame.BioSFHandler_Conversation", "m_bDisplayInvestigateSubMenu"};
ue3::FunctionRef gSelectEntry{"SFXGame.BioSFHandler_Conversation.SelectConversationEntry"};  // (byte nWheelLocation) -> bool
ue3::FunctionRef gSkipConversation{"SFXGame.BioSFHandler_Conversation.SkipConversation"};
```

How to get the objects: the conversation as `ConversationWorld.cpp` does (the world info's `m_oCurrentConversation`);
the handler with that file's `WheelHandler(player::Controller())` (make it a declared function in
`ConversationWorld.hpp` instead of copying it). A string a script function returns is read as
`game/ConsoleRequest.cpp` does: `ue3::FString s = parms.Return<ue3::FString>().value_or({}); std::string text =
ue3::memory::Narrow(s); ue3::memory::FreeString(s);`.

Console command: in `src/game/ConsoleRequest.cpp` add the verb `thespian` beside `convo` (`mevr thespian cards`): while
`m_bDisplayingWheel` is set, print for every element `i` of `m_lstCurrentReplyIndices`: `i`, the value, and
`GetReplyText(x, true)`, `GetReplyParaphraseText(x)`, `GetReplyCategory(x)`, `GetReplyGUIStyle(x)` **called once with
`x = i` and once with `x = the element's value`**; then `m_aReplyLocations[0..5]`, `m_aInvestigateLocations[0..5]`,
`m_bSlotsUsed[0..5]`, `m_bDisplayInvestigateSubMenu`.

**[verify] with that dump** (play three wheels: one plain, one with investigate topics, one with a charm option; e.g.
`tools/conversation-from-save.ps1` on a Citadel save, then `tools/console.ps1 "mevr thespian cards"`), and write the
answers here:

- (a) Do the `GetReply*` functions take the position in `m_lstCurrentReplyIndices` or the reply's index in
  `m_ReplyList`? (The one that returns the lines the wheel shows is the right one.)
- (b) Are the investigate topics in `m_lstCurrentReplyIndices` while the main wheel is shown? (Expected: yes.)
- (c) What do `m_aReplyLocations[loc]` and `m_aInvestigateLocations[loc]` hold: a position in the current list, a reply
  index, or -1 for an unused location?
- (d) Is an unavailable charm/intimidate reply in the list, and how is it marked (the reply node's `bIllegal`, or a
  GUI style)? An unavailable reply must not become a card.
- (e) How long before the other person's line ends does `m_bDisplayingWheel` turn on
  (`BioConversation.m_fShowRepliesOffset`, `m_fRemainingVOLength`)? Log both once per wheel.

### Step 2. Commit by typing (mod only)

`mevr thespian say <words>`: build a `script_match::Wheel` from the live cards (label and line as read in step 1; skip
unavailable replies), `Match(words, true)`, and if `decided`: commit the leader.

Commit = the same call as `mevr convo pick` (`ConversationWorld.cpp`, verb "pick"): `SelectConversationEntry(location)`
on the handler, **only while `m_bDisplayingWheel` is set and `m_bSlotsUsed[location]` is non-zero** (a pick at any other
moment crashed the game on 2026-11-06). The location comes from step 1's answer (c). For an investigate card while the
main wheel is shown: first the location that opens the sub-menu (the one whose reply location answers to "Investigate";
answer (c) tells how to find it), wait until `m_bDisplayInvestigateSubMenu` is set (next tick), then the topic's
location from `m_aInvestigateLocations`.

Then, on the next tick in which the conversation's speaker is the player's pawn (`BioConversation.m_pSpeaker`) and the
wheel is down: call `SkipConversation()` on the handler once, unless the reply is unskippable (the reply node's
`bUnskippable`; then leave the line playing). Name the player's pawn to `engine/VoicePlace` with gain 0 from the commit
until the speaker is no longer the player (add a publisher `Thespian` to `voice_place::Publisher`, placed first in the
order, `centredOnly = false`), so no part of the recording is heard. **[verify]** with `build/Release/mevr-audio-loopback.exe`
that the recording is silent and that the next line starts within two frames of the skip; log the time from the commit
to `m_pSpeaker` changing.

Log one line per commit: `thespian: "<words>" -> card <n> "<label>" (score, margin, by label or line), location <l>,
skipped|played (unskippable)`.

Check: ten scenes from validation saves; in each, type one card's line, then a label, then nonsense (must refuse). The
plot after a typed commit must equal the plot after the same pick by stick: save both ways and compare with
`python tools/le1-save.py diff`.

### Step 3. The open microphone (voice DLL and host; host and `ASI\voice` deploy together)

`src/voice/VoiceApi.h`: `MEVR_VOICE_API_VERSION 3`. Add to the state struct (`core/vr/Voice.hpp` `State`):
`std::uint32_t silentMs` (time since the last chunk whose level was over the speech threshold; the capture thread
already computes the level per chunk) and `std::uint8_t open` (an open listen is running). Add `int MevrVoiceListenOpen(int on)`:
like `MevrVoiceListen(1)` but the stream is not closed by the library's own line splits and has no tail timer; it ends
only at `MevrVoiceListenOpen(0)`. Partials keep coming every `intervalMs` as today. The recognizer stays on one thread.

Host (`src/plugin/Voice.cpp`): pass the mod's request through (`HostServices`, next version: `setThespianListen(bool)`,
or a mode argument on the existing listen request) and publish `silentMs` in the voice state the mod already reads.

Mod (`game/Thespian`): while the wheel is up and the mode is on, ask for the open listen; set the wheel's key terms
(`Wheel::Keyterms(12)` joined with commas, through the link `game/Voice.cpp` uses for `setKeyterms`); feed every
partial and final line's text (all lines of the listen joined with a space, as `game/Voice.cpp` joins them) to the
matcher.

### Step 4. The turn's rules (`src/core/vr/Thespian.hpp/.cpp`, unit-tested in `tests/ThespianTests.cpp`)

A pure state machine, no engine: inputs per tick are `wheelUp`, the matcher's `Result` for the current transcript,
`silentMs`, `otherStillSpeaking` (the other person's line has not ended), the strictness, the clock. States: `Off`,
`Armed` (wheel up, nothing heard), `Hearing` (speech, no leader), `Leading` (a decided leader), `Committed`. Commit
rules (settings in `[Thespian]`):

| Situation | Commit when |
|---|---|
| The leader is decided and `reachedEnd` | `silentMs >= endMs` (default 200) |
| The leader is decided, not at the end | `silentMs >= pauseMs` (default 900) and the strictness allows it |
| Not decided while speaking | at `silentMs >= pauseMs` ask the matcher once more with `final = true`; commit if that decides and the strictness allows it; else stay `Hearing` (and ask tier 2 if built) |
| Words heard only while the other person still spoke | never; they count once speech continues after that line's end |

Strictness: `relaxed` (decided is enough), `performer` (default: decided and `coverage >= 0.5`, or matched by label),
`method` (decided and `coverage >= 0.9`). After a commit the machine ignores input until the wheel has gone down and
come up again.

### Step 5. The teleprompter (host; host and mod deploy together)

`core/vr/Thespian.hpp` also defines the struct the mod sends each tick (`HostServices::setThespian`): up to 12 cards,
each with label (96 chars), line (512 chars), category, style, `lit` frontier (word index), state (idle, leading,
committed, unavailable); the microphone's state (off, armed, hearing, understood, unsure) and level. Size-checked like
`world_ui::State`.

Host: `src/plugin/Thespian.cpp` + `ThespianDraw.cpp`, a third RmlUi context (`menu::Ui`, as `plugin/OmniToolRail` is
the second), `assets/menu/thespian.rml` / `.rcss`, one swapchain from `plugin/Swapchains` (evictable). Placement: the
conversation wheel's quad (`conversation_world::State::wheelHand`, `wheelWidthM`, `wheelDistanceM`); while the mode is
on, the host does not draw the wheel's crop. Layout as the plan's section 3.5. Offline look first: add its states to
`mevr-menu-preview`.

### Step 6. The rest

Settings and the menu's Voice page rows (`[Thespian] on`, `strictness`, `handPick = voice | silent`, `endMs`,
`pauseMs`); a hand pick (stick or pointer) while the mode is on plays Shepard's recording as the game does; the stand-downs
(no recognizer, the game's text not English: compare a known string id's text with its English text); tier 2 if section
7.1 found one worth shipping; the changelog and the controller guide's text.

## 9. Log

- **2026-11-12.** The plan. Phase 0: the line dump, the census, the matcher with its unit tests, the text bench over
  every wheel, the extractor for Shepard's recordings and the speech bench. Findings: no unchosen Shepard lines; investigate
  is a wheel grouping; 746 same-line pairs with one outcome; Shepard's recordings are per-conversation Ogg banks with
  both voices. Results: text, whole line 11,771 of 11,774 right, 0 wrong; speech, small model with per-wheel key terms
  1,192 of 1,200 right, 8 undecided, 0 wrong. Builds: `mevr-tests` passes, `build-trilogy` builds.

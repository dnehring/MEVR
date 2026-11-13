// SPDX-License-Identifier: MIT
// Thespian mode's matcher (docs/thespian-mode-plan.md, research/70): which of a dialogue wheel's replies is the player
// reading aloud? The candidates are known exactly: each "card" has the wheel's short label and the full line Shepard would
// say. A transcript (the recognizer's partial or final text) is aligned against every card's line and label, in order,
// forgiving skipped, misheard and extra words, and the cards are scored against each other.
//
// Game-independent and deterministic: no engine, no clock, no I/O. Unit tests: tests/ScriptMatchTests.cpp. Bench over every
// wheel of the game: tools/thespian-bench (mevr-thespian-bench).
//
// How a transcript is scored against one card (the numbers are Settings' defaults):
//   1. Both are split into words (Tokens): lower case, letters, digits and apostrophes; everything else separates.
//   2. A word weighs 1, a stop word (the, a, to, you, is, um...) 0.25 (IsStopWord).
//   3. The transcript is aligned with the card's line by the heaviest common subsequence (words in the same order; any
//      number of words skipped on either side). Two words are the same (SameWord) when their normal forms are equal
//      (apostrophes dropped, "2" as "two"), their stems are equal ("rushed"/"rush"), their spellings are close
//      (similarity 0.75 or more, both five letters or longer) or they sound alike (SoundKey: "Sarin"/"Saren"). The
//      transcript's last word also counts when it is the first three or more letters of the script's word: a partial
//      transcript often ends inside a word ("my name's shep").
//   4. score = the matched words' weight - 0.5 x the weight of the transcript's words the line doesn't hold.
//      The same against the label; the card's score is the better of the two.
// How the cards are compared (Wheel::Match):
//   - Cards with the same line (ME1 often offers one line under two labels) are one group; the choice is between groups.
//     Lines that differ by a word's form or a word or two ("I need to..." / "I needed to...") are one group too.
//   - While the player speaks (final = false) a group is "decided" when it has enough evidence (two content words, or most
//     of a short script), its score is at least 0.5 (more of the card was said than of anything else) and it leads the
//     runner-up by 1.2 (a content word the other doesn't have, plus its penalty there).
//   - When the player has stopped (final = true) each card also loses 0.5 x the weight of its script that was not said
//     (at most 1): the card whose whole line was said wins over a longer one that only starts the same way ("Yes." against
//     "Yes, I'll help you."), and a lead of 0.6 is enough.
//   - A script of one or two words ("Goodbye.") is decided only at the end and only when it is all that was said.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mevr::voice::script_match {

struct Settings {
    float stopWeight = 0.25f;           // a stop word's weight; any other word's is 1
    float extraPenalty = 0.5f;          // times the weight of each transcript word the script doesn't hold
    float leadMargin = 1.2f;            // the leader's score over the runner-up's to be decided while speaking
    float finalMargin = 0.6f;           // the same once the player has stopped
    float minContent = 2.0f;            // matched weight the leader needs...
    float minShare = 0.6f;              // ...or this share of its script's whole weight, whichever is less
    float wordSimilarity = 0.75f;       // spelling similarity (1 - edit distance / longer length) that makes two words one
    float minScore = 0.5f;              // the leader's score must reach this: talk that only shares a few words is no reply
    float missingPenalty = 0.5f;        // at the end: times the weight of the script's words that were not said...
    float missingCap = 2.0f;            // ...counted up to this much weight (stopping part-way must stay possible)
    float sameLine = 0.85f;             // two lines sharing this much of the heavier one's weight, in order, are one group
    int shortScriptWords = 2;           // scripts this short are decided only at the end, said alone
};

struct Card {
    std::string label;  // the wheel's short text ("The geth?"); may be empty
    std::string line;   // what Shepard says; empty for a menu card (the label is the script then)
};

struct CardState {
    float score = 0.0f;      // the better of the line's and the label's score
    float matched = 0.0f;    // matched weight of the script that gave the score
    float coverage = 0.0f;   // the line's matched weight over its whole weight, 0..1 (the label's for a menu card)
    int frontier = -1;       // index (into Wheel::Words(card)) of the last line word matched; -1 none
    bool reachedEnd = false; // one of the line's last two words was matched
    bool byLabel = false;    // the label scored better than the line
    int group = -1;          // the lowest card index with the same script words
    std::vector<std::uint8_t> lit;  // per word of Wheel::Words(card): 1 = matched (the teleprompter's lit words)
};

struct Result {
    int leader = -1;        // the best card (the first of its group); -1: nothing matched
    int runnerUp = -1;      // the best card of the next group; -1: there is none
    float margin = 0.0f;    // the leader's score minus the runner-up's (the leader's score when alone)
    bool decided = false;   // the leader may be shown as the choice, and committed at an end
    std::vector<CardState> cards;
};

// The words of a text: lower case; letters, digits and apostrophes (the typographic one too); everything else separates.
std::vector<std::string> Tokens(std::string_view text);
// The same, with each word as it is written in the text (capitals kept): what the teleprompter draws. Same count and order
// as Tokens.
std::vector<std::string> DisplayWords(std::string_view text);
bool IsStopWord(std::string_view word);
// A word's consonant skeleton, for words that are spelled differently and sound alike ("saren", "sarin": "SRN").
std::string SoundKey(std::string_view word);
// 1 - edit distance / the longer length.
float Similarity(std::string_view a, std::string_view b);
// The recognizer's word `said` is the script's word `want` (rule 3 above). Both lower case, as Tokens gives them.
bool SameWord(std::string_view said, std::string_view want, float similarity = 0.75f);

// A word ready to compare: computed once per script and once per transcript, not per comparison.
struct Word {
    std::string normal;  // apostrophes dropped, small numbers as words
    std::string stem;    // endings off: "rushed", "rushes" and "rush" share one
    std::string sound;   // SoundKey
    bool stop = false;   // IsStopWord
    bool cut = false;    // the transcript's last word: it may be the start of a longer word ("shep" for "Shepard")
    float weight = 1.0f; // stopWeight for a stop word, else 1
};
Word MakeWord(std::string_view token, float stopWeight = 0.25f);
bool Same(Word const& said, Word const& want, float similarity = 0.75f);  // SameWord on prepared words

class Wheel {
public:
    explicit Wheel(std::vector<Card> cards, Settings settings = {});

    // Score a transcript against every card. `final`: the player has stopped speaking (an end-of-speech was detected).
    Result Match(std::string_view transcript, bool final) const;

    int Count() const { return static_cast<int>(cards_.size()); }
    Card const& At(int card) const { return cards_[static_cast<std::size_t>(card)]; }
    // The line's words as written (the label's for a menu card): what CardState::lit and frontier index.
    std::vector<std::string> const& Words(int card) const { return display_[static_cast<std::size_t>(card)]; }
    int Group(int card) const { return group_[static_cast<std::size_t>(card)]; }
    // The cards' names and rare words for the recognizer's key-term bias, most distinctive first, at most `count`:
    // words written with a capital inside a sentence ("Saren", "Protheans", "Feros").
    std::vector<std::string> Keyterms(int count) const;

private:
    struct Script {
        std::vector<Word> words;
        float total = 0.0f;
    };
    struct Aligned {
        float matched = 0.0f;
        float penalty = 0.0f;
        int frontier = -1;
        std::vector<std::uint8_t> lit;
    };
    Aligned Align(std::vector<Word> const& said, Script const& script) const;
    Script MakeScript(std::string_view text) const;

    Settings settings_;
    std::vector<Card> cards_;
    std::vector<Script> lines_;
    std::vector<Script> labels_;
    std::vector<std::vector<std::string>> display_;
    std::vector<int> group_;
};

}  // namespace mevr::voice::script_match

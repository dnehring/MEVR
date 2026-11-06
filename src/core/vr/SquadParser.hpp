// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/SquadContext.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The rule tier of the spoken squad orders (research/54 "Rules first, a fine-tuned sub-1B model second"): a transcript
// becomes one Action per clause without a model, in about a millisecond, matched only against what the scene says is
// legal: the squadmates present (their names and aliases, fuzzily: a recognizer's "Gareth" is Garrus when he is the only
// G-name along), their weapons and powers (the roster's spoken names and a synonym table), the eight orders with their
// everyday verbs, and the targets: a deictic word ("those", "that one", "there") becomes PT n with its position for the
// pose history; a description ("the left ones", "the nearest", "up top", "on the bridge", "the troopers") is matched
// against the groups' words; "my target" is AIM, "what I'm looking at" GAZE, "the others" OTHER, "again" LAST; "go left",
// "move up", "cover the right" are directions from the player. A leading "no" is a correction, "belay that" a cancel,
// "and"/"then" before a verb, a squadmate's name after an order, or talk ("I'm hit") starts a new clause. Phrasal verbs
// split around their object ("take them out"); a power named with no squadmate goes to the one present who has it; "stay
// on him" keeps attacking, "stay on the bridge" holds there; talk ("nice shot", "they're flanking us") and orders the game
// can't take ("hold fire", "medi-gel") are refused by name. The confidence says when to fall through to the model: it
// drops for a fuzzy name, an ambiguous description and for content words the rules can't place. Unit-tested in
// tests/Main.cpp; the corpus of natural phrasings and its coverage figure in tests/SquadPhrasings.cpp.
namespace mevr::vr::squad_parser {

struct Settings {
    float nameSimilarity = 0.5f;  // a token this similar to a present squadmate's name or alias is that squadmate...
    float nameMargin = 0.2f;      // ...if the next-best present squadmate is this much less similar
    float wordSimilarity = 0.8f;  // other vocabulary words are matched this strictly (a plural or an ASR slip)
};

struct DeicticWord {
    int index = 1;         // PT n
    std::string word;      // as said ("those", "over there")
    int position = 0;      // the word's index in the transcript, 0-based (the first of a two-word deictic)
    bool place = false;    // "there", "over there": a place, not a thing
};

// What the words said about keeping the order (research/54 "Standing orders"): "until it's clear" / "until they're dead" /
// "until I say so", "and stay there" (no coming back), "no matter what" (no retreat), "then come back" (the default, said).
// A squadmate's name after an order starts a new clause ("Liara, target that krogan, Ashley, switch to your sniper"), except
// as the object of "support" / "cover" / "back up" / "help" / "protect" / "stay with", which is who to support.
struct StandingHint {
    enum class Until : std::uint8_t { Default, Calm, GroupGone, Relieved } until = Until::Default;
    bool stay = false;
    bool noRetreat = false;
    bool comeBack = false;
};

struct Clause {
    squad_context::Action action;
    StandingHint standing;
    char supportOf = 0;      // "support Liara": the squadmate to support (the dispatcher takes her target, else goes to her)
    float confidence = 0.0f;
    std::string unresolved;  // what the rules couldn't settle (empty when nothing)
    std::string words;       // the clause's words
};

struct Parsed {
    std::vector<Clause> clauses;
    std::vector<DeicticWord> deictics;
    bool correction = false;  // "no, ..." amends the last order
    bool cancel = false;      // "belay that", "cancel", "never mind"
    bool empty = true;        // nothing that looks like an order
    float confidence = 0.0f;  // the least confident clause's (0 when empty)
    std::string notes;        // how it parsed, for the log
};

// The transcript against the scene (who is present, their weapons and powers), the groups (their words) and the
// roster (names, aliases, the powers' spoken names and codes).
Parsed Parse(std::string_view transcript, squad_context::Scene const& scene, std::vector<squad_context::Group> const& groups,
    std::vector<squad_context::RosterEntry> const& roster, Settings const& settings);
// With the landmarks (the cover around the squad): "get behind that crate", "take cover behind the wall on the left" become
// a hold at that cover (Target::Landmark, or Pointed for "that"); "the ones behind the crates" the group nearest it.
Parsed Parse(std::string_view transcript, squad_context::Scene const& scene, std::vector<squad_context::Group> const& groups,
    std::vector<squad_context::Landmark> const& landmarks, std::vector<squad_context::RosterEntry> const& roster, Settings const& settings);

// Helpers, tested on their own.
std::vector<std::string> Tokens(std::string_view text);  // lower-case words, apostrophes kept, the rest dropped
float Similarity(std::string_view a, std::string_view b); // 1 - edit distance / the longer length, 0..1
std::string Singular(std::string_view word);              // "troopers" -> "trooper", "geth" -> "geth"

}  // namespace mevr::vr::squad_parser

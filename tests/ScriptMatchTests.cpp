// SPDX-License-Identifier: MIT
// Offline tests for Thespian mode's matcher (core/voice/ScriptMatch, research/70): the word split, which words count as the
// same, the alignment's lit words, and the choice between a wheel's cards while the player speaks and once they stop.

#include "core/voice/ScriptMatch.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/ScriptMatchTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace sm = mevr::voice::script_match;

void TestWords() {
    // Lower case; punctuation separates; both apostrophes stay inside a word; dashes and the ellipsis separate.
    std::vector<std::string> const t = sm::Tokens("I'm not sure\xE2\x80\x94the Council won\xE2\x80\x99t help... Saren's ship?");
    CHECK(t.size() == 9);
    CHECK(t.size() == 9 && t[0] == "i'm" && t[3] == "the" && t[5] == "won't" && t[7] == "saren's" && t[8] == "ship");
    std::vector<std::string> const d = sm::DisplayWords("What about the Protheans?");
    CHECK(d.size() == 4 && d[0] == "What" && d[3] == "Protheans");
    CHECK(sm::Tokens("  ...  ").empty());
    CHECK(sm::Tokens("'em").size() == 1 && sm::Tokens("'em")[0] == "em");

    CHECK(sm::IsStopWord("the") && sm::IsStopWord("you're") && sm::IsStopWord("um"));
    CHECK(!sm::IsStopWord("no") && !sm::IsStopWord("yes") && !sm::IsStopWord("don't") && !sm::IsStopWord("what") && !sm::IsStopWord("geth"));

    // The same word: as written, without the apostrophe, a number, a stem, a near spelling, a like sound.
    CHECK(sm::SameWord("dont", "don't"));
    CHECK(sm::SameWord("2", "two"));
    CHECK(sm::SameWord("rushed", "rush") && sm::SameWord("moved", "move") && sm::SameWord("running", "run") && sm::SameWord("protheans", "prothean"));
    CHECK(sm::SameWord("prothians", "protheans"));
    CHECK(sm::SameWord("sarin", "saren") && sm::SameWord("benesia", "benezia") && sm::SameWord("crogan", "krogan"));
    CHECK(sm::SoundKey("saren") == "SRN" && sm::SoundKey("sarin") == "SRN");
    CHECK(sm::SoundKey("knight") == sm::SoundKey("night"));
    // Not the same: short words a letter apart, function words a letter apart, unrelated words.
    CHECK(!sm::SameWord("then", "than") && !sm::SameWord("them", "they") && !sm::SameWord("yes", "no"));
    CHECK(!sm::SameWord("geth", "council") && !sm::SameWord("help", "kill"));
    CHECK(sm::Similarity("protheans", "prothians") > 0.85f && sm::Similarity("abc", "xyz") == 0.0f);
}

sm::Wheel Investigate() {
    return sm::Wheel({
        {"The Protheans?", "What can you tell me about the Protheans?"},
        {"The geth?", "What can you tell me about the geth?"},
        {"Saren?", "What do you know about Saren?"},
        {"I should go.", "I should go."},
    });
}

void TestChoiceWhileSpeaking() {
    sm::Wheel const wheel = Investigate();
    // The shared opening decides nothing.
    sm::Result r = wheel.Match("what can you tell me about", false);
    CHECK(!r.decided);
    // One telling word isn't enough evidence while speaking... but with the opening said, the line has its two content words.
    r = wheel.Match("what can you tell me about the protheans", false);
    CHECK(r.decided && r.leader == 0 && r.runnerUp == 1);
    CHECK(r.cards[0].reachedEnd && r.cards[0].coverage > 0.99f && r.cards[0].frontier == 7);
    CHECK(r.cards[0].lit.size() == 8 && r.cards[0].lit[0] == 1 && r.cards[0].lit[7] == 1);
    // The recognizer's spelling of a name still lands.
    r = wheel.Match("what can you tell me about the prothians", false);
    CHECK(r.decided && r.leader == 0);
    r = wheel.Match("what can you tell me about the geth", false);
    CHECK(r.decided && r.leader == 1);
    r = wheel.Match("what do you know about sarin", false);
    CHECK(r.decided && r.leader == 2);
    // Part of the line: lit up to where the player is, not at the end yet.
    r = wheel.Match("what do you know", false);
    CHECK(r.leader == 2 && !r.cards[2].reachedEnd && r.cards[2].frontier == 3 && r.cards[2].coverage < 0.8f);
    // Fillers and a restart cost little.
    r = wheel.Match("uh what what can you tell me about um the geth", false);
    CHECK(r.decided && r.leader == 1);
    // Talk that is on no card decides nothing, speaking or stopped.
    r = wheel.Match("hang on a second somebody is at the door", false);
    CHECK(!r.decided);
    r = wheel.Match("hang on a second somebody is at the door", true);
    CHECK(!r.decided);
    r = wheel.Match("", true);
    CHECK(!r.decided && r.leader == -1);
}

void TestChoiceAtTheEnd() {
    sm::Wheel const wheel = Investigate();
    // The label alone: enough once the player has stopped (a two-word script is never decided mid-speech).
    sm::Result r = wheel.Match("the geth", false);
    CHECK(!r.decided);
    r = wheel.Match("the geth", true);
    CHECK(r.decided && r.leader == 1 && r.cards[1].byLabel);
    r = wheel.Match("saren", true);
    CHECK(r.decided && r.leader == 2);
    // A short line said whole, alone.
    r = wheel.Match("I should go", true);
    CHECK(r.decided && r.leader == 3);

    // One card is the start of another: only the end tells them apart.
    sm::Wheel const yes({{"Yes.", "Yes."}, {"I'll help.", "Yes, I'll help you find him."}, {"No.", "No."}});
    r = yes.Match("yes", false);
    CHECK(!r.decided);
    r = yes.Match("yes", true);
    CHECK(r.decided && r.leader == 0);
    r = yes.Match("yes I'll help you find him", false);
    CHECK(r.decided && r.leader == 1);
    r = yes.Match("yes I'll help you", true);
    CHECK(r.decided && r.leader == 1);
    r = yes.Match("no", true);
    CHECK(r.decided && r.leader == 2);

    // Nearly the same line twice: one group, so reading either decides (and the group's cards share an outcome or the
    // caller asks by label).
    sm::Wheel const twice({{"I had to.", "I needed to get to Peak 15. The shortest route was a straight line."},
        {"It was faster.", "I need to get to Peak 15. The shortest route was a straight line."}, {"None of your business.", "That's my concern."}});
    CHECK(twice.Group(1) == 0);
    r = twice.Match("I need to get to peak fifteen the shortest route was a straight line", true);
    CHECK(r.decided && twice.Group(r.leader) == 0);

    // The recognizer's run-together spelling of two words.
    sm::Wheel const leave({{"Goodbye.", "I'm out of here."}, {"Wait.", "Tell me what you saw first."}});
    r = leave.Match("I'm outta here", true);
    CHECK(r.decided && r.leader == 0 && r.cards[0].coverage > 0.99f);
    sm::Wheel const going({{"I'll do it.", "I'm gonna find him."}, {"No.", "Find him yourself."}});
    r = going.Match("I'm going to find him", true);
    CHECK(r.decided && r.leader == 0);
    r = going.Match("I'm gonna find him", true);
    CHECK(r.decided && r.leader == 0);

    // A transcript that ends inside a word (a streaming partial, or a final the recognizer cut): the last word counts as
    // the start of the script's word; a word in the middle does not.
    sm::Wheel const name({{"Shepard.", "My name's Shepard."}, {"None of your business.", "That's not your concern."}});
    r = name.Match("my name's shep", true);
    CHECK(r.decided && r.leader == 0 && r.cards[0].reachedEnd);
    r = name.Match("shep my name's", true);
    CHECK(r.cards[0].frontier == 1);

    // Talk that shares a line's opening and goes elsewhere is no reply.
    sm::Wheel const why({{"Why is she here?", "Do you know why the Matriarch is here?"}, {"Goodbye.", "I should go."}});
    r = why.Match("do you know why Benezia joined up with Saren", true);
    CHECK(!r.decided);
    r = why.Match("do you know why the matriarch is here", true);
    CHECK(r.decided && r.leader == 0);
    // Stopping part-way is fine once the card is clear.
    r = why.Match("do you know why the matriarch", true);
    CHECK(r.decided && r.leader == 0);
    // A "yes" inside other talk picks nothing.
    r = yes.Match("yes well I was just thinking about lunch", true);
    CHECK(!r.decided);
}

void TestGroupsAndMenus() {
    // One line under two labels (ME1 does this a lot): one group; the choice is the group, shown as its first card.
    sm::Wheel const same({
        {"I'll do it.", "You can count on me."},
        {"If I have to.", "You can count on me."},
        {"Not my problem.", "Find someone else to solve your problems."},
    });
    CHECK(same.Group(0) == 0 && same.Group(1) == 0 && same.Group(2) == 2);
    sm::Result r = same.Match("you can count on me", true);
    CHECK(r.decided && r.leader == 0 && r.runnerUp == 2);
    // The label still tells the two apart for a caller that needs to (the second card's own label).
    r = same.Match("if I have to", true);
    CHECK(r.decided && same.Group(r.leader) == 0 && r.leader == 1 && r.cards[1].byLabel);

    // Menu cards: no line, the label is the script.
    sm::Wheel const menu({{"Presidium Locations", ""}, {"Wards Locations", ""}, {"Exit", ""}});
    r = menu.Match("wards locations", true);
    CHECK(r.decided && r.leader == 1);
    r = menu.Match("exit", true);
    CHECK(r.decided && r.leader == 2);
    CHECK(menu.Words(1).size() == 2 && menu.Words(1)[0] == "Wards");

    // A single card: its own evidence decides.
    sm::Wheel const one({{"Goodbye.", "I'll be back when I know more."}});
    r = one.Match("I'll be back when I know", false);
    CHECK(r.decided && r.leader == 0 && r.runnerUp == -1);
}

void TestKeyterms() {
    sm::Wheel const wheel = Investigate();
    std::vector<std::string> const terms = wheel.Keyterms(8);
    // Names inside sentences and the setting's own small words ("geth"); not the sentences' first words, not "I".
    CHECK(terms.size() == 3);
    CHECK(terms.size() == 3 && terms[0] == "Protheans" && terms[1] == "geth" && terms[2] == "Saren");
    CHECK(wheel.Keyterms(1).size() == 1);
    // A possessive's name once; the setting's lower-case words too.
    sm::Wheel const lore({{"Saren?", "Where is Saren's base?"}, {"The rachni?", "How many rachni are in here?"}, {"Saren.", "I'll find Saren."}});
    std::vector<std::string> const names = lore.Keyterms(8);
    CHECK(names.size() == 2);
    CHECK(names.size() == 2 && names[0] == "rachni" && names[1] == "Saren");
}

}  // namespace

int RunScriptMatchTests() {
    gFailures = 0;
    TestWords();
    TestChoiceWhileSpeaking();
    TestChoiceAtTheEnd();
    TestGroupsAndMenus();
    TestKeyterms();
    return gFailures;
}

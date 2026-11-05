// SPDX-License-Identifier: MIT
// The rule parser against what a player would actually say (research/54 "Prove the game calls", step 5: the share of a
// corpus the rules parse completely decides whether the model tier is a necessity or a nicety). One scene (Garrus with a
// sniper and an assault rifle, Overload ready and Damping cooling; Wrex with a shotgun and Throw; Liara at home), three
// enemy groups: E1 three geth troopers left, far, above, on a bridge; E2 a geth destroyer ahead and near; E3 two troopers
// right and mid. Each line is what a player says and the one-line action(s) it must become, at a confidence the dispatcher
// acts on ([SquadOrders] minConfidence 0.6). `Refuse` lines are talk or orders the game has no call for: they must come
// back empty or below the threshold. `Gap` lines are known shortfalls kept on record: reported, never failed, and flagged
// when one starts to pass so it can be promoted. The counts are printed as the coverage figure research/54 quotes.

#include "core/vr/SquadContext.hpp"
#include "core/vr/SquadParser.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

namespace sc = mevr::vr::squad_context;
namespace sp = mevr::vr::squad_parser;

enum class Kind { Pass, Refuse, Gap };

struct Case {
    Kind kind;
    char const* said;
    char const* expect;  // Pass/Gap: the lines, " ; " between clauses; Refuse: why (for the report)
};

// clang-format off
Case const kCases[] = {
    // Attacks: verbs of every register, phrasal verbs split around their object, a weapon with a target.
    {Kind::Pass, "Garrus, take out those troops on the bridge", "G|-|-|attack|E1"},
    {Kind::Pass, "Garrus take those guys out", "G|-|-|attack|PT1"},
    {Kind::Pass, "take them out", "ALL|-|-|attack|PT1"},
    {Kind::Pass, "Wrex, take him down", "W|-|-|attack|PT1"},
    {Kind::Pass, "light them up", "ALL|-|-|attack|PT1"},
    {Kind::Pass, "Garrus, light up the destroyer", "G|-|-|attack|E2"},
    {Kind::Pass, "wipe them out", "ALL|-|-|attack|PT1"},
    {Kind::Pass, "Wrex, finish him off", "W|-|-|attack|PT1"},
    {Kind::Pass, "Garrus, pick off the troopers on the bridge", "G|-|-|attack|E1"},
    {Kind::Pass, "Wrex get the destroyer", "W|-|-|attack|E2"},
    {Kind::Pass, "get them", "ALL|-|-|attack|PT1"},
    {Kind::Pass, "Garrus, snipe the ones up top", "G|-|-|attack|E1"},
    {Kind::Pass, "Garrus, snipe", "G|-|-|attack|-"},
    {Kind::Pass, "everybody focus the destroyer", "ALL|-|-|attack|E2"},
    {Kind::Pass, "focus fire on the destroyer", "ALL|-|-|attack|E2"},
    {Kind::Pass, "Wrex, charge the destroyer", "W|-|-|attack|E2"},
    {Kind::Pass, "Wrex, rush them", "W|-|-|attack|PT1"},
    {Kind::Pass, "Wrex, smash them", "W|-|-|attack|PT1"},
    {Kind::Pass, "Garrus, kill it", "G|-|-|attack|PT1"},
    {Kind::Pass, "Garrus, fire", "G|-|-|attack|-"},
    {Kind::Pass, "open fire", "ALL|-|-|attack|-"},
    {Kind::Pass, "Garrus, aim for the destroyer", "G|-|-|attack|E2"},
    {Kind::Pass, "Garrus, eyes on the bridge", "G|-|-|attack|E1"},
    {Kind::Pass, "Garrus, suppress the troopers on the right", "G|-|-|attack|E3"},
    {Kind::Pass, "keep shooting", "ALL|-|-|attack|-"},
    {Kind::Pass, "Garrus, keep on him", "G|-|-|attack|PT1"},
    {Kind::Pass, "Garrus, stay on him", "G|-|-|attack|PT1"},
    {Kind::Pass, "Garrus, take the bridge", "G|-|-|attack|E1"},
    {Kind::Pass, "Garrus, take down the big group", "G|-|-|attack|E1"},
    {Kind::Pass, "Garrus, take out the geth on the right", "G|-|-|attack|E3"},
    {Kind::Pass, "shoot the geth on the left", "ALL|-|-|attack|E1"},
    {Kind::Pass, "shoot the geth", "ALL|-|-|attack|E2"},  // three groups fit: the nearest, at 0.7
    {Kind::Pass, "the trooper on the far left", "ALL|-|-|attack|E1"},
    {Kind::Pass, "Garrus, the three on the bridge", "G|-|-|attack|E1"},
    {Kind::Pass, "Garrus, the two on the right", "G|-|-|attack|E3"},
    {Kind::Pass, "Garrus, those two on the right", "G|-|-|attack|E3"},
    {Kind::Pass, "Wrex, in the middle", "W|-|-|attack|E2"},
    {Kind::Pass, "Wrex, the destroyer", "W|-|-|attack|E2"},
    {Kind::Pass, "Garrus, bridge", "G|-|-|attack|E1"},
    {Kind::Pass, "Wrex, up top", "W|-|-|attack|E1"},
    {Kind::Pass, "Garrus, him", "G|-|-|attack|PT1"},
    {Kind::Pass, "Garrus, that krogan", "G|-|-|attack|PT1"},  // no krogan here: the pointed thing, its noun not a mystery
    {Kind::Pass, "Garrus, that one over there", "G|-|-|attack|PT1"},
    {Kind::Pass, "Garrus, that destroyer, now", "G|-|-|attack|E2"},
    {Kind::Pass, "everyone on the destroyer", "ALL|-|-|attack|E2"},
    {Kind::Pass, "Garrus, sniper, those on the bridge", "G|SR|-|attack|E1"},
    {Kind::Pass, "Garrus, sniper the bridge", "G|SR|-|attack|E1"},
    {Kind::Pass, "Garrus, use your sniper on the troopers on the bridge", "G|SR|-|attack|E1"},
    {Kind::Pass, "Garrus, long range, the bridge", "G|SR|-|attack|E1"},
    {Kind::Pass, "Garrus, shoot him with your sniper", "G|SR|-|attack|PT1"},
    {Kind::Pass, "Garrus, could you please take out the destroyer", "G|-|-|attack|E2"},
    {Kind::Pass, "Garrus, I need you to take out the destroyer", "G|-|-|attack|E2"},
    {Kind::Pass, "uh Garrus um overload the uh destroyer", "G|-|OVR|use|E2"},
    {Kind::Pass, "Wrex, don't just stand there, attack", "W|-|-|attack|-"},
    {Kind::Pass, "all right Garrus, overload", "G|-|OVR|use|-"},
    {Kind::Pass, "Garrus, overload the destroyer's shields", "G|-|OVR|use|E2"},
    // Powers: named plainly, with an attack verb, with "use", with the owner left out when only one present has it.
    {Kind::Pass, "Garrus, overload", "G|-|OVR|use|-"},
    {Kind::Pass, "overload", "G|-|OVR|use|-"},
    {Kind::Pass, "Garrus, hit him with an overload", "G|-|OVR|use|PT1"},
    {Kind::Pass, "Garrus, use overload on the destroyer", "G|-|OVR|use|E2"},
    {Kind::Pass, "Wrex, throw", "W|-|THR|use|-"},
    {Kind::Pass, "throw them", "W|-|THR|use|PT1"},
    {Kind::Pass, "Wrex, lift that guy", "W|-|LFT|use|PT1"},
    {Kind::Pass, "Wrex, warp the destroyer", "W|-|WRP|use|E2"},
    {Kind::Pass, "hit the big group with warp", "W|-|WRP|use|E1"},
    {Kind::Pass, "pop an overload on the big one", "G|-|OVR|use|E1"},
    {Kind::Pass, "Garrus, damping on those", "G|-|DMP|use|PT1"},
    {Kind::Pass, "sabotage their weapons", "G|-|SAB|use|-"},
    {Kind::Pass, "Garrus, overload what I'm looking at", "G|-|OVR|use|GAZE"},
    {Kind::Pass, "Wrex, throw my target", "W|-|THR|use|AIM"},
    // Weapons: an equip in any wording.
    {Kind::Pass, "Garrus, equip your sniper", "G|SR|-|-|-"},
    {Kind::Pass, "Garrus, sniper", "G|SR|-|-|-"},
    {Kind::Pass, "Garrus, go sniper", "G|SR|-|-|-"},
    {Kind::Pass, "Garrus, rifle", "G|AR|-|-|-"},
    {Kind::Pass, "Garrus, assault rifle out", "G|AR|-|-|-"},
    {Kind::Pass, "Wrex, switch to your shotgun", "W|SG|-|-|-"},
    {Kind::Pass, "Wrex, shotgun out", "W|SG|-|-|-"},
    {Kind::Pass, "Wrex, pistol", "W|PI|-|-|-"},  // the parser's job; Validate says he carries none
    {Kind::Pass, "Garrus, pull out your sniper", "G|SR|-|-|-"},
    {Kind::Pass, "garrus equip your snipper", "G|SR|-|-|-"},
    // Moves: a place, a described group, a direction, forward by default.
    {Kind::Pass, "Wrex, go there", "W|-|-|move|PT1"},
    {Kind::Pass, "Garrus, there", "G|-|-|move|PT1"},
    {Kind::Pass, "Wrex, over there, now", "W|-|-|move|PT1"},
    {Kind::Pass, "Garrus, up there", "G|-|-|move|PT1"},
    {Kind::Pass, "Garrus, go up there", "G|-|-|move|PT1"},
    {Kind::Pass, "Wrex, get in there", "W|-|-|move|PT1"},
    {Kind::Pass, "Wrex, go over there by the crates", "W|-|-|move|PT1"},
    {Kind::Pass, "Garrus, get up on the bridge", "G|-|-|move|E1"},
    {Kind::Pass, "Garrus, push up to the bridge", "G|-|-|move|E1"},
    {Kind::Pass, "everybody move up to the catwalk", "ALL|-|-|move|E1"},
    {Kind::Pass, "Garrus, take the high ground", "G|-|-|move|E1"},
    {Kind::Pass, "Wrex, flank left", "W|-|-|move|LEFT"},
    {Kind::Pass, "Garrus, go left", "G|-|-|move|LEFT"},
    {Kind::Pass, "Wrex, go right", "W|-|-|move|RIGHT"},
    {Kind::Pass, "Garrus, go around the left", "G|-|-|move|LEFT"},
    {Kind::Pass, "Garrus, move back", "G|-|-|move|BEHIND"},
    {Kind::Pass, "everyone move back", "ALL|-|-|move|BEHIND"},
    {Kind::Pass, "move forward", "ALL|-|-|move|AHEAD"},
    {Kind::Pass, "Garrus, push up", "G|-|-|move|AHEAD"},
    {Kind::Pass, "Garrus, move", "G|-|-|move|AHEAD"},
    {Kind::Pass, "go go go", "ALL|-|-|move|AHEAD"},
    {Kind::Pass, "spread out", "ALL|-|-|move|AHEAD"},  // the game has no spread order; forward is the stand-in
    {Kind::Pass, "Wrex, shotgun, get in close", "W|SG|-|move|E2"},
    // Holds, cover, standing modifiers.
    {Kind::Pass, "Garrus, hold", "G|-|-|hold|-"},
    {Kind::Pass, "Wrex, wait here", "W|-|-|hold|-"},
    {Kind::Pass, "hold the line", "ALL|-|-|hold|-"},
    {Kind::Pass, "everyone freeze", "ALL|-|-|hold|-"},
    {Kind::Pass, "Garrus, don't move", "G|-|-|hold|-"},
    {Kind::Pass, "Wrex, stop", "W|-|-|hold|-"},
    {Kind::Pass, "Wrex, stay back", "W|-|-|hold|-"},
    {Kind::Pass, "get down", "ALL|-|-|hold|-"},
    {Kind::Pass, "take cover", "ALL|-|-|hold|-"},
    {Kind::Pass, "everyone, get to cover", "ALL|-|-|hold|-"},
    {Kind::Pass, "Garrus, cover the door", "G|-|-|hold|-"},  // no door word on this level: holds where he is
    {Kind::Pass, "Wrex, guard the door", "W|-|-|hold|-"},
    {Kind::Pass, "Wrex, cover the left", "W|-|-|hold|LEFT"},
    {Kind::Pass, "Garrus, cover the bridge", "G|-|-|hold|E1"},
    {Kind::Pass, "Garrus, stay on the bridge", "G|-|-|hold|E1"},
    {Kind::Pass, "Garrus, dig in on the bridge", "G|-|-|hold|E1"},
    {Kind::Pass, "Wrex, hold there no matter what", "W|-|-|hold|PT1"},
    {Kind::Pass, "Wrex, hold this position no matter what", "W|-|-|hold|-"},
    {Kind::Pass, "everyone hold position until it's clear", "ALL|-|-|hold|-"},
    {Kind::Pass, "Garrus, hold the bridge until they're dead then come back", "G|-|-|hold|E1"},
    {Kind::Pass, "Garrus, go to the bridge and stay there", "G|-|-|move|E1"},
    {Kind::Pass, "Garrus, get to the bridge, then fall back", "G|-|-|move|E1"},
    {Kind::Pass, "Wrex, stay there", "W|-|-|hold|-"},
    // Follows and calls back.
    {Kind::Pass, "Garrus, on me", "G|-|-|follow|-"},
    {Kind::Pass, "Garrus, with me", "G|-|-|follow|-"},
    {Kind::Pass, "Wrex, you're with me", "W|-|-|follow|-"},
    {Kind::Pass, "everyone fall back", "ALL|-|-|follow|-"},
    {Kind::Pass, "fall back", "ALL|-|-|follow|-"},
    {Kind::Pass, "retreat", "ALL|-|-|follow|-"},
    {Kind::Pass, "pull back", "ALL|-|-|follow|-"},
    {Kind::Pass, "everybody, back off", "ALL|-|-|follow|-"},
    {Kind::Pass, "Wrex, get out of there", "W|-|-|follow|-"},
    {Kind::Pass, "Garrus, get back here", "G|-|-|follow|-"},
    {Kind::Pass, "let's go", "ALL|-|-|follow|-"},
    {Kind::Pass, "come on, let's move", "ALL|-|-|follow|-"},
    {Kind::Pass, "regroup on me", "ALL|-|-|follow|-"},
    {Kind::Pass, "stay close", "ALL|-|-|follow|-"},
    {Kind::Pass, "Garrus, stay behind me", "G|-|-|follow|-"},
    {Kind::Pass, "Wrex, come with me", "W|-|-|follow|-"},
    {Kind::Pass, "Wrex, on my six", "W|-|-|follow|-"},
    {Kind::Pass, "cover me", "ALL|-|-|follow|-"},
    {Kind::Pass, "Garrus, watch my back", "G|-|-|follow|-"},
    {Kind::Pass, "Garrus, help", "G|-|-|follow|-"},
    {Kind::Pass, "Garrus, Wrex, on me", "ALL|-|-|follow|-"},
    {Kind::Pass, "Garrus and Wrex, attack", "ALL|-|-|attack|-"},
    {Kind::Pass, "Garrus, I'm hit, cover me", "G|-|-|follow|-"},
    // Support.
    {Kind::Pass, "Wrex, help Garrus", "W|-|-|attack|-"},
    {Kind::Pass, "Garrus, protect Wrex", "G|-|-|attack|-"},
    {Kind::Pass, "Wrex, cover Garrus", "W|-|-|attack|-"},
    // Compounds: conjunctions, vocatives, carried squadmates, "you too".
    {Kind::Pass, "Garrus, equip your sniper and take out those troops on the bridge", "G|SR|-|-|- ; G|-|-|attack|E1"},
    {Kind::Pass, "Garrus, go sniper and take out the troopers up top", "G|SR|-|-|- ; G|-|-|attack|E1"},
    {Kind::Pass, "Garrus, sniper and cover the bridge", "G|SR|-|-|- ; G|-|-|hold|E1"},
    {Kind::Pass, "Wrex, hold here, Garrus, take the bridge", "W|-|-|hold|- ; G|-|-|attack|E1"},
    {Kind::Pass, "Garrus, with me, Wrex, hold here", "G|-|-|follow|- ; W|-|-|hold|-"},
    {Kind::Pass, "Wrex, come here, Garrus, you stay", "W|-|-|follow|- ; G|-|-|hold|-"},
    {Kind::Pass, "Wrex, that one, Garrus, the ones on the bridge", "W|-|-|attack|PT1 ; G|-|-|attack|E1"},
    {Kind::Pass, "Wrex, you take the destroyer, Garrus, you got the bridge", "W|-|-|attack|E2 ; G|-|-|attack|E1"},
    {Kind::Pass, "Garrus, overload, Wrex, throw, everyone fall back", "G|-|OVR|use|- ; W|-|THR|use|- ; ALL|-|-|follow|-"},
    {Kind::Pass, "Garrus, take the bridge, Wrex, the destroyer, and everyone regroup after", "G|-|-|attack|E1 ; W|-|-|attack|E2 ; ALL|-|-|follow|-"},
    {Kind::Pass, "Garrus, hold, then follow me", "G|-|-|hold|- ; G|-|-|follow|-"},
    {Kind::Pass, "Garrus attack those and everyone else fall back", "G|-|-|attack|PT1 ; ALL|-|-|follow|-"},
    {Kind::Pass, "everyone attack, then regroup", "ALL|-|-|attack|-"},
    {Kind::Pass, "Garrus, you too", "G|-|-|repeat|-"},
    {Kind::Pass, "again", "ALL|-|-|repeat|-"},
    {Kind::Pass, "Wrex, same target", "W|-|-|attack|LAST"},
    {Kind::Pass, "now the others", "ALL|-|-|attack|OTHER"},
    {Kind::Pass, "no, the other one", "ALL|-|-|attack|OTHER"},
    {Kind::Pass, "no no, the ones on the right", "ALL|-|-|attack|E3"},
    // A recognizer's slips on names.
    {Kind::Pass, "gary's overload the destroyer", "G|-|OVR|use|E2"},
    {Kind::Pass, "wrecks throw that one", "W|-|THR|use|PT1"},
    // Talk, questions and orders the game can't take: refused, never guessed at.
    {Kind::Refuse, "what the hell was that", "talk with a deictic in it"},
    {Kind::Refuse, "I'm hit", "a statement"},
    {Kind::Refuse, "nice shot Garrus", "praise"},
    {Kind::Refuse, "watch out", "a warning"},
    {Kind::Refuse, "they're flanking us", "a report"},
    {Kind::Refuse, "good job Wrex", "praise"},
    {Kind::Refuse, "Garrus, where are you", "a question"},
    {Kind::Refuse, "Garrus", "a name alone"},
    {Kind::Refuse, "um okay", "fillers"},
    {Kind::Refuse, "Wrex, medi-gel", "no such order"},
    {Kind::Refuse, "hold fire", "no such order"},
    {Kind::Refuse, "stop shooting", "no such order"},
    {Kind::Refuse, "Garrus, reload", "no such order"},
    {Kind::Refuse, "throw a grenade at them", "no such order"},
    {Kind::Refuse, "Garrus, heal me", "no such order"},
    {Kind::Refuse, "singularity", "nobody present has it"},
    {Kind::Refuse, "Garrus, do the thing with the flibbertigibbet", "words the rules can't place"},
    {Kind::Refuse, "Garrus, support", "whom to support"},
    // Known gaps, on record: what a model (or a surface lexicon) would have to take over.
    {Kind::Gap, "Garrus, snipe them from the bridge", "G|-|-|attack|PT1"},         // "from" a place: the description wins over the deictic
    {Kind::Gap, "Garrus, nice shot, now the destroyer", "G|-|-|attack|E2"},        // praise before a verbless order
    {Kind::Gap, "Wrex, behind the crates", "W|-|-|move|PT1"},                       // a place the level has no word for
    {Kind::Gap, "Wrex, take care of the big guy", "W|-|-|attack|E2"},               // "big guy" is Wrex's own alias
    {Kind::Gap, "Garrus, I'm hit by the destroyer, get him", "G|-|-|attack|E2"},    // the report's noun lands in the order
};
// clang-format on

std::string Lines(sp::Parsed const& p) {
    std::string out;
    for (sp::Clause const& c : p.clauses) {
        if (!out.empty()) out += " ; ";
        out += sc::Line(c.action);
    }
    return out;
}

}  // namespace

int RunSquadPhrasingTests() {
    sc::Scene scene;
    scene.combat = true;
    sc::Member garrus;
    garrus.letter = 'G';
    garrus.present = true;
    garrus.carried = (1u << 3) | (1u << 2);
    garrus.powers = {{"OVR", 0.0f}, {"DMP", 9.0f}};
    sc::Member wrex;
    wrex.letter = 'W';
    wrex.present = true;
    wrex.carried = 1u << 1;
    wrex.powers = {{"THR", 0.0f}};
    sc::Member liara;
    liara.letter = 'L';
    scene.party = {garrus, wrex, liara};
    auto const group = [](int id, char const* type, int count, sc::Sector sector, sc::Band band, sc::Elevation elevation, char const* surface,
                           float distance) {
        sc::Group g;
        g.id = id;
        g.type = type;
        g.members.assign(static_cast<std::size_t>(count), 0);
        g.sector = sector;
        g.band = band;
        g.elevation = elevation;
        g.surface = surface;
        g.distanceM = distance;
        return g;
    };
    std::vector<sc::Group> groups = {group(1, "geth trooper", 3, sc::Sector::Left, sc::Band::Far, sc::Elevation::Above, "bridge", 32.0f),
        group(2, "geth destroyer", 1, sc::Sector::Ahead, sc::Band::Near, sc::Elevation::Level, "", 8.0f),
        group(3, "geth trooper", 2, sc::Sector::Right, sc::Band::Mid, sc::Elevation::Level, "", 23.0f)};
    groups[0].farthest = groups[0].leftmost = groups[0].highest = groups[0].biggest = true;
    groups[1].nearest = true;
    groups[2].rightmost = true;
    std::vector<sc::RosterEntry> roster(3);
    roster[0].letter = 'G';
    roster[0].name = "Garrus Vakarian";
    roster[0].aliases = {"Garrus", "Vakarian", "the turian"};
    roster[0].powers = {{"OVR", "overload"}, {"DMP", "damping"}, {"SAB", "sabotage"}};
    roster[1].letter = 'W';
    roster[1].name = "Urdnot Wrex";
    roster[1].aliases = {"Wrex", "the krogan", "big guy"};
    roster[1].powers = {{"THR", "throw"}, {"WRP", "warp"}, {"LFT", "lift"}};
    roster[2].letter = 'L';
    roster[2].name = "Liara T'Soni";
    roster[2].aliases = {"Liara", "the asari"};
    roster[2].powers = {{"SNG", "singularity"}, {"STS", "stasis"}};
    sp::Settings const st;
    float const threshold = 0.6f;  // [SquadOrders] minConfidence's default

    int failures = 0, passes = 0, refusals = 0, gapsOpen = 0, gapsClosed = 0;
    int wanted = 0, talk = 0;
    for (Case const& c : kCases) {
        sp::Parsed const p = sp::Parse(c.said, scene, groups, roster, st);
        std::string const got = Lines(p);
        bool const acted = !p.empty && p.confidence >= threshold;
        switch (c.kind) {
        case Kind::Pass:
            ++wanted;
            if (acted && got == c.expect) {
                ++passes;
            } else {
                ++failures;
                std::printf("FAIL tests/SquadPhrasings.cpp: \"%s\" -> %s (confidence %.2f%s%s), wanted %s\n", c.said,
                    got.empty() ? "(nothing)" : got.c_str(), p.confidence, p.clauses.empty() ? "" : "; ",
                    p.clauses.empty() ? "" : p.clauses.back().unresolved.c_str(), c.expect);
            }
            break;
        case Kind::Refuse:
            ++talk;
            if (!acted) {
                ++refusals;
            } else {
                ++failures;
                std::printf("FAIL tests/SquadPhrasings.cpp: \"%s\" (%s) was acted on as %s at %.2f\n", c.said, c.expect, got.c_str(), p.confidence);
            }
            break;
        case Kind::Gap:
            if (acted && got == c.expect) {
                ++gapsClosed;
                std::printf("NOTE tests/SquadPhrasings.cpp: the known gap \"%s\" now parses as wanted: promote it\n", c.said);
            } else {
                ++gapsOpen;
            }
            break;
        }
    }
    std::printf("squad phrasings: %d of %d orders parsed as said, %d of %d non-orders refused, %d known gaps open (%d closed)\n", passes,
        wanted, refusals, talk, gapsOpen, gapsClosed);
    return failures;
}

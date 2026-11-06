// SPDX-License-Identifier: MIT
// Offline tests for the landmarks (research/54 "Landmarks"): the level's cover as L ids in the squad context, their words,
// the point behind a piece of cover, "that crate" resolved from the rays, the validator, the parser's "get behind the crate" /
// "take cover behind the wall on the left" / "the ones behind the crates", and the grammar's L targets.

#include "core/voice/Grammar.hpp"
#include "core/vr/SquadContext.hpp"
#include "core/vr/SquadParser.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/LandmarkTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace sc = mevr::vr::squad_context;
namespace sp = mevr::vr::squad_parser;

sc::Cover Piece(std::uint64_t id, float x, float y, float yaw, std::vector<sc::Vec3> slots, char const* type = "") {
    sc::Cover c;
    c.id = id;
    c.position = {x, y, 0.0f};
    c.yawDeg = yaw;
    c.slots = std::move(slots);
    c.type = type;
    return c;
}

sc::Scene MakeScene() {
    sc::Scene scene;
    scene.player = {0.0f, 0.0f, 0.0f};
    scene.head = {0.0f, 0.0f, 1.6f};
    scene.headYawDeg = 0.0f;
    scene.combat = true;
    sc::Member ashley;
    ashley.letter = 'A';
    ashley.present = true;
    ashley.carried = 1u << 2;
    ashley.inHand = 2;
    scene.party = {ashley};
    sc::Hostile h;
    h.id = 11;
    h.position = {20.0f, -4.0f, 0.0f};
    h.type = "geth trooper";
    scene.hostiles = {h};
    // Left near: a nameless piece with two slots. Ahead mid: a wall with no slots facing the player. Right mid: a crate.
    scene.cover = {Piece(101, 6.0f, -4.0f, 0.0f, {{5.0f, -4.0f, 0.0f}, {6.0f, -4.0f, 0.0f}}), Piece(103, 15.0f, 0.0f, 180.0f, {}, "wall"),
        Piece(102, 7.0f, 5.0f, 0.0f, {{6.5f, 5.0f, 0.0f}}, "crate")};
    return scene;
}

std::vector<sc::RosterEntry> Roster() {
    sc::RosterEntry a;
    a.letter = 'A';
    a.name = "Ashley";
    a.aliases = {"Ash", "Williams"};
    a.weapons = 1u << 2;
    return {a};
}

void TestLandmarks() {
    sc::Scene scene = MakeScene();
    sc::GroupSettings gs;
    sc::Grouper grouper;
    auto const& groups = grouper.Update(scene, gs);
    CHECK(groups.size() == 1 && groups[0].id == 1);
    sc::LandmarkSettings ls;
    sc::Landmarker marker;
    auto const& landmarks = marker.Update(scene, ls);
    // Left to right: the nameless piece, the wall, the crate.
    CHECK(landmarks.size() == 3);
    CHECK(landmarks[0].id == 1 && landmarks[0].cover == 101 && landmarks[0].type == "cover");
    CHECK(landmarks[1].id == 2 && landmarks[1].cover == 103 && landmarks[1].type == "wall");
    CHECK(landmarks[2].id == 3 && landmarks[2].cover == 102 && landmarks[2].type == "crate");
    CHECK(landmarks[0].sector == sc::Sector::Left && landmarks[1].sector == sc::Sector::Ahead && landmarks[2].sector == sc::Sector::Right);
    CHECK(landmarks[0].band == sc::Band::Near && landmarks[1].band == sc::Band::Mid && landmarks[2].band == sc::Band::Mid);
    CHECK(landmarks[0].nearest && landmarks[0].leftmost && landmarks[2].rightmost && !landmarks[1].nearest);
    CHECK(std::abs(landmarks[0].centre.x - 5.5f) < 0.01f && std::abs(landmarks[0].centre.y + 4.0f) < 0.01f);  // the slots' centre
    CHECK(landmarks[1].slots.empty() && std::abs(landmarks[1].centre.x - 15.0f) < 0.01f);
    // The ids stay with the pieces: the crate gone and back keeps 3.
    sc::Scene fewer = scene;
    fewer.cover.pop_back();
    marker.Update(fewer, ls);
    CHECK(marker.Landmarks().size() == 2 && marker.Landmarks()[0].id == 1 && marker.Landmarks()[1].id == 2);
    marker.Update(scene, ls);
    CHECK(marker.Landmarks().size() == 3 && marker.Landmarks()[2].id == 3);
    CHECK(sc::Find(landmarks, 2) != nullptr && sc::Find(landmarks, 2)->type == "wall" && sc::Find(landmarks, 7) == nullptr);
    // Out of range or too many: left out, the nearest kept.
    sc::LandmarkSettings tight = ls;
    tight.rangeM = 10.0f;
    sc::Landmarker m2;
    CHECK(m2.Update(scene, tight).size() == 2);
    tight.rangeM = 25.0f;
    tight.maxLandmarks = 1;
    sc::Landmarker m3;
    CHECK(m3.Update(scene, tight).size() == 1 && m3.Landmarks()[0].cover == 101);
    // The block lists them after the groups, with the words.
    std::string const block = sc::Block(scene, groups, landmarks, {}, {}, "get behind the crate");
    CHECK(block.find("E1 geth trooper x1") != std::string::npos);
    CHECK(block.find("L1 cover | left near level | nearest leftmost\n") != std::string::npos);
    CHECK(block.find("L2 wall | ahead mid level\n") != std::string::npos);
    CHECK(block.find("L3 crate | right mid level | rightmost\n") != std::string::npos);
    CHECK(block.find("E1") < block.find("L1") && block.find("L3") < block.find("SAY"));
    // The point to take: the slot farthest from the threat (E1 at x 20): the slot at x 5; the wall's player side (the enemy
    // is beyond it).
    sc::Vec3 const p1 = sc::LandmarkPoint(landmarks[0], groups, scene);
    CHECK(std::abs(p1.x - 5.0f) < 0.01f && std::abs(p1.y + 4.0f) < 0.01f);
    sc::Vec3 const p2 = sc::LandmarkPoint(landmarks[1], groups, scene);
    CHECK(std::abs(p2.x - 14.0f) < 0.01f && std::abs(p2.y) < 0.01f);
    // No groups: the far side from the player.
    sc::Vec3 const p3 = sc::LandmarkPoint(landmarks[1], {}, scene);
    CHECK(std::abs(p3.x - 16.0f) < 0.01f);
    // "That crate": the right hand held on the crate around the word scores the landmark, not the group.
    sc::PoseHistory poses;
    for (int i = 0; i <= 20; ++i) {
        sc::PoseSample s;
        s.time = 9.0 + 0.1 * i;
        s.head = {scene.head, {0.0f, 1.0f, -0.1f}};  // looking away from E1 (else the look makes it a faint candidate too)
        s.rightAim = {{0.3f, 0.2f, 1.2f}, {6.5f - 0.3f, 5.0f - 0.2f, -1.2f}};
        s.rightValid = true;
        poses.Push(s);
    }
    sc::DeixisSettings ds;
    std::vector<sc::Candidate> const cands = sc::Resolve(poses, 10.0, groups, landmarks, scene, ds);
    CHECK(!cands.empty() && cands[0].landmark == 3 && cands[0].group == 0 && cands[0].score > 0.8f);
    // The validator: a hold or move to cover is fine, an attack on it isn't; a pointed crate is the hold's place.
    sc::Action hold;
    hold.who = {sc::Who::Member, 'A'};
    hold.order = sc::Order::Hold;
    hold.target = {sc::Target::Landmark, 3};
    sc::Resolved r = sc::Validate(hold, scene, groups, landmarks, {}, {});
    CHECK(r.ok && r.landmark == 3 && r.group == 0);
    hold.order = sc::Order::Attack;
    r = sc::Validate(hold, scene, groups, landmarks, {}, {});
    CHECK(!r.ok && r.problem.find("cover") != std::string::npos);
    hold.order = sc::Order::Move;
    hold.target = {sc::Target::Landmark, 9};
    r = sc::Validate(hold, scene, groups, landmarks, {}, {});
    CHECK(!r.ok);
    sc::Deictic that;
    that.index = 1;
    that.word = "that";
    that.candidates = cands;
    hold.order = sc::Order::Hold;
    hold.target = {sc::Target::Pointed, 1};
    r = sc::Validate(hold, scene, groups, landmarks, {that}, {});
    CHECK(r.ok && r.landmark == 3);
    hold.order = sc::Order::Attack;
    r = sc::Validate(hold, scene, groups, landmarks, {that}, {});
    CHECK(!r.ok && r.problem.find("not an enemy") != std::string::npos);
    // A move "over there" with nothing pointed at is still a move (the dispatcher has the floor).
    sc::Deictic there;
    there.index = 1;
    there.word = "there";
    hold.order = sc::Order::Move;
    r = sc::Validate(hold, scene, groups, landmarks, {there}, {});
    CHECK(r.ok && r.group == 0 && r.landmark == 0);
    // The line form.
    CHECK(sc::Line(sc::Action{{sc::Who::Member, 'A'}, -1, "", sc::Order::Hold, {sc::Target::Landmark, 2}}) == "A|-|-|hold|L2");
    auto const parsed = sc::ParseLine("A|-|-|hold|L2");
    CHECK(parsed && parsed->target.kind == sc::Target::Landmark && parsed->target.index == 2);
    CHECK(!sc::ParseLine("A|-|-|hold|L"));
    // The grammar offers the L ids, from the scene and from the block.
    mevr::voice::grammar::Options options;
    std::string const gbnf = mevr::voice::grammar::Gbnf(scene, groups, landmarks, options);
    CHECK(gbnf.find("\"L2\"") != std::string::npos && gbnf.find("\"L3\"") != std::string::npos);
    std::string const fromBlock = mevr::voice::grammar::GbnfFromBlock(block);
    CHECK(fromBlock.find("\"L3\"") != std::string::npos && fromBlock.find("\"E1\"") != std::string::npos);
    CHECK(mevr::voice::grammar::GbnfFromBlock("NOW combat\nLAST A hold E1 3s\nSAY hi\n").find("\"L1\"") == std::string::npos);  // LAST isn't a landmark
    // The parser.
    sp::Settings ps;
    auto const parse = [&](char const* text) {
        sp::Parsed const p = sp::Parse(text, scene, groups, landmarks, Roster(), ps);
        if (!p.clauses.empty()) {
            std::printf("  \"%s\" -> %s (%.2f%s%s)\n", text, sc::Line(p.clauses[0].action).c_str(), p.clauses[0].confidence,
                p.clauses[0].unresolved.empty() ? "" : "; ", p.clauses[0].unresolved.c_str());
        }
        return p;
    };
    sp::Parsed p = parse("Ashley, get behind the crate");
    CHECK(!p.empty && p.clauses.size() == 1 && p.clauses[0].action.order == sc::Order::Hold);
    CHECK(p.clauses[0].action.target.kind == sc::Target::Landmark && p.clauses[0].action.target.index == 3);
    CHECK(p.clauses[0].action.who.kind == sc::Who::Member && p.clauses[0].action.who.letter == 'A' && p.clauses[0].confidence > 0.9f);
    p = parse("take cover behind the wall");
    CHECK(!p.empty && p.clauses[0].action.order == sc::Order::Hold && p.clauses[0].action.target.index == 2 && p.clauses[0].action.target.kind == sc::Target::Landmark);
    p = parse("Ash, behind the cover on the left");
    CHECK(!p.empty && p.clauses[0].action.order == sc::Order::Hold && p.clauses[0].action.target.kind == sc::Target::Landmark && p.clauses[0].action.target.index == 1);
    p = parse("everyone, get to the nearest cover");
    CHECK(!p.empty && p.clauses[0].action.target.kind == sc::Target::Landmark && p.clauses[0].action.target.index == 1 && p.clauses[0].action.who.kind == sc::Who::All);
    p = parse("get behind that");
    CHECK(!p.empty && p.clauses[0].action.order == sc::Order::Hold && p.clauses[0].action.target.kind == sc::Target::Pointed && p.deictics.size() == 1);
    p = parse("Ashley, hold the wall");
    CHECK(!p.empty && p.clauses[0].action.order == sc::Order::Hold && p.clauses[0].action.target.index == 2);
    p = parse("duck behind the pillar");  // no pillar named here: the nameless piece may be it, a little less sure
    CHECK(!p.empty && p.clauses[0].action.target.kind == sc::Target::Landmark && p.clauses[0].action.target.index == 1);
    CHECK(p.clauses[0].confidence > 0.8f && p.clauses[0].confidence < 0.95f && p.notes.find("nameless") != std::string::npos);
    p = parse("get behind the crate on the left");  // the crate is on the right; the nameless piece on the left may be one
    CHECK(!p.empty && p.clauses[0].action.target.index == 1 && p.clauses[0].confidence > 0.8f && p.clauses[0].confidence < 0.95f);
    p = parse("take cover behind the wall on the right");  // the wall is ahead, the right piece is a crate: no such piece, the model's
    CHECK(!p.empty && p.clauses[0].confidence < 0.6f && p.clauses[0].unresolved.find("no cover") != std::string::npos);
    p = parse("take out the ones behind the crates");  // an attack near the cover, not a hold
    CHECK(!p.empty && p.clauses[0].action.order == sc::Order::Attack && p.clauses[0].action.target.kind != sc::Target::Landmark);
    p = parse("Ashley, attack the ones behind us");  // the Behind sector, no cover named
    CHECK(!p.empty && p.clauses[0].action.order == sc::Order::Attack && p.clauses[0].action.target.kind != sc::Target::Landmark);
    // Without landmarks the words are just words.
    sp::Parsed const none = sp::Parse("get behind the crate", scene, groups, Roster(), ps);
    CHECK(none.empty || none.clauses[0].action.target.kind != sc::Target::Landmark);
}

}  // namespace

int RunLandmarkTests() {
    TestLandmarks();
    std::printf("landmark tests: %d failure(s)\n", gFailures);
    return gFailures;
}

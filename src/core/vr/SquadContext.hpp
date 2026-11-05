// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The squad's world as a command sees it (research/54, "Code computes the scene; the model only picks an id"): the
// game-independent representation behind every squad order, gestured or spoken. The mod snapshots the Scene on the
// game thread (party, hostiles, what the gun and the head point at); this module clusters the hostiles into groups
// whose ids stay put through an encounter and describes each in the player's own words (left/ahead/right, near/mid/
// far, above/level/below, nearest, leftmost, biggest...), resolves "that" and "those" against a short history of the
// head's and the hands' rays at the moment the word was said, writes the compact text block a small language model
// reads, and defines the Action every order becomes: the d-pad's four orders from a gesture, an ME3-style "name,
// power" parsed by rules, or a model's one-line answer all end in the same struct, validated against the same scene,
// so one dispatcher serves them all. Unit-tested in tests/Main.cpp. No engine here; the words come from the mod.
//
// Conventions: the game's world frame in metres (the caller divides units by its scale; LE1 100 per metre), X forward
// at yaw 0, Y to the right, Z up (the engine's left-handed frame), so a bearing is + to the right; degrees; seconds on
// one clock shared by the scene, the pose history and the recognizer's word times.
namespace mevr::vr::squad_context {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
struct Ray {
    Vec3 origin;
    Vec3 direction;  // any length; zero = no ray
};
float Distance(Vec3 const& a, Vec3 const& b);
// The angle (degrees, 0..180) between a ray and the line from its origin to a point; 180 when the point sits on the
// origin or the ray has no direction.
float AngleTo(Ray const& ray, Vec3 const& point);
// Where a point lies from `from` facing `yawDeg` (the game's yaw: 0 = +X, 90 = +Y): + right, in -180..180.
float BearingDeg(Vec3 const& from, float yawDeg, Vec3 const& to);

using PawnId = std::uint64_t;  // whatever identifies a pawn while it lives (its object's index or address); 0 = none

// ---- The scene the mod snapshots -----------------------------------------------------------------------------------

inline constexpr int kWeapons = 4;  // squad_menu's types: 0 pistol, 1 shotgun, 2 assault rifle, 3 sniper rifle
char const* WeaponCode(int weapon);  // "PI", "SG", "AR", "SR"; "-" for anything else
std::optional<int> WeaponFromCode(std::string_view code);

struct Power {
    std::string code;        // the prompt's short name (e.g. "OVR"): three or four capitals, from the roster
    float readyInS = 0.0f;   // the cooldown left; 0 = ready
};
struct Member {
    char letter = 0;           // the roster letter the prompt uses (G Garrus, W Wrex, T Tali, L Liara, A Ashley, K Kaidan)
    bool present = false;      // in the party now (the roster lists all six; two come along)
    bool alive = true;
    std::uint8_t carried = 0;  // bit w: carries weapon type w
    int inHand = -1;           // the type in hand (-1: none)
    std::vector<Power> powers;
    Vec3 position;
};
struct Hostile {
    PawnId id = 0;
    Vec3 position;        // the pawn's location (its centre, like the player's)
    std::string type;     // the word for what it is ("geth trooper"); empty = unknown
    float health = 1.0f;  // 0 = dead (left out of the groups)
    bool visible = true;  // rendered within the last second
    std::string surface;  // where it stands, if the mod has a word ("bridge", "catwalk"); empty = none
};
struct Scene {
    double time = 0.0;
    Vec3 player;             // the player's pawn location (distances and heights are measured from it)
    Vec3 head;               // the eyes
    float headYawDeg = 0.0f;  // which way the player faces, flat
    bool combat = false;     // combat enabled and a weapon drawn
    int shepardWeapon = -1;  // in hand (-1: holstered)
    std::vector<Member> party;
    std::vector<Hostile> hostiles;
    PawnId aimTarget = 0;   // what the gun's line hits
    PawnId gazeTarget = 0;  // the game's selection from the head's gaze
};

// ---- Enemy groups --------------------------------------------------------------------------------------------------

enum class Sector : std::uint8_t { Ahead, Left, Right, Behind };
enum class Band : std::uint8_t { Near, Mid, Far };
enum class Elevation : std::uint8_t { Level, Above, Below };
char const* Name(Sector sector);
char const* Name(Band band);
char const* Name(Elevation elevation);

struct GroupSettings {
    float joinM = 6.0f;        // a hostile within this of a group's member joins the group...
    float splitAboveM = 2.5f;  // ...unless it stands this much higher or lower (another floor, the bridge)
    int maxGroups = 6;         // the nearest ones; the rest stay out of the block
    float nearM = 10.0f;       // closer than this: near
    float farM = 30.0f;        // farther than this: far
    float levelM = 1.5f;       // within this of the player's height: level
    float aheadDeg = 30.0f;    // a bearing within this of straight ahead: ahead
    float behindDeg = 120.0f;  // past this: behind
};

struct Group {
    int id = 0;                   // E<id>: given once, kept while any member lives
    std::vector<PawnId> members;
    Vec3 centre;
    std::string type;             // the members' most common word
    std::string surface;          // the first member's surface word
    float distanceM = 0.0f;       // from the player
    float bearingDeg = 0.0f;      // + right
    float heightM = 0.0f;         // above the player
    Sector sector = Sector::Ahead;
    Band band = Band::Near;
    Elevation elevation = Elevation::Level;
    bool visible = true;          // any member rendered recently
    // Against the other groups (none with a single group).
    bool nearest = false, farthest = false, leftmost = false, rightmost = false, highest = false, biggest = false;
};
int Count(Group const& group);

// Keeps the groups' ids across updates: a new cluster takes the id of the old group it shares most members with,
// else the next unused id. The groups come left to right (the fixed order the prompt uses).
class Grouper {
public:
    std::vector<Group> const& Update(Scene const& scene, GroupSettings const& settings);
    std::vector<Group> const& Groups() const { return groups_; }
    void Reset();  // a new encounter or level: ids start again at 1

private:
    std::vector<Group> groups_;
    int nextId_ = 1;
};
Group const* Find(std::vector<Group> const& groups, int id);
// The group a pawn belongs to (0: none).
int GroupOf(std::vector<Group> const& groups, PawnId pawn);

// ---- "Those": the rays around the word -----------------------------------------------------------------------------

struct PoseSample {
    double time = 0.0;
    Ray head;             // the eyes' line of sight (zero direction: unknown)
    Ray leftAim;          // the controllers' aim rays, in the world
    Ray rightAim;
    bool leftValid = false, rightValid = false;
};
class PoseHistory {
public:
    explicit PoseHistory(double keepS = 4.0) : keepS_(keepS) {}
    void Push(PoseSample const& sample);  // in time order; drops what is older than keepS
    // The samples with time in [from, to], oldest first.
    void Between(double from, double to, std::vector<PoseSample>& out) const;
    std::size_t Size() const { return samples_.size(); }
    void Clear() { samples_.clear(); }

private:
    double keepS_;
    std::deque<PoseSample> samples_;
};

// The kernels (research/54 "Those must be resolved when it was said"): the head looks at the target before the word
// (gaze leads speech, the head follows gaze), a pointing hand holds around the word. A temporal weight rises from 0
// at word - before to 1 at word + peak and falls to 0 at word + after; the angular weight is a Gaussian in the angle
// from the ray to the group's nearest member. Starting values, to fit on recordings.
struct DeixisSettings {
    float headPeakS = -0.4f, headBeforeS = 1.3f, headAfterS = 0.4f;
    float headSigmaDeg = 11.0f;   // head direction sits a median 12.6 degrees from where the eyes look
    float headUpBiasDeg = 6.0f;   // ...and about 6 degrees below it
    float headWeight = 1.0f;
    float handPeakS = 0.0f, handBeforeS = 1.2f, handAfterS = 0.5f;
    float handSigmaDeg = 3.0f;
    float handWeight = 1.5f;      // a point beats a look
    int top = 3;                  // candidates kept
    float minScore = 0.05f;       // below this a group isn't a candidate
};
struct Candidate {
    int group = 0;
    float score = 0.0f;  // 0..1
};
// Who "those" meant when it was said at `wordTime`: the groups the head and the hands pointed at around then, best
// first (the best source's score, normalised so a hand held on a group all along scores 1).
std::vector<Candidate> Resolve(PoseHistory const& history, double wordTime, std::vector<Group> const& groups,
    Scene const& scene, DeixisSettings const& settings);

// A deictic word as the block shows it: PT<index> "<word>" = E1 .81, E3 .12
struct Deictic {
    int index = 1;
    std::string word;
    std::vector<Candidate> candidates;
};

// ---- The action every order becomes -------------------------------------------------------------------------------

enum class Order : std::uint8_t { None, Attack, Move, Follow, Hold, Regroup, Use, Cancel, Repeat };
char const* Name(Order order);
std::optional<Order> ParseOrder(std::string_view text);

struct Who {
    enum Kind : std::uint8_t { None, Member, All, Last } kind = None;
    char letter = 0;  // Member
};
struct Target {
    enum Kind : std::uint8_t { None, Group, Pointed, Aim, Gaze, Last, Other, Self } kind = None;
    int index = 0;  // Group: the id; Pointed: the deictic's index
};
struct Action {
    Who who;
    int weapon = -1;     // squad_menu's type, or -1
    std::string power;   // the roster code, or empty
    Order order = Order::None;
    Target target;
};
// The one-line form, "G|SR|-|attack|PT1": what a model answers and what the log shows.
std::string Line(Action const& action);
std::optional<Action> ParseLine(std::string_view line);

// What was done, for LAST, OTHER and "do it again".
struct Command {
    char who = 0;        // the member letter (0: everyone)
    Action action;
    int group = 0;       // the resolved target (0: none)
    double time = 0.0;
};

// An action checked against the scene and resolved to one member (or everyone), a weapon they carry, a power they own
// and have ready, and one group. Not ok: `problem` says why; a cooldown is the one soft refusal (`notReady`,
// `readyInS`), so the caller can queue or say "not ready".
struct Resolved {
    bool ok = false;
    std::string problem;
    bool notReady = false;
    float readyInS = 0.0f;
    bool all = false;
    char who = 0;
    int weapon = -1;
    std::string power;
    Order order = Order::None;
    int group = 0;      // the target group (0: none)
    int runnerUp = 0;   // the next-best group, kept for "no, the other ones"
};
Resolved Validate(Action const& action, Scene const& scene, std::vector<Group> const& groups,
    std::vector<Deictic> const& deictics, std::vector<Command> const& history);

// ---- The text --------------------------------------------------------------------------------------------------------

// The dynamic block (research/54's schema): NOW, one PARTY line per present squadmate (weapons, * in hand; powers,
// ok or the seconds left), SHEP, one line per group (id, type, count, sector band elevation, surface, flags), AIM and
// GAZE, each deictic's candidates, the last commands with their age, and SAY <the transcript>.
std::string Block(Scene const& scene, std::vector<Group> const& groups, std::vector<Deictic> const& deictics,
    std::vector<Command> const& history, std::string_view said);

// The static prefix, cached once: the format's rules and the roster with aliases, weapon and power codes.
struct RosterEntry {
    char letter = 0;
    std::string name;
    std::vector<std::string> aliases;
    std::uint8_t weapons = 0;  // bit w
    std::vector<std::pair<std::string, std::string>> powers;  // code, spoken name
};
std::string Prefix(std::vector<RosterEntry> const& roster);

}  // namespace mevr::vr::squad_context

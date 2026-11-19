// SPDX-License-Identifier: MIT
#pragma once

#include <optional>
#include <cstdint>
#include <string_view>

// VR cover (research/23 section 6): the protection all three games give a player in cover, for a VR player who takes
// cover with their body. In each game the damage code cuts a hit on the player while they're in cover and not leaning out,
// if the shot comes from the cover's front (its direction within about 72 degrees of the cover's normal) and from more than
// 300 units away: LE1 by `CoverDamageReduction` (all of it on Casual and Normal, 90% above), LE2 to zero, LE3 by its own
// `CoverDamageReduction` (and LE3 adds 40% out of cover from Normal up). A VR player never takes the game's cover (no
// snapping: research/23 section 2), so this decides, for one hit, whether they're in cover against that shooter: the head
// plays the game's "not leaning out". Covered when the line from the eyes to the shooter meets a surface within `reachM` of
// the eyes; then `coverNormal` is the wall's facing (from the cover toward the player) for the game's own cone test. The
// line is traced from the eyes so the surface found is the player's own cover, not one near the shooter. Pure math, game
// independent (world units, Z up); unit-tested in tests/VrCoverTests.cpp. Each game's module traces the line and makes its
// damage code see the verdict.
namespace mevr::vr::cover {

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

struct Settings {
    bool on = true;
    double reachM = 1.2;          // the cover must stand within this of the eyes (crouched behind a crate, about 0.4-0.9)
    double unitsPerMeter = 100.0;
    double topNormalZ = 0.7;      // a surface whose normal is steeper than this (a crate's top, an overhang) gets its facing from the shot
};

// The first surface the line from the eyes toward the shooter meets.
struct Block {
    Vec3 point;
    Vec3 normal;  // the surface's normal there (facing back toward the eyes)
};

enum class Why {
    Off,          // the setting is off
    NoShooter,    // no shooter's place (the game's code then has none either)
    Exposed,      // nothing between the eyes and the shooter: the head is in the open
    CoverTooFar,  // something blocks the line, but farther than reachM from the eyes (not the player's cover)
    Overhead,     // the shooter is (nearly) straight above or below: no cover's facing
    Covered,      // the head is hidden behind cover from this shooter
};
char const* Name(Why why);

struct Verdict {
    Why why = Why::Off;
    Vec3 coverNormal;    // when covered: horizontal, unit, from the cover toward the player (the game's CoverNormal)
    double coverM = 0.0; // when blocked: the eyes' distance to the surface
    bool Covered() const {
        return why == Why::Covered;
    }
};

// A temporary native slot borrow for one exact damage invocation. The native adapter verifies
// object/world/name identity before Release; another native write to either field takes ownership.
struct SlotState {
    std::uintptr_t link = 0;
    std::int32_t index = -1;
    bool operator==(SlotState const&) const = default;
};
struct SlotLease {
    SlotState original{}, written{};
    bool active = false, suspended = false;
    bool Acquire(SlotState& state, SlotState candidate) {
        if (active || suspended || !candidate.link || candidate.index < 0) return false;
        original = state; written = candidate; state = candidate; active = true; return true;
    }
    bool Release(SlotState& state, bool sameObject) {
        bool const restore = active && sameObject && state == written;
        active = suspended = false;
        if (restore) state = original;
        return restore;
    }
    bool Suspend(SlotState& state, bool sameObject) {
        suspended = Release(state, sameObject);
        return suspended;
    }
    bool Resume(SlotState& state, bool sameObject) {
        bool const restore = suspended && !active && sameObject && state == original;
        suspended = false;
        return restore && Acquire(state, written);
    }
};

Verdict Judge(Vec3 const& eyes, std::optional<Vec3> const& shooter, std::optional<Block> const& block, Settings const& settings);

// ---- The level look (the sequels, research/23 section 7.7, 2026-11-18) ---------------------------------------------------------
// The games' own cone takes only the shot's direction against the cover slot's FLAT facing (ME2's `Normal(HitLocation -
// InstigatedBy.Pawn.Location) Dot Vector(slot rotation) < -0.3`), so Shepard crouched behind low cover is protected from a drone
// hovering above it: the shooter's height doesn't count. The eye line (Judge with the line from the eyes to the shooter) asks
// more: a drone 2 m above the eyes 15 m away sees over a barrier 0.5 m ahead whose top is a few cm above the eyes. The level look
// asks the game's question of the player's body: a surface on the flat line from the eyes toward the shooter, `reachM` long (at
// the eyes' height, less a drop)? Judge with that line's first surface gives its verdict (reach and facing as for the eye line).
// The eyes must still be under the cover's top: eyes over it see over it, the game's "leaning out".
enum class Rule : std::uint8_t {
    Line,    // the eye line only (ME1's rule since 2026-11-05)
    Level,   // the level look only
    Either,  // covered by either (the sequels' default since 2026-11-18)
};
char const* Name(Rule rule);
std::optional<Rule> ParseRule(std::string_view text);
struct LevelLine {
    Vec3 from, to;
};
// The level look's line: from the eyes (less `dropUnits`) `reachM` toward the shooter, flat. Nullopt with no shooter, or a
// shooter (nearly) straight above or below.
std::optional<LevelLine> Level(Vec3 const& eyes, std::optional<Vec3> const& shooter, Settings const& settings, double dropUnits = 0.0);
// The rule's verdict: Line the eye line's, Level the level look's, Either the covered one (the eye line's first), else the eye
// line's (exposed, too far, overhead).
Verdict Choose(Rule rule, Verdict const& line, Verdict const& level);

// The games' own cone and distance (LE1 BioPawnBehavior.ProxyTakeDamage, LE2 SFXGame.ModifyDamage, LE3
// SFXPawn_PlayerParty.ModifyDamage): the hit is cut when the shot's direction, from the instigator to the hit, against the
// cover's normal is below -0.3 (dot with the reversed normal) and the hit is more than 300 units from the instigator.
inline constexpr double kGameConeDot = -0.3;
inline constexpr double kGameMinUnits = 300.0;
bool GameCone(Vec3 const& coverNormal, Vec3 const& hitLocation, Vec3 const& instigatorLocation);

// Behind cover outside a shot (research/23 section 7.5, the owner 2026-11-16: "Faster recharge behind cover, yes"). ME2 shortens
// the shields' wait before they recharge for a pawn in cover and not leaning out (SFXShield_Base.Tick, bFastCoverRegen,
// PlayerCoverRegenTimerMultiplier 1.5); there is no shot, so no single shooter to judge against. The VR player is sheltered when
// the head is hidden from EVERY awake hostile that could see it, and by the player's own cover from at least one: each hostile
// judged as a shot from it would be (Judge), tallied here. Covered: hidden by a surface within reach. CoverTooFar: something
// far away stands between (that hostile can't see the head: neither cover nor exposure). Exposed or Overhead: in the open to
// that one, so not sheltered.
struct ShelterCount {
    int hostiles = 0, covered = 0, exposed = 0, blockedFar = 0, overhead = 0;
};
void Add(ShelterCount& count, Why why);
enum class ShelterWhy {
    NoHostile,     // no awake hostile was judged: nothing to shelter from (no faster recharge)
    Exposed,       // the head is in the open to at least one hostile
    NoCloseCover,  // hidden from every hostile, but only by things far away: not the player's own cover
    Sheltered,     // hidden from every hostile, by the player's own cover from at least one
};
char const* Name(ShelterWhy why);
ShelterWhy Shelter(ShelterCount const& count);

// The shields' wait before they recharge, measured from the game's own timer (ME2: SFXShield_Base.ShieldRegenTimer): a hit
// sets it to the regen delay, the shield's tick counts it down (faster in cover), and the recharge begins at 0. Step each mod
// tick with the timer read and whether the shelter was answered this tick; a wait that reached 0 is returned once.
struct RegenWait {
    bool waiting = false;
    double startS = 0.0, lastS = 0.0, shelteredS = 0.0;
    float startTimer = 0.0f, lastTimer = 0.0f;
};
struct RegenWaitDone {
    double waitS = 0.0;       // from the hit (the timer's rise) to the timer's 0
    double shelteredS = 0.0;  // how much of it the shelter was answered
    float startTimer = 0.0f;  // the delay the hit set
};
std::optional<RegenWaitDone> Step(RegenWait& wait, double nowS, float timer, bool sheltered);

} // namespace mevr::vr::cover

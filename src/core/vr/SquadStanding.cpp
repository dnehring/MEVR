// SPDX-License-Identifier: MIT
#include "core/vr/SquadStanding.hpp"

namespace mevr::vr::squad_standing {

char const* Name(Kind kind) {
    switch (kind) {
    case Kind::HoldAt: return "hold";
    case Kind::AttackGroup: return "attack";
    }
    return "hold";
}

char const* Name(Until until) {
    switch (until) {
    case Until::Calm: return "until it's calm";
    case Until::GroupGone: return "until the group is gone";
    case Until::Seconds: return "for a while";
    case Until::Relieved: return "until relieved";
    }
    return "until it's calm";
}

char const* Name(Phase phase) {
    switch (phase) {
    case Phase::Active: return "active";
    case Phase::Returning: return "returning";
    case Phase::Retreating: return "retreating";
    case Phase::Done: return "done";
    }
    return "done";
}

Issue Standing::Advance(Observation const& seen, Settings const& s) {
    Issue issue;
    if (phase == Phase::Done) {
        return issue;
    }
    ageS += seen.dt;
    sinceIssueS += seen.dt;
    if (!seen.alive) {
        phase = Phase::Done;
        issue.why = "down";
        return issue;
    }
    // Giving up: hurt or overwhelmed, they fall back to Shepard and the order ends.
    if (phase == Phase::Active && intent.mayRetreat && (seen.health < s.retreatHealth || seen.hostilesNearMember >= s.overwhelmCount)) {
        phase = Phase::Retreating;
        issue.kind = Issue::Follow;
        issue.why = seen.health < s.retreatHealth ? "hurt" : "overwhelmed";
        phase = Phase::Done;
        return issue;
    }
    if (phase == Phase::Active) {
        // The end of the order by itself.
        bool over = false;
        char const* why = "";
        if (ageS >= s.maxAgeS) {
            over = true;
            why = "stale";
        }
        switch (intent.until) {
        case Until::Calm:
            engaged = engaged || seen.hostilesNearPoint > 0;
            calmForS = seen.hostilesNearPoint == 0 && !seen.combat ? calmForS + seen.dt : 0.0;
            if (calmForS >= s.calmS) {
                over = true;
                why = engaged ? "the fighting stopped" : "quiet";
            }
            break;
        case Until::GroupGone:
            if (!seen.groupAlive) {
                over = true;
                why = "the group is gone";
            }
            break;
        case Until::Seconds:
            if (ageS >= intent.untilS) {
                over = true;
                why = "time's up";
            }
            break;
        case Until::Relieved:
            break;
        }
        if (over) {
            phase = Phase::Returning;
            if (intent.returnAfter) {
                issue.kind = Issue::Follow;
            }
            issue.why = why;
            phase = Phase::Done;
            return issue;
        }
        // Keeping it: back to the point when they drift, after the group again when it's dropped.
        switch (intent.kind) {
        case Kind::HoldAt:
            if (squad_context::Distance(seen.position, intent.point) > s.holdRadiusM && sinceIssueS >= s.reassertS) {
                issue.kind = Issue::MoveTo;
                issue.point = intent.point;
                issue.why = sinceIssueS > 1e8 ? "the order" : "drifted";
                sinceIssueS = 0.0;
            }
            break;
        case Kind::AttackGroup:
            if (seen.groupAlive && sinceIssueS >= s.reassertS) {
                issue.kind = Issue::Attack;
                issue.group = intent.group;
                issue.why = sinceIssueS > 1e8 ? "the order" : "again";
                sinceIssueS = 0.0;
            }
            break;
        }
    }
    return issue;
}

}  // namespace mevr::vr::squad_standing

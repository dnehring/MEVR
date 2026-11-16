// SPDX-License-Identifier: MIT
#include "core/vr/Traversal.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace mevr::vr::traversal {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

std::mutex gMutex;
Cue gLatest{};
std::uint64_t gSequence = 0;

double SmoothStep(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

bool Finite(double v) {
    return std::isfinite(v);
}

std::string Lower(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c != ' ' && c != '\t') {
            out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
    }
    return out;
}

} // namespace

char const* Label(Kind kind) {
    switch (kind) {
    case Kind::Vault: return "Vault";
    case Kind::ClimbUp: return "Climb up";
    case Kind::LadderUp: return "Climb ladder";
    case Kind::LadderDown: return "Descend ladder";
    case Kind::JumpDown: return "Drop down";
    case Kind::Evade: return "Evade";
    case Kind::Ladder: return "Use ladder";
    case Kind::None: break;
    }
    return "";
}

char const* Name(Kind kind) {
    switch (kind) {
    case Kind::Vault: return "vault";
    case Kind::ClimbUp: return "climb up";
    case Kind::LadderUp: return "ladder up";
    case Kind::LadderDown: return "ladder down";
    case Kind::JumpDown: return "jump down";
    case Kind::Evade: return "evade";
    case Kind::Ladder: return "ladder approach";
    case Kind::None: break;
    }
    return "none";
}

char const* Name(View view) {
    return view == View::Level ? "level" : "game";
}

char const* Name(Mask mask) {
    switch (mask) {
    case Mask::Vignette: return "vignette";
    case Mask::Blink: return "blink";
    case Mask::None: break;
    }
    return "none";
}

std::optional<View> ParseView(std::string_view text) {
    std::string const t = Lower(text);
    if (t == "game" || t == "0") return View::Game;
    if (t == "level" || t == "1") return View::Level;
    return std::nullopt;
}

std::optional<Mask> ParseMask(std::string_view text) {
    std::string const t = Lower(text);
    if (t == "none" || t == "off" || t == "0") return Mask::None;
    if (t == "vignette" || t == "1") return Mask::Vignette;
    if (t == "blink" || t == "2") return Mask::Blink;
    return std::nullopt;
}

char const* Name(PressWhy why) {
    switch (why) {
    case PressWhy::Fresh: return "a fresh press next to a vault or climb";
    case PressWhy::NotEligible: return "nothing to vault or climb from here";
    case PressWhy::UseFirst: return "A uses the selected object first";
    case PressWhy::FromMenu: return "pressed in a menu or without input focus";
    case PressWhy::DuringTraversal: return "pressed while a move ran";
    case PressWhy::AuthoredSeated: return "authored seated scene blocks traversal";
    case PressWhy::NotOnFoot: break;
    }
    return "not on foot";
}

char const* Name(Tap tap) {
    switch (tap) {
    case Tap::First: return "first tap";
    case Tap::Double: return "double tap";
    case Tap::Other: return "a press that can't vault";
    case Tap::None: break;
    }
    return "no press";
}

char const* Name(CommitWhy why) {
    switch (why) {
    case CommitWhy::Allowed: return "allowed (the second press of a double tap, its first move)";
    case CommitWhy::NoPress: return "no press of A near it";
    case CommitWhy::NotFresh: return "the press wasn't one that may commit";
    case CommitWhy::SingleTap: return "a single press, not a double tap";
    case CommitWhy::Repeated: break;
    }
    return "repeated (the press had already begun a move)";
}

Tap PressTracker::Step(PressInput const& in, double doubleTapS) {
    PressInput const before = haveBefore_ ? before_ : PressInput{in.nowS, false, in.focused, in.menuOpen, in.onFoot, in.eligible,
        in.useFirst, in.traversing, in.authoredSeated};
    bool const edge = in.down && !down_;
    // No first tap survives an interruption, including a menu that opens and closes between taps.
    // Keep held-button history: restoring gameplay must not manufacture a fresh edge.
    if (in.authoredSeated || in.menuOpen || !in.focused || !in.onFoot) last_.clean = false;
    if (!in.down && down_ && last_.valid && last_.upS < 0.0) {
        last_.upS = in.nowS;
    }
    Tap tap = Tap::None;
    if (edge) {
        Press p;
        p.valid = true;
        p.serial = last_.serial + 1;
        p.atS = in.nowS;
        // The verdict from the state before this tick: the game may have acted on the press already (used something, begun a move).
        bool const menu = in.menuOpen || !in.focused || before.menuOpen || !before.focused;
        bool const seated = in.authoredSeated || before.authoredSeated;
        p.clean = !seated && !menu && in.onFoot && before.onFoot && !before.traversing && !before.useFirst;
        if (seated) {
            p.why = PressWhy::AuthoredSeated;
        } else if (menu) {
            p.why = PressWhy::FromMenu;
        } else if (!in.onFoot || !before.onFoot) {
            p.why = PressWhy::NotOnFoot;
        } else if (before.traversing) {
            p.why = PressWhy::DuringTraversal;
        } else if (before.useFirst) {
            p.why = PressWhy::UseFirst;
        } else if (!before.eligible) {
            p.why = PressWhy::NotEligible;
        } else {
            p.why = PressWhy::Fresh;
        }
        // The second of a double tap: this press fresh, the one before it clean, not itself a double tap's second, let go before this
        // one went down, and within doubleTapS of it.
        bool const pairs = last_.valid && last_.clean && last_.tap != Tap::Double && last_.upS >= 0.0 &&
            p.atS - last_.atS <= std::max(0.0, doubleTapS);
        if (p.why == PressWhy::Fresh) {
            tap = pairs ? Tap::Double : Tap::First;
        } else {
            tap = p.clean ? Tap::First : Tap::Other;  // a clean press away from the obstacle may still begin a double tap
        }
        p.tap = tap;
        previous_ = last_;
        last_ = p;
    }
    down_ = in.down;
    before_ = in;
    haveBefore_ = true;
    return tap;
}

void PressTracker::Reset() {
    *this = PressTracker{};
}

Commit Judge(PressTracker& presses, double nowS, double windowS) {
    Commit c;
    Press const& last = presses.Last();
    if (!last.valid) {
        return c;
    }
    double const since = nowS - last.atS;
    // The game reads the pad within a tick of the mod seeing it, either side.
    if (since > windowS || since < -windowS) {
        return c;
    }
    c.press = last.serial;
    c.sincePressS = since;
    if (last.why != PressWhy::Fresh || !last.clean) {
        c.why = CommitWhy::NotFresh;
    } else if (last.commits > 0) {
        c.why = CommitWhy::Repeated;
    } else if (last.tap != Tap::Double) {
        c.why = CommitWhy::SingleTap;
    } else {
        c.why = CommitWhy::Allowed;
    }
    presses.NoteCommit();
    return c;
}

void PressTracker::NoteCommit() {
    if (last_.valid) {
        ++last_.commits;
    }
}

char const* Name(ActionPhase phase) {
    switch (phase) {
    case ActionPhase::Idle: return "idle";
    case ActionPhase::Requested: return "requested";
    case ActionPhase::Started: return "started";
    case ActionPhase::Completed: return "native end observed";
    case ActionPhase::Interrupted: return "native interrupt observed";
    case ActionPhase::EndedUnknown: return "left action without classified end";
    case ActionPhase::NoStartObserved: return "no start observed";
    case ActionPhase::Cancelled: return "observation cancelled";
    }
    return "unknown";
}
bool ActionLifecycle::Request(std::uint64_t epoch, double now) {
    if (Busy() || !std::isfinite(now)) return false;
    auto const next = serial + 1;
    *this = {};
    serial = next;
    generation = epoch;
    requestedAt = now;
    phase = ActionPhase::Requested;
    return true;
}
void ActionLifecycle::Observe(std::uint64_t epoch, std::uintptr_t current, Kind type, double now) {
    if (epoch != generation) { Cancel(); return; }
    if (phase == ActionPhase::Requested) {
        if (current != 0 && type != Kind::None) {
            action = current;
            kind = type;
            startedAt = now;
            phase = ActionPhase::Started;
        } else if (now - requestedAt >= 0.5) {
            phase = ActionPhase::NoStartObserved; // polling window, not proof of a native refusal
        }
    } else if (phase == ActionPhase::Started && current != action) {
        endedAt = now;
        phase = ActionPhase::EndedUnknown;
    }
}
void ActionLifecycle::End(std::uint64_t epoch, std::uintptr_t ended, bool interrupted, double now) {
    if (epoch != generation || ended == 0 || ended != action || phase == ActionPhase::Cancelled ||
        phase == ActionPhase::Idle || phase == ActionPhase::Requested) return;
    if (interrupted || phase != ActionPhase::Interrupted)
        phase = interrupted ? ActionPhase::Interrupted : ActionPhase::Completed;
    endedAt = now;
}
void ActionLifecycle::Cancel() {
    if (Busy() || phase == ActionPhase::EndedUnknown) phase = ActionPhase::Cancelled;
    action = 0;
}

Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(Vec3 a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}

double Length(Vec3 v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

double LengthXY(Vec3 v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

double SignedDegrees(double degrees) {
    if (!Finite(degrees)) {
        return 0.0;
    }
    double d = std::fmod(degrees, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}

void MoveRecord::Begin(Kind kind) {
    active_ = true;
    kind_ = kind;
    endT_ = -1.0;
    samples_.clear();
    samples_.reserve(512);
}

void MoveRecord::Add(Sample const& sample) {
    if (active_ && samples_.size() < 4096) {
        samples_.push_back(sample);
    }
}

void MoveRecord::End(double t) {
    if (active_ && endT_ < 0.0) {
        endT_ = t;
    }
}

bool MoveRecord::Settled(double speedMS, double forS) const {
    if (samples_.size() < 2 || endT_ < 0.0) {
        return false;
    }
    double const upm = unitsPerMeter > 1.0 ? unitsPerMeter : 100.0;
    Sample const& last = samples_.back();
    if (last.t - endT_ < forS) {
        return false;
    }
    for (std::size_t i = samples_.size() - 1; i > 0; --i) {
        Sample const& a = samples_[i - 1];
        Sample const& b = samples_[i];
        if (last.t - b.t > forS) {
            break;
        }
        double const dt = b.t - a.t;
        if (dt > 0.0 && Length(b.camera - a.camera) / upm / dt > speedMS) {
            return false;
        }
    }
    return true;
}

Summary MoveRecord::Current() const {
    Summary s;
    s.kind = kind_;
    s.samples = static_cast<unsigned>(samples_.size());
    if (samples_.empty()) {
        return s;
    }
    double const upm = unitsPerMeter > 1.0 ? unitsPerMeter : 100.0;
    Sample const& first = samples_.front();
    Sample const& last = samples_.back();
    s.seconds = (endT_ >= 0.0 ? endT_ : last.t) - first.t;
    s.pitchMinDeg = s.pitchMaxDeg = s.pitchStartDeg = first.pitch;
    s.rollMinDeg = s.rollMaxDeg = first.roll;
    double pawnPeak = first.pawn.z;
    double cameraPeak = first.camera.z;
    for (std::size_t i = 0; i < samples_.size(); ++i) {
        Sample const& c = samples_[i];
        s.pitchMinDeg = std::min(s.pitchMinDeg, c.pitch);
        s.pitchMaxDeg = std::max(s.pitchMaxDeg, c.pitch);
        s.rollMinDeg = std::min(s.rollMinDeg, c.roll);
        s.rollMaxDeg = std::max(s.rollMaxDeg, c.roll);
        pawnPeak = std::max(pawnPeak, c.pawn.z);
        cameraPeak = std::max(cameraPeak, c.camera.z);
        if (i == 0) {
            continue;
        }
        Sample const& p = samples_[i - 1];
        double const dt = std::max(0.0, c.t - p.t);
        if (!p.collision) {
            s.collisionOffS += dt;
        }
        s.maxStepM = std::max(s.maxStepM, Length(c.camera - p.camera) / upm);
        s.pawnStepM = std::max(s.pawnStepM, Length(c.pawn - p.pawn) / upm);
        if (i >= 2) {
            Sample const& pp = samples_[i - 2];
            double const span = c.t - pp.t;
            if (span > 1e-4) {
                s.cameraPeakSpeedMS = std::max(s.cameraPeakSpeedMS, Length(c.camera - pp.camera) / upm / span);
                s.cameraPeakVerticalMS = std::max(s.cameraPeakVerticalMS, std::abs(c.camera.z - pp.camera.z) / upm / span);
            }
        }
    }
    s.horizontalM = LengthXY(last.pawn - first.pawn) / upm;
    s.riseM = (pawnPeak - first.pawn.z) / upm;
    s.dropM = (last.pawn.z - first.pawn.z) / upm;
    s.yawTurnDeg = SignedDegrees(last.pawnYaw - first.pawnYaw);
    s.cameraTravelM = LengthXY(last.camera - first.camera) / upm;
    s.cameraRiseM = (cameraPeak - first.camera.z) / upm;
    s.cameraDropM = (last.camera.z - first.camera.z) / upm;
    // The bob: the camera's height against its own centred 0.3 s average (where the whole window lies inside the record).
    double low = 0.0;
    double high = 0.0;
    bool any = false;
    std::size_t lo = 0;
    std::size_t hi = 0;
    double sum = 0.0;
    for (std::size_t i = 0; i < samples_.size(); ++i) {
        double const t = samples_[i].t;
        while (hi < samples_.size() && samples_[hi].t <= t + 0.15) {
            sum += samples_[hi].camera.z;
            ++hi;
        }
        while (lo < hi && samples_[lo].t < t - 0.15) {
            sum -= samples_[lo].camera.z;
            ++lo;
        }
        if (t - first.t < 0.15 || last.t - t < 0.15 || hi <= lo) {
            continue;
        }
        double const off = samples_[i].camera.z - sum / static_cast<double>(hi - lo);
        low = any ? std::min(low, off) : off;
        high = any ? std::max(high, off) : off;
        any = true;
    }
    s.cameraBobM = (high - low) / upm;
    // At rest: the start of the last still stretch after the end (the camera under 0.05 m/s from there on).
    if (endT_ >= 0.0 && samples_.size() >= 2) {
        std::size_t still = samples_.size();
        for (std::size_t i = samples_.size() - 1; i > 0; --i) {
            Sample const& a = samples_[i - 1];
            Sample const& b = samples_[i];
            double const dt = b.t - a.t;
            if (dt > 0.0 && Length(b.camera - a.camera) / upm / dt > 0.05) {
                break;
            }
            still = i - 1;
        }
        if (still < samples_.size() && last.t - samples_[still].t >= 0.2) {
            s.settledS = std::max(0.0, samples_[still].t - endT_);
        }
    }
    return s;
}

Summary MoveRecord::Finish() {
    Summary const s = Current();
    active_ = false;
    return s;
}

void LevelView::Begin(Pose const& camera) {
    started_ = true;
    haveEnded_ = false;
    start_ = camera;
    z_ = camera.location.z;
    vz_ = 0.0;
    last_ = camera;
    last_.roll = 0.0;
}

Pose LevelView::During(Pose const& game, double dt, double tauS) {
    if (!started_) {
        return game;
    }
    if (!(tauS > 0.0)) {
        z_ = game.location.z;  // no filter: the game's height
        vz_ = 0.0;
    } else {
        // A critically damped spring for the height, in steps of at most 2 ms (frames up to 100 ms).
        double const omega = 1.0 / std::max(0.005, tauS);
        double left = std::clamp(Finite(dt) ? dt : 0.0, 0.0, 0.1);
        while (left > 0.0) {
            double const h = std::min(left, 0.002);
            double const a = omega * omega * (game.location.z - z_) - 2.0 * omega * vz_;
            vz_ += a * h;
            z_ += vz_ * h;
            left -= h;
        }
    }
    Pose p;
    p.location = {game.location.x, game.location.y, z_};
    p.pitch = start_.pitch;  // the pitch the view had when the move began (0 on foot without the stick's pitch), never the body's
    p.yaw = game.yaw;
    p.roll = 0.0;
    last_ = p;
    return p;
}

void LevelView::End() {
    if (!started_) {
        return;
    }
    ended_ = last_;
    haveEnded_ = true;
}

std::optional<Pose> LevelView::After(Pose const& game, double sinceEndS, double settleS) {
    if (!started_ || !haveEnded_) {
        return std::nullopt;
    }
    if (!(settleS > 0.0) || sinceEndS >= settleS) {
        started_ = false;
        haveEnded_ = false;
        return std::nullopt;
    }
    double const w = SmoothStep(std::max(0.0, sinceEndS) / settleS);
    Pose p;
    p.location = ended_.location + (game.location - ended_.location) * w;
    p.pitch = ended_.pitch + SignedDegrees(game.pitch - ended_.pitch) * w;
    p.yaw = SignedDegrees(ended_.yaw + SignedDegrees(game.yaw - ended_.yaw) * w);
    p.roll = ended_.roll + SignedDegrees(game.roll - ended_.roll) * w;
    return p;
}

void AuthoredYaw::Begin(double nativeYaw) {
    if (!Finite(nativeYaw)) return;
    held_ = SignedDegrees(nativeYaw + offset_);
    holding_ = true;
}
double AuthoredYaw::Update(double nativeYaw, bool actionOwned) {
    if (!Finite(nativeYaw)) return held_;
    if (holding_ && actionOwned) offset_ = SignedDegrees(held_ - nativeYaw);
    else holding_ = false;
    return SignedDegrees(nativeYaw + offset_);
}
bool PresentationFresh(Presentation const& p, std::uint64_t nowMs, std::uint32_t recenters,
    std::uint32_t sessions) {
    return p.size == sizeof(Presentation) && p.version == kPresentationVersion &&
        (p.flags & ~(kPresentationActive | kPresentationActionOwned)) == 0 &&
        p.timeMs != 0 && p.timeMs <= nowMs && nowMs - p.timeMs <= 100 &&
        p.recenters == recenters && p.sessions == sessions && p.sceneGeneration != 0 &&
        p.yawOffsetUU >= -32768 && p.yawOffsetUU <= 32768;
}

float MaskAmount(Mask mask, Settings const& s, bool running, double sinceStartS, double sinceEndS) {
    if (mask == Mask::None || !Finite(sinceStartS) || sinceStartS < 0.0) {
        return 0.0f;
    }
    if (mask == Mask::Vignette) {
        return running ? std::clamp(s.vignetteStrength, 0.0f, 1.0f) : 0.0f;
    }
    // The blink: black after blinkInS, held for blinkHoldS at most (or until the move ended), back over blinkOutS.
    double const in = std::max(0.001, static_cast<double>(s.blinkInS));
    double const out = std::max(0.001, static_cast<double>(s.blinkOutS));
    double holdEnd = in + std::max(0.0, static_cast<double>(s.blinkHoldS));
    if (!running) {
        double const endedAt = sinceStartS - std::max(0.0, Finite(sinceEndS) ? sinceEndS : 0.0);
        holdEnd = std::min(holdEnd, std::max(endedAt, 0.0));
    }
    double a = std::clamp(sinceStartS / in, 0.0, 1.0);
    if (sinceStartS > holdEnd) {
        double const down = std::clamp((sinceStartS - holdEnd) / out, 0.0, 1.0);
        a = std::min(a, std::clamp(holdEnd / in, 0.0, 1.0)) * (1.0 - down);
    }
    return static_cast<float>(std::clamp(a, 0.0, 1.0));
}

void Publish(Cue const& cue) {
    std::lock_guard lock(gMutex);
    gLatest = cue;
    gLatest.size = sizeof(Cue);
    gLatest.sequence = ++gSequence;
    gLatest.timeMs = GetTickCount64();
}

Cue Latest(std::uint64_t maxAgeMs) {
    Cue copy;
    {
        std::lock_guard lock(gMutex);
        copy = gLatest;
    }
    if (copy.timeMs == 0 || GetTickCount64() - copy.timeMs > maxAgeMs) {
        copy.size = 0;
        copy.visible = false;
        copy.blink = 0.0f;
    }
    return copy;
}

void Reset() {
    std::lock_guard lock(gMutex);
    gLatest = {};
}

Vec3 CuePoint(Vec3 slot, std::optional<Vec3> target, std::optional<double> coverTopZ, double raise, double fallbackRise) {
    Vec3 p = slot;
    if (target) {
        p.x = 0.5 * (slot.x + target->x);
        p.y = 0.5 * (slot.y + target->y);
    }
    p.z = (coverTopZ ? *coverTopZ : slot.z + fallbackRise) + raise;
    return p;
}

bool PlaceCue(XrVector3f const& position, XrVector3f const& head, float heightDeg, float aspect, Quad& out) {
    XrVector3f const toHead{head.x - position.x, head.y - position.y, head.z - position.z};
    double const distance = std::sqrt(static_cast<double>(toHead.x) * toHead.x + static_cast<double>(toHead.y) * toHead.y +
        static_cast<double>(toHead.z) * toHead.z);
    double const flat = std::sqrt(static_cast<double>(toHead.x) * toHead.x + static_cast<double>(toHead.z) * toHead.z);
    if (!(distance > 0.05) || !(flat > 0.01) || !(heightDeg > 0.0f) || !(aspect > 0.0f)) {
        return false;
    }
    // Upright, its front (+Z) turned to the head about +Y.
    double const theta = std::atan2(static_cast<double>(toHead.x), static_cast<double>(toHead.z));
    out.pose.orientation = {0.0f, static_cast<float>(std::sin(theta / 2.0)), 0.0f, static_cast<float>(std::cos(theta / 2.0))};
    out.pose.position = position;
    double const half = std::clamp(static_cast<double>(heightDeg), 0.1, 30.0) * 0.5 * kDegToRad;
    out.height = static_cast<float>(std::clamp(2.0 * distance * std::tan(half), 0.04, 0.20));
    out.width = out.height * aspect;
    return true;
}

bool CueShows(CueInput const& in) {
    return in.enabled && in.onFoot && in.focused && !in.menuOpen && in.eligible && !in.useFirst && !in.traversing &&
        Finite(in.distanceM) && in.distanceM <= in.rangeM;
}

} // namespace mevr::vr::traversal

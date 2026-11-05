// SPDX-License-Identifier: MIT
#include "game/Voice.hpp"

#include "core/Log.hpp"
#include "core/voice/Grammar.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/SquadParser.hpp"
#include "core/vr/TestRequest.hpp"
#include "core/vr/VoiceGesture.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "game/SquadContext.hpp"
#include "game/SquadOrders.hpp"
#include "game/Throwables.hpp"
#include "game/Vent.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace mevr::game::voice {

namespace sc = vr::squad_context;
namespace sp = vr::squad_parser;
namespace vg = vr::voice_gesture;
namespace json = vr::test_request;

namespace {

constexpr float kGripOn = 0.6f;
constexpr float kGripOff = 0.4f;

vg::Machine gMachine;
Link gLink;
bool gClaimed = false;
bool gOwnGrip = false;
std::uint32_t gLastCount = 0;
bool gHaveCount = false;
bool gToldNoHost = false;
double gClock = 0.0;
double gSinceKeyterms = 1e9;
std::string gKeyterms;
float gHandToEarM = 9.0f;
// The listen in progress: the last partial, when it settled, what was dispatched early.
std::string gPartial;
double gPartialSince = 0.0;
std::string gConsumed;  // the early-dispatched text (its tokens are dropped from the final)
std::uint64_t gLastListenStart = 0;
std::string gLastText, gLastOutcome;
// The model tier: the request the rules couldn't settle, its answer when it comes.
std::uint32_t gModelRequest = 0;
std::string gModelText;
double gModelAskedAt = 0.0;
unsigned gModelAsked = 0, gModelAnswered = 0, gModelGiven = 0;
std::string gLastModelAnswer;
unsigned gListens = 0, gBlips = 0, gLines = 0, gEarly = 0, gOrders = 0, gRefusals = 0;
vr::voice::State gState;

void Pulse(float seconds, float amplitude) {
    if (gLink.haptic != nullptr) gLink.haptic(0, seconds, amplitude);
}

// Tokens of `text` after `prefix`'s tokens (the early dispatch's words), or the whole text when they don't match.
std::string Remainder(std::string const& text, std::string const& prefix) {
    if (prefix.empty()) return text;
    std::vector<std::string> const a = sp::Tokens(text);
    std::vector<std::string> const b = sp::Tokens(prefix);
    if (b.empty() || a.size() < b.size()) return text;
    std::size_t same = 0;
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (sp::Similarity(a[i], b[i]) >= 0.8f) ++same;
    }
    if (same * 10 < b.size() * 8) return text;  // not the same words: parse it all
    std::string rest;
    for (std::size_t i = b.size(); i < a.size(); ++i) rest += (rest.empty() ? "" : " ") + a[i];
    return rest;
}

// A parse worth acting on before the player lets go: confident, complete, no power (a cooldown is spent at once).
bool EarlyWorthy(std::string const& text, double minConfidence) {
    sp::Parsed const parsed = sp::Parse(text, squad_context::Scene(), squad_context::Groups(), squad_context::Roster(), {});
    if (parsed.empty || parsed.confidence < minConfidence || parsed.correction) return false;
    for (sp::Clause const& c : parsed.clauses) {
        if (!c.action.power.empty() || c.action.order == sc::Order::Use || !c.unresolved.empty()) return false;
    }
    return true;
}

// What the rules couldn't settle goes to the model (research/54): the scene block with the SAY line and the first deictic
// resolved at its time, under the scene's grammar; the answer is dispatched when it comes (TakeModelAnswer).
void AskModel(std::string const& text, double secondsAgo) {
    if (gLink.askModel == nullptr) return;
    sp::Parsed const parsed = sp::Parse(text, squad_context::Scene(), squad_context::Groups(), squad_context::Roster(), {});
    std::string const word = parsed.deictics.empty() ? std::string() : parsed.deictics.front().word;
    std::string const block = squad_context::Block(text, word, secondsAgo);
    mevr::voice::grammar::Options options;
    options.deictics = static_cast<int>(parsed.deictics.size());
    std::string const gbnf = mevr::voice::grammar::Gbnf(squad_context::Scene(), squad_context::Groups(), options);
    std::string const prefix = squad_context::Prefix();
    std::uint32_t const id = gLink.askModel(prefix.c_str(), mevr::voice::grammar::Prompt(block).c_str(), gbnf.c_str());
    if (id == 0) return;
    gModelRequest = id;
    gModelText = text;
    gModelAskedAt = gClock;
    ++gModelAsked;
    log::Info("voice: the rules weren't sure: asked the model (#{}) about \"{}\"", id, text);
}

void TakeModelAnswer() {
    if (gLink.takeModelAnswer == nullptr) return;
    std::uint32_t id = 0, ms = 0;
    char text[1024]{};
    while (gLink.takeModelAnswer(&id, text, sizeof text, &ms)) {
        ++gModelAnswered;
        gLastModelAnswer = text;
        if (id != gModelRequest) {
            log::Info("voice: the model's answer #{} is stale (waiting for #{}): dropped", id, gModelRequest);
            continue;
        }
        gModelRequest = 0;
        std::string const answer = text;
        if (answer.empty() || answer == "none") {
            log::Info("voice: the model heard no order in \"{}\" ({} ms)", gModelText, ms);
            Pulse(0.05f, 0.6f);
            Pulse(0.05f, 0.6f);
            continue;
        }
        // One or more action lines, " ; " between.
        std::size_t at = 0;
        std::string outcomes;
        while (at <= answer.size()) {
            std::size_t const sep = answer.find(" ; ", at);
            std::string const line = answer.substr(at, sep == std::string::npos ? std::string::npos : sep - at);
            at = sep == std::string::npos ? answer.size() + 1 : sep + 3;
            auto const action = sc::ParseLine(line);
            std::string const outcome = action ? squad_orders::Give(*action) : "the line doesn't parse";
            outcomes += std::format("{}{} -> {}", outcomes.empty() ? "" : "; ", line, outcome);
            if (action && !outcome.starts_with("refused") && !outcome.starts_with("not ready")) ++gModelGiven;
        }
        gLastOutcome = "the model: " + outcomes;
        Pulse(0.08f, 0.8f);
        log::Info("voice: the model answered \"{}\" ({} ms, {:.2f} s after the ask): {}", gModelText, ms, gClock - gModelAskedAt, outcomes);
    }
}

void Dispatch(std::string const& text, double secondsAgo, char const* how) {
    gLastText = text;
    gLastOutcome = squad_orders::Say(text, secondsAgo);
    bool const unsure = gLastOutcome.starts_with("not sure");
    bool const refused = unsure || gLastOutcome.starts_with("nothing") || gLastOutcome.find("refused") != std::string::npos;
    if (unsure && gLink.askModel != nullptr) {
        AskModel(text, secondsAgo);
        log::Info("voice: {} \"{}\" -> {}", how, text, gLastOutcome);
        return;
    }
    if (refused) {
        ++gRefusals;
        Pulse(0.05f, 0.6f);  // "say again?": two short buzzes
        Pulse(0.05f, 0.6f);
    } else {
        ++gOrders;
        Pulse(0.08f, 0.8f);
    }
    log::Info("voice: {} \"{}\" -> {}", how, text, gLastOutcome);
}

// The listen's lines so far, by the recognizer's ids (its VAD splits "Garrus." from the order: they're joined before parsing).
struct Current {
    std::uint64_t id = 0;
    std::string text;
    float endS = 0.0f;
    std::uint32_t latencyMs = 0;
};
std::vector<Current> gCurrent;

std::string Joined() {
    std::string out;
    for (Current const& c : gCurrent) {
        if (c.text.empty()) continue;
        out += (out.empty() ? "" : " ") + c.text;
    }
    return out;
}

void TakeLines(hand_calibration::Voice const& s) {
    if (gLink.takeLine == nullptr) return;
    vr::voice::Line line;
    std::uint64_t const now = GetTickCount64();
    while (gLink.takeLine(&line)) {
        if (line.listenStartMs != gLastListenStart) {
            gLastListenStart = line.listenStartMs;
            gCurrent.clear();
            gPartial.clear();
            gConsumed.clear();
        }
        if (line.endOfListen != 0) {
            std::string const text = Joined();
            float endS = 0.0f;
            std::uint32_t latency = 0;
            for (Current const& c : gCurrent) {
                endS = std::max(endS, c.endS);
                latency = std::max(latency, c.latencyMs);
            }
            // The word's time: the last line's end on the host clock, plus the lag of saying "those" before the line ends.
            double const endMs = static_cast<double>(line.listenStartMs) + endS * 1000.0;
            double const secondsAgo = std::max(0.0, (static_cast<double>(now) - endMs) / 1000.0) + s.deicticLagS;
            ++gLines;
            std::string const rest = Remainder(text, gConsumed);
            bool const early = !gConsumed.empty();
            gCurrent.clear();
            gPartial.clear();
            gConsumed.clear();
            if (sp::Tokens(rest).empty()) {
                log::Info("voice: final \"{}\" ({} ms): {}", text, latency, early ? "already given" : "nothing said");
                continue;
            }
            Dispatch(rest, secondsAgo, early ? "the rest of the final" : "final");
            continue;
        }
        auto it = std::find_if(gCurrent.begin(), gCurrent.end(), [&](Current const& c) { return c.id == line.id; });
        if (it == gCurrent.end()) {
            gCurrent.push_back({line.id, line.text, line.startS + line.durationS, line.latencyMs});
        } else {
            it->text = line.text;
            it->endS = line.startS + line.durationS;
            it->latencyMs = line.latencyMs;
        }
        std::string const joined = Joined();
        if (joined != gPartial) {
            gPartial = joined;
            gPartialSince = gClock;
        }
    }
    // A partial that settled: given early when it parses whole (the research's semantic endpoint).
    if (s.earlyDispatch && !gPartial.empty() && gConsumed.empty() && gClock - gPartialSince >= s.settleMs / 1000.0 && gMachine.listening) {
        hand_calibration::SquadOrders const so = hand_calibration::GetSquadOrders();
        if (EarlyWorthy(gPartial, so.minConfidence)) {
            ++gEarly;
            gConsumed = gPartial;
            Dispatch(gPartial, s.deicticLagS, "early (the partial settled)");
        }
    }
}

void UpdateKeyterms(double dt) {
    gSinceKeyterms += dt;
    if (gSinceKeyterms < 2.0 || gLink.setKeyterms == nullptr) return;
    gSinceKeyterms = 0.0;
    std::string terms = "Shepard";
    // Title case: the game names them "ASHLEY", and the recognizer writes a key term as it's spelled.
    auto add = [&](std::string const& raw) {
        std::string t;
        bool start = true;
        for (char c : raw) {
            unsigned char const u = static_cast<unsigned char>(c);
            t += static_cast<char>(start ? std::toupper(u) : std::tolower(u));
            start = c == ' ' || c == '\'' || c == '-';
        }
        if (t.empty() || terms.find("," + t + ",") != std::string::npos || terms == t || terms.ends_with("," + t)) return;
        terms += "," + t;
    };
    for (sc::RosterEntry const& e : squad_context::Roster()) {
        add(e.name);
        for (auto const& [code, spoken] : e.powers) add(spoken);
    }
    if (terms != gKeyterms) {
        gKeyterms = terms;
        gLink.setKeyterms(terms.c_str());
    }
}

}  // namespace

void Tick(double deltaSeconds, Link const& link) {
    gClock += deltaSeconds;
    gLink = link;
    gClaimed = false;
    hand_calibration::Voice const s = hand_calibration::GetVoice();
    if (!link.has) {
        if (!gToldNoHost && s.gesture) {
            gToldNoHost = true;
            log::Info("voice: the host has no voice services (older than v31): no voice orders until the host is redeployed");
        }
        return;
    }
    if (link.state != nullptr) link.state(&gState);
    UpdateKeyterms(deltaSeconds);
    vr::controllers::State const c = vr::controllers::Latest();
    bool const was = gOwnGrip;
    gOwnGrip = c.active && (gOwnGrip ? c.left.squeeze > kGripOff : c.left.squeeze >= kGripOn);
    bool fresh = false;
    bool squeezed = gOwnGrip;
    if (link.leftGrip) {
        std::uint32_t const count = *link.leftGrip >> 1;
        bool const current = (*link.leftGrip & 1u) != 0;
        fresh = gHaveCount && count != gLastCount && current;
        squeezed = current;
        gLastCount = count;
        gHaveCount = true;
    } else {
        fresh = gOwnGrip && !was;
    }
    // The ear: the head's left side, a little below the eyes, in the world; the hand's distance from it.
    std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
    gHandToEarM = 9.0f;
    if (frame && frame->unitsPerMeter > 0.0 && c.active && c.left.gripValid) {
        double const upm = frame->unitsPerMeter;
        engine::Basis const head = engine::OrientationToWorld(*frame, frame->head.orientation);
        engine::UnrealVector const eyes = engine::ToWorld(*frame, frame->head.position);
        engine::UnrealVector const ear{eyes.x - head.right.x * s.earSideM * upm - head.up.x * s.earDownM * upm,
            eyes.y - head.right.y * s.earSideM * upm - head.up.y * s.earDownM * upm, eyes.z - head.right.z * s.earSideM * upm - head.up.z * s.earDownM * upm};
        engine::UnrealVector const hand = engine::ToWorld(*frame, c.left.grip.position);
        double const dx = hand.x - ear.x, dy = hand.y - ear.y, dz = hand.z - ear.z;
        gHandToEarM = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz) / upm);
    }
    ue3::Object* const controller = ue3::runtime::Ready() ? player::Controller() : nullptr;
    std::optional<std::uint8_t> const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    bool const ready = gState.status == static_cast<std::uint8_t>(vr::voice::Status::Ready);
    vg::Settings gs;
    gs.enabled = s.gesture;
    gs.radioRadiusM = static_cast<float>(s.radioRadiusM);
    gs.minHoldS = static_cast<float>(s.minHoldS);
    gs.maxHoldS = static_cast<float>(s.maxHoldS);
    vg::Input in;
    in.dt = static_cast<float>(deltaSeconds);
    in.usable = ready && mode && *mode == 0 && frame.has_value() && c.active && c.left.gripValid;
    in.newSqueeze = fresh && !throwables::ClaimedLeft() && !vent::ClaimedLeft();
    in.squeezed = squeezed;
    in.handToEarM = gHandToEarM;
    vg::Step const step = gMachine.Advance(in, gs);
    if (step.started) {
        gClaimed = true;
        ++gListens;
        gCurrent.clear();
        gPartial.clear();
        gConsumed.clear();
        Pulse(0.03f, 0.4f);
        if (link.setListen != nullptr) link.setListen(1);
        log::Info("voice: listening (the radio gesture, the hand {:.2f} m from the ear)", gHandToEarM);
    }
    if (step.ended || step.cancelled) {
        if (link.setListen != nullptr) link.setListen(0);
        if (step.blip) ++gBlips;
        Pulse(0.02f, 0.3f);
        log::Info("voice: {} after {:.2f} s{}", step.cancelled ? "cancelled" : "released", step.heldS, step.blip ? " (a blip: nothing said)" : "");
    }
    TakeLines(s);
    TakeModelAnswer();
}

bool ClaimedLeft() {
    return gClaimed;
}

std::string Console(std::string_view rest) {
    std::string line(rest);
    while (!line.empty() && line.front() == ' ') line.erase(line.begin());
    if (line == "status" || line.empty()) {
        return std::format("voice: host status {} ({}), listening {}, device \"{}\", model {}, {} listens, {} lines, {} early, {} orders, {} refusals; last: \"{}\" -> {}",
            gState.status, gState.error[0] != '\0' ? gState.error : "-", gState.listening != 0 ? "yes" : "no", gState.device, gState.model, gListens, gLines, gEarly,
            gOrders, gRefusals, gLastText, gLastOutcome);
    }
    if (line.starts_with("say ")) {
        // The typed stand-in for a transcript on the whole path: the rules, and the model when they're not sure (`mevr say`
        // alone stops at the rules). The answer comes on the host's worker a second later: the log's `voice: the model answered`.
        Dispatch(line.substr(4), 0.0, "typed");
        return "voice: " + gLastOutcome + (gLastOutcome.starts_with("not sure") && gLink.askModel != nullptr ? " -> asked the model" : "");
    }
    if (!gLink.has || gLink.command == nullptr) return "voice: the host has no voice services (older than v31)";
    gLink.command(line.c_str());
    return std::format("voice: request \"{}\" sent to the host (its log says what came of it)", line);
}

std::string StatusJson() {
    return std::format("{{\"listening\": {}, \"heldS\": {}, \"handToEarM\": {}, \"hostStatus\": {}, \"hostListening\": {}, \"level\": {}, \"listens\": {}, \"blips\": {}, "
                       "\"lines\": {}, \"early\": {}, \"orders\": {}, \"refusals\": {}, \"partial\": {}, \"lastText\": {}, \"lastOutcome\": {}, \"keyterms\": {}, "
                       "\"model\": {{\"has\": {}, \"asked\": {}, \"answered\": {}, \"given\": {}, \"pending\": {}, \"lastAnswer\": {}}}}}",
        gMachine.listening ? "true" : "false", json::JsonNumber(gMachine.heldS), json::JsonNumber(gHandToEarM), gState.status, gState.listening != 0 ? "true" : "false",
        json::JsonNumber(gState.level), gListens, gBlips, gLines, gEarly, gOrders, gRefusals, json::JsonString(gPartial), json::JsonString(gLastText),
        json::JsonString(gLastOutcome), json::JsonString(gKeyterms), gLink.askModel != nullptr ? "true" : "false", gModelAsked, gModelAnswered, gModelGiven, gModelRequest,
        json::JsonString(gLastModelAnswer));
}

void LogStats() {
    if (!gLink.has) return;
    log::Info("voice: {} listens ({} blips), {} final lines, {} given early, {} orders, {} refusals; host status {}{}{}", gListens, gBlips, gLines, gEarly, gOrders, gRefusals,
        gState.status, gState.error[0] != '\0' ? std::format(" ({})", gState.error) : std::string(),
        gLastText.empty() ? std::string() : std::format("; last: \"{}\" -> {}", gLastText, gLastOutcome));
}

void Shutdown() {
    if (gMachine.listening && gLink.setListen != nullptr) gLink.setListen(0);
    gMachine.Reset();
    gCurrent.clear();
    gPartial.clear();
    gConsumed.clear();
}

}  // namespace mevr::game::voice

// SPDX-License-Identifier: MIT
#include "core/vr/Thespian.hpp"

#include "core/voice/ScriptMatch.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

namespace mevr::vr::thespian {

Strictness ParseStrictness(std::string_view text, Strictness fallback) {
    std::string lower;
    for (char const c : text) {
        if (c != ' ' && c != '\t') lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "relaxed" || lower == "0") return Strictness::Relaxed;
    if (lower == "performer" || lower == "1") return Strictness::Performer;
    if (lower == "method" || lower == "2") return Strictness::Method;
    return fallback;
}

char const* Name(Strictness strictness) {
    switch (strictness) {
    case Strictness::Relaxed: return "relaxed";
    case Strictness::Performer: return "performer";
    case Strictness::Method: return "method";
    }
    return "performer";
}

bool Allows(Strictness strictness, Match const& match) {
    if (!match.decided || match.leader < 0) return false;
    switch (strictness) {
    case Strictness::Relaxed: return true;
    case Strictness::Performer: return match.byLabel || match.coverage >= 0.5f;
    case Strictness::Method: return match.coverage >= 0.9f;
    }
    return false;
}

Output Step(State& state, Settings const& settings, Input const& in) {
    Output out;
    float const dt = static_cast<float>(std::clamp(in.dt, 0.0, 0.25));
    if (!in.wheelUp) {
        state = {};
        return out;  // Off
    }
    if (state.phase == Phase::Off) {
        // The wheel came up: a fresh transcript for this turn.
        state.phase = Phase::Armed;
        state.otherSpoke = in.otherSpeaking;
        state.unsureLeft = 0.0f;
        out.restart = true;
        out.phase = Phase::Armed;
        out.mic = Mic::Armed;
        return out;
    }
    if (state.phase == Phase::Committed) {
        state.committedS += dt;
        if (state.committedS < settings.committedMaxS) {
            out.phase = Phase::Committed;
            out.mic = Mic::Understood;
            out.leader = state.committed;
            return out;
        }
        // The game didn't take it (the wheel is still up): listen again.
        state.phase = Phase::Armed;
        state.committed = -1;
        out.restart = true;
        out.phase = Phase::Armed;
        out.mic = Mic::Armed;
        return out;
    }

    // Words heard only while the other person spoke don't count: when their line ends with no card decided, the
    // transcript starts again (their voice in the microphone, a cough, an aside).
    bool const otherEnded = state.otherSpoke && !in.otherSpeaking;
    state.otherSpoke = in.otherSpeaking;
    if (otherEnded && !in.now.decided && in.now.words) {
        state.phase = Phase::Armed;
        out.restart = true;
        out.phase = Phase::Armed;
        out.mic = Mic::Armed;
        return out;
    }

    state.unsureLeft = std::max(0.0f, state.unsureLeft - dt);
    state.phase = !in.now.words ? Phase::Armed : in.now.decided ? Phase::Leading : Phase::Hearing;
    out.leader = in.now.decided ? in.now.leader : -1;

    if (!in.otherSpeaking && in.now.words) {
        std::uint32_t const endMs = static_cast<std::uint32_t>(std::max(settings.endMs, 0));
        std::uint32_t const pauseMs = static_cast<std::uint32_t>(std::max(settings.pauseMs, settings.endMs));
        int take = -1;
        if (in.now.decided && in.now.reachedEnd && in.silentMs >= endMs && Allows(settings.strictness, in.now)) {
            take = in.now.leader;  // the line was read to its end
        } else if (in.silentMs >= pauseMs && Allows(settings.strictness, in.atEnd)) {
            take = in.atEnd.leader;  // stopped part-way, and the matcher is sure at the end
        }
        if (take >= 0) {
            state.phase = Phase::Committed;
            state.committed = take;
            state.committedS = 0.0f;
            state.unsureLeft = 0.0f;
            out.commit = take;
            out.leader = take;
            out.phase = Phase::Committed;
            out.mic = Mic::Understood;
            return out;
        }
        if (in.silentMs >= pauseMs) {
            state.unsureLeft = settings.unsureS;  // quiet, and no card can be taken: say so, keep listening
        }
        if (in.silentMs >= static_cast<std::uint32_t>(std::max(settings.staleMs, settings.pauseMs))) {
            // Words nobody could place, and a long quiet: they would only weigh on what is said next.
            state.phase = Phase::Armed;
            out.restart = true;
            out.leader = -1;
        }
    }

    out.phase = state.phase;
    out.mic = state.unsureLeft > 0.0f ? (state.phase == Phase::Leading ? Mic::More : Mic::Unsure) : state.phase == Phase::Armed ? Mic::Armed : Mic::Hearing;
    return out;
}

Tone ToneOf(int category, int guiStyle) {
    if (guiStyle == 4) return Tone::Unavailable;
    if (guiStyle == 1) return Tone::Charm;
    if (guiStyle == 2) return Tone::Intimidate;
    switch (category) {
    case 1:
    case 3: return Tone::Agree;
    case 2:
    case 4: return Tone::Disagree;
    case 5: return Tone::Investigate;
    default: return Tone::Default;
    }
}

char const* ClassOf(Tone tone) {
    switch (tone) {
    case Tone::Default: return "default";
    case Tone::Agree: return "agree";
    case Tone::Disagree: return "disagree";
    case Tone::Investigate: return "investigate";
    case Tone::Charm: return "charm";
    case Tone::Intimidate: return "intimidate";
    case Tone::Unavailable: return "unavailable";
    }
    return "default";
}

namespace {

void Escape(std::string& out, std::string_view text) {
    for (char const c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\n':
        case '\r':
        case '\t': out += ' '; break;
        default: out += c; break;
        }
    }
}

std::string_view Text(char const* chars, std::size_t capacity) {
    return std::string_view(chars, strnlen(chars, capacity));
}

char const* LookClass(Look look) {
    switch (look) {
    case Look::Idle: return "idle";
    case Look::Hover: return "hover";
    case Look::Leading: return "leading";
    case Look::Dim: return "dim";
    case Look::Committed: return "committed";
    case Look::Unavailable: return "off";
    }
    return "idle";
}

}  // namespace

std::string RowsRml(Panel const& panel) {
    std::string out;
    std::size_t const count = std::min<std::size_t>(panel.cardCount, kMaxCards);
    for (std::size_t i = 0; i < count; ++i) {
        PanelCard const& card = panel.cards[i];
        bool const topic = i >= panel.mainCount;
        if (topic && i == panel.mainCount && i > 0) {
            out += "<div class=\"divider\"/>";
        }
        out += "<div class=\"row ";
        out += ClassOf(static_cast<Tone>(card.tone));
        out += ' ';
        out += LookClass(static_cast<Look>(card.look));
        out += topic ? " topic\">" : "\">";
        out += "<div class=\"tag\"><div class=\"mark\"/><div class=\"label\">";
        std::string_view const label = Text(card.label, kLabelChars);
        std::string_view const line = Text(card.line, kLineChars);
        Escape(out, label);
        out += "</div></div><div class=\"bar\"/><div class=\"line\">";
        // The line word by word; a reply without one (a menu card) shows its label as the thing to say.
        std::string_view const script = line.empty() ? label : line;
        std::size_t at = 0;
        std::size_t word = 0;
        for (auto const& [begin, end] : mevr::voice::script_match::WordSpans(script)) {
            // What stands before a word (a space, an opening quote) goes inside its span: the renderer drops a text node
            // of spaces alone between two spans.
            bool const said = !line.empty() && word < 64 && (card.lit >> word & 1ull) != 0;
            bool const next = !line.empty() && card.next >= 0 && static_cast<std::size_t>(card.next) == word;
            out += said ? "<span class=\"said\">" : next ? "<span class=\"next\">" : "<span>";
            Escape(out, script.substr(at, end - at));
            out += "</span>";
            at = end;
            ++word;
        }
        Escape(out, script.substr(at));
        out += "</div></div>";
    }
    return out;
}

char const* MicClass(Mic mic) {
    switch (mic) {
    case Mic::Off: return "";
    case Mic::Armed: return "armed";
    case Mic::Hearing: return "hearing";
    case Mic::Understood: return "understood";
    case Mic::Unsure: return "unsure";
    case Mic::More: return "more";
    }
    return "";
}

std::string PromptText(Panel const& panel) {
    switch (static_cast<Mic>(panel.mic)) {
    case Mic::Off: return {};
    case Mic::Armed: return "Read a line aloud";
    case Mic::Hearing: return "Listening...";
    case Mic::Unsure: return "Didn't catch that";
    case Mic::More: return "Keep reading...";
    case Mic::Understood:
        for (std::size_t i = 0; i < std::min<std::size_t>(panel.cardCount, kMaxCards); ++i) {
            if (static_cast<Look>(panel.cards[i].look) == Look::Committed || static_cast<Look>(panel.cards[i].look) == Look::Leading) {
                return std::string(Text(panel.cards[i].label, kLabelChars));
            }
        }
        return "Understood";
    }
    return {};
}

XrPosef PlacePanel(XrVector3f const& eyes, XrVector3f const& headForward, bool hasSpeaker, XrVector3f const& speakerHead, float heightM) {
    constexpr float kPi = 3.14159265f;
    // The flat way the panel stands: toward the speaker, else where the head looks.
    float fx = headForward.x, fz = headForward.z;
    float topDeg = -kBelowHorizonDeg;
    if (hasSpeaker) {
        float const dx = speakerHead.x - eyes.x, dz = speakerHead.z - eyes.z;
        float const flat = std::sqrt(dx * dx + dz * dz);
        if (flat > 0.2f) {
            fx = dx;
            fz = dz;
            float const elevation = std::atan2(speakerHead.y - eyes.y, flat) * 180.0f / kPi;
            topDeg = std::min(elevation - kBelowSpeakerDeg, -kBelowHorizonDeg);
        }
    }
    float const length = std::sqrt(fx * fx + fz * fz);
    if (length > 1e-4f) {
        fx /= length;
        fz /= length;
    } else {
        fx = 0.0f;
        fz = -1.0f;
    }
    // The centre: half the panel's height (as an angle at its distance) under the top edge, never under 55 degrees down.
    float const halfDeg = std::atan2(heightM * 0.5f, kPanelDistanceM) * 180.0f / kPi;
    float const centreDeg = std::max(topDeg - halfDeg, -55.0f);
    float const c = std::cos(centreDeg * kPi / 180.0f), s = std::sin(centreDeg * kPi / 180.0f);
    XrPosef pose{};
    pose.position = {eyes.x + fx * c * kPanelDistanceM, eyes.y + s * kPanelDistanceM, eyes.z + fz * c * kPanelDistanceM};
    // Facing the eyes: +Z from the centre to the eyes, +X to the reader's right, +Y up along the panel.
    XrVector3f const z{-fx * c, -s, -fz * c};
    XrVector3f x{z.z, 0.0f, -z.x};  // up (0,1,0) cross z
    float const xl = std::sqrt(x.x * x.x + x.z * x.z);
    x = {x.x / xl, 0.0f, x.z / xl};
    XrVector3f const y{z.y * x.z - z.z * x.y, z.z * x.x - z.x * x.z, z.x * x.y - z.y * x.x};
    // The rotation matrix with columns x, y, z to a quaternion.
    float const m00 = x.x, m01 = y.x, m02 = z.x, m10 = x.y, m11 = y.y, m12 = z.y, m20 = x.z, m21 = y.z, m22 = z.z;
    float const trace = m00 + m11 + m22;
    XrQuaternionf q{};
    if (trace > 0.0f) {
        float const k = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m21 - m12) / k, (m02 - m20) / k, (m10 - m01) / k, 0.25f * k};
    } else if (m00 > m11 && m00 > m22) {
        float const k = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * k, (m01 + m10) / k, (m02 + m20) / k, (m21 - m12) / k};
    } else if (m11 > m22) {
        float const k = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / k, 0.25f * k, (m12 + m21) / k, (m02 - m20) / k};
    } else {
        float const k = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / k, (m12 + m21) / k, 0.25f * k, (m10 - m01) / k};
    }
    pose.orientation = q;
    return pose;
}

}  // namespace mevr::vr::thespian

// SPDX-License-Identifier: MIT
// The voice path's game-independent parts (research/54): the WAV reader and writer the developer channel and the bench use,
// and the push-to-talk gesture's rules (core/vr/VoiceGesture).
#include "core/voice/Grammar.hpp"
#include "core/voice/Wav.hpp"
#include "core/vr/VoiceGesture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what) {
    if (!ok) {
        ++gFailures;
        std::printf("FAIL voice: %s\n", what);
    }
}

void TestWav() {
    using namespace mevr::voice::wav;
    std::vector<float> tone(1600);
    for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = 0.5f * std::sin(static_cast<float>(i) * 0.1f);
    std::vector<std::uint8_t> const bytes = Encode16(tone, 16000);
    Check(bytes.size() == 44 + tone.size() * 2, "a 16-bit mono WAV is 44 bytes of header plus the samples");
    std::string why;
    auto const back = Parse(bytes, &why);
    Check(back.has_value(), "the encoded WAV parses");
    if (back) {
        Check(back->sampleRate == 16000 && back->channels == 1 && back->mono.size() == tone.size(), "the rate, channels and length round-trip");
        float worst = 0.0f;
        for (std::size_t i = 0; i < tone.size(); ++i) worst = std::max(worst, std::fabs(back->mono[i] - tone[i]));
        Check(worst < 1.0f / 32000.0f, "16-bit samples round-trip within a quantum");
    }
    // Stereo float, mixed down.
    std::vector<std::uint8_t> f;
    auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) f.push_back(static_cast<std::uint8_t>(v >> (8 * i))); };
    auto put16 = [&](std::uint16_t v) { f.push_back(static_cast<std::uint8_t>(v)); f.push_back(static_cast<std::uint8_t>(v >> 8)); };
    f.insert(f.end(), {'R', 'I', 'F', 'F'});
    put32(36 + 16);
    f.insert(f.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(16);
    put16(3);  // float
    put16(2);
    put32(48000);
    put32(48000 * 8);
    put16(8);
    put16(32);
    f.insert(f.end(), {'d', 'a', 't', 'a'});
    put32(16);
    float const samples[4] = {1.0f, 0.0f, -0.5f, 0.5f};  // two frames: (1, 0) -> 0.5, (-0.5, 0.5) -> 0
    for (float const s : samples) {
        std::uint32_t u = 0;
        std::memcpy(&u, &s, 4);
        put32(u);
    }
    auto const mixed = Parse(f, &why);
    Check(mixed.has_value() && mixed->sampleRate == 48000 && mixed->channels == 2 && mixed->mono.size() == 2, "a stereo float WAV parses to two mono frames");
    if (mixed && mixed->mono.size() == 2) Check(std::fabs(mixed->mono[0] - 0.5f) < 1e-6f && std::fabs(mixed->mono[1]) < 1e-6f, "channels are averaged");
    Check(!Parse({1, 2, 3}, &why).has_value() && why == "not a RIFF WAVE file", "junk is refused with a reason");
}

void TestGesture() {
    using namespace mevr::vr::voice_gesture;
    Settings s;
    Machine m;
    Input in;
    in.dt = 0.016f;
    in.usable = true;
    // The hand away from the ear: a squeeze is somebody else's.
    in.handToEarM = 0.5f;
    in.newSqueeze = true;
    in.squeezed = true;
    Step st = m.Advance(in, s);
    Check(!st.started && !m.listening, "a squeeze with the hand away from the ear doesn't start a listen");
    // At the ear: it starts, and holds while squeezed wherever the hand goes.
    in.handToEarM = 0.1f;
    st = m.Advance(in, s);
    Check(st.started && m.listening, "a new squeeze at the ear starts a listen");
    in.newSqueeze = false;
    in.handToEarM = 0.6f;
    for (int i = 0; i < 10; ++i) st = m.Advance(in, s);
    Check(m.listening && !st.ended, "the listen holds while the grip is held, hand anywhere");
    in.squeezed = false;
    st = m.Advance(in, s);
    Check(st.ended && !m.listening && st.heldS > 0.1f, "letting go ends it, with how long it was held");
    // A blip shorter than minHoldS is dropped.
    in.handToEarM = 0.1f;
    in.newSqueeze = true;
    in.squeezed = true;
    m.Advance(in, s);
    in.newSqueeze = false;
    in.squeezed = false;
    st = m.Advance(in, s);
    Check(st.ended && st.blip, "a blip is reported as such (nothing was said)");
    // Not usable mid-listen (a menu opened): cancelled.
    in.newSqueeze = true;
    in.squeezed = true;
    m.Advance(in, s);
    in.newSqueeze = false;
    in.usable = false;
    st = m.Advance(in, s);
    Check(st.cancelled && !m.listening, "a listen is cancelled when the gesture stops being usable");
    // Held past maxHoldS: ended (the stream can't grow forever).
    in.usable = true;
    in.newSqueeze = true;
    in.squeezed = true;
    m.Advance(in, s);
    in.newSqueeze = false;
    in.dt = 1.0f;
    bool ended = false;
    for (int i = 0; i < 20 && !ended; ++i) ended = m.Advance(in, s).ended;
    Check(ended, "a listen held longer than maxHoldS ends by itself");
}

void TestGrammar() {
    using namespace mevr::vr::squad_context;
    Scene scene;
    Member g;
    g.letter = 'G';
    g.present = true;
    g.powers.push_back({"OVR", 0.0f});
    g.powers.push_back({"DMP", 9.0f});
    Member w;
    w.letter = 'W';
    w.present = true;
    w.powers.push_back({"THR", 0.0f});
    Member l;
    l.letter = 'L';
    l.present = false;  // not along: never a legal WHO
    l.powers.push_back({"SNG", 0.0f});
    scene.party = {g, w, l};
    std::vector<Group> groups(2);
    groups[0].id = 1;
    groups[1].id = 3;
    mevr::voice::grammar::Options o;
    o.deictics = 2;
    std::string const gbnf = mevr::voice::grammar::Gbnf(scene, groups, o);
    Check(gbnf.find("who ::= \"-\" | \"ALL\" | \"G\" | \"W\"\n") != std::string::npos, "WHO lists the squadmates present and ALL");
    Check(gbnf.find("\"L\"") == std::string::npos, "an absent squadmate isn't a legal WHO");
    Check(gbnf.find("power ::= \"-\" | \"DMP\" | \"OVR\" | \"THR\"\n") != std::string::npos, "POWER lists the party's codes (an absent squadmate's not)");
    Check(gbnf.find("\"E1\"") != std::string::npos && gbnf.find("\"E3\"") != std::string::npos && gbnf.find("\"E2\"") == std::string::npos, "TARGET lists the live groups' ids only");
    Check(gbnf.find("\"PT1\" | \"PT2\"") != std::string::npos && gbnf.find("\"PT3\"") == std::string::npos, "TARGET lists the deictics said");
    Check(gbnf.find("root ::= (line (\" ; \" line){0,2} | \"none\") \"\\n\"?") != std::string::npos, "up to three clauses or none, a newline or the end of the turn after");
    // What the grammar allows must parse.
    for (char const* line : {"G|-|-|attack|E1", "ALL|-|-|follow|-", "W|-|THR|use|PT2", "G|SR|-|-|-", "-|-|-|move|LEFT"}) {
        Check(ParseLine(line).has_value(), line);
    }
    Check(mevr::voice::grammar::Prompt("SAY: hold the line").ends_with("\nACTION:"), "the prompt ends with the answer's cue");
}

}  // namespace

int RunVoiceTests() {
    gFailures = 0;
    TestWav();
    TestGesture();
    TestGrammar();
    return gFailures;
}

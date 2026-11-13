// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

// Voice orders (research/54 "Push-to-talk release makes a 300 ms budget realistic"): what the recognizer hands the mod.
// The host owns the microphone and the recognizer (plugin/Voice, over MEVR-Voice.dll: Moonshine v2's streaming models on
// ONNX Runtime, both MIT); the mod owns the push-to-talk gesture, the parser and the dispatch (game/Voice, game/SquadOrders).
// These structs cross HostServices (v31) and the DLL boundary (src/voice/VoiceApi.h), so they only grow at the end and
// carry their sizes.
namespace mevr::vr::voice {

inline constexpr std::size_t kTextChars = 512;
inline constexpr std::size_t kWordChars = 32;
inline constexpr std::size_t kMaxWords = 48;

struct Word {
    char text[kWordChars]{};
    float startS = 0.0f;  // from the start of the listen (the stream)
    float endS = 0.0f;
    float confidence = 0.0f;
};

// One line of a transcript as the recognizer updates it: a partial while the player speaks (updated, not complete), the
// final one at the end (complete). Times count from the listen's start, whose host clock (GetTickCount64) is
// `listenStartMs`, so a word's time maps onto the pose history.
struct Line {
    std::uint64_t id = 0;
    char text[kTextChars]{};
    float startS = 0.0f;
    float durationS = 0.0f;
    std::uint8_t complete = 0;
    std::uint8_t updated = 0;   // the text changed since the last line with this id
    std::uint8_t fromFile = 0;  // a WAV fed by the developer channel, not the microphone
    std::uint8_t endOfListen = 0;  // not a line: the listen's final transcript is complete (every line before it is final)
    std::uint32_t latencyMs = 0;  // the recognizer's last transcription time
    std::uint64_t listenStartMs = 0;
    std::uint32_t wordCount = 0;  // 0 without word timestamps
    Word words[kMaxWords]{};
};

enum class Status : std::uint8_t { Off = 0, Loading = 1, Ready = 2, Failed = 3 };

struct State {
    std::uint8_t status = 0;     // Status
    std::uint8_t listening = 0;  // a stream is open (the gesture, the key or a file)
    std::uint8_t keyHeld = 0;    // the keyboard push-to-talk is down
    std::uint8_t draining = 0;   // the listen ended; the final transcript is on its way
    float level = 0.0f;          // the microphone's RMS over the last chunk, 0..1
    std::uint32_t transcribeMs = 0;  // the last partial's cost on the worker
    std::uint32_t loadMs = 0;        // how long the models took to load
    std::uint64_t listenStartMs = 0;
    std::uint32_t listens = 0;
    std::uint32_t lines = 0;  // final lines so far
    char device[96]{};
    char model[32]{};
    char error[128]{};
    // Thespian mode's open listen (research/70 section 8 step 3; the DLL's interface v3).
    std::uint32_t silentMs = 0;  // since the last chunk of sound over the speech level (the microphone's, or a fed file's), or the listen's start
    std::uint8_t open = 0;       // the listen is an open one (MevrVoiceListen(2)): not ducked, ended only by the caller
    std::uint8_t pad[3]{};
};

}  // namespace mevr::vr::voice

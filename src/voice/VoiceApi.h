// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Voice.hpp"

#include <cstdint>

// The interface between the host and MEVR-Voice.dll (src/voice/Voice.cpp), which links Moonshine Voice's static
// libraries (MIT; external/moonshine-voice, fetched by tools/fetch-voice.ps1) and ONNX Runtime (MIT). The host loads it
// from ASI\voice at run time (plugin/Voice), so a game without the DLL or the models runs without voice. Every function is
// extern "C" and takes plain data; the DLL has its own C runtime and heap, so nothing allocated on one side is freed on the
// other (the text lives in the caller's structs).
#define MEVR_VOICE_API_VERSION 3  // 2: MevrVoiceDevices (the menu's microphone picker); 3: the open listen, State::silentMs

struct MevrVoiceConfig {
    int version = MEVR_VOICE_API_VERSION;
    char const* modelDir = nullptr;  // the folder with streaming_config.json, encoder.ort...
    int arch = 2;                    // MOONSHINE_MODEL_ARCH_*: 2 tiny streaming, 4 small, 5 medium
    char const* device = nullptr;    // a substring of the capture device's name; null or empty: the default device
    int tailMs = 200;                // audio kept after the listen ends (the last word's lag over the link)
    float duckDb = 12.0f;            // the game's own audio session is turned down this much while listening (0: never)
    int threads = 1;                 // 1: ONNX Runtime on one thread (Moonshine's MOONSHINE_ORT_SINGLE_THREAD); else its default,
                                     // every core and spinning, which starved the game's threads (research/54 "One thread")
    int wordTimestamps = 1;          // ask for a time per word
    int intervalMs = 150;            // how often the worker asks for a partial transcript while listening
    char const* keyterms = nullptr;  // comma-separated words to bias toward (squadmates' and powers' names)
    void (*log)(int level, char const* message) = nullptr;  // 0 info, 1 warn
};

extern "C" {
// Starts the microphone and loads the models on a worker. 0, or a negative error. Logs through config->log.
__declspec(dllexport) int MevrVoiceStartup(MevrVoiceConfig const* config);
__declspec(dllexport) void MevrVoiceShutdown();
// Push-to-talk: 1 opens a stream and feeds the microphone into it, 0 ends it after the tail; the final transcript follows.
// 2 is the open listen (Thespian mode, research/70): the same stream, the game's audio not ducked; it lasts until 0 or 1.
// While one is open a fed file (MevrVoiceFeedFile) is heard in the microphone's place and doesn't end the listen.
__declspec(dllexport) int MevrVoiceListen(int on);
// The next line update (partial or final), oldest first. 1 if one was written.
__declspec(dllexport) int MevrVoiceTake(mevr::vr::voice::Line* out, std::uint32_t size);
__declspec(dllexport) void MevrVoiceState(mevr::vr::voice::State* out, std::uint32_t size);
// Developer channel: a 16 kHz (or any rate) WAV spoken as one listen, through the same path as the microphone.
__declspec(dllexport) int MevrVoiceFeedFile(wchar_t const* path);
__declspec(dllexport) int MevrVoiceSetKeyterms(char const* commaSeparated);
__declspec(dllexport) int MevrVoiceApiVersion();
// The active capture endpoints' friendly names (UTF-8), each ended by a newline, into `names` (a name that doesn't fit is left out);
// `*defaultIndex` the default communications capture device's place in the list (-1: none). Works before MevrVoiceStartup (its
// own COM apartment, on the caller's thread: call it off the render thread). The number of names written, or negative.
__declspec(dllexport) int MevrVoiceDevices(char* names, std::uint32_t size, int* defaultIndex);
}

using MevrVoiceStartupFn = decltype(&MevrVoiceStartup);
using MevrVoiceShutdownFn = decltype(&MevrVoiceShutdown);
using MevrVoiceListenFn = decltype(&MevrVoiceListen);
using MevrVoiceTakeFn = decltype(&MevrVoiceTake);
using MevrVoiceStateFn = decltype(&MevrVoiceState);
using MevrVoiceFeedFileFn = decltype(&MevrVoiceFeedFile);
using MevrVoiceSetKeytermsFn = decltype(&MevrVoiceSetKeyterms);
using MevrVoiceApiVersionFn = decltype(&MevrVoiceApiVersion);
using MevrVoiceDevicesFn = decltype(&MevrVoiceDevices);

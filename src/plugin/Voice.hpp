// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Voice.hpp"

#include <string>

// The host's side of the voice orders (research/54): MEVR-Voice.dll (src/voice) loaded from ASI\voice at attach on a
// worker, the models with it; the push-to-talk from the mod's radio gesture (HostServices v31 setVoiceListen) or the
// keyboard ([Voice] key, held); the lines handed to the mod (takeVoiceLine); the key terms the mod sets from the roster;
// and the developer channel %LOCALAPPDATA%\MEVR\voice.request ("wav <path>": a WAV spoken as one listen; "listen <s>";
// "keyterms <a,b>"; "stop"), read like the test loop's requests. `[Voice]` in ASI\LE1MEVR.ini: enabled, model
// (tiny|small|medium), modelDir, dll, device, key, tailMs, duckDb, intervalMs, wordTimestamps, keyterms. Without the DLL
// or the models the game runs without voice (one log line). Log `voice:`; snapshot "voice".
namespace mevr::host::voice {

void Init();       // at attach (the settings file known): starts the loading
void OnPresent();  // every Present: the key, the requests, the listen state
void Shutdown();   // before the host unloads: stops the DLL's threads

// HostServices v31.
void SetListen(int on);
bool TakeLine(vr::voice::Line* out);
void GetState(vr::voice::State* out);
void SetKeyterms(char const* terms);
void Command(char const* line);

std::string StatusJson();

}  // namespace mevr::host::voice

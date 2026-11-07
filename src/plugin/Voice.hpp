// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Voice.hpp"

#include <string>
#include <vector>

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

// The menu's Voice page (research/54 "The menu's Voice page"; plugin/MainMenuSettings). Render thread.
struct MenuView {
    bool enabled = true;     // [Voice] as the recognizer last read it
    std::string model;       // tiny | small | medium
    std::string device;      // [Voice] device: a part of the name, empty for the default communications device
    int key = 0;             // [Voice] key (a virtual key, 0 none)
    bool restarting = false; // a start or restart is queued or running
    vr::voice::State state;  // as GetState
    bool listed = false;     // the capture devices were listed (needs MEVR-Voice.dll, not the models)
    std::vector<std::string> devices;  // their friendly names (UTF-8)
    int defaultDevice = -1;  // the default communications device's place in `devices`
    std::string listNote;    // why there's no list
};
MenuView Menu();
void ListDevices();                  // lists the capture devices on the voice worker (the page opened)
void Restart(std::string const& why);  // re-reads [Voice] and restarts the recognizer on the worker (a new microphone, model, on/off)
void SetKey(int vk);                 // the push-to-talk key from now on (the menu writes [Voice] key itself)
float Level();                       // the microphone's RMS over its last 20 ms (0..1), 0 while the recognizer isn't running

std::string StatusJson();

}  // namespace mevr::host::voice

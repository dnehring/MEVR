// SPDX-License-Identifier: MIT
// Offline tests for the main menu's Voice page (research/54 "The menu's Voice page"): which microphone [Voice] device picks, the
// level bar's scale and fall, the recognizer size's chip, the status line and the hold-to-talk key (core/vr/MenuSettings).

#include "core/vr/MenuSettings.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/VoiceMenuTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace ms = mevr::vr::menu_settings;

bool Close(float a, float b, float tolerance = 1e-3f) {
    return std::abs(a - b) <= tolerance;
}

void TestMicIndex() {
    std::vector<std::string> const names = {"Headset Microphone (PlayStation VR2)", "Microphone (Realtek(R) Audio)",
        "Headset Microphone (Oculus Virtual Audio Device)"};
    CHECK(ms::MicIndex(names, "") == -1);                       // the default communications device
    CHECK(ms::MicIndex(names, names[1]) == 1);                  // the whole name, as the menu writes it
    CHECK(ms::MicIndex(names, "oculus") == 2);                  // a part of it, any case, as the DLL matches
    CHECK(ms::MicIndex(names, "Headset Microphone") == 0);      // the first that holds it, as the DLL picks
    CHECK(ms::MicIndex(names, "Index") == -2);                  // set, not connected
    CHECK(ms::MicIndex({}, "Realtek") == -2);
    CHECK(ms::Find("mic") != nullptr && ms::Find("mic7") != nullptr && ms::Find("mic8") == nullptr);  // kMicSlots rows
}

void TestMeter() {
    CHECK(ms::VoiceMeterPercent(0.0f) == 0.0f);
    CHECK(ms::VoiceMeterPercent(-1.0f) == 0.0f);
    CHECK(ms::VoiceMeterPercent(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(Close(ms::VoiceMeterPercent(0.001f), 0.0f));    // -60 dBFS
    CHECK(Close(ms::VoiceMeterPercent(0.01f), 100.0f / 3.0f, 0.01f));  // -40 dBFS: the "hearing you" mark
    CHECK(Close(ms::VoiceMeterPercent(0.1f), 200.0f / 3.0f, 0.01f));   // -20 dBFS
    CHECK(ms::VoiceMeterPercent(1.0f) == 100.0f && ms::VoiceMeterPercent(4.0f) == 100.0f);
    CHECK(ms::VoiceMeterPercent(0.0101f) >= ms::kVoiceHeardPercent && ms::VoiceMeterPercent(0.009f) < ms::kVoiceHeardPercent);
    // Up at once, down at 90 %/s.
    CHECK(ms::VoiceMeterStep(10.0f, 70.0f, 0.033f) == 70.0f);
    CHECK(Close(ms::VoiceMeterStep(70.0f, 0.0f, 0.1f), 61.0f));
    CHECK(ms::VoiceMeterStep(5.0f, 0.0f, 1.0f) == 0.0f);         // never below the target
    CHECK(ms::VoiceMeterStep(50.0f, 20.0f, -1.0f) == 50.0f);     // a clock that went back holds the bar
    CHECK(ms::VoiceMeterStep(std::numeric_limits<float>::quiet_NaN(), 30.0f, 0.1f) == 30.0f);
}

void TestChoicesAndText() {
    CHECK(ms::VoiceModelChoice("tiny") == "tiny" && ms::VoiceModelChoice(" Small ") == "small" && ms::VoiceModelChoice("MEDIUM") == "medium");
    CHECK(ms::VoiceModelChoice("") == "tiny" && ms::VoiceModelChoice("large") == "tiny");  // plugin/Voice's fallback
    ms::VoiceStatusInput in;
    in.status = 2;
    in.device = "Headset Microphone (PlayStation VR2)";
    in.model = "tiny streaming";
    std::string const ready = ms::VoiceStatusText(in);
    CHECK(ready.starts_with("Ready") && ready.find("PlayStation VR2") != std::string::npos && ready.find("tiny streaming") != std::string::npos);
    in.listening = true;
    CHECK(ms::VoiceStatusText(in).starts_with("Listening"));
    in.restarting = true;
    CHECK(ms::VoiceStatusText(in).starts_with("Restarting"));  // the restart wins over the old state
    in = {};
    in.enabled = false;
    CHECK(ms::VoiceStatusText(in) == "Voice orders are off.");
    in = {};
    in.error = "no ...\\voice\\MEVR-Voice.dll (tools/deploy-asi.ps1 -Voice puts it there)";
    CHECK(ms::VoiceStatusText(in).starts_with("Not installed"));
    in.error = "no tiny models in ...";
    CHECK(ms::VoiceStatusText(in).starts_with("Not installed"));
    in.status = 3;
    in.error = "the models didn't load";
    CHECK(ms::VoiceStatusText(in).starts_with("The recognizer failed: the models didn't load"));
    in = {};
    in.status = 1;
    CHECK(ms::VoiceStatusText(in).starts_with("Loading"));
    in.status = 2;
    CHECK(ms::VoiceStatusText(in).find("no microphone opened") != std::string::npos);
}

void TestKey() {
    CHECK(ms::VoiceKeyFromPress('V', 0xDC) == 'V');
    CHECK(ms::VoiceKeyFromPress(0xA0, 0xDC) == 0xA0);  // left Shift
    CHECK(ms::VoiceKeyFromPress(0x10, 0xDC) == 0);     // the generic Shift: its left or right comes next
    CHECK(ms::VoiceKeyFromPress(0xDC, 0xDC) == 0);     // the menu's own key
    CHECK(ms::VoiceKeyFromPress(0x01, 0xDC) == 0 && ms::VoiceKeyFromPress(0x1B, 0xDC) == 0);  // the mouse, Escape
    CHECK(ms::VoiceKeyFromPress(0x23, 0xDC) == 0 && ms::VoiceKeyFromPress(0x24, 0xDC) == 0);  // End, Home: the test loop's
    CHECK(ms::VoiceKeyFromPress(0, 0xDC) == 0 && ms::VoiceKeyFromPress(300, 0xDC) == 0);
}

}  // namespace

int RunVoiceMenuTests() {
    TestMicIndex();
    TestMeter();
    TestChoicesAndText();
    TestKey();
    return gFailures;
}

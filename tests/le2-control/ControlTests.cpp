// SPDX-License-Identifier: MIT
#include "core/qa/ControlProtocol.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

using namespace mevr::qa::control;
void Check(bool ok, char const* why) { if (!ok) throw std::runtime_error(why); }
int main(int argc, char** argv) {
    try {
        // Cross-language conformance: decode and canonically re-encode a Python
        // frame, or reject it. No game or filesystem mutations beyond stdout.
        if (argc == 2) {
#if defined(_WIN32)
            _setmode(_fileno(stdout), _O_BINARY);
#endif
            std::ifstream in(argv[1], std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(in)), {});
            auto f = Decode(bytes);
            if (!f) return 2;
            std::cout << *Encode(*f);
            return 0;
        }
        Identity identity{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
            1234, 134127912301234567ULL, std::string(64, 'a'), std::string(64, 'b'), std::string(64, 'c')};
        Frame request{"request", identity, "cccccccc-cccc-4ccc-8ccc-cccccccccccc", 1, 20000, "ping", "pending", ""};
        auto encoded = Encode(request); Check(encoded.has_value(), "encode");
        auto decoded = Decode(*encoded); Check(decoded && decoded->identity == identity, "round trip exact start identity");
        Gate gate(identity);
        Check(!gate.Claim(request, 10000), "valid ping");
        Check(gate.Claim(request, 10000).has_value(), "duplicate rejected");
        request.sequence = 2;
        Check(gate.Claim(request, 10000).has_value(), "same id new sequence rejected");
        request.id = "dddddddd-dddd-4ddd-8ddd-dddddddddddd";
        request.identity.start++;
        Check(gate.Claim(request, 10000).has_value(), "PID reuse rejected");
        request.identity = identity; request.identity.lease[0] = 'c';
        Check(gate.Claim(request, 10000).has_value(), "previous lease rejected");
        request.identity = identity; request.identity.host[0] = 'd';
        Check(gate.Claim(request, 10000).has_value(), "wrong host rejected");
        request.identity = identity;
        Check(gate.Claim(request, 20000).has_value(), "expired rejected");
        Check(gate.Claim(request, 0).has_value() == false, "allowed bounded future");
        request.sequence = 3; request.id = "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee";
        request.deadline = 40001;
        Check(gate.Claim(request, 10000).has_value(), "excessive future rejected");
        request.deadline = 20000; request.verb = "console"; request.payload = "stat fps";
        Check(gate.Claim(request, 10000).has_value(), "console opt in required");
        Gate native(identity, true);
        Check(!native.Claim(request, 10000), "explicit console opt in");
        Check(native.Claim(request, 10000).has_value(), "failed/uncertain dispatch cannot be retried");
        request.sequence = 4; request.id = "ffffffff-ffff-4fff-8fff-ffffffffffff";
        for (auto command : {"streamlevelin BioP_Nor", "StReAmLeVeLiN BioP_Nor", "exec | STREAMLEVELIN BioP_Nor"}) {
            request.payload = command;
            Check(native.Claim(request, 10000).has_value(), "forbidden native streamlevelin rejected");
            Check(native.LastSequence() == 3, "pre-dispatch rejection does not claim request");
        }
        request.payload = "stat fps";
        Check(!native.Claim(request, 10000), "allowed command remains admissible after rejection");
        request.payload = "stat fps\nquit"; Check(!Encode(request), "batch command rejected");
        request.payload = std::string(4097, 'x'); Check(!Encode(request), "command size bounded");
        for (std::size_t n = 0; n < encoded->size(); ++n)
            Check(!Decode(std::string_view(*encoded).substr(0, n)), "partial file rejected");
        Check(!Decode(*encoded + "x"), "trailing payload rejected");
        auto wrongGame = *encoded; wrongGame.replace(wrongGame.find("game=LE2"), 8, "game=LE1");
        Check(!Decode(wrongGame), "wrong game rejected");
        auto duplicate = *encoded; duplicate.insert(duplicate.find("run="), "kind=request\n");
        Check(!Decode(duplicate), "duplicate key rejected");
        auto leadingZero = *encoded; leadingZero.insert(leadingZero.find("pid=") + 4, "0");
        Check(!Decode(leadingZero), "noncanonical number rejected");
        Frame result = *decoded; result.kind = "result"; result.status = "completed"; result.payload = "ready\nobserved";
        Check(Decode(*Encode(result)).has_value(), "multiline result");
        Check(gate.Claim(result, 10000).has_value(), "result cannot execute");
        Frame probe = *decoded; probe.verb = "probe-fov";
        Check(Decode(*Encode(probe))->verb == "probe-fov", "probe wire roundtrip");
        Gate noProbe(identity);
        Check(noProbe.Claim(probe, 10000) == "native FOV probe is disabled", "probe default disabled");
        Check(noProbe.LastSequence() == 0, "disabled probe does not claim");
        Gate consoleOnly(identity, true);
        Check(consoleOnly.Claim(probe, 10000).has_value(), "console permission does not grant probe");
        Gate probeOnly(identity, false, true);
        probe.payload = "stat fps";
        Check(!Encode(probe), "probe payload cannot encode");
        Check(probeOnly.Claim(probe, 10000).has_value(), "probe payload rejected at admission");
        Check(probeOnly.LastSequence() == 0, "invalid payload does not claim");
        probe.payload.clear();
        Check(!probeOnly.Claim(probe, 10000), "explicit independent probe opt in");
        Check(probeOnly.Claim(probe, 10000).has_value(), "uncertain probe cannot retry");
        Frame console = probe;
        console.id = "dddddddd-dddd-4ddd-8ddd-dddddddddddd";
        console.sequence = 2; console.verb = "console"; console.payload = "stat fps";
        Check(probeOnly.Claim(console, 10000).has_value(), "probe permission does not grant console");
        Check(probeOnly.LastSequence() == 1, "console rejection preserves claimed probe sequence");
        for (auto verb : {"advance-splash", "open-load-menu"}) {
            Frame menu = *decoded; menu.verb = verb;
            Check(Decode(*Encode(menu))->verb == verb, "menu action wire roundtrip");
            Gate disabledMenu(identity, true, true);
            Check(disabledMenu.Claim(menu, 10000) == "native menu actions are disabled", "other native opt-ins do not grant menu actions");
            Check(disabledMenu.LastSequence() == 0, "disabled menu action does not claim");
            Gate allowedMenu(identity, false, false, true);
            menu.payload = "arbitrary target";
            Check(!Encode(menu) && allowedMenu.Claim(menu, 10000).has_value(), "menu action cannot carry arbitrary targets");
            menu.payload.clear();
            Check(!allowedMenu.Claim(menu, 10000), "explicit menu opt in");
            Check(allowedMenu.Claim(menu, 10000).has_value(), "claimed menu action cannot retry");
            Check(allowedMenu.Claim(console, 10000).has_value(), "menu permission does not grant console");
        }
        Frame menuObservation = *decoded; menuObservation.verb = "observe-save-menu";
        Gate readOnly(identity);
        Check(Decode(*Encode(menuObservation))->verb == menuObservation.verb &&
              !readOnly.Claim(menuObservation, 10000), "menu observation needs no action opt in");
        menuObservation.payload = "x";
        Check(!Encode(menuObservation), "menu observation accepts no payload");
        std::cout << "Control identity, replay, expiry, opt-in, and wire corruption checks passed\n";
        return 0;
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}

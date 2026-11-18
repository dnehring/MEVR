// SPDX-License-Identifier: MIT
#include "Handoff.hpp"
#include <cstdio>
#include <cstdlib>
using namespace mevr::launcher;
void Check(bool value) { if (!value) { std::puts("Launcher handoff check failed"); std::exit(1); } }
int main() {
    Handoff h;
    Check(h.Step({0, false, false, true, false}) == Phase::Waiting); // stale focus cannot finish a launch
    Check(h.Step({1, true, false, true, false}) == Phase::Waiting); // a process alone is insufficient
    Check(h.Step({2, true, true, false, false}) == Phase::Waiting);
    Check(h.Step({2.1, true, false, false, false}) == Phase::Waiting); // interrupted frame streak
    Check(h.Step({3, true, true, false, false}) == Phase::Waiting);
    Check(h.Step({3.3, true, true, false, false}) == Phase::Released); // release input BEFORE awaiting focus
    Check(h.Step({4, true, true, false, false}) == Phase::Released);
    Check(h.Step({5, true, true, true, true}) == Phase::Released); // dashboard still owns interaction
    Check(h.Step({6, true, false, true, false}) == Phase::Released); // another renderer cannot finish
    Check(h.Step({7, true, true, true, false}) == Phase::Complete);
    Handoff neverStarted;
    Check(neverStarted.Step({120}) == Phase::Failed);
    Handoff exited;
    Check(exited.Step({1, true}) == Phase::Waiting);
    Check(exited.Step({2}) == Phase::Waiting);
    Check(exited.Step({22}) == Phase::Failed);
    Handoff restarted;
    restarted.Step({1, true});
    Check(restarted.Step({2}) == Phase::Waiting);
    Check(restarted.Step({4, true, true}) == Phase::Waiting);
    Check(restarted.Step({4.3, true, true}) == Phase::Released);
    Check(restarted.Step({5, true, true, true}) == Phase::Complete);
    Handoff unfocused;
    unfocused.Step({1, true, true});
    unfocused.Step({2, true, true});
    Check(unfocused.Step({120, true, true, false}) == Phase::Failed);
    std::puts("Launcher handoff checks passed");
}

// SPDX-License-Identifier: MIT
#include "plugin/ProcessEventWitness.hpp"
#include <iostream>
#include <stdexcept>

namespace d = mevr::host::process_event::diagnostic;
namespace {
unsigned checks{};
void Check(bool result, char const* message) { ++checks; if (!result) throw std::runtime_error(message); }
d::TargetInspection Target() { return d::InspectBounds(0x100000, 0x1000, 0x10000, 0x102000, 0x101000, true, true, 0x2000, 0x2100); }
d::ControllerWatch Identity() { return {0x200000, 0x300000, 0x102000, 0x400000, 0x500000, 42, 123}; }
}
int main() {
    try {
        auto target = Target();
        Check(target.inMainText && target.boundsUsable && target.exactFunctionStart && target.rva == 0x2000, "exact bounded target");
        Check(!d::InspectBounds(0x100000, 0x1000, 0x10000, 0x200000, 0, true, true, 0x2000, 0x2100).inMainText, "outside text rejected");
        Check(!d::InspectBounds(0x100000, 0x1000, 0x10000, 0x102001, 0, true, true, 0x2000, 0x2100).boundsUsable, "interior target rejected");
        Check(!d::InspectBounds(0x100000, 0x1000, 0x10000, 0x102000, 0, false, true, 0x2000, 0x2100).boundsUsable, "unknown bounds rejected");
        Check(!d::InspectBounds(0x100000, 0x1000, 0x20000, 0x102000, 0, true, true, 0x2000, 0x12001).boundsUsable, "oversized function rejected");
        Check(!d::InspectBounds(0x100000, 0x1000, 0x10000, 0x102000, 0, true, true, 0x2000, 0x12000).boundsUsable, "out-of-text bounds rejected");
        Check(d::WithinInspection(target, target.functionEnd - 5, 5), "last whole jump fits");
        Check(!d::WithinInspection(target, target.functionEnd - 5, 14), "instruction cannot cross function end");
        Check(!d::WithinInspection(target, target.functionBegin - 5, 5), "no hotpatch read outside function");
        Check(d::WithinInspection(target, target.baseTarget, 1) && !d::WithinInspection(target, target.baseTarget, 5), "known base single byte only");
        Check(!d::WithinInspection(target, 0x600000, 14), "unknown relay not inspected");
        d::NaturalCallWitness witness;
        auto identity = Identity();
        auto first = witness.Configure(identity, target, true, 100);
        Check(first.status == d::WatchStatus::Watching && first.epoch == 1 && first.expiresMs == 10100, "bounded initial arm");
        auto wrongObject = witness.Enter(identity.object + 1, identity.gameThread, 0x102020, 0x700000, 101);
        auto wrongThread = witness.Enter(identity.object, identity.gameThread + 1, 0x102020, 0x700000, 101);
        Check(!wrongObject.epoch && !wrongThread.epoch && witness.Sample(101).entries == 0, "object and thread filter");
        auto unrelated = witness.Enter(identity.object, identity.gameThread, 0x600000, 0x700000, 102);
        witness.ReturnedOriginal(unrelated);
        auto matching = witness.Enter(identity.object, identity.gameThread, 0x102020, 0x700000, 103);
        auto observed = witness.Sample(104);
        Check(observed.entries == 2 && observed.originalReturns == 1 && observed.matchingEntries == 1 && observed.matchingOriginalReturns == 0,
            "entry does not imply normal original return");
        witness.ReturnedOriginal(matching);
        observed = witness.Configure(identity, target, true, 5000);
        Check(observed.epoch == first.epoch && observed.expiresMs == first.expiresMs && observed.matchingOriginalReturns == 1,
            "same-scope observation never renews window");
        Check(observed.lastMatchingCaller == 0x102020 && observed.lastMatchingFunction == 0x700000, "correlated caller and function recorded");
        auto beforeExpiry = witness.Enter(identity.object, identity.gameThread, 0x102030, 0x700000, 10099);
        Check(!witness.Enter(identity.object, identity.gameThread, 0x102030, 0x700000, 10100).epoch, "entry at deadline refused");
        witness.ReturnedOriginal(beforeExpiry);
        observed = witness.Configure(identity, target, true, 11000);
        Check(observed.status == d::WatchStatus::Expired && observed.matchingOriginalReturns == 2, "late return of admitted entry retained without rearm");
        ++identity.index;
        observed = witness.Configure(identity, target, true, 12000);
        Check(observed.epoch == first.epoch + 1 && observed.entries == 0 && observed.status == d::WatchStatus::Watching, "index change resets epoch");
        witness.ReturnedOriginal(matching);
        Check(witness.Sample(12001).originalReturns == 0, "old epoch return cannot contaminate new scope");
        auto epoch = observed.epoch;
        identity.classAddress += 8;
        Check(witness.Configure(identity, target, true, 12002).epoch == ++epoch, "class change resets epoch");
        identity.world += 8;
        Check(witness.Configure(identity, target, true, 12003).epoch == ++epoch, "world change resets epoch");
        identity.localPlayer += 8;
        Check(witness.Configure(identity, target, true, 12004).epoch == ++epoch, "local player change resets epoch");
        identity.target += 0x10; target.target += 0x10; target.functionBegin += 0x10;
        Check(witness.Configure(identity, target, true, 12005).epoch == ++epoch, "target change resets epoch");
        identity.object += 8;
        Check(witness.Configure(identity, target, true, 12005).epoch == ++epoch, "object address change resets epoch");
        ++identity.gameThread;
        Check(witness.Configure(identity, target, true, 12005).epoch == ++epoch, "thread change resets epoch");
        target.functionEnd += 8;
        Check(witness.Configure(identity, target, true, 12005).epoch == ++epoch, "target bounds change resets epoch");
        target.baseTarget += 8;
        Check(witness.Configure(identity, target, true, 12005).epoch == ++epoch, "base target change resets epoch");
        auto denied = witness.Configure(identity, target, false, 12006);
        Check(denied.status == d::WatchStatus::Unavailable && denied.epoch == ++epoch, "lost owned chain disarms");
        Check(!witness.Enter(identity.object, identity.gameThread, target.functionBegin + 8, 0, 12007).epoch, "disarmed hook ignores entries");
        target.sameAsBase = true;
        Check(witness.Configure(identity, target, true, 12008).status == d::WatchStatus::Unavailable, "base itself needs no override witness");
        target.sameAsBase = false;
        witness.Configure(identity, target, true, 12009);
        auto active = witness.Enter(identity.object, identity.gameThread, target.functionBegin + 8, 0, 12010);
        auto cleared = witness.Configure({}, {}, true, 12011);
        witness.ReturnedOriginal(active);
        Check(cleared.status == d::WatchStatus::Unavailable && witness.Sample(12012).originalReturns == 0, "invalid controller clears old scope and ticket");
        std::uint64_t count = UINT64_MAX - 1;
        d::SaturatingIncrement(count); d::SaturatingIncrement(count);
        Check(count == UINT64_MAX, "counts saturate instead of wrapping");
        std::cout << checks << " target boundary and passive witness checks passed\n";
        return 0;
    } catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}

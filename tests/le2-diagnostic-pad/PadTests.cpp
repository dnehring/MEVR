// SPDX-License-Identifier: MIT
#include "core/qa/DiagnosticPad.hpp"
#include <atomic>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace mevr::qa::diagnostic_pad;
namespace {
unsigned checks{};
void Check(bool ok, char const* reason) { ++checks; if (!ok) throw std::runtime_error(reason); }
Pad const tap{0x1000};
Physical const disconnected{PhysicalState::Disconnected};
Physical const neutral{PhysicalState::Connected,{},10};
void Arm(Source& s, std::uint64_t run = 1) {
    Check(s.BeginRun(run) == Admission::Accepted,"open fresh nonzero run");
    Check(s.Publish(run,1,tap,80,100) == Admission::Accepted,"publish bounded tap");
}
}
int main() try {
    {
        Source s;
        Check(!s.Poll(0,disconnected,0).replace,"disabled source preserves disconnected result");
        Check(s.BeginRun(0)==Admission::InvalidRun,"zero run refused"); Arm(s);
        auto view=s.Snapshot();
        Check(view.phase==Phase::Published && view.pulsePolls==0 && view.neutralPolls==0,"publication is not poll consumption");
        Check(s.BeginRun(2)==Admission::RunAlreadyOpen,"new run cannot replace active run");
        Check(!s.Poll(1,disconnected,9999).replace && s.Snapshot().phase==Phase::Published,"other pad slots do not advance clock or consume");
        auto first=s.Poll(0,neutral,101), again=s.Poll(0,neutral,102);
        Check(first.replace && first.pad==tap && first.packet!=neutral.packet,"first consumed tap has own packet");
        Check(again.replace && again.packet==first.packet,"same effective pad preserves packet identity across polls");
        view=s.Snapshot(); Check(view.phase==Phase::Consumed && view.pulsePolls==2 && view.firstPulseMs==101 && view.lastPulseMs==102,"poll count is not frame count");
        Check(s.Publish(1,2,tap,80,102)==Admission::Busy,"one outstanding request, no queue or replacement");
        Check(s.Publish(1,1,tap,80,102)==Admission::StaleSequence,"duplicate sequence refused while active");
        auto release=s.Poll(0,neutral,181);
        Check(release.replace && release.pad==Pad{} && release.packet!=first.packet && release.packet!=neutral.packet,"exact deadline produces distinct neutral packet");
        view=s.Snapshot(); Check(view.phase==Phase::NeutralEmitted && view.end==End::Expired && view.neutralPolls==1,"expiry reports observed neutral poll");
        Check(!s.Poll(0,neutral,181).replace,"finished source restores original passthrough");
        Check(s.Publish(1,1,tap,80,181)==Admission::StaleSequence,"terminal request cannot replay");
        Check(s.Publish(1,2,tap,80,181)==Admission::Accepted,"next sequence after emitted release accepted");
        auto next=s.Poll(0,neutral,182); Check(next.replace && next.packet!=release.packet,"second press differs from release packet");
    }
    {
        Source s; Arm(s);
        auto r=s.Poll(0,disconnected,5100);
        Check(r.replace && r.pad==Pad{} && s.Snapshot().pulsePolls==0,"first poll at activation deadline never starts input");
        Check(s.Snapshot().end==End::StartTimedOut && s.Snapshot().expiresMs==0,"start timeout remains distinct from active hold expiry");
    }
    {
        Source s; Arm(s); s.Poll(0,disconnected,101);
        Check(!s.CancelRun(7,102),"wrong run cannot cancel another run");
        Check(s.CancelRun(1,102),"owned cancellation closes run");
        Check(s.Snapshot().phase==Phase::DrainPending && !s.Snapshot().runOpen,"cancellation retains pending release");
        Check(s.BeginRun(2)==Admission::Busy,"new run cannot skip pending neutral drain");
        Check(s.Publish(1,2,tap,80,103)==Admission::InvalidRun,"cancelled run cannot publish");
        Check(s.Poll(0,disconnected,104).pad==Pad{} && s.Snapshot().neutralPolls==1,"cancelled run drains even disconnected physical pad");
        Check(s.Snapshot().end==End::RunCancelled,"cancellation reason preserved");
        Check(s.BeginRun(1)==Admission::InvalidRun,"same closed run cannot reopen");
        Check(s.BeginRun(2)==Admission::Accepted,"new run allowed after release");
    }
    {
        // Every nonneutral component, including tiny stick drift and Guide, is
        // a conflict; no dead-zone policy silently eats physical input.
        std::vector<Pad> physicals{tap,Pad{0x400},Pad{0,1},Pad{0,0,1},Pad{0,0,0,1},
            Pad{0,0,0,0,-1},Pad{0,0,0,0,0,1},Pad{0,0,0,0,0,0,-1}};
        for(auto p:physicals) {
            Source s; Arm(s); auto r=s.Poll(0,{PhysicalState::Connected,p,99},101);
            auto view=s.Snapshot();
            Check(!r.replace && view.phase==Phase::DrainPending && view.end==End::PhysicalConflict && view.pulsePolls==0,"physical input never masked or merged silently");
            Check(view.lastPhysical.pad==p && view.conflictingPolls==1,"exact physical conflict retained");
            Check(!s.Poll(0,{PhysicalState::Connected,p,99},102).replace && s.Snapshot().neutralPolls==0,"physical conflict cannot be claimed as neutral delivery");
            auto zero=s.Poll(0,neutral,103); Check(zero.replace && zero.pad==Pad{} && s.Snapshot().neutralPolls==1,"drain only on safely neutral physical poll");
        }
        for(auto error:{PhysicalState::Error,static_cast<PhysicalState>(99)}) {
            Source s; Arm(s); Check(!s.Poll(0,{error},101).replace && s.Snapshot().end==End::PhysicalError,"physical read error or unknown state fails closed");
        }
    }
    {
        Source s; Arm(s); auto pressed=s.Poll(0,disconnected,120);
        auto zero=s.Poll(0,disconnected,119);
        Check(pressed.pad==tap && zero.replace && zero.pad==Pad{} && s.Snapshot().end==End::ClockRegressed,"rollback cannot extend held input");
        Check(s.Publish(1,2,tap,80,119)==Admission::ClockRegressed,"rollback publication refused");
        Check(s.Publish(1,2,tap,80,121)==Admission::Accepted,"safe later clock may publish next sequence");
        Check(s.CancelRun(1,120) && s.Snapshot().end==End::ClockRegressed,"cancellation still closes run on clock rollback");
    }
    {
        Source s; Check(s.BeginRun(1)==Admission::Accepted,"validation run open");
        Check(s.Publish(2,1,tap,1,0)==Admission::InvalidRun,"foreign publication refused");
        Check(s.Publish(1,0,tap,1,0)==Admission::StaleSequence,"zero sequence refused");
        Check(s.Publish(1,1,{},1,0)==Admission::InvalidPulse,"neutral command is not a pulse");
        Check(s.Publish(1,1,Pad{0x400},1,0)==Admission::InvalidPulse,"reserved synthetic button refused");
        Check(s.Publish(1,1,tap,0,0)==Admission::InvalidPulse && s.Publish(1,1,tap,1001,0)==Admission::InvalidPulse,"zero or excessive duration refused");
        Check(s.Publish(1,1,tap,1,std::numeric_limits<std::uint64_t>::max())==Admission::InvalidPulse,"deadline overflow refused");
        Pad extremes{kButtons,255,255,-32768,32767,-32768,32767};
        Check(s.Publish(1,1,extremes,1000,0)==Admission::Accepted,"fixed-width axis/trigger extrema accepted without conversion overflow");
        auto p=s.Poll(0,{PhysicalState::Disconnected,{},std::numeric_limits<std::uint32_t>::max()},0);
        Check(p.pad==extremes && p.packet==0,"packet wrap keeps changed packet distinct");
        auto q=s.Poll(0,{PhysicalState::Connected,{},1},1000);
        Check(q.pad==Pad{} && q.packet!=p.packet && q.packet!=1,"release packet avoids physical packet collision after wrap");
    }
    {
        Source s; Arm(s); std::atomic<bool> bad{}, go{};
        std::vector<std::thread> threads;
        for(unsigned i=0;i<4;++i) threads.emplace_back([&]{ while(!go.load()) std::this_thread::yield();
            for(unsigned j=0;j<5000;++j) { auto p=s.Poll(0,disconnected,101); auto v=s.Snapshot();
                if((p.replace && p.pad!=tap && p.pad!=Pad{}) || v.run!=1 || v.sequence!=1 ||
                    v.activationDeadlineMs!=5100 || v.requestedDurationMs!=80 ||
                    v.expiresMs!=(v.pulsePolls ? 181u : 0u)) bad=true; }
        });
        threads.emplace_back([&]{ while(!go.load()) std::this_thread::yield(); s.CancelRun(1,101); });
        go=true; for(auto& t:threads) t.join();
        auto view=s.Snapshot(); Check(!bad && !view.runOpen && view.phase==Phase::NeutralEmitted && view.neutralPolls==1,"concurrent polls snapshots and cancellation stay coherent with one release");
        Check(!s.Poll(0,disconnected,102).replace,"no held input after concurrent cancellation drain");
    }
    {
        // Exact delays seen in native polling. These are synthetic reproductions
        // of scheduling, not evidence that the game acts on the output.
        struct Delayed { std::uint64_t published, first; std::uint32_t duration; };
        for(auto sample : {Delayed{197660078,197660390,150},Delayed{197712984,197714406,1000}}) {
            Source s; Check(s.BeginRun(1)==Admission::Accepted,"delayed run admitted");
            Check(s.Publish(1,1,tap,sample.duration,sample.published)==Admission::Accepted,"delayed publication admitted");
            auto before=s.Snapshot();
            Check(before.expiresMs==0 && before.requestedDurationMs==sample.duration &&
                before.activationDeadlineMs==sample.published+5000 && before.pulsePolls==0,"unstarted hold exposes separate fixed activation deadline");
            Check(!s.Poll(1,neutral,sample.published+99999).replace && s.Snapshot().expiresMs==0,"ineligible slot cannot start or timeout pulse");
            Check(s.Poll(0,neutral,sample.first).pad==tap,"delayed eligible poll starts full requested hold");
            auto active=s.Snapshot();
            Check(active.firstPulseMs==sample.first && active.expiresMs==sample.first+sample.duration && active.end==End::None,"active expiry is relative to first output poll");
            Check(s.Poll(0,neutral,sample.first+sample.duration-1).pad==tap,"hold lasts requested duration after delayed start");
            Check(s.Publish(1,2,tap,1,sample.first+sample.duration-1)==Admission::Busy,"active hold cannot queue or replace request");
            auto release=s.Poll(0,neutral,sample.first+sample.duration);
            Check(release.replace && release.pad==Pad{} && s.Snapshot().end==End::Expired,"active deadline emits neutral with expired reason");
            Check(!s.Poll(0,neutral,sample.first+sample.duration+1).replace && s.Snapshot().pulsePolls==2,"no automatic rearm after delayed hold");
        }
    }
    {
        Source s; Arm(s);
        Check(s.Poll(0,neutral,5099).pad==tap && s.Snapshot().expiresMs==5179,"last admissible start receives full hold");
        Check(s.Poll(0,neutral,5100).pad==tap,"activation deadline does not truncate an active hold");
        Check(s.Poll(0,neutral,5179).pad==Pad{} && s.Snapshot().end==End::Expired,"late-started hold still ends at active deadline");
        Source timeout; Arm(timeout);
        Check(timeout.Publish(1,2,tap,80,5101)==Admission::Busy,"unpolled timeout cannot be replaced or queued");
        Check(timeout.CancelRun(1,5101) && timeout.BeginRun(2)==Admission::Busy,"closing expired wait cannot bypass neutral drain");
        Check(timeout.Poll(0,neutral,5102).pad==Pad{} && timeout.Snapshot().expiresMs==0 &&
            timeout.Snapshot().end==End::RunCancelled,"cancellation before first output never starts hold");
        Source late; Arm(late);
        Check(late.Poll(0,neutral,9000).pad==Pad{} && late.Snapshot().end==End::StartTimedOut,"poll beyond activation deadline times out explicitly");
        Check(!late.Poll(0,neutral,9001).replace && late.Snapshot().pulsePolls==0,"timeout cannot rearm on a later poll");
    }
    {
        Source conflict; Arm(conflict);
        Check(!conflict.Poll(0,{PhysicalState::Connected,tap},500).replace && conflict.Snapshot().expiresMs==0,"prestart physical conflict does not start hold");
        Check(conflict.Poll(0,neutral,501).pad==Pad{} && conflict.Snapshot().end==End::PhysicalConflict,"cleared conflict drains without starting pulse");
        Source rollback; Arm(rollback);
        Check(rollback.Poll(0,neutral,99).pad==Pad{} && rollback.Snapshot().expiresMs==0 &&
            rollback.Snapshot().end==End::ClockRegressed,"prestart rollback cannot start hold");
    }
    {
        constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
        Source s; Check(s.BeginRun(1)==Admission::Accepted,"overflow boundary run admitted");
        Check(s.Publish(1,1,tap,1000,maximum-5999)==Admission::InvalidPulse,"activation plus latest hold overflow refused");
        Check(s.Publish(1,1,tap,1000,maximum-6000)==Admission::Accepted,"full activation plus hold headroom accepted");
        Check(s.Snapshot().activationDeadlineMs==maximum-1000 && s.Snapshot().expiresMs==0,"activation deadline does not overflow");
        Check(s.Poll(0,neutral,maximum-1001).pad==tap && s.Snapshot().expiresMs==maximum-1,"latest possible start preserves nonoverflowing active deadline");
        Check(s.Poll(0,neutral,maximum-1).pad==Pad{} && s.Snapshot().end==End::Expired,"near-limit hold drains normally");
    }
    std::cout << checks << " diagnostic pad checks passed (synthetic; no hooks or native input)\n";
    return 0;
} catch(std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }

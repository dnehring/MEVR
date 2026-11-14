// SPDX-License-Identifier: MIT
#include "game/GamepadBridge.hpp"
#include "core/Hook.hpp"
#include "core/Log.hpp"
#include <Windows.h>
#include <Xinput.h>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
namespace bridge = mevr::game::gamepad_bridge;
namespace pad = mevr::qa::diagnostic_pad;
namespace {
unsigned checks{}, installCalls{}, uninstallCalls{}, lookups{};
std::atomic<unsigned> originals{};
std::atomic<std::uint64_t> now{100};
bool disconnectedLeavesUntouched{};
bool noDll{}, alias{}, noOrdinal{}, invalidExport{}, invalidMain{}, badCaller{}, forward{}, failSecond{}, callbackOnInstall{};
DWORD physicalResult = ERROR_SUCCESS;
XINPUT_STATE physical{19,{}};
std::mutex waitMutex;
std::condition_variable waitCondition;
bool blockOriginal{}, originalEntered{}, releaseOriginal{};
std::vector<void*> installedTargets;
void Check(bool value,char const* why) { ++checks; if(!value) throw std::runtime_error(why); }
DWORD WINAPI PhysicalB(DWORD,XINPUT_STATE* state) {
    ++originals;
    if(state && !(disconnectedLeavesUntouched && physicalResult==ERROR_DEVICE_NOT_CONNECTED)) *state=physical;
    return physicalResult;
}
DWORD WINAPI PhysicalA(DWORD index,XINPUT_STATE* state) {
    ++originals;
    {
        std::unique_lock lock(waitMutex);
        if(blockOriginal) { originalEntered=true; waitCondition.notify_all(); waitCondition.wait(lock,[]{return releaseOriginal;}); }
    }
    if(forward) return bridge::testing::Poll(1,index,state,0x1500);
    if(state && !(disconnectedLeavesUntouched && physicalResult==ERROR_DEVICE_NOT_CONNECTED)) *state=physical;
    return physicalResult;
}
bridge::DiagnosticExport Lookup(char const* dll,bool ordinal) {
    ++lookups; bridge::DiagnosticExport out;
    if(noDll || std::strcmp(dll,"xinput1_4.dll")!=0) return out;
    out.moduleLoaded=true;
    out.requested.base=0x8000; out.requested.size=0x1000; strcpy_s(out.requested.path,"synthetic/xinput1_4.dll");
    if(ordinal && noOrdinal) return out;
    out.found=true; out.valid=!invalidExport;
    out.target=reinterpret_cast<std::uintptr_t>(!ordinal || alias ? &PhysicalA : &PhysicalB);
    out.owner.base=out.target&~std::uintptr_t(0xffff); out.owner.size=0x20000;
    strcpy_s(out.owner.path,"synthetic/forwarded-owner.dll");
    return out;
}
bridge::DiagnosticMain Main() { return invalidMain ? bridge::DiagnosticMain{} : bridge::DiagnosticMain{0x1000,0x1100,0x2000}; }
bool Executable(std::uintptr_t address,std::uintptr_t owner) { return !badCaller && owner==0x1000 && address>=0x1100 && address<0x2000; }
std::uint64_t Clock() { return now.load(); }
void Reset() {
    installCalls=uninstallCalls=lookups=0; originals=0; now=100;
    noDll=alias=noOrdinal=invalidExport=invalidMain=badCaller=forward=failSecond=callbackOnInstall=false;
    disconnectedLeavesUntouched=false; physicalResult=ERROR_SUCCESS; physical={19,{}}; installedTargets.clear();
    blockOriginal=originalEntered=releaseOriginal=false;
    bridge::testing::Reset({Lookup,Main,Executable,Clock});
}
mevr::hook::Context context{nullptr,"SyntheticOnly"};
pad::Pad const tap{0x1000};
void Arm(std::uint64_t run=1) {
    Check(bridge::DiagnosticTryInstall(context,true),"all discovered existing bridge targets installed");
    Check(bridge::DiagnosticBeginRun(run)==pad::Admission::Accepted,"owned diagnostic run admitted");
    Check(bridge::DiagnosticPublish(run,1,tap,80)==pad::Admission::Accepted,"bounded tap admitted");
}
DWORD Poll(XINPUT_STATE& state,std::uintptr_t caller=0x1500) { return bridge::testing::Poll(0,0,&state,caller); }
}
namespace mevr::log { void Write(std::string_view,std::string_view) {} }
namespace mevr::hook {
bool Install(Context&,char const*,void* target,void* detour,void** original) {
    ++installCalls;
    if(failSecond && installCalls==2) { *original=nullptr; return false; }
    *original=target; installedTargets.push_back(target);
    if(callbackOnInstall) { XINPUT_STATE state{}; reinterpret_cast<DWORD(WINAPI*)(DWORD,XINPUT_STATE*)>(detour)(0,&state); }
    return true;
}
bool Uninstall(Context&,char const*) { ++uninstallCalls; return true; }
}
namespace {
void DelayedActivation() {
    XINPUT_STATE state{};
    bridge::DiagnosticObservation s;
    // Native scheduler delays reproduced through the actual Bridge path. Both
    // delays exceed the requested hold but fit the bounded activation window.
    struct Delayed { std::uint64_t published, first; std::uint32_t duration; };
    for(auto sample : {Delayed{197660078,197660390,150},Delayed{197712984,197714406,1000}}) {
        Reset(); now=sample.published;
        Check(bridge::DiagnosticTryInstall(context,true) && bridge::DiagnosticBeginRun(1)==pad::Admission::Accepted,"delayed bridge run admitted");
        Check(bridge::DiagnosticPublish(1,1,tap,sample.duration)==pad::Admission::Accepted,"delayed bridge pulse published");
        s=bridge::DiagnosticSnapshot();
        Check(s.source.expiresMs==0 && s.source.activationDeadlineMs==sample.published+5000 && s.source.requestedDurationMs==sample.duration,"bridge exposes unstarted timing");
        now=sample.first; Poll(state,0x3000);
        Check(bridge::DiagnosticSnapshot().source.pulsePolls==0 && bridge::DiagnosticSnapshot().source.expiresMs==0,"foreign caller cannot start activation");
        Check(Poll(state)==ERROR_SUCCESS && state.Gamepad.wButtons==tap.buttons,"eligible delayed callback writes active pad");
        Check(bridge::DiagnosticSnapshot().source.expiresMs==sample.first+sample.duration,"bridge starts hold at eligible callback");
        now=sample.first+sample.duration-1; Poll(state);
        Check(state.Gamepad.wButtons==tap.buttons,"bridge preserves requested active hold");
        now=sample.first+sample.duration; Poll(state); s=bridge::DiagnosticSnapshot();
        Check(state.Gamepad.wButtons==0 && s.source.end==pad::End::Expired && s.neutralWrites==1,"bridge active deadline writes neutral");
        now=sample.first+sample.duration+250; Poll(state); Poll(state);
        Check(std::memcmp(&state,&physical,sizeof state)==0 && bridge::DiagnosticSnapshot().source.pulsePolls==2,"bridge does not rearm after hold");
    }
    Reset(); Arm(); now=5100;
    Check(bridge::DiagnosticPublish(1,2,tap,80)==pad::Admission::Busy,"expired unpolled activation cannot queue replacement");
    Poll(state); s=bridge::DiagnosticSnapshot();
    Check(state.Gamepad.wButtons==0 && s.source.end==pad::End::StartTimedOut && s.source.expiresMs==0 &&
        s.source.pulsePolls==0 && s.neutralWrites==1,"bridge first poll timeout releases without active write");
    now=5350; Poll(state); Poll(state);
    Check(std::memcmp(&state,&physical,sizeof state)==0 && bridge::DiagnosticSnapshot().source.pulsePolls==0,"bridge timeout remains terminal");

}
}
// Keep the large by-value telemetry temporaries in separate test stack frames,
// particularly in unoptimized MSVC builds; do not raise the process stack limit.
void OriginalBridgeChecks() {
    Reset();
    Check(!bridge::TryInstall(context) && installCalls==0,"ordinary install entry cannot silently activate diagnostic build");
    Check(!bridge::DiagnosticTryInstall(context,false) && lookups==0,"no lookup or hook before explicit opt-in");
    Check(bridge::DiagnosticBeginRun(1)==pad::Admission::InvalidRun,"no run without ready hooks");
    noDll=true; Check(!bridge::DiagnosticTryInstall(context,true) && !bridge::DiagnosticSnapshot().faulted,"no loaded DLL remains unavailable but retryable");
    noDll=false; callbackOnInstall=true; Arm();
    auto s=bridge::DiagnosticSnapshot();
    Check(installCalls==2 && s.available && s.hookCount==2 && s.recordCount==2,"readiness requires installed unique targets");
    Check(s.hooks[0].mapping.requested.base==0x8000 && s.hooks[0].mapping.owner.base!=0x8000 && s.hooks[0].original && s.hooks[0].detour,"forwarded mapped ownership and original/detour recorded");
    Check(bridge::DiagnosticTryInstall(context,true) && installCalls==2,"repeated install shares existing registry");
    XINPUT_STATE state{}; now=101;
    Check(Poll(state)==ERROR_SUCCESS && state.Gamepad.wButtons==XINPUT_GAMEPAD_A,"actual Bridge path writes source pad");
    auto packet=state.dwPacketNumber;
    Check(Poll(state)==ERROR_SUCCESS && state.dwPacketNumber==packet,"same synthetic state retains packet");
    Check(bridge::DiagnosticSnapshot().source.pulsePolls==2,"one output write per eligible nonrecursive poll");
    now=181; Poll(state);
    Check(state.Gamepad.wButtons==0 && state.dwPacketNumber!=packet && bridge::DiagnosticSnapshot().neutralWrites==1,"expiry emits neutral in actual output buffer");
    now=431; Poll(state);
    Check(Poll(state)==physicalResult && std::memcmp(&state,&physical,sizeof state)==0,"after drain exact physical state passthrough");
    Check(!bridge::Uninstall(context) && uninstallCalls==0 && bridge::DiagnosticSnapshot().disabled,"diagnostic hooks never uninstalled");
    Check(!bridge::DiagnosticTryInstall(context,true),"disabled adapter cannot re-enable");

    Reset(); alias=true; Arm();
    Check(installCalls==1 && bridge::DiagnosticSnapshot().hooks[1].status==bridge::DiagnosticHookStatus::Alias,"ordinal alias deduplicates final target");
    Reset(); noOrdinal=true; Arm();
    Check(installCalls==1 && bridge::DiagnosticSnapshot().hooks[1].status==bridge::DiagnosticHookStatus::MissingOptionalExport,"optional ordinal absence is not a failed required hook");
    Reset(); failSecond=true;
    Check(!bridge::DiagnosticTryInstall(context,true) && installCalls==2 && bridge::DiagnosticSnapshot().faulted,"partial install fails entire capability");
    Check(bridge::DiagnosticBeginRun(1)==pad::Admission::InvalidRun,"partial success cannot admit input");
    Reset(); invalidExport=true;
    Check(!bridge::DiagnosticTryInstall(context,true) && installCalls==0 && bridge::DiagnosticSnapshot().faulted,"invalid mapped target never hooked");
    Reset(); invalidMain=true;
    Check(!bridge::DiagnosticTryInstall(context,true) && installCalls==0,"unavailable main image prevents hooks");

    Reset(); Arm();
    for(auto caller:{std::uintptr_t(0),std::uintptr_t(0x1000),std::uintptr_t(0x2000),std::uintptr_t(0x3000)}) {
        state={}; Check(Poll(state,caller)==physicalResult && std::memcmp(&state,&physical,sizeof state)==0,"unknown or out-of-text caller passthrough");
    }
    badCaller=true; Poll(state); badCaller=false;
    Check(bridge::DiagnosticSnapshot().source.pulsePolls==0 && bridge::DiagnosticSnapshot().foreignCallerPassthrough==5,"unmapped executable caller cannot consume");
    Check(bridge::testing::Poll(0,0,nullptr,0x1500)==physicalResult,"null buffer passed to original without synthetic write");
    state={}; bridge::testing::Poll(0,1,&state,0x1500);
    Check(std::memcmp(&state,&physical,sizeof state)==0 && bridge::DiagnosticSnapshot().source.pulsePolls==0,"other pad index preserves original state");
    physical.Gamepad.sThumbLX=1;
    Poll(state); s=bridge::DiagnosticSnapshot();
    Check(std::memcmp(&state,&physical,sizeof state)==0 && s.source.end==pad::End::PhysicalConflict && s.source.pulsePolls==0,"physical drift cancels rather than masks input");
    physical.Gamepad={}; now=102; Poll(state);
    Check(bridge::DiagnosticSnapshot().neutralWrites==1,"conflicted source drains on safe physical poll");
    Reset(); Arm(); physicalResult=ERROR_ACCESS_DENIED; physical.Gamepad.wButtons=0x2000;
    Check(Poll(state)==ERROR_ACCESS_DENIED && std::memcmp(&state,&physical,sizeof state)==0 && bridge::DiagnosticSnapshot().source.end==pad::End::PhysicalError,"other original errors preserved exactly");
    Reset(); Arm(); physicalResult=ERROR_DEVICE_NOT_CONNECTED;
    Check(Poll(state)==ERROR_SUCCESS && state.Gamepad.wButtons==0x1000,"synthetic pad may stand in for disconnected pad");
    bridge::DiagnosticDisable(); now=102;
    Check(Poll(state)==ERROR_SUCCESS && state.Gamepad.wButtons==0 && bridge::DiagnosticSnapshot().neutralWrites==1,"disable retains success-valued neutral release before disconnect");
    Check(Poll(state)==ERROR_SUCCESS && state.Gamepad.wButtons==0,"disable continues neutral window");
    now=352; Poll(state);
    Check(Poll(state)==ERROR_DEVICE_NOT_CONNECTED,"physical disconnection restored after release window");

    Reset(); Arm(); forward=true;
    auto before=originals.load(); Poll(state); s=bridge::DiagnosticSnapshot();
    Check(originals==before+2 && s.recursivePassthrough==1 && s.source.pulsePolls==1 && s.outputWrites==1,"forwarded GetState reentry does not deadlock or double consume");
    forward=false;

    // Actual original call is stalled across cancellation, neutral drain and a
    // new run. The old callback must not write the new run's pulse on return.
    Reset(); Arm();
    { std::lock_guard lock(waitMutex); blockOriginal=true; }
    XINPUT_STATE delayed{};
    std::thread worker([&]{Poll(delayed);});
    { std::unique_lock lock(waitMutex); waitCondition.wait(lock,[]{return originalEntered;}); }
    Check(bridge::DiagnosticSnapshot().callbacksInFlight==1,"in-flight original visible without blocking snapshot");
    Check(bridge::DiagnosticCancelRun(1),"cancellation can close run while original is blocked");
    Check(bridge::DiagnosticBeginRun(2)==pad::Admission::Busy,"new run waits for neutral output");
    now=102; bridge::testing::Poll(1,0,&state,0x1500); // separate physical export, no stall
    Check(bridge::DiagnosticSnapshot().neutralWrites==1,"fresh callback commits neutral while old original waits");
    Check(bridge::DiagnosticBeginRun(2)==pad::Admission::Busy,"new run waits full neutral window");
    now=352; bridge::testing::Poll(1,0,&state,0x1500);
    Check(bridge::DiagnosticBeginRun(2)==pad::Admission::Accepted && bridge::DiagnosticPublish(2,1,pad::Pad{0x2000},80)==pad::Admission::Accepted,"new run only after committed neutral");
    { std::lock_guard lock(waitMutex); releaseOriginal=true; waitCondition.notify_all(); }
    worker.join();
    s=bridge::DiagnosticSnapshot();
    Check(std::memcmp(&delayed,&physical,sizeof delayed)==0 && s.source.pulsePolls==0 && s.staleCallbackPassthrough==1,"old callback cannot consume or apply new run input");
    bridge::testing::Poll(1,0,&state,0x1500);
    Check(state.Gamepad.wButtons==0x2000 && bridge::DiagnosticSnapshot().source.pulsePolls==1,"fresh callback consumes only the new request");

}
void SeparateConsumerRelease() {
    Reset(); Arm(); disconnectedLeavesUntouched=true; physicalResult=ERROR_DEVICE_NOT_CONNECTED;
    XINPUT_STATE gameplay{}, discovery{};
    now=101;
    Check(bridge::testing::Poll(0,0,&gameplay,0x1500)==ERROR_SUCCESS && gameplay.Gamepad.wButtons==tap.buttons,
          "gameplay consumer receives held state in its own buffer");
    now=181;
    Check(bridge::testing::Poll(1,0,&discovery,0x1600)==ERROR_SUCCESS && discovery.Gamepad.wButtons==0,
          "other export and caller cannot exhaust release with first neutral");
    auto const releasePacket=discovery.dwPacketNumber;
    now=182;
    Check(bridge::testing::Poll(0,0,&gameplay,0x1500)==ERROR_SUCCESS && gameplay.Gamepad.wButtons==0 &&
          gameplay.dwPacketNumber==releasePacket,"later gameplay consumer receives neutral despite disconnected original leaving held buffer untouched");
    Check(bridge::DiagnosticPublish(1,2,tap,80)==pad::Admission::Busy,"new action cannot replace multi-consumer release");
    auto const count=bridge::DiagnosticSnapshot().neutralWrites;
    now=10000; bridge::testing::Poll(0,1,&gameplay,0x1500); Poll(gameplay,0x3000);
    Check(bridge::DiagnosticSnapshot().neutralWrites==count && bridge::DiagnosticSnapshot().source.phase==pad::Phase::DrainPending,
          "foreign/index polls cannot finish neutral window");
    now=430; forward=true;
    Check(Poll(gameplay)==ERROR_SUCCESS && gameplay.dwPacketNumber==releasePacket,"recursive export forwards one stable neutral sample");
    forward=false;
    Check(bridge::DiagnosticSnapshot().neutralWrites==count+1,"recursive nested callback does not double drain");
    now=431;
    Check(Poll(gameplay)==ERROR_SUCCESS && bridge::DiagnosticSnapshot().source.phase==pad::Phase::NeutralEmitted,
          "safe final neutral completes window");
    now=432;
    Check(Poll(gameplay)==ERROR_DEVICE_NOT_CONNECTED && gameplay.Gamepad.wButtons==0,"terminal passthrough preserves already released consumer buffer");
}
int main() try {
    OriginalBridgeChecks();
    DelayedActivation();
    SeparateConsumerRelease();
    std::cout << checks << " existing-bridge diagnostic checks passed; no native hooks installed\n";
    return 0;
} catch(std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }

// SPDX-License-Identifier: MIT
#include "game/CallTrace.hpp"

#include "core/Log.hpp"
#include "engine/CallFunction.hpp"
#include "engine/ProcessEvent.hpp"
#include "ue3/Reflect.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <mutex>
#include <unordered_map>

namespace mevr::game::call_trace {
namespace {

constexpr int kMaxLines = 600;

struct Named {
    std::string name;  // "SFXGame.SFXPawn_Player.DrawPrimaryWeapon"
    bool matches = false;
};
std::vector<std::string> gExcluded;  // lower case: a path holding one of these is never logged (`-word`)

std::mutex gMutex;
bool gOn = false;
std::vector<std::string> gWords;  // lower case
std::unordered_map<void const*, Named> gNames;
std::uint64_t gDeadlineMs = 0;
int gLines = 0;
int gSuppressed = 0;
int gScriptSeen = 0;
int gEventsSeen = 0;

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// A live object in GObjects (IsAlive's read is fault-guarded), and for `function` one of class Function: what a hook's
// argument or a frame's Node must be before anything reads its name.
bool Listed(void const* object, bool function) {
    auto const* const o = static_cast<ue3::Object const*>(object);
    if (!ue3::runtime::IsAlive(o)) {
        return false;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    return !function || r.ClassOf(o) == r.FunctionClass();
}

// `function` is a hook's own argument (a Function); a caller is a frame's Node (a Function, State or Class), checked here.
Named const& Resolve(ue3::Function* function, bool caller = false) {
    auto const found = gNames.find(function);
    if (found != gNames.end()) {
        return found->second;
    }
    Named named;
    if (!Listed(function, !caller)) {
        named.name = std::format("<not a listed {}: 0x{:X}>", caller ? "object" : "function", reinterpret_cast<std::uintptr_t>(function));
        named.matches = false;
        return gNames.emplace(function, std::move(named)).first->second;
    }
    named.name = ue3::runtime::FullName(reinterpret_cast<ue3::Object const*>(function));
    if (named.name.rfind("Function ", 0) == 0) {
        named.name.erase(0, 9);
    }
    std::string const lower = Lower(named.name);
    auto const holds = [&](std::string const& word) { return lower.find(word) != std::string::npos; };
    named.matches = (gWords.empty() || std::any_of(gWords.begin(), gWords.end(), holds)) &&
        std::none_of(gExcluded.begin(), gExcluded.end(), holds);
    return gNames.emplace(function, std::move(named)).first->second;
}

// Under gMutex.
void EndLocked(char const* why) {
    if (!gOn) {
        return;
    }
    gOn = false;
    engine::call_function::SetObserver(nullptr);
    engine::process_event::SetObserver(nullptr);
    log::Info("call trace: {}: {} lines, {} more matching calls not logged; {} script calls and {} ProcessEvents seen", why, gLines,
        gSuppressed, gScriptSeen, gEventsSeen);
}

void Note(char const* kind, ue3::Object* context, ue3::Function* function, ue3::Function* caller) {
    if (function == nullptr) {
        return;
    }
    std::lock_guard lock(gMutex);
    if (!gOn) {
        return;
    }
    if (GetTickCount64() > gDeadlineMs) {
        return EndLocked("done");
    }
    Named const& named = Resolve(function);
    if (!named.matches) {
        return;
    }
    if (gLines >= kMaxLines) {
        ++gSuppressed;
        return;
    }
    ++gLines;
    ue3::Reflection& r = ue3::runtime::Get();
    std::string const on = context == nullptr ? std::string("None")
        : Listed(context, false)              ? std::format("{} {}", r.NameText(r.ClassOf(context)), r.NameText(context))
                                              : std::string("<not a listed object>");
    std::string const from = caller != nullptr ? Resolve(caller, true).name : std::string("native code");
    log::Info("call trace: {} {} on {} <- {}", kind, named.name, on, from);
}

void OnScript(ue3::Object* context, ue3::Function* function, ue3::Function* caller) {
    {
        std::lock_guard lock(gMutex);
        ++gScriptSeen;
    }
    Note("script", context, function, caller);
}

void OnEvent(ue3::Object* context, ue3::Function* function) {
    {
        std::lock_guard lock(gMutex);
        ++gEventsSeen;
    }
    Note("event ", context, function, nullptr);
}

} // namespace

std::string Start(double seconds, std::vector<std::string> const& words) {
    if (!ue3::runtime::Ready()) {
        return "call trace: the reflection layer isn't ready";
    }
    if (!engine::call_function::Installed()) {
        return "call trace: the script call hook isn't in yet (it goes in when hand aim first comes on)";
    }
    seconds = std::clamp(seconds, 0.1, 120.0);
    std::lock_guard lock(gMutex);
    EndLocked("restarted");
    gWords.clear();
    gExcluded.clear();
    for (std::string const& word : words) {
        if (word.size() > 1 && word[0] == '-') {
            gExcluded.push_back(Lower(word.substr(1)));
        } else {
            gWords.push_back(Lower(word));
        }
    }
    gNames.clear();
    gLines = gSuppressed = gScriptSeen = gEventsSeen = 0;
    gDeadlineMs = GetTickCount64() + static_cast<std::uint64_t>(seconds * 1000.0);
    gOn = true;
    engine::call_function::SetObserver(&OnScript);
    engine::process_event::SetObserver(&OnEvent);
    std::string list;
    for (std::string const& word : gWords) {
        list += (list.empty() ? "" : ", ") + word;
    }
    for (std::string const& word : gExcluded) {
        list += (list.empty() ? "not " : ", not ") + word;
    }
    log::Info("call trace: on for {:.1f} s ({})", seconds, list.empty() ? std::string("every call") : "calls whose path holds " + list);
    return std::format("call trace on for {:.1f} s: the matching calls go to the log as `call trace:`", seconds);
}

std::string Stop() {
    std::lock_guard lock(gMutex);
    bool const was = gOn;
    EndLocked("stopped");
    return was ? "call trace stopped" : "call trace: not running";
}

void Shutdown() {
    std::lock_guard lock(gMutex);
    EndLocked("the mod unloads");
    gNames.clear();
}

} // namespace mevr::game::call_trace

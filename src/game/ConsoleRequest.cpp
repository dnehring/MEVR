// SPDX-License-Identifier: MIT
#include "game/ConsoleRequest.hpp"

#include "core/Log.hpp"
#include "engine/Legacy.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "game/Player.hpp"
#include "core/WriteJournal.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace mevr::game::console_request {
namespace {

constexpr float kPollSeconds = 0.5f;
constexpr char const* kConsoleCommand = "Engine.PlayerController.ConsoleCommand";

float gSincePoll = 0.0f;
ue3::Function* gConsoleCommand = nullptr;
bool gLookedUp = false;

std::filesystem::path Directory() {
    wchar_t buffer[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) == 0) {
        return {};
    }
    return std::filesystem::path(buffer) / L"MEVR";
}

#if defined(MEVR_GAME_LE1)

// Our reflection layer (research/34): the function, found once in the running game.
ue3::FunctionRef gConsoleCommandRef{kConsoleCommand};
bool gToldNoMemory = false;
bool gToldParameters = false;

ue3::Object* PlayerController() {
    return ue3::runtime::LocalPlayerController();
}

// Our own lines, for the measuring loop (research/35 attempt 42): "mevr loc" prints the player's pawn's Location and
// Rotation, "mevr setloc X Y Z" moves the pawn there (Engine.Actor.SetLocation through ProcessEvent, on the game thread,
// which this tick is). The console's own `getall`/`displayall` print nowhere we can read, and the game has no way to
// jump to a known place: this is how a heavy spot is reached again and again without a person walking there.
ue3::FunctionRef gSetLocationRef{"Engine.Actor.SetLocation"};

std::optional<std::string> RunOurs(ue3::Object* pc, std::string const& line) {
    if (line.rfind("mevr ", 0) != 0) {
        return std::nullopt;
    }
    std::istringstream words(line.substr(5));
    std::string verb;
    words >> verb;
    ue3::Object* const pawn = player::Pawn(pc);
    if (pawn == nullptr) {
        return std::string("(no pawn)");
    }
    char text[160]{};
    if (verb == "loc") {
        std::optional<ue3::Vector> const at = player::Location(pawn);
        std::optional<ue3::Rotator> const rot = player::Rotation(pawn);
        if (!at || !rot) {
            return std::string("(the pawn's Location or Rotation can't be read)");
        }
        std::snprintf(text, sizeof(text), "loc %.1f %.1f %.1f rot %d %d %d", at->x, at->y, at->z, rot->pitch, rot->yaw, rot->roll);
        return std::string(text);
    }
    if (verb == "setloc") {
        ue3::Vector to{};
        if (!(words >> to.x >> to.y >> to.z)) {
            return std::string("usage: mevr setloc X Y Z");
        }
        ue3::Function* const setLocation = gSetLocationRef.Get();
        if (setLocation == nullptr) {
            return std::string("(Engine.Actor.SetLocation not found)");
        }
        ue3::Params parms(ue3::runtime::Get(), setLocation);
        parms.Set("NewLocation", to);
        ue3::runtime::Call(pawn, setLocation, parms);  // the native through ProcessEvent (game thread: this tick)
        write_journal::Wrote("ConsoleRequest.SetLocation", pawn, 0);
        std::optional<ue3::Vector> const at = player::Location(pawn);
        std::snprintf(text, sizeof(text), "moved: now %.1f %.1f %.1f", at ? at->x : 0.0f, at ? at->y : 0.0f, at ? at->z : 0.0f);
        return std::string(text);
    }
    return std::string("unknown: mevr loc | mevr setloc X Y Z");
}

ue3::Function* FindConsoleCommand() {
    return gConsoleCommandRef.Get();
}

// One command; bWriteToLog false, so what it prints comes back as the return value instead of going to the log.
// Both strings are in the engine's memory: ours goes in, the engine's comes back, and the block (ours) frees both.
// Nullopt: it can't be run (no allocator, or the parameters aren't as expected; logged once).
std::optional<std::string> Run(ue3::Object* pc, ue3::Function* function, std::string const& command) {
    if (!ue3::memory::Ready()) {
        if (!gToldNoMemory) {
            gToldNoMemory = true;
            log::Warn("console: the engine's allocator wasn't found (see the 'reflect:' lines): commands aren't run");
        }
        return std::nullopt;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::memory::OwnedString text(ue3::memory::Widen(command));
    if (!text.Valid() || !parms.Set("Command", text.Get()) || !parms.SetBool("bWriteToLog", false) ||
        !parms.Return<ue3::FString>()) {
        if (!gToldParameters) {
            gToldParameters = true;
            log::Warn("console: ConsoleCommand's parameters aren't (Command, bWriteToLog, a string ReturnValue): commands aren't run");
        }
        return std::nullopt;
    }
    ue3::runtime::Call(pc, function, parms);  // the native directly, never a generated wrapper
    ue3::FString returned = parms.Return<ue3::FString>().value_or(ue3::FString{});
    std::string output = ue3::memory::Narrow(returned);
    ue3::memory::FreeString(returned);
    return output;
}

#else

// LE2/LE3: LExSDKv2 until stage 5 (engine/Legacy, research/34).
ue3::Object* PlayerController() {
    return engine::legacy::LocalPlayerController();
}

ue3::Function* FindConsoleCommand() {
    return engine::legacy::FindFunction(kConsoleCommand);
}

std::optional<std::string> Run(ue3::Object* pc, ue3::Function* function, std::string const& command) {
    return engine::legacy::ConsoleCommand(pc, function, command);
}

#endif

// Once, with a player there (so, on LE1, our reflection layer is up).
bool LookUp() {
    if (!gLookedUp) {
        gLookedUp = true;
        gConsoleCommand = FindConsoleCommand();
        if (gConsoleCommand == nullptr) {
            log::Warn("console: Engine.PlayerController.ConsoleCommand not found: console commands are ignored");
        }
    }
    return gConsoleCommand != nullptr;
}

} // namespace

bool RunCommand(std::string const& command, std::string* output) {
    ue3::Object* const pc = PlayerController();
    if (pc == nullptr || !LookUp()) {
        return false;
    }
    std::optional<std::string> const printed = Run(pc, gConsoleCommand, command);
    if (!printed) {
        return false;
    }
    if (output != nullptr) {
        *output = *printed;
    }
    return true;
}

void Tick(float deltaSeconds) {
    gSincePoll += deltaSeconds;
    if (gSincePoll < kPollSeconds) {
        return;
    }
    gSincePoll = 0.0f;
    std::filesystem::path const directory = Directory();
    std::error_code ec;
    if (directory.empty() || !std::filesystem::exists(directory / L"console.request", ec)) {
        return;
    }
    ue3::Object* const pc = PlayerController();
    if (pc == nullptr) {
        return;  // kept until there is a player
    }
    LookUp();
    std::stringstream text;
    {
        std::ifstream in(directory / L"console.request", std::ios::binary);
        text << in.rdbuf();
    }
    std::filesystem::remove(directory / L"console.request", ec);
    std::string result;
    std::string line;
    while (std::getline(text, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        size_t const start = line.find_first_not_of(" \t\xEF\xBB\xBF");  // a BOM from PowerShell
        if (start == std::string::npos) {
            continue;
        }
        line.erase(0, start);
        std::string output;
#if defined(MEVR_GAME_LE1)
        if (std::optional<std::string> ours = RunOurs(pc, line)) {
            output = *ours;
        } else
#endif
        {
            output = gConsoleCommand != nullptr
                ? Run(pc, gConsoleCommand, line).value_or("(ConsoleCommand can't be run: see the log)")
                : std::string("(ConsoleCommand not found)");
        }
        log::Info("console: {} -> {}", line, output.empty() ? std::string("(no output)") : output);
        result += "> " + line + "\n" + output + (output.empty() || output.back() == '\n' ? "" : "\n");
    }
    // Written whole, then renamed, so a reader never sees half of it.
    {
        std::ofstream out(directory / L"console.result.tmp", std::ios::binary | std::ios::trunc);
        out << result;
    }
    std::filesystem::rename(directory / L"console.result.tmp", directory / L"console.result", ec);
}

} // namespace mevr::game::console_request

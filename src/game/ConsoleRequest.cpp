// SPDX-License-Identifier: MIT
#include "game/ConsoleRequest.hpp"

#include "core/Log.hpp"
#include "engine/Objects.hpp"

#include <LESDK/Headers.hpp>

#include <filesystem>
#include <fstream>
#include <new>
#include <sstream>
#include <string>

namespace mevr::game::console_request {
namespace {

constexpr float kPollSeconds = 0.5f;

float gSincePoll = 0.0f;
UFunction* gConsoleCommand = nullptr;
bool gLookedUp = false;

std::filesystem::path Directory() {
    wchar_t buffer[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) == 0) {
        return {};
    }
    return std::filesystem::path(buffer) / L"MEVR";
}

std::wstring Widen(std::string const& utf8) {
    int const size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
    }
    return wide;
}

std::string Narrow(wchar_t const* chars) {
    int const size = WideCharToMultiByte(CP_UTF8, 0, chars, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string narrow(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, chars, -1, narrow.data(), size, nullptr, nullptr);
    return narrow;
}

// One command; bWriteToLog false, so what it prints comes back as the return value instead of going to the log.
std::string Run(APlayerController* pc, std::string const& command) {
    APlayerController_execConsoleCommand_Parms parms;  // zeroed
    new (&parms.Command) FString(Widen(command).c_str());  // the game's allocator
    parms.bWriteToLog = 0;
    pc->ProcessEvent(gConsoleCommand, &parms, nullptr);  // the native directly, never the SDK's wrapper
    std::string output = Narrow(parms.ReturnValue.Chars());
    parms.Command.~FString();  // union members: the parms' destructor leaves them alone
    parms.ReturnValue.~FString();
    return output;
}

} // namespace

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
    APlayerController* const pc = engine::LocalPlayerController();
    if (pc == nullptr) {
        return;  // kept until there is a player
    }
    if (!gLookedUp) {
        gLookedUp = true;
        gConsoleCommand = engine::FindFunction(L"ConsoleCommand", "Function Engine.PlayerController.ConsoleCommand");
        if (gConsoleCommand == nullptr) {
            log::Warn("console: Engine.PlayerController.ConsoleCommand not found: console.request is ignored");
        }
    }
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
        std::string const output = gConsoleCommand != nullptr ? Run(pc, line) : std::string("(ConsoleCommand not found)");
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

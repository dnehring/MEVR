// SPDX-License-Identifier: MIT
// A stand-in for Mass Effect Modder's command line (MassEffectModderNoGui.exe), for testing the setup's texture-pack flow
// without MEM or a game (tools/setup/test-network.py, research/33 section 10). Built at test time, never shipped or committed
// as a binary. It only does what the setup relies on, the way research/33 section 3 describes MEM:
//
//   every call            appends "call: <arg> | <arg> ..." and "cwd: <folder>" to fake-mem.log next to itself
//   --version             prints a version line
//   --unpack-archive --input <archive> --output <folder> [--ipc]
//                         "unpacks": writes <folder>\<stem>-1.mem and <folder>\Sub\<stem>-2.mem (text naming the archive)
//   --set-game-data-path --gameid 1 --path <root>
//                         the root must hold Game\ME1\Binaries\Win64\MassEffect1.exe; remembered in fake-mem.game
//   --install-mods --gameid 1 --input <list.mfl> [--verify] [--ipc]
//                         every line of the list must be an existing .mem file; prints [IPC] stages and progress, then
//                         "installs": appends MEM's marker to BioGame\CookedPCConsole\SFXTest.pcc and turns MotionBlur back
//                         on in GamerSettings.ini (MEM writes the game's graphics settings too)
//   MEVR_FAKE_MEM_FAIL=<command>  (unpack-archive, set-game-data-path, install-mods): that call prints [IPC]ERROR, exits 1

#include <Windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string Utf8(std::wstring const& w) {
    if (w.empty()) {
        return {};
    }
    int const n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

fs::path FromUtf8(std::string const& s) {
    if (s.empty()) {
        return {};
    }
    int const n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return fs::path(out);
}

fs::path Self() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path);
}

void Say(std::string const& line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
}

std::wstring Value(std::vector<std::wstring> const& args, wchar_t const* name) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) {
            return args[i + 1];
        }
    }
    return {};
}

bool Has(std::vector<std::wstring> const& args, wchar_t const* name) {
    for (std::wstring const& a : args) {
        if (a == name) {
            return true;
        }
    }
    return false;
}

bool Fails(char const* command) {
    char buffer[64]{};
    DWORD const n = GetEnvironmentVariableA("MEVR_FAKE_MEM_FAIL", buffer, sizeof buffer);
    if (n == 0 || n >= sizeof buffer || std::string(buffer) != command) {
        return false;
    }
    Say(std::string("[IPC]ERROR the stand-in was told to fail in ") + command);
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::vector<std::wstring> const args(argv + 1, argv + argc);
    fs::path const dir = Self().parent_path();
    {
        std::ofstream log(dir / L"fake-mem.log", std::ios::binary | std::ios::app);
        std::string line = "call:";
        for (std::size_t i = 0; i < args.size(); ++i) {
            line += (i == 0 ? " " : " | ") + Utf8(args[i]);
        }
        log << line << "\n" << "cwd: " << Utf8(fs::current_path().wstring()) << "\n";
    }
    std::error_code ec;
    if (Has(args, L"--version")) {
        Say("MassEffectModderNoGui stand-in (MEVR tests) v0");
        return 0;
    }
    if (Has(args, L"--unpack-archive")) {
        if (Fails("unpack-archive")) {
            return 1;
        }
        fs::path const input = Value(args, L"--input");
        fs::path const output = Value(args, L"--output");
        if (input.empty() || output.empty() || !fs::is_regular_file(input, ec)) {
            Say("[IPC]ERROR the archive isn't there");
            return 1;
        }
        Say("[IPC]STAGE_CONTEXT STAGE_UNPACK");
        std::wstring const stem = input.stem().wstring();
        for (fs::path const& file : {output / (stem + L"-1.mem"), output / L"Sub" / (stem + L"-2.mem")}) {
            fs::create_directories(file.parent_path(), ec);
            std::ofstream(file, std::ios::binary) << "stand-in .mem unpacked from " << Utf8(input.filename().wstring()) << "\n";
            Say("[IPC]PROCESSING_FILE " + Utf8(file.filename().wstring()));
        }
        Say("[IPC]TASK_PROGRESS 100");
        return 0;
    }
    if (Has(args, L"--set-game-data-path")) {
        if (Fails("set-game-data-path")) {
            return 1;
        }
        fs::path const root = Value(args, L"--path");
        if (Value(args, L"--gameid") != L"1" || !fs::is_regular_file(root / L"Game" / L"ME1" / L"Binaries" / L"Win64" / L"MassEffect1.exe", ec)) {
            Say("[IPC]ERROR not a Legendary Edition folder: " + Utf8(root.wstring()));
            return 1;
        }
        std::ofstream(dir / L"fake-mem.game", std::ios::binary) << Utf8(root.wstring());
        Say("Game path updated");
        return 0;
    }
    if (Has(args, L"--install-mods")) {
        if (Fails("install-mods")) {
            return 1;
        }
        std::ifstream gameFile(dir / L"fake-mem.game", std::ios::binary);
        std::string rootText((std::istreambuf_iterator<char>(gameFile)), std::istreambuf_iterator<char>());
        fs::path const root = FromUtf8(rootText);
        fs::path const list = Value(args, L"--input");
        std::ifstream in(list, std::ios::binary);
        if (Value(args, L"--gameid") != L"1" || rootText.empty() || !in) {
            Say("[IPC]ERROR no game path set, or no list");
            return 1;
        }
        std::vector<std::string> mems;
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            fs::path const mem = FromUtf8(line);
            if (!fs::is_regular_file(mem, ec) || _wcsicmp(mem.extension().c_str(), L".mem") != 0) {
                Say("[IPC]ERROR not a .mem file: " + line);
                return 1;
            }
            mems.push_back(line);
        }
        if (mems.empty()) {
            Say("[IPC]ERROR the list is empty");
            return 1;
        }
        for (char const* stage : {"STAGE_PRESCAN", "STAGE_SCAN", "STAGE_INSTALLTEXTURES"}) {
            Say(std::string("[IPC]STAGE_CONTEXT ") + stage);
            for (int percent = 0; percent <= 100; percent += 50) {
                Say("[IPC]TASK_PROGRESS " + std::to_string(percent));
            }
        }
        for (std::string const& mem : mems) {
            Say("[IPC]PROCESSING_FILE " + mem);
        }
        if (Has(args, L"--verify")) {
            Say("[IPC]STAGE_CONTEXT STAGE_VERIFYTEXTURES");
        }
        Say("[IPC]STAGE_CONTEXT STAGE_MARKERS");
        fs::path const marked = root / L"Game" / L"ME1" / L"BioGame" / L"CookedPCConsole" / L"SFXTest.pcc";
        fs::create_directories(marked.parent_path(), ec);
        std::ofstream(marked, std::ios::binary | std::ios::app) << "ThisIsMEMEndOfFileMarker";
        fs::path const gamer = root / L"Game" / L"ME1" / L"BioGame" / L"Config" / L"GamerSettings.ini";
        std::ifstream g(gamer, std::ios::binary);
        if (g) {
            std::stringstream text;
            text << g.rdbuf();
            g.close();
            std::string s = text.str();
            if (std::size_t const at = s.find("MotionBlur=False"); at != std::string::npos) {
                s.replace(at, 16, "MotionBlur=True");
                std::ofstream(gamer, std::ios::binary | std::ios::trunc) << s;
            }
        }
        Say("[IPC]STAGE_CONTEXT STAGE_DONE");
        return 0;
    }
    Say("[IPC]ERROR the stand-in doesn't know this command");
    return 1;
}

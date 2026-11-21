// SPDX-License-Identifier: MIT
// A stand-in for the rigged bodies' builder (tools/rig's `mevr-rig bodies`, MEVR Setup's payload\rig\mevr-rig.exe), for testing
// the setup's bodies step on a fake game without Legendary Explorer or the game's packages (install/Bodies, research/33
// section 18; tools/setup/test-fake-game.ps1, tests/InstallTests.cpp TestBodies). Built at test time, never shipped. It does
// what the setup relies on:
//
//   bodies --game LE1|LE2|LE3 --game-root <root> --out <folder> [--ref <package>]
//       the root must hold Game\ME<n>\Binaries\Win64; prints "progress <percent> <text>" lines and writes into <folder> the
//       packages MEVR_Bodies_<GAME>.pcc (and MEVR_Miranda.pcc for LE2 and LE3: 4 KB each, the same bytes every run), their
//       index MEVR_Bodies_<GAME>.json, colliders\HMF_ARM_NKDa_MDL_MEVR.json and bodies.json (`mevr-rig-bodies` version 1)
//   every call appends "call: <arg> | <arg> ..." to fake-rig.log next to itself
//   MEVR_FAKE_RIG_FAIL=1        prints progress, then exits 1 with nothing written
//   MEVR_FAKE_RIG_FAIL=nothing  writes a bodies.json without packages, exits 3 (nothing could be built)

#include <Windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
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

fs::path Self() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path);
}

void Say(std::string const& line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
}

std::wstring Arg(std::vector<std::wstring> const& args, wchar_t const* name) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) {
            return args[i + 1];
        }
    }
    return {};
}

void WriteText(fs::path const& file, std::string const& text) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream(file, std::ios::binary) << text;
}

// A "package": an Unreal package's tag, then text naming it, padded to 4 KB.
std::uint64_t WritePackage(fs::path const& file, std::string const& what) {
    std::string bytes = "\xC1\x83\x2A\x9E" + what;
    bytes.resize(4096, '\0');
    WriteText(file, bytes);
    return bytes.size();
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::vector<std::wstring> args(argv + 1, argv + argc);
    {
        std::string line = "call:";
        for (std::wstring const& a : args) {
            line += (line.size() > 5 ? " | " : " ") + Utf8(a);
        }
        std::ofstream(Self().parent_path() / L"fake-rig.log", std::ios::binary | std::ios::app) << line << "\n";
    }
    if (args.empty() || args[0] != L"bodies") {
        Say("fake mevr-rig: only `bodies` is here");
        return 2;
    }
    std::wstring const game = Arg(args, L"--game");
    fs::path const root = Arg(args, L"--game-root");
    fs::path const out = Arg(args, L"--out");
    if ((game != L"LE1" && game != L"LE2" && game != L"LE3") || root.empty() || out.empty()) {
        Say("mevr-rig bodies --game LE1|LE2|LE3 --game-root <folder> --out <folder>");
        return 2;
    }
    std::wstring const folder = L"ME" + game.substr(2);
    std::error_code ec;
    if (!fs::is_directory(root / L"Game" / folder / L"Binaries" / L"Win64", ec)) {
        Say("no Game\\" + Utf8(folder) + " in " + Utf8(root.wstring()));
        return 2;
    }
    std::string const key = Utf8(game);
    Say("progress 0 reading " + Utf8(game) + "'s plan");
    wchar_t fail[32]{};
    GetEnvironmentVariableW(L"MEVR_FAKE_RIG_FAIL", fail, 32);
    if (std::wstring(fail) == L"1") {
        Say("progress 40 body HMF_ARM_NKDa_MDL_MEVR");
        Say("bodies: FAILED on purpose (MEVR_FAKE_RIG_FAIL=1)");
        return 1;
    }
    fs::create_directories(out, ec);
    std::vector<std::pair<std::string, std::uint64_t>> packages;
    if (std::wstring(fail) != L"nothing") {
        packages.emplace_back("MEVR_Bodies_" + key + ".pcc", WritePackage(out / (L"MEVR_Bodies_" + game + L".pcc"), "fake rigged bodies for " + key));
        Say("progress 60 body HMF_ARM_NKDa_MDL_MEVR");
        WriteText(out / (L"MEVR_Bodies_" + game + L".json"),
            "{\n  \"format\": \"mevr-rig-index\",\n  \"version\": 1,\n  \"package\": \"MEVR_Bodies_" + key + "\",\n  \"meshes\": [\n" +
            "    { \"path\": \"BIOG_HMF_ARM_NKD_R.NKDa.HMF_ARM_NKDa_MDL\", \"name\": \"HMF_ARM_NKDa_MDL_MEVR\", \"breastMax\": 1.5 }\n  ]\n}\n");
        WriteText(out / L"colliders" / L"HMF_ARM_NKDa_MDL_MEVR.json", "{ \"format\": \"mevr-body-spheres\", \"version\": 1, \"spheres\": [] }\n");
        if (game != L"LE1") {
            packages.emplace_back("MEVR_Miranda.pcc", WritePackage(out / L"MEVR_Miranda.pcc", "fake Miranda for " + key));
            WriteText(out / L"MEVR_Miranda.json", "{ \"format\": \"mevr-rig-index\", \"version\": 1, \"package\": \"MEVR_Miranda\", \"meshes\": [] }\n");
        }
        Say("progress 90 the clubs' bodies");
    }
    std::string result = "{\n  \"format\": \"mevr-rig-bodies\",\n  \"version\": 1,\n  \"game\": \"" + key + "\",\n  \"packages\": [";
    for (std::size_t i = 0; i < packages.size(); ++i) {
        result += std::string(i == 0 ? "\n" : ",\n") + "    { \"file\": \"" + packages[i].first + "\", \"size\": " + std::to_string(packages[i].second) + " }";
    }
    result += std::string(packages.empty() ? "" : "\n  ") + "],\n  \"bodies\": " + std::to_string(packages.empty() ? 0 : 1) +
        ",\n  \"skipped\": 0,\n  \"failed\": 0,\n  \"seconds\": 0\n}\n";
    WriteText(out / L"bodies.json", result);
    Say("progress 100 done");
    Say("bodies " + key + ": " + std::to_string(packages.size()) + " packages");
    return packages.empty() ? 3 : 0;
}

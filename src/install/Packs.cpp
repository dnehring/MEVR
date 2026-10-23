#include "install/Packs.hpp"

#include "core/Json.hpp"
#include "install/Detect.hpp"
#include "install/Manifest.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <system_error>

namespace mevr::install::packs {
namespace {

namespace fs = std::filesystem;

std::string Lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

fs::path LocalMevr() {
    wchar_t buffer[MAX_PATH]{};
    GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    return fs::path(buffer) / L"MEVR";
}

// Runs `commandLine` hidden, calling `line` for each line of its output. The exit code, or -1.
int Run(std::wstring commandLine, fs::path const& workDir, std::function<void(std::string_view)> const& line, std::atomic<bool> const* cancel) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        return -1;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    BOOL const started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
        workDir.empty() ? nullptr : workDir.c_str(), &startup, &process);
    CloseHandle(writePipe);
    if (!started) {
        CloseHandle(readPipe);
        return -1;
    }
    std::string pending;
    char buffer[4096];
    for (;;) {
        if (cancel != nullptr && cancel->load()) {
            TerminateProcess(process.hProcess, 1);
        }
        DWORD read = 0;
        if (!::ReadFile(readPipe, buffer, sizeof buffer, &read, nullptr) || read == 0) {
            break;
        }
        pending.append(buffer, read);
        for (std::size_t nl; (nl = pending.find('\n')) != std::string::npos;) {
            std::string text = pending.substr(0, nl);
            if (!text.empty() && text.back() == '\r') {
                text.pop_back();
            }
            if (line) {
                line(text);
            }
            pending.erase(0, nl + 1);
        }
    }
    if (!pending.empty() && line) {
        line(pending);
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(readPipe);
    return static_cast<int>(code);
}

} // namespace

std::vector<Pack> const& Catalogue() {
    static std::vector<Pack> const kPacks = {
        {
            "alot-le1",
            "ALOT for ME1",
            "CreeperLava (ALOT team)",
            "A Lot Of Textures: the most used texture pack for the Legendary Edition. Sharper characters, armour, "
            "environments and UI across the whole game.",
            "about 176 MB",
            "Free on Nexus Mods. The author doesn't allow re-uploads, so it's downloaded from its own page.",
            "https://www.nexusmods.com/masseffectlegendaryedition/mods/363",
            "ALOT ME1",
            "8 GB of RAM; a GPU with 8 GB of video memory recommended",
            Method::PageThenMem,
            false,
        },
        {
            "isl-le1",
            "Improved Static Lighting for ME1",
            "CreeperLava (ALOT team)",
            "Rebuilt baked lighting: smooth shadows instead of the blocky ones, very visible in VR. Very large; "
            "install ALOT first.",
            "about 9.7 GB",
            "Free on Nexus Mods (the ALOT page). No re-uploads, so it's downloaded from its own page.",
            "https://www.nexusmods.com/masseffectlegendaryedition/mods/363",
            "Static Lighting",
            "12 GB of RAM; a GPU with 8 GB of video memory recommended",
            Method::PageThenMem,
            true,
        },
        {
            "leuitm-le1",
            "LEUITM (Updated Improved Textures)",
            "Catachrism",
            "Higher-detail faces and characters for LE1. You pick one variant per character on its page.",
            "about 1.2 GB",
            "Free on Nexus Mods. No re-uploads; downloaded from its own page.",
            "https://www.nexusmods.com/masseffectlegendaryedition/mods/1825",
            "LEUITM",
            "",
            Method::PageThenMem,
            false,
        },
    };
    return kPacks;
}

Pack const* Find(std::string_view id) {
    for (Pack const& p : Catalogue()) {
        if (id == p.id) {
            return &p;
        }
    }
    return nullptr;
}

fs::path MemExe() {
    return LocalMevr() / L"tools" / L"MassEffectModderNoGui.exe";
}

std::optional<Asset> MemAssetFrom(std::string_view releaseJson) {
    auto const release = json::Parse(releaseJson);
    if (!release) {
        return std::nullopt;
    }
    for (json::Value const& asset : release->Get("assets").Items()) {
        std::string const name = asset.String("name");
        std::string const lower = Lower(name);
        bool const noGui = lower.rfind("masseffectmoddernogui", 0) == 0;
        bool const sevenZip = lower.size() > 3 && lower.compare(lower.size() - 3, 3, ".7z") == 0;
        bool const otherOs = lower.find("linux") != std::string::npos || lower.find("macos") != std::string::npos ||
            lower.find("mac") != std::string::npos;
        if (noGui && sevenZip && !otherOs) {
            Asset a;
            a.name = name;
            a.url = asset.String("browser_download_url");
            a.size = static_cast<std::uint64_t>(asset.Number("size", 0));
            a.tag = release->String("tag_name");
            if (a.url.rfind("https://github.com/", 0) != 0) {
                return std::nullopt;  // only GitHub's own download links
            }
            return a;
        }
    }
    return std::nullopt;
}

IpcLine ParseIpc(std::string_view line) {
    constexpr std::string_view kPrefix = "[IPC]";
    std::size_t const at = line.find(kPrefix);
    if (at == std::string_view::npos) {
        return {};
    }
    line.remove_prefix(at + kPrefix.size());
    std::size_t const space = line.find(' ');
    IpcLine out;
    out.command = std::string(line.substr(0, space));
    if (space != std::string_view::npos) {
        out.argument = std::string(line.substr(space + 1));
    }
    return out;
}

std::string StageText(std::string_view stage) {
    if (stage == "STAGE_PRESCAN") {
        return "Checking the game";
    }
    if (stage == "STAGE_SCAN") {
        return "Scanning the game's textures (first time only)";
    }
    if (stage == "STAGE_INSTALLTEXTURES") {
        return "Installing textures";
    }
    if (stage == "STAGE_VERIFYTEXTURES") {
        return "Verifying textures";
    }
    if (stage == "STAGE_MARKERS") {
        return "Marking the game's files";
    }
    if (stage == "STAGE_DONE") {
        return "Done";
    }
    return std::string(stage);
}

std::string MflFor(std::vector<fs::path> const& memFiles) {
    std::string out;
    for (fs::path const& f : memFiles) {
        out += detect::Utf8(f.wstring());
        out += "\r\n";
    }
    return out;
}

bool TexturesMarked(fs::path const& gameRoot) {
    fs::path const file = gameRoot / L"Game" / L"ME1" / L"BioGame" / L"CookedPCConsole" / L"SFXTest.pcc";
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) {
        return false;
    }
    std::streamoff const size = in.tellg();
    std::streamoff const tail = std::min<std::streamoff>(size, 256);
    std::string bytes(static_cast<std::size_t>(tail), '\0');
    in.seekg(size - tail);
    in.read(bytes.data(), tail);
    return bytes.find("ThisIsMEMEndOfFileMarker") != std::string::npos;
}

std::vector<fs::path> FindDownloads(Pack const& pack) {
    std::vector<fs::path> found;
    PWSTR downloads = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloads))) {
        return found;
    }
    fs::path const dir(downloads);
    CoTaskMemFree(downloads);
    // "ALOT ME1" matches "ALOT_for_ME1_1.3.7z", "ALOT for ME1 1.3-363-...": every word of the hint, in any order.
    std::vector<std::string> words;
    std::string const hint = Lower(pack.fileHint);
    for (std::size_t start = 0; start < hint.size();) {
        std::size_t const end = hint.find(' ', start);
        words.push_back(hint.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    std::error_code ec;
    for (auto const& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        std::string const name = Lower(detect::Utf8(entry.path().filename().wstring()));
        std::string const ext = Lower(detect::Utf8(entry.path().extension().wstring()));
        if (ext != ".7z" && ext != ".zip" && ext != ".rar" && ext != ".mem") {
            continue;
        }
        bool all = true;
        for (std::string const& w : words) {
            all = all && name.find(w) != std::string::npos;
        }
        if (all) {
            found.push_back(entry.path());
        }
    }
    return found;
}

std::string UnpackMem(fs::path const& archive) {
    fs::path const dir = MemExe().parent_path();
    std::error_code ec;
    fs::create_directories(dir, ec);
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    std::wstring const command = L"\"" + (fs::path(system) / L"tar.exe").wstring() + L"\" -xf \"" + archive.wstring() + L"\" -C \"" + dir.wstring() + L"\"";
    int const code = Run(command, dir, nullptr, nullptr);
    if (code != 0) {
        return "couldn't unpack " + detect::Utf8(archive.filename().wstring()) + " (tar exit code " + std::to_string(code) + ")";
    }
    // The archive may hold the exe in a subfolder.
    if (!fs::exists(MemExe(), ec)) {
        for (auto const& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (_wcsicmp(entry.path().filename().c_str(), L"MassEffectModderNoGui.exe") == 0) {
                fs::copy_file(entry.path(), MemExe(), fs::copy_options::overwrite_existing, ec);
                break;
            }
        }
    }
    return fs::exists(MemExe(), ec) ? std::string() : std::string("MassEffectModderNoGui.exe isn't in the archive");
}

std::string InstallWithMem(Pack const& pack, fs::path const& download, fs::path const& gameRoot, std::function<void(InstallStep const&)> const& progress,
    std::atomic<bool> const* cancel) {
    std::error_code ec;
    fs::path const mem = MemExe();
    if (!fs::exists(mem, ec)) {
        return "Mass Effect Modder isn't downloaded yet";
    }
    auto report = [&](std::string stage, int percent, std::string detail) {
        if (progress) {
            progress({std::move(stage), percent, std::move(detail)});
        }
    };
    fs::path const work = LocalMevr() / L"packs" / detect::Wide(pack.id);
    fs::create_directories(work, ec);
    std::string lastError;
    auto onLine = [&](std::string_view text) {
        IpcLine const ipc = ParseIpc(text);
        if (ipc.command.empty()) {
            return;
        }
        if (ipc.command == "ERROR" || ipc.command == "EXCEPTION_OCCURRED") {
            lastError = ipc.argument;
        }
    };
    // 1. The .mem files: as they are, or unpacked from the archive.
    std::vector<fs::path> memFiles;
    std::string const ext = detect::Utf8(download.extension().wstring());
    if (_stricmp(ext.c_str(), ".mem") == 0) {
        memFiles.push_back(download);
    } else {
        report("Unpacking " + detect::Utf8(download.filename().wstring()), 0, "");
        std::wstring const command = L"\"" + mem.wstring() + L"\" --unpack-archive --input \"" + download.wstring() + L"\" --output \"" + work.wstring() +
            L"\" --ipc";
        if (int const code = Run(command, work, onLine, cancel); code != 0) {
            return "unpacking failed (MEM exit code " + std::to_string(code) + ")" + (lastError.empty() ? "" : ": " + lastError);
        }
        for (auto const& entry : fs::recursive_directory_iterator(work, ec)) {
            if (_wcsicmp(entry.path().extension().c_str(), L".mem") == 0) {
                memFiles.push_back(entry.path());
            }
        }
    }
    if (memFiles.empty()) {
        return "no .mem files in " + detect::Utf8(download.filename().wstring());
    }
    std::sort(memFiles.begin(), memFiles.end());
    fs::path const mfl = work / L"install.mfl";
    if (!WriteFileAtomic(mfl, MflFor(memFiles))) {
        return "couldn't write the file list";
    }
    // 2. Point MEM at this game.
    std::wstring const setPath = L"\"" + mem.wstring() + L"\" --set-game-data-path --gameid 1 --path \"" + gameRoot.wstring() + L"\"";
    if (int const code = Run(setPath, work, onLine, cancel); code != 0) {
        return "MEM didn't accept the game folder (exit code " + std::to_string(code) + ")";
    }
    // 3. Install, after every content mod (the caller has warned the player).
    std::string stage = "Installing textures";
    std::wstring const install = L"\"" + mem.wstring() + L"\" --install-mods --gameid 1 --input \"" + mfl.wstring() + L"\" --verify --ipc";
    int const code = Run(install, work, [&](std::string_view text) {
        IpcLine const ipc = ParseIpc(text);
        if (ipc.command == "STAGE_CONTEXT") {
            stage = StageText(ipc.argument);
            report(stage, 0, "");
        } else if (ipc.command == "TASK_PROGRESS") {
            report(stage, std::atoi(ipc.argument.c_str()), "");
        } else if (ipc.command == "PROCESSING_FILE") {
            report(stage, -1, ipc.argument);
        } else if (ipc.command == "ERROR" || ipc.command == "EXCEPTION_OCCURRED") {
            lastError = ipc.argument;
        }
    }, cancel);
    if (code != 0) {
        return "MEM failed (exit code " + std::to_string(code) + ")" + (lastError.empty() ? "" : ": " + lastError);
    }
    report("Done", 100, "");
    return {};
}

} // namespace mevr::install::packs

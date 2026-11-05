// SPDX-License-Identifier: MIT
#include "plugin/Voice.hpp"

#include "core/DevChannels.hpp"
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"
#include "halcyon/le1/mevr_bridge.h"
#include "plugin/MevrMenu.hpp"
#include "plugin/VoiceModel.hpp"
#include "voice/VoiceApi.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace mevr::host::voice {

namespace fs = std::filesystem;
namespace json = vr::test_request;

namespace {

struct Settings {
    bool enabled = true;
    std::string model = "tiny";  // its partials cost the least beside the running game (research/54 "In the game": 300 ms at worst against small's 950)
    std::wstring modelDir;  // empty: ASI\voice\models\<model>-streaming-en
    std::wstring dll;       // empty: ASI\voice\MEVR-Voice.dll
    std::string device;
    int key = 0;  // a virtual key held to talk (0: none)
    int tailMs = 200;
    double duckDb = 12.0;
    int intervalMs = 250;  // fewer partials: each costs 100-300 ms of a worker beside the game
    bool wordTimestamps = true;
    std::string keyterms = "Garrus,Wrex,Liara,Tali,Ashley,Kaidan,Shepard,Overload,Sabotage,Damping,Lift,Throw,Singularity,Warp,Stasis,Barrier,Neural Shock,"
                           "Marksman,Carnage,Adrenaline Burst,Immunity,Assassination,Shield Boost,First Aid,Unity";
};

struct Functions {
    HMODULE module = nullptr;
    MevrVoiceStartupFn startup = nullptr;
    MevrVoiceShutdownFn shutdown = nullptr;
    MevrVoiceListenFn listen = nullptr;
    MevrVoiceTakeFn take = nullptr;
    MevrVoiceStateFn state = nullptr;
    MevrVoiceFeedFileFn feedFile = nullptr;
    MevrVoiceSetKeytermsFn setKeyterms = nullptr;
    MevrVoiceApiVersionFn apiVersion = nullptr;
};

Settings gSettings;
Functions gFns;
std::atomic<bool> gLoaded{false};  // the DLL's functions are usable
std::atomic<bool> gStarted{false};
std::thread gLoader;
std::wstring gSettingsPath;
fs::path gAsiDir;
std::string gWhyOff;
std::atomic<bool> gModListen{false};
std::atomic<bool> gKeyListen{false};
std::atomic<std::uint64_t> gRequestListenUntilMs{0};
bool gListening = false;
std::mutex gKeytermsMutex;
std::string gPendingKeyterms;  // set before the DLL loaded
std::uint64_t gNextRequestMs = 0;
std::uint64_t gNextLogMs = 0;
unsigned gTaken = 0, gListens = 0;
std::string gLastLine;

void DllLog(int level, char const* message) {
    if (level == 0) {
        log::Info("{}", message);
    } else {
        log::Warn("{}", message);
    }
}

std::string Narrow(std::wstring const& w) {
    return ini::Utf8(fs::path(w));
}

std::string ReadText(wchar_t const* key, char const* fallback) {
    wchar_t text[1024]{};
    GetPrivateProfileStringW(L"Voice", key, L"", text, static_cast<DWORD>(std::size(text)), gSettingsPath.c_str());
    std::string const narrow = Narrow(text);
    return narrow.empty() ? std::string(fallback) : narrow;
}

double ReadNumber(wchar_t const* key, double fallback) {
    wchar_t text[64]{};
    GetPrivateProfileStringW(L"Voice", key, L"", text, static_cast<DWORD>(std::size(text)), gSettingsPath.c_str());
    return ini::Number(Narrow(text)).value_or(fallback);  // never strtod (the C locale)
}

void ReadSettings() {
    gSettings.enabled = ReadNumber(L"enabled", 1.0) != 0.0;
    gSettings.model = ReadText(L"model", "tiny");
    if (gSettings.model != "tiny" && gSettings.model != "small" && gSettings.model != "medium") {
        log::Warn("voice: [Voice] model = {} isn't tiny, small or medium: tiny", gSettings.model);
        gSettings.model = "tiny";
    }
    wchar_t text[1024]{};
    GetPrivateProfileStringW(L"Voice", L"modelDir", L"", text, static_cast<DWORD>(std::size(text)), gSettingsPath.c_str());
    gSettings.modelDir = text;
    GetPrivateProfileStringW(L"Voice", L"dll", L"", text, static_cast<DWORD>(std::size(text)), gSettingsPath.c_str());
    gSettings.dll = text;
    gSettings.device = ReadText(L"device", "");
    gSettings.key = static_cast<int>(ReadNumber(L"key", 0.0));
    gSettings.tailMs = static_cast<int>(ReadNumber(L"tailMs", 200.0));
    gSettings.duckDb = ReadNumber(L"duckDb", 12.0);
    gSettings.intervalMs = static_cast<int>(ReadNumber(L"intervalMs", 250.0));
    gSettings.wordTimestamps = ReadNumber(L"wordTimestamps", 1.0) != 0.0;
    gSettings.keyterms = ReadText(L"keyterms", gSettings.keyterms.c_str());
}

int Arch(std::string const& model) {
    if (model == "tiny") return 2;
    if (model == "medium") return 5;
    return 4;
}

template <typename T>
bool Resolve(HMODULE module, char const* name, T& out) {
    out = reinterpret_cast<T>(GetProcAddress(module, name));
    return out != nullptr;
}

void LoaderMain() {
    fs::path const dll = gSettings.dll.empty() ? gAsiDir / L"voice" / L"MEVR-Voice.dll" : fs::path(gSettings.dll);
    fs::path const models = gSettings.modelDir.empty() ? gAsiDir / L"voice" / L"models" / (std::wstring(gSettings.model.begin(), gSettings.model.end()) + L"-streaming-en")
                                                       : fs::path(gSettings.modelDir);
    std::error_code ec;
    if (!fs::exists(dll, ec)) {
        gWhyOff = std::format("no {} (tools/deploy-asi.ps1 -Voice puts it there; tools/fetch-voice.ps1 fetches the library)", ini::Utf8(dll));
        log::Info("voice: off: {}", gWhyOff);
        return;
    }
    if (!fs::exists(models / L"streaming_config.json", ec)) {
        gWhyOff = std::format("no models in {} (tools/fetch-voice.ps1, then tools/deploy-asi.ps1 -Voice)", ini::Utf8(models));
        log::Info("voice: off: {}", gWhyOff);
        return;
    }
    auto const t0 = std::chrono::steady_clock::now();
    // The DLL's own folder first (onnxruntime.dll sits next to it), then the system's: never the game's folder or the path.
    HMODULE const module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        gWhyOff = std::format("{} didn't load (error {})", ini::Utf8(dll), GetLastError());
        log::Warn("voice: off: {}", gWhyOff);
        return;
    }
    Functions f;
    f.module = module;
    bool const ok = Resolve(module, "MevrVoiceStartup", f.startup) && Resolve(module, "MevrVoiceShutdown", f.shutdown) && Resolve(module, "MevrVoiceListen", f.listen) &&
        Resolve(module, "MevrVoiceTake", f.take) && Resolve(module, "MevrVoiceState", f.state) && Resolve(module, "MevrVoiceFeedFile", f.feedFile) &&
        Resolve(module, "MevrVoiceSetKeyterms", f.setKeyterms) && Resolve(module, "MevrVoiceApiVersion", f.apiVersion);
    if (!ok || f.apiVersion() != MEVR_VOICE_API_VERSION) {
        gWhyOff = std::format("{} is another version of the interface ({} against ours {})", ini::Utf8(dll), ok ? f.apiVersion() : -1, MEVR_VOICE_API_VERSION);
        log::Warn("voice: off: {}", gWhyOff);
        FreeLibrary(module);
        return;
    }
    std::string const modelDir = ini::Utf8(models);
    std::string keyterms;
    {
        std::lock_guard<std::mutex> lock(gKeytermsMutex);
        keyterms = gPendingKeyterms.empty() ? gSettings.keyterms : gPendingKeyterms;
    }
    MevrVoiceConfig config;
    config.modelDir = modelDir.c_str();
    config.arch = Arch(gSettings.model);
    config.device = gSettings.device.c_str();
    config.tailMs = gSettings.tailMs;
    config.duckDb = static_cast<float>(gSettings.duckDb);
    config.wordTimestamps = gSettings.wordTimestamps ? 1 : 0;
    config.intervalMs = gSettings.intervalMs;
    config.keyterms = keyterms.c_str();
    config.log = &DllLog;
    int const err = f.startup(&config);
    if (err != 0) {
        gWhyOff = std::format("the library refused to start ({})", err);
        log::Warn("voice: off: {}", gWhyOff);
        FreeLibrary(module);
        return;
    }
    gFns = f;
    gStarted.store(true);
    gLoaded.store(true);
    log::Info("voice: {} loaded in {} ms; the {} models load on its worker from {}; push-to-talk: the radio gesture (the mod){}", ini::Utf8(dll),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(), gSettings.model, modelDir,
        gSettings.key != 0 ? std::format(", or key {} held", gSettings.key) : std::string());
}

void Apply(bool listen) {
    if (listen == gListening) return;
    gListening = listen;
    if (!gLoaded.load()) return;
    int const err = gFns.listen(listen ? 1 : 0);
    if (listen) ++gListens;
    if (err != 0) log::Warn("voice: listen {} refused ({})", listen ? "on" : "off", err);
}

bool TakeRequest(fs::path const& path, std::string& text) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return false;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (!DeleteFileW(path.c_str())) {
        fs::path const taken = fs::path(path).concat(L".taken");
        MoveFileExW(path.c_str(), taken.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    return true;
}

fs::path RequestFile() {
    wchar_t buffer[MAX_PATH]{};
    DWORD const n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    std::error_code ec;
    fs::path const base = (n > 0 && n < MAX_PATH) ? fs::path(buffer) : fs::temp_directory_path(ec);
    return base / L"MEVR" / L"voice.request";
}

}  // namespace

void Init() {
    gSettingsPath = MELEVR::MevrBridge::SettingsPath();
    if (gSettingsPath.empty()) {
        gWhyOff = "no settings file path";
        return;
    }
    gAsiDir = fs::path(gSettingsPath).parent_path();
    ReadSettings();
    if (!gSettings.enabled) {
        gWhyOff = "[Voice] enabled = 0";
        log::Info("voice: off ([Voice] enabled = 0)");
        return;
    }
    gLoader = std::thread(LoaderMain);
}

void OnPresent() {
    std::uint64_t const now = GetTickCount64();
    bool const key = gSettings.key != 0 && MELEVR::MevrBridge::KeyDown(gSettings.key);
    gKeyListen.store(key);
    if (now >= gNextRequestMs) {
        gNextRequestMs = now + 250;
        if (dev_channels::Allowed(fs::path(gSettingsPath))) {
            std::string text;
            if (TakeRequest(RequestFile(), text)) {
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
                Command(text.c_str());
            }
        }
    }
    bool const request = gRequestListenUntilMs.load() > now;
    Apply(gModListen.load() || key || request);
    if (gLoaded.load() && now >= gNextLogMs) {
        gNextLogMs = now + 30000;
        vr::voice::State s;
        gFns.state(&s, sizeof s);
        if (s.status == static_cast<std::uint8_t>(vr::voice::Status::Ready) || s.status == static_cast<std::uint8_t>(vr::voice::Status::Failed)) {
            log::Info("voice: {}; device \"{}\"; {} listens, {} lines, the last partial cost {} ms; level {:.3f}{}", s.status == 2 ? "ready" : "failed", s.device, s.listens, s.lines,
                s.transcribeMs, s.level, s.error[0] != '\0' ? std::format("; error: {}", s.error) : std::string());
        }
    }
}

void Shutdown() {
    gModListen.store(false);
    if (gLoader.joinable()) gLoader.join();
    if (gStarted.exchange(false)) {
        gFns.shutdown();
    }
    gLoaded.store(false);
    // The DLL stays loaded (its static ONNX Runtime registers thread-local state; freeing it at exit isn't worth the risk).
}

void SetListen(int on) {
    gModListen.store(on != 0);
}

bool TakeLine(vr::voice::Line* out) {
    if (out == nullptr || !gLoaded.load()) return false;
    if (gFns.take(out, sizeof *out) == 0) return false;
    ++gTaken;
    gLastLine = out->text;
    return true;
}

void GetState(vr::voice::State* out) {
    if (out == nullptr) return;
    *out = {};
    if (gLoaded.load()) {
        gFns.state(out, sizeof *out);
    } else {
        std::size_t const n = std::min(gWhyOff.size(), sizeof out->error - 1);
        std::memcpy(out->error, gWhyOff.data(), n);
        out->error[n] = '\0';
        out->status = static_cast<std::uint8_t>(gLoader.joinable() && gWhyOff.empty() ? vr::voice::Status::Loading : vr::voice::Status::Off);
    }
    out->keyHeld = gKeyListen.load() ? 1 : 0;
}

void SetKeyterms(char const* terms) {
    std::string const text = terms != nullptr ? terms : "";
    {
        std::lock_guard<std::mutex> lock(gKeytermsMutex);
        if (text == gPendingKeyterms) return;
        gPendingKeyterms = text;
    }
    if (gLoaded.load()) {
        int const err = gFns.setKeyterms(text.c_str());
        log::Info("voice: key terms {}: {}", err == 0 ? "set" : std::format("refused ({})", err), text);
    }
}

void Command(char const* line) {
    std::string const text = line != nullptr ? line : "";
    auto const space = text.find(' ');
    std::string const verb = text.substr(0, space);
    std::string const rest = space == std::string::npos ? std::string() : text.substr(space + 1);
    if (verb == "wav") {
        if (!gLoaded.load()) {
            log::Warn("voice: wav: the recognizer isn't loaded ({})", gWhyOff.empty() ? "still loading" : gWhyOff);
            return;
        }
        std::wstring const path = fs::path(rest).wstring();
        int const err = gFns.feedFile(path.c_str());
        log::Info("voice: request wav {} -> {}", rest, err == 0 ? "fed as one listen" : std::format("refused ({})", err));
    } else if (verb == "listen") {
        double const seconds = ini::Number(rest).value_or(3.0);
        gRequestListenUntilMs.store(GetTickCount64() + static_cast<std::uint64_t>(std::max(0.0, seconds) * 1000.0));
        log::Info("voice: request listen {:.1f} s from the microphone", seconds);
    } else if (verb == "keyterms") {
        SetKeyterms(rest.c_str());
    } else if (verb == "stop") {
        gRequestListenUntilMs.store(0);
        gModListen.store(false);
        log::Info("voice: request stop");
    } else if (!verb.empty()) {
        log::Warn("voice: unknown request \"{}\" (wav <path> | listen <seconds> | keyterms <a,b> | stop)", text);
    }
}

std::string StatusJson() {
    vr::voice::State s;
    GetState(&s);
    return std::format("{{\"status\": {}, \"listening\": {}, \"draining\": {}, \"keyHeld\": {}, \"level\": {}, \"device\": {}, \"model\": {}, \"error\": {}, \"loadMs\": {}, "
                       "\"transcribeMs\": {}, \"listens\": {}, \"lines\": {}, \"taken\": {}, \"lastLine\": {}, \"model\": {}}}",
        s.status, s.listening != 0 ? "true" : "false", s.draining != 0 ? "true" : "false", s.keyHeld != 0 ? "true" : "false", json::JsonNumber(s.level), json::JsonString(s.device),
        json::JsonString(s.model), json::JsonString(s.error), s.loadMs, s.transcribeMs, s.listens, s.lines, gTaken, json::JsonString(gLastLine), voice_model::StatusJson());
}

}  // namespace mevr::host::voice

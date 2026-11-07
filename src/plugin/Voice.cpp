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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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
    int threads = 1;       // 1: the recognizer on one thread; anything else: ONNX Runtime's every core (it starved the game)
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
    MevrVoiceDevicesFn devices = nullptr;
};

Settings gSettings;  // written by the worker (a restart re-reads it) under gSettingsMutex; read there or under it
std::mutex gSettingsMutex;
std::atomic<int> gKey{0};  // [Voice] key, read every Present (the menu sets it at once)
Functions gFns;            // set once, by the worker, before gDllLoaded; never changed after (the DLL stays loaded)
std::atomic<bool> gDllLoaded{false};
std::atomic<bool> gLoaded{false};  // the recognizer runs: Listen, Take, State usable
std::atomic<bool> gStarted{false};
std::atomic<bool> gStarting{false};  // a start (or the menu's restart) is queued or running
std::atomic<bool> gResyncListen{false};
std::wstring gSettingsPath;
fs::path gAsiDir;
std::mutex gWhyMutex;
std::string gWhyOff;  // why the recognizer isn't running (empty: it is, or it's starting)

// The voice worker (research/54 "The menu's Voice page"): the start at attach, the menu's restarts and its device lists, one at a
// time, off the render thread (a restart joins the DLL's threads; the models load for seconds).
std::mutex gJobMutex;
std::thread gJob;
bool gJobRunning = false;
bool gWantStart = false;
bool gWantDevices = false;
std::string gStartWhy;
std::atomic<bool> gStopping{false};
std::mutex gMenuMutex;  // the menu's view below
std::vector<std::string> gDevices;
int gDefaultDevice = -1;
bool gListed = false;
std::string gListNote;  // why the list is empty (no DLL...)
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

void SetWhyOff(std::string text) {
    std::lock_guard<std::mutex> lock(gWhyMutex);
    gWhyOff = std::move(text);
}

std::string WhyOff() {
    std::lock_guard<std::mutex> lock(gWhyMutex);
    return gWhyOff;
}

// [Voice] from the file into gSettings (the worker, or Init before it starts).
void ReadSettings() {
    std::lock_guard<std::mutex> lock(gSettingsMutex);
    gSettings = Settings{};  // a key taken out of the file goes back to its default
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
    gSettings.threads = static_cast<int>(ReadNumber(L"threads", 1.0));
    gSettings.wordTimestamps = ReadNumber(L"wordTimestamps", 1.0) != 0.0;
    gSettings.keyterms = ReadText(L"keyterms", gSettings.keyterms.c_str());
    gKey.store(gSettings.key);
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

// The DLL loaded and its functions resolved, once (it stays loaded). Empty, or why not. The worker's.
std::string EnsureDll(Settings const& settings) {
    if (gDllLoaded.load()) return {};
    fs::path const dll = settings.dll.empty() ? gAsiDir / L"voice" / L"MEVR-Voice.dll" : fs::path(settings.dll);
    std::error_code ec;
    if (!fs::exists(dll, ec)) {
        return std::format("no {} (tools/deploy-asi.ps1 -Voice puts it there; tools/fetch-voice.ps1 fetches the library)", ini::Utf8(dll));
    }
    auto const t0 = std::chrono::steady_clock::now();
    // The DLL's own folder first (onnxruntime.dll sits next to it), then the system's: never the game's folder or the path.
    HMODULE const module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        return std::format("{} didn't load (error {})", ini::Utf8(dll), GetLastError());
    }
    Functions f;
    f.module = module;
    bool const ok = Resolve(module, "MevrVoiceStartup", f.startup) && Resolve(module, "MevrVoiceShutdown", f.shutdown) && Resolve(module, "MevrVoiceListen", f.listen) &&
        Resolve(module, "MevrVoiceTake", f.take) && Resolve(module, "MevrVoiceState", f.state) && Resolve(module, "MevrVoiceFeedFile", f.feedFile) &&
        Resolve(module, "MevrVoiceSetKeyterms", f.setKeyterms) && Resolve(module, "MevrVoiceApiVersion", f.apiVersion) && Resolve(module, "MevrVoiceDevices", f.devices);
    int const version = f.apiVersion != nullptr ? f.apiVersion() : -1;
    if (!ok || version != MEVR_VOICE_API_VERSION) {
        FreeLibrary(module);
        return std::format("{} is another version of the interface ({} against ours {}): deploy the host and ASI\\voice together", ini::Utf8(dll), version,
            MEVR_VOICE_API_VERSION);
    }
    gFns = f;
    gDllLoaded.store(true);
    log::Info("voice: {} loaded in {} ms", ini::Utf8(dll), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    return {};
}

// Starts the recognizer with these settings (the worker's). Its outcome in gWhyOff.
void StartRecognizer(Settings const& settings) {
    if (!settings.enabled) {
        SetWhyOff("[Voice] enabled = 0");
        log::Info("voice: off ([Voice] enabled = 0)");
        return;
    }
    if (std::string const why = EnsureDll(settings); !why.empty()) {
        SetWhyOff(why);
        log::Info("voice: off: {}", why);
        return;
    }
    fs::path const models = settings.modelDir.empty() ? gAsiDir / L"voice" / L"models" / (std::wstring(settings.model.begin(), settings.model.end()) + L"-streaming-en")
                                                      : fs::path(settings.modelDir);
    std::error_code ec;
    if (!fs::exists(models / L"streaming_config.json", ec)) {
        std::string const why = std::format("no {} models in {} (MEVR Setup's Optional content, or tools/fetch-voice.ps1 then tools/deploy-asi.ps1 -Voice)",
            settings.model, ini::Utf8(models));
        SetWhyOff(why);
        log::Info("voice: off: {}", why);
        return;
    }
    std::string const modelDir = ini::Utf8(models);
    std::string keyterms;
    {
        std::lock_guard<std::mutex> lock(gKeytermsMutex);
        keyterms = gPendingKeyterms.empty() ? settings.keyterms : gPendingKeyterms;
    }
    MevrVoiceConfig config;
    config.modelDir = modelDir.c_str();
    config.arch = Arch(settings.model);
    config.device = settings.device.c_str();
    config.tailMs = settings.tailMs;
    config.duckDb = static_cast<float>(settings.duckDb);
    config.wordTimestamps = settings.wordTimestamps ? 1 : 0;
    config.intervalMs = settings.intervalMs;
    config.threads = settings.threads;
    config.keyterms = keyterms.c_str();
    config.log = &DllLog;
    int const err = gFns.startup(&config);
    if (err != 0) {
        std::string const why = std::format("the library refused to start ({})", err);
        SetWhyOff(why);
        log::Warn("voice: off: {}", why);
        return;
    }
    SetWhyOff({});
    gStarted.store(true);
    gLoaded.store(true);
    log::Info("voice: the {} models load on its worker from {}; microphone: {}; push-to-talk: the radio gesture (the mod){}", settings.model, modelDir,
        settings.device.empty() ? std::string("the default communications device") : std::format("named like \"{}\"", settings.device),
        settings.key != 0 ? std::format(", or key {} held", settings.key) : std::string());
}

void StopRecognizer() {
    gLoaded.store(false);  // the render thread stops listening and taking; the functions stay valid (the DLL stays loaded)
    if (gStarted.exchange(false)) gFns.shutdown();
    gResyncListen.store(true);
}

// The capture devices, for the menu.
void ListDevicesNow() {
    Settings settings;
    {
        std::lock_guard<std::mutex> lock(gSettingsMutex);
        settings = gSettings;
    }
    std::vector<std::string> names;
    int defaultIndex = -1;
    std::string note;
    if (std::string const why = EnsureDll(settings); !why.empty()) {
        note = why;
    } else {
        std::vector<char> buffer(16384);
        int const n = gFns.devices(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &defaultIndex);
        if (n < 0) {
            note = std::format("the capture devices couldn't be listed ({})", n);
        }
        std::string_view const all(buffer.data());
        for (std::size_t at = 0; at < all.size();) {
            std::size_t const end = all.find('\n', at);
            if (end == std::string_view::npos) break;
            names.emplace_back(all.substr(at, end - at));
            at = end + 1;
        }
    }
    std::lock_guard<std::mutex> lock(gMenuMutex);
    bool const changed = !gListed || names != gDevices || defaultIndex != gDefaultDevice;
    gDevices = std::move(names);
    gDefaultDevice = defaultIndex < static_cast<int>(gDevices.size()) ? defaultIndex : -1;
    gListed = note.empty();
    gListNote = note;
    if (changed) {
        log::Info("voice: {} capture devices for the menu{}{}", gDevices.size(),
            gDefaultDevice >= 0 ? std::format(" (the default: {})", gDevices[static_cast<std::size_t>(gDefaultDevice)]) : std::string(),
            note.empty() ? std::string() : std::format("; {}", note));
    }
}

void JobMain() {
    for (;;) {
        bool start = false, devices = false;
        std::string why;
        {
            std::lock_guard<std::mutex> lock(gJobMutex);
            start = gWantStart && !gStopping.load();
            devices = gWantDevices && !gStopping.load();
            gWantStart = gWantDevices = false;
            why = gStartWhy;
            if (!start && !devices) {
                gJobRunning = false;
                return;
            }
        }
        if (start) {
            bool const restart = !why.empty();
            if (restart) log::Info("voice: restarting the recognizer ({})", why);
            auto const t0 = std::chrono::steady_clock::now();
            StopRecognizer();
            ReadSettings();
            Settings settings;
            {
                std::lock_guard<std::mutex> lock(gSettingsMutex);
                settings = gSettings;
            }
            StartRecognizer(settings);
            if (restart) {
                log::Info("voice: restarted in {} ms ({})", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(),
                    WhyOff().empty() ? std::string("the models are loading") : WhyOff());
            }
            gStarting.store(false);
        }
        if (devices) ListDevicesNow();
    }
}

// Queues work for the voice worker (the render thread's, or Init's).
void Post(bool start, bool devices, std::string why) {
    std::lock_guard<std::mutex> lock(gJobMutex);
    if (gStopping.load()) return;
    if (start) {
        gWantStart = true;
        gStartWhy = std::move(why);
        gStarting.store(true);
    }
    if (devices) gWantDevices = true;
    if (!gJobRunning) {
        if (gJob.joinable()) gJob.join();  // it has left its loop (gJobRunning false), so this returns at once
        gJobRunning = true;
        gJob = std::thread(JobMain);
    }
}

void Apply(bool listen) {
    if (gResyncListen.exchange(false)) gListening = false;  // a restarted recognizer listens to nothing yet
    if (listen == gListening) return;
    // gListening is the last accepted request, not the desired input. Keep retrying a held input through restart/loading.
    // The DLL then owns an accepted start until phase 3 has drained and StartListen succeeds (see its worker state machine).
    if (!gLoaded.load() || gStarting.load()) return;
    vr::voice::State state;
    gFns.state(&state, sizeof state);
    if (state.status != static_cast<std::uint8_t>(vr::voice::Status::Ready)) return;
    static int lastRefused = 0;  // retried every frame while held: one line per new refusal
    int const err = gFns.listen(listen ? 1 : 0);
    if (err == 0) {
        lastRefused = 0;
        gListening = listen;
        if (listen) ++gListens;
    } else {
        if (err != lastRefused) log::Warn("voice: listen {} refused ({})", listen ? "on" : "off", err);
        lastRefused = err;
        return;
    }
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
        SetWhyOff("no settings file path");
        return;
    }
    gAsiDir = fs::path(gSettingsPath).parent_path();
    ReadSettings();
    Post(true, false, {});  // the worker reads them again and starts (or says why not)
}

void OnPresent() {
    std::uint64_t const now = GetTickCount64();
    int const vk = gKey.load();
    bool const key = vk != 0 && MELEVR::MevrBridge::KeyDown(vk);
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
    std::thread job;
    {
        std::lock_guard<std::mutex> lock(gJobMutex);
        gStopping.store(true);  // a queued restart or list is dropped; one running finishes first
        job = std::move(gJob);
    }
    if (job.joinable()) job.join();
    StopRecognizer();
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
        std::string const why = WhyOff();
        std::size_t const n = std::min(why.size(), sizeof out->error - 1);
        std::memcpy(out->error, why.data(), n);
        out->error[n] = '\0';
        out->status = static_cast<std::uint8_t>(gStarting.load() ? vr::voice::Status::Loading : vr::voice::Status::Off);
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
            log::Warn("voice: wav: the recognizer isn't loaded ({})", WhyOff().empty() ? std::string("still loading") : WhyOff());
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

MenuView Menu() {
    MenuView view;
    {
        std::lock_guard<std::mutex> lock(gSettingsMutex);
        view.enabled = gSettings.enabled;
        view.model = gSettings.model;
        view.device = gSettings.device;
    }
    view.key = gKey.load();
    view.restarting = gStarting.load();
    GetState(&view.state);
    std::lock_guard<std::mutex> lock(gMenuMutex);
    view.listed = gListed;
    view.devices = gDevices;
    view.defaultDevice = gDefaultDevice;
    view.listNote = gListNote;
    return view;
}

void ListDevices() {
    Post(false, true, {});
}

void Restart(std::string const& why) {
    Post(true, true, why.empty() ? std::string("asked") : why);
}

void SetKey(int vk) {
    gKey.store(vk);
}

float Level() {
    if (!gLoaded.load()) return 0.0f;
    vr::voice::State s;
    gFns.state(&s, sizeof s);
    return s.level;
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

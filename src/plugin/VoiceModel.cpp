// SPDX-License-Identifier: MIT
#include "plugin/VoiceModel.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"
#include "plugin/MevrMenu.hpp"

#if !defined(MEVR_HAVE_LLAMA)
// Built without llama.cpp's header (external/llama-cpp absent: CI, a fresh clone): the model tier is off.
namespace mevr::host::voice_model {
void Init() {
    log::Info("voice model: off: this build has no llama.cpp runtime (tools/fetch-voice.ps1 -Llm, then configure again)");
}
void Shutdown() {}
std::uint32_t Ask(char const*, char const*, char const*) { return 0; }
bool TakeAnswer(std::uint32_t*, char*, std::uint32_t, std::uint32_t*) { return false; }
std::string StatusJson() { return "{\"loaded\": false, \"whyOff\": \"built without llama.cpp\"}"; }
}  // namespace mevr::host::voice_model
#else

#include "core/voice/Llm.hpp"

#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <thread>

namespace mevr::host::voice_model {

namespace fs = std::filesystem;
namespace json = vr::test_request;

namespace {

struct Request {
    std::uint32_t id = 0;
    std::string prefix, rest, gbnf;
};
struct Reply {
    std::uint32_t id = 0;
    std::string text;
    std::uint32_t ms = 0;
};

voice::llm::Runner gRunner;
std::thread gWorker;
std::mutex gMutex;
std::condition_variable gWake;
bool gStop = false;
Request gPending;  // id 0: none
Reply gReply;      // id 0: none
std::atomic<std::uint32_t> gNextId{1};
std::atomic<bool> gLoaded{false};
std::string gWhyOff;
std::string gModelPath;
unsigned gAsked = 0, gAnswered = 0, gReplaced = 0;
std::uint32_t gLastPrefillMs = 0, gLastDecodeMs = 0;
std::string gLastAnswer;

std::wstring gSettingsPath;

double ReadNumber(wchar_t const* key, double fallback) {
    wchar_t text[64]{};
    GetPrivateProfileStringW(L"Voice", key, L"", text, static_cast<DWORD>(std::size(text)), gSettingsPath.c_str());
    return ini::Number(ini::Utf8(fs::path(text))).value_or(fallback);
}

std::wstring ReadPath(wchar_t const* key) {
    wchar_t text[1024]{};
    GetPrivateProfileStringW(L"Voice", key, L"", text, static_cast<DWORD>(std::size(text)), gSettingsPath.c_str());
    return text;
}

void WorkerMain(fs::path libraryDir, fs::path model, int threads) {
    voice::llm::Settings settings;
    settings.threads = threads;
    std::string error;
    if (!gRunner.Load(libraryDir, model, settings, &error)) {
        std::lock_guard<std::mutex> lock(gMutex);
        gWhyOff = error;
        log::Warn("voice model: off: {}", error);
        return;
    }
    gLoaded.store(true);
    log::Info("voice model: {} loaded from {} in {} ms on {} threads", gRunner.Description(), ini::Utf8(model), gRunner.LoadMs(), threads);
    while (true) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(gMutex);
            gWake.wait(lock, [] { return gStop || gPending.id != 0; });
            if (gStop) break;
            request = gPending;
            gPending = {};
        }
        std::string why;
        if (!gRunner.SetPrefix(request.prefix, &why)) {
            log::Warn("voice model: the prefix didn't decode: {}", why);
            continue;
        }
        voice::llm::Answer const answer = gRunner.Ask(request.rest, request.gbnf);
        std::lock_guard<std::mutex> lock(gMutex);
        gLastPrefillMs = answer.prefillMs;
        gLastDecodeMs = answer.decodeMs;
        gLastAnswer = answer.ok ? answer.text : "error: " + answer.error;
        gReply = {request.id, answer.ok ? answer.text : std::string(), answer.prefillMs + answer.decodeMs};
        ++gAnswered;
        log::Info("voice model: #{} -> \"{}\" ({} prompt tokens in {} ms, {} answer tokens in {} ms){}", request.id, gLastAnswer, answer.promptTokens, answer.prefillMs,
            answer.answerTokens, answer.decodeMs, answer.ok ? "" : " (an error: nothing given)");
    }
}

}  // namespace

void Init() {
    gSettingsPath = MELEVR::MevrBridge::SettingsPath();
    if (gSettingsPath.empty()) return;
    if (ReadNumber(L"llm", 1.0) == 0.0) {
        gWhyOff = "[Voice] llm = 0";
        return;
    }
    fs::path const asi = fs::path(gSettingsPath).parent_path();
    std::wstring const dirSetting = ReadPath(L"llmDir");
    fs::path const dir = dirSetting.empty() ? asi / L"voice" / L"llm" : fs::path(dirSetting);
    std::wstring const modelSetting = ReadPath(L"llmModel");
    fs::path model = modelSetting.empty() ? dir / L"model.gguf" : fs::path(modelSetting);
    std::error_code ec;
    if (modelSetting.empty() && !fs::exists(model, ec) && fs::is_directory(dir, ec)) {
        for (auto const& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() == L".gguf") {
                model = entry.path();
                break;
            }
        }
    }
    if (!fs::exists(dir / L"llama.dll", ec)) {
        gWhyOff = std::format("no llama.dll in {} (tools/fetch-voice.ps1 -Llm, then tools/deploy-asi.ps1)", ini::Utf8(dir));
        log::Info("voice model: off: {}", gWhyOff);
        return;
    }
    if (!fs::exists(model, ec)) {
        gWhyOff = std::format("no model at {} (a .gguf in ASI\\voice\\llm, or [Voice] llmModel)", ini::Utf8(model));
        log::Info("voice model: off: {}", gWhyOff);
        return;
    }
    gModelPath = ini::Utf8(model);
    int const threads = static_cast<int>(ReadNumber(L"llmThreads", 4.0));
    gWorker = std::thread(WorkerMain, dir, model, std::clamp(threads, 1, 16));
}

void Shutdown() {
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gStop = true;
    }
    gWake.notify_all();
    if (gWorker.joinable()) gWorker.join();
    gRunner.Unload();
    gLoaded.store(false);
}

std::uint32_t Ask(char const* prefix, char const* rest, char const* gbnf) {
    if (!gLoaded.load() || rest == nullptr) return 0;
    std::uint32_t const id = gNextId.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (gPending.id != 0) ++gReplaced;
        gPending = {id, prefix != nullptr ? prefix : "", rest, gbnf != nullptr ? gbnf : ""};
        ++gAsked;
    }
    gWake.notify_one();
    return id;
}

bool TakeAnswer(std::uint32_t* id, char* text, std::uint32_t capacity, std::uint32_t* ms) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gReply.id == 0 || text == nullptr || capacity == 0) return false;
    if (id != nullptr) *id = gReply.id;
    if (ms != nullptr) *ms = gReply.ms;
    std::size_t const n = std::min<std::size_t>(gReply.text.size(), capacity - 1);
    std::memcpy(text, gReply.text.data(), n);
    text[n] = '\0';
    gReply = {};
    return true;
}

std::string StatusJson() {
    std::lock_guard<std::mutex> lock(gMutex);
    return std::format("{{\"loaded\": {}, \"model\": {}, \"whyOff\": {}, \"asked\": {}, \"answered\": {}, \"replaced\": {}, \"lastPrefillMs\": {}, \"lastDecodeMs\": {}, \"lastAnswer\": {}}}",
        gLoaded.load() ? "true" : "false", json::JsonString(gModelPath), json::JsonString(gWhyOff), gAsked, gAnswered, gReplaced, gLastPrefillMs, gLastDecodeMs,
        json::JsonString(gLastAnswer));
}

}  // namespace mevr::host::voice_model

#endif  // MEVR_HAVE_LLAMA

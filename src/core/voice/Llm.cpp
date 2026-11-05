// SPDX-License-Identifier: MIT
#include "core/voice/Llm.hpp"

// llama.h for its structs and enums only: every function is resolved by name from the DLL below (LLAMA_SHARED is not
// defined, so the header's declarations carry no import attribute and nothing links against them).
#include "llama.h"

#include <Windows.h>

#include <chrono>
#include <cstring>
#include <format>
#include <vector>

namespace mevr::voice::llm {

namespace {

using Clock = std::chrono::steady_clock;

struct Api {
    HMODULE module = nullptr;
    void (*backend_init)() = nullptr;
    void (*log_set)(ggml_log_callback, void*) = nullptr;
    llama_model_params (*model_default_params)() = nullptr;
    llama_model* (*model_load_from_file)(char const*, llama_model_params) = nullptr;
    void (*model_free)(llama_model*) = nullptr;
    llama_context_params (*context_default_params)() = nullptr;
    llama_context* (*init_from_model)(llama_model*, llama_context_params) = nullptr;
    void (*free)(llama_context*) = nullptr;
    llama_vocab const* (*model_get_vocab)(llama_model const*) = nullptr;
    std::int32_t (*tokenize)(llama_vocab const*, char const*, std::int32_t, llama_token*, std::int32_t, bool, bool) = nullptr;
    std::int32_t (*token_to_piece)(llama_vocab const*, llama_token, char*, std::int32_t, std::int32_t, bool) = nullptr;
    bool (*vocab_is_eog)(llama_vocab const*, llama_token) = nullptr;
    llama_batch (*batch_get_one)(llama_token*, std::int32_t) = nullptr;
    std::int32_t (*decode)(llama_context*, llama_batch) = nullptr;
    llama_memory_t (*get_memory)(llama_context const*) = nullptr;
    void (*memory_clear)(llama_memory_t, bool) = nullptr;
    bool (*memory_seq_rm)(llama_memory_t, llama_seq_id, llama_pos, llama_pos) = nullptr;
    llama_sampler_chain_params (*sampler_chain_default_params)() = nullptr;
    llama_sampler* (*sampler_chain_init)(llama_sampler_chain_params) = nullptr;
    void (*sampler_chain_add)(llama_sampler*, llama_sampler*) = nullptr;
    llama_sampler* (*sampler_init_grammar)(llama_vocab const*, char const*, char const*) = nullptr;
    llama_sampler* (*sampler_init_greedy)() = nullptr;
    llama_token (*sampler_sample)(llama_sampler*, llama_context*, std::int32_t) = nullptr;
    void (*sampler_free)(llama_sampler*) = nullptr;
    std::int32_t (*model_desc)(llama_model const*, char*, std::size_t) = nullptr;
    std::uint64_t (*model_n_params)(llama_model const*) = nullptr;
};

template <typename F>
bool Get(HMODULE module, char const* name, F& out, std::string& missing) {
    out = reinterpret_cast<F>(GetProcAddress(module, name));
    if (out == nullptr) missing += std::string(missing.empty() ? "" : ", ") + name;
    return out != nullptr;
}

// The library's own log: errors kept for the caller's message, the rest dropped.
std::string gLastLogError;
void CaptureLog(ggml_log_level level, char const* text, void*) {
    if (level == GGML_LOG_LEVEL_ERROR && text != nullptr) {
        gLastLogError = text;
        while (!gLastLogError.empty() && (gLastLogError.back() == '\n' || gLastLogError.back() == '\r')) gLastLogError.pop_back();
    }
}

}  // namespace

struct Runner::Impl {
    Api api;
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    llama_vocab const* vocab = nullptr;
    Settings settings;
    std::string prefix;
    std::vector<llama_token> prefixTokens;
    bool prefixDecoded = false;
    std::uint32_t loadMs = 0;
    std::string description;

    std::vector<llama_token> Tokenize(std::string_view text, bool addSpecial) const {
        std::vector<llama_token> tokens(text.size() + 16);
        std::int32_t n = api.tokenize(vocab, text.data(), static_cast<std::int32_t>(text.size()), tokens.data(), static_cast<std::int32_t>(tokens.size()), addSpecial, true);
        if (n < 0) {
            tokens.resize(static_cast<std::size_t>(-n));
            n = api.tokenize(vocab, text.data(), static_cast<std::int32_t>(text.size()), tokens.data(), static_cast<std::int32_t>(tokens.size()), addSpecial, true);
        }
        tokens.resize(static_cast<std::size_t>(std::max(n, 0)));
        return tokens;
    }

    // Decodes tokens in batches of the context's batch size; false on a decode error.
    bool Decode(std::vector<llama_token>& tokens) {
        std::size_t at = 0;
        while (at < tokens.size()) {
            std::size_t const n = std::min<std::size_t>(512, tokens.size() - at);
            llama_batch batch = api.batch_get_one(tokens.data() + at, static_cast<std::int32_t>(n));
            if (api.decode(ctx, batch) != 0) return false;
            at += n;
        }
        return true;
    }
};

Runner::Runner() : impl_(new Impl) {}

Runner::~Runner() {
    Unload();
    delete impl_;
}

bool Runner::Load(std::filesystem::path const& libraryDir, std::filesystem::path const& model, Settings const& settings, std::string* error) {
    auto fail = [&](std::string const& why) {
        if (error != nullptr) *error = why;
        return false;
    };
    Unload();
    auto const t0 = Clock::now();
    std::error_code ec;
    std::filesystem::path const dll = std::filesystem::absolute(libraryDir / L"llama.dll", ec);  // the search flags need an absolute path
    HMODULE const module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) return fail(std::format("llama.dll didn't load from the library folder (error {})", GetLastError()));
    Api& a = impl_->api;
    a.module = module;
    std::string missing;
    Get(module, "llama_backend_init", a.backend_init, missing);
    Get(module, "llama_log_set", a.log_set, missing);
    Get(module, "llama_model_default_params", a.model_default_params, missing);
    Get(module, "llama_model_load_from_file", a.model_load_from_file, missing);
    Get(module, "llama_model_free", a.model_free, missing);
    Get(module, "llama_context_default_params", a.context_default_params, missing);
    Get(module, "llama_init_from_model", a.init_from_model, missing);
    Get(module, "llama_free", a.free, missing);
    Get(module, "llama_model_get_vocab", a.model_get_vocab, missing);
    Get(module, "llama_tokenize", a.tokenize, missing);
    Get(module, "llama_token_to_piece", a.token_to_piece, missing);
    Get(module, "llama_vocab_is_eog", a.vocab_is_eog, missing);
    Get(module, "llama_batch_get_one", a.batch_get_one, missing);
    Get(module, "llama_decode", a.decode, missing);
    Get(module, "llama_get_memory", a.get_memory, missing);
    Get(module, "llama_memory_clear", a.memory_clear, missing);
    Get(module, "llama_memory_seq_rm", a.memory_seq_rm, missing);
    Get(module, "llama_sampler_chain_default_params", a.sampler_chain_default_params, missing);
    Get(module, "llama_sampler_chain_init", a.sampler_chain_init, missing);
    Get(module, "llama_sampler_chain_add", a.sampler_chain_add, missing);
    Get(module, "llama_sampler_init_grammar", a.sampler_init_grammar, missing);
    Get(module, "llama_sampler_init_greedy", a.sampler_init_greedy, missing);
    Get(module, "llama_sampler_sample", a.sampler_sample, missing);
    Get(module, "llama_sampler_free", a.sampler_free, missing);
    Get(module, "llama_model_desc", a.model_desc, missing);
    Get(module, "llama_model_n_params", a.model_n_params, missing);
    if (!missing.empty()) {
        FreeLibrary(module);
        a.module = nullptr;
        return fail("llama.dll is another version: missing " + missing);
    }
    a.log_set(&CaptureLog, nullptr);
    // The CPU backend is a DLL of its own next to llama.dll (ggml-cpu-*.dll, one per instruction set): ggml loads the best
    // from a folder; without this call no backend exists and no model loads.
    if (HMODULE const ggml = GetModuleHandleW(L"ggml.dll")) {
        auto const loadAll = reinterpret_cast<void (*)(char const*)>(GetProcAddress(ggml, "ggml_backend_load_all_from_path"));
        std::string const dir = std::filesystem::absolute(libraryDir, ec).string();
        if (loadAll != nullptr) loadAll(dir.c_str());
    }
    a.backend_init();
    llama_model_params mp = a.model_default_params();
    mp.n_gpu_layers = 0;  // the GPU is the VR frame's (research/54)
    std::string const path = std::filesystem::absolute(model, ec).string();
    gLastLogError.clear();
    impl_->model = a.model_load_from_file(path.c_str(), mp);
    if (impl_->model == nullptr) return fail("the model didn't load from " + path + (gLastLogError.empty() ? "" : ": " + gLastLogError));
    llama_context_params cp = a.context_default_params();
    cp.n_ctx = static_cast<std::uint32_t>(std::max(settings.contextTokens, 512));
    cp.n_batch = 512;
    cp.n_ubatch = 512;
    cp.n_threads = settings.threads;
    cp.n_threads_batch = settings.threads;
    impl_->ctx = a.init_from_model(impl_->model, cp);
    if (impl_->ctx == nullptr) {
        a.model_free(impl_->model);
        impl_->model = nullptr;
        return fail("no context for the model");
    }
    impl_->vocab = a.model_get_vocab(impl_->model);
    impl_->settings = settings;
    char desc[256]{};
    a.model_desc(impl_->model, desc, sizeof desc);
    impl_->description = std::format("{} ({:.0f} M parameters)", desc, static_cast<double>(a.model_n_params(impl_->model)) / 1e6);
    impl_->loadMs = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
    return true;
}

bool Runner::Loaded() const {
    return impl_->ctx != nullptr;
}

std::string Runner::Description() const {
    return impl_->description;
}

std::uint32_t Runner::LoadMs() const {
    return impl_->loadMs;
}

bool Runner::SetPrefix(std::string_view prefix, std::string* error) {
    if (!Loaded()) {
        if (error != nullptr) *error = "not loaded";
        return false;
    }
    if (prefix == impl_->prefix && impl_->prefixDecoded) return true;
    impl_->prefix = std::string(prefix);
    impl_->prefixTokens = impl_->Tokenize(prefix, true);
    impl_->api.memory_clear(impl_->api.get_memory(impl_->ctx), true);
    impl_->prefixDecoded = impl_->Decode(impl_->prefixTokens);
    if (!impl_->prefixDecoded && error != nullptr) *error = "the prefix didn't decode";
    return impl_->prefixDecoded;
}

Answer Runner::Ask(std::string_view rest, std::string_view gbnf) {
    Answer answer;
    if (!Loaded()) {
        answer.error = "not loaded";
        return answer;
    }
    Api& a = impl_->api;
    auto const t0 = Clock::now();
    // Back to the end of the prefix; without one, from the start.
    llama_pos const from = impl_->prefixDecoded ? static_cast<llama_pos>(impl_->prefixTokens.size()) : 0;
    if (impl_->prefixDecoded) {
        a.memory_seq_rm(a.get_memory(impl_->ctx), 0, from, -1);
    } else {
        a.memory_clear(a.get_memory(impl_->ctx), true);
    }
    std::vector<llama_token> tokens = impl_->Tokenize(rest, !impl_->prefixDecoded);
    answer.promptTokens = static_cast<int>(tokens.size());
    if (tokens.empty() || !impl_->Decode(tokens)) {
        answer.error = "the prompt didn't decode";
        return answer;
    }
    answer.prefillMs = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
    auto const t1 = Clock::now();
    llama_sampler* const chain = a.sampler_chain_init(a.sampler_chain_default_params());
    if (!gbnf.empty()) {
        std::string const text(gbnf);
        llama_sampler* const grammar = a.sampler_init_grammar(impl_->vocab, text.c_str(), "root");
        if (grammar == nullptr) {
            a.sampler_free(chain);
            answer.error = "the grammar didn't parse";
            return answer;
        }
        a.sampler_chain_add(chain, grammar);
    }
    a.sampler_chain_add(chain, a.sampler_init_greedy());
    std::string out;
    for (int i = 0; i < impl_->settings.maxAnswerTokens; ++i) {
        llama_token token = a.sampler_sample(chain, impl_->ctx, -1);
        if (a.vocab_is_eog(impl_->vocab, token)) break;
        char piece[64]{};
        std::int32_t const n = a.token_to_piece(impl_->vocab, token, piece, sizeof piece, 0, false);
        if (n > 0) out.append(piece, static_cast<std::size_t>(n));
        ++answer.answerTokens;
        if (out.find('\n') != std::string::npos) break;
        llama_batch next = a.batch_get_one(&token, 1);
        if (a.decode(impl_->ctx, next) != 0) {
            answer.error = "a token didn't decode";
            break;
        }
    }
    a.sampler_free(chain);
    answer.decodeMs = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t1).count());
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\r')) out.pop_back();
    while (!out.empty() && (out.front() == ' ' || out.front() == '\n')) out.erase(out.begin());
    answer.text = out;
    answer.ok = answer.error.empty();
    return answer;
}

void Runner::Unload() {
    Api& a = impl_->api;
    if (impl_->ctx != nullptr) a.free(impl_->ctx);
    if (impl_->model != nullptr) a.model_free(impl_->model);
    impl_->ctx = nullptr;
    impl_->model = nullptr;
    impl_->vocab = nullptr;
    impl_->prefixDecoded = false;
    impl_->prefix.clear();
    // The DLL stays loaded for the process: ggml keeps thread pools and backends registered.
}

}  // namespace mevr::voice::llm

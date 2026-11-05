// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// The model tier's runtime (research/54 "Rules first, a fine-tuned sub-1B model second"): llama.cpp's shared build
// (MIT; external/llama-cpp, fetched by tools/fetch-voice.ps1; deployed to ASI\voice\llm) loaded at run time by name, never
// linked, so a game without it runs without the model; a GGUF (Qwen3-0.6B, Apache-2.0, or our fine-tune) on the CPU on a
// few threads; the prompt's prefix (the rules and the roster) decoded once and kept in the KV cache, each request decoding
// only its scene block and transcript after it, the answer sampled greedily under a per-scene GBNF grammar
// (core/voice/Grammar) so it can only be a legal action line. One Runner, one caller at a time (the host's worker).
namespace mevr::voice::llm {

struct Settings {
    int threads = 4;
    int contextTokens = 2048;
    int maxAnswerTokens = 48;
};

struct Answer {
    bool ok = false;
    std::string text;         // the action line(s), or "none"
    std::string error;
    std::uint32_t prefillMs = 0;
    std::uint32_t decodeMs = 0;
    int promptTokens = 0;     // decoded for this request (after the cached prefix)
    int answerTokens = 0;
};

class Runner {
public:
    Runner();
    ~Runner();
    Runner(Runner const&) = delete;
    Runner& operator=(Runner const&) = delete;

    // Loads llama.dll (and the ggml DLLs next to it) from `libraryDir`, then the model. False with `error` set.
    bool Load(std::filesystem::path const& libraryDir, std::filesystem::path const& model, Settings const& settings, std::string* error);
    bool Loaded() const;
    std::string Description() const;  // the model's own description, parameters
    std::uint32_t LoadMs() const;

    // The prefix every prompt starts with (decoded once; a new prefix replaces the cache).
    bool SetPrefix(std::string_view prefix, std::string* error);
    // The rest of the prompt (the scene block and the SAY line, ending in the answer's cue) under the grammar (GBNF; empty: free).
    Answer Ask(std::string_view rest, std::string_view gbnf);

    void Unload();

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace mevr::voice::llm

// SPDX-License-Identifier: MIT
// mevr-llm-bench (research/54 step 6): the model tier offline. Loads llama.cpp's shared build from a folder and a GGUF
// (core/voice/Llm), then answers the prompts of a make_dataset.py JSONL under each record's grammar (core/voice/Grammar's
// GbnfFromBlock), the prefix (through the WORDS line) decoded once and kept, and reports exact-match accuracy against the
// gold answers and the times: the prefill of each scene block and the decode of the answer, median and p95. Zero-shot on
// Qwen3-0.6B this is the baseline the fine-tune (train_lora.py) has to beat.
//
//   mevr-llm-bench --lib external/llama-cpp --model models/voice/llm/Qwen3-0.6B-Q8_0.gguf --data scratch/voice-model/sample.jsonl
//                  [--threads 4] [--limit 50] [--no-grammar] [--show 5]
#include "core/voice/Grammar.hpp"
#include "core/voice/Llm.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

// The two JSON strings of a record ({"prompt": "...", "answer": "..."}), unescaped.
bool Field(std::string const& json, char const* name, std::string& out) {
    std::string const key = std::string("\"") + name + "\": \"";
    std::size_t at = json.find(key);
    if (at == std::string::npos) return false;
    at += key.size();
    out.clear();
    while (at < json.size()) {
        char const c = json[at++];
        if (c == '"') return true;
        if (c != '\\' || at >= json.size()) {
            out += c;
            continue;
        }
        char const e = json[at++];
        switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'u': {
            if (at + 4 <= json.size()) {
                unsigned code = static_cast<unsigned>(std::stoul(json.substr(at, 4), nullptr, 16));
                at += 4;
                if (code < 0x80) out += static_cast<char>(code);
                else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
                else { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
            }
            break;
        }
        default: out += e; break;
        }
    }
    return false;
}

std::string Trimmed(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n')) s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    std::string lib = "external/llama-cpp", model, data;
    int threads = 4, limit = 0, show = 5;
    bool grammar = true;
    for (int i = 1; i < argc; ++i) {
        std::string const a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--lib") lib = next();
        else if (a == "--model") model = next();
        else if (a == "--data") data = next();
        else if (a == "--threads") threads = std::max(1, std::atoi(next().c_str()));
        else if (a == "--limit") limit = std::atoi(next().c_str());
        else if (a == "--show") show = std::atoi(next().c_str());
        else if (a == "--no-grammar") grammar = false;
    }
    if (model.empty() || data.empty()) {
        std::fprintf(stderr, "usage: mevr-llm-bench --lib <folder with llama.dll> --model <gguf> --data <jsonl> [--threads n] [--limit n] [--no-grammar] [--show n]\n");
        return 2;
    }
    mevr::voice::llm::Runner runner;
    mevr::voice::llm::Settings settings;
    settings.threads = threads;
    std::string error;
    if (!runner.Load(lib, model, settings, &error)) {
        std::fprintf(stderr, "load: %s\n", error.c_str());
        return 1;
    }
    std::printf("loaded %s in %u ms, %d threads\n", runner.Description().c_str(), runner.LoadMs(), threads);
    std::ifstream in(data);
    std::string line, prompt, answer;
    int n = 0, hits = 0, parsed = 0, shown = 0;
    std::vector<unsigned> prefills, decodes;
    std::string currentPrefix;
    while (std::getline(in, line) && (limit <= 0 || n < limit)) {
        if (!Field(line, "prompt", prompt) || !Field(line, "answer", answer)) continue;
        // The prefix ends with the WORDS line (core/vr/SquadContext's Prefix); the block follows.
        std::size_t const cut = prompt.find("\nWORDS ");
        std::size_t const blockAt = cut == std::string::npos ? 0 : prompt.find('\n', cut + 1) + 1;
        std::string const prefix = prompt.substr(0, blockAt);
        std::string const rest = prompt.substr(blockAt);
        if (prefix != currentPrefix) {
            if (!runner.SetPrefix(prefix, &error)) {
                std::fprintf(stderr, "prefix: %s\n", error.c_str());
                return 1;
            }
            currentPrefix = prefix;
        }
        std::string const gbnf = grammar ? mevr::voice::grammar::GbnfFromBlock(rest) : std::string();
        mevr::voice::llm::Answer const got = runner.Ask(rest, gbnf);
        ++n;
        if (!got.ok) {
            std::printf("#%d: %s\n", n, got.error.c_str());
            continue;
        }
        ++parsed;
        prefills.push_back(got.prefillMs);
        decodes.push_back(got.decodeMs);
        bool const hit = Trimmed(got.text) == Trimmed(answer);
        hits += hit;
        if (!hit && shown < show) {
            ++shown;
            std::size_t const say = rest.find("SAY ");
            std::string const said = say == std::string::npos ? "" : rest.substr(say + 4, rest.find('\n', say) - say - 4);
            std::printf("  \"%s\": gold %s, model %s (%u + %u ms, %d tokens)\n", said.c_str(), answer.c_str(), got.text.c_str(), got.prefillMs, got.decodeMs, got.answerTokens);
        }
    }
    if (prefills.empty()) {
        std::printf("no answers\n");
        return 1;
    }
    std::sort(prefills.begin(), prefills.end());
    std::sort(decodes.begin(), decodes.end());
    auto p = [](std::vector<unsigned> const& v, double q) { return v[std::min(v.size() - 1, static_cast<std::size_t>(q * static_cast<double>(v.size())))]; };
    std::printf("%d prompts: %d exact (%.1f%%), %s; scene prefill median %u ms, p95 %u ms; answer decode median %u ms, p95 %u ms; total median %u ms\n", n, hits,
        100.0 * hits / std::max(n, 1), grammar ? "grammar on" : "grammar off", p(prefills, 0.5), p(prefills, 0.95), p(decodes, 0.5), p(decodes, 0.95),
        p(prefills, 0.5) + p(decodes, 0.5));
    return 0;
}

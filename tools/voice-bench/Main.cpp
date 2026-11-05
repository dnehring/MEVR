// SPDX-License-Identifier: MIT
// mevr-voice-bench (research/54 step 2): Moonshine Voice's streaming models over WAV files, on the CPU, timed the way the
// voice orders use them: the audio fed 100 ms at a time (in real time with --realtime), a partial asked for every
// --interval ms, the stop after the last chunk, then the final transcript. Reports each file's text, the time from the end
// of the audio to the final text (the number the budget cares about), the partials' costs and the load time. Also runs the
// non-streaming call for comparison with --nonstream.
//
//   mevr-voice-bench --models <dir> [--arch tiny|small|medium] [--keyterms a,b,c] [--interval 150] [--realtime]
//                    [--nonstream] [--words] file.wav...
#include "core/voice/Wav.hpp"
#include "moonshine-c-api.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int Arch(std::string const& name) {
    if (name == "tiny") return MOONSHINE_MODEL_ARCH_TINY_STREAMING;
    if (name == "small") return MOONSHINE_MODEL_ARCH_SMALL_STREAMING;
    if (name == "medium") return MOONSHINE_MODEL_ARCH_MEDIUM_STREAMING;
    if (name == "base") return MOONSHINE_MODEL_ARCH_BASE_STREAMING;
    return -1;
}

std::string Join(transcript_t const* t) {
    std::string out;
    if (t == nullptr) return out;
    for (std::uint64_t i = 0; i < t->line_count; ++i) {
        if (t->lines[i].text == nullptr || t->lines[i].text[0] == '\0') continue;
        if (!out.empty()) out += " | ";
        out += t->lines[i].text;
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::string models, arch = "tiny", keyterms, boost;
    int interval = 150;
    bool realtime = false, nonstream = false, words = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        std::string const a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--models") models = next();
        else if (a == "--arch") arch = next();
        else if (a == "--keyterms") keyterms = next();
        else if (a == "--boost") boost = next();  // keyterm_boost (the library's default 2.0)
        else if (a == "--interval") interval = std::max(30, std::atoi(next().c_str()));
        else if (a == "--realtime") realtime = true;
        else if (a == "--nonstream") nonstream = true;
        else if (a == "--words") words = true;
        else files.push_back(a);
    }
    if (models.empty() || files.empty() || Arch(arch) < 0) {
        std::fprintf(stderr, "usage: mevr-voice-bench --models <dir> [--arch tiny|small|medium] [--keyterms a,b] [--interval ms] [--realtime] [--nonstream] [--words] file.wav...\n");
        return 2;
    }
    std::vector<moonshine_option_t> options;
    char const* timestamps = words ? "true" : "false";
    options.push_back({"word_timestamps", timestamps});
    if (!keyterms.empty()) options.push_back({"keyterms", keyterms.c_str()});
    if (!boost.empty()) options.push_back({"keyterm_boost", boost.c_str()});
    auto const t0 = Clock::now();
    std::int32_t const th = moonshine_load_transcriber_from_files(models.c_str(), static_cast<std::uint32_t>(Arch(arch)), options.data(), options.size(), MOONSHINE_HEADER_VERSION);
    if (th < 0) {
        std::fprintf(stderr, "the models didn't load from %s: %s\n", models.c_str(), moonshine_error_to_string(th));
        return 1;
    }
    std::printf("loaded %s (%s) in %.0f ms; library version %d%s\n", arch.c_str(), models.c_str(), Ms(t0, Clock::now()), moonshine_get_version(),
        keyterms.empty() ? "" : ("; key terms " + keyterms).c_str());
    std::vector<double> finals;
    for (std::string const& file : files) {
        std::string why;
        auto const audio = mevr::voice::wav::Read(file, &why);
        if (!audio) {
            std::printf("%s: can't read: %s\n", file.c_str(), why.c_str());
            continue;
        }
        if (nonstream) {
            auto const n0 = Clock::now();
            transcript_t* t = nullptr;
            std::vector<float> samples = audio->mono;
            std::int32_t const err = moonshine_transcribe_without_streaming(th, samples.data(), samples.size(), audio->sampleRate, 0, &t);
            std::printf("%s (%.2f s): non-streaming %.0f ms -> \"%s\"%s\n", file.c_str(), audio->Seconds(), Ms(n0, Clock::now()), err == 0 ? Join(t).c_str() : "",
                err == 0 ? "" : moonshine_error_to_string(err));
        }
        std::int32_t const stream = moonshine_create_stream(th, 0);
        if (stream < 0 || moonshine_start_stream(th, stream) != 0) {
            std::printf("%s: can't start a stream\n", file.c_str());
            continue;
        }
        std::size_t const chunk = static_cast<std::size_t>(audio->sampleRate / 10);
        auto const s0 = Clock::now();
        Clock::time_point lastPartial = s0;
        std::vector<double> partialMs;
        std::string lastText;
        double firstTextMs = -1.0;
        for (std::size_t at = 0; at < audio->mono.size(); at += chunk) {
            std::size_t const n = std::min(chunk, audio->mono.size() - at);
            moonshine_transcribe_add_audio_to_stream(th, stream, audio->mono.data() + at, n, audio->sampleRate, 0);
            if (realtime) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (Ms(lastPartial, Clock::now()) >= interval || !realtime) {
                auto const p0 = Clock::now();
                transcript_t* t = nullptr;
                if (moonshine_transcribe_stream(th, stream, 0, &t) == 0) {
                    partialMs.push_back(Ms(p0, Clock::now()));
                    std::string const text = Join(t);
                    if (!text.empty() && firstTextMs < 0.0) firstTextMs = Ms(s0, Clock::now());
                    lastText = text;
                }
                lastPartial = Clock::now();
            }
        }
        auto const end = Clock::now();  // the end of the audio
        moonshine_stop_stream(th, stream);
        transcript_t* t = nullptr;
        std::int32_t const err = moonshine_transcribe_stream(th, stream, 0, &t);
        double const finalMs = Ms(end, Clock::now());
        finals.push_back(finalMs);
        double worst = 0.0, sum = 0.0;
        for (double const m : partialMs) {
            worst = std::max(worst, m);
            sum += m;
        }
        std::printf("%s (%.2f s): final %.0f ms after the audio's end -> \"%s\"; %zu partials, mean %.0f ms, worst %.0f ms%s%s\n", file.c_str(), audio->Seconds(),
            finalMs, err == 0 ? Join(t).c_str() : moonshine_error_to_string(err), partialMs.size(), partialMs.empty() ? 0.0 : sum / static_cast<double>(partialMs.size()), worst,
            firstTextMs >= 0.0 ? (" ; first text at " + std::to_string(static_cast<int>(firstTextMs)) + " ms").c_str() : "",
            lastText.empty() ? "" : (" ; last partial \"" + lastText + "\"").c_str());
        if (words && err == 0 && t != nullptr) {
            for (std::uint64_t i = 0; i < t->line_count; ++i) {
                for (std::uint64_t w = 0; w < t->lines[i].word_count; ++w) {
                    transcript_word_t const& word = t->lines[i].words[w];
                    std::printf("    %-16s %.2f-%.2f s  %.2f\n", word.text, word.start, word.end, word.confidence);
                }
            }
        }
        moonshine_free_stream(th, stream);
    }
    if (!finals.empty()) {
        std::sort(finals.begin(), finals.end());
        std::printf("final after the audio's end: median %.0f ms, p95 %.0f ms over %zu files\n", finals[finals.size() / 2], finals[std::min(finals.size() - 1, finals.size() * 95 / 100)],
            finals.size());
    }
    moonshine_free_transcriber(th);
    return 0;
}

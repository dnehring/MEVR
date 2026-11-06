// SPDX-License-Identifier: MIT
#include "install/Voice.hpp"

#include "core/Json.hpp"
#include "install/Detect.hpp"
#include "install/Packs.hpp"
#include "install/Updates.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <system_error>

namespace mevr::install::voice {
namespace {

namespace fs = std::filesystem;

constexpr char kMoonshineUrl[] = "https://github.com/moonshine-ai/moonshine/releases/download/v0.1.5/moonshine-voice-windows-x86_64.tar.gz";
constexpr char kModelsUrl[] = "https://huggingface.co/moonshine-ai/moonshine-voice-assets/resolve/main/model/";
constexpr char kLlamaUrl[] = "https://github.com/ggml-org/llama.cpp/releases/download/b12128/llama-b12128-bin-win-cpu-x64.zip";

struct Pinned {
    char const* name;
    std::uint64_t size;
    char const* sha256;
};

// tools/fetch-voice.ps1's eight files per streaming model, as Hugging Face served them on 2026-11-05.
constexpr Pinned kTiny[] = {
    {"streaming_config.json", 509ull, "74fe5ddebd63b17caf59e8a3b18c17547ff7bce1642050edbb1c3962674f8950"},
    {"frontend.model.ort", 23344ull, "5121b561417b638afce0c6c31b760e37c93cf97f80d9b0031aad1fe7b6f25d61"},
    {"frontend.weights.ort", 2093464ull, "217da24ac6f522ebf02da8ef288e77d1ac68d50d4a6821433182e4fbf4204bbd"},
    {"encoder.ort", 7675440ull, "a8414e1a5dedf9f2093d7680601dd8a9b0433e7020260eafe0e370ead91134ca"},
    {"adapter.ort", 1319664ull, "22ecc949e146c49667fda28d102d4e30749a107dc88a396292aa8f277ef1347c"},
    {"cross_kv.ort", 1287544ull, "143a36667b8d05fd9d04e8c337b7ee121f37ef299aea6b3d82bdb3d3401950b4"},
    {"decoder_kv.ort", 32583720ull, "8852553f312adb6c9aa4d17418015049b30f412209ee569d336548c0044627de"},
    {"tokenizer.bin", 249974ull, "6884b35fd6377d4c4d32336a0bc152f36b64d1e45b6503683cdc238250a8472d"},
};
constexpr Pinned kSmall[] = {
    {"streaming_config.json", 512ull, "26f02b6afb22d60871a5efd85c3d38e569cc0ddb6c5eb6e93d3260152ae8a47a"},
    {"frontend.model.ort", 26944ull, "09b1210ae30dc5f0f3e45f0ebab914c254741323114f53fbbe5ae62cca35058f"},
    {"frontend.weights.ort", 7769464ull, "7ef97521bd4bad3928f5bb6808586f4fcc6e92bd5990394112eed7d4052ec338"},
    {"encoder.ort", 44148576ull, "2d4d973e91e8aca08c51e7e7efa28a46ab265b63d809d5294d18b86bcd85b993"},
    {"adapter.ort", 2870368ull, "c665f742364febad597cc9ac1e0b341ffbee0e24a1466e2f3bde95e6e4771762"},
    {"cross_kv.ort", 5356536ull, "e2d3417144e9514055ebfefe8dcc4c0a55a55adcb8530435844c75c53e352bf6"},
    {"decoder_kv.ort", 81878600ull, "1a05465b1dd955858dfcbee039c0020fb5dd982b0f5094c34e61735d518d771b"},
    {"tokenizer.bin", 249974ull, "6884b35fd6377d4c4d32336a0bc152f36b64d1e45b6503683cdc238250a8472d"},
};
constexpr Pinned kMedium[] = {
    {"streaming_config.json", 513ull, "28e83b7a28e91472692a035e0dae3116422ae43aeb2bef5ed822c44ce89b88af"},
    {"frontend.model.ort", 28720ull, "95768855c70c8251eeecc05fedf69999da1b8ab16f605c9f457fd3354b0ad6b5"},
    {"frontend.weights.ort", 11889560ull, "5ac941f490cbe035b335b99a414cc393d62d4c6f9f2423495b286870d271d709"},
    {"encoder.ort", 94705376ull, "12915e76ebac7dd287c5ea63965d06103a53ba1ce242a4a34f318f3958c60c37"},
    {"adapter.ort", 3651296ull, "3f2a287def57cc094367a0eec3c4f5fc36a32ec420e86764b696920991b20281"},
    {"cross_kv.ort", 11643776ull, "642f6e21cd305be79342207c6f9e6b681d469d55bc48c72b27b84846fb71fd1e"},
    {"decoder_kv.ort", 146972408ull, "193bb366492b74fc4ad338c6778e8d8eb916aaa11b5aa264f9057f4db7759486"},
    {"tokenizer.bin", 249974ull, "6884b35fd6377d4c4d32336a0bc152f36b64d1e45b6503683cdc238250a8472d"},
};
// What plugin/VoiceModel needs from llama.cpp's CPU build: llama.dll, what it imports (ggml, ggml-base, which imports
// libomp) and the CPU backends ggml_backend_load_all_from_path picks from; not its tools, mtmd or the RPC backend.
constexpr Pinned kLlama[] = {
    {"llama.dll", 3835392ull, ""},
    {"ggml.dll", 78848ull, ""},
    {"ggml-base.dll", 832512ull, ""},
    {"libomp.dll", 768000ull, ""},
    {"ggml-cpu-alderlake.dll", 1442816ull, ""},
    {"ggml-cpu-cannonlake.dll", 1638400ull, ""},
    {"ggml-cpu-cascadelake.dll", 1611776ull, ""},
    {"ggml-cpu-cooperlake.dll", 1621504ull, ""},
    {"ggml-cpu-haswell.dll", 1447424ull, ""},
    {"ggml-cpu-icelake.dll", 1618432ull, ""},
    {"ggml-cpu-ivybridge.dll", 1328640ull, ""},
    {"ggml-cpu-piledriver.dll", 1321472ull, ""},
    {"ggml-cpu-sandybridge.dll", 1319424ull, ""},
    {"ggml-cpu-sapphirerapids.dll", 1897472ull, ""},
    {"ggml-cpu-skylakex.dll", 1631744ull, ""},
    {"ggml-cpu-sse42.dll", 944128ull, ""},
    {"ggml-cpu-x64.dll", 936448ull, ""},
    {"ggml-cpu-zen4.dll", 1627648ull, ""},
    {"LICENSE-LLVM-OpenMP", 19741ull, ""},
};

template <std::size_t N>
Part ModelsPart(char const* size, Pinned const (&files)[N], std::string name, std::string description) {
    Part p;
    p.id = std::string("models-") + size;
    p.name = std::move(name);
    p.description = std::move(description);
    p.origin = "Hugging Face (moonshine-ai/moonshine-voice-assets)";
    p.licence = "MIT (Moonshine AI)";
    for (Pinned const& f : files) {
        Source s;
        s.url = std::string(kModelsUrl) + size + "-streaming-en/quantized_26_08_21/" + f.name;
        s.size = f.size;
        s.sha256 = f.sha256;
        s.files.push_back({"", std::string(kModelsDir) + "/" + size + "-streaming-en/" + f.name, f.size, f.sha256});
        p.sources.push_back(std::move(s));
    }
    return p;
}

std::string Lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool PlainName(std::string_view name) {
    return !name.empty() && name.find_first_of("\\/:*?\"<>|") == std::string_view::npos && name.find("..") == std::string_view::npos;
}

bool HexSha(std::string_view s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

Outcome Finish(Transaction const& tx, bool ok) {
    Outcome out;
    out.ok = ok && tx.Errors().empty();
    out.log = tx.Log();
    out.errors = tx.Errors();
    return out;
}

Outcome Error(std::string message) {
    Outcome out;
    out.errors.push_back(std::move(message));
    return out;
}

// A file name for the cache from a download's URL (its last segment), prefixed with the part so two parts' files
// (every model has an encoder.ort) never meet.
fs::path CacheFile(std::string_view partId, std::string_view url) {
    std::string_view name = url.substr(url.rfind('/') + 1);
    return DownloadDir() / detect::Wide(std::string(partId) + "-" + std::string(name));
}

bool Present(Layout const& layout, FileSpec const& f) {
    std::error_code ec;
    std::uintmax_t const size = fs::file_size(layout.Full(f.target), ec);
    return !ec && (f.size == 0 || size == f.size);
}

} // namespace

std::vector<Part> const& Catalogue() {
    static std::vector<Part> const kParts = [] {
        std::vector<Part> parts;
        Part recognizer;
        recognizer.id = "recognizer";
        recognizer.name = "Speech recognizer runtime";
        recognizer.description = "ONNX Runtime, which Moonshine Voice's recognizer runs on (taken out of Moonshine Voice's Windows release)";
        recognizer.origin = "GitHub (moonshine-ai/moonshine, release v0.1.5)";
        recognizer.licence = "MIT (Moonshine AI; ONNX Runtime: Microsoft)";
        Source moonshine;
        moonshine.url = kMoonshineUrl;
        moonshine.size = 26109939ull;
        moonshine.sha256 = "97c1987e8e1cd77bb5fe3b12ce5aad5172637107e1dfda112ea9b21bac8f4b65";
        moonshine.archive = Archive::TarGz;
        moonshine.files.push_back({"moonshine-voice-windows-x86_64/lib/onnxruntime.dll", kOnnxRuntime, 14186016ull,
            "dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea"});
        recognizer.sources.push_back(std::move(moonshine));
        parts.push_back(std::move(recognizer));
        parts.push_back(ModelsPart("tiny", kTiny, "Recognizer model (tiny)",
            "Moonshine's tiny streaming English model: the one the mod uses ([Voice] model = tiny), the fastest beside the game"));
        parts.push_back(ModelsPart("small", kSmall, "Recognizer model (small)",
            "More accurate, slower partials beside the game; used with [Voice] model = small in LE1MEVR.ini"));
        parts.push_back(ModelsPart("medium", kMedium, "Recognizer model (medium)",
            "The most accurate, the slowest; used with [Voice] model = medium in LE1MEVR.ini"));
        Part runtime;
        runtime.id = "llm-runtime";
        runtime.name = "Model tier runtime";
        runtime.description = "llama.cpp's CPU build, which runs the fine-tuned model";
        runtime.origin = "GitHub (ggml-org/llama.cpp, release b12128)";
        runtime.licence = "MIT (the ggml authors; LLVM OpenMP: Apache-2.0 with LLVM exceptions)";
        Source llama;
        llama.url = kLlamaUrl;
        llama.size = 24730477ull;
        llama.sha256 = "2864bc950fe1681f1867ebc1bf0d6a66d18cc5561b29c285b4504787145bd7b7";
        llama.archive = Archive::Zip;
        for (Pinned const& f : kLlama) {
            llama.files.push_back({f.name, std::string(kLlmDir) + "/" + f.name, f.size, f.sha256});
        }
        runtime.sources.push_back(std::move(llama));
        parts.push_back(std::move(runtime));
        Part model;
        model.id = "llm-model";
        model.name = "Fine-tuned order model";
        model.description = "Qwen3-0.6B fine-tuned on squad orders: it reads the wordings the rules aren't sure of";
        model.origin = "GitHub (this mod's release " + std::string(kModelTag) + ")";
        model.licence = "Apache-2.0 (Qwen3-0.6B by the Qwen team; our fine-tune under the same terms)";
        model.modelSize = 639446880ull;
        parts.push_back(std::move(model));
        return parts;
    }();
    return kParts;
}

Part const* Find(std::string_view id) {
    for (Part const& p : Catalogue()) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

std::string ContentTag(std::string_view partId) {
    return std::string(kContent) + "/" + std::string(partId);
}

std::vector<std::string> PartsFor(Choices const& c) {
    std::vector<std::string> parts{"recognizer", "models-tiny"};
    if (c.smallModels) {
        parts.push_back("models-small");
    }
    if (c.mediumModels) {
        parts.push_back("models-medium");
    }
    if (c.llm) {
        parts.push_back("llm-runtime");
        parts.push_back("llm-model");
    }
    return parts;
}

Sizes SizesFor(std::vector<std::string> const& parts, std::uint64_t modelSize) {
    Sizes s;
    for (std::string const& id : parts) {
        Part const* p = Find(id);
        if (p == nullptr) {
            continue;
        }
        if (p->id == "llm-model") {
            std::uint64_t const size = modelSize != 0 ? modelSize : p->modelSize;
            s.download += size;
            s.disk += size;
            continue;
        }
        for (Source const& src : p->sources) {
            s.download += src.size;
            for (FileSpec const& f : src.files) {
                s.disk += f.size;
            }
        }
    }
    return s;
}

// ---- The model's release ---------------------------------------------------------------------------------------------

ModelSource ReadModelSource(fs::path const& setupIni) {
    auto read = [&](wchar_t const* section, wchar_t const* key) {
        wchar_t buffer[1024]{};
        GetPrivateProfileStringW(section, key, L"", buffer, 1024, setupIni.c_str());
        std::string text = detect::Utf8(buffer);
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
            text.pop_back();
        }
        return text;
    };
    ModelSource m;
    m.repo = read(L"Voice", L"modelRepo");
    if (m.repo.empty()) {
        m.repo = read(L"Update", L"repo");
    }
    if (m.repo.empty()) {
        m.repo = updates::kDefaultRepo;
    }
    if (std::string const tag = read(L"Voice", L"modelTag"); !tag.empty()) {
        m.tag = tag;
    }
    if (std::string const asset = read(L"Voice", L"modelAsset"); !asset.empty()) {
        m.asset = asset;
    }
    m.url = read(L"Voice", L"modelUrl");
    m.sha256 = Lower(read(L"Voice", L"modelSha256"));
    if (!HexSha(m.sha256)) {
        m.sha256.clear();
    }
    std::string const size = read(L"Voice", L"modelSize");
    m.size = size.empty() || size.find_first_not_of("0123456789") != std::string::npos ? 0 : std::stoull(size);
    return m;
}

std::string ReleaseByTagApi(std::string_view repoUrl, std::string_view tag) {
    std::string api = updates::LatestReleaseApi(repoUrl);
    constexpr std::string_view kLatest = "/releases/latest";
    bool const plainTag = !tag.empty() && tag.size() <= 100 &&
        std::all_of(tag.begin(), tag.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.'; });
    if (api.empty() || !plainTag || api.size() < kLatest.size()) {
        return {};
    }
    api.resize(api.size() - kLatest.size());
    return api + "/releases/tags/" + std::string(tag);
}

std::optional<Source> ModelFromRelease(std::string_view releaseJson, std::string_view assetName, std::string* why) {
    auto say = [why](std::string text) {
        if (why != nullptr) {
            *why = std::move(text);
        }
        return std::nullopt;
    };
    auto const release = json::Parse(releaseJson);
    if (!release || !release->IsObject()) {
        return say("GitHub's answer wasn't a release");
    }
    for (json::Value const& asset : release->Get("assets").Items()) {
        if (asset.String("name") != assetName) {
            continue;
        }
        Source s;
        s.url = asset.String("browser_download_url");
        s.size = static_cast<std::uint64_t>(asset.Number("size", 0));
        if (s.url.rfind("https://github.com/", 0) != 0) {
            return say("its download isn't on github.com");
        }
        std::string const digest = Lower(asset.String("digest"));
        if (digest.size() == 71 && digest.rfind("sha256:", 0) == 0 && HexSha(std::string_view(digest).substr(7))) {
            s.sha256 = digest.substr(7);
        }
        s.files.push_back({"", kLlmModel, s.size, s.sha256});
        return s;
    }
    std::string const tag = release->String("tag_name");
    return say("the release" + (tag.empty() ? std::string() : " " + tag) + " has no file called " + std::string(assetName) + " yet");
}

ModelLookup FindModel(ModelSource const& from) {
    ModelLookup out;
    if (!from.url.empty()) {
        if (from.url.rfind("https://", 0) != 0) {
            out.error = "[Voice] modelUrl in MEVR-Setup.ini isn't an https:// address";
            return out;
        }
        Source s;
        s.url = from.url;
        s.size = from.size;
        s.sha256 = from.sha256;
        s.files.push_back({"", kLlmModel, from.size, from.sha256});
        out.source = std::move(s);
        return out;
    }
    std::string const api = ReleaseByTagApi(from.repo, from.tag);
    if (api.empty() || !PlainName(from.asset)) {
        out.error = "MEVR-Setup.ini's [Voice] modelRepo, modelTag or modelAsset isn't a GitHub repository page, a release tag and a file name";
        return out;
    }
    http::Response const r = http::Get(api);
    if (r.status == 0) {
        out.error = "Couldn't reach GitHub: " + r.error;
    } else if (r.rateLimited) {
        out.error = "GitHub's limit for requests without an account is reached for now (60 an hour): try again later";
    } else if (r.status == 404) {
        out.notPublished = true;
        out.error = "The fine-tuned model isn't published yet (no release " + from.tag + " at " + from.repo + ")";
    } else if (r.status != 200) {
        out.error = "GitHub answered HTTP " + std::to_string(r.status);
    } else if (!r.error.empty()) {
        out.error = "Couldn't read GitHub's answer: " + r.error;
    } else {
        std::string why;
        out.source = ModelFromRelease(r.body, from.asset, &why);
        if (!out.source) {
            out.notPublished = why.find("has no file") != std::string::npos;
            out.error = (out.notPublished ? "The fine-tuned model isn't published yet: " : "The fine-tuned model's download wasn't found: ") + why;
        }
    }
    return out;
}

// ---- What's installed ------------------------------------------------------------------------------------------------

bool State::Has(std::string_view part) const {
    return std::find(parts.begin(), parts.end(), part) != parts.end();
}

State ReadState(Layout const& layout) {
    State s;
    std::error_code ec;
    s.dll = fs::exists(layout.Full(kDll), ec);
    Transaction tx(layout);
    if (!tx.Load() || !tx.Exists()) {
        return s;
    }
    std::string const prefix = std::string(kContent) + "/";
    for (FileEntry const& f : tx.Data().files) {
        if (f.content.rfind(prefix, 0) == 0) {
            std::string const part = f.content.substr(prefix.size());
            if (!s.Has(part)) {
                s.parts.push_back(part);
            }
        }
    }
    s.damaged = tx.VerifyContent(kContent, false);
    s.recorded = std::any_of(tx.Data().packs.begin(), tx.Data().packs.end(), [](PackRecord const& p) { return p.id == kContent; });
    return s;
}

fs::path DownloadDir() {
    fs::path const local = LocalDataDir();
    return local.empty() ? fs::path() : local / L"downloads" / L"voice";
}

namespace {

// Takes `files` out of `archive` into `dir` with Windows' tar.exe (bsdtar reads gzip and zip). "" or an error.
std::string Unpack(fs::path const& archive, fs::path const& dir, std::vector<FileSpec> const& files, std::atomic<bool> const* cancel) {
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    fs::path const tar = fs::path(system) / L"tar.exe";
    std::error_code ec;
    if (!fs::exists(tar, ec)) {
        return "Windows' tar.exe isn't on this PC (Windows 10 1803 or later has it): can't unpack " + detect::Utf8(archive.filename().wstring());
    }
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    std::wstring command = packs::QuoteArg(tar.wstring()) + L" -xf " + packs::QuoteArg(archive.wstring()) + L" -C " + packs::QuoteArg(dir.wstring());
    for (FileSpec const& f : files) {
        command += L" " + packs::QuoteArg(detect::Wide(f.member));
    }
    std::string lastLine;
    int const code = packs::RunHidden(command, dir, [&](std::string_view text) {
        if (!text.empty()) {
            lastLine = std::string(text);
        }
    }, cancel);
    if (code != 0) {
        return std::format("tar.exe couldn't unpack {} (exit code {}{}{})", detect::Utf8(archive.filename().wstring()), code,
            lastLine.empty() ? "" : ": ", lastLine);
    }
    return {};
}

struct Job {
    std::string part;
    Source source;
};

} // namespace

Outcome Install(Layout const& layout, std::optional<Payload> const& payload, Choices const& choices, ModelSource const& model,
    std::function<void(Step const&)> const& progress, std::atomic<bool> const* cancel) {
    if (!detect::IsGameRoot(layout.root)) {
        return Error("Mass Effect Legendary Edition isn't in " + detect::Utf8(layout.root.wstring()));
    }
    if (detect::GameRunningIn(layout.root)) {
        return Error("Mass Effect (or its launcher) is running: close it first (the game holds the voice files open)");
    }
    if (!detect::CanWrite(layout.Win64())) {
        return Error("Can't write to the game's folder: run the setup as administrator");
    }
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    auto step = [&](std::string text, int percent) {
        if (progress) {
            progress({std::move(text), percent});
        }
    };
    std::error_code ec;
    // Our recognizer's DLL: the release's install put it there; an older install gets it from this release's payload.
    if (!fs::exists(layout.Full(kDll), ec)) {
        fs::path const fromPayload = payload ? payload->dir / L"voice" / L"MEVR-Voice.dll" : fs::path();
        if (fromPayload.empty() || !fs::exists(fromPayload, ec)) {
            return Error("MEVR-Voice.dll isn't in the game's ASI\\voice folder: install (or Repair) this release of the mod with the "
                         "MEVR-Setup.exe from its download first");
        }
        if (!tx.CopyIn(fromPayload, kDll)) {
            return Finish(tx, false);
        }
    }
    std::vector<std::string> wanted = PartsFor(choices);
    // The files already in place, each against its SHA-256 (a few seconds for the model): one changed without changing its
    // size is fetched again too, as Check found it.
    step("Checking the voice files already in place", 0);
    std::vector<std::string> const damaged = tx.VerifyContent(kContent, true);
    auto intact = [&](std::string const& target, std::string const& part) {
        FileEntry const* e = tx.Data().Find(target);
        return e != nullptr && e->content == ContentTag(part) && std::find(damaged.begin(), damaged.end(), target) == damaged.end();
    };
    // The model first: if it isn't published yet, its runtime isn't worth fetching either.
    std::optional<Source> modelSource;
    if (std::find(wanted.begin(), wanted.end(), "llm-model") != wanted.end()) {
        if (!intact(kLlmModel, "llm-model")) {
            step("Looking for the fine-tuned model on GitHub", 0);
            ModelLookup const found = FindModel(model);
            if (!found.source) {
                if (!found.notPublished) {
                    return Error(found.error);
                }
                tx.Note(found.error + ": installing the rest without the model tier (the rule parser alone reads the orders; the "
                                      "setup gets it once it's published)");
                std::erase(wanted, std::string("llm-model"));
                std::erase(wanted, std::string("llm-runtime"));
            } else {
                modelSource = found.source;
            }
        }
    }
    // What has to be fetched: every source of a wanted part with a file missing, another size or another SHA-256.
    std::vector<Job> jobs;
    for (std::string const& id : wanted) {
        Part const* part = Find(id);
        std::vector<Source> sources = part->sources;
        if (id == "llm-model" && modelSource) {
            sources = {*modelSource};
        }
        for (Source const& src : sources) {
            bool const complete = std::all_of(src.files.begin(), src.files.end(), [&](FileSpec const& f) {
                return intact(f.target, id) && Present(layout, f);
            });
            if (!complete) {
                jobs.push_back({id, src});
            }
        }
    }
    std::uint64_t total = 0;
    for (Job const& j : jobs) {
        total += j.source.size;
    }
    std::uint64_t done = 0;
    fs::create_directories(DownloadDir(), ec);
    bool ok = true;
    for (Job const& job : jobs) {
        if (cancel != nullptr && cancel->load()) {
            tx.Fail("stopped: what's downloaded so far stays installed; Install again fetches the rest");
            ok = false;
            break;
        }
        Part const* part = Find(job.part);
        fs::path const file = CacheFile(job.part, job.source.url);
        std::string const name = detect::Utf8(file.filename().wstring());
        // A finished download from an earlier try that stopped before placing it.
        bool cached = false;
        if (job.source.size != 0 && fs::file_size(file, ec) == job.source.size && !ec) {
            cached = job.source.sha256.empty() || Sha256File(file) == job.source.sha256;
        }
        if (!cached) {
            std::uint64_t const before = done;
            std::string const error = http::Download(job.source.url, file, [&](http::Progress const& p) {
                std::uint64_t const now = before + p.received;
                int const percent = total == 0 ? 0 : static_cast<int>(std::min<std::uint64_t>(99, now * 100 / total));
                step(std::format("Downloading {}: {} of {}", part->name, http::SizeText(p.received),
                         http::SizeText(p.total != 0 ? p.total : job.source.size)), percent);
            }, cancel, http::Expect{job.source.size, job.source.sha256});
            if (!error.empty()) {
                tx.Fail(part->name + ": " + error);
                ok = false;
                break;
            }
        }
        done += job.source.size;
        std::string const sha = job.source.sha256.empty() ? Sha256File(file) : job.source.sha256;
        tx.Note(std::format("{} {} ({}{})", cached ? "found the download" : "downloaded", name, http::SizeText(fs::file_size(file, ec)),
            job.source.sha256.empty() ? ", no sum to check it against" : ", its SHA-256 checked"));
        if (job.source.archive == Archive::None) {
            ok = tx.PlaceIn(file, job.source.files.front().target, sha, ContentTag(job.part));
        } else {
            step("Unpacking " + name, total == 0 ? 0 : static_cast<int>(std::min<std::uint64_t>(99, done * 100 / total)));
            fs::path const dir = DownloadDir() / detect::Wide("unpack-" + job.part);
            std::string const error = Unpack(file, dir, job.source.files, cancel);
            if (!error.empty()) {
                tx.Fail(error);
                ok = false;
                break;
            }
            for (FileSpec const& f : job.source.files) {
                fs::path const out = dir / fs::path(detect::Wide(f.member)).make_preferred();
                std::uintmax_t const size = fs::file_size(out, ec);
                if (ec || (f.size != 0 && size != f.size)) {
                    tx.Fail(std::format("{} in {} isn't the expected file (size {})", f.member, name, ec ? std::string("missing") : std::to_string(size)));
                    ok = false;
                    break;
                }
                std::string const fileSha = Sha256File(out);
                if (!f.sha256.empty() && fileSha != f.sha256) {
                    tx.Fail(f.member + " in " + name + " isn't the expected file (another SHA-256)");
                    ok = false;
                    break;
                }
                if (!tx.PlaceIn(out, f.target, fileSha, ContentTag(job.part))) {
                    ok = false;
                    break;
                }
            }
            fs::remove_all(dir, ec);
            if (ok) {
                fs::remove(file, ec);
            }
        }
        if (!ok) {
            break;
        }
    }
    // Installed parts no longer chosen come out (the model tier unticked, a larger recognizer dropped).
    if (ok) {
        State const now = ReadState(layout);
        std::vector<std::string> const chosen = PartsFor(choices);
        for (std::string const& part : now.parts) {
            if (std::find(chosen.begin(), chosen.end(), part) == chosen.end()) {
                Part const* p = Find(part);
                tx.Note("taking out " + (p != nullptr ? p->name : part) + " (not chosen)");
                ok = tx.RemoveContent(ContentTag(part)) && ok;
            }
        }
    }
    if (ok) {
        auto& list = tx.Data().packs;
        std::erase_if(list, [](PackRecord const& p) { return p.id == kContent; });
        list.push_back({kContent, kName, "download", NowIso()});
        ok = tx.Save();
        step("Voice orders are installed", 100);
        tx.Note("voice orders are installed: in game, hold the left grip at your left ear, speak, let go");
    }
    // The download folders, once empty (a stopped download's part file keeps them).
    fs::remove(DownloadDir(), ec);
    fs::remove(DownloadDir().parent_path(), ec);
    return Finish(tx, ok);
}

Outcome Remove(Layout const& layout) {
    if (detect::GameRunningIn(layout.root)) {
        return Error("Mass Effect (or its launcher) is running: close it first (the game holds the voice files open)");
    }
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    if (!tx.Exists()) {
        return Error("Nothing to remove: no install record (MEVR-install.json) in the game's ASI folder");
    }
    bool ok = tx.RemoveContent(kContent);
    if (ok) {
        std::erase_if(tx.Data().packs, [](PackRecord const& p) { return p.id == kContent; });
        ok = tx.Save();
        tx.Note("voice orders are taken out (MEVR-Voice.dll stays with the mod: without its models the mod logs \"voice off\")");
    }
    return Finish(tx, ok);
}

Outcome Check(Layout const& layout, bool deep) {
    Transaction tx(layout);
    if (!tx.Load()) {
        return Finish(tx, false);
    }
    std::size_t files = 0;
    for (FileEntry const& f : tx.Data().files) {
        files += f.content.rfind(std::string(kContent) + "/", 0) == 0 ? 1 : 0;
    }
    if (files == 0) {
        tx.Note("no voice files on record");
        return Finish(tx, true);
    }
    std::vector<std::string> const bad = tx.VerifyContent(kContent, deep);
    for (std::string const& path : bad) {
        tx.Fail("missing or changed: " + path);
    }
    if (bad.empty()) {
        tx.Note(std::format("all {} voice files are as installed ({})", files, deep ? "SHA-256 checked" : "sizes checked"));
    } else {
        tx.Note("Install on the Optional content page fetches them again");
    }
    return Finish(tx, bad.empty());
}

} // namespace mevr::install::voice

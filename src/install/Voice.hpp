// SPDX-License-Identifier: MIT
#pragma once

#include "install/Http.hpp"
#include "install/Installer.hpp"
#include "install/Manifest.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The voice orders' files as optional content of the setup app (research/33 section 14, research/54 "The voice path
// built"): what tools/fetch-voice.ps1 fetches and tools/deploy-asi.ps1 deploys on the dev machine, downloaded by the
// player's setup from where each lives, checked against the sizes and SHA-256 sums pinned here, and recorded in
// MEVR-install.json like every other file (tagged with the content "voice/<part>"), so Uninstall takes them out and
// Remove takes out only them. Unlike the texture packs (Nexus terms), nothing forbids fetching them: Moonshine Voice
// (MIT), its models (MIT), llama.cpp (MIT) and our fine-tuned model (Apache-2.0 base, Qwen3-0.6B; NOTICE.md).
//
// Our own MEVR-Voice.dll (the recognizer's C interface, built as the MEVR-Voice target) isn't downloaded: the release
// carries it (payload\voice\MEVR-Voice.dll) and Install puts it in ASI\voice with the mod. Without the downloads the
// host finds no models and logs "voice off"; nothing else changes.
namespace mevr::install::voice {

// Where the files go (relative to the game root, as the manifest's paths are).
inline constexpr char kDir[] = "Game/ME1/Binaries/Win64/ASI/voice";
inline constexpr char kDll[] = "Game/ME1/Binaries/Win64/ASI/voice/MEVR-Voice.dll";
inline constexpr char kOnnxRuntime[] = "Game/ME1/Binaries/Win64/ASI/voice/onnxruntime.dll";
inline constexpr char kModelsDir[] = "Game/ME1/Binaries/Win64/ASI/voice/models";
inline constexpr char kLlmDir[] = "Game/ME1/Binaries/Win64/ASI/voice/llm";
inline constexpr char kLlmModel[] = "Game/ME1/Binaries/Win64/ASI/voice/llm/model.gguf";
// The manifest's content tag for all of it ("voice/recognizer", ...) and its line in `packs` (the in-game menu's Display >
// Optional content lists the names there).
inline constexpr char kContent[] = "voice";
inline constexpr char kName[] = "Voice orders";

// The fine-tuned model's GitHub release (the user uploads it; until then the lookup says it isn't published and the rest
// installs without it: the rule parser alone). MEVR-Setup.ini's [Voice] keys override each (ReadModelSource).
inline constexpr char kModelTag[] = "voice-model-1";
inline constexpr char kModelAsset[] = "mevr-voice-qwen3-0.6b-q8_0.gguf";

enum class Archive {
    None,   // the download is the file itself
    TarGz,  // Moonshine's release
    Zip,    // llama.cpp's release
};

// One file a part puts in the game.
struct FileSpec {
    std::string member;  // its path inside the archive ("" for a plain download)
    std::string target;  // relative to the game root
    std::uint64_t size = 0;
    std::string sha256;  // lower-case hex; "" not pinned (computed when it's placed; the archive's own sum is)
};

// One download.
struct Source {
    std::string url;
    std::uint64_t size = 0;  // 0: unknown
    std::string sha256;      // "": not checked (a direct model URL without its sum)
    Archive archive = Archive::None;
    std::vector<FileSpec> files;
};

struct Part {
    std::string id;           // "recognizer", "models-tiny", "models-small", "models-medium", "llm-runtime", "llm-model"
    std::string name;
    std::string description;
    std::string origin;       // where it comes from, for the page
    std::string licence;
    std::vector<Source> sources;  // the model's: empty here, found on GitHub (FindModel)
    std::uint64_t modelSize = 0;  // llm-model only: about what it is, for the page's sizes before the lookup
};

// The parts in install order. Only the fine-tuned model has no sources here.
std::vector<Part> const& Catalogue();
Part const* Find(std::string_view id);
// "voice/<id>".
std::string ContentTag(std::string_view partId);

// What the player picked on the page: the recognizer and the tiny models always (the mod's default, [Voice] model = tiny),
// the larger recognizers and the model tier as they chose.
struct Choices {
    bool smallModels = false;  // not "small": a Windows header macro
    bool mediumModels = false;
    bool llm = true;  // llama.cpp's runtime and the fine-tuned model, together
    bool operator==(Choices const&) const = default;
};
std::vector<std::string> PartsFor(Choices const& choices);

// The download and the disk space the chosen parts take (the model's from its release when found, else Part::modelSize).
struct Sizes {
    std::uint64_t download = 0;
    std::uint64_t disk = 0;
};
Sizes SizesFor(std::vector<std::string> const& parts, std::uint64_t modelSize = 0);

// ---- The fine-tuned model's release ----------------------------------------------------------------------------------
struct ModelSource {
    std::string repo;   // https://github.com/<owner>/<repo>: [Voice] modelRepo, else [Update] repo, else updates::kDefaultRepo
    std::string tag = kModelTag;      // [Voice] modelTag
    std::string asset = kModelAsset;  // [Voice] modelAsset
    // [Voice] modelUrl: a direct https:// download instead of the release lookup, checked against modelSha256 / modelSize
    // when those are set.
    std::string url;
    std::string sha256;
    std::uint64_t size = 0;
};
// From MEVR-Setup.ini next to the setup (missing file or keys: the defaults).
ModelSource ReadModelSource(std::filesystem::path const& setupIni);
// "https://api.github.com/repos/<owner>/<repo>/releases/tags/<tag>"; "" if `repoUrl` isn't a GitHub repository page or the
// tag isn't a plain name.
std::string ReleaseByTagApi(std::string_view repoUrl, std::string_view tag);
// The asset named `assetName` in GitHub's answer for a release: its github.com download URL, size and digest, placed as
// ASI\voice\llm\model.gguf. `why` says what was missing.
std::optional<Source> ModelFromRelease(std::string_view releaseJson, std::string_view assetName, std::string* why = nullptr);
// The lookup over the network (the test server under --test-server). `notPublished`: GitHub has no such release or no
// such file in it yet (a 404, or the asset missing): the rest installs without the model tier's model.
struct ModelLookup {
    std::optional<Source> source;
    std::string error;
    bool notPublished = false;
};
ModelLookup FindModel(ModelSource const& from);

// ---- What's installed ------------------------------------------------------------------------------------------------
struct State {
    bool dll = false;                 // MEVR-Voice.dll is in ASI\voice (the release's install put it there)
    std::vector<std::string> parts;   // part ids with files on record
    std::vector<std::string> damaged; // files on record that are missing or another size (the quick check)
    bool recorded = false;            // "Voice orders" is in the manifest's packs
    bool Has(std::string_view part) const;
};
State ReadState(Layout const& layout);

struct Step {
    std::string text;
    int percent = 0;
};

// Makes the installed parts the chosen ones: downloads what's missing or damaged (into %LOCALAPPDATA%\MEVR\downloads\voice,
// each checked, an archive's files taken out with Windows' tar.exe), puts each in place (Transaction::PlaceIn, recorded as
// voice/<part>), takes out installed parts no longer chosen, records "Voice orders". MEVR-Voice.dll comes from `payload`
// when the game hasn't got it (an install from before this release); the copy in the game folder has no payload and then
// refuses. The model not published yet is a note, not an error: the rest is installed. Refused while the game runs.
Outcome Install(Layout const& layout, std::optional<Payload> const& payload, Choices const& choices, ModelSource const& model,
    std::function<void(Step const&)> const& progress, std::atomic<bool> const* cancel);
// Takes every downloaded voice file out (MEVR-Voice.dll stays: it's the mod's) and the record with them.
Outcome Remove(Layout const& layout);
// Every voice file on record against its SHA-256 (`deep`) or its size. ok when all are as installed.
Outcome Check(Layout const& layout, bool deep);

// Where the downloads wait until they're placed (emptied after an install).
std::filesystem::path DownloadDir();

} // namespace mevr::install::voice

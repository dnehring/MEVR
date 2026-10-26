// SPDX-License-Identifier: MIT
#include "plugin/menu/MenuUi.hpp"

#include "plugin/menu/MenuDecorators.hpp"
#include "plugin/menu/MenuRenderer.hpp"

#include <RmlUi/Core.h>

#include <Windows.h>

#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <system_error>

namespace mevr::menu {

struct Ui::System final : Rml::SystemInterface {
    LogFunction log;
    std::function<double()> clock;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    double GetElapsedTime() override {
        if (clock) {
            return clock();
        }
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
    bool LogMessage(Rml::Log::Type type, Rml::String const& message) override {
        if (log) {
            LogLevel const level = type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ? LogLevel::Error
                                 : type == Rml::Log::LT_WARNING                              ? LogLevel::Warn
                                                                                             : LogLevel::Info;
            if (type != Rml::Log::LT_DEBUG) {
                log(level, "rmlui: " + message);
            }
        }
        return true;  // never break into a debugger
    }
    // Our own textures ("mevr:backdrop") aren't files next to the document.
    void JoinPath(Rml::String& translated, Rml::String const& document, Rml::String const& path) override {
        if (path.rfind("mevr:", 0) == 0) {
            translated = path;
            return;
        }
        Rml::SystemInterface::JoinPath(translated, document, path);
    }
};

namespace {

bool gRmlInitialised = false;

// A path as UTF-8, the text RmlUi takes (never path::string(): that throws for characters outside the ANSI code
// page, e.g. a Cyrillic or Chinese folder name on an English Windows).
std::string Utf8(std::filesystem::path const& path) {
    try {
        std::u8string const u = path.u8string();
        return std::string(u.begin(), u.end());
    } catch (...) {
        return {};
    }
}

std::wstring Wide(std::string const& utf8) {
    if (utf8.empty()) {
        return {};
    }
    int const n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    }
    return out;
}

} // namespace

// RmlUi's own file interface opens paths with the ANSI fopen; ours takes them as UTF-8 and opens them wide.
struct Ui::Files final : Rml::FileInterface {
    Rml::FileHandle Open(Rml::String const& path) override {
        FILE* file = nullptr;
        if (_wfopen_s(&file, Wide(path).c_str(), L"rb") != 0) {
            return 0;
        }
        return reinterpret_cast<Rml::FileHandle>(file);
    }
    void Close(Rml::FileHandle file) override {
        std::fclose(reinterpret_cast<FILE*>(file));
    }
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        return std::fread(buffer, 1, size, reinterpret_cast<FILE*>(file));
    }
    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        return std::fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0;
    }
    size_t Tell(Rml::FileHandle file) override {
        long const at = std::ftell(reinterpret_cast<FILE*>(file));
        return at < 0 ? 0 : static_cast<size_t>(at);
    }
};

namespace {

std::uint64_t NowMs() {
    return GetTickCount64();
}

} // namespace

Ui::Ui() = default;

Ui::~Ui() {
    Shutdown();
}

void Ui::Shutdown() {
    if (context_ != nullptr) {
        Rml::RemoveContext(context_->GetName());
        context_ = nullptr;
        document_ = nullptr;
    }
    if (gRmlInitialised && renderer_ != nullptr) {
        Rml::Shutdown();
        gRmlInitialised = false;
    }
    renderer_.reset();
}

bool Ui::Init(UiSetup const& setup) {
    if (context_ != nullptr) {
        return true;
    }
    log_ = setup.log;
    auto say = [&](LogLevel level, std::string const& text) {
        if (log_) {
            log_(level, text);
        }
    };
    if (setup.device == nullptr) {
        say(LogLevel::Error, "menu: no D3D11 device");
        return false;
    }
    if (gRmlInitialised) {
        say(LogLevel::Error, "menu: RmlUi was set up by another menu already");
        return false;
    }
    documents_ = setup.documents;
    documentName_ = setup.document.empty() ? std::wstring(L"main.rml") : setup.document;
    width_ = setup.width;
    height_ = setup.height;
    system_ = std::make_unique<System>();
    system_->log = setup.log;
    system_->clock = setup.clock;
    renderer_ = std::make_unique<Renderer>(setup.device);
    files_ = std::make_unique<Files>();
    Rml::SetFileInterface(files_.get());
    Rml::SetSystemInterface(system_.get());
    Rml::SetRenderInterface(renderer_.get());
    if (!Rml::Initialise()) {
        say(LogLevel::Error, "menu: RmlUi didn't initialise");
        return false;
    }
    gRmlInitialised = true;
    RegisterDecorators();

    // The mockup's fonts (SIL OFL, third_party/fonts, deployed to ASI\fonts): Rajdhani for headings, Titillium Web
    // for text. Named families and weights, so a SemiBold file is weight 600 whatever its tables say.
    struct Face {
        wchar_t const* file;
        char const* family;
        Rml::Style::FontWeight weight;
    };
    static constexpr Face kFaces[] = {
        {L"TitilliumWeb-Regular.ttf", "Titillium Web", Rml::Style::FontWeight(400)},
        {L"TitilliumWeb-SemiBold.ttf", "Titillium Web", Rml::Style::FontWeight(600)},
        {L"Rajdhani-SemiBold.ttf", "Rajdhani", Rml::Style::FontWeight(600)},
        {L"Rajdhani-Bold.ttf", "Rajdhani", Rml::Style::FontWeight(700)},
    };
    int loaded = 0;
    for (Face const& face : kFaces) {
        bool found = false;
        for (std::filesystem::path const& dir : setup.fontDirs) {
            std::error_code ec;
            std::filesystem::path const file = dir / face.file;
            if (std::filesystem::exists(file, ec) &&
                Rml::LoadFontFace(Utf8(file), face.family, Rml::Style::FontStyle::Normal, face.weight, loaded == 0)) {
                found = true;
                ++loaded;
                break;
            }
        }
        if (!found) {
            say(LogLevel::Warn, std::format("menu: font {} not found", Utf8(face.file)));
        }
    }
    if (loaded == 0) {
        // Windows' own, so text still shows.
        wchar_t windows[MAX_PATH]{};
        GetWindowsDirectoryW(windows, MAX_PATH);
        std::filesystem::path const fonts = std::filesystem::path(windows) / L"Fonts";
        for (wchar_t const* file : {L"bahnschrift.ttf", L"segoeui.ttf"}) {
            std::error_code ec;
            if (std::filesystem::exists(fonts / file, ec)) {
                Rml::LoadFontFace(Utf8(fonts / file), "Titillium Web", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto, true);
                Rml::LoadFontFace(Utf8(fonts / file), "Rajdhani", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto, false);
                say(LogLevel::Warn, std::format("menu: the bundled fonts are missing; using {}", Utf8(file)));
                break;
            }
        }
    }
    context_ = Rml::CreateContext("mevr-menu", Rml::Vector2i(width_, height_));
    if (context_ == nullptr) {
        say(LogLevel::Error, "menu: RmlUi context failed");
        return false;
    }
    say(LogLevel::Info, std::format("menu: RmlUi {} ready, {}x{}, {} font faces, documents in {}", Rml::GetVersion(), width_, height_,
        loaded, Utf8(documents_)));
    return true;
}

bool Ui::LoadDocument() {
    if (context_ == nullptr) {
        return false;
    }
    if (document_ != nullptr) {
        document_->Close();
        document_ = nullptr;
        context_->Update();  // releases the closed document
    }
    Rml::Factory::ClearStyleSheetCache();
    Rml::Factory::ClearTemplateCache();
    std::filesystem::path const file = documents_ / documentName_;
    document_ = context_->LoadDocument(Utf8(file));
    std::error_code ec;
    stamps_[0] = std::filesystem::last_write_time(file, ec);
    stamps_[1] = std::filesystem::last_write_time(documents_ / L"main.rcss", ec);
    if (document_ == nullptr) {
        if (log_) {
            log_(LogLevel::Error, std::format("menu: {} didn't load (see the rmlui: lines)", Utf8(file)));
        }
        return false;
    }
    document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    return true;
}

bool Ui::ReloadIfChanged() {
    std::uint64_t const now = NowMs();
    if (context_ == nullptr || now < nextCheckMs_) {
        return false;
    }
    nextCheckMs_ = now + 1000;
    std::error_code ec;
    auto const rml = std::filesystem::last_write_time(documents_ / documentName_, ec);
    auto const rcss = std::filesystem::last_write_time(documents_ / L"main.rcss", ec);
    if (ec || (rml == stamps_[0] && rcss == stamps_[1])) {
        return false;
    }
    if (log_) {
        log_(LogLevel::Info, "menu: main.rml or main.rcss changed: reloaded");
    }
    return LoadDocument();
}

void Ui::Render(ID3D11DeviceContext* immediate, ID3D11RenderTargetView* target) {
    if (context_ == nullptr) {
        return;
    }
    context_->Update();
    renderer_->Render(immediate, context_, target, width_, height_);
}

} // namespace mevr::menu

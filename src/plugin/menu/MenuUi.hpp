// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

// RmlUi for the main menu (research/28): the library's one-time setup, our system interface, the fonts, one
// context of the menu's size and its document (assets/menu/main.rml), reloaded when its files change. Shared by
// the host (plugin/MainMenu) and the offline preview (tools/menu-preview). Render thread only.
namespace mevr::menu {

class Renderer;

enum class LogLevel { Info, Warn, Error };
using LogFunction = std::function<void(LogLevel, std::string const&)>;

struct UiSetup {
    ID3D11Device* device = nullptr;
    std::filesystem::path documents;              // the folder with main.rml and main.rcss
    std::wstring document = L"main.rml";          // the document in it (the setup app: setup.rml)
    std::vector<std::filesystem::path> fontDirs;  // where Rajdhani-*.ttf and TitilliumWeb-*.ttf are
    int width = 1440;                             // the context, in dp (1 dp = 1 pixel)
    int height = 900;
    LogFunction log;
    // The time RmlUi animates with; empty: the real clock.
    std::function<double()> clock;
};

class Ui {
public:
    Ui();
    ~Ui();
    Ui(Ui const&) = delete;
    Ui& operator=(Ui const&) = delete;

    // Initialises RmlUi (once per process), loads the fonts, makes the context. False: no menu (logged).
    bool Init(UiSetup const& setup);
    bool Ready() const { return context_ != nullptr; }

    Rml::Context* Context() const { return context_; }
    Renderer* GetRenderer() const { return renderer_.get(); }
    Rml::ElementDocument* Document() const { return document_; }
    int Width() const { return width_; }
    int Height() const { return height_; }

    // (Re)loads main.rml (after the data model exists: it binds to it). False: no document (logged).
    bool LoadDocument();
    // Once a second at most: reloads the document if main.rml or main.rcss changed. True when it did.
    bool ReloadIfChanged();

    // Update, then render into `target` (premultiplied sRGB RGBA8). All of the context's state is restored.
    void Render(ID3D11DeviceContext* immediate, ID3D11RenderTargetView* target);

    // Closes RmlUi (the preview, at exit). The host never calls it: it keeps its menu for the process, whose exit
    // mustn't touch D3D (a Ui is never destroyed there).
    void Shutdown();

private:
    std::unique_ptr<Renderer> renderer_;
    struct System;
    std::unique_ptr<System> system_;
    struct Files;  // UTF-8 paths opened wide (_wfopen): any folder name
    std::unique_ptr<Files> files_;
    Rml::Context* context_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    std::filesystem::path documents_;
    std::wstring documentName_ = L"main.rml";
    int width_ = 0;
    int height_ = 0;
    LogFunction log_;
    std::filesystem::file_time_type stamps_[2]{};
    std::uint64_t nextCheckMs_ = 0;
};

} // namespace mevr::menu

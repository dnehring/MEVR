// ASI entry point, loaded by ME3Tweaks' LEBinkProxy through its SPI interface.

#include "core/Log.hpp"
#include "core/Version.hpp"
#include "engine/Engine.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>

#define MEVR_NAME_W L"MEVR"
#define MEVR_VERSION_W L"0.0.1"

SPI_PLUGINSIDE_SUPPORT(SDK_TARGET_NAME_W MEVR_NAME_W, L"ME VR Attempt contributors", MEVR_VERSION_W,
    SPI_GAME_SDK_TARGET, SPI_VERSION_ANY);
// Attach after the DRM has decrypted the exe; the SDK's RVAs point into decrypted code.
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_ASYNCATTACH;

SPI_IMPLEMENT_ATTACH
{
    mevr::log::Open(SDK_TARGET_NAME_W L"MEVR.log");
    mevr::log::Info("MEVR {} attaching to {}", "0.0.1", SDK_TARGET_NAME_A);
    mevr::log::Info("log file: {}", mevr::log::Path().string());

    // Fail closed: hardcoded RVAs are only valid for one exe build.
    auto const version = mevr::GetHostExeVersion();
    if (!version || *version != mevr::kSupportedGameVersion) {
        mevr::log::Error("unsupported game version {} (need {}); VR disabled, game untouched",
            version ? version->ToString() : "unknown", mevr::kSupportedGameVersion.ToString());
        return true;
    }
    mevr::log::Info("game version {} OK", version->ToString());

    ::LESDK::Initializer init{InterfacePtr, SDK_TARGET_NAME_A "MEVR"};
    if (!mevr::engine::Initialize(init)) {
        mevr::log::Error("engine initialization failed; VR disabled");
        return true;
    }
    mevr::log::Info("attached");
    return true;
}

SPI_IMPLEMENT_DETACH
{
    (void)InterfacePtr;
    mevr::log::Info("detaching");
    mevr::log::Close();
    return true;
}

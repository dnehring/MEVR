// ASI entry point, loaded by ME3Tweaks' LEBinkProxy through its SPI interface.
// The host stays small and stable: version check, the engine tick hook, and the mod loader.

#include "core/Log.hpp"
#include "core/Version.hpp"
#include "plugin/ModLoader.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>

#define MEVR_NAME_W L"MEVR"
#define MEVR_VERSION "0.1.0"

SPI_PLUGINSIDE_SUPPORT(SDK_TARGET_NAME_W MEVR_NAME_W, L"ME VR Attempt contributors", L"" MEVR_VERSION,
    SPI_GAME_SDK_TARGET, SPI_VERSION_ANY);
// Attach after the DRM has decrypted the exe; the SDK's RVAs point into decrypted code.
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_ASYNCATTACH;

namespace {

using tGameEngineTick = void(UGameEngine* self, float deltaSeconds);
tGameEngineTick* gGameEngineTickOrig = nullptr;

void GameEngineTickHook(UGameEngine* self, float deltaSeconds) {
    gGameEngineTickOrig(self, deltaSeconds);
    mevr::host::Tick(deltaSeconds);
}

} // namespace

SPI_IMPLEMENT_ATTACH
{
    mevr::log::Open(SDK_TARGET_NAME_W L"MEVR.log");
#if defined(MEVR_MONOLITHIC)
    mevr::log::Info("MEVR {} (monolithic) attaching to {}", MEVR_VERSION, SDK_TARGET_NAME_A);
#else
    mevr::log::Info("MEVR {} (hot reload host) attaching to {}", MEVR_VERSION, SDK_TARGET_NAME_A);
#endif
    mevr::log::Info("log file: {}", mevr::log::Path().string());

    // Fail closed: hardcoded RVAs are only valid for one exe build.
    auto const version = mevr::GetHostExeVersion();
    if (!version || *version != mevr::kSupportedGameVersion) {
        mevr::log::Error("unsupported game version {} (need {}); VR disabled, game untouched",
            version ? version->ToString() : "unknown", mevr::kSupportedGameVersion.ToString());
        return true;
    }
    mevr::log::Info("game version {} OK", version->ToString());

    static ::LESDK::Initializer init{InterfacePtr, SDK_TARGET_NAME_A "MEVR"};
    auto* const tickTarget = init.ResolveTyped<tGameEngineTick>(BUILTIN_GAMEENGINETICK_RVA);
    if (tickTarget == nullptr) {
        mevr::log::Error("failed to resolve UGameEngine::Tick; VR disabled");
        return true;
    }

    // Load the mod before the tick hook goes live, so its first tick sees a started mod.
    mevr::host::Start(InterfacePtr);

    gGameEngineTickOrig = reinterpret_cast<tGameEngineTick*>(init.InstallHook("UGameEngine::Tick",
        reinterpret_cast<void*>(tickTarget), reinterpret_cast<void*>(&GameEngineTickHook)));
    if (gGameEngineTickOrig == nullptr) {
        mevr::log::Error("failed to hook UGameEngine::Tick; VR disabled");
        mevr::host::Stop();
        return true;
    }
    mevr::log::Info("host attached");
    return true;
}

SPI_IMPLEMENT_DETACH
{
    (void)InterfacePtr;
    mevr::host::Stop();
    mevr::log::Info("detaching");
    mevr::log::Close();
    return true;
}

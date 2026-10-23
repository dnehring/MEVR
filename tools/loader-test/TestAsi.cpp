// A test ASI plugin for tools/loader-test, compiled against LExSDKv2's own SPI header exactly as LE1MEVR.asi is
// (the SDK as a submodule, nothing copied): it checks that our loader's interface (src/loader/SpiAbi.hpp) is the
// one SDK plugins call. It hands the interface to the test host through an export.

#define SDK_TARGET 1
#include <SPI.h>

#include <Windows.h>

#include <atomic>

SPI_PLUGINSIDE_SUPPORT(L"MEVR loader test", L"MEVR", L"1", SPI_GAME_LE1, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_ASYNCATTACH;

namespace {
std::atomic<ISharedProxyInterface*> gInterface{nullptr};
std::atomic<DWORD> gAttachThread{0};
} // namespace

SPI_IMPLEMENT_ATTACH
{
    gAttachThread = GetCurrentThreadId();
    gInterface = InterfacePtr;
    return true;
}

SPI_IMPLEMENT_DETACH
{
    (void)InterfacePtr;
    return true;
}

extern "C" __declspec(dllexport) ISharedProxyInterface* MevrTestInterface() {
    return gInterface.load();
}

extern "C" __declspec(dllexport) DWORD MevrTestAttachThread() {
    return gAttachThread.load();
}

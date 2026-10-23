// Our ASI loader (src/loader, research/33) in a tiny host process, no game: this folder holds the host, our
// bink2w64.dll, a stand-in bink2w64_original.dll with the same 79 export names and ASI\MEVRLoaderTest.asi (built
// against LExSDKv2's SPI header). The host loads bink2w64.dll as the game does and checks:
//   - every Bink export resolves, and a forwarded call reaches the original (RADTimerRead's known value);
//   - the test ASI is loaded and attached (on a thread of its own, as it asks);
//   - through the SDK's own interface declaration: GetVersion, GetBuildMode, GetHostGame (not a game here),
//     FindPattern (a pattern taken from this exe's own code, wildcards, malformed and too long), InstallHook (the
//     detour runs, the original is callable, a duplicate name is refused), UninstallHook.
// A ctest test (mevr-loader). Exit code 0: all passed.

#define SDK_TARGET 1
#include <SPI.h>

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    std::printf("%s line %d: %s\n", ok ? "ok  " : "FAIL", line, what);
    if (!ok) {
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

// The hook target: noinline, with enough code for MinHook's jump.
__declspec(noinline) int Target(int a, int b) {
    volatile int x = a * 7;
    volatile int y = b * 3;
    return x + y;
}

using TargetFn = int (*)(int, int);
TargetFn gOriginal = nullptr;

__declspec(noinline) int Detour(int a, int b) {
    return gOriginal(a, b) + 1000;
}

char const* const kBinkNames[] = {"BinkAllocateFrameBuffers", "BinkClose", "BinkCloseTrack", "BinkControlBackgroundIO", "BinkCopyToBuffer",
    "BinkCopyToBufferRect", "BinkCurrentSubtitle", "BinkDoFrame", "BinkDoFrameAsync", "BinkDoFrameAsyncMulti", "BinkDoFrameAsyncWait",
    "BinkDoFramePlane", "BinkFindXAudio2WinDevice", "BinkFreeGlobals", "BinkGetError", "BinkGetFrameBuffersInfo", "BinkGetGPUDataBuffersInfo",
    "BinkGetKeyFrame", "BinkGetPlatformInfo", "BinkGetRealtime", "BinkGetRects", "BinkGetSubtitleByIndex", "BinkGetSummary",
    "BinkGetTrackData", "BinkGetTrackID", "BinkGetTrackMaxSize", "BinkGetTrackType", "BinkGoto", "BinkLoadSubtitles", "BinkLogoAddress",
    "BinkNextFrame", "BinkOpen", "BinkOpenDirectSound", "BinkOpenTrack", "BinkOpenWaveOut", "BinkOpenWithOptions", "BinkOpenXAudio2",
    "BinkOpenXAudio27", "BinkOpenXAudio28", "BinkOpenXAudio29", "BinkPause", "BinkRegisterFrameBuffers", "BinkRegisterGPUDataBuffers",
    "BinkRequestStopAsyncThread", "BinkRequestStopAsyncThreadsMulti", "BinkService", "BinkSetError", "BinkSetFileOffset", "BinkSetFrameRate",
    "BinkSetIO", "BinkSetIOSize", "BinkSetMemory", "BinkSetOSFileCallbacks", "BinkSetPan", "BinkSetSimulate", "BinkSetSoundOnOff",
    "BinkSetSoundSystem", "BinkSetSoundSystem2", "BinkSetSoundTrack", "BinkSetSpeakerVolumes", "BinkSetVideoOnOff", "BinkSetVolume",
    "BinkSetWillLoop", "BinkShouldSkip", "BinkStartAsyncThread", "BinkUtilCPUs", "BinkUtilFree", "BinkUtilMalloc", "BinkUtilMutexCreate",
    "BinkUtilMutexDestroy", "BinkUtilMutexLock", "BinkUtilMutexLockTimeOut", "BinkUtilMutexUnlock", "BinkUtilSoundGlobalLock",
    "BinkUtilSoundGlobalUnlock", "BinkWait", "BinkWaitStopAsyncThread", "BinkWaitStopAsyncThreadsMulti", "RADTimerRead"};

} // namespace

int main() {
    // As the game: bink2w64.dll from the exe's folder.
    HMODULE const bink = LoadLibraryW(L"bink2w64.dll");
    CHECK(bink != nullptr);
    if (bink == nullptr) {
        return 1;
    }
    int resolved = 0;
    for (char const* name : kBinkNames) {
        resolved += GetProcAddress(bink, name) != nullptr ? 1 : 0;
    }
    std::printf("     %d of %zu Bink exports resolve\n", resolved, sizeof kBinkNames / sizeof kBinkNames[0]);
    CHECK(resolved == static_cast<int>(sizeof kBinkNames / sizeof kBinkNames[0]));
    using TimerFn = std::uint64_t (*)();
    auto const timer = reinterpret_cast<TimerFn>(GetProcAddress(bink, "RADTimerRead"));
    CHECK(timer != nullptr && timer() == 0x4D455652);
    CHECK(GetModuleHandleW(L"bink2w64_original.dll") != nullptr);

    // The test ASI: loaded and attached by the loader's thread.
    HMODULE asi = nullptr;
    ISharedProxyInterface* spi = nullptr;
    for (int i = 0; i < 500 && spi == nullptr; ++i) {
        asi = GetModuleHandleW(L"MEVRLoaderTest.asi");
        if (asi != nullptr) {
            auto const get = reinterpret_cast<ISharedProxyInterface* (*)()>(GetProcAddress(asi, "MevrTestInterface"));
            spi = get != nullptr ? get() : nullptr;
        }
        if (spi == nullptr) {
            Sleep(10);
        }
    }
    CHECK(asi != nullptr);
    CHECK(spi != nullptr);
    if (spi == nullptr) {
        return 1;
    }
    auto const attachThread = reinterpret_cast<DWORD (*)()>(GetProcAddress(asi, "MevrTestAttachThread"));
    CHECK(attachThread != nullptr && attachThread() != 0 && attachThread() != GetCurrentThreadId());

    unsigned long version = 0;
    CHECK(spi->GetVersion(&version) == SPIReturn::Success && version == 3);
    bool release = false;
    CHECK(spi->GetBuildMode(&release) == SPIReturn::Success && release);
    SPIGameVersion game = SPIGameVersion::LE3;
    CHECK(spi->GetHostGame(&game) == SPIReturn::FailureGeneric && game == SPIGameVersion::Launcher);  // not a game exe

    // FindPattern: Target's first 12 bytes with two wildcards. A jump thunk (incremental linking) would point
    // elsewhere; this target is built without it.
    auto const* code = reinterpret_cast<std::uint8_t const*>(&Target);
    std::string pattern;
    for (int i = 0; i < 12; ++i) {
        char byte[4];
        std::snprintf(byte, sizeof byte, "%02X", code[i]);
        pattern += (i == 4 || i == 5) ? std::string("??") : std::string(byte);
        pattern += i < 11 ? " " : "";
    }
    void* found = nullptr;
    SPIReturn const rc = spi->FindPattern(&found, pattern.data());
    std::printf("     pattern %s -> %p (Target %p)\n", pattern.c_str(), found, reinterpret_cast<void*>(&Target));
    CHECK(rc == SPIReturn::Success && found != nullptr);
    if (found != nullptr) {
        CHECK(std::memcmp(found, code, 4) == 0 && std::memcmp(static_cast<std::uint8_t*>(found) + 6, code + 6, 6) == 0);
    }
    char bad[] = "48 ZZ";
    CHECK(spi->FindPattern(&found, bad) == SPIReturn::FailurePatternInvalid);
    std::string tooLong;
    for (int i = 0; i < 101; ++i) {
        tooLong += "90 ";
    }
    CHECK(spi->FindPattern(&found, tooLong.data()) == SPIReturn::FailurePatternTooLong);

    // Hooks.
    CHECK(Target(2, 3) == 23);
    void* original = nullptr;
    CHECK(spi->InstallHook("test_Target", reinterpret_cast<void*>(&Target), reinterpret_cast<void*>(&Detour), &original) == SPIReturn::Success);
    gOriginal = reinterpret_cast<TargetFn>(original);
    CHECK(original != nullptr);
    CHECK(Target(2, 3) == 1023);
    CHECK(gOriginal != nullptr && gOriginal(2, 3) == 23);
    void* again = nullptr;
    CHECK(spi->InstallHook("test_Target", reinterpret_cast<void*>(&Target), reinterpret_cast<void*>(&Detour), &again) == SPIReturn::FailureDuplicacy);
    CHECK(spi->UninstallHook("test_Target") == SPIReturn::Success);
    CHECK(Target(2, 3) == 23);
    CHECK(spi->UninstallHook("test_Target") != SPIReturn::Success);

    std::printf("%s\n", gFailures == 0 ? "loader test passed" : "loader test FAILED");
    return gFailures == 0 ? 0 : 1;
}

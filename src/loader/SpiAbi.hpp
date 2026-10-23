// SPDX-License-Identifier: MIT
#pragma once

// The binary interface ASI plugins built against LExSDKv2 expect from the bink2w64.dll proxy that loads them
// ("SPI", version 3), declared here from its documented shape so our loader needs no SDK code (research/33):
//
//   plugin exports       SpiSupportDecl(wchar_t** name, wchar_t** author, wchar_t** version, int* gameFlags, int* minSpi)
//                        bool SpiShouldPreload()       true: attach before the DRM has decrypted the exe
//                        bool SpiShouldSpawnThread()   true: attach on a thread of its own
//                        bool SpiOnAttach(Interface*), bool SpiOnDetach(Interface*)
//   gameFlags            1 << 0 launcher, 1 << 1 LE1, 1 << 2 LE2, 1 << 3 LE3
//   Interface            six virtual functions in this order, each returning a 16-bit status; no destructor in the
//                        vtable. tools/loader-test checks it from a plugin compiled with the SDK's own header.
namespace mevr::loader::spi {

enum class Return : short {
    Undefined = 0,
    Success = 1,
    FailureGeneric = 10,
    FailureDuplicacy = 11,
    FailureHooking = 12,
    FailureInvalidParam = 13,
    FailureUnsupportedYet = 14,
    FailureDeprecated = 15,
    FailurePatternInvalid = 16,
    FailurePatternTooLong = 17,
    ErrorFatal = 100,
    ErrorWinApi = 115,
};

enum class Game : int {
    Launcher = 0,
    LE1 = 1,
    LE2 = 2,
    LE3 = 3,
};

inline constexpr unsigned long kVersion = 3;

class Interface {
public:
    virtual Return GetVersion(unsigned long* outVersion) = 0;
    virtual Return GetBuildMode(bool* outIsRelease) = 0;
    virtual Return GetHostGame(Game* outGame) = 0;
    // A PEiD-style pattern ("48 8B ?? 05"), at most 100 bytes (299 characters), in the game's executable sections.
    virtual Return FindPattern(void** outAddress, char* pattern) = 0;
    // `original` is written before the hook goes live.
    virtual Return InstallHook(char const* name, void* target, void* detour, void** original) = 0;
    virtual Return UninstallHook(char const* name) = 0;
};

using SupportDecl = void (*)(wchar_t** name, wchar_t** author, wchar_t** version, int* gameFlags, int* minVersion);
using ShouldFlag = bool (*)();
using AttachPoint = bool (*)(Interface*);

} // namespace mevr::loader::spi

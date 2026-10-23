// The test stand-in for bink2w64_original.dll (tools/loader-test): every Bink export name (TestOriginal.def), so our
// loader's forwarders resolve; RADTimerRead returns a known value the test host checks.

#include <cstdint>

extern "C" std::uint64_t StubZero() {
    return 0;
}

extern "C" std::uint64_t StubTimer() {
    return 0x4D455652;  // "MEVR"
}

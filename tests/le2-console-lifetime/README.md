<!-- SPDX-License-Identifier: MIT -->
# Console parameter C++ lifetime checks

This standalone project tests `ScopedUnionMember` without loading a game or
calling `ProcessEvent`. The default fake-string test exercises successful
construction, failure during either constructor, dispatch failure, and output
conversion failure. Checks remain enabled in Release and verify cleanup order,
partial construction, copied command input, and surviving output ownership.

```powershell
cmake -S tests/le2-console-lifetime -B local-tools/console-lifetime-tests
cmake --build local-tools/console-lifetime-tests --config Release
ctest --test-dir local-tools/console-lifetime-tests -C Release --output-on-failure
```

For actual SDK empty-string ABI checks on MSVC x64, configure with the existing
SDK checkout, then build and test both Release and Debug:

```powershell
cmake -S tests/le2-console-lifetime -B local-tools/console-lifetime-tests -DMEVR_LIFETIME_SDK_DIR="$((Resolve-Path external/LExSDKv2).Path)"
cmake --build local-tools/console-lifetime-tests --config Release
ctest --test-dir local-tools/console-lifetime-tests -C Release --output-on-failure
cmake --build local-tools/console-lifetime-tests --config Debug
ctest --test-dir local-tools/console-lifetime-tests -C Debug --output-on-failure
```

The SDK is not vendored here. Verification used LExSDKv2 commit
`6bd05809cccb1a099a520371a95779d4a56bb246` and MSVC 19.44.35228 on Windows x64.
LE2 and LE3 tests compile actual SDK definitions and assert parameter size
`0x24`, return offset `0x14`, parameter alignment 4, and `FString` alignment 8.
Empty strings are constructed and destroyed at block address residues 0, 4, 8,
and 12 modulo 16, with surrounding sentinels checked. Allocator stubs abort if
called; these checks never use the engine allocator.

Packed member addresses must retain MSVC's `__unaligned` qualifier. Using
`std::addressof` to avoid C4366 does not establish safe alignment. The guard's
stored pointer and `std::construct_at` return type retain that qualifier, as
checked by distinct-type assertions. No C4366 warning suppression is used.
The fake parameter block uses packing only on MSVC x64 and passes direct member
addresses, as production does. Other compilers test naturally aligned storage.
The standard C++ fallback requires naturally aligned storage; this is not a
portable unaligned-object abstraction. Microsoft documents the packed pointer
issue in [C4366](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-4-c4366?view=msvc-170)
and the extension in [__unaligned](https://learn.microsoft.com/en-us/cpp/cpp/unaligned?view=msvc-170).

For the pinned compiler, inspection of the Release SDK test object's named
`ExerciseSdkPackedEmpty` function showed scalar qword/dword constructor stores
and destructor reads at the packed return address; template symbols explicitly
retained `FString __unaligned`. This is compiler/SDK-specific emitted-code
evidence, not a general language guarantee. Repeatable inspection command:

```powershell
dumpbin /DISASM:NOBYTES local-tools/console-lifetime-tests/le2-console-lifetime-sdk-LE2.dir/Release/SdkLifetimeTests.obj
dumpbin /SYMBOLS local-tools/console-lifetime-tests/le2-console-lifetime-sdk-LE2.dir/Release/SdkLifetimeTests.obj
```

These checks establish ordinary C++ lifetime cleanup and the tested packed ABI.
They do not establish native allocation ownership, returned-string bounds,
conversion validity, native call safety, or recovery after SEH/native faults.
The production change retains SDK `FString`, conversion, and direct dispatch.

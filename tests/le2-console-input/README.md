<!-- SPDX-License-Identifier: MIT -->
# Console input validation checks

Run from the repository root on Windows:

```powershell
cmake -S tests/le2-console-input -B local-tools/console-input-tests
cmake --build local-tools/console-input-tests --config Release
ctest --test-dir local-tools/console-input-tests -C Release --output-on-failure
cmake --build local-tools/console-input-tests --config Debug
ctest --test-dir local-tools/console-input-tests -C Debug --output-on-failure
```

The tests call the actual Windows `MultiByteToWideChar` API. They cover ASCII,
non-ASCII and supplementary Unicode, exact byte limits, views without a final
NUL, every protocol-forbidden C0/DEL byte, overlong encodings, surrogate encodings,
truncated sequences, invalid continuations, and scalars above the Unicode limit.
Checks remain active in Release; no SDK, allocator, game or dispatch is involved.

`detail::ConsoleCommandInput` accepts 1..4096 UTF-8 bytes, matching the existing
control protocol's size and C0/DEL restrictions. It uses `MB_ERR_INVALID_CHARS`
and checks both sizing and conversion results before the caller constructs any
SDK parameters. Invalid input throws `std::invalid_argument`; unexpected API
failures throw `std::system_error` or `std::runtime_error`. Successful conversion
returns an owned, terminated `std::wstring`. See Microsoft's
[conversion API contract](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar).

This is input protection only. It does not authorize a command or prove that
native dispatch is safe. Target/function validation, returned `FString` bounds
and allocator ownership remain separate work. The existing output conversion,
SDK allocator, packed union lifetime guards and direct `ProcessEvent` call are
unchanged. No native capability is enabled by this change.

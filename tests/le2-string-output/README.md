<!-- SPDX-License-Identifier: MIT -->
# Bounded FString observation checks

`mevr::qa::utf16::ReadFString` reads a 16-byte Win64 header through an injected
guarded reader: pointer at byte 0, unsigned count at 8, unsigned capacity at 12.
It copies fields with `memcpy`, so the source header need not be aligned. It
never constructs an SDK string over those bytes, takes ownership, frees the
pointer, or invokes an engine allocator. Source: pinned LExSDKv2
`6bd05809cccb1a099a520371a95779d4a56bb246`, `Common/FString.hpp` and `TArray.hpp`.

Nonzero count includes the final NUL. Null/zero/zero and retained allocated
storage with count zero are valid empty strings; the latter occurs after SDK
`Clear`. Nonzero text must have its declared final terminator. Embedded NULs and
malformed surrogate sequences are rejected, without truncation, substitution,
or Unicode normalization. A legitimate U+FFFD remains legitimate text.

Defaults limit count/capacity/UTF-8 bytes to 1024/4096/4096; immutable ceilings are
4096/65536/16384. The body copy covers count only, never spare capacity. The
header is reread after the bounded copy. Changed/unreadable memory, configured
limits, Windows conversion errors and host allocation failure are unavailable;
inconsistent headers or malformed declared content are malformed. Valid text,
valid empty, unavailable and malformed have distinct statuses and reason codes.
Only available results contain text. Missing fields must not become empty labels.

Conversion uses `WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS)` with explicit
length, null default-character arguments, checked size query, and an exact
second-pass byte count, following [Microsoft's API contract](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte).
Existing INI decoding substitutes malformed surrogates and is intentionally not
reused. JSON escaping belongs to the consumer. A descriptor content key can use
explicit length-prefixed UTF-8 bytes and fixed-width integer encodings; do not
hash C++ padding or treat engine pointers as durable identity.

Build offline on MSVC Win64:

```powershell
cmake -S tests/le2-string-output -B local-tools/string-output-tests -A x64
cmake --build local-tools/string-output-tests --config Release
ctest --test-dir local-tools/string-output-tests -C Release --output-on-failure
```

Repeat with `Debug`. Add `-DMEVR_STRING_SDK_DIR=<initialized pinned SDK checkout>`
to configure the optional actual-SDK header regression. That test constructs
only its own SDK objects using host allocator stubs, compares the real header
with SDK accessors, and reads a header copy at alignment residue four. It does
not link the SDK engine library, use game memory or prove native allocation.

The synthetic suite covers bounds, unreadable or changing memory, terminators,
surrogates, deterministic UTF-8, partial conversion failure and injected output-
buffer allocation failure. The allocation fault is armed after the header copy,
so it targets output resizing rather than debug-STL container bookkeeping.
Ordinary conversions use the real Windows API; test-only
fault injection covers its failed/short return paths. A separate production
object target compiles without that test seam. These are read/encoding checks,
not atomic-body, object-lifetime, save-selection or ConsoleCommand return-
ownership proof. Call from the owning game-thread observation and revalidate
the enclosing object/list separately.

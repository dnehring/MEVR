<!-- SPDX-License-Identifier: MIT -->
# LE2 native console reuse: remaining call prerequisites

Source audit at main `ea25e76c`, with SDK submodule
`6bd05809cccb1a099a520371a95779d4a56bb246`. No native operation was performed.
The diagnostic candidates remain read-only; this report does not grant dispatch
capability. Finish their owned native handshake and inspect actual observations
before enabling new calls. See the [protected workflow](../tools/qa-le2/control.md).

Reuse the existing `engine::legacy::ConsoleCommand` route: direct ProcessEvent
plus LE2's SDK-backed FString allocator. Do not replace it with the LE1 generic
reflection/allocator path or a generated SDK function wrapper. The following
facts identify the remaining small adaptations and live checks.

## ProcessEvent ownership and target

The LE2 [address table](../src/engine/Addresses.hpp) identifies ProcessEvent at
RVA `0xD8D50`. Its entry has no byte pattern: the existing executable check is
`AtRvaUnchecked`, meaning function-start/section structure, not instruction
identity. Report that classification accurately. A pointer somewhere in `.text`
does not establish the intended function.

The [host hook](../src/plugin/ProcessEventHook.cpp) resolves the target, installs
its own detour, and stores the installer-returned `gOriginal`. Installation can
fail while host startup continues. The current public hook API and HostServices
do not expose these provenance facts; observer availability does not imply the
hook installed successfully.

The smallest proposed addition is a diagnostic-only read-only snapshot in
`ProcessEventHook.hpp/.cpp`: installation status, resolved installation target,
exact owned detour, installer-returned next hop, backend, and executable-check
classification. Diagnostic builds are monolithic, so this needs no HostServices
ABI change. Publish a coherent snapshot after successful installation.

Within the observer's existing guarded controller sample, report its vtable,
slot **72** (byte offset **0x240**, pinned LE2 `Core_classes.hpp`) and observed
slot target. Compare the slot with the recorded installation target exactly.
An inline hook normally leaves the slot pointing to the executable entry;
another text address, an arbitrary executable allocation or the trampoline is
not an acceptable substitute.

Keep three separate findings: slot agreement, recorded installation, and current
patch-chain agreement. A bounded guarded reader can recognize the forms emitted
by the existing MinHook backend (`third_party/minhook/src/hook.c` and
`trampoline.c`): near relative jump to relay, RIP-relative indirect jump to the
exact owned detour, and its documented short-jump hotpatch form. Unknown forms,
cycles or exhausted bounds remain unresolved. Existing `exe::HookJumpLength`
recognizes forms but does not prove destination ownership. `gOriginal` is the
installer-returned next hop, not proof of pristine engine code beneath earlier
hooks. Do not describe recorded installation as validation of that entire chain.

## String allocation and lifetime

The pinned SDK's `Common/Core.hpp` uses Malloc vtable offset `0x08` and Free
offset **`0x18` for LE2/LE3**; LE1 Free is **`0x20`**. SDK FString destruction
routes through the correct game-specific `sdkFree` (`Common/Common.cpp`). Keep
that route. `GameAdapter::AllocatorGlobalsForGame` currently supplies no LE2
configuration for `ue3::memory`, so that module is not a drop-in replacement.

Inspection of [Legacy.cpp](../src/engine/Legacy.cpp) and pinned SDK
`Common/FString.hpp`, `TArray.hpp`, and LE2 `Engine_f_structs.hpp` found:

| Finding | Required adaptation or evidence |
|---|---|
| The generated ConsoleCommand parameter constructor zeroes bytes; its destructor is empty. Legacy constructs Command, but does not explicitly construct the nontrivial ReturnValue union member before use/destruction. | Explicitly construct both strings; preserve the exact packed parameter layout. See [C++ union lifetime guidance](https://learn.microsoft.com/en-us/cpp/cpp/unions?view=msvc-170). |
| Both string destructors occur only after output conversion. Ordinary C++ exceptions can bypass them. | Use a small local ownership guard for successfully constructed members and test normal unwinding. This does not make arbitrary native access faults recoverable. |
| Widen uses conversion flags zero, ignores the second conversion result and accepts unrestricted length/NUL from direct callers. | Enforce the existing bounded command contract, reject embedded NUL, and require strict successful UTF-8 conversion before engine allocation. See [MultiByteToWideChar](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar). |
| Narrow scans the returned pointer to NUL rather than validating FString data/count/capacity and applying the channel output limit. Empty output and conversion failure collapse to the same result. | Validate and bounded-copy the return header/content, then convert with explicit length and distinguish error from empty output. See [WideCharToMultiByte](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte). |
| SDK allocation helpers rely on assertions and can record an unchecked allocation result before copying. | Validate the live allocator/pointee/vtable and handle allocation failure before copying; Release assertions are not a runtime guard. This is a failure path, not evidence that the live allocator normally returns null. |

The lifetime findings above are addressed by `ScopedUnionMember`: both union
strings are constructed, and successful construction is paired with destruction
on normal return and ordinary C++ unwinding. The packed block is still `0x24`
bytes, with ReturnValue at `0x14`; FString requires alignment 8 while the block
uses alignment 4. The guard preserves MSVC's `__unaligned` pointer qualifier
through construction, storage and destruction. `std::addressof` alone would only
avoid a warning, not repair alignment. See Microsoft's [C4366 explanation](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-4-c4366?view=msvc-170)
and [unaligned modifier](https://learn.microsoft.com/en-us/cpp/cpp/unaligned?view=msvc-170).
The [focused checks](../tests/le2-console-lifetime/README.md) exercise exception
cleanup and actual SDK empty strings at each valid alignment residue. This proof
is scoped to the pinned MSVC/SDK; repeat it after either changes. Nonempty engine
allocation, native dispatch, conversion bounds and fault recovery remain open.

Two suspected issues are **not** defects: `Widen(command).c_str()` remains alive
while the FString constructor copies it, and the recently corrected Narrow
buffer includes room for the terminator before resizing. Preserve those facts
when reviewing a later fix.

Reuse the ownership-test patterns in `tests/ReflectTests.cpp`, not the existing
LE1 converter unchanged. Its current narrow loop is not the bounded LE2 return
validator needed here. Meaningful focused cases are malformed UTF-8, embedded
NUL, oversized/inconsistent return headers, missing terminators, invalid
surrogates, allocation failure and exceptions after member construction.

Before enabling the existing native console path, require live controller and
function identity, parameter metadata, hook-aware target checks, guarded GMalloc
and LE2 Malloc/Free target observations, and an explicit allocator/returned-string
lifetime proof. A read-only SEH wrapper around the whole call cannot establish
safe cleanup after a native fault. Preserve post-dispatch uncertainty; do not
retry or free a pointer whose ownership is unknown.

## Smaller first call proof

`Engine.PlayerController.GetFOVAngle` is a **scripted event**, not a FUNC_Native
getter. The pinned LE2 declarations in `Engine_functions.cpp` and
`Engine_f_structs.hpp` specify flags `0x20802`, one float ReturnValue at offset
zero, size four, array dimension one, property flags `0x580`. Expected metadata
is `ParmsSize=4`, `NumParms=1`, `ReturnValueOffset=0`, and `iNative=0`.

The observer currently reports ConsoleCommand metadata only. A later bounded
extension can reuse its exact-owner/property traversal for this getter. Once
live metadata and target checks pass, a separate game-thread experiment can
initialize the four-byte return to a NaN sentinel with surrounding canaries,
invoke through the validated controller slot, and record a finite changed return
with intact bounds. Reacquire identities immediately before the call. Do not
use generic `runtime::Call` here: its LE1-oriented path can clear nonzero native
indices, and LE2 generic reflection remains disabled. Do not modify function
flags or use a generated wrapper.

That experiment would establish only this bounded ProcessEvent/script-return
path. It would not prove ConsoleCommand allocation, arbitrary console commands,
normal save loading, story coherence or any VR interaction. The first pending
native action remains the existing read-only handshake, not this proposed call.

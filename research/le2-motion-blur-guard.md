<!-- SPDX-License-Identifier: MIT -->
# ME2's motion-blur guard (2026-11-18, lane AU6)

ME1's native guard (`plugin/MotionBlurGuard`, [research/62](62-motion-blur-and-menu-pacing.md)) on ME2, as ME3 got it
([research/le3-recenter-transition-failure.md](le3-recenter-transition-failure.md) "Reusing the existing motion-blur guard"):
the same module and test, ME2's own address map (`engine/Addresses.hpp` `le2::kMotionBlurProxyRender`, `kNoBlurProxyRender`,
`kMotionBlurProxyPairs` and ten `kTable` rows with byte patterns). The ME2 audit's finding 13 (rendering REN-25, ledger R10,
plan T12). Local data only: the decrypted `.text` dump (`%LOCALAPPDATA%\MEVR\dump\MassEffect-60AEEFA5.text.bin`), the exe's own
`.rdata`, the private Ghidra project; no native code is kept anywhere in the repository.

## How it was found

1. **The method checked on ME1 first.** A small local script scanned the dump for the constructors' vtable stores, ME1's two
   patterns (`48 8D 05 ?? ?? ?? ?? 48 89 03 48 85 FF 74 0A F3`, the base constructors; `48 8D 05 ?? ?? ?? ?? 48 89 07 45 8B C4
   B8 01 00`, the extended factory), read each referenced table's ten slots from the exe and computed the guard's fingerprint:
   ME1's five pairs came out with the fingerprints already in `Addresses.hpp`. Slot 1 is the render entry (ME1 through a `jmp`
   thunk to `0x279360` / `0x27A100`; ME3 directly).
2. **ME2.** The base pattern finds six tables; ME2's extended factory stores with `45 8B C5` instead of `45 8B C4`, and a scan for
   every RIP-relative `lea` whose table's slot 1 is one of the two render thunks finds those four too: ten tables, the same
   structure as ME1's. Slot 1 is a `jmp` thunk on both sides: `0x3CE710` to the blur body `0x3A9FE0`, `0x3CE720` to the no-blur
   body `0x3AAE40`. Both bodies start with ME1's prologue bytes (`48 8B C4 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C
   24 90`) and take the same six argument slots; Ghidra's decompiles differ in one call only, the blur body's binding of the
   blur's shader parameters (as ME1's).
3. **The pairs.** A blur table has the blur render thunk in slot 1 and a blur-only entry in slot 5; its sharp twin the other
   thunk and the default entry. Paired by their constructors (the decompiles of each base pair differ only in the vtable they
   assign) and, for the extended factory, by its branches: one function, four branches, each allocating 0xF8 bytes over the
   same base constructor of its kind (the blur base for the two blur tables, the sharp base for the two sharp ones) and then
   storing its own table; the branch's second flag selects which of the two. The shader-selector slots (7, 8) of a pair sit
   0x60 apart in the base pairs and 0x220 apart in the extended ones, as on ME1.

| pair | blur table | no-blur table | blur fingerprint | no-blur fingerprint | stored at (blur / no-blur) |
|---|---|---|---|---|---|
| 1 | `0x10916F8` | `0x1091608` | `0xBFC71FCE68229650` | `0x52E76B8A421260FC` | `0x3ACA94` / `0x3AD6F4` |
| 2 | `0x1091748` | `0x1091658` | `0xCE1E0C8FBBC5C108` | `0x21030D6CD55FEDF1` | `0x3AC601` / `0x3AD261` |
| 3 | `0x1091798` | `0x10916A8` | `0x13C2B4294DA71338` | `0x50E1AB55D374BD01` | `0x3AC214` / `0x3ACE74` |
| 4 | `0x10931A8` | `0x1093108` | `0xA48F5C1ED1E104C0` | `0xAA81D23F6690FEA1` | `0x3B2387` / `0x3B2145` |
| 5 | `0x10931F8` | `0x1093158` | `0x2723B679797B8B86` | `0x21761A01E2EB112F` | `0x3B24A4` / `0x3B2262` |

Pair 2 is also the extended factory's base (ME3's "intermediate third pair").

## Wiring

The host installs it at attach (`Entry.cpp`, `MEVR_WITH_HALCYON_LE2`), reports it every 10 s (`motion blur guard: N proxies
converted`), serves `HostServices::motionBlurProtected` (so `game/SystemSettings` stops its menu-return reapply while the
guard is healthy) and puts `motionBlurGuard` in the snapshot. `[Performance] motionBlurGuard = 0` still turns it off at start.
ctest `mevr-le2-motion-blur-guard` runs the shared test on ME2's table; `mevr-rva-table` covers the ten rows.

## In the game (fake headset, Save 4)

`exe: 58 of 58 addresses at their RVAs`; `motion blur guard: installed (5 paired post-process variants ...)`; `system settings:
menu-return reapply disabled: native motion-blur guard healthy`; 45 then 81 proxies converted to no-blur in the first 40 s, 0
unknown (all of pair 5, the extended factory's); the eye snapshot (`2026-11-18_14-04-40-124`) renders as before (bloom and the
grade intact). [verify] in the headset: no one-eye smear on a fast head turn (ME1's symptom), over a level change and a
cinematic.

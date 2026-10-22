# 17: Reading LE1's native code with Ghidra

2026-10-21. Tooling: [tools/ghidra/README.md](../tools/ghidra/README.md). Ghidra 12.1.4, headless,
over the exe with the mod's decrypted `.text` spliced in (LE1 2.0.0.48602, timestamp
`0x60AEEFA2`). Local research data stays in `%LOCALAPPDATA%\MEVR\ghidra`.

## What auto-analysis gives

- 57,803 functions (the `.pdata` unwind table seeds them), 7 min 35 s on the dev machine.
- No RTTI: the RTTI analyzer finds nothing usable ("No vf table pointer is defined for this
  TypeDescriptor model", 6 times), so classes and vtables have no names. Vtables are still plain
  runs of code pointers in `.rdata`, so `decompile.ps1` finds a virtual's slot by searching for
  pointers to it.
- Names come from us: LExSDKv2's RVA table (33), our curated list (`tools/ghidra/symbols-le1.csv`,
  51) and, once the game has run with `MEVR_DUMP_NATIVES=1`, every native UFunction's `exec`
  (thousands; not run yet).
- A native's `exec` thunk reads its parameters from the bytecode and calls the C++ virtual through
  a vtable slot, so `exec` names lead straight to the C++ bodies.
- The vtable search agrees with LExSDKv2: `UObject::ProcessEvent` sits at slot `+0x250` of every
  vtable that holds it (the SDK's "vtable slot 0x250").
- **The 2026-10-08 dump has hook patches in it.** It was taken at the mod's first heartbeat, after
  the hooks went in, so 5 function starts are a 5-byte `jmp` out of the image: `0x102DC0`
  (unknown, maybe another ASI's hook), `CallFunction` `0x12AE40`, `ProcessEvent` `0x12DD20`,
  `UGameEngine::Tick` `0x3CA090`, `CalcSceneView` `0x4C5300`. LExSDKv2's commented post-hook
  patterns record the original first bytes of ProcessEvent (`40 55 41 56 41`) and CallFunction
  (`40 55 53 56 57`), and the bytes after the jmp match their patterns, so
  `tools/ghidra/restore_prologues.py` writes them back. The other three stay cut short until the
  host's attach-time dump (built 2026-10-21, **[verify: run once, check the `.txt`'s
  `patched_starts`]**) replaces this one.

## Sanity check: the fire path against research/14

`tools/ghidra/decompile.ps1 0xB72D30 0xB72660 0xC59870 0xC621E0` (natives' addresses from the
log line `weapon: natives: BioWeaponRanged.GetMuzzleLoc exe+0xB72D30, BioWeaponRanged.TickFire
exe+0xB72660, BioPawn.GetWeaponStartTraceLocation exe+0xB939E0,
SkeletalMeshComponent.GetSocketWorldLocationAndRotation exe+0x5740C0`, LE1MEVR.log 2026-10-08).

| Function | Decompile shows | research/14 says | Verdict |
|----------|-----------------|------------------|---------|
| `execGetMuzzleLoc` `0xB72D30` (94 bytes) | Skips one optional `0x41` token (`EX_DebugInfo`, with a call through `GNatives`), then `this->vtable[+0x8E8](this, &out)` and copies the 12-byte vector to `Result` | (only its address) | The native is a thunk to virtual slot `0x8E8` |
| vtable at `0x1315460` | `+0x8E8` (`0x1315D48`) holds `0x140C59870`; `+0x8A0` (`0x1315D00`) holds `0x140C621E0` | `GetMuzzleLoc` C++ `0xC59870`, TickFire body `0xC621E0` | **Confirmed**: the script natives reach exactly the bodies research/14 read |
| `execTickFire` `0xB72660` (128 bytes) | Steps one float parameter through `GNatives[token]`, skips `EX_DebugInfo`, calls `this->vtable[+0x8A0](this, DeltaTime)` | TickFire's body `0xC621E0` **[verify: the log prints the native's address]** | **Confirmed** (the `[verify]` is resolved) |
| `GetMuzzleLoc` `0xC59870` (168 bytes) | If `SkelMeshComp` (`+0x3E8`) and a check on `m_MuzzleSocketName` (`+0x490`): `GetSocketWorldLocationAndRotation(SkelMeshComp, name, &loc, &rot, 0)`; if any fails: `Instigator` (`+0x1A4`)'s `Location` (`+0x108`), else its own `+0x108` | Same | **Matches.** New detail: the "not None" check calls `0x78C50`, a shared 6-byte `return 1` stub (identical-code folding), so only a null mesh or a failed socket lookup falls back. Named `ReturnTrue_folded` |
| `TickFire` `0xC621E0` (1128 bytes) | `-= DeltaTime` on `+0x4F4` and `+0x4F0` while positive; returns unless bit 8 of `+0x548`; the alternative path `0xC62650` when `0x4A7550(Instigator, 0)` and byte `+0xDF4` of the virtual `+0x890`'s result is 3 or 4; mode byte `Stats (+0x4C0) +0x259`: 0 or 2 (or no Stats) automatic, 1 single, 3 burst (`+0x4EC` count, `+0x4F4` = `Stats +0x22C` when it runs out), **any other mode doesn't fire**; firing sets bit 1 of `+0x548` | Same, with "else automatic" | **Matches**, refined: only modes 0 and 2 are automatic |
| Shot start and aim in TickFire | Single/automatic: `Instigator->ProcessEvent(FindFunction(name), parms)` (vtable `+0x250`) with `CurrentWeapon` at parms `+0x0` (None) and the start read back from `+0x8`; burst: the thunk `0xC63C10` (named `APawn::eventGetWeaponStartTraceLocation`). Aim: `this->ProcessEvent(...)` with the start at `+0x0` and the rotator read from `+0xC`. Then `0xC589F0(this, &start, &aim)`, and `+0x4F0 += 1 / this->vtable[+0x8F8]()` | Same parms and order | **Matches** |

So the code reading in research/14 holds; it was done with `dumpbin`, this took seconds.

## Notes for later reading

- `FUN_1400db580` + `FUN_14015f4d0` before a `vtable[+0x250]` call is the `FindFunctionChecked(FName)`
  then `ProcessEvent` pattern of a script event call; the `FName` comes from a static (`DAT_...`).
- `exec` thunks: `*(Stack + 0x24)` is the bytecode pointer (`Stack.Code`), `*(Stack + 0x1C)` the
  object; `0x41` is `EX_DebugInfo`.
- Ghidra's own `FUN_`/`DAT_` names are absolute addresses (`FUN_140c59870` = RVA `0xC59870`).

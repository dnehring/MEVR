# tools/ghidra: decompiling LE natives

Headless Ghidra over the decrypted LE1 exe, so we can read any native function as C (with its
callers, callees and vtable slots) in a few seconds, instead of `dumpbin /disasm` and grep.

**Local research data only.** The spliced exe, the Ghidra project, its logs and the decompile
output all live under `%LOCALAPPDATA%\MEVR\ghidra` and `%LOCALAPPDATA%\MEVR\dump`. Never commit or
share them, never copy them into the repo tree. The repo holds only the scripts and the names we
found (`symbols-le1.csv`).

## Requirements

- Ghidra 12.1.4 (`$env:GHIDRA_INSTALL_DIR`, default `F:\downloads\ghidra_public\ghidra_12.1.4_PUBLIC`)
- JDK 21+ (`$env:MEVR_GHIDRA_JAVA_HOME`, default the newest `C:\Program Files\Eclipse Adoptium\jdk-*`)
- Python 3 (for `collect_symbols.py`, and the maintainer's local `local-tools/splice_text.py`, which is not published)
- The mod's `.text` dump: run the game once with the mod (`%LOCALAPPDATA%\MEVR\dump\MassEffect-<timestamp>.text.bin`)
- For `mcp.ps1`: uv (`uvx`), the GhidraMCP 2.0 extension installed in Ghidra and its Python wheel

The scripts point Ghidra's settings and cache directories at `%LOCALAPPDATA%\MEVR\ghidra\settings`
and `\cache` (`XDG_CONFIG_HOME`/`XDG_CACHE_HOME`). The packaged Claude app sees `%APPDATA%` through
MSIX redirection, which breaks Ghidra's script loader ("The data file must be inside the data dir").

## The project

| What | Where |
|------|-------|
| Ghidra project | `%LOCALAPPDATA%\MEVR\ghidra\LE1.gpr` (project name `LE1`) |
| Program inside it | `/MassEffect1.exe` (x86:LE:64, windows; image base `0x140000000`) |
| Spliced exe | `%LOCALAPPDATA%\MEVR\ghidra\bin\LE1-60AEEFA2\MassEffect1.exe` |
| Logs | `%LOCALAPPDATA%\MEVR\ghidra\logs\` (`setup-LE1.log`, `seed-LE1.log`, `decompile-LE1.log`) |
| Decompile cache | `%LOCALAPPDATA%\MEVR\ghidra\out\LE1\0x<RVA>.c`, `last.txt` |
| Timings | `%LOCALAPPDATA%\MEVR\ghidra\timings.txt` |

**One process at a time.** A Ghidra project can be open in only one process. `setup.ps1`,
`seed.ps1`, `decompile.ps1` and the MCP server (`mcp.ps1`) each open it, so never run two at
once; while the MCP server runs, the others stop with "is open in another process". Stop the MCP
server first (Claude Code: `/mcp`), then decompile, then let the client start it again.

## Usage

```powershell
# 1. Build the project (once per exe build; -Force rebuilds). Run it in the background.
powershell -File tools\ghidra\setup.ps1            # splice, import, auto-analysis, then seed.ps1

# 2. Names (rerun after editing symbols-le1.csv or after a natives dump)
powershell -File tools\ghidra\seed.ps1

# 3. Read code
tools\ghidra\decompile.ps1 0xB72D30                # RVA
tools\ghidra\decompile.ps1 exe+0xC621E0 0x4C7A4E   # exe offset; an address inside a function shows the function
tools\ghidra\decompile.ps1 0x140C59870             # absolute address
tools\ghidra\decompile.ps1 ProcessEvent            # name: exact, last part, or a unique substring
tools\ghidra\decompile.ps1 -Search MuzzleLoc       # list matching functions and labels
tools\ghidra\decompile.ps1 0xB72D30 -NoCache       # ignore the cache

# 4. Persist findings (saved in the project; -Record also appends to symbols-le1.csv)
tools\ghidra\decompile.ps1 0x78C50 -Rename ReturnTrue_folded -Record
tools\ghidra\decompile.ps1 0xC621E0 -Comment "vtable 0x1315460 slot 0x8A0" -Record
```

Each decompiled function prints a header, then Ghidra's C:

```
// ==== ABioWeaponRanged::GetMuzzleLoc  RVA 0xC59870  (exe+0xC59870, 0x140C59870)  168 bytes
// callers (0):
// pointers to it in data (1; vtable start guessed from the run of code pointers):
//   0x1315D48 in .rdata = vtable? 0x1315460 + 0x8E8
// callees (2):
//   ReturnTrue_folded 0x78C50, USkeletalMeshComponent::GetSocketWorldLocationAndRotation 0x584640
```

Virtual functions have no direct callers; the "pointers to it" line finds them in vtables (the
vtable start is a guess: the first of the contiguous code pointers). A native's `exec` thunk
usually just calls the C++ virtual through such a slot (`execGetMuzzleLoc` calls `+0x8E8`).

`-Record` lines go to `symbols-le1.csv`, so a rebuilt project (`setup.ps1 -Force`) keeps them.
Prefer renames that say how sure we are: a `_guess` suffix for inferred roles.

## Hook-patched functions in the dump

A hook overwrites a function's first 5 bytes with a `jmp` to its trampoline, outside the image.
The first dump (2026-10-08) was taken at the mod's first heartbeat, after the hooks went in, so 5
LE1 functions start with such a jmp: `0x102DC0` (unknown), `UObject::CallFunction` `0x12AE40`,
`UObject::ProcessEvent` `0x12DD20`, `UGameEngine::Tick` `0x3CA090` and
`ULocalPlayer::CalcSceneView` `0x4C5300`. Ghidra then sees a 5-byte function ending in bad data.

- `setup.ps1` runs `restore_prologues.py`, which writes back the original 5 bytes where LExSDKv2's
  commented post-hook patterns record them and the following bytes match: ProcessEvent and
  CallFunction. The other three stay broken in this dump.
- `decompile.ps1` prints a WARNING on any function that still starts with a hook jmp.
- The real fix (built, not yet run): the host now dumps at attach, before our or Halcyon's hooks
  (`engine/CodeDump`, called from `plugin/Entry.cpp`), records the starts other ASIs already
  patched in the `.txt` (`patched_starts=`, `patched=`), and replaces an existing dump when its
  own code is cleaner (a dump without that record counts as unknown, so the old one is replaced on
  the first run). After that run, `seed.ps1` warns that the dump is newer than the project; rebuild
  with `setup.ps1 -Force`.

## Names (seeding)

`seed.ps1` applies, in order (a later source wins; the earlier name stays as a label):

1. **The natives dump** `%LOCALAPPDATA%\MEVR\dump\natives-LE1-<timestamp>.csv`: every UFunction
   with `FUNC_Native` and its `Func` pointer (`ABioWeaponRanged::execGetMuzzleLoc`, comment
   `native BioWeaponRanged.GetMuzzleLoc (SFXGame)`), plus the GNatives bytecode handlers no
   UFunction names. The mod writes it (`engine/NativesDump.cpp`) when the game runs with
   `MEVR_DUMP_NATIVES=1` in its environment or `dumpNatives = 1` under `[Debug]` in
   `Binaries\Win64\ASI\LE1MEVR.ini`; it walks GObjects once the script packages are loaded
   (the first heartbeat after the hooks go live) and logs `natives dump: N native functions ...`.
2. **LExSDKv2's RVA table** (`external/LExSDKv2/Src/LESDK/Init.hpp`, `BUILTIN_*_RVA`), read by
   `collect_symbols.py`: ProcessEvent, CallFunction, GObjects, GNames, GEngine, ...
3. **`symbols-le1.csv`**, hand-curated from `src/` and `research/` (columns `rva,name,kind,comment`;
   `kind` is `func`, `label` or `data`).

Comments become plate comments, so the decompile shows where each name came from.

## MCP server (GhidraMCP 2.0)

`mcp.ps1` serves the same project to an MCP client through GhidraMCP's headless (PyGhidra) mode,
with `--no-analyze` (the project is already analyzed and seeded). It runs
`uvx --python 3.12 --from "ghidra-mcp[headless] @ file:///<wheel>" ghidra-mcp --headless
--binary <spliced exe> --project %LOCALAPPDATA%\MEVR\ghidra --project-name LE1 --no-analyze ...`,
which reuses the existing `/MassEffect1.exe`. Renames and comments made through MCP tools are
saved to the project when the server exits. `-Offline` resolves the wheel's dependencies from uv's
cache only. The wheel path defaults to
`F:\downloads\ghidramcp-2.0\x\GhidraMCP-release-2-0\ghidra_mcp-2.0.0-py3-none-any.whl`
(`$env:MEVR_GHIDRA_MCP_WHEEL`).

Register it with Claude Code (from the repo root, once):

```
claude mcp add ghidra -s local -- powershell -NoProfile -ExecutionPolicy Bypass -File "H:\ME VR Attempt\tools\ghidra\mcp.ps1"
```

It offers 28 tools (`decompile_function_by_address` takes absolute addresses such as
`0x140B72D30`; `search_functions_by_name`, xrefs, renames, comments, ...). Remember the
one-process rule above: while it runs, `decompile.ps1` and `seed.ps1` can't open the project.

## Timings (RTX 4090 machine: 24 threads, 64 GB; LE1 2.0.0.48602)

| Step | Time |
|------|------|
| `setup.ps1` import + auto-analysis (57,803 functions; `-max-cpu 22`, 24 GB heap) | 7 min 35 s to 10 min 40 s (three runs) |
| `seed.ps1` | 6-8 s |
| `decompile.ps1`, one or several functions in one run | 6-9 s (mostly Ghidra's startup) |
| `decompile.ps1`, cached | 0.03 s |
| `mcp.ps1` to the first tool answer | about 10 s |

The slowest analyzers were Stack (157 s), x86 Constant Reference (106 s), Decompiler Switch
Analysis and Disassemble Entry Points (47 s each). The pre-script `MevrPreAnalysis.java` keeps the
optional Decompiler Parameter ID and Aggressive Instruction Finder off (they're off by default).

## Files

| File | Role |
|------|------|
| `common.ps1` | Paths, the Ghidra/JDK environment, the project lock check, address parsing |
| `setup.ps1` | Splice, import, analysis, seeding; idempotent (`-Force` rebuilds, `-NoSeed`) |
| `restore_prologues.py` | Writes back hook-patched prologues LExSDKv2 records (run by `setup.ps1`) |
| `seed.ps1`, `collect_symbols.py` | Gather and apply names |
| `decompile.ps1` | Decompile / search / rename / comment |
| `mcp.ps1` | GhidraMCP headless server on the project |
| `symbols-le1.csv` | Our names (committed) |
| `scripts/MevrPreAnalysis.java`, `MevrApplyNames.java`, `MevrDecompile.java` | Ghidra scripts (Java GhidraScripts, no extra installs) |

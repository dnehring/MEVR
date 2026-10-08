# 06: Reference repo licenses and metadata

From the GitHub API (`gh api repos/<owner>/<repo>`), 2026-10-07. `size_kb` is GitHub's repo size
estimate.

| Repo | SPDX license | size_kb | Last push | Default branch |
|------|--------------|---------|-----------|----------------|
| praydog/UEVR | NOASSERTION (LICENSE says "All rights reserved") | 4,569 | 2026-08-30 | master |
| Aeka0/Overlord | GPL-3.0 | 57,673 | 2026-10-07 | main |
| ME3Tweaks/LExASIs | none | 1,071 | 2026-08-06 | main |
| ME3Tweaks/LEBinkProxy | none | 206 | 2025-07-04 | main |
| ME3Tweaks/LegendaryExplorer | GPL-3.0 | 686,116 | 2026-08-29 | Beta |
| praydog/REFramework | MIT | 53,447 | 2026-09-16 | master |
| alicealys/h2-mod | GPL-3.0 | 92,157 | 2026-07-27 | develop |
| llamathings/LExSDKv2 | none (deliberate, see README) | 14,893 | 2026-07-12 | (HEAD) |
| CircuitLord/CircuitLordVRModInstaller | Installer MIT; **mods closed, all rights reserved** | n/a | n/a | n/a |
| jrush64/MassEffect1-VR, MassEffect2-VR, MassEffect3-VR (Halcyon) | GPL-3.0 | 1,785 (ME2) | 2026-09-30 | main |

Added 2026-10-07 (later): Halcyon's three repos, cloned to `reference/` at `3c51514` (ME1),
`4784e90` (ME2) and `cfd4001` (ME3). Created 2026-09-20, two commits each. GPL-3.0: design and
fact reference only. See [13](13-existing-me-vr-mods.md).

## Cloned into `reference/` (2026-10-07, via `tools/fetch-references.ps1`, shallow)

| Repo | Commit | Commit date | On disk |
|------|--------|-------------|---------|
| Overlord | `11f3005034188894976cb8392122ad166d59f927` | 2026-10-08 | 163 MB (submodules **not** fetched) |
| REFramework | `d1461375aee4ec3f313170f8eaad12064eb542d9` | 2026-09-16 | 132 MB (submodules not fetched) |
| LExASIs | `aaad03ab6e5112e27ff5528f7b08c3bcf3870c31` | 2026-07-16 | 115 MB including submodules |
| ├ External/LExSDKv2 | `3885be14113816ff1d2a0d941aa96bba6df9dc58` | | |
| ├ External/spdlog | `27cb4c76708608465c413f6d0e6b8d99a4d84302` | | |
| └ External/simdutf | `cd79a258af781b5fd92da3c671a2d679b97be227` | | |
| LEBinkProxy | `1c435d8df0367642d9fc8572b67b6ae92295967e` | 2024-06-02 | 0.5 MB |

Not cloned: UEVR and TF2VR (all rights reserved / closed), Legendary Explorer source (~690 MB;
use the release build).

Overlord's submodules (listed in its `.gitmodules`, not fetched) show its dependency stack:
minhook, lua, sol2, GSL, rapidjson, discord-rpc, stb, asmjit, udis86, WinToast, libtomcrypt,
libtommath, zlib, imgui (fork), curl, nlohmann json, gsc-tool, shader-tool, **KhronosGroup
OpenXR-SDK**, CascLib.

## Compatibility with an MIT-licensed mod

| Source | Can we copy code? |
|--------|-------------------|
| REFramework (MIT) | **Yes**, keeping its copyright notice |
| Overlord, h2-mod, LEX, Halcyon's MassEffect1/2/3-VR (GPL-3.0) | **No.** It would force GPL. Design reference only, or ask the author for an MIT grant |
| UEVR, TF2VR (all rights reserved) | **No.** Ideas only |
| LExASIs, LEBinkProxy (no license) | **No** by default. Ask ME3Tweaks; reading for patterns is fine |
| LExSDKv2 (deliberately none) | Consume as a submodule, as the whole LE native-mod scene does |

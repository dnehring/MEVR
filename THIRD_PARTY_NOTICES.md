# Third-party notices

MEVR's own code is MIT-licensed ([LICENSE](LICENSE)). It builds with, or incorporates, the
following third-party work.

| Component | Where | License / terms | How it's used |
|-----------|-------|-----------------|---------------|
| [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) (Khronos Group), release 1.1.63 | `external/OpenXR-SDK` | Apache-2.0 (see `external/OpenXR-SDK/LICENSE`) | OpenXR loader, statically linked into the mod; OpenXR headers |
| [LExSDKv2](https://github.com/llamathings/LExSDKv2) (`RVA` branch) | `external/LExSDKv2` | None specified by its author (see its README) | Generated UE3 / ME LE class layouts and engine addresses, statically linked. Consumed as a submodule, not vendored |
| [MassFPS (LE1)](https://www.nexusmods.com/masseffectlegendaryedition/mods/1731) by Aphar | `src/script/le1/massfps-base` | Author: "You may do whatever you want with this mod, no credit or permission needed" | Script-side first-person base. Credited with thanks |
| [REFramework](https://github.com/praydog/REFramework) (praydog) | (not yet copied) | MIT | Design reference so far. If code is copied, its MIT notice goes here |

Runtime dependencies that are not distributed with MEVR: the user's OpenXR runtime and Windows'
`d3dcompiler_47.dll`. The ASI loader is our own `bink2w64.dll` (`src/loader`, installed by
MEVR-Setup), or ME3Tweaks' LEBinkProxy if Mod Manager installed it (used, never shipped).
Mass Effect Modder and texture packs are downloaded only at the player's request (NOTICE.md).

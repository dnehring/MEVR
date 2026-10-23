# Notices

ME VR Attempt is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, version 3 of the
License ([LICENSE](LICENSE)). It is distributed WITHOUT ANY WARRANTY; see the license.

Copyright (C) 2026 ME VR Attempt contributors.

The project was MIT-licensed until 2026-10-08, when it moved to GPL-3.0 to build on Halcyon's
GPL VR mods.

## Licensing per file

- **The whole is GPL-3.0** (`LICENSE`, also `LICENSES/GPL-3.0-only.txt`): the host ASI links Halcyon's
  GPL-3.0 code, so the mod as built is a GPL-3.0 work.
- **Our original files are MIT** (`LICENSES/MIT.txt`, Copyright (c) 2026 ME VR Attempt contributors).
  Every source file says which applies on its first line: `SPDX-License-Identifier: MIT` or
  `SPDX-License-Identifier: GPL-3.0-only`. `python tools/check-spdx.py` lists files without one.
- **GPL-3.0-only, because they contain GPL code:** all of `src/halcyon/` (Halcyon's files and our seam
  header `mevr_bridge.h`), the Overlord-derived files in the table below (`src/core/ik/ArmPose.*`,
  `TouchReference.*`, `TwoHand.*`, `src/core/vr/Sights.*`, `src/engine/BonePose.cpp`,
  `src/game/GamepadMapping.*`) and `notes/upstream-halcyon/*.patch`. Neither Halcyon nor Overlord
  says "or later", so neither do we.
- **Third-party code keeps its own licence** (the table below): `third_party/`, `external/fsr1` and the
  submodules carry no identifier of ours.
- Docs and data files without an identifier (Markdown, notes, research) are MIT unless marked.

Code it contains from other projects keeps its own license and notices:

| Code | Where | License |
|------|-------|---------|
| Halcyon's [MassEffect1-VR](https://github.com/jrush64/MassEffect1-VR) (Copyright (C) 2026 Halcyon), and later its ME2/ME3 siblings | `src/halcyon/` (imported verbatim, then modified; see the README there) | GPL-3.0 |
| [Overlord](https://github.com/Aeka0/Overlord) (Aeka0): the arm orientation, elbow pole, subtree transfer and 1.25x arm stretch of `hands/pose_solver.hpp`, adapted | `src/core/ik/ArmPose.cpp`, `src/core/ik/ArmPose.hpp` (marked in the files) | GPL-3.0 |
| [Overlord](https://github.com/Aeka0/Overlord) (Aeka0): the canonical Touch controller frame of `touch_controller_reference.hpp` (offset and 20.6 degree pitch from the OpenXR grip), adapted | `src/core/ik/TouchReference.cpp`, `src/core/ik/TouchReference.hpp` (marked in the files) | GPL-3.0 |
| [Overlord](https://github.com/Aeka0/Overlord) (Aeka0): the support grip's acquire/release lease of `gameplay/support_grip.hpp` and its default distances, adapted | `src/game/GamepadMapping.cpp` (`UpdateSupportGrip`), `src/game/GamepadMapping.hpp` (marked at its declaration) | GPL-3.0 |
| [Overlord](https://github.com/Aeka0/Overlord) (Aeka0): the two-hand aim of `gameplay/weapon_profile.hpp` (`aimed_rotation`) and the retain-at-the-two-hand-pose and coincident-hands rules of `grip_presenter.hpp`, adapted | `src/core/ik/TwoHand.cpp`, `src/core/ik/TwoHand.hpp`, `src/engine/BonePose.cpp` (`ApplySupport`) (marked in the files) | GPL-3.0 |
| [Overlord](https://github.com/Aeka0/Overlord) (Aeka0): the critically damped, speed-limited eye-distance transition of `gameplay/ads_comfort.hpp` (`transition::update`), adapted from one distance along the barrel to a vector across the sight line (the sight alignment assist, research/21 section 8) | `src/core/vr/Sights.cpp` (`AssistSpring::Update`), `src/core/vr/Sights.hpp` (`AssistSpring`, `Assist`) (marked in the files) | GPL-3.0 |
| [OpenXR Toolkit](https://github.com/mbucchia/OpenXR-Toolkit) (Matthieu Bucchianeri): the idea of its post-processing contrast (a blend toward smoothstep), rewritten and applied per channel; no code copied | `src/core/vr/Colour.cpp` (research/27) | MIT (concept only) |
| [MinHook](https://github.com/TsudaKageyu/minhook) (Tsuda Kageyu), also compiled into our ASI loader `bink2w64.dll` (`src/loader`, research/33) | `third_party/minhook/` | BSD-2-Clause (`third_party/minhook/LICENSE.txt`) |
| [Dear ImGui](https://github.com/ocornut/imgui) (Omar Cornut) | `third_party/imgui/` | MIT (`third_party/imgui/LICENSE.txt`) |
| [Rajdhani](https://github.com/google/fonts/tree/main/ofl/rajdhani) (Copyright (c) 2014 Indian Type Foundry), SemiBold and Bold, unmodified | `third_party/fonts/` (deployed to `ASI\fonts`) | SIL OFL 1.1 (`third_party/fonts/Rajdhani-OFL.txt`) |
| [Titillium Web](https://github.com/google/fonts/tree/main/ofl/titilliumweb) (Copyright (c) 2009-2011 Accademia di Belle Arti di Urbino and students of MA course of Visual design), Regular and SemiBold, unmodified | `third_party/fonts/` (deployed to `ASI\fonts`) | SIL OFL 1.1 (`third_party/fonts/TitilliumWeb-OFL.txt`) |
| [RmlUi](https://github.com/mikke89/RmlUi) 6.3 (Copyright (c) 2008-2014 CodePoint Ltd, Shift Technology Ltd, and contributors; (c) 2019-2026 The RmlUi Team, and contributors): the UI library of our VR main menu, and its Direct3D 11 renderer (`Backends/RmlUi_Renderer_DX11.cpp`, compiled from the submodule, wrapped by `src/plugin/menu/MenuRenderer.cpp`) | `external/RmlUi` (submodule, tag 6.3) | MIT (`external/RmlUi/LICENSE.txt`) |
| [FreeType](https://freetype.org) 2.13.3, RmlUi's font engine. Portions of this software are copyright (c) 2024 The FreeType Project (www.freetype.org). All rights reserved. | `external/freetype` (submodule, tag VER-2-13-3) | The FreeType License (FTL, chosen of its FTL / GPLv2 dual licence; `external/freetype/docs/FTL.TXT`) |
| [AMD FidelityFX Super Resolution 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) (Copyright (c) 2021 Advanced Micro Devices, Inc.; `ffx_a.h` also Copyright (c) 2014 Michal Drobot for the float approximations): the shader headers `ffx_a.h` and `ffx_fsr1.h`, unmodified, embedded at build time and compiled at run time for the eye upscale (core/vr/Upscale, plugin/Upscale, research/32) | `external/fsr1/` | MIT (`external/fsr1/LICENSE.txt`) |
| [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) | `external/OpenXR-SDK` (submodule) | Apache-2.0 |
| [LExSDKv2](https://github.com/llamathings/LExSDKv2) | `external/LExSDKv2` (submodule; being replaced by `src/ue3/`, research/34) | None given (see its README). **Distributing a build needs its author's permission** while it is compiled into the ASI (research/33) |
| Prior art, no code taken: [LExSDKv2](https://github.com/llamathings/LExSDKv2) (llamathings and ME3Tweaks contributors) and TheFeckless' UE3 SDK generator, which showed that UE3 games can be driven through their own reflection. Our reflection layer is written from our own analysis of the engine (Ghidra and the engine's own reflection data, research/34); LExSDKv2's compiled offsets are used only as a run-time cross-check while both exist (`src/engine/ReflectCheck.cpp`) | `src/ue3/` | MIT (ours) |

[MassFPS (LE1)](https://www.nexusmods.com/masseffectlegendaryedition/mods/1731) by Aphar (Nexus mod
1731) was studied locally; its files aren't in the repository. Its author allows any use ("do
whatever you want", no credit required); we credit Aphar with thanks.

Runtime dependencies that are not distributed with ME VR Attempt: the user's OpenXR runtime and
Windows' `d3dcompiler_47.dll`. The ASI loader is our own `bink2w64.dll` (`src/loader`, installed by
MEVR-Setup), or ME3Tweaks' LEBinkProxy if Mod Manager installed it (used, never shipped).

## The release package and the setup (research/33)

`MEVR-Setup.exe` (`tools/setup`), the install library (`src/install`) and the ASI loader (`src/loader`) are this
project's own code (MIT source files). The loader is new code: it contains no ME3Tweaks LEBinkProxy code (LEBinkProxy has no
licence and was only read for how it behaves). It forwards the game's Bink calls to RAD's DLL, which is never shipped:
the setup renames the player's own copy to `bink2w64_original.dll`.

The release ships nothing of anyone else's but the components in the table above (in the ASI, the loader, the setup and
the fonts folder). Downloaded by the setup only when the player asks, never bundled:

| What | Where it comes from | License / terms |
|------|---------------------|-----------------|
| [Mass Effect Modder](https://github.com/MassEffectModder/MassEffectModder) (`MassEffectModderNoGui`), run as a separate program to install texture packs | its GitHub releases, after the player confirms | GPL-3.0 |
| Texture packs (ALOT for ME1, Improved Static Lighting, LEUITM...) | their own Nexus Mods pages, downloaded by the player | their authors' terms: no re-uploads |

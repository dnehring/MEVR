# Notices

ME VR Attempt is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, version 3 of the
License ([LICENSE](LICENSE)). It is distributed WITHOUT ANY WARRANTY; see the license.

Copyright (C) 2026 ME VR Attempt contributors.

The project was MIT-licensed until 2026-10-08, when it moved to GPL-3.0 to build on Halcyon's
GPL VR mods. Code it contains from other projects keeps its own license and notices:

| Code | Where | License |
|------|-------|---------|
| Halcyon's [MassEffect1-VR](https://github.com/jrush64/MassEffect1-VR) (Copyright (C) 2026 Halcyon), and later its ME2/ME3 siblings | `src/halcyon/` (imported verbatim, then modified; see the README there) | GPL-3.0 |
| [Overlord](https://github.com/Aeka0/Overlord) (Aeka0): the arm orientation, elbow pole, subtree transfer and 1.25x arm stretch of `hands/pose_solver.hpp`, adapted | `src/core/ik/ArmPose.cpp` (marked in the file) | GPL-3.0 |
| [Overlord](https://github.com/Aeka0/Overlord) (Aeka0): the canonical Touch controller frame of `touch_controller_reference.hpp` (offset and 20.6 degree pitch from the OpenXR grip), adapted | `src/core/ik/TouchReference.cpp` (marked in the file) | GPL-3.0 |
| [Overlord](https://github.com/Aeka0/Overlord) (Aeka0): the support grip's acquire/release lease of `gameplay/support_grip.hpp` and its default distances, adapted | `src/game/GamepadMapping.cpp` (`UpdateSupportGrip`, marked in the header) | GPL-3.0 |
| [Overlord](https://github.com/Aeka0/Overlord) (Aeka0): the two-hand aim of `gameplay/weapon_profile.hpp` (`aimed_rotation`) and the retain-at-the-two-hand-pose and coincident-hands rules of `grip_presenter.hpp`, adapted | `src/core/ik/TwoHand.cpp`, `src/engine/BonePose.cpp` (`ApplySupport`) (marked in the files) | GPL-3.0 |
| [OpenXR Toolkit](https://github.com/mbucchia/OpenXR-Toolkit) (Matthieu Bucchianeri): the idea of its post-processing contrast (a blend toward smoothstep), rewritten and applied per channel; no code copied | `src/core/vr/Colour.cpp` (research/27) | MIT (concept only) |
| [MinHook](https://github.com/TsudaKageyu/minhook) (Tsuda Kageyu) | `third_party/minhook/` | BSD-2-Clause (`third_party/minhook/LICENSE.txt`) |
| [Dear ImGui](https://github.com/ocornut/imgui) (Omar Cornut) | `third_party/imgui/` | MIT (`third_party/imgui/LICENSE.txt`) |
| [Rajdhani](https://github.com/google/fonts/tree/main/ofl/rajdhani) (Copyright (c) 2014 Indian Type Foundry), SemiBold and Bold, unmodified | `third_party/fonts/` (deployed to `ASI\fonts`) | SIL OFL 1.1 (`third_party/fonts/Rajdhani-OFL.txt`) |
| [Titillium Web](https://github.com/google/fonts/tree/main/ofl/titilliumweb) (Copyright (c) 2009-2011 Accademia di Belle Arti di Urbino and students of MA course of Visual design), Regular and SemiBold, unmodified | `third_party/fonts/` (deployed to `ASI\fonts`) | SIL OFL 1.1 (`third_party/fonts/TitilliumWeb-OFL.txt`) |
| [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) | `external/OpenXR-SDK` (submodule) | Apache-2.0 |
| [LExSDKv2](https://github.com/llamathings/LExSDKv2) | `external/LExSDKv2` (submodule) | None given (see its README) |
| MassFPS (Aphar) | `src/script/le1/massfps-base/` | "Do whatever you want", no credit required (credited anyway) |

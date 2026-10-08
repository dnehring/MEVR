# 05: VR frameworks

## UEVR (praydog)

- Supported engines: **UE 4.8–5.4** per the GitHub README. UploadVR says 4.8 and above, including
  UE5. Mixed-News frames it as "almost any UE4 and UE5 flatscreen title". **No UE3 support found.**
  [UEVR GitHub, UploadVR, Mixed-News]
- **Method:** forces Unreal's built-in VR rendering path and hooks engine entities (camera, player
  controller). The feature list includes a "Native UE4/UE5 stereo rendering system". UE3 has no
  equivalent built-in stereo path to force. [UploadVR]
- **License: all rights reserved.** The LICENSE file reads "Copyright (c) 2022-2025 praydog. All
  rights reserved." (GitHub reports NOASSERTION.) [UEVR LICENSE via GitHub API]
- **Community:** a compatibility spreadsheet maintained by Alex Nicholas; profiles shared on the
  Flat2VR Discord and a profiles website. [ispr.info, Mixed-News]
- **For us:** ideas only. Not cloned into `reference/` (all rights reserved; still true now that we're GPL-3.0).

## REFramework (praydog)

- **MIT.** A modding framework for Capcom's RE Engine with a full VR mod. [GitHub API]
- VR module layout (from the repo tree, `master`, 2026-10-07):
  - `src/mods/VR.cpp`, `src/mods/VR.hpp`: the VR mod.
  - `src/mods/vr/D3D11Component.{cpp,hpp}`: D3D11 swapchain and eye texture plumbing.
  - `src/mods/vr/D3D12Component.{cpp,hpp}`, `src/mods/vr/d3d12/`
  - `src/mods/vr/OverlayComponent.{cpp,hpp}`: overlays and UI. **OpenVR-only**; the OpenXR path
    has no quad or cylinder layers ([10](10-vr-code-survey.md)).
  - `src/mods/vr/runtimes/OpenXR.{cpp,hpp}`, `OpenVR.{cpp,hpp}`, `VRRuntime.hpp`: runtime
    abstraction.
  - `src/mods/vr/Bindings.cpp`, `src/mods/vr/games/`
- Overlord acknowledges REFramework as an upstream.
- **For us:** the primary **code** source for OpenXR plus D3D11 plumbing, used with attribution
  (keep the MIT notice).

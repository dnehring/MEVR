<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Halcyon ME3 renderer import

Upstream: https://github.com/jrush64/MassEffect3-VR

Pinned commit: `cfd40019450d3469b44a9b155f03e0263a61f787` (Copyright (C) 2026 Halcyon, GPL-3.0).

The preceding import commit copied upstream's `src/` verbatim. SPDX annotations and `[MEVR]` host seams were added only afterward. The upstream namespace `ME2VR`, `me2_*.cpp` filenames and historical ME2 comments are intentional: this is the ME3-specific upstream source, not a duplicate of our ME2 fork. `dllmain.cpp` and `MELE3VR.vcxproj` are preserved as provenance, not used by the host build. Dependencies use this repository's existing packages.

The ASI has one renderer and one OpenXR session. `dxgi_proxy.cpp` hooks the real factory exports; no proxy DLL entry point is linked. `mevr_bridge.h` connects the renderer to the existing controller actions, haptics, floor/view publication, pad mapping, HDR/colour copy and snapshot/record/replay machinery. The public facade lives in `plugin/RendererLe3.cpp`.

UnrealScript setters queue through `plugin/GameThreadCalls` when called off the game thread. Focus-gated hotkeys and the process-local OpenXR override follow LE1/LE2. Native head aim and experimental first-person/VR cinematics are disabled in the ASI; ordinary on-foot first person and native flat cinema remain upstream paths. Authored conversations/cinematics reject the synthetic movement stick. No shared menus, gestures, HUD labels, scopes, voice or Thespian callbacks are claimed by this foundation.

See `research/le3-renderer-integration.md` for the source contract and outstanding proof gates. The bridge is source work; compilation, native camera/eye/controller agreement, clean session lifecycle and Quest 3/Virtual Desktop acceptance are separate gates.

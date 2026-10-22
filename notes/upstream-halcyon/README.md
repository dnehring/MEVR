# Fixes to offer upstream (Halcyon's MassEffect1-VR)

These are bugs in Halcyon's code that we fixed in `src/halcyon/le1`. Both projects are GPL-3.0,
so the fixes can go back as merge proposals. **Nothing has been sent:** ask the user before
opening issues or pull requests.

Upstream checked: `reference/MassEffect1-VR` at 3c51514 (2026-09-30), still the latest on
2026-10-21.

## 1. The menu ignores every gamepad (ready)

- **Patch:** `0001-menu-report-gamepad-to-imgui.patch`. It applies cleanly to upstream
  (`git apply --check`), with paths mapped from `src/halcyon/le1/` to upstream's `src/`.
- **Bug:** `vr_menu.cpp` sets `ImGuiConfigFlags_NavEnableGamepad` but never
  `ImGuiBackendFlags_HasGamepad`. Without that flag, ImGui 1.92 clears every
  `ImGuiKey_Gamepad_*` key in `NewFrame` and gamepad navigation stays off. Its ME1 menu therefore
  can't be driven by any pad (Xbox, or Touch through SteamVR's emulation), only by mouse and
  keyboard.
- **Fix:** one line in `ApplyGamepadInput`. Its ME2 and ME3 mods don't enable pad navigation, so
  only ME1 is affected.

## 2. Windows HDR on: blue headset image (needs extracting)

- **Our commit:** 9f959d7.
- **Bug:** with Windows HDR on, the backbuffer is FP16 (scRGB), and Halcyon's eye copy used a
  CopyResource between mismatched formats, so the headset showed a blue screen. The fix recreates
  the blit source per format and uses sRGB eyes.
- **To do before offering it:** the commit also contains our own `[MEVR]` changes in
  `xr_session.cpp` (about 500 lines in total). Split out the HDR-only hunks into a patch against
  upstream. Upstream's installer asks players to turn HDR off; the fix makes that unnecessary.

## 3. World-locked flat screen and menu (a feature to offer, not a bug fix)

- **Our change:** the `PlacePanel` seam (2026-10-21, `mevr_bridge.h`, `src/plugin/WorldPanels.cpp`,
  rules in `src/core/vr/OverlayPlacement` `WorldPanel`, unit-tested).
- **Upstream behaviour:** the flat screen (menus, galaxy map, flat conversations, cutscenes and
  movies) and the Insert menu are quads in VIEW space, so they ride the head, with no parallax.
- **What it does:** with `worldLockedPanels = 1`, each is placed in the app space in front of the
  head when it appears (yaw only, level, at upstream's distance and offsets). It stays there until
  it hides, its kind changes, the player recenters, or the head stays turned past `panelFollowDeg`
  (70°) for a second. A user asked for exactly this (the intro's screen rode the head).
- **To do before offering it:**
  - The seam calls into our host. For upstream, fold `WorldPanels.cpp` and the `WorldPanel` class
    into its `xr_session.cpp` (about 150 lines, no dependencies beyond its own quaternion helpers)
    and keep the setting default off, so its players see no change unless they opt in.
  - Its menu has no pointer, so the menu part needs no pointer change there.
  - Ask the user first.

## Possibly later

- Its F1-F4 profile hotkeys clash with the game's F-keys (we turn them off in our deploy).
- Profile slots that are missing from `MELEVR.ini` load as code defaults, the menu key included;
  a player without an Insert key can then lose the menu.
- The installer's settings (borderless near-square target) are required for stereo. A bordered
  window is silently capped at the monitor's height. A log warning when `[TARGETCHK]` reports
  MISMATCH because of window borders would help players.

# 42: A laser pointer on the game's own flat menus (2026-10-31)

Goal: in LE1's VR, the game's own screens (inventory, journal, the area map, the galaxy map, the pause menu,
the loading and save screens) are shown by Halcyon's renderer on a flat quad in front of the player. Until now
they were driven by the pad alone (the Touch controllers as Xbox pad 0, [12](12-vr-frameworks-survey.md),
`game/GamepadBridge`). This note describes the **game pointer**: a controller's aim ray hitting that quad becomes the
game's MOUSE (the hit's UV mapped to a pixel of the game window's client area), the trigger is the left mouse
button, the right stick is the wheel, and a dot on the quad marks the hit. Host side (`src/plugin/GamePointer.cpp`
over `src/core/vr/GamePointer.cpp`), LE1 only, `[Pointer]` in `ASI\LE1MEVR.ini`, log `game pointer:`.

The template was our pointer for Halcyon's ImGui menu ([28](28-main-menu.md); `src/plugin/MevrMenuPointer.cpp`,
`core/vr/MenuPointer`): the same ray-quad intersection, the same trigger latch with a dead zone, the same
"which hand points" rule. What is new is the target (the game's flat screen instead of our own quad) and the
receiver (the game's window procedure and the OS cursor instead of ImGui's `io.MousePos`).

## 1. How Halcyon places the flat screen

Halcyon's per-frame policy (`xr_session.cpp`, `CineFinishDecision`) sets `forceFlatCine` from the game mode byte
and its settings: mode 7 (GUI, a full-screen front-end menu) with `menuFlat`, unless the mode-7 frame is really a
live in-world GUI; mode 9 (the galaxy map) with `galaxyFlat`; a conversation or cutscene whose VR presentation is
off; a Bink movie. On such a frame the whole backbuffer is copied into both eye swapchains (stretched to the eye
image's size when they differ, `CopyTextureToEyeFullFrame`) and submitted as ONE `XrCompositionLayerQuad` instead of
the projection layer: `flatCineQuad`, 1.4 m ahead in VIEW space, `menuMonoQuadWidth` wide (2.35 m by default, clamped
to 1..3.5 m; the galaxy map, conversations and cutscenes have widths of their own), its height the width over the
backbuffer's aspect (`flatCineQuadAspect`; 16:9 if the aspect is implausible). With `worldLockedPanels` on, our
`MevrBridge::PlacePanel` ([research/28](28-main-menu.md), `src/plugin/WorldPanels.cpp`) fixes that quad in the app
space where it appeared instead (yaw only, level, placed afresh when it reappears or its kind changes, or once the
head has stayed turned past `panelFollowDeg`).

Because the quad shows the backbuffer edge to edge, a point at UV (u, v) on it (u from the left, v from the top) is
the backbuffer pixel (u × width, v × height), which is the game window's client pixel: the backbuffer is the client
area. That is the whole mapping. The quad's aspect follows the backbuffer's, so nothing is letterboxed.

The seam: right after the panel is placed, `xr_session.cpp` calls `MevrBridge::OnFlatQuad(kind, head, placed, width,
height, headValid)` (the one `[MEVR]` addition for this feature, declared in `mevr_bridge.h` next to `OnMenuQuad`).
`plugin/GamePointer.cpp` brings the quad into the app space (`menu_pointer::InTracking`: VIEW-relative when it is
head-locked, as it is when it is fixed in the room) and stamps the time. A quad not submitted for 250 ms counts as
gone, as the menu pointer's does.

## 2. The pointer (core/vr/GamePointer, unit-tested)

`game_pointer::Pointer::Step(quad, input)` each frame:

- Both hands' aim rays are intersected with the quad's front (`menu_pointer::Intersect`). The pointing hand is
  kept while its ray is on the quad or it holds a press; otherwise the other hand takes over when it is on the
  quad and ours isn't, or when it pulls its trigger on the quad while ours doesn't. The right hand starts.
- The hit's UV is smoothed (half way per frame, hand tremor; none on arrival) and turned into a client pixel
  (`ToPixel`: floor of u × width, clamped). A move is reported only when the pixel differs from the last reported
  one by more than `deadBandPx` (1 px): the game flips its glyphs from pad to mouse on mouse input, so a steady hand
  mustn't keep "moving" the mouse. Off the quad nothing moves.
- The trigger is a latch: closed at 0.6, opened below 0.4 (the menu pointer's values). A press starts only on the
  quad and lasts until the trigger lets go, even when the ray slides off (a drag past the edge): the button up
  always follows the button down, at the last point sent. Controllers lost, the quad gone, or `Release()`: the
  press is let go with one button up.
- The right stick (outside a 0.2 dead zone) accumulates wheel notches at `wheelPerSecond` (6 at full deflection);
  whole notches are reported as they come; letting go drops the fraction.
- `PointOnQuad` and `DotSize` place the dot: the hit point in the app space, and the edge length that looks
  `dotDeg` (0.6 degrees) across from the head.

## 3. How the mouse reaches the game

UE3 on Windows takes the mouse two ways, and which one each screen listens to isn't known until the headset says
(section 5): the window procedure's `WM_MOUSEMOVE` / `WM_LBUTTONDOWN` / `WM_LBUTTONUP` / `WM_MOUSEWHEEL`, and a per-
tick `GetCursorPos` poll of the OS cursor (which the Scaleform GFx movies' cursor follows). Each is a setting and
the log says what is sent:

- `[Pointer] moveMessages = 1`: `PostMessageW` to the game's window (the swapchain's output window, which Halcyon
  already hands us for the foreground test: `MevrBridge::SetGameWindow`, now also `GameWindow()`), client
  coordinates in `lParam`, `MK_LBUTTON` while the trigger is held. `WM_MOUSEWHEEL` carries SCREEN coordinates and
  `notches × WHEEL_DELTA` in the high word of `wParam`. Posted messages pass Halcyon's subclassed window procedure
  (`vr_menu.cpp MenuWndProc`) untouched while its menu is closed, which is the only time we send.
- `[Pointer] setCursor = 1`: `SetCursorPos` to the client point mapped to the screen (`ClientToScreen`). Halcyon
  hooks `SetCursorPos` too (its menu's input block) and passes it through while its menu is closed. The OS clamps
  the cursor to the real screen: with Halcyon's display-size spoof the window can hang off the desktop, and then
  the bottom or right of the quad is unreachable by this path (the messages path has no such limit). The log's
  first-hit line names the client size so this can be checked against the monitor.
- `[Pointer] sendInput = 0`: with 1, clicks and the wheel go through `SendInput` (real input: it also sets the
  asynchronous key state a `GetAsyncKeyState(VK_LBUTTON)` poll reads), which reaches the FOREGROUND window only;
  the host checks the game's window is in front, else posts the click and warns once. Off by default: `SendInput`
  events are also seen by Halcyon's low-level mouse hook (`MenuMouseLLProc`), harmless while its menu is closed,
  and by any other program in front.

Nothing is sent while the ray is off the quad (apart from a button up ending a press begun on it), while our main
menu or Halcyon's is open (they own the pointer; the mod sees no controllers then anyway), while the guided
calibration runs, or while the flat screen is a conversation, cutscene or movie unless `[Pointer] flatCine = 1`.
`[Pointer] gameMenus = 0` turns the feature off; `wheel = 0` the stick's wheel; `dotDeg` sizes the dot.

The controllers keep reaching the game as the pad at the same time (`game/GamepadBridge`): A still activates, the
sticks still move the pad's focus, the right trigger is still RT. Nothing takes the pad from the game while the
pointer is used; the game sees both a mouse and a pad, as it does on a desktop with both plugged in.

## 4. The dot

`HalcyonOverlays.cpp` asks `game_pointer::Step` every frame (it must run on frames without a flat screen too, to
release what it holds) and, when it has a hit, submits a 64 × 64 quad (a light blue disc over a half-transparent dark
rim, premultiplied, one static image like the hand reticle's) at the hit point, 5 mm in front of the quad and facing
like it, `dotDeg` across as seen from the head, a fifth smaller while the trigger is held. It is a layer after the
flat screen's quad, so it composes above it. Snapshots know it as the `gamepointer` layer and `"gamePointer"` in
the JSON (`on`, `flat`, `kind`, `worldLocked`, `quadM`, `client`, `hit`, `hand`, `px`, `uv`, `pressed`, the counts of
moves, clicks and notches sent this appearance).

## 5. What to check in the headset

Not verifiable without the game (the host side only: a redeploy, since nothing of this is in the hot-reloaded mod):

1. Open the inventory (or the pause menu) in VR. The log should say `game pointer: the flat screen is up (the game's
   menus, 2.35 x 1.32 m, fixed in the room)`, then on pointing at it `the right controller's ray is on the game's
   menus at (x, y) px of the WxH window, 1.40 m away; sending WM_MOUSEMOVE and SetCursorPos`. The dot should sit
   where the ray meets the picture, and the game's own cursor (if its screen shows one) under the dot.
2. Does the hover highlight follow the dot? If yes with both paths on, turn one off at a time (`moveMessages = 0`,
   then `setCursor = 0`) to learn which one the game reads; record it here. If the highlight follows only the
   cursor path and the bottom of the quad is unreachable, the display spoof is in the way (section 3).
3. Does the trigger click? `game pointer: the trigger clicked the game's menu at (x, y) px (WM_LBUTTONDOWN posted)`.
   If the cursor moves but no click lands, the screen polls the button state (`GetAsyncKeyState`): try `sendInput = 1`
   with the game window in front (the bridge brings it to the foreground when the headset has focus). If that works
   and the messages don't, the next step would be a hook on the poll rather than real input.
4. The wheel on a long list (the journal, the codex): `the right stick scrolled the game's menu`. If the stick also
   moves the pad's focus in that screen, `wheel = 0` or a lower `wheelPerSecond`.
5. The galaxy map (mode 9): click a system, drag to pan if the game allows it (the press is held while the ray
   slides off the quad), scroll to zoom.
6. The glyphs: the first mouse move flips the screen's button prompts to the keyboard/mouse set; A on the pad
   should flip them back. Acceptable; noted so nobody reports it as a bug.
7. Our main menu over a game screen: with both sticks held the game pointer must stop (no dot, no moves) and the
   log says `the press was let go (a menu of ours or the calibration took the controllers)` if the trigger was down.
8. Snapshot while pointing: `"gamePointer"` shows `hit: true`, the pixel, and the `gamepointer` layer in the list.

## 6. Out of scope, noted

- **The dialogue wheel.** In a conversation rendered in VR (`cineVrConvo`, the default) the UI is in the eye images,
  not on a flat quad: there is no quad to hit and no UV to map, so the pointer does nothing there by design. A
  flat conversation (`cineVrConvo = 0`) does put the wheel on the flat screen; `[Pointer] flatCine = 1` points at it,
  untested. A pointer for the VR dialogue wheel would need the wheel's screen position projected from the eye's
  view (as the HUD panel's wheel crop does for the weapon wheel, [15](15-hud.md)) and is a separate job.
- The game's in-world GUIs on a mode-7 frame that Halcyon keeps in VR (the Eden Prime bomb disarm, `liveWorldGui`)
  are not flat frames either.
- LE2 and LE3 (our own renderer, no flat quad of this kind yet).
- Whether UE3 LE's menus take `WM_MOUSEMOVE`, the cursor poll, or both is **[verify]** until the headset check in
  section 5 answers it; the two paths exist so the answer is a setting, not a rebuild.

## Sources

- Halcyon's `xr_session.cpp` (the flat screen's placement and copy), `vr_menu.cpp` (its input block: the subclassed
  window procedure, the `SetCursorPos` hook, the low-level hooks), GPL-3.0, in `src/halcyon/le1`.
- Our `src/plugin/MevrMenuPointer.cpp` and `core/vr/MenuPointer` ([28](28-main-menu.md)); `src/plugin/WorldPanels.cpp`.
- Win32: `WM_MOUSEWHEEL` takes screen coordinates and `WHEEL_DELTA` multiples; `SendInput` targets the foreground
  window; `SetCursorPos` is clamped to the virtual screen (Microsoft Learn, see [sources.md](sources.md)).

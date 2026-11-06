---
type: Gotcha
title: The OpenXR runtime limits a session's swapchains, and ours used to give up on the first refusal
description: SteamVR answers xrCreateSwapchain with XR_ERROR_LIMIT_REACHED (-10) at about 16 in a session; each quad of ours made its own, never freed it and stopped trying, so one refusal knocked out a dozen features in turn.
tags: [openxr, steamvr, overlays, host]
status: stable
timestamp: 2026-11-06T18:30:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-06T18:30:00Z }
bitten: [2026-11-06]
---

# Symptom

Features of ours that draw a quad stop showing one after another in a long session, each for the rest of it. The
player's words: on the Normandy "the aiming steps showed no targets" (the calibration's board); "I see the option
appear, I feel the vibration, but I can't open it" (the omni-tool's wrist tap fired, the rail never showed). In the
same session the sight's reticle and window ring, the scope picture, the galaxy map's hologram, the squad arc, the gun
status, the wrist's fan of light and the game pointer's dot were gone too. Each logged once, then nothing:
`... swapchain ... failed (XrResult -10)`.

# Cause

xrCreateSwapchain returns XR_ERROR_LIMIT_REACHED when the runtime's own limit is reached; the spec leaves the number to
the runtime. On SteamVR it was reached with about 16 live in the session (Halcyon's 4: two eyes, its menu, its black
scene; then ours: the HUD panel, label, wrist status, reticle, power wheel, its hint, the main menu, the calibration's
panel, orb and hand, and the tap target's first looks) [verify: `tools/swapchain-probe`]. Every owner of ours made its
swapchain the first time it showed, kept it for the session, and on a refusal latched `failed` for the session: nothing
was ever freed, and nothing refused ever asked again.

# Check

The log: `swapchains: <name> WxH refused (XrResult -10, XR_ERROR_LIMIT_REACHED ...)` names every swapchain of ours that
is live and how long ago each was shown; `swapchains: freed ...` says what made room. A snapshot's JSON has
`"swapchains"` (ours, Halcyon's seen, refusals, frees, each live one's idle time). Before 2026-11-06 the line was the
owner's own: `<module>: ... swapchain ... failed (XrResult -10)`.

# Fix in place

Every swapchain of ours goes through `plugin/Swapchains` (rules in `core/vr/SwapchainBudget`, unit-tested): a refused
create asks for room, and at that frame's end (after xrEndFrame, when no frame holds an image) the least recently shown
of ours not shown for a second are freed through their owners' own paths; the refused owner asks again on the next
frame, else every 2 s, never giving up for the session. The calibration's creates are Essential: they may also free
ours shown 0.1 s ago. A new quad must make its swapchain with `swapchains::Create`, let go of it in its `evict`
(redrawing a static picture when it's made again), and never latch a refusal for the session. Fewer swapchains is
still better: the tap target's four looks share one (d375fdc); a static picture can be a rect of a shared image.

# Citations

[1] [plugin/Swapchains.hpp](../../../src/plugin/Swapchains.hpp): the header.
[2] [core/vr/SwapchainBudget.hpp](../../../src/core/vr/SwapchainBudget.hpp) and its tests (tests/SwapchainBudgetTests.cpp).
[3] [research/25 section 10](../../../research/25-calibration.md): the calibration's board refused (2026-11-06).
[4] The OpenXR 1.1 reference page for xrCreateSwapchain: XR_ERROR_LIMIT_REACHED is a listed failure, the limit the runtime's.

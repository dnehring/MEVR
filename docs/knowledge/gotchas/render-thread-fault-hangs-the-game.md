---
type: Gotcha
title: A fault inside a render-thread hook hangs the game without a crash
description: An access violation in our render-thread code is caught by the engine's own handler behind the hook; the render thread ends and the game thread waits forever: no crash dialog, no breadcrumb file, only first-chance log lines.
tags: [engine, threading, render-thread, hooks, crash]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-25]
---

# Symptom

The picture stands still, the process stays alive, no crash record is written. The log ends without an error, except
the first-chance lines `CrashBreadcrumbs` writes (three per address).

# Cause

Inside a hooked render-thread function the engine's own exception handler sits outside ours: it ends the thread, and
the game thread's wait on the render thread never returns. The trigger seen: reading `FSceneRenderer`'s views after
`Render` returned, when the renderer's view array is already freed (2026-10-25, the one-Draw design's replay-proof
offset).

# Check

The first-chance lines in `LE1MEVR.log` (the address names the hook); the window frozen with the process alive; the
breadcrumbs file not updated, because the unhandled-exception filter never ran.

# Fix in place

Read renderer data inside the render (a pass or finish hook) or at the renderer's deleter (after the render, before the
free). Wrap render-thread work in SEH that disables the feature for the session instead of faulting
(`ReplayDrawsSEH`, `ApplyReplayProofOffsetSEH`). `CrashBreadcrumbs` logs first-chance exceptions so a silent hang
leaves a trace.

# Citations

[1] [research/37, "The one-Draw design"](../../../research/37-single-pass-stereo.md): the view array freed after Render, the SEH wrappers.
[2] [research/36](../../../research/36-crash-breadcrumbs.md): the breadcrumbs and the first-chance log.

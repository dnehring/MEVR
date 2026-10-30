---
type: Gotcha
title: Never run UnrealScript off the game thread
description: ProcessEvent, or anything that runs script, from the Present or render thread races the game thread's script VM, whose state lives in process-wide globals; the 2026-10-23 crash.
tags: [engine, threading, crash, process-event]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-23]
---

# Symptom

An access violation inside the script VM with no obvious cause. The crash breadcrumbs
(`%LOCALAPPDATA%\MEVR\crash-breadcrumbs.txt`) show a ProcessEvent on a thread other than the game thread; or, before
anything crashes, the log line `process event: off the game thread: <fn>`.

# Cause

The script VM keeps its state in process-wide globals (GPropAddr, GRuntimeUCFlags, ...). A ProcessEvent from the
Present/render thread runs script at the same time as the game thread's, and the two corrupt each other's state. The
2026-10-23 crash was Halcyon's Present-thread hides (`head_aim.cpp`, `game_camera.cpp`, `ProcessEventSEH`).

# Check

The host's ProcessEvent hook logs any call off the game thread. After a crash, the breadcrumbs name the faulting
thread, its native stack, its script frames and the last 128 ProcessEvents.

# Fix in place

Off-thread calls are queued for the next tick: Halcyon's `ProcessEventSEH` goes through the `[MEVR]` seam
`MevrBridge::QueueProcessEvent` into `plugin/GameThreadCalls`. Our own code calls ProcessEvent only from the mod's tick
or from handlers the engine calls on the game thread. Hooks stack: the host hooks `UObject::ProcessEvent` at attach and
never removes it, Halcyon's MinHook sits on top, so never uninstall a hook another hooker may sit on.

# Citations

[1] [research/36, "ProcessEvent off the game thread: detection and the fix"](../../../research/36-crash-breadcrumbs.md).

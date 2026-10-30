---
type: Gotcha
title: LExSDKv2's generated function wrappers corrupt the UFunction's flags
description: All 5,301 LE1 native wrappers do `FunctionFlags |= ~0x400` (meant `&=`), which permanently sets every flag on that UFunction; never call them.
tags: [lexsdk, engine, process-event]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-07]
---

# Symptom

A UFunction behaves strangely after a wrapper call: every bit of its `FunctionFlags` is set, whatever the engine then
reads from them.

# Cause

The wrapper in `*_functions.cpp` does `pFn->FunctionFlags |= ~0x400;` before ProcessEvent and `|= 0x400` after; the
intent was `&= ~0x400` (clear the native flag for the call).

# Fix in place

Never call the generated wrappers (e.g. `USceneCaptureComponent::SetFrameRate`). Find the UFunction and call
ProcessEvent directly, through our reflection layer (`ue3::FunctionRef`, `ue3::Params`, `ue3::runtime::Call`). Class
layouts, `StaticClass()` and `IsA()` are fine. LExSDKv2 is on its way out anyway (no licence): the release build links
none of it, and no new use goes into an LE1 code path.

# Citations

[1] [research/09, the ME3Tweaks code survey](../../../research/09-me3tweaks-code-survey.md): the wrapper note.
[2] [research/34, reflection](../../../research/34-reflection.md): the layer that replaces it.

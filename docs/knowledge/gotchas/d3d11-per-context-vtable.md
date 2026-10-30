---
type: Gotcha
title: d3d11 swaps per-context vtable entries, so a vtable or single-body hook misses calls
description: d3d11.dll keeps a vtable per device context and switches its draw, clear, copy and dispatch entries between variants of each method as the context's state changes; a hook in that vtable or on one variant's body hears only some calls.
tags: [d3d11, hooks, draw-replay]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-25]
---

# Symptom

A hook on `ID3D11DeviceContext::Draw*` (or a Clear, Copy or Dispatch) counts far fewer calls than the frame draws; the
recorded tail misses draws. What looked like an "overlay" drawn around our hook was d3d11's own per-context vtable.

# Cause

d3d11 keeps a vtable per context and rewrites its entries as the context's state changes: a method has several variant
bodies and the entry points at the variant for the current state. A hook installed in one context's vtable, or on one
variant's body, misses the other variants.

# Fix in place

The recorder (`plugin/DrawReplay`) hears the draws through Halcyon's own hooks (`[MEVR]` seams `MevrBridge::Record*`)
and hooks the other entries inline per variant. New D3D11 hooks go the same way.

# Citations

[1] [research/37, 2026-10-25 01:58, experiment 7](../../../research/37-single-pass-stereo.md).

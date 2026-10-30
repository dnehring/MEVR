---
type: Gotcha
title: The engine's dynamic vertex and index rings wrap inside a recorded frame
description: UE3's 4 MB dynamic vertex and index buffers wrap with a DISCARD map inside the recorded tail on 4-5% of quiet frames and far more in combat; a replay of the draws before the wrap reads the post-wrap contents.
tags: [d3d11, draw-replay, rendering]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-25]
---

# Symptom

Under the draw replay (`[Performance] sharePass` 2, 3 or 5) the right eye strobes: a full-screen fog quad or other
dynamic geometry drawn from garbage on some frames.

# Cause

The dynamic rings are written with NO_OVERWRITE maps (about 336 a frame) and wrap with a DISCARD map. A wrap inside the
tail means the replayed draws before it would read what was written after it.

# Check

The levers line (`render levers:`): `large buffers discarded inside the tail: N frames (M discards, K shadowed)`. An
unshadowed one blocks the frame.

# Fix in place

The recorder copies such a buffer on the GPU into a shadow just before the discard, and the replay binds the shadow for
the draws before the wrap.

# Citations

[1] [research/37, 2026-10-25 15:08-15:50, the strobing right eye](../../../research/37-single-pass-stereo.md).

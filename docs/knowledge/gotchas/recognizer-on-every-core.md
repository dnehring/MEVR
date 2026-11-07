---
type: Gotcha
title: A library's ONNX Runtime session runs on every core and spins unless told otherwise
description: Moonshine passes no thread count to ONNX Runtime, so each partial transcript took a pool of every core, spinning between ops, for 200-300 ms, and the game lost about 2 s of frames per spoken order; the DLL's own threads setting never reached it; MOONSHINE_ORT_SINGLE_THREAD in the CRT's environment puts the sessions on the calling thread.
tags: [voice, performance, onnx, threads]
status: stable
timestamp: 2026-11-07T03:40:00Z
generated: { by: claude-code/fable-5.1, at: 2026-11-07T03:40:00Z }
bitten: [2026-11-06]
---

# Symptom

| date | wording |
|---|---|
| 2026-11-06 | the owner, speaking the first test orders into the headset: "the CPU spike is pretty aggressive, where it pretty much pauses and stops the game in some capacity" |
| 2026-11-06 | the log: `render levers: last 10 s: 700 frames` and `767` in the two windows with a spoken order, `899` otherwise; `voice: the listen's final transcript took 163 ms after the stop; 7 partials, the costliest 283 ms` for the tiny model on a 24-core machine |

# Cause

ONNX Runtime's session options default to an intra-op thread pool of one thread per physical core, and its threads
spin-wait between operators (`allow_spinning`), so every run takes every core at full load for its whole duration,
whether or not the model needs them: the tiny streaming model's partial cost more (283 ms) on 24 spinning threads than
it does on one. The game's render and game threads were preempted for the length of each partial, every `intervalMs`
(250 ms) through the listen and the tail: the frames lost.

Moonshine's C API has no option for the thread count. `MevrVoiceConfig::threads` (2 by default until today) was copied
into the DLL's config and never passed anywhere, so the setting that looked like it limited the recognizer did nothing.
Moonshine's ort-utils does read one switch, the environment variable `MOONSHINE_ORT_SINGLE_THREAD`
(`ort_maybe_force_single_thread`), with the CRT's `getenv`: it has to be in the CRT's copy of the environment of the
module that holds ort-utils (`_putenv_s` in our DLL, which links the library statically), not only in the process's
(`SetEnvironmentVariable`), and set before any session is created.

The same shape waited at the release: `MevrVoiceListen(0)` took the recognizer's lock on the host's render thread, so a
release during a partial in flight stalled the frame for the rest of that partial (up to 300 ms).

# Check

With the game running, speak an order and read the log's `render levers: last 10 s: N frames` around it against the
quiet windows, and `voice: ... the costliest N ms`. Task Manager during a listen: every core busy is the pool; one
core is the fix. The DLL logs at load which it is: `voice: the recognizer runs on one thread (MOONSHINE_ORT_SINGLE_THREAD)`.

# Fix in place

- `src/voice/Voice.cpp` `LoaderMain`: `[Voice] threads = 1` (the default) sets `MOONSHINE_ORT_SINGLE_THREAD=1` with
  `_putenv_s` and `SetEnvironmentVariableA` before the models load; any other value leaves ONNX Runtime's default and
  says so in the log.
- `MevrVoiceListen` only records the request and wakes the worker, which applies it under the lock (`ApplyListen`); the
  tail is counted from the release's time, not from when the worker got to it.
- When it comes back: a new Moonshine build may rename or drop the switch (it is undocumented; found by the string in
  `ort-utils.lib`). Then the thread count has to come from a session option Moonshine exposes, or the library is built
  from source with `SetIntraOpNumThreads` set.

# Citations

[1] [research/54](../../../research/54-voice-squad-commands.md) "One thread for the recognizer".
[2] [src/voice/Voice.cpp](../../../src/voice/Voice.cpp): `LoaderMain`, `ApplyListen`, `WorkerMain`.
[3] ONNX Runtime docs, "Thread management": `intra_op_num_threads` defaults to the physical core count; `allow_spinning` defaults on.

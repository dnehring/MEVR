# 12: Is there a framework we should build on instead? (2026-10-07)

The question came up while chasing M3 micro-stutter: are we re-creating core VR functionality
that a framework already provides?

## Findings

| Option | What it is | UE3 / ME LE? | License | Verdict |
|--------|-----------|--------------|---------|---------|
| **UEVR** (praydog) | Universal UE injector: 6DOF, stereo (Native Stereo, Synchronized Sequential, AFR), controller support via profiles | **UE 4.8–5.4 only.** Forums confirm UE3 games (e.g. BioShock Infinite) aren't supported. No UE3 support found as of 2026 | All rights reserved | Not usable. Its rendering-mode vocabulary is a useful reference |
| **VorpX** | Commercial "simulated VR" for DX/OpenGL games (geometry 3D or screen modes) | Unknown for ME LE | Closed, paid | Not a base for an open mod |
| **Geo-11 / 3Dmigoto / HelixMod** | DX11 wrapper that makes games stereo by patching shaders (both eyes rendered in sync); output is mostly stereo on a virtual screen | No ME LE stereo fix found. The only ME hit was a 3Dmigoto *visual* tweak mod for ME3 LE | 3Dmigoto: GitHub reports NOASSERTION (check) | A possible route to *synchronized* stereo without engine reverse engineering, but per-game shader fixing is needed and it isn't 6DOF VR by itself |
| **vrframework** (elliotttate) | **A 17-part guide (~6,000 lines) plus an engine-agnostic VR core scaffold**, derived from REFramework. Grounded in REFramework, starfield2vr and anvilengine2vr | Engine-agnostic | **MIT** (preserves praydog's notice) | **Adopt its guides and architecture.** The core is a scaffold: contract headers are complete, but several `.cpp` files are stubs pointing at REFramework code. It isn't a drop-in runtime |
| REFramework | The original | RE Engine only | MIT | Already our code source for D3D11/OpenXR pieces |
| Overlord | MW2CR VR | IW engine | GPL-3.0 | Design reference only |

## What this means for us

- **No framework does VR for UE3.** The engine adapter work (camera, view, FOV, frame
  timeline, HUD, input for UE3 and ME) is unavoidable. That's where this project's value is.
- **The engine-agnostic layer we've written is small.** About 1,000 lines: D3D11 Present
  hook, OpenXR session, swapchain, blit, submission. It works. vrframework's equivalent is
  partly stubs, so switching gains little.
- **vrframework's guides save re-learning.** Guide 07 (frame timing) explained the M3
  stutter. Read 08 (stereo strategies), 09 (camera math), 10 (submission and the TAA
  problem), 11 (HUD), 12 (input) and 14 (engine quirks) before each milestone.
- **Align our naming with its SPI** (`IEngineAdapter`, `FrameTimeline`, `StereoView`) as the
  code grows. That keeps porting to LE2/LE3, and borrowing from it, cheap.
- **Synchronized stereo is still open (M4).** Options:
  1. AFR first, the vrframework/REFramework default. Cheap, but each eye updates at half rate.
  2. Engine-level two views per frame (UE3 split-screen path, H2).
  3. 3Dmigoto-style shader stereo.

## Frame-timing lessons applied (guide 07)

- **Three clocks:** engine, render and presenter. The image submitted for a frame must carry
  the pose that frame was rendered with.
- **"Verify with a counter readout, not your eyes"**: log the counters, queue depth and
  intervals.
- **Our implementation:** a game-thread lockstep. After each engine tick, wait (30 ms timeout)
  until that frame is presented, so tagging is exact. The head is predicted for the very next
  display time.

## Sources

- [UEVR site](https://uevr.org/), [UEVR explained (PCVR Central)](https://pcvrcentral.com/frameworks/uevr), [80.lv on UEVR](https://80.lv/articles/a-free-toolset-for-turning-any-unreal-engine-game-into-vr-is-now-available)
- [BioShock Infinite UEVR thread (UE3 unsupported)](https://steamcommunity.com/app/8870/discussions/0/4030224579613325356/)
- [ResetEra: UE4/5 only](https://www.resetera.com/threads/biggest-moment-in-pc-vr-for-a-while-praydog%E2%80%99s-universal-unreal-engine-injector-released.800217/page-3)
- [vrframework (GitHub)](https://github.com/elliotttate/vrframework), [awesome.ecosyste.ms listing](https://awesome.ecosyste.ms/projects/github.com%2Felliotttate%2Fvrframework)
- [3Dmigoto (GitHub)](https://github.com/bo3b/3Dmigoto), [HelixMod VR](https://rawg.io/games/helixmod-vr), [Wikipedia: stereoscopic games list](https://en.wikipedia.org/wiki/List_of_stereoscopic_video_games)

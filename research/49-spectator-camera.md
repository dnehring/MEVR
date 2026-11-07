# 49: The spectator camera for promotional stills and video (2026-11-03)

**Question.** The user wants promotional stills and video of what it is like inside the headset. Until now the only
pictures were the test loop's per-eye snapshots (each eye's wide, asymmetric projection with our layers composited on the
CPU), and the game's desktop window shows Halcyon's one-eye mirror without any of our layers. How do VR games make that
material, and can LE1 do the same?

**Answer.** A spectator camera: a third scene render, mono, 16:9, at 80-90 degrees, from a smoothed head pose or from a
pose placed freely, with the UI composited in, shown in the desktop window so any recorder takes it. Built as
`[Spectator]` (off by default), HostServices v24. How-to: [docs/recording-promo.md](../docs/recording-promo.md).

## 1. Sources

- **Prior art (from general knowledge, no source in this repository):** Beat Saber's smoothed first-person camera and its
  third-person "camera plus" mods, Half-Life: Alyx's and Valve's SteamVR "spectator" window, LIV's mixed-reality capture,
  and the guidance VR studios give for trailers: record a mono render at 80-90 degrees horizontal, smooth the head's
  rotation over a quarter second or so (raw head motion is unwatchable on a flat screen), and never use the eye images'
  wide, off-centre projections. **No claim here is from a fetched page.**
- **The smoothing:** the critically damped spring of Game Programming Gems 4 ("Critically Damped Ease-In/Ease-Out
  Smoothing", Thomas Lowe), the form Unity ships as `SmoothDamp`: no overshoot, settles within a few smoothing times.
  `core/vr/Spectator` applies it per axis to the position and to the rotation vector from the target to the current
  orientation.
- **Our own code it builds on:** the scope capture (research/39: spawning a `SceneCapture2DActor`, the render target,
  `SetView` + `ForceUpdateComponents`, the D3D texture search, the pair-arm pose), research/47 (`bUpdateMatrices`, the
  formats `execCreate` takes, a capture nested in the eye's render command, the final post pass bound to its own target,
  the owner-chain experiments with `capturehide`), the test loop's layer parsing and location (`plugin/TestLoop`), and
  Halcyon's Present hook (`d3d_capture.cpp`: `[MIRRORTHROTTLE]`, `[ME2PORT v2]`'s backbuffer-dirty gate).
- **Ghidra was not available:** the project `LE1` was held by another session's GhidraMCP server (started 14:36). So how a
  capture's probe picks its view actor was not read; section 3 settles what matters by experiment.

## 2. What was built

**Mod (`game/Spectator`, hot-reloaded).** A `SceneCapture2DActor` (`Actor.Spawn` on the player controller, the default
object's `bStatic`/`bNoDelete` cleared for the call as the scope capture does) with a `TextureRenderTarget2D` of
`width` x `height` (1920x1080, up to 3840x2160; `format = float` FloatRGB by default, `rgba8` A8R8G8B8), the player's
post-process chain, `bUpdateMatrices` off, the update skips off. Each tick after the cockpit's tick it is pointed
(`SetView`, then `ForceUpdateComponents`: the full re-attach after `SetEnabled`, `SetCaptureParameters` or an owner change,
else the transform only) at:

- `camera = head`: the head pose the view was built for (`view_anchor::Current()->head`, tracking space, roll removed),
  through the critically damped follow (`smoothS`, 0.25 s; 0 = the raw head), then into the world through the same frame
  (`engine::ToWorld`, `OrientationToWorld`, `BasisRotation`). The smoothing runs in tracking space, so a stick or snap
  turn turns the picture with the world exactly as the headset shows it, and only the head's own motion is smoothed.
- `camera = free`, set from the console: `mevr spectator at X Y Z YAW PITCH` (world units, degrees; a fixed camera, no
  smoothing: the tracking space moves under it, the world doesn't; `at` alone freezes the current pose) or `mevr spectator
  follow RIGHT FORWARD UP [LOOKUP]` (metres from the pawn in its heading, looking at the pawn LOOKUP above its origin;
  smoothed). `mevr spectator head|on|off|loc|status`.
- **No head-tracked view** (menus, conversations, cutscenes: `view_anchor` has no frame): a fixed camera stays, the others
  film the game's own camera (`CameraCache.POV`), and the host composites no layers (they have no pose to be seen from).

The field is `fovDeg` (85) across; `SetCaptureParameters`' `NewFOV` is taken as the horizontal field (UE3's
`FPerspectiveMatrix(halfFOV, SizeX, SizeY, ...)`) [verify: a still's horizontal extent against the eye's]. Each tick
publishes a `vr::spectator::Frame` (state, the render target's `FTextureResource`, size, format, generation, the camera's
tracking pose and both half-angle tangents, flags: mirror, layers).

**Host (`plugin/Spectator`, needs a redeploy).**

- Each quad layer's swapchain image is copied (`CopyResource` into a typeless texture of ours) as it is released: Halcyon's
  through the test loop's wrapped `xrReleaseSwapchainImage`, ours through `OnImageWritten`. The runtime owns an image once
  released, so it is never read after. The eye swapchains are skipped; the reticle's static pixels are uploaded once.
  Nothing is copied while the mod isn't capturing.
- At `xrEndFrame` (the test loop's wrapper, before the snapshot's arming) the frame's quads are located exactly as the
  snapshots locate them (`LocateQuads`, shared), the capture's D3D11 texture is found behind its `FTextureResource` (the
  scope source's guarded search, now `scope_capture_source::FindTargetTexture`), drawn into a 16:9 FP16 picture in linear
  light with SDR white at 1 (a float capture times Halcyon's `floatWhiteScale`; an 8-bit one decoded), and every quad is
  drawn over it on the GPU as a perspective-correct textured quad seen from the pose the capture rendered from (the
  presented Draw's pair arm, `NoteDrawArm`, as research/39 section 5), blended in linear light (premultiplied or straight as
  the layer's flags say, opaque without blending), front faces only as `core/vr/LayerComposite`. The comfort vignette is
  left out; of the single-eye layers (the sights' dot, the scope's picture) only the right eye's are drawn.
- **The window:** one `[MEVR]` seam in Halcyon's `PresentHook`, right before the real Present and after
  `[MIRRORTHROTTLE]`: `MevrBridge::SpectatorMirror` clears the backbuffer and draws the picture letterboxed (sRGB-encoded for
  an 8-bit backbuffer, linear for an sRGB view, divided by the white scale for FP16). Our draw passes through Halcyon's draw
  hooks: they mark the backbuffer dirty for the pass-0 snapshot ([ME2PORT v2]), so the seam saves `g_sfrBbDirty` and puts it
  back. The UI redirect and the UI mirror ignore it (opaque blend). With `mirrorPresentEvery = 2` the window shows every
  other frame.
- **Stills:** a snapshot armed in a frame that made a picture stages it beside the layers and writes
  `<stamp>-spectator.png` (`tools/snapshot.ps1 -Spectator` turns the camera on first). Snapshot JSON: the mod's
  `"mod"."spectator"` (status, actor, owner, pose, ticks) and the host's `"spectator"` (state, sizes, formats, layers drawn).
- **Logs:** `spectator:` (settings, creation, render target, texture found, first picture, the backbuffer's format, every 10
  s the pictures made and mirrored and their CPU cost, the layers drawn; heartbeat line).

## 3. The view actor [verify]

A view leaves out a component with `bOwnerNoSee` when the view's actor is in its owner chain, and one with
`bOnlyOwnerSee` when it isn't (research/47). The eyes' view actor is the pawn: Halcyon hides Shepard's head meshes from
the eyes with `bOwnerNoSee` ([FPSHEAD], `game_camera.cpp`), and the cockpit's parts and the seated driver are
`bOnlyOwnerSee`, owned by the pawn. research/47 found a capture's view actor is neither the pawn, the controller, nor the
capture actor itself (`capturehide` 1 and 2). So, untreated, a head camera would sit inside the head, and the cockpit's
parts would vanish from it.

What is built: `viewActor = 1` writes the capture actor's `Owner` to the pawn (the field, journalled `Spectator.owner`, since
`SetOwner` through ProcessEvent does nothing: research/47), a test of whether the probe takes its view actor from its actor's
Owner; and `hideHead = 1` hides the pawn's head meshes (`PrimitiveComponent.SetHidden` on `m_oHeadMesh`, hair, head gear,
visor, face plate; restored when the camera leaves the head, the spectator goes off or the mod unloads) for the head
camera only. The eyes never show those meshes, so the only change in the headset is their shadow [verify]. The free camera
keeps the head: from outside it shows Shepard whole.

Results of the run: (to be filled in).

## 4. Cost [verify]

(to be measured with `tools/perf-probe.ps1` at the heavy Eden Prime cell, spectator off against on, 1080p and 4K.)

## 5. Open points

- 4K is wider than the per-eye render (2824 on the PS VR2), and the scene's render targets only grow: a 3840-wide capture
  makes them 3840 wide for the eyes too [verify the memory and that the eyes stay right].
- The world in the picture is the game's post-processed float output; whether its brightness matches the eyes'
  (`floatWhiteScale`) is checked by comparing a still with the eye snapshot of the same frame [verify].
- The sights for a left-eye aimer: only the right eye's single-eye layers are drawn.

## 2026-11-06, the first run in the game (the owner's 0.1.0 QA)

Turned on in the release build (`[Spectator] enabled = 1`, 1920x1080, head camera): the mod spawned the capture, owned it by
the pawn and hid the head meshes, but the host logged `no 1920x1080 D3D11 texture found behind the render target's resource
... in 240 frames; the desktop window keeps Halcyon's mirror` and went off. So the window showed Halcyon's one-eye mirror
(the left eye), which the owner recorded and found "strange" (the raw wide head view, not a 16:9 picture), while the game
paid for the third render. Turned off again for the release. Next, before any promo footage:
- find the render target's texture (the scope capture's search finds its own; compare the two targets' layouts);
- check that the picture carries the player's VRIK pose (arms on the controllers), not the animation's;
- then sections 3 and 4 (the view actor, the cost) and the hand angle the owner saw, with a real 16:9 recording.


## 2026-11-07, the picture found (ff47917)

Cause of the first run's failure: a capture target's texture sits behind an unaligned pointer that only the search's
known-class walk finds, and the class is learned by `scope_capture_source::NoteDevice`, which only the scope, the galaxy map,
the world UI's figure and the sights called. The spectator calls it itself now (and takes the game's device from the window's
swap chain before any quad is copied). On the fake headset (BIOA_PRO00, the release build plus the fix): `the render
target's D3D11 texture: 1920x1080 format 27, read as format 29 (found in 1 frames)`, the window showed the picture
letterboxed into the 2118x2160 backbuffer. Under a replay of the owner's PS VR2 recording (2026-11-06 22:40, controllers
tracked) the picture shows the VRIK arms where the eyes show them (both hands raised, open), so it does carry the player's
pose. Its 85 degrees across is narrower than an eye's view, so it frames tighter.

For recording: the window is the stereo target's size (2118x2160 at the owner's Performance mode), so a 16:9 picture always
has bars inside it; `width = 2118`, `height = 1192` fills its width pixel for pixel, and OBS's Crop/Pad filter takes 484 rows
off the top and 485 off the bottom (rows depend on the window's size: (H - 1192) / 2). The cost (section 4) and the view actor
(section 3) are still unmeasured.

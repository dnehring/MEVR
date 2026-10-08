# Stereo stage 2: two full renders per frame (design, not built)

Stage 1 (alternate-eye, built) shows each eye at half the frame rate, so moving objects ghost
and flicker between eyes. Stage 2 renders both eyes from the same game instant. This plan builds
on Halcyon's documented findings ([research/13](../research/13-existing-me-vr-mods.md)), written
our own way.

## Why not the obvious routes

- **Split-screen / two views in one view family: refuted.** UE3-LE runs deferred lighting,
  bloom, light shafts and DoF for the first view only. The second eye comes out flat, with black
  characters.
- **Shader-level stereo (3Dmigoto-style): rejected.** It needs a per-shader fix-up for ME's
  whole shader set and isn't 6DOF on its own.

## The approach: draw the frame twice

1. **Game thread.** Hook `FViewportClient::Draw` (LE1 `0x4C71A0`; LE2 `0x6D0A00`; LE3
   `0x6D1E30`). After the original Draw returns, call it once more.
   - Each Draw calls `CalcSceneView` (we already hook it), so the eye offset goes in there: pass 0
     gets −halfIpd and pass 1 gets +halfIpd, using the same `ApplyTranslation` as stage 1.
   - Each Draw builds a fresh `FSceneView`, so nothing leaks into game state.
   - Guard the second call (SEH, with auto-disable after a few faults) and run it only in
     gameplay modes.
2. **Render thread, finding where pass 0 ends.** Both passes write the same backbuffer before
   one Present, so pass 0 has to be copied out at the boundary between the passes. Options, in
   order of preference:
   - a. **Our own marker in the command stream.** If we can enqueue a render command from the
     game thread between the two Draws (UE3's `ENQUEUE_UNIQUE_RENDER_COMMAND` goes through the
     render command ring buffer), the render thread runs it exactly at the boundary. This needs
     the ring buffer's allocation function, a new RVA hunt. It's the most robust option because
     it doesn't depend on the game's shaders.
   - b. **The D3D signature of the boundary** (Halcyon's route): the first scene-sized depth clear
     after the frame's final composite draw (pixel-shader hash `0x9AA7F54A` on LE1). It needs
     inline hooks on the draw and clear functions, because the engine caches the D3D function
     pointers (vtable patches miss them). Conversations take another path.
   - c. **Two Presents per frame.** Hook the caller that wraps Draw with Begin/EndDrawing (the
     Present), so each pass ends in its own Present; we swallow the first one. The caller's
     address is unknown, and it changes Present-count assumptions in the pacing code.
3. **The HUD.** Scaleform draws once, after both passes, so only pass 1 has it. Either mirror the
   UI draws into the held pass-0 image (they must use the game's own depth-stencil view, since
   Scaleform masks with stencil), or move the HUD to its own quad layer (M5), which avoids the
   problem entirely.
4. **Pacing.** Two renders per present roughly halve the frame rate. Lock presents to a whole
   number of display periods, adaptive N/N+1 per guide 07 and Halcyon: a regular cadence
   reprojects smoothly, an irregular one judders. The lockstep pairing still holds: each tick
   gives one Present carrying two views.
5. **Temporal state.** Both passes share the player's `FSceneViewState` (occlusion queries,
   temporal history), which may flicker: eye 1's occlusion results get used by eye 0.
   `AllocateViewState` (LE1 `0x742B70`) can make a second state for pass 1, swapped in via
   `FSceneView+0x8`. Try without it first.

## What to measure first (in game)

- Does a second `FViewportClient::Draw` call render at all, and what does it cost? Log the frame
  time before and after.
- Which option in step 2 is feasible: look for the render-command allocator in the decrypted code
  (needs an in-process dump; `.text` is encrypted on disk).
- Flicker from the shared view state.

## Done when

- Both eyes come from the same tick; there's no ghosting on strafing.
- The HUD appears in both eyes.
- The frame rate holds a regular cadence.

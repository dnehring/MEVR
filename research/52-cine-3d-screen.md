# 52: Conversations and cutscenes on a 3D cinema screen (2026-11-03)

The user (2026-11-03): watching LE1's cinematics in full VR can be poor because of the camera moves; could they play as
3D on a viewing window instead? This note is how that was built: a third way to show a conversation or cutscene, next to
Halcyon's two, and how it was checked.

## What Halcyon already had

Read in `src/halcyon/le1/xr_session.cpp` and `vr_config.h`:

| Mode | Render | Shown | The director's camera moves |
|---|---|---|---|
| VR cine (`cineVrConvo` / `cineVrCutscene = 1`, the default) | both Stereo 2 passes at the headset's field of view (the fill window, `SetTargetFovHalfAngles`) | the projection layer: the scene all around | fill the view |
| VR cine, `cineHeadTracking = 0` | the same | the same pair, anchored to the head each frame (`[VRCINE]` anchor) | still fill the view |
| Flat screen (those flags 0, `forceFlatCine`) | one render (`sfr2Active` false) at the game's native projection | the one backbuffer in both eyes on one quad (eye visibility BOTH), 2.35 m wide at 1.4 m, world-locked by our `PlacePanel` | inside the frame, but 2D |

Halcyon removed a head-locked per-eye quad path for cine on 2026-08-02 because it declared no field of view and so
couldn't carry its fill model (comment above the flat quad). A screen fixed in the room has no fill model: that reason
doesn't apply to it.

## The 3D screen

A VR cine with `[Cinematics] conversations` / `cutscenes = 1` in `ASI\LE1MEVR.ini` (`plugin/CineScreen`):

- **Render.** Both Stereo 2 passes keep running (it is a VR cine: the second pass's gate, the cine capture boundary,
  `[NOLETTERBOX]`'s full frame), but at the game's native projection, as the flat screen renders: the shot as the director
  framed it. Head look and lean stay parked. The two passes are parallel cameras on the director's camera, half the
  separation to each side (`PlanStereo`'s screen branch: mode 1, no convergence shift at the eye copy).
- **Show.** Instead of the projection layer, two quads at one pose and size: eye visibility LEFT on the left eye's
  swapchain, RIGHT on the right's. `PlacePanel` (kind Conversation or Cutscene) fixes the screen in the room where it
  appears, `distanceM` ahead. The game pointer gets a quad as wide as the whole image (the crop's average), so a
  controller's ray maps onto the game window's pixel as on the flat screen.
- **Depth** (`core/vr/CineScreen`, unit-tested). Parallel cameras put infinity on the screen plane and everything nearer
  in front of it. Each eye drops a sliver `c` of its image on opposite sides (the left eye its left edge: horizontal image
  translation), so the shown parts are `c` apart and infinity's disparity is `c / (1 - c)` = `depth` of the screen's width:
  behind the screen. The cameras' distance is then `c * converge * 2 tan(halfH)` for the render's own horizontal half
  angle (`RenderHook::GetRenderFovHalfAngles`, the projection the engine drew with), which puts the scene `convergeM` away
  (at the scene's 100 units per metre, research/30) on the screen plane: a close-up's narrow lens gets a narrow baseline,
  so cuts don't change the depth budget. Infinity's disparity on the screen is capped at 80% of the viewer's eye distance
  (`MaxDepth`): the eyes never diverge.

Defaults (on for conversations and cutscenes since the check below): a screen 3.0 m wide at 2.6 m (60 degrees across;
3.1 m tall, since the eye image is nearly square), depth 1.5% of its width (4.5 cm: infinity about 8.8 m away for 64 mm
eyes), the scene 1.2 m away on the plane (2.5 m at first: see below). Settings: `[Cinematics] conversations,
cutscenes, distanceM, widthM, depth, convergeM`; our menu's Comfort > Conversations and cutscenes: In VR / 3D screen /
Flat screen (it sets Halcyon's VR flags and ours together), screen width, distance and 3D depth (100% = 1.5%).
Prerendered movies stay on the flat screen (`[DEADVETO]`: no depth exists). Logs `cine screen:`, `[VRSTATE] ... -> 3D
screen (MEVR)`; snapshot `"cineScreen"`.

## Seams

All `[MEVR]` in `xr_session.cpp`: `CineDecision.mevrScreen` (from `MevrBridge::CineScreenWanted`), the head-look park,
the native projection, the lean, `StereoPlanInput.screen` and `fovOverride`, the freeze (`lockedCine`), the submit's two
quads, the nearest quad's distance and the overlay frame's `worldFrame` (no world layer on the screen).

## Checked (2026-11-03, 21:04-21:12, fake headset)

Main at 8b1272f deployed (host and mod), `[Cinematics] conversations = cutscenes = 1`, a fresh Shepard's `open
BIOA_STA00` from the title: the Citadel arrival cutscene (game mode 6) and then a conversation with Ashley (mode 5).

- **It engages and holds.** `[VRSTATE] gm 6 alive cutscene -> 3D screen (MEVR)`, then `gm 5 alive convo -> 3D screen
  (MEVR)`; movie segments inside the cutscene went to the flat screen and back (`[DEADVETO]`, as designed). The title's
  animated background is a cutscene to the game too and showed on the screen. The snapshot: two quads, eye visibility 1
  and 2, 3.0 x 3.11 m, rects `[35, 0, 2309, 2392]` and `[0, 0, 2309, 2392]` of the 2344 x 2392 eye images (the 1.48% crop),
  nothing else but Halcyon's 64 x 64 placeholder projection layer. Per-eye composites: the shot as framed on a screen in
  the dark, the dialogue wheel in both eyes.
- **The separation follows the lens** (`cine screen:` lines): 4.86 units apart for a 66.7-degree shot, 2.11 at 31.9, 1.61
  at 24.6.
- **Depth, measured** (`scratchpad` cross-correlation of the two quad images, on-screen disparity as a share of the width,
  + behind the screen), in the held conversation (Shepard's close-up, Ashley behind him, the corridor):

  | Region | convergeM 2.5 | convergeM 1.2 |
  |---|---|---|
  | Shepard's face (close-up) | -3.33% (10 cm, 2.2 degrees in front) | -0.74% (2.2 cm, 0.5 degrees in front) |
  | Ashley (mid distance) | +1.08% | +1.17% |
  | The corridor, the ceiling (far) | +1.52% | +1.52% |

  The far scene sits exactly at the set depth (1.5%) whatever the convergence distance, as the geometry says. In the
  arrival cutscene's close-up the camera was 0.84 m from Shepard's face (from the disparity and the baseline): ME1's
  cinematic close-ups are that near, so at 2.5 m every close-up came out of the screen by 2 degrees, twice the usual
  comfort limit. 1.2 m keeps them within half a degree and the far scene at 1.0 degree behind: the default since.
- **Cost** (the held conversation, 20 s probes, A B A B by switching `[Cinematics] conversations` live):

  | Mode | Render thread | Draws |
  |---|---|---|
  | 3D screen | 4.53-4.57 ms | 1,106 |
  | VR cine | 5.45-5.48 ms | 1,802 |

  The director's narrower lens culls 39% of the draws. Both modes were paced the same (the conversation ran at half the
  null display's 90 Hz).

## Limits

- **The dialogue wheel was small** (about 0.9 degrees tall): Halcyon's VR conversation layout (`convoScale*Stereo` 0.50 x
  0.65, centred) shrinks the conversation movie for a head-locked view. Since 2026-11-04 (below) the screen shows it at its
  authored size.
- **The screen is nearly square** (the full frame `[NOLETTERBOX]` renders). The director's 16:9 framing is its middle
  band; a crop to that band would cut the bottom of the frame where the game puts subtitles, so the full frame stays.
- **Not checked in the headset** by a person yet: comfort, the wheel's readability, the game pointer on the screen.

## The dialogue wheel at its authored size (2026-11-04)

A `[MEVR]` seam in `xr_session.cpp` gives the conversation movie the identity transform (`Pchud::SetConvoTransform(true, 1,
1, 0, 0)`) while `mevrCineScreen` is up, and Halcyon's own layout otherwise. Checked on the fake headset (host and mod
from main e8b48b6 plus this branch, `open BIOA_STA00` from the Mako autosave: the arrival cutscene, then the medical bay
conversation): the conversation goes to the screen as before, and the wheel's ring is about 8% of the screen's width
(190 of 2309 pixels) against about 5% in the 2026-11-03 capture of the same conversation (the scratchpad's
`convo-lr.png`), so about 1.75 times larger: twice as wide and 1.5 times as tall, Halcyon's 0.50 x 0.65 undone. The
options' capitals are about 1% of the screen's height, 0.7 degrees on the default screen (3.0 m at 2.6 m); a wider
screen (Comfort > Conversations and cutscenes) enlarges them. The wheel sits where the game draws it (left of centre,
low), over the shot. Not yet read by a person in a headset.

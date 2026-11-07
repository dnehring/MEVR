# 21: Sights and scopes (2026-10-22)

**Request (user, 2026-10-22).** The player knows where shots land only from the floating ring reticle
(`src/plugin/HalcyonOverlays.cpp`, on `core/vr/AimRay`). Give the rifle a scope option, taking Overlord
(MW2 VR, GPL-3.0) as the reference for good scope handling, and consider, from cheapest to richest: a
collimated red-dot or holographic sight for every gun; a magnified scope by picture-in-picture (PiP); a
true render-to-texture scope. Settings in a `[Sights]` section. The sight line from the gun's measured
muzzle and barrel (`game/weapon`). The new layers in the test loop's snapshots. And: should aiming the
sniper rifle engage ME1's aimed-fire bonus without the game's scope camera?

**Built:** option 1 for every gun and option 2 for the sniper rifle (option 2 is one setting away for the
assault rifle), plus aimed fire for scoped shots. Design below; test steps in `notes/next-vr-session.md`
step 14.

## 1. Sources

### Overlord (reference/Overlord, GPL-3.0; read, no code copied)

- `docs/vr-native-ads.md` "Physical-lens image magnifier": a shader that "compresses sampling
  coordinates around the optical axis inside a projected circular rear-lens quad", reading the **same
  eye's** finished image; no extra scene render, readback or wait. The reticle is drawn after the
  magnification, so zoom doesn't enlarge it, and "eye translation moves the reticle relative to the
  tube"; "an eye outside the aperture sees a dark lens". Default magnifications 4x (ACOG, thermal) and 6x
  (sniper scopes). Red dots and holographic sights are left to the game's own models.
- Same doc, "Independent scene prototype": one extra native scene per frame for the selected aiming eye,
  an off-axis crop of that eye's projection (more samples, same visibility); the other eye gets "scope
  shadow". Headset acceptance and performance numbers are pending there.
- `docs/vr-optic-rendering-feasibility.md`: the three routes in a table (same-eye magnifier, an extra
  rasterized view of the shared scene, optic-aware preparation plus an extra view); the pinhole relation
  `scope scene angle = 2 atan(r / (d M))` for lens radius r, eye-to-lens d, magnification M; choose the
  aiming eye **with hysteresis**; "a same-sized third view adds 50%" of the pixels; reduced update rates
  show as scope lag because "headset reprojection does not update the gun-relative image".
- `src/client/component/vr/gameplay/optic_geometry.hpp` `project`: the eye's place on the lens is
  `u = .5 + dot(eye - centre, right) / 2r` (and v), the point where the line through the eye along the
  optic axis meets the lens; the image is visible when that point is inside the exit pupil.
- `spatial_panel_renderer.cpp` `optic_ps`: the lens UV is pulled toward that point by 1/M
  (`lens_uv = canvas.xy + (uv - canvas.xy) / canvas.z`), re-projected to the eye's screen and sampled;
  the reticle is centred on that point; a circular exit pupil of the lens's radius about it, black
  outside ("scope shadow"), both circle edges faded.
- `optic_catalog.hpp`: rear-lens radii 10.5-20.4 mm. `ads_alignment.hpp`: the ADS corridor (eye
  0.15-1.5 m behind the muzzle, within 9 cm sideways, 30 degrees, 100 ms dwell; exit 40 degrees).
- `vr-native-ads.md` "ADS eye-distance assistance": while aimed two-handed, the gun slides up to 15 cm
  toward the eye for sniper scopes (5 cm holographic), keeping 8 cm clearance. Built here for scopes (section 14).

### Elsewhere

- Render-to-texture scopes (a second camera behind the lens) are the common engine route and are costly:
  an [Unreal forum answer](https://forums.unrealengine.com/t/how-to-dual-render-scopes-video-example/393900)
  ("will reduce your performance drastically"), the [Source engine wiki](https://developer.valvesoftware.com/wiki/Adding_a_Dynamic_Scope)
  (a dedicated scope render target), and [Bodycam's press claim](https://tweaktown.com/news/113239/bodycam-develops-in-house-optics-shading-technique-for-scopes-in-unreal-engine-5-with-no-performance-loss/index.html)
  of a shader route "with no performance loss" (unverified).
- VR specifics are thinly documented. A [Sniper Elite VR thread](https://steamcommunity.com/app/752480/discussions/0/3049483485751256789)
  says Pavlov renders the scope view from the player's eye rather than the gun (a player's description).
- Collimation: a reflex sight's dot is parallax-free only at infinity ([Wikipedia: red dot sight](https://en.wikipedia.org/wiki/Red_dot_sight)).
  An MIT-licensed [Godot collimator shader](https://godotshaders.com/shader/red-dot-hud-holographic-projection-shader/)
  projects the view ray onto the glass plane, so the reticle depends only on the direction from the eye,
  not its place: the same idea as drawing the dot far out along the sight line. A commercial
  [VRChat sight shader](https://booth.pm/ja/items/7275536) lists "eye relief" and "scope shadow" as
  features: the expected behaviours.
- OpenXR quad layers take `eyeVisibility` (both, left, right: [XrEyeVisibility](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrEyeVisibility.html)),
  which is how a sight shows to one eye only.

## 2. Constraints here

- **Rendering budget.** Halcyon renders the scene twice per present (Stereo 2, research/16). A third
  render is not a draw call we can add: UE3 renders one scene per view the engine sets up, and Halcyon's
  second pass comes from re-running the game's own render with a moved view. A scope camera would need
  another engine pass (a scene capture actor, or a third SFR pass with its own projection), with its own
  cost (Overlord: a same-sized view is +50% pixels; the CPU work doesn't shrink with the view).
- **ME1's zoom.** LT (`TightAim`) sets the weapon's `bIsZoomed`, and the camera picks its TightAim mode
  from that flag (research/18). For the sniper rifle that mode is the game's scope camera, which Halcyon's
  first person leaves to the game (third person: research/14, `FpStateForClass`). So the sniper rifle must
  never get LT from us (`[Controls] sniperAims = 0`).
- **The gun model.** ME1's sniper rifles carry a scope model on top. Looking along the sight line, the
  aiming eye sees that model's back, so magnifying the aiming eye's own image magnifies the scope tube
  (shown offline: `tools/sight-preview`, `scope-same-eye.png` is black).

## 3. The options

| | 1. Collimated dot / holo | 2. PiP magnifier | 3. Render-to-texture |
|---|---|---|---|
| What | A reticle quad far out along the sight line, shown only to an eye looking through the window | A disc at the eyepiece showing a magnified crop of an eye image already rendered | An extra scene render with a narrow FOV behind the lens |
| Detail | n/a (no zoom) | The eye image's pixels, enlarged (4x: about 6 source pixels per degree of the target) | Full detail |
| Cost | Two static quads | One eye-image copy and one 768-pixel shader pass per frame while aiming | A third scene render (or a reduced one), engine work in Halcyon's render path |
| Risk | Low | Medium: parallax between the eyes, blur at high zoom | High: invasive in Halcyon's core, the performance budget |

## 4. Design as built

### The sight on the gun (mod: `game/sights`, `core/vr/Sights`)

- Each tick, from hand aim's ray (the muzzle and barrel in tracking space, `game/hand_aim`), the held
  weapon's type and `[Sights]`: the sight's centre `back` gun units behind the muzzle along the barrel and
  `height` above the bore, toward the gun's top; through the view's scale (`TrackingFrame::unitsPerMeter`)
  into tracking meters.
- **Placement:** `auto` uses a share of the gun's muzzle-to-grip length (the weapon mesh's origin, newly
  in `weapon::Muzzle`): 0.75 for the pistol (on the slide), 0.85 shotgun, 0.9 assault rifle (just ahead of
  the grip), 1.05 sniper rifle (the eyepiece just behind it); heights 4-7 units. The lengths of the pistol
  (27 units) and the assault rifle (39) are measured (log of 2026-10-21); the shotgun's, the sniper
  rifle's and every height are guesses **[verify with a snapshot]**. `<type>Place = back, height` sets
  them by hand.
- **The gun's top:** the right controller's aim-pose up, made perpendicular to the barrel: arm IK rolls
  the gun with that up (research/14, "Gun roll"), so the sight sits on the gun as drawn. `[Hands]
  gunRoll` isn't followed (it's 0 unless set).
- **The sight line:** parallel to the barrel (zeroed at infinity), or `zeroM` (game meters) to cross the
  shot's line there. Parallel means the reticle marks a point the sight height above where the shot lands
  (5-7 units), at every distance; ME1's shots are hitscan.

### Option 1: dot and holo (host: `plugin/SightLayers`)

- **Collimated:** the reticle is a quad 50 m out along the sight line (`sights::Reticle`), at a fixed
  angular size. From an eye behind the window its direction is the sight line plus at most the window's
  radius over 50 m (under 0.07 degrees for a 6 cm window): it sits on the target wherever the eye is.
- **Only through the window:** per eye, where the eye's line along the sight meets the window's plane
  (`sights::EyeLine`): on inside the window's radius (`windowUnits`, 3), off past 1.25 of it, between 2 cm
  and 1 m behind (`sights::Looking`, hysteresis). The quad's `eyeVisibility` is that eye (both when both
  look through a big window). A dominant eye sees the dot, the other eye the target: the same fusion as a
  real red dot with both eyes open.
- **Looks:** `dot` = a dot with a glow (`dotSizeDeg` 0.25: a headset pixel is about 0.05 degrees, so a
  real 2-MOA dot would vanish); `holo` = a ring (`ringSizeDeg` 1.6, about an EOTech's 65 MOA) with a dot.
  A faint ring at the window itself (`frame`) shows where the sight is on the gun, since ME1's guns have
  no optic model.
- **The floating ring reticle hides** while an eye looks through (`replaceRing`, held 0.3 s).

### Option 2: the scope, by picture-in-picture

- **The picture:** a disc at the eyepiece (`lensUnits` 2: 4 cm across at the 50 units/m view, 2 cm at
  100), shown to the aiming eye only. Per pixel (shader in `plugin/SightPictures`; the same maths in
  `core/vr/Sights` `LensDirection`, `FocusPoint`, `Project`, unit-tested):
  1. Q: where the line through the aiming eye along the sight meets the lens (Overlord's u, v).
  2. The lens point L shows the direction `axis * along + (L - Q) / zoom`: the view through the lens,
     magnified `zoom` times about the sight line (Overlord's compression about the axis point).
  3. That direction's point `focusM` ahead, projected into the **source eye image** with the pose and FOV
     it was rendered for (the projection layer's view as submitted), sampled.
  4. The reticle (duplex, mil-dot or dot; fixed on the lens: second focal plane) centred on Q, so it stays
     on the target as the eye moves (collimated). Its lit centre in `reticleColor`.
  5. The **exit pupil**: a disc of the lens's radius about Q; outside it black (scope shadow), so an eye
     off the line sees a crescent. Past 1.5 eye reliefs (`eyeReliefM`, 12 cm) the pupil shrinks (to 0.2
     at 4.5), and past 6 the picture goes.
- **The source eye is the other eye** (`source = other`): its line along the sight passes the IPD beside
  the gun's scope model, so the target is in its image. The picture is exact for things at `focusM`
  (40 game meters); nearer or further, the two eyes' parallax shifts the image by IPD x (1/D - 1/focus)
  against the reticle: for a 6.4 cm IPD at the life-size scale (100 units per meter), 0.09 degrees for
  something at infinity and 0.28 degrees at 10 m (true angle, before magnification), half that at the
  50 units/m view. `source = same` uses the aiming eye's own image (for a sight placed
  clear of the gun model).
- **Which eye aims:** of the two eyes behind the eyepiece, the one nearest the sight line within two lens
  radii, kept while within 2.5 unless the other is clearly nearer (`sights::AimingEye`).
- **Getting the eye image:** Halcyon releases each eye's swapchain image before the layers are built, and
  a released image is the runtime's. The test loop's `xrReleaseSwapchainImage` wrapper now takes a tap
  (`testloop::SetReleaseTap`): while a scope picture is wanted, the source eye's image is copied (one
  GPU copy) into our texture right before it's released. The picture uses that copy with the same
  frame's projection view, so image and pose match. The copy is read through an sRGB view (the eye
  swapchains are sRGB, as the snapshots assume; Halcyon's experimental CAMCB96 atlas path declares UNORM
  and would look darker in the scope **[verify if that path is ever used]**).
- **Assault rifle:** `assaultRifle = scope 2` gives it a 2x scope the same way.

### Aimed fire through the scope (mod: `game/sights`, `game/weapon`)

- research/18: the aimed-fire bonus is the weapon's `bIsZoomed`, read by `GetPlayerAimError` (and
  `GetRecoil`); the camera picks TightAim (the sniper's scope camera) from the same flag. So the flag must
  not be set where the camera can see it.
- Read in Ghidra (2026-10-22): `ABioWeaponRanged::GetPlayerAimError` (`0xC59920`) returns
  `lerp(Stats' aim-error range chosen by bIsZoomed, Accuracy)`, nothing else. TickFire calls the pawn's
  `GetWeaponStartTraceLocation` then the weapon's `GetAdjustedAim` (whose script adds
  `GetPlayerAimError(Inventory.Accuracy)`), but `GetWeaponStartTraceLocation` is also called after the
  trace, by a damage falloff helper (`0xC5A010`), so setting the flag around the start-trace call would
  have left it set.
- **Built:** a CallFunction post-handler on `BioWeaponRanged.GetPlayerAimError` (it's called from
  script). While the host shows the scope's picture for the held sniper rifle (`HostServices::sightView`),
  the answer becomes the zoomed one for the same accuracy: the unzoomed lerp is undone from the native's
  answers for accuracy 0 and 1, and the zoomed answer is the native's with `bIsZoomed` set for that one
  ProcessEvent call. The camera never sees the flag. Recoil keeps its unzoomed value (`GetRecoil` isn't
  touched). `[Sights] aimedFire = 0` turns it off.
- **[verify]** the first `sights: first scoped shot with aimed fire: aim error A -> B` log line (B
  smaller); shot groups through the scope against hip fire.
- Other guns keep aimed fire from the two-handed grip at the shoulder (LT). Looking through a red dot
  could count too; not built (question for the user).

## 5. Performance

- Dot and holo: two quads with static textures; per frame a few vector operations. Nothing measurable.
- Scope, only while an eye looks through it: one GPU copy of the source eye image (2204x2352 RGBA8 on
  SteamVR here, about 20 MB, well under 0.1 ms on an RTX 4090), one full-screen pass into a 768-pixel
  square (one texture sample per pixel), a state save and restore on the immediate context. No extra
  scene render, no CPU readback, no wait.
- The CallFunction handler runs once per shot (three extra native calls through ProcessEvent).
- Option 3 would cost a scene render: up to +50% of the scene's GPU work at full size and the game
  thread's scene setup, against research/16's budget where Stereo 2 already runs below 90 Hz in heavy
  scenes.

## 6. Settings (`[Sights]` in `Binaries\Win64\ASI\LE1MEVR.ini`, appended to older files, live)

`enabled`; `pistol = dot`, `shotgun = holo`, `assaultRifle = holo`, `sniperRifle = scope 4` (each: none,
dot, holo, or scope with a magnification 1-12); `<type>Place = auto` or `back, height` (gun units);
`dotColor`, `dotSizeDeg`, `ringSizeDeg`, `windowUnits`, `frame`, `zeroM`; `lensUnits`, `reticle`,
`reticleColor`, `source`, `focusM`, `eyeReliefM`, `resolution`; `replaceRing`, `aimedFire`; since the
evening of 2026-10-22 the alignment assist (section 8): `assist`, `assistConeDeg`, `assistStrength`,
`assistMaxM`, `assistRangeM`; since 2026-10-23 (sections 9-11) `eyeBox` (6 units), `converge` (1),
and the shotgun and the assault rifle default to `dot`; since 2026-10-25 (section 13) `scopeFieldDeg` (22), `scopeEyeBoxM`
(0.05), `scopeMaxM` (0.70), and `<type>Place = back, height, side`; since 2026-10-27 (section 14) `assaultRifle = scope 1.5`,
`scopeReliefM` (0.12), `scopePullMaxM` (0.15), `<type>Lens` (gun units; assault rifle 1.6, sniper rifle 2, else `lensUnits`)
and `<type>Hide` (sniper rifle `Scope_Lens`).

## 7. Risks and what's left

- **Placement** is a guess until a snapshot shows the sight on each gun **[verify]**. The window ring
  makes a wrong place obvious; `<type>Place` fixes it live.
- **Blur:** the picture enlarges the eye image's pixels: the swapchain the scope samples has about 24
  per degree (2204 px over the 90 degrees SteamVR keeps of Halcyon's 108), so 4x leaves about 6 per
  degree of the target where the eye wants 20 or more. 6x and above will look soft. A sharper upscale
  (bicubic) is a small shader change if wanted; only a real render (option 3) adds detail.
- **Edge of the image:** a magnified direction outside the source eye's image samples black (it only
  happens far off-axis).
- **The gun's own scope model** may poke out around the eyepiece disc or sit in front of it if the
  placement is off; the disc is drawn over the eye image (no depth test).
- **One eye only depends on the runtime** honouring a quad's `eyeVisibility` **[verify on SteamVR and
  the Oculus runtime: close the other eye]**. The snapshots draw it per eye either way, so they can't
  tell. If a runtime ignores it, both eyes see the dot (still collimated, harmless) and the scope's
  picture (the other eye sees the aiming eye's picture at its own angle: rivalry, not breakage).
- **Latency:** the sight comes from the last game tick (like the gun model in the eye image), the eyes
  from this frame.
- **Not done:** option 3; thermal; the zoomed recoil; a dot through a scope's picture. (The sideways
  alignment assist is built: section 8; Overlord's ADS eye-distance assistance, for scopes: section 14.)

## 8. Lining the sight up with the eye (headset session 2026-10-22, 19:52; built the same evening)

**Report (user):** looking through the rifle's sight or scope, the rifle had to be moved a long way
sideways before the eye could see through it.

**What the logs show** (`LE1MEVR.log`, 19:52-19:57; the session's two snapshots were taken with the gun
holstered and with Halcyon's menu open, so they show no sight):
- Only the pistol's red dot and, for 0.6 s, the assault rifle's holo were looked through (the sniper
  rifle wasn't drawn). The one geometry line: `[MEVR-SIGHT] first red dot reticle: the right eye looks
  through the window (0.514 m behind it, 0.0296 m off the line, window radius 0.0300 m)`: the eye had to
  come within 3 cm of the line, half a metre from the sight.
- The sights themselves were where auto placement put them (pistol 20.4 units behind the muzzle, 4.0
  above the bore; assault rifle 35.3 behind, 6.0 above) on guns measured as before (`grip measured`:
  muzzle 36.8 and 49.2 units from the wrist). Nothing there is off by more than a few units.
- **The systematic offset is the arm-length calibration:** the user's file has `[Arms] armLengthM =
  0.723`, so arm IK scales each wrist goal's distance from Shepard's (moved) shoulder by 55.2 / (0.723 x
  100) = **0.76** (research/14 "Arm-length calibration"; the view is 100 units per meter, life-size). Near
  the face that pulls the drawn gun 24% of the way toward the right shoulder: with the shoulder about 18 cm
  right of the head's centre and 20 cm below the eyes, a controller held in front of the right eye puts the
  drawn sight about 3.5 cm further right and 2.5 cm lower (and the gun 24% nearer the shoulder along the
  way out). To see through it, the controller had to go about 5 cm left of the eye, in front of the nose:
  the "long way sideways". **[inference from the numbers; verify with the new heartbeat line]**
- Also seen, separate: every draw re-measured the gun's grip and waited for the hand to hold still
  (`grip being measured (hold still)` for 0.4-1.4 s): no aimed gun, sight or support grip until then.

**Built** (mod only, hot reload):
1. **No reach scaling near the face** (`ik::ReachWeight`, `ik::ScaledReach`; `[Hands] reachNearFaceM =
   0.30`): a hand within 0.30 m of the eyes follows its controller 1:1, the full calibration from 0.45 m
   (smoothstep between); a left hand holding the gun takes the gun hand's weight (blended by the support
   grip), so the two-hand line isn't bent. Arm-length calibration still maps full reach at arm's length.
2. **Grips remembered per gun** (`game/weapon`: per weapon mesh asset, body asset and holding bone, this
   world): a redrawn gun is aimed at once; a still hand still re-checks it.
3. **The alignment assist** (`vr::sights::AssistTarget`, `AssistSpring`; `game/sights`; arm IK's
   `Request::gunShift`). Overlord slides the gun along the barrel toward the eye; ours slides it **across**
   the sight line, since the problem here is lateral:
   - **Measured without itself:** the sight as drawn minus the shift arm IK applied for that pose (the
     engine poses the gun before our tick, so the shift taken the tick before), so the assist never chases
     its own move.
   - **The aiming eye:** of the two eyes (the view's head pose +/- 3.2 cm along its right; roll-free, so
     about 1 cm off with a tilted head **[verify]**), the one nearest the line by angle seen from the sight,
     kept unless the other is 1.5 degrees nearer.
   - **When:** the eye within `assistConeDeg` (10) of the line (full within 5), the sight between 2 cm and
     `assistRangeM` (0.65 m) ahead (full to 0.50 m), the line within 35 degrees of the head's forward (full
     within 15): "raised to the face and looking along it". Each fades with a quintic smoothstep.
   - **How far:** the eye's offset from the line times the weight times `assistStrength` (1), at most
     `assistMaxM` (0.10 m). Then Overlord's critically damped spring (10/s, at most 0.25 m/s): no jump on
     the way in or out, and it eases back when the gun is lowered.
   - **Applied to the whole gun:** the right wrist's goal after reach scaling, and the left's while the
     support grip holds (fading with its blend after a release): a hand on the gun moves with it, so the
     two-hand line doesn't turn; a free hand stays.
     The muzzle, the shots, the reticle's ray and the sight all follow the gun as drawn (shots shift by at
     most 10 cm, parallel).
   - **Logs:** `sights: alignment assist on: the right eye N cm left of and N cm above the sight line ...`,
     `... off`, and in the heartbeat the aiming eye's **average offset from the unassisted line** while the
     gun was raised: a steady value there is the sight's placement error to fix (`<type>Place`). Snapshot:
     the mod's `"sights"."assist"`.
   - **Risks:** a gun that moves by itself can feel pulled (`assistStrength` < 1 or `assist = 0`); with the
     eye 10 degrees off at 0.6 m the move is capped at 10 cm; the assist follows the sight's line, so a
     placement error is hidden rather than fixed (the heartbeat keeps measuring it).
- **Offline check:** `mevr-sight-preview` renders the scope with the host's shader from ray-traced eye
  images (a bullseye at 40 m, the scope tube in front of the aiming eye) and the reticles; the pictures
  showed the bullseye centred and 4x magnified from the other eye, black from the aiming eye's own,
  the crescent off-axis with the reticle still on the bullseye, and the shrunken picture far back.

## 9. The sights sit beside the game's picture: Halcyon's stereo isn't metric (headset session 2026-10-22, 22:2x-00:19)

**Report (user, PS VR 2 on SteamVR, after the guided calibration):** the sniper's scope window and the model's own
scope lens (its blue glow) were in different places, and so for the rifle; the sniper's shots landed reliably to the
right of the crosshairs, the pistol's similarly; the assault rifle showed no sight.

**What the snapshot shows.** `snapshots\2026-10-22_23-51-45-233` has no gun drawn, but it has distant scenery in both
eyes (Eden Prime's far tower and sky). The eye images are crops of a symmetric +-61.5 degree render (2824 px wide; the
left eye's rect is x 0-2138 for -61.5..43.4 degrees, the right's x 686-2824 for -43.4..61.5). For anything far away
the true geometry puts it 685.6 px further right in the left image than in the right (the two crops' offset).
Matched by normalized cross-correlation of 120 px patches at four places: **652, 649, 652, 651 px**. Each image's
content is moved 16.9 px outward: **1.26 degrees per eye**, the left image left, the right image right.

**Why (read in `src/halcyon/le1`).**
- Stereo 2 renders each eye from the eye midpoint +- `sfr2HalfEyeUU` (3.4 UE units) along the camera's right
  (`render_hook.cpp` `ApplyReplayProofOffsetSEH`; swap-eyes flips the sign and the routing together, so each eye
  gets its own side). At the view's 100 units per meter that's 3.4 cm, against the headset's 3.1 (the snapshot's
  eyes are 6.21 cm apart). Upstream calls it a world-scale knob ("render disparity moves, tags stay put").
- At submit each finished image is shifted by half the convergence (`sfr2Convergence` 0.012) in UV:
  `CopyStereoPassCaptureToEyes` passes `-conv/2` to pass 0 and `+conv/2` to pass 1, and the blit samples
  `u + sourceX`, so pass 0's content moves right by 0.006 of the image. With `sfr2SwapEyes = 1` (the default),
  pass 0 is the right eye: the right image moves right, the left image left, so far things need the eyes to
  diverge (Halcyon's comment: with swap on "the convergence plane INVERTS"). 0.006 x 2824 = 16.9 px; in angle
  0.006 x 2 tan(61.5) = 0.0221 tangent units, 1.27 degrees at the centre. Measured above.
- The layer declares the runtime's true eye poses (`[EYETAG]`), so the runtime shows each image as the true eye's
  view. Our quads (the dot, the scope disc, the window ring, the hand ring) are composited at their true, metric
  place for those same true eyes.

**What that does to a quad seen by one eye** (the right eye, which aims):

| distance | image vs truth (right eye) | at that distance |
|---|---|---|
| 0.1 m (eyepiece) | -0.45 degrees (the 3 mm wider render wins) | 0.8 mm |
| 0.2 m | +0.41 degrees | 1.4 mm |
| 0.3 m (the sight) | +0.70 degrees | 3.6 mm |
| 0.5 m | +0.93 degrees | 8 mm |
| far (the target) | +1.27 degrees | 22 cm at 10 m, 44 cm at 20 m |

- **The shots:** the dot is collimated along the true barrel; the target under it in the right eye's image is truly
  1.27 degrees to its left. Put the dot on the target and the bullet (along the true barrel) lands 1.27 degrees to
  the target's **right**: the report, for the pistol's dot and for anything aimed with the right eye (the floating
  ring reticle, seen by both eyes, reads the same to a right-dominant eye).
- **The scope:** its picture magnifies the left eye's image (`source = other`), projected with the left eye's true
  pose: that image's content is 1.27 degrees left, so the reticle showed a point 1.27 degrees right of the true
  line. By itself that would put scoped shots left of the reticle; the report's "right" fits the ring reticle or a
  right-eye read, or a scope picture that wasn't up for those shots (that session's log is gone). Either way the
  picture was 5 degrees off at 4x. **[verify]** next session.
- **Depth:** binocularly the game's picture sits further away than our quads (at 0.4 m: 0.49 m; far things beyond
  infinity, the eyes diverging 2.5 degrees). A quad at the model's true place reads nearer than the model: the
  calibration orb "closer than it is", and "the blue orb for the model and then the window for the sniper sight".
- **Hypothesis "shots and sight use different frames": refuted.** Since calibration both follow the drawn gun:
  the barrel is grip x trim, the shots go along `weapon::Current()`'s barrel from its muzzle (log 00:17:12 `the
  shot started at the muzzle (0.00 units off) and went along the barrel`), and the sight is placed on that same
  muzzle and barrel (`hand_aim::AimRay`). The pistol stance's frame is 9.4 degrees right of and 6.4 below the
  runtime's aim pose (the snapshot's controllers against `pistolTrim`), but nothing uses the aim pose with a gun
  drawn.
- **The "273 units from the muzzle" warnings** (00:17:13; `1216 units` at 00:14:22) weren't our shots: the
  GetAdjustedAim handler counted every weapon's aim query after ours, and Ashley's and Kaidan's shots start where
  they stand. Ours: 14 of 14 from the muzzle.

**The fix is in the picture, not in our quads (decision 2026-10-23).** A per-eye workaround was built and taken out
again the same day: drawing each one-eye quad where that eye's image shows the point (and sampling the scope's source
image the way it was drawn) works only for one-eye quads and has to track every Stereo 2 knob. Instead a separate job
gives Halcyon a metric stereo mode (the runtime's eye poses, its asymmetric FOVs, no convergence shift), so the game's
picture matches our metric quads; the sights stay metric and on the model's real place (section 11). Until that mode
is on, expect the dot, the scope and the ring reticle about 1.27 degrees left of what the bullets hit for a right
aiming eye. A partial stopgap is the user's to try (Halcyon's settings): Advanced > Stereo > Eye distance 3.1 and
Convergence 0 removes the shift; the symmetric render window stays. (Built the same day as life-size stereo,
[research/30](30-metric-stereo.md), `sfr2Metric`, on in the deploy; `tools/stereo-check.py` repeats this
section's measurement on any snapshot.)

## 10. Shots that land on the dot (built 2026-10-23)

- A real sight is zeroed: its line and the bore cross at some range. Ours was parallel (`zeroM = 0`): the bullet
  lands the sight's height (4-7 units) under the dot at every distance. VR shooters converge instead: trace the
  world along the sight line, and fire from the muzzle at what it hits.
- **Where ME1 starts its traces:** the pawn's `GetWeaponStartTraceLocation` (our handler moves it to the muzzle;
  research/14) along `GetAdjustedAim` (our `GetAdjustedAimFor` answer, plus the weapon's random aim error), traced
  by `0xC589F0` to `GetTraceRange()`. So the shot's start is the muzzle and its direction is ours.
- **Built:** each tick, with a sight on the held gun and `[Sights] converge = 1`, `game/sights` traces
  `Actor.Trace` (through ProcessEvent on the pawn, which it ignores; bisection if the hit point doesn't come back,
  as game/throwables does) from the sight's centre along its line, 300 m. At the shot, hand aim rebuilds the sight
  line from the live muzzle (the place on the gun, the tick's top) and aims from the muzzle at the point that
  distance along it (`vr::sights::ConvergedShot`, unit-tested; parallel when that point isn't 20 units past the
  muzzle). Menu: HUD & sights > "Shots land on the dot".
- **Logs:** `hand aim: shot N: from the muzzle (0.00 units from the game's start) toward what the sight line hit,
  D m past the sight (actor): A degrees off the sight line, B off the barrel` (the first three, then every 2 s); the
  heartbeat's `N of them toward what the sight line hit, A degrees off the sight line on average`; `sights: the
  sight line's trace (Actor.Trace through ProcessEvent): the hit point came back`. Snapshot: the mod's
  `"handAim"."shots"."lastShot"` and `"sights"."converge"`.
- **Risks:** the trace may stop on something the bullet's own trace doesn't (an invisible blocking volume): the shot
  then bends for a wrong distance, at most the sight's height over that distance. A target edge is seen a tick late.

## 11. The sight on the model; a red dot that shows (built 2026-10-23)

- **No sight sockets:** the assault rifle's mesh has `Flash_1`, `Flash_2`, `Tracer`, `Cooldown`, `Sabotage`, all on
  `Root` (log 00:14:11). The other guns' lists come with the next session.
- **Built:** once per held weapon, `weapon::ModelInGunFrame` reads the mesh's bounds (`USkeletalMesh.Bounds`
  +0x60, the reference pose) and its bones and sockets in the gun's own frame (back from the muzzle, up toward the
  sight's top, right), logged as `sights: the <gun> model against its bore (gun units): bounds a..b units behind
  the muzzle, c..d above the bore ...; Root (...); socket Flash_1 (...)`. A bone or socket named for a sight
  (scope, sight, optic, lens, eyepiece, reticle) is where auto puts it; otherwise auto keeps the length share for
  `back` and takes the height from the model's top (`vr::sights::HeightOnModel`): a scope's eyepiece a lens radius
  under it, a dot's window one unit over it. `<type>Place` still overrides. **[verify]** with a snapshot per gun:
  the eyepiece on the model's scope, the window ring just over the gun.
- **The assault rifle showed nothing:** its holo needed the eye's line within the 3-unit window (`no eye through
  it` all session; the one hit at 00:17:21 was 2.9 cm off). Now the eye counts as looking through within `[Sights]
  eyeBox` (6 units) of the window's centre, one eye (the nearer, with hysteresis: `vr::sights::AimingEye`), and the
  dot stays collimated. The shotgun and the assault rifle default to `dot`; a settings file from before keeps any
  choice but the old default `holo`, which becomes `dot` once (log `hands: [Sights] shotgun and assaultRifle holo
  (the old default) -> dot`).

## 12. A calibration without the rifle step

The user's file has only the pistol stance (the rifle step kept cycling). Long guns then used the runtime's aim pose,
9 degrees from where this controller's grip points for the player. Now (`body_calibration::StanceFor`) they take the
pistol's grip frame but not its sight line (`StanceBorrowed`: arm IK doesn't move the gun onto a pistol's line); the
two-handed grip and the shoulder zone still need the rifle step. A stance whose aims disagreed by more than 10 degrees
isn't used (logged).

## 13. A scope too small at a real hold; the assault rifle's sight off the model (headset session 2026-10-25, 18:22-18:29)

**Report (user, PS VR2 on SteamVR, life-size stereo):** the sniper scope, held up, was "very small", as if the eye had to
be much closer than possible; the assault rifle's sight was "not aligned well at all". No snapshot that session shows a
gun (the 18:36-18:39 series is holstered), so this is from `LE1MEVR.prev.log` (pid 21840).

### The scope: why it was small

- `[MEVR-SIGHT] first scope picture: the right eye aims (0.485 m behind the eyepiece, 0.0316 m off the line, pupil
  0.20)`. The picture was the physical eyepiece: a 2-unit (2 cm at 100 units per meter) radius disc, about 4.7 degrees
  across at 0.485 m, and past 1.5 eye reliefs (18 cm) the exit pupil shrinks it to a fifth: **under 1 degree** of
  picture. The alignment assist lines put the eye 0.31-0.61 m behind the sight all session. A real scope's eye relief
  is about 8 cm (this file's `eyeReliefM` 12 cm); a headset's lenses and a tracked controller can't get there.
- **And it was on the wrong bone:** the sniper rifle's model has `Scope_Lens` (44.0 units behind the muzzle) and
  `Scope_Eye` (59.2 behind, 7.0 up). Auto took the first bone with "scope" in its name, the front lens, 15 units
  further from the eye than the model's eyepiece.

**How VR shooters handle it (one search, 2026-10-25).** Little is documented. A
[BetaHub feature request](https://app.betahub.io/projects/pr-7682550116/feature_requests/2669) asks for a reticle and
picture that keep a fixed size whatever the eye's distance from the lens, saying that's how H3VR, Onward and Pavlov handle
optics (the requester's claim, not verified), with the size a player setting. The
[Sniper Elite VR thread](https://steamcommunity.com/app/752480/discussions/0/3049483485751256789) (section 1) prefers
Pavlov's magnified sights to a scope whose picture follows the gun. Real-scope guides describe the opposite trend for
glass (too much eye relief shrinks the picture), which is exactly what our physical model did. Overlord's ADS
eye-distance assistance (section 1) slides the gun up to 15 cm toward the eye; ours can't close 30-60 cm. So: a
**virtual eye relief**.

**Built: the virtual eyepiece** (`vr::sights::VirtualView`, `PictureRadius`; host `plugin/SightLayers`, shader
`plugin/SightPictures`; unit-tested `TestSightVirtualEyepiece`; offline `mevr-sight-preview`: `scope-virtual-*.png`,
`eye-right-virtual-*.png`, and `eye-right-physical-45cm.png` for the old look).
- The picture is a disc in the eyepiece's plane, centred on the eyepiece, whose radius is `along x tan(fieldDeg / 2)`
  (never under the lens's own): it looks `[Sights] scopeFieldDeg` across (22 degrees: a 4x hunting scope's apparent
  field is about 20-25 degrees) wherever the eye is behind it, up to `scopeMaxM` (0.70 m); it fades out by 1.25 times
  that. Shown to the aiming eye only, as before; the other eye sees the world (both eyes open, like a real scope).
- **The magnification stays true across the picture:** the lens point L still shows the direction `axis x along + (L -
  Q) / zoom` (section 4): a point seen r off the centre shows the true angle atan(tan(r) / zoom). At 22 degrees and 4x
  the picture holds 5.5 degrees of the world. The reticle stays centred on Q, the eye's line along the sight
  (collimated).
- **The shadow:** the exit pupil's centre goes one picture radius off for each `scopeEyeBoxM` (0.05 m) the eye is off the
  line (on the eye's side, like the physical lens's), so a centred eye sees all of it, an eye 2.5 cm off a crescent,
  5 cm off half, 10 cm off nothing. The aiming-eye box widens to two eye boxes. A dark rim (84-97% of the radius) frames
  it like a tube's edge.
- `scopeFieldDeg = 0` keeps the physical lens (`lensUnits`, `eyeReliefM`) exactly as before.
- **Metric:** the disc is a quad at the eyepiece's real place; its content is the other eye's image projected with that
  image's own pose and FOV (unchanged), so it agrees with life-size stereo (research/30). Its size is an angle at the
  eye, so the view's scale doesn't change it.
- **Resolution:** 22 degrees of picture at 4x come from 5.5 degrees of the eye image (about 24 pixels per degree): about
  130 source pixels across, enlarged. Soft, as section 7 said; the picture texture (768) isn't the limit.
- **Logs:** `[MEVR-SIGHT] first scope picture: ... virtual eyepiece: the picture N degrees across (... m at the
  eyepiece; set N), N degrees of the world at 4.0x, eye box ..., full to ... m behind it`; when the picture goes, `sights:
  the scope's picture was up N frames: the eye A..B m behind the eyepiece (C on average), D cm off the sight line on
  average, the picture E degrees across on average, its shadow F picture radii in (1 = half shaded), opacity G`.
  Snapshot: the host's `sights.scope` (`fieldDeg`, `along`, `pictureRadiusM`, `pictureDeg`, `pupilCentre`, `fade`).

### The assault rifle's sight: the causes in the log

1. **The sight's bone is animated, and was measured mid-unfold.** The rifle's model has a `Scope` bone. Its first
   measurement (18:23:25, after the grip measurement's hold) put it at 54.9 back, 4.9 up, 3.8 right; every later draw
   (18:25:36 onward, the grip remembered, so measured on the draw's first tick) at **48.3 back, 0.8 up, 2.2 right**, and
   that stayed for the rest of the session, including the whole rifle-calibrated stretch (18:27:29-18:27:56). ME1's guns
   unfold for about a second after a draw; measured once, the window sat 0.8 units over the bore, inside the receiver,
   6.6 units behind where the unfolded part goes. **Evidence: the two log lines. Fixed.**
2. **The model was measured against the controller's top, not the drawn gun's.** On that first tick the engine still
   shows the gun as posed before arm IK aimed it. Bones that lie in a model's symmetry plane came out rolled about the
   bore: the pistol's `Barrel_Upper_front`, `Barrel_Lower_front`, `Guard` and `handle` all at side = 0.59 x up (30
   degrees), the rifle's `handle` and `Trigger_Guard` about 21 degrees (`squash_left` and `squash_right` tilted the
   same), the sniper's 13 degrees, while arm IK's own line says the thumb-side up is 176-180 degrees from the socket's
   up, the same every draw. And the side offset was dropped (the rifle's 3.8 units: the dot sat 3.8 cm left of the
   bone). **Evidence: the bone lists. Fixed** by following the bone in the world (below), side included.
3. **The model's top from the mesh bounds was 15-22 units** (pistol 15.1, shotgun 17.2, sniper 22.1) on guns whose bones
   reach 3-7: the bounds are a loose culling box (the pistol's is 33 units tall). The pistol's and shotgun's windows sat
   16-18 cm above the bore. **Fixed** (the top from the bones).
4. **Two-hand aim turns the barrel off the calibrated stance.** With the support grip held, `two-hand aim (the grab's
   line), swing 23.0 / 11.0 / 9.6 degrees`: the barrel follows the hands' line, while the calibrated rifle stance's
   sight-line shift (`its sight moves 5.8 cm up and 2.4 cm right onto your sight line`, 18:27:29) is computed for the
   one-handed hold. The rifle stretch was fired two-handed and says `no eye through it` in every heartbeat but one
   (10 assist ticks, the eye 1.5 cm right of and 1.9 cm above the line, 7 degrees). **Plausible, not settled; not
   changed** (it's the support grip's and the calibration's, VRIK's core): the next session's heartbeat lines settle it.
5. **The eye sits about 4 cm above the sniper's sight line** (heartbeat 18:28:04-18:28:49: 3.5-4.2 cm above, under 2.5
   cm sideways, 6-7 degrees, every 5 s). Partly causes 1-2 (the front-lens bone 2 units lower than the eyepiece bone,
   measured in a rolled frame). The assist's 10-degree cone (full within 5) weighted it at 10-50%. **Partly fixed by the
   bone; the next heartbeat says what remains** (a steady offset left: `assistConeDeg`, or a placement).
6. Ruled out: the stereo (life-size, `[MEVR-STEREO] stereo mode 1`); the shots (`hand aim: shot N: from the muzzle ...
   0.002 degrees off the sight line`: they follow the sight line as placed).

**Built (mod, hot reload; the host for the scope):**
- `vr::sights::SightBone`: a scope takes a bone named for its eyepiece ("eye", "ocular" or "rear" beside a sight word:
  `Scope_Eye`) before the front lens; a dot the first sight-named bone; sockets never.
- `weapon::BoneWorld`: that bone's live world place every tick (the mesh's pose and transform); the sight is placed
  there (`back`, `height` and the new `side`, `sv::Place(..., sideM)`), so it rides the model while the gun unfolds and
  however the model is rolled. The arm-IK stance shift (`SightInHand`) and the shots' rebuilt sight line
  (`ShotAlongSight`) take the side too.
- The model's bones are re-read every 0.25 s; without a sight bone the height comes from the bones' top
  (`vr::sights::ModelTop`: the highest bone between the muzzle and 1.6 gun lengths back, within 8 units of the middle),
  the bounds kept only for the log (`boundsTop`).
- `<type>Place` takes a third number, the side (`back, height, side`).
- **Logs:** the model line now says `as first drawn: it may still be unfolding and not yet aimed`, the bones' top and
  which bone auto follows; `sights: the <gun>'s sight sits on its '<bone>' bone: B units behind the muzzle, U above the
  bore, R to its right (still for 0.5 s, T s after the draw; it moved M units before)` once it settles (again if it
  moves); the heartbeat's `placed B back, U up, R right`. Snapshot: the mod's `sights.place` (`side`, `bone`,
  `boneLive`, `boneSettled`, `modelTop`, `boundsTop`).
- **[verify]** with a snapshot per gun: the window ring on the model's sight part, the scope's picture over the model's
  eyepiece; the rifle's settled numbers near 54.9 back, 4.9 up (the unfolded measurement).

## 14. Scopes pulled to the eye; the assault rifle's scope; the sniper's lens hidden (built 2026-10-27)

**Report (user, PS VR2 on SteamVR, life-size stereo, 2026-10-26):** the sniper rifle shows "a blue light that shines on the
model itself" with the gun at the face; the scope's picture doesn't line up with the model's scope; the assault rifle (the
starting rifle) has a scope on its model, and its red dot floated on the back of the housing.

**What the log shows.** `sights: the scope's picture was up N frames: the eye 0.23..0.24 m behind the eyepiece`: at that
hold section 13's 22-degree virtual eyepiece is a disc about 9 cm across over a model eyepiece about 4 cm across. The
cause is the model: the sniper's `Scope_Eye` is 59.2 units behind the muzzle (7.0 up), its grip (`handle`) 66.6, so the
eyepiece sits about 7 cm AHEAD of the hand, where a real rifle's is behind it, about 8 cm from the eye. `Scope_Lens` is 51.4
back, 3.2 up. The assault rifle's `Scope` bone settles at 39.6 back, 9.1 up, about 0 to the side, its grip at 39.2: a sight
housing right over the hand, with a red dot in front of it, not through it.

**Decisions (user, 2026-10-27).** "Pull to the lens": a scope raised to the eye slides back along the sight line toward
the eye (Overlord's ADS eye-distance assistance, up to 15 cm, section 1) until the model's eyepiece is at a comfortable
virtual eye relief, and the picture is drawn on the model's own eyepiece, framed by its tube (the H3VR / Pavlov / Onward
look); the virtual hand moving about 13 cm toward the eye is accepted. The assault rifle gets a low-power scope, about
1.5x, the same way. The sniper's glowing lens part hides while the picture is up.

### The near plane

- The camera's near plane is the local player's `NearClipPlane`: 10 game units by default (Halcyon's render hook read it
  live next to `DynamicResolutionFraction`: `NearClipPlane == 10.0`; MassFPS pulls it in to 1, research/11). Life-size
  stereo only replaces the projection's x and y terms (`core/vr/MetricStereo` `OffCentre`), so the engine's near plane
  stands. At the life-size 100 units per meter that's **0.10 m**; at Halcyon's own 50 units per meter, 0.20 m.
- Anything nearer than that to an eye is cut away: an eyepiece pulled inside it loses the tube's rear for both eyes (the
  aiming eye sees our picture on top, but the tube around it is cut; the other eye sees the cut).
- So the relief is `max(scopeReliefM, near / unitsPerMeter + 2 cm)` (`vr::sights::ReliefFor`): **0.12 m** with the
  defaults at life-size (the setting and the floor agree). From the reported hold (0.235 m) the pull is 11.5 cm; the
  user's expected 13 cm would put the eyepiece 5 mm past the near plane. The mod reads the field each tick through our
  reflection layer (`Engine.PlayerController.Player`, `Engine.LocalPlayer.NearClipPlane`) and logs it: **[verify]** the
  line says `LocalPlayer.NearClipPlane`, not `UE3's default`, and that this field is what the projection uses (nothing
  here has checked it; a visible cut of the tube at 0.12 m would say it isn't).
- Pulling the near plane in (as MassFPS does) would allow a shorter relief; not done (it changes the whole view's depth
  precision, and nothing has asked for it).

### Built (mod: `game/sights`, `game/scope_parts`, `core/vr/Sights`; unit-tested)

1. **The pull** (`vr::sights::AssistTarget` with a `Pull`): for a scope, besides the sideways move, the gun slides back
   along the sight line by `clamp(along - relief, 0, scopePullMaxM) x weight`, where `along` is the aiming eye's
   distance to the eyepiece measured on the gun without the assist's own move, and the weight is the sideways move's
   (the 10-degree cone, the range, the head, `assistStrength`). Never forward, never past the relief. The sideways move
   keeps its own 10 cm cap. The same spring eases both in and out (10/s, at most 0.25 m/s: the 11.5 cm take about half a
   second); lowered, the gun eases back. Off with `assist = 0` or `scopePullMaxM = 0`; dots never pull.
   - **The hands follow** as for the sideways move (`engine/BonePose`): arm IK adds `Request::gunShift` to the right
     wrist's goal after reach scaling, and to the left wrist's while the support grip holds (fading with its blend after a
     release), before the support grip ties the hands; a free left hand stays. The muzzle, the shots, the reticle's ray
     and the sight follow the gun as drawn (the shots start 11.5 cm nearer the face, along the same line).
   - **Logs:** `sights: the camera's near plane is N units (...): a scope is pulled to R m in front of the eye ([Sights]
     scopeReliefM ..., at least the near plane + 2 cm), at most M m; the <gun>'s eyepiece (L units, D cm across) then looks
     F degrees across` when any of it changes; `alignment assist on: ...; the scope is pulled N cm back along its line, to
     R m in front of the eye`; `alignment assist off ...; the scope was pulled at most N cm: the eye A m behind the
     eyepiece before, B m after (relief R m[: the pull's limit was reached])`; the heartbeat's `the scope pulled N cm
     toward the eye on average`. Snapshot: the mod's `sights.assist.pull` (`reliefM`, `maxM`, `goalM`, `nearUnits`,
     `nearFromGame`, `raisePulledM`, `raiseEyeBeforeM`, `raiseEyeAfterM`).
2. **The picture on the eyepiece.** Our picture is a quad composited over the eye image with no depth: drawn bigger than
   the model's eyepiece it covers the tube, smaller it leaves the eyepiece's own (glowing) face showing around it. So
   with the pull the picture's apparent size is what the model's eyepiece subtends from the relief, `2 atan(lens /
   relief)` (`vr::sights::PulledFieldDeg`), sent to the host as the sight's `scopeFieldDeg`. The host's virtual eyepiece
   (section 13, `PictureRadius = max(lens, along x tan(field / 2))`) then gives exactly the eyepiece at the relief and
   nearer, and the same angle further back: when the pull can't reach (the eye more than relief + 15 cm back) the picture
   grows past the eyepiece rather than shrinking (section 13's fallback, never tiny). Numbers at life-size: the sniper's
   eyepiece 2 units (4 cm across) looks 18.9 degrees at 0.12 m; held at 0.315 m and pulled 15 cm, the picture is 5.5 cm on
   a 4 cm eyepiece; the assault rifle's 1.6 units (3.2 cm) looks 15.2 degrees. `scopePullMaxM = 0` keeps section 13's 22
   degrees. **No host change for the picture:** the sight the mod and the host share is the same struct, only its
   `scopeFieldDeg` is chosen by the mod; the picture's source image and `RenderScope`'s sampling are untouched (a separate
   job replaces the source with a real zoomed render).
3. **Per-gun eyepieces** (`[Sights] <type>Lens`, gun units; `vr::sights::LensUnits`; else the old global `lensUnits`): sniper
   rifle 2 (the old global, which the report's "about 4 cm" eyepiece came from), assault rifle 1.6. Both are guesses from
   the bones **[verify]** with a snapshot: the picture should cover the model's eyepiece and no more.
4. **The assault rifle's scope:** `assaultRifle = scope 1.5` by default (the parser takes decimals; zoom 1-12). A file
   from before keeps any choice but the old default `dot`, which becomes `scope 1.5` once (`hands: [Sights] assaultRifle
   dot (the old default) -> scope 1.5`; the marker is the new key `scopeReliefM`). Its eyepiece: the `Scope` bone isn't
   named for an eyepiece, so it's taken as the housing's middle and the picture goes `HousingToEyepiece` (1.5 units for
   the rifle, 2 sniper, 1.5 shotgun, 1 pistol) behind it **[verify]**: on the housing's rear in a snapshot, else
   `assaultRiflePlace`. As for any scope, its shots get the zoomed aim error while its picture is up (section 4, the same
   spread as the support grip's LT).
5. **The sniper's glowing lens hidden** (`game/scope_parts`): while the held gun's scope picture is up (the host's
   `sightView`), the bones in `[Sights] <type>Hide` (sniper rifle `Scope_Lens` **[verify]**: which bone carries the glow is
   a guess) are hidden with `SkeletalMeshComponent.HideBone` (index, `PBO_None`), shown again with `UnHideBone` 0.15 s after
   the picture goes, on a weapon switch or holster, when the list or the sight's bone changes, and when the mod unloads
   (hot reload: the host's tick, the game thread). Through our reflection layer and ProcessEvent on the game thread (rows in
   `engine/ReflectCheck`), never a generated wrapper; by index rather than `HideBoneByName` because the bones are known by
   index already and `IsBoneHidden` (same class) tells a bone the game hid itself, which is then left alone. Never hidden
   (`vr::sights::PartsToHide`): the root, the sight's own bone and its ancestors (a hidden bone is drawn collapsed and its
   children with it **[verify]**: the sight would move). Logs: `sights: the sniper rifle's scope picture is up: hid
   'Scope_Lens' while it shows ([Sights] sniperRifleHide)[; not hidden: ...]` once per model, `sights: showed 'Scope_Lens'
   again (<why>)` (the first three, and always when the mesh is gone); snapshot `sights.parts`.
6. **The material log**, once per weapon model: `sights: the <gun> model is <mesh>`, then each of the component's material
   overrides and the mesh's materials with, for a `MaterialInstanceConstant` (and its parents, 3 deep), the scalar, vector
   (RGBA) and texture parameters by name. A blue vector or an emissive-named one identifies the glow, and so the part
   (`<type>Hide`) or a parameter to change, if the bone guess is wrong.
7. **The menu:** HUD & sights' "Shotgun and assault rifle" row has a Scope 1.5× chip: it writes the assault rifle's
   (`scope 1.5`, or its earlier zoom) and leaves the shotgun's; the other chips set both (host: `plugin/MainMenuSettings`,
   `assets/menu/main.rml`; `MenuSettings::SightValue` takes the fallback zoom).

**Offline check** (`mevr-sight-preview`): the scene's tube now has a glowing blue eyepiece; `eye-right-pulled-sniper-4x.png`
shows the picture exactly on it with the tube's rim around and no blue (`-model.png` the model alone),
`eye-right-pulled-sniper-at-limit.png` the picture 5.5 cm on the 4 cm eyepiece (over most of the rim),
`eye-right-pulled-rifle-1.5x.png` the rifle's, and `eye-right-unpulled-sniper-23cm.png` the report's hold without the pull
(a 9 cm picture well past the tube).

**[verify] next session (notes/next-vr-session.md step 43):** the near plane line (read from the game, 10 units); the pull
feels right (about 11.5 cm, half a second) and the gun goes back when lowered; the picture covers the sniper's eyepiece
and no more (`sniperRifleLens`); `Scope_Lens` is the glow and hiding it leaves the rest of the scope (else the material
lines name the glowing material); the assault rifle's picture sits on its housing's rear (`HousingToEyepiece`,
`assaultRifleLens`); nothing stays hidden after a switch, a holster or a hot reload.

## 15. Aiming down the sight: the scope only at the eye, Overlord's ADS (headset session 2026-10-27, built the same day)

**Report (user, headset, 2026-10-27):** the assault rifle shouldn't zoom: "it should just be a red dot", but it "doesn't
line up perfectly"; "I would almost expect when I pull the rifle up to my eye, that's when I would start seeing the scope.
Instead right now I just see that little circle, which the scope kind of lives in." And: can we pull in better systems
from Overlord?

**What the log shows** (`LE1MEVR.log`, `sights:` lines 14:52-14:57):
- **The assist's gate flickered.** `alignment assist on ... 8.5 degrees` then `off ... 9.1 degrees` 20 ms later, again
  and again, every 0.02-1 s: the aiming eye sat at the edge of section 8's 10-degree cone (full within 5), where the
  weight runs from 0 to 0.2 over one degree. The sideways slide (up to 10 cm) and section 14's pull (up to 15 cm) both
  rode that weight.
- **The picture at arm's length.** The scope's picture was up with the eye 0.45-0.57 m behind the eyepiece and about 9 cm
  off its line, its shadow about 1.9 picture radii in (almost all shaded): the "little circle". `scopeMaxM` 0.70 (section
  13) let it show that far back.
- **The pull too short.** Even at its 15 cm limit the eye stayed 0.16-0.20 m behind the eyepiece (relief 0.12 never
  reached): the model's eyepiece sits 7 cm ahead of the hand (section 14), so a hold "at the eye" is 0.31-0.35 m from it.
  The picture was then about 15 degrees across, smaller than the eyepiece's 18.9 at the relief.

### Overlord's ADS (reference/Overlord, GPL-3.0; adapted, `src/core/vr/Ads.*`, NOTICE.md)

`docs/vr-native-ads.md` ("Controls", "ADS eye-distance assistance", "Physical-lens image magnifier") and
`src/client/component/vr/gameplay/`:
- **A corridor with dwell and hysteresis** (`ads_alignment.hpp` `measure`/`inside`, `controller_ads.hpp`
  `ads_policy::consume`): the centre eye against the muzzle in the muzzle's frame, measured on the gun WITHOUT the ADS
  translation (`hands::sub(muzzle.position, muzzle.ads_translation)`), so the assistance can't feed its own gate. Entry:
  the head 0.15-1.5 m behind the muzzle, within 9 cm of the bore line sideways, 2.5 cm below to 16 cm above it, facing
  within 30 degrees of the barrel; exit 0.10-1.7 m, 13 cm, -6/+22 cm, 40 degrees. The far depth is extended by
  sight-to-muzzle + 0.60 m (`sight_reach_meters`) past 1.5 m, so long barrels don't use up the reach. 100 ms in (out)
  before ADS turns on (off); 40 ms for the thermal.
- **A continuous approach weight** (`ads_alignment::approach`): quintic smoothstep of the head-to-barrel angle (full at
  5 degrees, none at 45) times smooth corridor edges between the entry and exit limits, independent of the ADS latch;
  times a distance weight (`ads_comfort::distance_weight`: the eye to the rear lens, full within 0.30 m, none at 0.60;
  thermal 0.50 / 0.70). The product drives the eye-distance transition, a critically damped spring with a speed limit
  (`transition::update`: 10/s, 0.25 m/s; thermal 24/s, 0.9 m/s), which section 8 already took over.
- **Per-optic pull** (`ads_comfort::distance`, `measure_eye`): red dots and iron sights none, holographic 5 cm, the 4x
  ACOG 10 cm, scopes 15 cm, the thermal 45 cm; the goal is `clamp(depth - relief, 0, distance)` with an 8 cm relief (5
  for the thermal): never pushing a close optic away.
- **The optic's image only while ADS is active** (`optic_view.hpp` `view::active`; the doc's magnifier): below it the
  scope model is shown as authored, its lens part visible. The picture is the physical rear lens with a circular exit
  pupil on the aiming axis and "scope shadow" outside it.

### What we built (mod only: `core/vr/Ads`, `core/vr/Sights`, `game/sights`, `game/hand_calibration`; the menu's default)

1. **The assault rifle is a red dot again** (`assaultRifle = dot`; `Settings::guns[2]` a dot that keeps the model's
   1.6-unit eyepiece for `scope 1.5`). One-time migration back: a file without the new key `ads` whose value is exactly
   `scope 1.5` (section 14's migration wrote it; case, spaces and `1.50` aside: `vr::sights::MigrateAssaultRifle`)
   becomes `dot` (`hands: [Sights] assaultRifle scope 1.5 (section 14's default) -> dot`); any other value is kept and
   logged. Section 14's own migration (dot -> scope 1.5) is gone. The menu's chip row starts with Red dot; "Scope 1.5x"
   stays selectable.
2. **ADS for scopes** (`vr::ads::Measure`, `Inside`, `Policy`; `[Sights] ads = 1`): Overlord's corridor, dwell and
   hysteresis exactly, on the centre eye (the head pose's position) against the muzzle and sight minus the shift arm IK
   applied the tick before (`gTakenPrev`, as section 8's assist), plus **our reach**: the centre eye within `adsReachM`
   (0.40 m) of the sight's centre to come on, 0.50 m to go off. Overlord's far depth alone (1.5 m behind the muzzle, the
   sniper's eyepiece 0.59 m behind it) would admit the eye 0.9 m back: arm's length, the report's "little circle". 0.40
   is between the report's two holds, casual (0.45-0.57 m from the eyepiece: never ADS) and up to the eye (0.31-0.35:
   ADS), and the pull's own move never counts (the unassisted gun). **The scope is published only under ADS**: below it
   the mod sends the host no sight (kind None), so there is no picture, the model's own lens shows (game/scope_parts
   hides `<type>Hide` only while the picture is up, now also requiring ADS), the ring reticle stays on the aim ray, and
   the scope capture (research/39) doesn't render. Under ADS the published `scopeMaxM` is the reach's exit + 0.10 m, so
   the host's own range never cuts the picture; `[Sights] scopeMaxM` applies only with `ads = 0` (section 13's gate).
3. **The continuous weight replaces the cone** (`vr::sights::AssistTarget(..., weight, ...)`; `Assist::coneDeg`,
   `rangeM`, `headDeg` removed): weight = `vr::ads::Approach` x `vr::ads::DistanceWeight(eye to sight, ADS)`, times
   `assistStrength`, for the sideways move and the pull of every sight, through the same spring. The aiming eye is still
   the one nearer the line by angle with 1.5 degrees of hysteresis. A red dot or holo keeps a sideways alignment, gentle:
   at most `assistDotMaxM` (0.03 m; scopes `assistMaxM` 0.10). Pulls (Overlord's): a dot none, a holo 5 cm, a scope under
   3x 10 cm, other scopes `scopePullMaxM` (0.15). `assistConeDeg` and `assistRangeM` are no longer read (left in files,
   no longer added, marked unused in a new file's comments).
4. **The pull reaches the relief under ADS**: the limit becomes `scopeAdsPullMaxM` (0.35 m) with Overlord's thermal spring
   (24/s, 0.9 m/s), also while a pull past the ordinary limit eases back. From the session's "at the eye" hold (0.31-0.35 m)
   the eyepiece comes to the 0.12 m relief in about a third of a second; 0.35 covers a hold up to 0.47 m (the reach's exit
   is 0.50). The hands follow (arm IK's `gunShift`, both wrists while the support grip holds): with the sniper's eyepiece
   ahead of its grip, the right hand ends about 5 cm in front of and below the eye **[verify: how that feels]**.
   **Relief: kept at `scopeReliefM` 0.12 m**, floored at the near plane + 2 cm (`ReliefFor`), which with the default
   10-unit near plane at 100 units per meter is the same 0.12. Overlord's 8 cm would put the eyepiece 2 cm inside the
   near plane (the tube cut for both eyes, section 14), so max(near + 2 cm, 0.08) = 0.12 either way; the line `sights: the
   camera's near plane is ...` says which.
5. **The picture at the eyepiece**: unchanged from section 14 (the host's virtual eyepiece with the field the model's
   eyepiece subtends at the relief, `PulledFieldDeg`: at the relief the picture is the eyepiece, its rim the tube's), now
   that the eye actually gets there; the exit-pupil shadow stays (`scopeEyeBoxM`). Nothing on the host changed.
6. **Logs**: `sights: ADS on (the centre eye D m behind the muzzle, L cm beside the bore, H cm above it, the head F degrees
   off the barrel, R m from the sight; inside the entry corridor for 100 ms): the scope's picture shows and the scope may be
   pulled 0.35 m toward the eye, to 0.12 m in front of it (W m wanted now, the right eye A m behind the eyepiece); approach
   weight X`; `sights: ADS off (<which exit limits failed, with their numbers>; outside the exit corridor for 100 ms) after
   T s: ... pulled at most N cm: the eye A m behind the eyepiece before, B m after (relief 0.12 m[: the pull's limit was
   reached])`; `alignment assist on/off` now with hysteresis on the weight (on from 0.10, off below 0.02) and the
   approach and distance weights; the heartbeat adds `ADS N times, on M ticks: the aiming eye E m behind the pulled
   eyepiece on average`. The host's `the scope's picture was up N frames: the eye A..B m behind the eyepiece` should now
   read about 0.12. Snapshot: the mod's `sights.ads` (`gate`, `active`, `entering`, `leaving`, `heldS`, `depthM`,
   `lateralM`, `heightM`, `facingDeg`, `eyeToSightM`, `sightToMuzzleM`, `approach`, `distanceWeight`, `weight`,
   `pullLimitM`, `fastSpring`).
7. **Settings** (`[Sights]`, appended to older files): `ads = 1`, `adsReachM = 0.40` (0..1.2; 0 = Overlord's corridor
   alone), `scopeAdsPullMaxM = 0.35` (0..0.5), `assistDotMaxM = 0.03` (0..0.1).

**Unit tests** (`tests/Main.cpp`): `TestAds` (each corridor limit between entry and exit, the far extension, the reach,
the approach weight's halves and its smoothness across the session's 8.5-9.1 degrees, the distance weights, the per-optic
pulls, the latch's 100 ms in and out and its immunity to every-tick flicker, and the session's holds through the whole
gate), `TestSightPullReachesRelief` (the session's 0.47 m hold: 15 cm leaves the eye at 0.32 m, ADS's 35 cm with the
fast spring reaches 0.12 m in 0.6 s; the picture at the relief is the eyepiece), `TestSightAssist` (the weight continuous,
no jumps), `TestSightSettings` (the defaults, the migration's exact match).

**[verify] next session (notes/next-vr-session.md step 45):** the picture appears only with the rifle at the eye and
never at a casual hold (`ADS on` lines' reach near 0.3-0.4 m); no ADS flicker (`ADS on`/`off` seconds apart, not
milliseconds); the eye reaches the relief (the host's picture-up line about 0.12 m); the 35 cm pull and the hand at the
chin feel acceptable (else `scopeAdsPullMaxM` lower); the lens part is hidden only while the picture shows; the assault
rifle's dot lines up (`alignment assist on` with a gentle move, the heartbeat's average offset); `adsReachM` might want
0.45 for players who hold the gun further out.

## 16. What the game itself says about each gun's sight (read from the game's files, 2026-11-03)

**Question (user, 2026-11-03):** our red dots and scopes are generic; the game has its own look for some weapons. Can the
live game assets give each gun a scope that fits it?

**Sources.** The weapon meshes and appearances in `BIOC_Materials.pcc` (forced exports `BIOG_WPN_*_R.*` and
`BIOG_WPN_ALL_MASTER_L.Appearance.*`; the Geth guns in `SFXWorldResources.pcc`), read with Legendary Explorer's library: the
cockpit packager's `inspect`, and a throwaway dumper for each mesh's bones, sockets and the bounds of the vertices each bone
carries (LOD 0, the dominant influence; reference pose). The stats classes' config in `Coalesced_INT.bin` (length-prefixed
UTF-16 strings). Field names from LExSDKv2's LE1 headers (`SFXWeaponStatsBase`, `ABioWeaponRanged`, `BioSFHandler_Reticule`).
The HUD's reticle movie `GUI_SF_HUD.Reticule` and its textures in `Startup_INT.pcc`. Nothing extracted is in the repository
(game data); the numbers below are our measurements.

### The models: eight shapes, many paint jobs

- Every player gun is one of 8 skeletal meshes, `BIOG_WPN_<type>_R.<variant>.WPN_<variant>_MDL`: `PSTa`/`PSTb` (pistol),
  `BLSa`/`BLSb` (shotgun), `ASLa`/`ASLb` (assault rifle), `SNPa`/`SNPb` (sniper rifle). A type's appearance lists variants (the
  sniper rifle's 12), each one material instance (the manufacturer's colours `HGR_Colour_*`, stripes, the blue emissive tint
  `WPN_MASTER_Emis_ColourTint`) on model a or b with its anim set. One material and one section per mesh: the lens has no
  material of its own; its glow is the shared material's emissive mask.
- No sight sockets on any of them (Flash x2, Tracer, Cooldown, Sabotage, all on `Root`, as section 11 saw on one).
- **The pistols and shotguns have no optic part at all**: their dots and holos are ours alone.
- **The rifles' sight parts** (mesh frame: x toward the butt, z up, gun units; each bone's own vertices' box):

| Model | Muzzle | Sight bones (origin x, z) | Their geometry | Reading |
|---|---|---|---|---|
| SNPa | x -64.7, bore z 5.3 | `Scope_Eye` (-5.5, 12.4), `Scope_Lens` (-20.7, 10.8) | Eye: x -6.1..8.0, 4.2 wide, z 8.5..12.8. Lens: x -26.3..-17.0, 5.0 x 4.8 | a 34-unit scope; the eyepiece tube runs from the bone back to x 8.0, **6 units behind the grip** (`handle` 1.9); its axis at z 10.65, 1.7 under the bone |
| SNPb | x about -66.5 [verify] | `Scope_Rear` (5.8, 10.2), `Scope_Mid` (-7.0, 10.2), `Scope_Forward` (-18.6, 10.2) | Rear: a disc x 6.6..7.2, 3.4 across, centre z 9.85 | the eyepiece glass is its own bone; radius 1.7 units (not 2); 5 units behind the grip; no `Scope_Lens` |
| ASLa | x -39.2, bore z 1.5 | `Scope` (-0.3, 10.65) | x -5.8..7.1, 4.3 wide, z 9.5..13.9 | a 12.8-unit housing over the grip; rear face 7.4 behind the bone, axis z 11.7 |
| ASLb | x about -41.7 [verify] | `Scope` (7.1, 10.2) | x 3.0..9.0, 3.3 wide, z 8.4..12.0 | a short 6-unit sight behind the grip; rear face 1.9 behind the bone |

- The sessions so far held **SNPa** and **ASLa**: section 13's `Scope_Eye` at 59.2 back, 7.0 up is SNPa's (59.2, 7.1) and
  section 13/14's settled `Scope` at 39.6 back, 9.1 up is ASLa's (38.9, 9.15). The reference pose is the unfolded gun for these
  bones. The pistol of 2026-11-03 was `PSTb` (the material log).

**What this changes:**
1. **SNPa's eyepiece is behind the hand, not ahead of it.** Section 14's "the eyepiece sits about 7 cm AHEAD of the hand" came
   from the bone's origin; the tube's rear face is 13.5 units further back. Drawn on the face, a hold "at the eye" (section 15:
   0.31-0.35 m from the bone) is about 0.18-0.22 m from the glass: 6-10 cm of pull reach the 0.12 m relief, within
   `scopePullMaxM`'s 15 cm, and the 35 cm ADS pull (the hand at the chin) is not needed for that hold. The axis 1.7 units under the bone is part of section 13
   item 5's eye "4 cm above the line".
2. **ASLa's housing ends 7.4 units behind its bone** (`HousingToEyepiece` guesses 1.5): with `scope 1.5` the picture sat
   mid-housing, and a dot window belongs at the housing's rear face (section 14's "red dot floated on the back of the housing").
3. **SNPb:** `sniperRifleHide = Scope_Lens` matches nothing on it; its eyepiece glass is `Scope_Rear`, which `SightBone`
   already takes (the "rear" word) and `PartsToHide` never hides (the sight bone). Its lens is 1.7 units, not 2.

### The magnification: per manufacturer, in the config

- `SFXWeaponStatsBase.AimModes` is an array of `WeaponAimMode {ZoomFOV, CameraSensitivityMultiplier, FrictionMultiplier,
  AdhesionMultiplier, bScoped}`; each manufacturer has its own stats class (`ABioWeaponRanged.StatsClassBindings`,
  `GetStatsBinding(ManufacturerID, weaponType)`); the weapon holds `Stats` (+0x4C0) and `CurrentAimMode` (+0x4E0) and has
  `GetZoomFOV`, `IsScoped` and `CycleMagnification`.
- The config: the pistol, shotgun and assault rifle bases have one mode, ZoomFOV 39.6, `bScoped=false` (no manufacturer
  overrides it); the sniper rifle base two scoped modes, 22.6 and 11.4 degrees; Equalizer, Hammer, Harpoon and Volkov 23.9 and
  12.1 (the other sniper classes keep the base's).
- Against the combat camera's 70 degrees (`SFXCameraMode`'s default, not overridden by `SFXCameraMode_Combat`; TightAim's own
  39.6): the unscoped aim is 1.9x; the sniper rifles 3.5x then 7.0x, the four 3.3x then 6.6x. **The game says only sniper rifles
  are scopes** (section 15's decision, the assault rifle a red dot, agrees), **and they have a second magnification**.
- The HUD shows it: `BioSFHandler_Reticule.ReticuleInfo.m_fMagnificationLevel`, `RETICULE_MODE_SNIPER`.

### The game's scope picture

- `GUI_SF_HUD.Reticule` (GFx, 84 KB; clips `SniperMode` with a `zoom` property and `RANGE_IN`/`RANGE_OUT` labels, beside
  `StandardMode`, `MarksmanMode`, `AssassinationMode`, ...) and 15 textures in `Startup_INT.pcc` (always loaded). The scope's:
  `Reticule_I41` (2048 square: the tube's dark rings around a clear disc), `I43`/`I44` (512x2048: the frame's sides for wide
  screens), `I4C` (2048 square: a soft lens mask with scratches), `I4E` (1024 square: one quarter of a cyan reticle, a post, a
  stem, arcs and a bracket below), `I70` (1024x2048: a ranging ladder and scale), `I6D` (a soft white mask). The others are the
  standard reticle's arcs and bars.
- How the pieces are placed, mirrored and animated with the zoom is in the movie's timeline: read below (the layout).

### The layout (read from the movie, 2026-11-03)

`tools/scope-art.py` reads the GFx (an uncompressed SWF with Scaleform's tags; images are `DefineExternalImage2`, tag 1009:
character id, format, the image's logical size, its file name) and walks the exported `SniperMode` clip's display list to its
last frame (the zoom-in animation settled; the zoom and range text `distanceMC` and the frame's widescreen sides `wideDetail`
left out): 11 pieces in a 1280x720 stage. At depth 1 the `needle` clip: the lens mask (`I4C`, 0.359 movie pixels a texel,
2048 texels: 735 pixels across) and nine pieces cut from the atlas `I4E` (0.203 pixels a texel; the stems, the side arcs and the
top arcs each placed twice, one mirrored about x = 0); at depth 12 the `scopeFrame` clip, the rings (`I41`, 0.333 a texel). The
mask's clear disc (alpha under 0.5) is centred at (6.85, 6.62) movie pixels, radius 223 (the median over 24 directions: specks
cut a few short); the reticle's centre (the post's column, the stems' brightest row) at (0.86, 1.31). Composed offline from the
exported textures, it is the game's zoomed sniper view (the picture stays local: game data).

### Built (2026-11-03, the same day)

1. **The per-model optic table** (`vr::sights::OpticFor`, `HideList`; mod `game/sights`): the held mesh's name
   (`SkeletalMeshComponent.SkeletalMesh`'s object name) picks the measured optic; it applies when the sight's bone is the one it
   was measured on. The eyepiece (or, for a dot, the housing's rear opening) is placed that far from the live bone along the gun's
   frame (`back`, `up`), the lens radius is the model's (`<type>Lens = auto`, the new default; a number overrides), the hidden parts
   the model's (`<type>Hide = auto`: SNPa `Scope_Lens`, SNPb nothing; a sniper we don't know `Scope_Lens`). A mesh we don't know
   keeps the generic placement (`HousingToEyepiece`). Log `sights: the <gun> model is <mesh>: the game's scope's eyepiece sight:
   ...`; snapshot `mod.sights.game.mesh`, `optic`.
2. **The game's magnifications** (`vr::sights::ZoomFromFov`, `AddAimMode`, `ZoomAt`, `ZoomFlick`; mod): `sniperRifle = scope
   auto` (the new default; `scope N` overrides) reads the held weapon's `Stats.AimModes` once per weapon (rows in
   `engine/ReflectCheck`): the scoped modes' zooms, 3.5x then 7.0x (the four makers 3.3x, 6.6x). Under ADS, a flick of the right
   stick forward steps to the next, back to the one before (`[Sights] zoomStick = 1`; a 30 ms detent in the right hand; the stick's
   Y does nothing else on foot: the view level holds the pitch). The game's own `CurrentAimMode` isn't touched. Unreadable: 4x for
   a sniper, 1.5x else. Log `sights: the <gun>'s own aim modes (...)`, `... scope zooms in to 7.0x ...`; snapshot `zooms`,
   `zoomLevel`.
3. **The game's scope view on our picture** (`vr::scope_art`; mod `game/sights`, host `plugin/SightLayers` and `SightPictures`;
   `[Sights] reticle = game`, the new default; duplex, mildot, dot as before). The mod finds the three `GUI_SF_HUD` Texture2Ds in
   GObjects (by class, name and package; again every 10 s while missing) and publishes each one's `Resource`, size and a
   generation with the scope, also while its picture waits for ADS (the sight the host and mod share grew: they deploy
   together). The host, once per generation, finds each D3D texture behind its resource (`scope_capture_source::
   FindTargetTexture`, research/39's guarded search), reads it UNORM and decodes the UI art's gamma (2.2; an sRGB format would
   be decoded by its view), composes two 1024-pixel float layers with mips (`kArtShader`: 4x4 samples a pixel, each piece's
   texels clipped as the movie's shapes clip them), and lets the game's textures go. The scope picture samples the reticle layer
   about the sight line through the eye (collimated, like our reticles) and the tube layer (mask and rings) about the eyepiece;
   the picture's edge is 251 movie pixels from the centre (the field fills 0.89 of it), the exit pupil's shadow as before. The
   game's lines were a pixel wide on a 720-line HUD; the field here is fewer headset pixels across, so the reticle gets 1.8x
   coverage and a soft dark halo (the reticle blurred, 0.45; `kArtBoost`, `kArtHalo`). Not composed (no texture, another size or
   format): the duplex, logged once. Logs `sights: the game's scope art found`, `sights: the game's scope view composed: ...`,
   `... isn't used: <why>`; snapshots `mod.sights.game.art`, the host's `sights.gameArt`.
4. **Settings migration** (marker `zoomStick`): exactly the old defaults become the new ones once (`sniperRifle scope 4 -> scope
   auto`, `reticle duplex -> game`, `assaultRifleLens 1.6`, `sniperRifleLens 2`, `sniperRifleHide Scope_Lens -> auto`); anything
   else is the player's and kept. The menu's sniper chip is "Scope" (`scope auto`).
5. **Tools**: `cockpit-package export <pcc> --filter --out` (textures to PNG, GFx movies to .gfx; local only),
   `tools/scope-art.py` (the layout above, printed as `ScopeArt.cpp`'s table), `mevr-sight-preview --art <folder>` (the composed
   layers and `scope-game-*.png` with the host's own shaders). Unit tests `TestSightGameOptics`, `TestScopeArt`.

**[verify] in the headset (notes/next-vr-session.md step 49):** the D3D texture is found behind each `Resource` (the search was
made for render targets; a `FTexture2DResource` may hold its RHI reference elsewhere); the eyepiece on the glass at 0.12 m with a
much shorter pull; the reticle readable; the zoom flick.

### In the game, on the fake headset (2026-11-04, 891ba5e + 601a938)

A save in the Mako on BIOA_UNC20 (`mevr vehicle exit`, then `mevr weapon sniper` under the replay), a synthetic ADS replay
(`tools/make-ads-recording.py out 30 90 0 0.15`: the null driver recentres with the head at the origin, so the head goes at
height 0; the aim pose 15 cm under the eye, since this rifle's bore runs about 10 cm over the controller's aim pose).
- **Works:** `sights: the sniper rifle model is WPN_SNPa_MDL: the game's scope's eyepiece sight: 13.5 units behind its 'Scope_Eye'
  bone ...`; the sight placed 72.7 units back (was 59.2); `own aim modes (SFXWeaponStats_AvengerSniperRifle ...): 22.6 degrees (a
  scope, 3.5x), 11.4 degrees (a scope, 7.0x)`; the reflection rows agree with LExSDKv2; the settings migration ran once. **ADS
  with the hand 30 cm ahead of the eye: `0.09 m wanted now, the right eye 0.21 m behind the eyepiece`** (the old placement needed
  0.35 m); the picture-up line `the eye 0.12..0.19 m behind the eyepiece (0.12 on average)` over 27,700 frames. Section 14's
  "eyepiece ahead of the hand" premise is gone in the game.
- **The game's scope view can't come from `Texture.Resource`.** The art's three `FTexture2DResource`s have no RHI texture (an
  exe vtable, a self pointer, the resource list's links, then zeros where the RHI references go; no D3D texture within 0x400
  -> 0x400 bytes), and each mip's bulk data has flags 0x8 (single use) and a null data pointer (element count 0x100000 for
  the 1024 atlas, 0x400000 for the 2048 ones): Scaleform made its own textures from the bytes and the engine freed them. The
  host logs `the game's scope view isn't used: no D3D texture ... found behind Reticule_I4E's resource` and keeps the duplex.
  Routes left: catch Scaleform's upload of those three images (a device `CreateTexture2D` / context `UpdateSubresource` hook
  matching size, BC3 and a hash of the data: hashes, not the data, in our code), or read the bulk data from Startup_INT.pcc
  ourselves (the offsets are into the decompressed package: its chunk table and the game's own decompressor). Not built.
- **A lesson paid for with a crash:** a wide probe (`QueryInterface` on every object with a vtable inside d3d11.dll within
  two 0x400-byte hops) took the game down 2 ms after a call (the engine jumped to 0 on the game thread): some of d3d11's
  internal objects aren't COM, and slot 0 of their vtable is not QueryInterface. Never call into an object found by
  scanning; compare its vtable with a known texture's first (one we create on the same device).

### The options as first written (before building)

1. **A per-model optic table** (mod only, hot reload, unit-testable): keyed by the mesh's name (the mod logs it), our numbers
   above: the eyepiece's rear-face point and axis in the sight bone's frame, the lens radius, parts to hide; an unknown mesh
   keeps today's generic path. Fixes 1-3.
2. **Magnification from the held weapon** (mod): `Stats.AimModes[CurrentAimMode]` through our reflection layer: `bScoped` picks
   scope or dot, the zoom `tan(35 deg) / tan(ZoomFOV / 2)`; the second mode on a gesture (the game's `CycleMagnification`); the
   scope capture's field follows. `sniperRifle = scope 4` would become `scope auto`, a number still overriding.
3. **The game's reticle and tube on our picture** (host and mod, a HostServices bump): the `Reticule_*` textures sampled from the
   running game's memory (the host already finds a `UTexture`'s D3D11 texture for the scope capture, research/39) and composed
   in `SightPictures`' shader as the movie composes them; nothing from the game in the repository; our duplex when they aren't
   found. Not the movie itself: its `SniperMode` follows the camera's zoom (`bIsZoomed`), which must stay off (section 4).

## 17. The assault rifle's red dot: seen through its housing, and steadier (headset session 2026-11-05, built the same evening)

**Report (user, 2026-11-05, PS VR2 on SteamVR, the players' build 1ce16cf):** holding the assault rifle up to the eye "seems to
be a little finicky, like it's not very stable. But more importantly, the asset for the scope is completely solid, so I can't
actually see through the weapon with my right eye."

### What the session shows (`LE1MEVR.log`, 21:44:55-21:45:14, the rifle `WPN_ASLa_MDL`, held two-handed)

- **The housing:** ASLa's sight is a closed box (section 16: 12.8 units long, 4.3 wide, 4.4 tall, one material, no lens). Since
  section 16 the dot's window sits on its rear face, and the dot is a quad composited over the eye with no depth, so the aiming
  eye sees the red dot on the box's back face: about 8 degrees of solid metal in the middle of the view at a 0.3 m hold.
- **The gun slid on its own:** `alignment assist on` 10 times and `off` 8 times in 18 s, on and off again within 10 ms at
  21:45:08 (`on ... 59% weight` then `off ... the centre eye 12.9 cm beside the bore`): the hold sat at the corridor's
  lateral edge (entry 9 cm, exit 13 cm), where the approach weight runs from 1 to 0, and each time the dot's 3 cm slide
  (`assistDotMaxM`, section 15) moved the gun toward the eye and back. Meanwhile the heartbeat says **the aiming eye sat on
  average 0.1 cm left of and 1.0 cm above the unassisted sight line**: the player's own hold was already on the line.
- **The dot blinked:** `the right eye through the red dot sight` / `no eye through the sight` 9 and 7 times; once (21:44:58.53,
  the moment the support grip's aimed fire turned the game's LT on and the camera went Combat, Interpolate, TightAim) one
  tick's sample put the gun 0.58 m from the eye and the dot went to the LEFT eye for 70 ms. Which part of that tick moved (the
  drawn gun or only our reading of it) isn't settled **[verify]** on the fake headset (aimed fire on and off under a replay).
- Not the cause: the calibration (rifle step done at 21:20, spread 4.0 degrees), the constant eye (`SFXCameraMode_TightAim
  ... the eye (50.1, 0.0, 94.9) from the hook bone, as exploring`).

### Built (host `plugin/SightLayers`, `SightPictures`; mod `game/Sights`; `core/vr/Sights`, unit-tested `TestSightWindow`)

1. **The window** (`[Sights] dotWindow = 1`; flag `vr::sights::kWindow`, a new bit in the shared struct's `flags`: no size
   change, an older host ignores it): the mod sets it for a dot or holo on a model whose optic table entry applies (ASLa,
   ASLb: the housing's rear opening, auto placement). The host then draws, for the eye the dot shows to and behind its ring and
   dot, a disc of the window's radius (3 units: it covers ASLa's 4.3-unit face to its corners) showing **the other eye's image
   at 1x**: the scope picture's shader and eye copy (section 4) with `sv::WindowView` (each window point shows its true
   direction from the eye, wherever the eye is behind it; no exit pupil, no shadow) and a window style (no reticle, no tube rim;
   the outer 15% of the radius fades into the housing). The other eye still sees the housing: both eyes open, like a real red
   dot. Parallax: the source is 6.3 cm to the side, so a thing at distance D shows up to 6.3 x (1 - D / 40 m) cm off in the
   window (0.6 degrees at 5 m, 0.3 at 10 m; at the 40 m focus none); the dot itself is drawn true. The scope capture
   (research/39) isn't used for it: its grade (or, without post, its lack of one) would show as a disc on the most-used gun.
   Logs `[MEVR-SIGHT] first window through the assault rifle's sight housing ...`, `sights: the dot's window was up N frames
   ...`; snapshot `sights.window` (`flag`, `drawn`, `frames`, `bridged`). Offline: `mevr-sight-preview`'s
   `eye-right-dot-housing.png` (before), `eye-right-dot-window.png`, `eye-right-dot-window-off-1.5cm.png`, `window.png`.
2. **Dots don't slide:** `assistDotMaxM` default 0 (was 0.03); a file without the new key `dotWindow` that still holds exactly
   `0.03` gets 0 once (`hands: [Sights] assistDotMaxM 0.03 -> 0`); any other value is kept. A sight the assist can't move no
   longer logs `alignment assist on/off` (its statistics stay in the heartbeat). Scopes keep their slide and pull.
3. **The dot's eye through a dropout** (`vr::sights::EyeHold`, 120 ms): a tick with no eye through the window keeps the last
   eye (never the other eye) until 120 ms have passed, so a one-tick jump or the eye a moment past the box's edge doesn't
   blink the dot and its window; the other eye still takes over at once when it's chosen.

### In the game, on the fake headset (2026-11-05 22:24, f2a9f47)

BIOA_UNC20 at (-10610, 13868, -28450) facing the Mako 10 m off, the assault rifle (`WPN_ASLa_MDL`) drawn, a synthetic ADS replay
(`tools/make-ads-recording.py out 30 90 0 0.133`: the aim pose 13.3 cm under the eye puts the sight line at the eyes' height;
0.20 left it 6.7 cm under them, outside the eye box). The snapshot's `sights.window`: `flag true, drawn true`, 184 frames in a
row; log `[MEVR-SIGHT] first window through the assault rifle's sight housing: the left eye (0.217 m behind it, 0.0038 m off the
line) sees the right eye's image at 1x on a 0.060 m disc`. In the replay the left eye sat nearer the line (2.2 cm against 4.7),
so it was the left eye's window from the right eye's image: the same code either way. **Seen:** the world layer alone shows the
model's sight as a tube end with a black inside covering the Mako's rear; the composited eye shows the Mako, the hills and the
dot through the window, the ring around it, no visible seam at 10 m. The window's lower edge reaches about a unit past the tube
onto the receiver's top (the window radius 3 units against the tube's ~2.9). Not reproduced: the tick aimed fire switches. On
the fake headset the game sees the controllers' pad only while its window has focus, so the support grip's LT never reached it
(the camera stayed `SFXCameraMode_Combat`).

**Also learned:** with another process training on the GPU (24 GB used, 100%) the game ran at 2.5 fps and the host's view went
stale before the mod read it, so the mod reported `no head-tracked world view` everywhere and published no sight. A test of
anything VR-side needs the GPU to itself.

**[verify] in the headset (notes/headset-checklist.md section 3):** the rifle's sight seen through with the right eye, the
target and the dot in the window, the housing around it, no seam that bothers; the gun stays put in the hand at the face; the
dot doesn't blink, also when the support grip's aimed fire comes on (with focus, the game's TightAim camera).

## 18. No sight at all: the runtime's swapchain limit (headset session 2026-11-06, fixed the same morning)

**Report (user, 2026-11-06):** "I wasn't able to actually see any of the scopes ... I saw that the model would disappear, so
it made it easier to just view the heads-up display, but I never saw the scopes render in my game." (The development build
3b5bc93, f2a9f47 included; not the 0.1.0 release the user thought it was.)

**The log (`LE1MEVR.log`, 09:42-10:00, PS VR2):** `sights: sight window ring swapchain 256x256 failed (XrResult -10)` at 09:55:26,
`sight reticle swapchain 256x256 failed (XrResult -10)` at 09:55:28 and `scope picture swapchain 768x768 failed (XrResult -10)` at
09:57:13: XR_ERROR_LIMIT_REACHED. Each latched `failed` for the session, so no dot on any gun, no ring, no rifle window and no
scope picture all session, although the mod published the sights, ADS came on and the scope capture rendered (`scope capture
source: first scope picture from the capture`: its texture was found; there was nothing to draw it into). The calibration's
board, the galaxy hologram, the omni-tool rail, the squad arc, the gun status and the pointer dot failed the same way.

**The limit, measured** (`tools/swapchain-probe`, `mevr-swapchain-probe`, c0f232f: a session of its own, swapchains until the
runtime refuses): SteamVR/OpenXR 2.18.4 with the PS VR2 refuses the 17th, at 64x64, 256x256, 1024x1024 and 2048x2048 alike (a
count, not memory); a destroyed one is given back at once; `maxLayerCount` is 16 too. Halcyon holds 4, so 12 are ours, and
every overlay made its own and kept it for the session.

**The fix:** the host-wide part is the Biotic session's `plugin/Swapchains` (ac68488): every owner creates through it, a
refused create frees the least recently shown of ours idle for a second (never one in this or the last frame's layers), and
nobody gives up for the session (`RetryDue`). Here: **the sight takes one swapchain instead of three.** One image (P + 256) x
max(P, 512), P the sight's resolution (768): the scope's or the window's picture at the origin (drawn into it each frame it
shows, viewport P x P), the dot or holo reticle and the window's ring in a column to its right (256 squares, copied from one
immutable texture into each image once per look: `gFilled` per image, refilled after a free); each quad takes its part by
`imageRect`. The ring and the dot used to be static swapchains filled once; now any frame that shows the sight acquires the
one image (a 0.5 MB copy the first time per image, nothing after). Log `sights: one swapchain WxH for the picture (P px), the
reticle and the window's ring`; snapshots name the layers `sight`, `sight-2`, `sight-3`.

**"The model would disappear":** not settled by this log. Likely the sniper rifle pulled toward the eye under ADS (`pulled at
most 7.8 cm` at 09:58:00): with its picture missing, what's left of the tube near the camera's near plane is cut away. [verify]
once the picture draws again.

## 19. Simpler: the scope at the shoulder and held out, the dot past its window, the crosshair (headset session 2026-11-06 21:12, fixed the same evening)

**Report (user, 2026-11-06, PS VR2, the players' build 5479d30):** "if I hold it too far into my shoulder, I no longer see the
red dot. But if I hold it too far away, I can still get these points where I actually see the model of the rifle. So I get a
white light that blinds my eye ... we may need to just get something a bit more naive when it's on shoulder or when it's
equipped and it's close to me, that it just tries to get to the synthetic scope faster." Also: "sometimes when equipping guns
I'd get the old HUD for the crosshair for the weapons like the assault rifle".

### What the session shows (`LE1MEVR.log` 21:21-21:22, the sniper rifle `WPN_SNPa_MDL`, the assault rifle `WPN_ASLa_MDL`)

- **Held out:** `ADS off (the eye 0.62 m from the sight (past 0.50) ...)` while the gun was still aimed: past adsReachM + 0.10
  the picture went, and 0.15 s later `Scope_Lens` (the glow) was back in front of the eye.
- **Into the shoulder:** while ADS was on the picture dropped out (`no eye through it` 21:21:44.965, back 190 ms later); the
  heartbeat before says the eye was `0.03..0.26 m behind the eyepiece`: the hold brought the eye up to and past the eyepiece
  the pull had put at 0.12 m, where no picture can be (the host needs the eye behind it).
- **The crosshair:** Halcyon's log has no `[MEVR-CROSSHAIR]` line in either of the day's sessions: the hide never ran. It acts
  on the crosshair handler's own ProcessEvents only, and that handler gets them now and then (a draw, a mode change), not
  while the player aims, so a hide asked for between them waited for the next one.

### Built (mod `game/Sights`, `game/ScopeParts`; `core/vr/Sights`, unit-tested; host `plugin/SightLayers`, `[MEVR]` in pchud.cpp)

1. **ADS sooner and kept further:** `[Sights] adsReachM` 0.40 -> 0.55 (a file without the marker `scopeAdsPushM` that holds
   exactly 0.40 moves once), off only past `adsReachExitM` (0.80; at least adsReachM + 0.05). Overlord's corridor (facing,
   beside and above the bore) still decides; the host's scope range follows (exit + 0.10).
2. **The push** (`scopeAdsPushM` 0.15, `vr::sights::Pull::pushM`): under ADS an eye nearer the eyepiece than the relief pushes
   the gun away along its line, up to 0.15 m, so the eyepiece stays 0.12 m ahead of it; an eye up to the push PAST the
   eyepiece still counts (`AssistTarget`'s floor). Same spring as the pull. `goal.pull` below 0 is a push.
3. **The lens hides whenever the gun is raised** (scope_parts' `up` = the picture up, or ADS, or `RaisedWeight` over 0.20 until
   it falls under 0.08), not only while the picture is up, so the glow never reaches the eye while aiming.
4. **The red dot past its window:** the host picks the dot's eye with along down to -0.08 m (`kDotMinAlong`): the dot (50 m out)
   shows; the window's picture still needs the eye behind it. `AimingEye`'s hysteresis now widens a negative floor too.
5. **The crosshair** (`[MEVR]` in `MevrApplyCrosshair`): the handler last seen is kept, and when the wanted state changes (or
   once a second for the re-stamp) it's used from the next ProcessEvent of any object on the game thread
   (`MevrBridge::OnGameThread`), after its class is checked again (SEH-guarded); the panel is still read live from it.

**[verify] in the headset (notes/headset-checklist.md section 3):** the sniper's picture comes as the gun comes up, stays at the
shoulder and held out, no glow; the gun moving off the shoulder isn't disturbing; the rifle's dot stays at the cheek; no game
crosshair after a draw (Halcyon's log: `[MEVR-CROSSHAIR] game crosshair hidden`).

## 20. At the calibrated shoulder: the sight on at once (2026-11-06, the same evening)

**Asked (user):** whether the two-handed calibration decides when the sight comes on. It didn't: the rifle stance (research/25)
posed the gun and its foregrip, and its stock point only set the support grip's "at the shoulder" zone (the game's aimed fire);
ADS was the eye's distance to the sight (section 19). The user: "please proceed" with using it.

**Why it's the right signal:** the calibration solved the rifle's gun frame from the eye's line to three targets with the right
hand at that stock point, so with the hand back there and the head looking along the barrel the eye is on the sight line by
construction, whatever the eye's distance to the sight: the case the distance rule kept losing at the cheek.

**Built** (`vr::ads::AtShoulder`, unit-tested in `TestAds`; mod `game/Sights`; host `plugin/SightLayers`; `[Sights] shoulderAds`,
1 by default): a rifle or shotgun (never the pistol), the right grip in `body_calibration::ShoulderZone()` (the zone 0.12 m around
the calibrated stock point, `ik::Shouldered` with its own hysteresis), and the head within 25 degrees of the barrel (35 to stay):
- a scope's ADS comes on at once (the dwell still 100 ms) and holds while it lasts, whatever the reach;
- the raised weight is 1 (the assist, the pull or push, the lens hidden, the gun's HUD panel off the gun);
- the mod sets `kShouldered` (16, a flags bit: no size change) and the host takes the dot's eye in a 0.10 m box instead of 0.06.
Without a calibration for the controllers in use (or `shoulderAds = 0`) the distance rule alone, as in section 19. Logs `sights: at
the calibrated shoulder (...)` / `away from ...`; snapshot `sights.ads.atShoulder`, `inShoulderZone`, `shoulderOns`.

**[verify] in the headset:** shouldered, the scope and the dot are there at once and stay while you shift the cheek; lowered
from the shoulder they go; a recalibration after a stance change.

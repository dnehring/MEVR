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
  toward the eye for sniper scopes (5 cm holographic), keeping 8 cm clearance. Not done here (section 7).

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
`assistMaxM`, `assistRangeM`.

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
- **Not done:** Overlord's ADS eye-distance assistance (sliding the gun toward the eye when aligned:
  useful if the eyepiece can't get close enough with Shepard's proportions); option 3; thermal; the
  zoomed recoil; a dot through a scope's picture. (The sideways alignment assist is built: section 8.)

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
   - **Applied to the whole gun:** the right wrist's goal after reach scaling, and the left's by the support
     grip's blend (a hand on the gun moves with it, so the two-hand line doesn't turn; a free hand stays).
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

<!-- SPDX-License-Identifier: MIT -->
# Weapon sights and optics for ME2 and ME3 VR

2026-11-14. P3 design and offline source inspection. First prototype the
Freedom's Progress pistol and rifle with the existing inexpensive sight
renderer. Then prove a magnified sniper optic on the same aiming path.
Give individual weapons their identity through measured geometry and profile
data. Share the renderer, input behavior and calibration between weapons.

## What ME1 already gives us

- [Sights and ADS](21-sights-and-scopes.md) already have a gun-relative frame,
  collimated dot and holo, per-eye aperture checks, eye selection and short
  dropout tolerance, a virtual eyepiece, scope shadow, zoom steps and ADS
  hysteresis. The current settings start from four ME1 weapon families.
  They already support model-specific placement where the game has a sight
  bone. Keep that foundation and move the default choice to the held model,
  with family settings and user calibration as fallbacks.
- [Scope capture](39-scope-capture.md) already renders a separate narrow view
  into a texture on demand. In the measured light LE1 scene it cost roughly
  0.5 ms more render-thread work and produced a sharper scope picture than an
  eye crop. That measurement belongs to LE1 at that location. Dense scenes,
  LE2/LE3 render cadence and physical headset behavior need their own checks.
  The old eye copy also showed range-dependent parallax when using the other
  eye and can magnify an opaque sight housing when using the aiming eye.
- [Physical headset findings](21-sights-and-scopes.md#17-the-assault-rifles-red-dot-seen-through-its-housing-and-steadier-headset-session-2026-11-05-built-the-same-evening)
  removed automatic gun pull for dots after small alignment changes caused
  visible sliding. Keep a generous dot eye box and the tracked gun steady in
  the hand. LE1 also consolidated its layers into one swapchain after reaching
  a SteamVR swapchain limit. Reuse its atlas and layer budget.
- [LE2's VRIK run](le2-vrik.md) establishes tracked weapon placement and
  muzzle observations for the pilot case. The lit feature visible on the
  pistol belongs to its model. The run logs show that the then-tested host
  did not expose the MEVR sight service. At source checkpoint `a6635b9f`,
  [LE2's overlay composition](../src/plugin/HalcyonLe2Bridge.cpp) still
  publishes the vignette and leaves sights to a later module port. The
  game-side `Sights`, `ScopeParts` and `ScopeCapture` paths still have LE1
  guards. Wiring both sides is the first engineering task.

## What the sequel packages establish

| Evidence | Implication for the port |
| --- | --- |
| LE2 `SFXWeapon.SetZoomed` writes weapon zoom state and emits the native Zoom hint event when entering. `GetZoomFOV(BaseFOV)` returns a positive `ZoomFOV` override or the supplied base FOV. The base sniper class extends the zoom transition with authored enter and exit sounds. `BioPlayerInput.PostProcessInput` gates entering and leaving by gameplay context. | Measure the normal ADS transition and retain its gameplay consequences. Suppress the flat camera zoom and full-screen scope presentation only while our replacement view is active. Avoid forcing a zoom flag every tick or skipping the native transition. Validate actual accuracy and spread separately from visual alignment. |
| LE3 `SFXWeapon.GetZoomFOV()` reads the current entry of `AimModes`. `SetZoomed` resets the aim-mode index and processes native zoom state, weapon-mod scope audio and cover-dependent audio. The aim-mode struct includes a scope resource and a scoped flag. The installed assault-rifle mod package contains a dedicated rifle zoom reticle export. | Include current aim mode and installed optic attachment in profile resolution. Sample after the native transition so the first mode and subsequent zoom steps agree. An attachment or aim-mode change invalidates the previous aperture and scope image. Keep native mod progression, effects and sounds. |
| LE2 bulk pistol, assault-rifle and sniper packages contain separate weapon classes and meshes. LE3's Javelin package contains standard and Geth zoom-reticle classes as well as its own model. | Per-model geometry and selected scope art can distinguish weapons even when their renderer is shared. A package export only establishes that the asset exists. Identify which class, texture and variant are mounted and live before relying on them. |

Inspected installed base `SFXGame.pcc` with LegendaryExplorerCore. LE2 SHA-256
`681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77`.
LE3 SHA-256
`1d8f7acb6f28e78030982c9f2d935cddec16877272aba314ce1c21dff5ed2d18`.
Also inspected LE2 `SFXWeapon_SniperRifles.pcc` and LE3
`SFXWeaponMods_AssaultRifles.pcc` and `SFXWeapon_SniperRifle_Javelin.pcc`.
The private scan did not recover a complete per-weapon default or effective
magnification table. The profiles below are design targets and need live
weapon, mesh and aim-mode measurements. Raw outputs are kept privately under
`local-tools/le2-le3-optics-survey`.

## Recommendations from other VR implementations

- **Overlord** gives us the closest technical precedent. Its profiles use the
  actual sight surfaces and separate the lens geometry, reticle and scene-image
  provider. Its same-eye magnifier is the low-cost option. A separate native
  render adds real detail for higher-power glass. Its source also explains why
  a slower scope image update shows up as the gun moves. Reuse the existing
  MEVR geometry first and use Overlord to review aperture and image choices.
  [Pinned Overlord optic design](https://github.com/Aeka0/Overlord/blob/2d9b0ea14afa6079d69e93ce0a6270fb2bb6a597/docs/vr-optic-rendering-feasibility.md)
- **TinyBlackDog's Titanfall 2 VR** documents separate motion-controller aim,
  an optional cap for ordinary ADS zoom and a threshold that preserves stronger
  scope zoom. Its controls also document an optional camera tilt toward the
  gun. The Mass Effect design keeps the world projection stable and uses the
  cap and threshold only as examples of treating short and long optics
  differently. [Titanfall 2 VR controls](https://github.com/kyus2001/titanfall2vr/blob/main/CONTROLS.md#view)
- **Into the Radius** is a useful feel reference. Its developers shipped
  optic-specific zoom and adjustments to make scopes readable at the game's
  engagement ranges, then improved scope parallax. Test a similarly deliberate
  balance between acquisition speed, image stability and weapon identity in
  Mass Effect's indoor and outdoor spaces. [Developer scope tuning notes](https://store.steampowered.com/news/posts/?appids=1012790&enddate=1615399013&feed=steam_community_announcements), [parallax update](https://store.steampowered.com/news/posts/?appids=1012790&enddate=1620742041&feed=steam_community_announcements)

## One system with a profile for each weapon

Resolve the live game and weapon class, mesh identity and equipped optic or
aim mode. Use a reviewed exact-model profile first, then a compatible family
default. An unknown or unverified mesh keeps the optional gameplay ring until
its sight can be measured. Reuse the existing `[Sights]` user settings and
dominant-eye and hand calibration. Keep art, optic geometry and native action
state independent so a cosmetic variation cannot change combat balance.

Each profile carries the sight type, front/rear anchors or lens plane and
aperture, optical axis in the gun frame, reticle art and angular size, eye
relief and eye-box tolerances, supported zoom steps, and any presentation
exceptions such as an opaque lens or charge indicator. Prefer a verified
socket or mesh surface, then an authored gun-local transform measured from
the actual model. Record the mesh version with each calibration. Use the
same final weapon pose and tracking generation for the scope, the visible
gun, and comparison with the game's shot trace. Preserve the game's spread,
projectile travel, intended native assist, charge and obstruction behavior.
The reticle represents the sight line. Show an optional impact cue separately
if the user wants a gameplay aim aid.

| Initial candidate | Proposed experience to test | What makes it distinct |
| --- | --- | --- |
| LE2 Predator (`SFXWeapon_HeavyPistol`) | Readable model sights, or a small projected point aligned to the existing sight geometry if the model needs help | Compact presentation and quick one-handed acquisition. Use Save 4 for the first geometry and shot-alignment proof. |
| LE2 Avenger (`SFXWeapon_AssaultRifle`) | A wide 1× reflex or holo window, matched to the model after inspection | Fast two-handed acquisition and a broad eye box. Compare the actual housing silhouette and visibility during sustained fire. |
| LE2 Katana | A front bead or short open sight, with the existing spread cue as an optional gameplay aid | Close-range acquisition and a low visual footprint. Keep the weapon's own spread and distinguish it from a precision dot. |
| LE2 Mantis (`SFXWeapon_SniperRifle`) | A live magnified lens with a quiet duplex and clear scope shadow | Distinct precision view and eye relief. Start with the native FOV reference and tune perceived magnification in a headset. |
| LE2 Viper | A more open marksman sight picture with a quicker re-acquisition profile, subject to its model and native zoom | Emphasize follow-up shots. Choose lens size, reticle and zoom feel from live measurements alongside the Mantis. |
| LE3 scoped rifle mod and Javelin | Swap the same rifle between base sights and its equipped optic. Give the Javelin its verified Geth styling and native special visibility behavior if the render path supports it. | The rifle changes according to the attachment. The Javelin's distinctive view can retain only information the game's own scope is authorized to show. |

**Iron sights and 1× optics.** Use the visible front/rear geometry whenever it
reads clearly. On reflex or holo glass, render a collimated reticle that stays
on the aim line as the viewer moves within the aperture. Clip the mark to the
actual visible window for each eye. Start with the existing eye-gated MEVR
compositor path. It is cheap, but a compositor layer alone has no scene-depth
contract for occluding it behind nearby cover or the gun model. Test those
cases explicitly. Use a depth-tested in-scene aperture if the overlay cannot
pass them. A solid mesh can use the existing 1× eye-copy patch for a bounded
prototype. Prefer an existing transparent lens or a narrowly scoped lens
material change if the patch visibly shifts or leaks through the housing.
[Khronos quad and per-eye layer specification](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerQuad.html)

**Magnified scopes.** Keep the outside world at headset FOV and put zoom in the
aiming eye's lens. The other eye retains its ordinary view. Apply eye relief,
soft scope shadow and stable eye selection, with a manual left/right eye
override. Center the lens image and reticle using the same pose and projection
generation. Source real magnification from a small demand-driven capture after
the LE2 renderer exposes a usable capture target and lifetime. Convert native
FOVs to starting magnification by comparing the tangents of their half angles
on the same axis. Measure how that looks through the lens before setting
per-weapon defaults. Preserve authored zoom steps where available. An eye crop
is an interim low-power fallback. If it is occluded, stale or too blurred,
fall back to the calibrated unzoomed sight or aim aid and log the reason.

## Proof order

1. **One ME2 pistol and one rifle at 1×.** Inspect current class and mesh,
   mount anchors, and actual sight aperture from both eyes. Wire the shared
   sight service and present using LE2's current renderer adapter. Check the
   native fire path and impact against the optic at several in-game ranges,
   with and without ADS, both hands, and after raising or lowering the gun.
   Walk up to cover, change stance, swap weapons and reload. Require timely
   fallback and no reticle showing through occluders or on the wrong eye.
2. **One LE2 sniper.** Compare an eye crop with the native scope capture in
   the same short and long engagement scenes. Check zoom readability, eye
   relief, both eye images, near muzzle obstruction, color, scope shadow,
   head movement and tracking dropouts. Measure unscoped versus active lens
   frame time, p90/p99 and capture cadence in light and dense scenes. Require
   no extra scene render for irons or 1× dots. Hide the scope picture as soon
   as the gun lowers, tracking is lost or a menu or traversal takes control.
   Disable capture after any bounded warm hold and discard stale textures on
   weapon, level or renderer changes. Record actual impact and native scoped
   state separately from lens alignment.
3. **Expand from calibrated profiles.** Add one contrasting optic in each
   family before the larger inventory sweep. In LE3 check one attachment
   install/removal and aim-mode transition, then a special optic. Use the
   existing [LE2 QA fixture flow](../tools/qa-le2/README.md),
   [Halcyon replay and snapshots](../tools/le2-halcyon-run/README.md) and
   [sight preview](../tools/sight-preview/) to collect comparable evidence.
   Physical headset review should cover first acquisition, comfort for either
   dominant eye, seated reach, repeated scoped fire and readability in dark
   scenes. The package/source audit does not exercise the runtime feature.

## LE2 implementation and first headset run

The LE2 Halcyon host submits the shared MEVR sight layers. They use the held
weapon's visible model and follow a recognized sight bone when available.
Without a usable anchor, they estimate placement from the model and the
weapon-family defaults. Pistols, shotguns and assault rifles use the default
1× dot. Snipers use the magnified lens with the shared duplex reticle and the
weapon's native `ZoomFOV` as the starting magnification when readable. The
other eye's game image supplies the lens by default. Both the eye selection
and the aperture test use tracked eye positions.

This pass leaves the game's shot path and accuracy alone. The exact lens
centers, aperture sizes and individual reticle profiles still need live
measurement. An opaque lens, close cover or a distant target can expose
limitations of the compositor and magnified eye copy. A dedicated zoomed
scene capture remains a follow-up.

1. Start with the Predator and Avenger in the Freedom's Progress Save 4
   fixture. Use the existing snapshots and mod status to record the mesh,
   inferred sight position and visible eye. Check the dot against the visible
   sights during a weapon swap, when aiming beside cover and with the gun lowered.
2. Equip a Mantis using a disposable fixture. In the logs, confirm the held
   mesh, placement source and `ZoomFOV` or fallback. Inspect both eyes for the
   scope picture, the model's own lens, and transitions when moving off-axis
   or opening a menu. The snapshot's `sights` block exposes the aiming eye,
   picture visibility and source.
3. Compare the Mantis with a Viper in the same scene. Check the native zoom
   values and what is actually readable through both lenses before authoring
   their distinct profiles. Use a physical headset for eye relief and comfort.

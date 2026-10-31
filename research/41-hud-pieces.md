# 41. HUD pieces on the body: the status on the wrist, the weapon panel on the gun

2026-10-31. Code: `src/core/vr/HudPieces.{hpp,cpp}` (the bands, the placement, the settings; unit-tested in
`tests/Main.cpp` `TestHudPieces`), `src/game/HudPieces.{hpp,cpp}` (the mod: the gun anchor; HostServices v22
`setHudPieces`), `src/plugin/HalcyonOverlays.cpp` (the host: `[Hud]` read, the two quads, the HUD panel's masks). The
template was the interaction prompt at the object ([research/40](40-interaction.md) section 2, `core/vr/InteractLabel`).

## 1. What a VR player wants from the HUD

On the body-locked HUD panel (1.30 m wide at 1.5 m) the player's health is a glance down and to the front, and the
gun's heat is in the bottom left corner of that panel, nowhere near the gun. In VR the natural places are the body:
the health and shields on the back of the left wrist (a look at the watch), and the gun's heat on the gun itself, above
the receiver where a rifle's status display would sit. The game draws them once, on its HUD movie; we cut each piece out
of the HUD panel texture (the same way the interaction prompt and the game's wheel are cut out) and show it on a small
quad of its own, masking its band out of the panel so it shows once.

## 2. Where the HUD's pieces are (measured)

Scanned programmatically on all 591 `*-hud.png` snapshots in `%LOCALAPPDATA%\MEVR\snapshots` (Python, Pillow and numpy:
the alpha channel's opaque clusters on a 16-px stage grid, then 4-px fine maps and averaged crops per cluster, and the
robust bounds (0.5 and 99.5 percentiles of alpha > 40) per frame size). Four frame sizes were in the set: 1059x1080
(527 files), 1172x1196 (56), 1412x1440 (2), 1536x1594 (6).

**The frame is taller than the stage.** The HUD panel texture is the game's near-square backbuffer. The Scaleform HUD's
1280-wide stage is shown across the frame's width, aligned to the top (`core/vr/WheelCrop`'s "show all, top left"), so a
2344x2392 frame is 1280 x 1306 stage pixels, and the HUD's bottom row of widgets is anchored to the **frame's bottom
edge**, not to the stage's 720: the same offset from the bottom, in stage pixels, on every frame size (the 1536x1594
frame is 1328 stage pixels tall and its bottom widgets sit 22 px lower in stage y, the same distance from the bottom).
Stage pixel = frame width / 1280.

| Piece | What it is | Stage x | From the frame's bottom (stage px) | Where it goes |
|---|---|---|---|---|
| Power bar | the 8 mapped-power slots, top left | 24..400 | top: y 0..68 | stays on the panel |
| Target status | the prompt's name and action, top centre (research/40) | 592..680 text (384..896 with the enemy arc) | top: y 44..92 | the object (research/40) |
| Crosshair | the brackets at the frame's true centre, (640, 653) on the 1306-tall frame | | | hidden by the hand reticle |
| **Weapon panel** | the gun's silhouette, the heat strip under it, the grenade count and icon at its top left, a bracket on the left and bottom | 69..200 (the bracket to 64..220) | 133..40 (the bracket to 12) | **the gun** |
| **Player status** | the health (red) and shield (blue) arc, the two squadmates' portraits and bars under it, the medi-gel count in the middle | 459..817 (the bars to 444..832) | 152..34 | **the left wrist** |
| Radar | the circle, bottom right | 1100..1212 | 161..40 | stays on the panel |

Correlations with the snapshots' JSON: the weapon panel and the status arc are present in the 507 files taken with a
weapon type known (drawn or holstered: 438 assault rifle, 50 pistol, 10 sniper, 9 none), absent in the title's and the
Mako's frames; the radar in all 591; the power bar only with the assault rifle (combat HUD up). A 54-file cluster at
stage (630..734, 878..958), only with no weapon, is the Mako's HUD. The target status appears in 308 files.

**The heat gauge is not near the crosshair.** ME1 LE draws the heat strip inside the weapon panel (the dotted bar under
the gun's silhouette, which fills as the gun heats); nothing of the weapon's state is drawn at the screen centre, so the
whole weapon panel can go on the gun without touching the crosshair's area. The weapon's name is not on the HUD during
play (it shows in the weapon wheel), so there is no name to move: the panel carries the gun's picture instead.

The default bands (`core/vr/HudPieces` `DefaultWristCrop`, `DefaultGunCrop`), with a margin for the glow and the
bracket: the status x 440..840, the bottom 165..25 (400 x 140 stage px, aspect 2.86); the weapon panel x 56..224, the
bottom 145..10 (168 x 135, aspect 1.24). On the 2344x2392 frame that is (806, 2090) 733 x 257 and (103, 2127) 308 x
247 pixels. `[Hud] wristCrop` / `gunCrop = x y width height` (fractions of the frame) override them.

## 3. The wrist (host only)

The host knows the left controller's grip pose (`core/vr/Controllers`, the same app space as the eyes). The quad's
centre is `[Hud] wristOffset` (meters) from the grip in the grip's own axes: OpenXR's grip +X points out of the palm
(the back of the left hand is -X), -Z along the handle toward the thumb, +Y the right-handed third axis (toward the
elbow for a handle held upright). The default `-0.03 0.06 0.05` is 3 cm over the back of the hand, 6 cm toward the elbow
and 5 cm down the handle: about where a watch sits with a Touch controller's handle angled in the fist. The quad faces
the eyes and stands upright (`hud_pieces::Place`: a billboard, so the picture is readable from any angle, like a
hologram projected off the wrist; the orientation of a flat display stuck to the skin would depend on the wrist's twist,
which the grip pose only partly gives). `wristWidthM` (0.08) is its width; its height follows the band's aspect
(32 mm).

Shown on foot (game mode 0) in gameplay with the HUD on the panel, the left controller tracked and no wheel up, the
same gate as the interaction label; the band is masked out of the panel meanwhile. The left controller lost: the piece
goes back to the panel (logged).

## 4. The gun (mod and host, HostServices v22)

The mod knows the gun: hand aim's ray (the muzzle and the barrel in tracking space) and the gun's top as the sights use
it (`game/sights GunTop`: the right controller's up as arm IK rolls the gun, or the calibrated gun frame's). Each tick
`game/HudPieces` publishes `vr::hud_pieces::Pieces` (size first, as `Label`): whether a gun is drawn on foot and aimed
along its barrel, the anchor `gunBackM` (0.15 m) behind the muzzle along the barrel and `gunUpM` (0.04 m) above the bore
toward the top, the top itself, and a serial that changes with the held weapon. The host shows the weapon panel there,
`gunWidthM` (0.06) wide, facing the eyes and leaning with the gun's top (so it rolls with the gun; a top along the line
of sight falls back to the world's up). It sits behind the sights (a red dot's window is about 10 units behind the
muzzle; a scope's eyepiece further back on the sniper rifle, which may cover the panel: `gunBackM` moves it).

The gun piece needs both sides: an older host logs `hud pieces: the host (services older than v22) ...` once and the
panel stays on the HUD; a mod of another `Pieces` size is ignored by the host with a warning.

## 5. The HUD panel's masks

The panel blit's shader masked one rectangle (the wheel, or the interaction prompt). It now takes three (`int4
masks[3]`; `PanelParams` 96 bytes): a wheel or the prompt, the wrist band, the gun band. The pieces are never shown with
a wheel up (the wheel is game mode 2 or 3; the pieces are mode 0 only), so three suffice. Halcyon's `xr_session.cpp`
seam gives our layers 8 slots now (6 before: the HUD panel, the reticle, two sight quads, the squad menu, the wheel,
the prompt, and now two pieces), `kMaxLayers` 12.

## 6. Settings, logs, snapshots

`[Hud]` in `ASI\LE1MEVR.ini` (the mod appends the block with comments to an older file; the host re-reads the section
within a second of a change): `wristStatus`, `wristWidthM`, `wristOffset`, `gunStatus`, `gunWidthM`, `gunBackM`,
`gunUpM`, `wristCrop`, `gunCrop`. Parsed by `vr::hud_pieces::ReadSettings` (shared; a bad value is named in the log and
its default kept). Logs: mod `hud pieces:` (settings, where the panel rides, why not), host `[MEVR-HUD] pieces: ...`
(the settings), `[MEVR-HUD] the player's status on the left wrist: ...` (once, then every minute), `[MEVR-HUD] the
weapon panel on the gun: ...` (per weapon), and `... is back on the HUD panel`. Snapshots: `"mod"."hudPieces"` (the
anchor, the top, why not, whether the host takes it) and the `wrist` and `gun` layers with their images.

## 7. Open until the headset

- The wrist offset's default is a guess from the grip pose's definition and a Touch controller's geometry; the headset
  session should tune `wristOffset` (and maybe make the quad smaller: 8 cm shows the portraits at about 1 cm).
- Whether a billboard reads better than a display fixed to the wrist's twist; a `wristFace` option is a small change.
- Whether 6 cm on the gun is big enough to read the heat strip at arm's length (about 2.5 degrees at 1.4 m on a 60 cm
  arm), and whether the scope on the sniper rifle hides it (`gunBackM` smaller, or per-weapon offsets).
- The squad's custom menu (`[Squad] style = custom`) is drawn at the left hand too and would overlap the wrist piece
  while it is up.

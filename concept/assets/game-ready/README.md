<!-- SPDX-License-Identifier: MIT -->
# Mako cockpit: the game-ready models

The eight `.glb` models here are what `tools/cockpit/package` turns into `BIOG_MEVR_Cockpit.pcc` (research/47, 48):
the chair, the joystick and throttle split into base and moving part, and the hull's console and two sides.
`manifest.json` records each model's source file and SHA-256, the decimation tolerances and the measured deviations
(`tools/cockpit/game_ready.py` wrote it); `previews/` shows each model rendered. The packager also reads the hull's
shell, the aim reticle and the glass panes straight from `../hull reconstruction/assembled/`, and hashes every model's
source (`../detached/*.glb`, the chair's `../repairs/.../Repaired.glb`, the assembled console and sides) against the
manifest, so those eleven source files are in git too; everything else under `concept/` is not (the gitignore lists
them). The glb and png files are stored with Git LFS (`.gitattributes`).

## Where they come from

- The parts (chair, joystick, throttle, the hull's console and side panes) were generated with
  [Tripo](https://www.tripo3d.ai) (Holymolly Ltd) from AI-generated concept images of an invented "M28" vehicle, then
  repaired, split and decimated by our scripts (`tools/cockpit`, the `tripo-game-ready` skill). An earlier Tripo front
  cockpit was replaced by the hull on 2026-11-02 and is no longer packaged.
- The hull's shell, roof, floor, pillars, screens' panels, stands, lamps, the aim reticle and the glass panes are our
  own geometry (`tools/cockpit/hull_assemble.py`, `hull_shapes.py`), with the Tripo console and panes placed in it.
- The raw Tripo outputs, the Blender working files and the concept images stay out of the repository.

## Licence

The Tripo parts were generated on the project owner's Max subscription (a paid plan; around 2026-10-31 to 2026-11-02,
confirmed 2026-11-03), so the generating user holds all rights to the inputs and outputs, including distribution and
licensing, under Tripo's Terms of User Agreement section 5.2.2 (last updated 2025-07-11, read 2026-11-03 at
https://www.tripo3d.ai/terms; Tripo's help page "Commercial Use Rights for Tripo AI 3D Models" says the same). They and
our own geometry are released under the MIT licence, like the project's other original files (`LICENSES/MIT.txt`); the
project as a whole is GPL-3.0 (`NOTICE.md`).

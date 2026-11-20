<!-- SPDX-License-Identifier: MIT -->
# Bone-driven soft-tissue jiggle (breasts, glutes): research for MEVR

Date: 2026-11-19. Scope: what the game industry and the literature do for jiggle with bones alone, and a concrete model for
MEVR (CPU, after the engine's pose, component-space bone writes, 30-120 Hz). Items I could not check against a primary
source are marked [unverified]. Numbers I derived myself are marked (derived).

## 1. Established game techniques

**Source engine `$jigglebone`** (read from `src/public/jigglebones.cpp` in Valve's Source SDK 2013,
https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/jigglebones.cpp; that is Valve's SDK licence, not
GPL, so we read it for concepts only and copy nothing):
- *Flexible:* a tip point simulated in world space. The local error is split along the goal bone's left, up and forward
  axes, and each axis gets its own linear spring-damper: `yawAccel = yawStiffness*err.x - yawDamping*vel.x`, the same for
  pitch, and optionally "along" for length. `tipMass` is a gravity term (`tipAccel.z -= tipMass`). Then the bone is turned
  to point at the tip, which gives a rotational jiggle about the base.
- *Base spring:* the base position gets a spring of its own (`baseStiffness*err - baseDamping*vel`, `baseMass` as gravity),
  clamped to a per-axis box (`baseMinLeft/MaxLeft`...), with friction at the limits. This is the translational jiggle.
- *Boing:* a volume-style squash and stretch along the velocity. It triggers on a change of speed or direction
  (`boingImpactSpeed`, `boingImpactAngle`) and decays as `amplitude*cos(freq*t)*(1-rate*t)^4`.
- *Integration:* plain explicit Euler once per frame (`tipVel += tipAccel*dt; tipPos += tipVel*dt`). dt is floored at
  1 ms, and below a frame-rate cutoff (`cl_jiggle_bone_framerate_cutoff`) the bone just takes the animated pose, with
  hysteresis of 32 frames. Valve's own comment: "movement will be too large between frames to simulate with a simple Euler
  integration." The lesson: per-frame Euler is not frame-rate independent.
- Wiki ranges: stiffness 1-1000, small damping means dramatic jiggle, and `is_rigid` and `is_flexible` are mutually exclusive
  (https://developer.valvesoftware.com/wiki/$jigglebone, https://developer.valvesoftware.com/wiki/Jiggle_Bone). A community
  hat example uses yaw/pitch stiffness 700, damping 6-8, tip_mass 5 (https://polycount.com/discussion/comment/1979831).

**Second Life avatar physics** (breasts, belly, butt; LGPL viewer source,
https://raw.githubusercontent.com/secondlife/viewer/main/indra/newview/llphysicsmotion.cpp). This is the closest prior art
to our case: a CPU model driven by the motion of the chest and pelvis joints.
- Per body part there is Mass, Gravity and Drag. Per axis (breasts: UpDown, InOut, LeftRight; butt: UpDown, LeftRight)
  there is Spring, Gain, Damping and MaxEffect. Fallback defaults: Mass 0.2, Gravity 0, Damping 0.05, Drag 0.15,
  MaxEffect 0.1, Spring 0.1, Gain 10.
- The force on each normalized 1-D offset is `-spring*x + gain*mass*a_joint_local + gravity_local - damping*v` plus
  `0.5*drag*v^2`. The input is the joint's acceleration in its own frame, smoothed as `a = a_new/3 + a_prev*2/3`.
- Integration is semi-implicit Euler in substeps of at most 0.05 s. The model resets if dt > 1 s, clamps velocity, and
  zeroes it at the bounds. MaxEffect is the output range, and MaxEffect 0 turns the effect off.
- The result drives morph/visual params, not bones. The effect is a user wearable with sliders
  (https://community.secondlife.com/knowledgebase/english/customizing-a-classic-style-avatar-r1377/).

**Unreal AnimDynamics** (https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprint-animdynamics-in-unreal-engine):
- Simulates boxes rather than bones, with linear, angular and planar constraints. Linear and angular springs pull toward
  the initial pose or an angular target. Damping overrides default to 0.7.
- Several settings matter for us. `ComponentLinearAccScale` sets how much of the component's world acceleration passes
  into a local-space simulation. `ComponentLinearVelScale` adds drag. `ComponentAppliedLinearAccClamp` caps the acceleration
  those two produce. All three are inertia-share and clamp concepts that we need too.

**Kawaii Physics** (UE plugin, made for "hair, skirts, breasts";
https://pafuhana1213.github.io/KawaiiPhysics-Portal/en/docs/parameters/physics):
- A position-based ("fake") chain with Damping (default 0.1, Quick Start range 0.1-0.3) and Stiffness (0.05, range
  0.05-0.2).
- WorldDampingLocation/Rotation (0.8) sets how much of the character's world motion is ignored, which is an inertia share.
- Also LimitAngle, a collision radius, and teleport thresholds (300 units, 10 degrees).
- Steps run at a fixed `TargetFramerate` (60) with accumulated substeps, capped by MaxSubsteps.
- Quick Start advice: "Erratic during movement → Increase World Damping"
  (https://github.com/pafuhana1213/KawaiiPhysics/wiki/Quick-Start-en).

**Unity Dynamic Bone**: Damping, Elasticity (the pull back to the original orientation), Stiffness (shape keeping), and
Inert (how much of the character's position change is ignored)
(https://g.freya.cat/unityprojects/plain/VRCSDK3AvatarsLegacy/Assets/Resources/DynamicBone/ReadMe.txt).

**VRChat PhysBones**: Pull (return force), Spring (wobble, Simplified mode) or Stiffness (Advanced mode), Gravity, and
Immobile (damps motion from locomotion) (https://creators.vrchat.com/common-components/physbones). One commercial breast
setup uses Pull 0.03-0.1, Momentum about 0.8, and Immobile 0.4-0.9 (https://booth.pm/en/items/4840812). Users report jitter
at high movement speed with high Immobile
(https://feedback.vrchat.com/avatar-30/p/physbones-jitter-at-high-movement-speeds-even-if-immobile-is-1).

**Unity JigglePhysics** (naelstrof, MIT; https://github.com/naelstrof/JigglePhysics):
- A Verlet solve with 0..1 Stiffness, Stretch, Soften, Angle Limit, Drag, Air Drag and Gravity.
- "Soften" reduces stiffness near rest so that neutral poses still move a little.
- "Motionless Root" is recommended for breasts.
- The Blender add-ons in the same family (Wiggle 2 by shteeve3d, GPL-3.0, https://github.com/shteeve3d/blender-wiggle-2,
  and the Verlet fork https://github.com/naelstrof/blender-jiggle-physics) expose Stiff, Dampen, Gravity and Mass. Their
  changelog notes that damping above 0.5 froze the simulation before a remapping
  (https://extensions.blender.org/add-ons/wiggle-2/versions/), a reminder to keep the parameters in physical units.

**Skyrim CBPC** (the dominant breast/butt bone physics in modding):
- Uses three bones per breast, with separate left and right configs.
- Has stiffness (linear), stiffness2 (quadratic), damping, maxoffset and a gravity bias
  (https://nexusmods.com/skyrimspecialedition/articles/3620).
- The guide fetch was refused (HTTP 403), so this summary comes from search snippets. A derived mod notes that it
  switched from Euler to Verlet, so the same numbers no longer feel the same. The internals (fixed `timetick`, per-axis
  linear/rotational output scaling) are [unverified].

**Summary of models.** Every system is one of two kinds:
- (a) A damped spring on a point (translation, or a tip that the bone is turned toward, which gives rotation), driven by
  the parent's motion with a share or gain on the inertia, a clamp, and optional gravity.
- (b) A Verlet chain with "pull to animated pose" fractions.

None of them uses an exact integrator. Those that work at variable frame rates do it with fixed substeps (Kawaii, Second
Life, our hair) or disable themselves at low frame rates (Source).

## 2. Academic work and what reduces to "a few bones plus a CPU solver"

- **Critically and under-damped springs in closed form.**
  - Holden's "Spring-It-On" (https://theorangeduck.com/page/spring-roll-call) gives exact spring-damper updates for any
    dt, parameterized by half-life or by frequency (`stiffness = (2πf)^2`) and damping ratio (`ζ = d/(2√s)`). It shows that
    `lerp(x, g, factor*dt)` is unstable once `damping*dt > 1`, and that the fix is a fixed decay rate,
    `1 - 2^(-dt/halflife)`. It also covers a moving goal through the goal velocity (`c = g + d*q/s`).
  - Juckett gives the same thing as `x'' + 2ωζx' + ω²x = 0`, with four coefficients that depend only on (dt, ω, ζ)
    (https://www.ryanjuckett.com/damped-springs/).
  - **This applies directly:** a per-axis exact spring on each jiggle bone's offset.
- **PD control.** The spring is a PD controller on the offset. Stable PD (Tan, Liu, Turk 2011,
  https://www.cc.gatech.edu/~turk/my_papers/stable_pd.pdf) evaluates the forces at the next state so that high gains stay
  stable at large dt. For a single decoupled 1-D spring the exact solution already gives that, so SPD is not needed.
- **Velocity skinning** (Rohmer et al. 2021, https://arxiv.org/abs/2104.04934, formulas from
  https://ar5iv.labs.arxiv.org/html/2104.04934):
  - Floppy: `ψ = -k_floppy * v_bone`, plus a rotation about the bone by `-k*|ω|`.
  - Squashy: a volume-preserving scale `(1+s, 1/√(1+s), 1/√(1+s))` with `s = k*|v|`.
  - It is per-vertex, has no state, and is "not physically based": no overshoot, and the effect is zero when the velocity
    is zero.
  - **Reduction:** compute the floppy term per jiggle bone as a bone offset, `offset = -k * v_parent_local`. The result is
    a lag with no wobble. It is useful as a cheap "first-order" fallback, or as the damping part of the spring.
- **Complementary Dynamics** (Zhang et al. 2020, https://www.dgp.toronto.edu/projects/complementary-dynamics/) adds
  physics only in the subspace orthogonal to what the rig can do, but is far from real time. The fast follow-up, **Fast
  Complementary Dynamics via Skinning Eigenmodes** (Benchekroun et al. 2023, https://arxiv.org/abs/2303.11886), represents
  the secondary motion as extra LBS weights (10-14 modes in its examples), each driving a 12-DOF affine handle. The
  complementarity is baked into the weights offline (the KKT eigenproblem against the rig Jacobian). It needs a tet mesh
  precompute and over 60 fps for a 330k-vertex scene (https://ar5iv.labs.arxiv.org/html/2303.11886).
  - **Reduction:** the modes *are* extra bones with computed weights. But our bones are rigid (plus a uniform scale
    [verify UE3 FBoneAtom]), not affine, and the offline tet and eigen work on the player's machine is heavy.
  - Worth borrowing the principle: the jiggle bone should hold the animated pose exactly at rest and add only an offset,
    so it never fights the animation.
- **Neural approaches** (Deep Emulator, Zheng et al. CVPR 2021, https://arxiv.org/abs/2103.01261; SoftSMPL, Santesteban et
  al. 2020, https://arxiv.org/abs/2004.00326) predict per-vertex dynamics. They are not reducible to a few bones without
  retraining on a bone subspace, and they are out of scope.
- **XPBD** (Macklin et al. 2016, https://matthias-research.github.io/pages/publications/XPBD.pdf) fixes PBD's dependence of
  stiffness on the time step and iteration count (compliance `α/Δt²`). That matters if we reuse the Verlet hair chain:
  "stiffness as a fraction per step" is only frame-rate independent because the substep is fixed.

## 3. Mechanics to target (real tissue, then the game's choice)

- **Breast measurements.** Breasts are modelled in biomechanics as a forced, damped mass-spring-damper driven by the
  suprasternal notch, and stiffness limits motion most (Haake & Scurr 2010,
  https://shura.shu.ac.uk/2367). A piecewise model fitted to free vibration found the tissue much softer above
  equilibrium than below: k_a 73.5, k_b 658 N/m, c_a 1.83, c_b 2.07 N·s/m (Cai et al., J. Biomech. 2018,
  https://repository.eduhk.hk/en/publications/a-piecewise-mass-spring-damper-model-of-the-human-breast/).
- **Breast frequency and damping (derived).** Assuming a breast mass of 0.3-0.5 kg [unverified, the paper estimated it from
  scans]:
  - Upward: about 1.9-2.5 Hz with ζ about 0.15-0.19.
  - Downward: 5.8-7.5 Hz with ζ about 0.06-0.07.
- **Displacement.** Unsupported resultant displacement is about 4.2 cm walking and 15.2 cm running. Vertical displacement
  at 10 km/h is 7.8 cm without a bra, 5.1 cm with an everyday bra and 4.0 cm with a sports bra
  (https://researchportal.port.ac.uk/en/publications/breast-displacement-in-three-dimensions-during-the-walking-and-ru/,
  https://ojs.ub.uni-konstanz.de/cpa/article/view/4910/4553). Clothing and armour cut motion strongly, and every Mass Effect
  outfit is clothed or armoured.
- **Driving frequencies.** Walking cadence is roughly 100-120 steps/min, about 1.7-2.0 Hz
  (https://pubmed.ncbi.nlm.nih.gov/29858465/). The thorax oscillates vertically at 2.66 Hz in running (the Konstanz paper
  above). **A breast spring tuned at 2-2.7 Hz resonates with gait**, which is the "bouncy" look. Keep ζ ≥ 0.2, or move f
  off the step frequency, to stay tasteful.
- **Glutes and thigh.** The thigh and shank's wobbling masses show 3-55 Hz and 3 mm-4 cm excursions after heel strike
  (https://link.springer.com/content/pdf/10.1007/s00419-010-0458-z.pdf). I found no gluteal-specific frequency or damping
  ratio. Glutes are muscle over pelvis and should read stiffer and more damped than breasts [unverified; a game-art choice].

## 4. Rigging in common rigs

- **CC4 / Character Creator** has `CC_Base_L_Breast`/`R_Breast`. Users report them placed backwards or at the centre line
  after some conversions. CC4's hip "extend" bones are not skin-weighted, so they do nothing as springs
  (https://reallusion.com/FeedBackTracker/Issue/Breast-bones-created-backwards-and-would-not-hold-corrections,
  https://www.reallusion.com/FeedBackTracker/Issue/Hip-and-abdominal-extend-bones-have-no-effect-when-activated-as-spring-bones).
- **Daz Genesis 8** has `lPectoral`/`rPectoral`, driven by collar rotation and body morphs. Exports often flip or misplace
  them. One user re-rigged with 3 bones per breast, 2 per glute and 1 for the belly
  (https://bitbucket.org/Diffeomorphic/import_daz/issues/1175/unable-to-import-daz-figure-with-modified).
- **MetaHuman** chest and glute helper bones are RBF pose-driver correctives. The working recipe is to duplicate the
  bones, parent the copies to the originals, transfer the weights and re-import
  (https://forums.unrealengine.com/t/jiggle-physics-for-metahumans/1219212).
- **Skyrim CBPC** uses 3 per breast, plus butt bones (Nexus guide above).
- **Mass Effect** bodies: Miranda's ME2 body is the standard 110-bone humanoid, weighted to Chest, Chest1, Chest2, Pelvis,
  the hips and their twists, with no breast or glute bone (docs/trilogy-hair-plan.md section 10).

**Pitfalls**
- Linear blend skinning loses volume under rotation, so prefer small rotations about a pivot deep at the chest wall, with
  translation limited to about 15-25% of the tissue depth (derived).
- Weights that reach the armpit, the sternum or the gluteal fold make the arm, the cleavage or the thigh crease move.
- Rigid armour plates on the same mesh must get zero jiggle weight.
- Arms cross the chest (especially our VRIK arms), so a collision is needed, as with the hair.

## 5. Defaults, intensity and content notes

- Systems expose a per-part intensity: Second Life's MaxEffect (0 means off), CBPC's per-NPC configs, VRChat's Pull and
  Immobile. Defaults in the tools are low: Kawaii stiffness 0.05 with world damping 0.8.
- Nexus Mods now locks adult tags (a "Sexualised" or "Pornographic" categorisation) and is adding age verification for
  UK and EU users (https://www.nexusmods.com/news/15319). A subtle physics default on an M-rated game is ordinary. What
  matters is scope:
  - apply it to adult humanoid body meshes only, through a mesh allowlist (never children, never non-humanoid species);
  - give each character and each part an off switch;
  - set a global intensity;
  - keep the default subtle and clothing-aware.

## 6. Recommendation for MEVR

**Model:** a per-bone offset spring in the parent's frame, driven by the inertia of the tissue's own point, solved
exactly. It is complementary: zero offset means exactly the animated pose.

**State and frame**
- For each jiggle bone j, the state is the offset `x` (in m) and velocity `w` in the frame of its parent bone (Chest2 for a
  breast, Pelvis for a glute). The frame is taken from the animated pose each frame.
- Per-axis parameters: frequency `f_i`, damping ratio `ζ_i`, and `ω_i = 2πf_i`.

**Each frame** (dt = real time since the last frame):
1. **Input.** Let P be the animated world position of the tissue's centre of mass: the bone's pivot plus `r`, about 0.05 m
   along its forward axis for a breast, 0.04 m for a glute. Using P rather than the bone origin brings in the chest's
   rotation (`v = v_chest + ω×r`) for free.
   - `u = (P - P_prev)/dt`, smoothed with a decay of half-life 15-25 ms: `u_s += (u - u_s)*(1 - 2^(-dt/h))`.
2. **Inertia as a velocity kick.**
   - `Δu = clampLen(u_s - u_s_prev, a_max*dt)`, with a_max 35 m/s² (about 3.5 g; UE's `ComponentAppliedLinearAccClamp`
     idea).
   - `w -= g_in * R^T Δu`, where g_in is the inertia share. The integral of the acceleration is exact whatever dt is, so
     the result does not depend on the frame rate.
3. **Gravity (optional).** Shift the equilibrium per axis:
   `c_i = g_grav * (R^T g - g_ref)_i / ω_i²`. g_ref is the gravity in the local frame of the standing pose, since the
   authored pose already sags; this term only matters when the body leans or lies down.
4. **Exact under-damped update per axis** (ζ<1; Juckett/Holden). Let `y = x - c`, `a = ζω`, `ω_d = ω√(1-ζ²)`,
   `e = exp(-a·dt)`:
   `y' = e·[y·cos(ω_d dt) + (w + a·y)/ω_d · sin(ω_d dt)]`,
   `w' = e·[w·cos(ω_d dt) - (a·w + ω²·y)/ω_d · sin(ω_d dt)]`, then `x = y' + c`.
   - This is stable for any dt. No substeps are needed unless we want the asymmetric up/down spring (Cai et al.); that
     option needs substeps of 1/240 s and a switch of ω by the sign of `x_up`.
5. **Limits.**
   - Show `x_out = M_i·tanh(x_i/M_i)`, a soft limit with no pop.
   - If `|x_i| > 1.5·M_i`, clamp the state and zero the outward `w_i` (Second Life zeroes velocity at its bounds).
6. **Reset.** Set x = w = 0 and take `u_s` fresh when:
   - P moves more than 0.5 m in a frame or `|u| > 10 m/s` (a teleport, floor lock, a cut; Kawaii uses 300 units and
     10 degrees);
   - dt > 0.25 s;
   - a value is not finite;
   - the mesh's LOD changes.
7. **Output in the parent frame.** The rotation share is ρ.
   - Translate the bone by `(1-ρ)·x_out`.
   - Rotate it about its pivot by the small angles `ρ·x_out.lateral/L` and `ρ·x_out.vertical/L` (yaw and pitch, no roll),
     with lever L = the distance from the pivot to the centre of mass. That is the Source "flexible" look on top of a
     "base spring".
   - Default ρ = 0.6.
   - Squash and stretch is skipped unless non-uniform bone scale exists [verify UE3 FBoneAtom: uniform scale only].
8. **Collision.** Push the breast's mass sphere (radius 0.06 m) out of the upper-arm and forearm capsules, both the
   person's own and the player's VRIK arms (the hair's `hand_contact::Capsule`), as an offset limit (no added velocity).

**Defaults** (the global `intensity` 0..1, default 0.5, scales M and g_in, never f or ζ):

| Part, axis (parent frame) | f Hz (range) | ζ (range) | M cm at intensity 1 (armour ×0.4) | g_in |
|---|---|---|---|---|
| Breast vertical | 2.4 (1.8-3.5) | 0.25 (0.15-0.5) | 3.0 | 0.35 (0.1-1) |
| Breast lateral | 2.2 (1.6-3.0) | 0.30 (0.2-0.5) | 2.0 | 0.30 |
| Breast in/out | 3.5 (2.5-5) | 0.35 (0.25-0.6) | 1.0 | 0.20 |
| Glute vertical | 4.0 (3-6) | 0.40 (0.3-0.6) | 1.2 | 0.30 |
| Glute lateral | 3.5 (2.5-5) | 0.45 (0.3-0.6) | 0.8 | 0.25 |
| Glute in/out | off, or 5.0 | 0.5 | 0.4 | 0.15 |

g_grav defaults to 0.3 (0-1). The breast values come from the derived 2-2.5 Hz, ζ about 0.2 above equilibrium, with damping
raised for taste. The glute values are [unverified] art choices, kept stiffer.

**Sanity check (derived).** The peak excursion from a velocity step Δu is about `g_in·Δu/ω`. A 3 m/s landing at
f = 2.4 Hz and g_in = 0.35 gives about 7 cm, which the soft limit brings to M. That is why g_in sits well below 1 and M
carries the look. The same check holds against walking: Δu of about 0.3-0.5 m/s per step gives about 1 cm, which is in
line with a clothed walk.

**Reusing `core/ik/HairChain`.** A one-bone chain gives only the rotational half (its first point is the animated one).
Its "fraction per 1/60 s" maps to physical units at substep h approximately as `ω ≈ √k/h` and `ζ ≈ c/(2√k)` for the
per-substep pull k and velocity loss c (derived, small k and c). Example: f = 3 Hz and ζ = 0.25 at h = 1/120 s give
k ≈ 0.025 and c ≈ 0.08. A dedicated `core/ik/Jiggle` with the exact spring above is simpler to tune and to test.

**Bones and weights** (generated per body from landmarks)
- **Breast (one bone per side, parent Chest2).**
  - Pivot: on the chest wall behind the breast's centroid, about 0.3 of the tissue depth in from the ribs and slightly
    above the centroid.
  - Axes: forward through the apex, up along the parent's up.
  - Weights: a smooth falloff (smoothstep) from about 0.9 at the apex region to 0 at the base perimeter, taken out of the
    existing Chest/Chest1/Chest2 weights so that each vertex still sums to 1.
  - Zero weight on the sternum line, under-arm and clavicle. Partial weight (≤ 0.3) in the inframammary fold.
  - A second "tip" bone per side is optional only if one bone looks rigid.
- **Glute (one bone per side, parent Pelvis).**
  - Pivot: deep under the gluteal centroid.
  - Weights: ≤ 0.7 at the centroid, fading to 0 toward the sacrum, the hip joint and the gluteal fold. Multiply by the
    vertex's existing Pelvis share so that the thigh's twist and hip weights keep the crease.
- **Outfits.** Per-material or per-section masks: rigid armour plates get 0. Cloth on top gets the same weights only if it
  is the same mesh.

**Validation plan** (the `mevr anim` stunt driver plus `engine/ClipPose` clips; log x, w, Δu and the clamp hits per bone to
CSV each frame):
1. **Idle.** A held pose for 5 s brings the offset under 0.5 mm, with no drift.
2. **Walk and run cycle.** Steady-state peak-to-peak |x| per axis is in range (breast vertical about 0.5-1.5 cm walking at
   intensity 0.5). The spectrum peaks at the step frequency, not the spring's. Clamp hits under 1% of frames.
3. **Jump and landing.** At landing: the peak offset, the number of overshoots (at most 2 visible, above 2 mm), and the time
   to settle to 5% (target 0.4-0.8 s for breasts, under 0.4 s for glutes). No pop at take-off.
4. **Frame-rate independence.** Play the same clip with fixed dt of 1/30, 1/60, 1/90 and 1/120, and with a jittered dt (±30%
   plus one 0.2 s spike). The RMS difference of the traces should stay under 5% of the peak, and the trace should be
   finite and without a jump after the spike.
5. **Turns and teleports.** A turn in place at 180°/s should give a small lateral response. A teleport or cut should
   reset with no visible snap.
6. **Collisions.** An arm sweeping across the chest (the VRIK replay): the breast sphere never inside the arm capsule, and
   no jitter frame to frame.
7. **Look check.** The same pose and frame in snapshots at intensity 0, 0.5 and 1, for the owner's choice of defaults.

## Status in MEVR

2026-11-19: this research (done by a research agent for the owner's jiggle request, sources above) is the design of
`core/ik/Jiggle`; it is benched offline with stand-in bones on the generated walk, run and jump clips (`mevr-hair-bench
--jiggle`, tools/motion). In the game it needs the added bones (docs/trilogy-hair-plan.md section 10). The look, the
defaults and whether it is on by default go to the owner first.

## 7. The owner's brief (2026-11-19)

The owner shared a research brief written for another LE3 project ("Soft-Body Secondary Motion Research Brief", 19 Nov
2026; the owner's copy, not in the repository). Its conclusions, against what MEVR has:

- **Same conclusion as section 6**: none of the runtime tools of today (Kawaii Physics and SPCR Joint Dynamics are UE4/5
  plugins, OmniBody and NVIDIA Newton are GPU solvers, NDG and PhysSkin++ are neural deformers needing a GPU or a Poisson
  solve per frame) can run in LE; what carries over is **helper bones driven by springs**, and the key unknown is **whether
  the game takes a mesh with added bones**. That is our probe P3 (docs/trilogy-hair-plan.md): one experiment unblocks the
  hair re-rig and the jiggle bones.
- **Its runtime drivers we don't need**: it proposes the PhysicsAsset's unfixed bodies with bone springs, or the AnimTree's
  `SkelControlTrail`, both unverified in LE (PhysX on non-ragdoll characters in conversations is its open question). MEVR
  writes the bones itself after `UpdateSkelPose` (`engine/HairPose`, already on ME3's Miranda), with `core/ik/Jiggle`'s exact
  spring, so neither the PhysicsAsset nor the AnimTree has to be touched, and it works in conversations and cinematics.
- **Baking is the fallback it gives for cutscenes**; with a runtime driver every animation is covered, reacting to turns and
  hits, so we don't bake.
- **What we take from it: an offline reference for tuning and rigging.** A volumetric simulation (FEM/XPBD, or the Deep
  Emulator as its proxy) of the body over our generated clips (tools/motion: walk, run, jumps), fitted back to a few helper
  bones by skinning decomposition (SSDR; Dem-Bones, Electronic Arts' open implementation [verify its licence before use]),
  gives three things: where the helper bones should sit and how the soft regions weight to them (a better start than painting
  by hand), the bones' motion a real tissue would make (the target `mevr-hair-bench --jiggle` fits the springs' frequency,
  damping and limits to, instead of numbers from the literature alone), and a check of how much 2 bones a side can show at
  all. Offline only: nothing of it ships or runs in the game.
- Its tone note matches ours: subtle and physically motivated.
- Out of scope here: its Character Creator 5 skin pipeline and Reallusion licensing (another project's look development).

## In the game (2026-11-19)

`tools/rig` added the four stand-in bones to ME3 Miranda's body (`BreastLeft/Right` on `Chest2`, `GluteLeft/Right` on
`Pelvis`, placed from her mesh's surface: the front peaks at x 12.5 cm around 132-138 cm, the back at x -12.7 around 92-98
cm) and weighted them (ellipsoids taking only the chest's or the pelvis's share: 92-112 vertices a breast on LOD 0, about
80 a glute, peak shares 0.8 and 0.5). Their rest axes are the body's (X out of the body), so at run time each bone's own
animated rotation is its jiggle frame. The hair stage drives them with `core/ik/Jiggle` on the same call as the hair.

Measured on the generated jump (the stage's trace, intensity 0.5, the fake headset's 32 fps): breasts +-1.0-1.2 cm
vertical, lagging at the take-off and overshooting at the landing, settled within 0.3 s; glutes +-0.3 cm; the two sides
agree. Subtle by design (the suit is tight): `[Hair] jiggleIntensity` (0..1) scales the reach, `mevr hair jiggle <f>|off`
live. Still to judge in the headset: whether 0.5 reads at all at conversation distance.

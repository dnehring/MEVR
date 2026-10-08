# VR features plan

Items marked **[verify]** are hypotheses based on stock UE3/UDK. Confirm them in Legendary Explorer
(LEX) once the game is installed, since BioWare's fork may differ.

## Camera and stereo (M1–M4)

- **First-person base camera:** MassFPS's script-side camera modes put the game camera at
  Shepard's eyes. Our ASI then only *adds* the HMD pose on top.
- **Camera hook (built, M3.2):** `ULocalPlayer::CalcSceneView` (LE1 `0x4C5300`). Inside it the
  engine asks script for the view point and FOV; our handlers change them only there, so only
  the rendered view follows the head (`src/engine/SceneView.*`, `src/game/HeadTracking.*`).
  Asymmetric per-eye projection is still to do (we render a symmetric FOV covering both eyes).
- **6DOF (built, `/`):** head position offsets the rendered view (1 m = 100 units [verify]).
- **Stereo, stage 1 (built, `;`):** alternate-eye rendering. Frames alternate eyes at ±IPD/2,
  each tagged with its eye's pose, and each eye keeps its latest image. Cheap; each eye updates
  at half the frame rate.
- **Stereo, stage 2:** ~~split-screen views~~ (refuted: UE3-LE skips lighting and post for a
  secondary view, research/13). Instead, render the whole frame twice per present, each a full
  primary render (Halcyon's approach: a second `FViewportClient::Draw`, LE1 `0x4C71A0`, and pass
  capture on the render thread).
- **Post effects to disable:** motion blur, depth of field, film grain, vignette, lens flares,
  chromatic aberration. Most are reachable through ini settings or the LEX console.

## First-person body and body IK

Current plan, prior art and what's built: [research/14-vrik.md](../research/14-vrik.md). The
IK math is in `src/core/ik` (unit-tested); the skeleton probe decides whether we drive the
engine's `SkelControlLimb` or write the arm bones ourselves.

- **Base: MassFPS (LE1)** ([research/11](../research/11-massfps.md)), which we're allowed to fold
  into our mod. It already provides:
  - first-person camera modes (`bFirstPerson`, `SFXCameraSetup` offsets at head height)
  - head hiding (the head is separate components)
  - near-clip pull-in
  - camera shake and FOV changes removed
  - movement relative to the PlayerController's facing
  - first-person cinematic transitions
- **VR changes to MassFPS:**
  - Strip all camera roll (cover lean, sprint wobble, interpolated roll) and the cover-peek
    camera offsets.
  - Keep the body visible in every gameplay mode, not just combat.
  - Replace the wireframe-head trick. 6DOF eyes can leave the head volume, so hide the head
    fully for the owner (`HideBoneByName` or SetOwnerNoSee without wireframe).
- **Arms:** UE3 AnimTrees include **`SkelControlLimb`**, a built-in two-bone IK node with
  `EffectorLocation` and `JointTargetLocation`. Add one per arm to Shepard's AnimTree in LEX, then
  write each controller's pose into `EffectorLocation` from the plugin every frame. That gives
  engine-native arm IK without writing a solver. **[verify]** which AnimTree the player pawn uses.
  Fallback: our own analytic two-bone solver, writing bone transforms directly, using the approach
  described for Overlord's `pose_solver` (elbow pole swung from a rest bend, single bend plane,
  1.25× stretch limit).
- **Head and spine:** `SkelControlLookAt` / `SkelControlSingleBone` to bend the torso toward the HMD.
- **Fingers:** our own finger-curl solver for grip and trigger, modeled on the approach in
  Overlord's `hands/pose_solver` (GPL, so we reimplement rather than copy).
- **Legs:** keep ME's locomotion animations; the pawn yaw follows the HMD past a deadzone.
  Stretch goal: `SkelControlFootPlacement`.

## Gun handling

- **Aim source:** UE3 weapons fire along `GetAdjustedAim` from `GetPhysicalFireStartLoc`
  (script). Override both to the controller's aim pose. **[verify]** in `SFXWeapon`.
  - Direction: done for LE1 (`game/HandAim`, through `BioPlayerController.GetAdjustedAimFor`),
    with our renderer and with Halcyon's, whose on-foot head aim steps aside while the right
    controller aims.
  - Start location: not yet. The log measures how far shots start from the controller's ray.
- **Weapon placement:** attach the weapon mesh to the hand socket, so it follows the IK hand
  automatically, plus a per-weapon-class grip offset table (pistol, SMG, shotgun, AR, sniper,
  heavy).
- **Two-handed stabilization:** off-hand near the foregrip sets the aim direction from the
  vector between the hands. Reimplement the carry and grip arbitration pattern from Overlord.
- **Reloads:** LE2/LE3 thermal clips: eject, grab a clip from the belt, insert. That maps
  directly onto TF2VR/Overlord-style reloads. LE1 has no reloads; an optional vent gesture could
  cool the weapon early.
- **Holsters:** four weapon slots. Hips, back and chest, as in Overlord.
- **Powers:** power wheel as a radial menu on the off-hand. Stretch goal: gesture casting.
- **Cover (LE2/LE3):** keep button cover, but let the player physically lean and peek out of it.

## HUD and menus

- ME LE's UI is Scaleform GFx. UE3's `GFxMoviePlayer` has a **`RenderTexture`** property that
  renders a movie into a render target instead of the backbuffer. Redirect the HUD and menu movies
  there, then show them as OpenXR cylinder or quad layers. This is new code:
  REFramework's overlay is OpenVR-only. Overlord's `openxr_menu` is the design reference.
  **[verify]** in LEX that ME's movie classes keep `RenderTexture`.
- **MVP:** the whole HUD on one lazy-follow quad. **Later:** diegetic placement, with ammo or heat
  on the gun and shields/health on the wrist, by rendering individual GFx elements separately.
  Overlord already ships the "ammo billboard at the weapon" pattern, drawn into both eyes at
  weapon depth ([research/10](../research/10-vr-code-survey.md)).
- **Dialogue wheel:** a world-anchored panel driven by a laser pointer.

## Conversations and cutscenes

ME spends a large share of playtime in conversations with cinematic cameras, and forced camera
motion in VR causes motion sickness. Plan:

1. **Default:** detect when a conversation starts (see LExASIs `ConvoSniffer` for the hooks) and
   move the game image onto a large virtual cinema screen, as Overlord does for scripted scenes.
2. **Option:** a stereo cinema screen (3D movie mode) using the M4 stereo path.
3. **Stretch:** keep the player head-tracked and in the world for static conversation shots, and
   cut to the cinema screen only for camera moves.

## Vehicles

The Mako (LE1) and Hammerhead (LE2): cockpit-camera VR is a stretch goal. Default to the cinema
screen.

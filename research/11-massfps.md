# 11: MassFPS: First-Person Camera (LE1) by Aphar

- **Nexus:** https://www.nexusmods.com/masseffectlegendaryedition/mods/1731
- **Version:** 1.2.2.1. Uploaded 2023-06-16, last updated 2023-07-11. About 4.3k unique downloads,
  271 endorsements.
- **Code article:** "MassFPS 1.2 code comments",
  https://www.nexusmods.com/masseffectlegendaryedition/articles/106
- Read 2026-10-07 in the built-in browser (Nexus returns 403 to WebFetch).

## Permissions: fully permissive, compatible with our license (GPL-3.0 since 2026-10-08)

From the mod page's "Permissions and credits": "This mod is entirely my own work and includes no
modifications from any other authors. You may do whatever you want with this mod, no credit or
permission needed. That includes … modifying it and releasing the modifications, incorporating
its modifications into your own mod, etc."

- **Raw files:** drag the `.m3m` files into ME3Tweaks Mod Manager (or run `tools/extract-m3m.ps1`)
  to extract the `.uc` sources and `MassFPS_LE1.pcc`. They're read locally and aren't in the
  repository.
- **One exception:** compatibility with LE1 Community Patch required packaging an alternate
  version of the Community Patch's `SFXCameraMode_CombatStorm.Initialize`. That function is the
  Community Patch's work; check its terms before reusing it.
- We'll credit Aphar anyway.

## What it does (mod page)

- Plays the entire game in first person, combat and exploration both. **Since v1.2, Shepard's body
  is visible in combat when not in cover.**
- Combat cameras are adjusted for first person, with multi-depth corner peeking and a dynamic lean.
- Exploration movement is more responsive with smooth stops. Sprint turns faster; sprint has no
  FOV change, just a slight camera wobble.
- Elevators and exploration crouch are first-person. **Conversations and vehicles stay vanilla
  (third person).**
- Installs via ME3MM; the first-person modes can be chosen separately at install.
- **Incompatible** with Proper Convo-Cutscene Skipper (LE1): wireframe or invisible heads in
  conversations.
- **Known issues:**
  - May not face the conversation partner after a conversation.
  - Camera vs certain cover geometry.
  - Arm clipping when switching to or from the sniper rifle or assault rifle.
  - Can't turn while sprinting on a controller.
  - Headless death cam.
- **Older coverage conflicts with the current page.** GamePressure and PC Gamer, citing an earlier
  version, said the hands and gun are hidden in first person. The current page (v1.2+) says
  Shepard appears in combat.
- **LE2 version** exists (feature parity per PC Gamer; crouch was in progress). The page says
  "An LE3 version is coming Soon™". Whether LE3 shipped is unknown. **The LE2 permissions haven't
  been checked yet.**

## How it works (code article), with what it means for VR

### Making Shepard visible to an inside-the-head camera

- **`Default__BioPawnType.m_bPawnHideWithCameraCollision = FALSE`.** Shepard isn't culled when the
  camera is inside the head.
- **`FPP_HideHead.uc`:** decides whether to hide just the head (combat cameras and flagged
  interpolation cameras) or the whole body (otherwise).
  - **Head hiding:** `SetOwnerNoSee()`, so the head still casts a shadow, plus **forcing wireframe
    on head parts**. Some lights reveal a shadowy silhouette, and the wireframe hides it.
  - **Separate head parts:** hair, helmet, eyes, tongue. Eyes and tongue get removed outright.
  - **Confirms H5:** the head is separate components from the body.
- **`FPP_ShowHead.uc`, `FPP_ShowHeadInConversations.uc`, `FPP_ShowHeadInCinematics.uc`:** restore
  the head for conversations and cinematics.
- **`FPP_InterpolateCheckAndTransitionSpeedFix.uc`:**
  - Exploration and cover transitions may hide the whole body.
  - Sprint, crouch and tight-aim transitions hide only the head.
  - Transition times are fixed to match the animations.
- **Sniper camera name:** `Default__SFXWeaponStats_SniperRifleBase.SniperTightAim0.CameraName` is
  changed to `'SniperTightAim'` so the head-hide logic can filter it.

**For VR:** the wireframe trick is tuned for a fixed camera inside the head. With 6DOF head motion
and stereo, the eyes can leave the head volume and see the wireframe. Prefer hiding the head
entirely for the owner, e.g. via `HideBoneByName` (H6), or accept losing the head shadow. VR
also wants the body visible in **all** gameplay modes (for IK arms), not only in combat.

### Camera system (ME1)

- **`Default__SFXCameraSetup`** holds every camera type, each with:
  - `HookOffset`: the pivot relative to the player origin.
  - `Offset`: the camera position relative to the hook.
- **Cover camera types:** Blind (hip-fire from cover), Peek, PowerCover, Lean. A Y component in
  `HookOffset` sets the lean direction.
- **First-person offsets:** MassFPS tuned X and Z offsets to Shepard's head position, and crouch
  offsets separately. These are useful starting values for our eye anchor.
- **`bFirstPerson = TRUE`** on `SFXCameraMode_Combat` (and inherited by CombatStorm), `_Explore`,
  and `SFXGameModeCinematic.CineCam0/1`. This **turns on the engine path that removes Shepard from
  the render list** (normally used for the sniper scope).
- **`SetRenderStateOfPlayer(0, 1.0)`** pulls the **near clip** in from the default **10.0** units
  to reduce shoulder clipping. VR needs this too, for hands near the face.
- **`SFXCameraMode_CombatStorm` / `_ExploreStorm`:** input interference, camera shake and FOV
  changes removed. `s_Input` turn speeds are rebalanced.
- **`Default__SFXPawn_Player.StormTurnSpeed = 250.0`:** faster turning while sprinting.
- **Cinematic cameras:** `CineCam0` gets FOV 70 (vs 80) so transitions into and out of cinematics
  stay first-person.
- **Exploration crouch:** a new `ExploreCrouch` camera mode (`ExploreCrouch_AddVariable.uc`,
  `ExploreCrouch_AllowSwitch.uc`).
- **Elevators and the Mako:** `ExploreCamElevator0` offset is set, and
  `VehicleTransitionTime = 0.0` avoids the camera passing through Shepard's head after Mako exits.

**For VR: remove or replace these.** All of them inject **camera roll** or forced camera motion,
which causes discomfort in a headset.
- `CoverLeanCombatCams.uc` and `CoverLeanTightAimCams.uc` (eased roll up to 11.25°).
- `CoverLeanInterpolateRoll.uc`.
- `SprintShake_*.uc` (±0.35° roll oscillation at footstep cadence).
- Cover peek offsets, which move the camera sideways. Replace with physical leaning.
- The sniper tight-aim zoom. Scopes should become render-to-texture on the scope, later.

### Movement (`SmoothExploreMovement.uc`)

- Pawn facing follows the input direction while moving.
- Stops smoothly: speed decays by `(1 - 8/framerate)`.
- **Movement direction comes from the PlayerController's facing plus the strafe inputs**, the
  same method combat movement uses, with `bForce = TRUE` on speed to skip animation-driven
  input interference.

**For VR:** this is the decoupling we need. Set the PlayerController yaw from the HMD (head-
relative movement) or from the off-hand controller (hand-relative movement), and movement follows.

## Verified from extracted source (2026-10-07)

The mod was extracted locally with `tools/extract-m3m.ps1` (download it from Nexus to repeat this;
its files aren't in the repository). Every change targets `SFXGame.pcc`.

- **`.m3m` format (v1):**
  - `"M3MM"` magic, then a version byte (1).
  - The manifest as an Unreal string (negative length means UTF-16 JSON).
  - An int32 asset count, then per asset: `"MMV1"`, name, int32 size, bytes.
  - Change kinds: `scriptupdate` (one function, inline `scripttext`), `addtoclassorreplace`
    (`scripts[]` plus `scriptfilenames[]`), `assetupdate` (copy an export from the embedded
    package), and `propertyupdates`.
- **Head parts** (`FPP_HideHead.uc`, in `SFXCameraMode.DoSniperCameraCollision`):
  - `BioPawn.m_oHeadMesh`, `m_oHairMesh`, `m_oHeadGearMesh`, `m_oVisorMesh`, `m_oFacePlateMesh`
    each get `SetOwnerNoSee(TRUE)` and `bForceWireframe = 1`.
  - Eyes and tongue: `GetPrimarySkelMeshComponent().HideBoneByName('Eye_Left'|'Eye_Right'|'Tongue', 0)`.
  - **This confirms H6.**
  - Whole-body hide: add the pawn and its `Attached` actors to `BioPlayerController.HiddenActors`.
  - Head-only cameras are matched by `CameraName`: `CombatCam`, `CombatStormCam`,
    `CombatTightAim`, `CineCombatCam`, `HideHeadInInterpolate`.
- **Camera tick** (`CoverLean_CombatCams.uc` replaces `SFXCameraMode_Combat.Tick`):
  - Builds `m_pov` (Rotation pitch/yaw from the view-target controller, `Location =
    GetCameraLocation()`, `FOV`).
  - Calls `BioWorldInfo.SetRenderStateOfPlayer(0, 1.0)` every tick.
  - Runs **auto-recentering** (`bRecenterCameraNew`, `RecenterStrength`) and **depth-of-field focus**
    via `SFXPlayerCamera.GetTrace` and `DOFHandler.Focus`. Disable both for VR.
  - Roll uses UE3 rotator units (65536 = 360°).
- **Movement** (`SmoothExploreMovement.uc`, replaces
  `BioPlayerController.PlayerWalking.PlayerMoveExplore`):
  `GetAxes(Rotation, X, Y, Z); MoveDir = aForward*X + aStrafe*Y`, so movement follows the
  **controller rotation**. `SetDesiredSpeed(..., TRUE)`, `SetDesiredRotation(MoveFacing, FALSE)`.

## Bottom line

MassFPS is the **script-side first-person foundation for LE1**, and its license lets us absorb
it. Plan:

1. Extract its raws (`.m3m`, `tools/extract-m3m.ps1`) locally to read them (not committed); credit
   Aphar.
2. Keep: `bFirstPerson` camera modes and offsets (as the eye anchor), the near-clip change,
   shake/FOV removal, smooth controller-relative movement, the exploration crouch camera,
   first-person cinematic transition cameras, and the conversation and cinematic head restore.
3. Remove: every camera roll, cover-peek camera offsets, sprint wobble, the wireframe head trick,
   and tight-aim zoom.
4. Change: body visible in every gameplay mode, with the head hidden for the owner only.
5. Our ASI then layers the HMD pose and stereo on top of an already first-person base camera.
   This should make M1–M3 simpler.

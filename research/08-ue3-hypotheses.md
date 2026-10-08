# 08: UE3 mechanisms we plan to rely on

All of this comes from stock UE3/UDK knowledge and is **unverified against ME LE**. Check each item
in Legendary Explorer (script classes, AnimTrees, GFx packages) or Ghidra (native code) and record
the result here.

| # | Hypothesis | Why it matters | How to verify | Result |
|---|-----------|----------------|---------------|--------|
| H1 | The view is built in native `ULocalPlayer::CalcSceneView`, after script `PlayerController.GetPlayerViewPoint` and `Camera.UpdateCamera` | Single hook point for HMD pose plus per-eye projection | Ghidra: find CalcSceneView (via the vtable/RTTI of LocalPlayer). LEX: ME camera classes (`BioCamera`/`SFXCamera`?) | **Partly confirmed (code reading, [09](09-me3tweaks-code-survey.md)).** ConsoleExtension hooks `CalcSceneView` on **LE3** and rewrites FSceneView view/projection matrices (LE3 offsets ViewMatrix 0x90, ProjectionMatrix 0xD0). It also overrides `CameraCache.POV` in `Engine.Camera.UpdateCamera` on all games. **Still needed: the LE1 `CalcSceneView` address and FSceneView offsets** |
| H2 | UE3's **split-screen** path renders several views per frame, each `ULocalPlayer` with its own `FSceneViewState` (occlusion and temporal history) | Synchronized stereo with isolated per-eye state, the problem Overlord solved by hand | Ghidra: split-screen code in GameViewportClient / scene renderer; check whether ME stripped it | pending |
| H3 | AnimTree **`SkelControlLimb`** (two-bone IK, `EffectorLocation`, `JointTargetLocation`) is available | Engine-native arm IK driven by controller poses | LEX: Shepard/player AnimTree; class list includes SkelControlLimb | pending |
| H4 | `SkelControlLookAt` / `SkelControlSingleBone` / `SkelControl_CCD_IK` / `SkelControlFootPlacement` exist | Torso and head lean; optional foot IK | LEX class browser | pending |
| H5 | Shepard's head is a **separate skeletal mesh component** (custom face) | Trivially hide the head in first person | LEX: BioPawn / player pawn components | pending |
| H6 | `SkeletalMeshComponent.HideBoneByName` exists | Fallback for hiding the head or helmet | LEX | pending |
| H7 | `GFxMoviePlayer` has a **`RenderTexture`** (TextureRenderTarget2D) property | Redirect HUD and menus to a render target, then an OpenXR layer | LEX: GFxUI package; ME's movie or handler subclasses | pending |
| H8 | Weapons fire along `GetAdjustedAim` from `GetPhysicalFireStartLoc` (script) | Swap the aim source to the controller pose | LEX: `SFXWeapon` / `BioWeapon` in each game | pending |
| H9 | Post effects (motion blur, DOF, grain, vignette) are disable-able via ini or console | VR comfort | LEX console / ini | pending |
| H10 | LExSDKv2 exposes `UObject::ProcessEvent` and the class layouts for all three games | Base for both script-call and hook plumbing | Read `reference/LExASIs/External/LExSDKv2` | **Confirmed (code reading).** Per-game generated SDKs; ProcessEvent at LE1 RVA `0x12dd20` (vtable slot 0x250); hardcoded RVAs for exe build **2.0.0.48602**. Must match our installed exe |
| H11 | Conversations can be detected natively on LE1 | Cinema-screen switching | ConvoSniffer source | **Confirmed (code reading).** Pattern hooks on `UBioConversation::StartConversation/EndConversation` (LE1) |

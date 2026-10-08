# Milestone log

## M0: plugin loads and hooks the engine (LE1). **Passed 2026-10-07**

- **Setup:**
  - Loader: ME3Tweaks Mod Manager's ASI loader (`bink2w64.dll` proxy 426,984 bytes plus
    `bink2w64_original.dll`).
  - Plugin `LE1MEVR.asi` 0.0.1 (commit `857079c`), deployed with `tools/deploy-asi.ps1`.
  - Game launched from Steam, LE1 vanilla apart from the loader.
- **Attach** (from `Win64/Logs/LE1MEVR.log`): about 13 s after process start. The version check
  passed, all globals resolved and the tick hook was installed within 43 ms:
  ```
  [20:57:39.265] MEVR 0.0.1 attaching to LE1
  [20:57:39.266] game version 2.0.0.48602 OK
  [20:57:39.266] resolved GMalloc / GObjObjects / SFXName pools / SFXName init / GEngine / GWorld
  [20:57:39.266] resolved UGameEngine::Tick at 0x7ff62a92a090   (image base + 0x3ca090)
  [20:57:39.308] hooked UGameEngine::Tick
  ```
- **Heartbeat:** every 5.00 s for 67+ beats (more than 5 minutes), with no errors.
  - GWorld changed from `0x2423f0cc060` (menu) to `0x24243331940` (in level), so we can detect
    level changes.
  - Object count: about 140k in the menu, about 166k in game.
- **Frame rate:** about **190 fps uncapped at the menu, 60 fps locked in game** (dips to 52 fps).
  VR needs 72/80/90 Hz, so the in-game cap (VSync or UE3 `bSmoothFrameRate`/`MaxSmoothedFrameRate`)
  must be lifted. **[verify]** which one.
- The loader writes its own log to `Win64/Logs/bink2w64_proxy.log`, not to `Win64/` as
  research/09 assumed.

## M1: plugin controls the game camera (LE1). **Passed 2026-10-07**

- **Dev loop:** a hot-reload host (`LE1MEVR.asi` 0.1.0) plus the mod DLL
  (`build/Release/LE1MEVR_mod.dll`, via `LE1MEVR.modpath`).
  - **First hot reload in a running game:** at 21:19:57 the host saw the new build and
    swapped it in.
  - Loaded generation 2. From build finished to mod live: under 10 s.
  - No restart needed.
- **First attempt failed safely:** at attach, `Engine.PlayerController.GetPlayerViewPoint` didn't
  exist yet, because the script packages load after the DRM wait.
  - Fix: retry the script-level setup every 1 s from the tick. The ProcessEvent hook is
    installed only after the handlers are registered.
- **Camera functions found:**
  - `Engine.Controller` / `PlayerController` / `AIController` / `SFXGame.BioAiController.GetPlayerViewPoint`
  - `Engine.Camera.UpdateCamera`, `Engine.Camera.GetCameraViewPoint`
  - `Engine.Actor` / `SVehicle.CalcCamera`
- **Call rates in game:**
  - `PlayerController.GetPlayerViewPoint`: about **10,000/s, roughly 43 per frame**. Many
    systems query the player view point, not just rendering.
  - `Camera.UpdateCamera`: **once per frame**.
  - The others: 0.
- **Test:** the backslash key toggles a ±30° yaw sine sweep (4 s period), added to
  `GetPlayerViewPoint`'s `out_Rotation` after the call (ProcessEvent post-handler, matched by
  cached `UFunction*`).
  - **The user confirmed the view sways left and right.**
  - Log: 12,282 and 43,067 adjusted views per 5 s window while on.
- **F9 note:** F9 is bound in game, so don't use F-keys for dev hotkeys. Backtick opens the UE
  console.
- **Frame rate this session:** about 230–240 fps in the heartbeat, versus the 60 fps lock seen
  in M0. Likely a different game state (menu or pause?) or the VSync setting. Still open (H14).

### Implications for VR

- `GetPlayerViewPoint` is shared by everything that asks "where is the player looking", so
  adding head rotation there probably also moves **aim**, AI perception, audio and so on. For VR
  the **render view** should come from `ULocalPlayer::CalcSceneView` (still needs locating in
  LE1), keeping gameplay aim on the controller.
- `Camera.UpdateCamera` (once per frame) is a clean per-frame point for writing the base
  camera pose (`CameraCache.POV`).

## M2: game mirrored onto a virtual screen in the headset. In progress

### Attempt 1 (2026-10-07, 21:31–21:35)

- **Present hook:** host-owned `IDXGISwapChain::Present` hook via SPI, found through a
  throwaway device and installed at `0x7ffc16bd9000` (in dxgi.dll).
- **Blit failed first:** `CreateDeferredContext failed`. The game's D3D11 device is created with
  **flags `0x1` (`D3D11_CREATE_DEVICE_SINGLETHREADED`)**, which forbids deferred contexts.
  - Fix, hot-reloaded in game: draw on the immediate context inside our own
    `ID3DDeviceContextState` (`SwapDeviceContextState` in and back out).
- **After the hot reload:**
  - **Runtime and headset:** OpenXR runtime **Oculus 1.208.0**, system **Meta Quest Pro** (max
    16 layers). The session reached **FOCUSED** and the user saw the full-screen app view.
  - **Game device:** feature level 0xb000 (11_0).
  - **Backbuffer:** **2490×1401 `R16G16B16A16_FLOAT`**. That's FP16 scRGB (linear), so there's
    no sRGB decode. **[verify]** whether highlights above 1.0 clip, and whether HDR is on.
  - **Frame rate:** about 88–104 fps while the session ran, paced by the headset (Quest Pro
    90 Hz). In-game it was 240 fps uncapped before XR.
- **Bugs found:**
  1. **Input didn't reach the game.** The game window didn't have Windows focus (UE3 ignores
     keyboard, mouse and gamepad without it).
     - Fix: when the XR session becomes FOCUSED, bring the game window to the foreground
       (`AttachThreadInput` + `SetForegroundWindow`).
  2. **Quit from the Meta menu kept relaunching VR.** EXITING was treated like LOSS_PENDING and
     the session was recreated after 2 s, twice.
     - Fix: EXITING means the user quit. Tear XR down and stay off until `]` is pressed in game.
  3. **Crash on game exit.** The user closed the game from the desktop with the session live.
     Fail-fast `0xc0000409` in `RuntimeIPCServiceClient_64.dll` (Oculus) during process
     teardown.
     - Fix: the host hooks `ExitProcess` and shuts the mod (and XR) down before Windows unloads
       DLLs.

### Attempt 2 (2026-10-07, 21:41–21:44): virtual screen works

- **Game in the headset:** the user saw the game rendered in the headset.
- **`]` toggles:** VR went OFF and ON three times cleanly; each XR session closed in about 70 ms.
- **Quit from the Meta menu:** EXITING, then VR off, and it stayed off until `]`. No relaunch
  loop.
- **Not a bug:** head movement doesn't change the game view. That's expected for M2: the panel
  is fixed in LOCAL space, and head tracking comes in M3.
- **Earlier crash at 21:41:08:** the run that started 21:40:16 crashed 52 s in.
  - **Dump:** `%LOCALAPPDATA%\CrashDumps\MassEffect1.exe.32192.dmp`, triaged with
    `tools/minidump_scan.py`.
  - **What it shows:** access violation writing `0x24` in `ntdll+0x1a12d`. The faulting worker
    thread's whole stack is MassEffect1.exe plus `ws2_32`/`mswsock` (networking). No MEVR frames,
    and no Oculus DLLs loaded.
  - **Open question:** whether the user was closing the game at that moment. If so, our
    ExitProcess hook (which closes XR before the real exit) may widen an existing exit-time race
    in the game's network thread.
  - **Mitigation added:** the log now keeps the previous run as `LE1MEVR.prev.log` and records
    the pid, so the next incident keeps its evidence.

### Attempt 3: Steam Link

- The user switched to Steam Link, so SteamVR needs to be the active OpenXR runtime. The mod is
  runtime-agnostic. Fix: `]` off/on now always recreates the OpenXR instance, so a runtime
  switch is picked up without restarting.
- Worked under SteamVR. The user's next request was head tracking (M3).

## M3: head tracking drives the camera (mono). In progress

Design (2026-10-07):
- **Render thread (Present):** submit frame N, then immediately `xrWaitFrame` + `xrBeginFrame`
  for N+1 and locate the head (VIEW in LOCAL) at `predictedDisplayTime + predictedDisplayPeriod`.
  The extra period is because UE3 renders a frame behind the game thread. The pose is published
  through `core/vr/ViewSync`.
- **Game thread:**
  - At `Engine.Camera.UpdateCamera` (once per frame), read the latest pose and convert it to a
    UE3 rotator (`engine/XrToUnreal`).
  - Queue `{pose, CameraCache.POV.FOV}` for the render thread.
  - Every `PlayerController.GetPlayerViewPoint` that frame gets `yaw = game yaw + head yaw`,
    `pitch = head pitch`, `roll = head roll`.
- **Submission:** a **projection layer**. Both eyes show the same image (mono), posed at the
  queued head pose with the game's FOV (horizontal, symmetric; vertical from the aspect). The
  runtime's reprojection keeps it world-stable. Queue order (FIFO, cap 3) pairs images with
  poses, since UE3 ticks once per Present.
- **Modes:** `[` switches between head-tracked and screen (M2). `]` turns VR on and off.
- **Known limitations:**
  - Aim follows the head (weapons use GetPlayerViewPoint too).
  - Only the game's FOV is covered (black outside).
  - No positional tracking yet.
  - In vanilla third person the camera pivots in place behind Shepard.
- **[verify]:** whether ME's FOV is horizontal (UE3 default) or aspect-constrained. If the scale
  is wrong, the world will "swim" when you turn your head.

### M3 test 1 (2026-10-07, 22:06–22:17, SteamVR 2.17.10 via Steam Link)

- **Head poses reach the game:** about 90 frames tagged per second, and the view function is
  adjusted 3–48 times per frame. The game's FOV is **70.0** (horizontal).
- **User report:**
  1. The game's main screen still showed over the head-tracked image.
  2. The two eyes' images were badly misaligned.
- **Diagnosis:** SteamVR's **Theater Screen** (the SteamVR 2.1+ successor to Desktop Game
  Theatre) shows any non-VR Steam game's window on a virtual screen while SteamVR runs. That
  gives two layers: SteamVR's theater screen at a finite distance, plus our mono projection at
  infinity. The doubled, misaligned look is most likely those two overlapping.
- **Fix (user setting):** SteamVR Settings → Show Advanced Settings → Dashboard → turn off
  presenting non-VR apps on the Theater Screen at launch.
  - My first instruction pointed at the per-game Steam checkbox. That was removed in SteamVR 2.1
    (late 2023), and the user found it missing.
  - Sources: [SteamVR 2.1 announcement](https://forum.fractalcoffee.duckdns.org/viewtopic.php?p=3856),
    [Steam discussion](https://steamcommunity.com/app/250820/discussions/0/4036978698796115769).
  - Retest pending.

### M3 test 2: **passed (2026-10-07)**

- **Setup:** Theater Screen off.
- **Result:** only the mod's output shows. Head rotation drives the game camera and the world
  stays fixed in space; new scenery enters the box as you turn ("option A").
  - This also **confirms ME's camera FOV is horizontal** (UE3 default); otherwise the world
    would swim.
- **Expected limitations at this stage:**
  - Mono (no depth).
  - A 70° box that follows the head.
  - Walking direction comes from the stick or mouse, not the head.

### M3.1: render at the headset's FOV (built, untested)

- The render thread runs `xrLocateViews` and computes the symmetric horizontal FOV that covers
  both eyes (outermost left and right edges), clamped to 60–150°, then publishes it.
- The game thread sets `CameraCache.POV.FOV` in UpdateCamera and overrides the
  `PlayerController.GetFOVAngle` event's return. Frames are tagged with the FOV they render at.
- Vertical coverage is still limited by the 16:9 backbuffer (about 75° vertical at about 108°
  horizontal). Filling it needs a squarer render size later.

### M3.1 test: FOV works; micro-stutter found

- **FOV:** the user confirmed the image fills most of the view and the world stays put.
  - The headset FOV override is honored, and our horizontal-FOV tagging matches what the game
    renders.
- **Problem:** "y-axis movement incredibly jagged, very bad micro-stutters."
- **Diagnosis** (vrframework guide 07): the image/pose pairing *assumed* one tick per Present
  in FIFO order. Any slip means every later image carries a neighboring frame's pose, which
  shows as judder during head motion.
- **Fix:**
  - **Lockstep.** After each engine tick, the game thread waits (30 ms timeout) until that
    frame has presented. At Present, the newest queued view is exactly the frame being shown.
    The head is predicted for the very next display time (the old +1 period is gone).
  - **Counters** in every heartbeat: presents and render thread id, present interval min/max,
    queue depth histogram (should be all depth 1), lockstep waits, average wait, timeouts.
- Retest pending.

### M3.2: render-only head rotation (built and unit-tested; untested in game)

Prior art ([research/13](../research/13-existing-me-vr-mods.md)) points to three suspects for
the vertical judder:
- our pitch going through the third-person camera, whose boom and smoothing fight it;
- VSync tying Present to the desktop monitor;
- motion blur on head turns.

Built 2026-10-07 while the user was away from VR:

- **CalcSceneView hook** (`src/engine/SceneView.*`, LE1 `0x4C5300`). The address is checked
  against `.pdata` at runtime, and the decrypted entry bytes are logged. Only the call from
  `FViewportClient::Draw` (return address `0x4C7A4E`) counts as the frame's player view.
- **Render path (new default).** GetPlayerViewPoint/GetFOVAngle are changed *only inside* that
  call. UE3 builds the matrices itself, and the camera, aim and AI never see the head. The head
  is sampled and the frame tagged at the start of the view (as late as possible on the game
  thread). Self-checks in the heartbeat:
  - if no GetPlayerViewPoint call happens inside the view, it falls back to the camera path;
  - if no GetFOVAngle call does, the FOV goes through the camera cache instead.
- **`\` cycles render / camera (old M3) / off** for A/B testing.
- **Desktop head simulator.** `'` cycles sweep / nod / off and is used while VR isn't
  head-tracked, so the render path can be checked on the monitor.
- **Offline tests:** `ctest -C Release --test-dir build` covers the rotation conversion and
  the simulator.
- **`tools/vr-game-settings.ps1`.** Applied to LE1 (backup kept): VSync off, motion blur off,
  film grain off, HDR to SDR. `-Restore` undoes it.
- **Known behaviour change:** on the render path, weapons aim along the game camera, not the
  head, so the crosshair is no longer at the view centre once you turn your head. Head or
  controller aim comes later.

Test plan: [next-vr-session.md](next-vr-session.md).

### M3.3 / M4 stage 1 / M6 groundwork (built 2026-10-07, untested in game)

Built while the user was away from VR, behind toggles so the M3.2 A/B test stays clean:

- **Motion controllers as a gamepad (on).**
  - `core/xr/Input`: an OpenXR action set with Touch, Index and simple-controller bindings,
    synced each frame on the render thread. It also locates grip and aim poses, for later.
  - `game/GamepadBridge`: hooks `XInputGetState` (and ordinal 100) in the XInput DLLs the game
    loaded. Pad 0 becomes the controllers merged with any real pad.
  - Mapping (`game/GamepadMapping`, unit-tested):

    | Touch | Xbox |
    |-------|------|
    | Sticks | Sticks |
    | Triggers | Triggers |
    | Grips | LB / RB (with hysteresis) |
    | A / B / X / Y | A / B / X / Y |
    | Stick clicks | LS / RS |
    | Left menu | Start |
    | Left grip + menu | Back |

  - On the render path the left stick is turned by head yaw, so forward is where you look.
  - No D-pad yet.
- **Alternate-eye stereo (`;`, off).** Frames alternate eyes at ±half the runtime's IPD along
  the view's right axis. Each frame is tagged with its eye's pose (`OffsetRight`). The output
  keeps a swapchain and a held image per eye and submits both every frame, so a stale eye is
  reprojected from its own pose.
- **6DOF (`/`, off).** Head position (LOCAL space) offsets the rendered view, rotated into the
  game yaw. 1 m = 100 units **[verify]**.
- **Reflection judder fix (on).** Every 3 s, finds live `SceneCaptureReflectComponent`s and calls
  `SetFrameRate(1000)` through ProcessEvent, then reads `FrameRate` back. It disables itself if
  the call has no effect (open question: does LE's ProcessEvent run natives?).

**Independent review** (a subagent read the whole 151c02e..9402a30 diff). No crash paths, but
real issues, all fixed in `6a5f283`:
- **View pairing.** After a lockstep timeout the newest view could be given to the previous
  image (a one-frame pose error; eyes swapped in stereo).
  - Now each game Present consumes exactly one view, oldest first, capped at UE3's one-frame
    lead.
  - Drift recovery: three timeouts in a row drop a stale view.
- **No player view, no image.** If the player-view call site didn't match, VR would have gone
  black. Now any tick without a player view is still tagged (head-locked), as before.
- **FOV.** The FOV tag could disagree with the rendered FOV. It's now snapshotted once per view.
- **Stick rotation** was applied everywhere. Now it applies only on foot (SFXGameModeManager
  mode 0).
- **Reflection scan.** A full GObjects walk in one tick could itself stutter. It now runs in
  20k slices.
- **Self-checks** could misfire at startup. They now count only once the hooks are live, and
  need two windows in a row.
- **Smaller fixes:** mono fallback when eye resources fail; button-only controller input counts
  as active; INI line endings.

**Second review pass** (on the fixes, plus my own walk-through of the pairing logic). Fixed in
`15ddd0a`:
- **No-view ticks no longer queue.** They publish a fallback pose that's never waited on. A late
  push after its Present would have cost a 100 ms wait per tick.
- **One queue entry per tick.** A second view replaces the first.
- **Lockstep timeout 30 → 100 ms**, so an ordinary hitch can't trigger drift recovery.
- **No waits while frames aren't wanted:** while the headset isn't showing frames, or after a
  timeout with no Present since.
- **Stick yaw** cleared on ticks without a player view.
- **Stereo forced to mono** when eye resources failed.
- **Code dump** checks page protections before reading.
- **Pairing-queue tests:** `TestViewSync`.
- **Accepted risk:** three consecutive render frames over 100 ms (heavy loading) can drop a valid
  view. That leaves views one frame ahead until the game thread becomes the slower side. A real
  fix needs a frame identity from the render thread.

## M6 groundwork: VRIK and hand aim (built 2026-10-08, untested in game)

The user chose to keep our own project (VRIK isn't in Halcyon's scope). Built:
- `core/ik`: unit-tested body, shoulder and arm math.
- `game/SkeletonProbe`: logs Shepard's skeleton and existing IK controls on the next launch.
- Hand aim (`,`), through a new `UObject::CallFunction` hook.

Plan: [research/14](../research/14-vrik.md).

Also built (2026-10-08, user away):
- **Route 1 arm IK driver** (`.`, off by default). Superseded the same day by route 2 (below).
- **`tools/xref.py`**, for the code dump.
- **All three games' plugins build.** Per-game spellings live in `engine/GameAdapter.hpp`.
  LE2/LE3 CalcSceneView and Draw addresses are in place; the gameplay call site is taken from
  inside Draw and logged so it can be pinned.

Controls simplified (2026-10-08, the user's call): `]` alone gives the full VR experience. Once a
headset session runs, stereo, 6DOF, hand aim and arm IK all come on; VR off turns them off. The
per-feature keys stay as in-session overrides for A/B tests and crash isolation.

VRIK route 2 (2026-10-08, built and unit-tested, not yet run in game):
- **Why the bone queries failed:** natives with an `out array<>` parameter (`GetBoneNames`) can't
  return data through ProcessEvent in LE1 (`EX_NativeParm` with a null result). Bones are now read
  from memory (`RefSkeleton`, `SpaceBases`, which is FBoneAtom, not the SDK's FMatrix), checked
  against `GetBoneLocation` before any write.
- **Arm writeback:** a post-hook on `UpdateSkelPose` (LE1 `0x592440`) rewrites the arm chains with
  `core/ik/PoseArm`, carrying everything below the upper arm rigidly.
- **Desktop test:** `.` with no VR session holds the right hand in front of the chest
  (next-vr-session.md, A0).

Known issue in our own renderer (LE2/LE3, or `MEVR_HALCYON=OFF`), from the 2026-10-08 review:
with the lockstep off (now the default) the drift recovery never runs, so a view whose frame never
presents can stay at the front of the queue and pair every later image with the previous tick's
view. Fix before using our renderer again: tag views with the engine frame and drop older entries
at Present.

### Still to do

- Retest with the fixes. Check image correctness (colors and brightness vs the monitor).
- ~~Find LE1 `ULocalPlayer::CalcSceneView`~~: `0x4C5300` (research/13).

## Controller aim with Halcyon's renderer (built 2026-10-08, untested in game)

The user's report with Halcyon's renderer: aiming followed the head. Now, on foot with motion
controllers, the right controller aims and the head only looks; a plain gamepad keeps Halcyon's
head aim.
- Halcyon publishes its view each frame (`[MEVR]` call in RunFrame); HostServices (v4 since the
  merge; v3 had two layouts, so the mod reads neither from a v3 host) hands it to the mod with
  `setHandAim`. `game/view_anchor` places it on the game camera
  (`engine::FromCameraView`, unit-tested): the one tracking-to-world mapping, for arm IK too.
- Hand aim turns on the first time the right controller aims. While it does, Halcyon's on-foot
  combat head aim stays off (`[MEVR]` gate). The Mako keeps head aim. `,` switches between them.
- Known: the HUD crosshair stays at the view's center (where the head looks). Shots take the
  controller's direction but start at the game's StartFireLoc; the log measures the offset.

Known issues to verify in game (from the 2026-10-08 review of the merge):
- **ControlRotation follows only the stick while hand aim is on.** With hand aim on, Halcyon's
  combat head aim is off, so ControlRotation no longer follows the head. Shots are redirected
  through `GetAdjustedAimFor`, but anything that reads ControlRotation directly (power
  targeting, the native reticle, the torso aim animation) follows neither the head nor the
  controller. Also, hand aim turns on whenever the right controller is merely tracked (lying on
  the desk counts), so head aim goes away even when the player isn't using the controller.
- **The third-person camera can jump when controller aim takes over.** `ReleaseHeadAim` removes
  the yaw Halcyon injected for head aim in one frame, so the camera snaps back by however far
  the head had turned it.

Test: [next-vr-session](next-vr-session.md), first section.

### Review fixes (2026-10-08, built and unit-tested, untested in game)

- **Desktop arm test** is decided every tick, never latched: our renderer with no VR session,
  or Halcyon's before any headset session has shown up (no view or controller sample since the
  host started). With a headset session, `.` while the view is momentarily gone (Insert menu,
  flat conversation, tracking blip) no longer switches to the test for the rest of the session.
- **Hands measured from the tracking origin** (the head at start or recenter), placed at
  Shepard's head bone: leaning no longer moves the virtual hands backwards.
- **Bone writer** also checks the mesh's owner, GWorld and the request's age, so a level load
  can't send the arm write to another component at the same address.
- **Hooks:** every original is written before its detour goes live (LEBinkProxy's SPI writes it
  through its out-parameter before enabling; LESDK only returned it afterwards). Unhook results
  are checked. A hook that couldn't be removed, or a call still inside one after a bounded wait
  (ProcessEvent callback 500 ms, UpdateSkelPose 200 ms), makes the host **pin** the old mod DLL
  instead of unloading it (logged as an error: restart the game when convenient). At exit
  nothing waits and the DLL stays mapped. HostServices v5 carries `processExiting` and
  `keepLoaded`; an older host still unloads (the mod logs it).
- The mod tells Halcyon it aims only when it actually can (CallFunction hook in, and the host's
  controllers and view readable: state sizes match).

## The gun decides: barrel aim, muzzle shots, aim ray (built 2026-10-08, untested in game)

The user's report: holding the controller normally, the gun pointed about 90 degrees up, and
shots didn't match the gun model. Now ([research/14](../research/14-vrik.md), "The gun in the
hand"):
- `game/weapon` (LE1) finds the held `BioWeaponRanged`, its muzzle socket and barrel, verifies the
  muzzle against `GetMuzzleLoc` once per weapon, and measures how the gun sits in the right hand.
- Arm IK turns the right hand so the barrel follows the right controller's aim ray, upright with
  it (`ik::HandForBarrel`). The `[Hands]` trims only matter without a gun.
- Hand aim: while the gun follows the controller, shots go along the barrel and start at the
  muzzle (`Pawn.GetWeaponStartTraceLocation`'s answer rewritten); otherwise the controller's ray as
  before.
- The aim ray (muzzle and barrel in tracking space) goes to the host every tick
  (`HostServices::setAimRay`, v6) for the renderer's reticle.
- A follow check logs once per weapon whether the gun follows the written hand in the same frame.

Test: [next-vr-session](next-vr-session.md), step 6.

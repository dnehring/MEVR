# Development approach (since 2026-10-21)

How we build the VR port. It was set after reviewing how to make progress faster: the slow part was
never writing code, it was the loop of "the user tests in the headset, describes what they saw,
Claude reads logs, guesses, changes, and asks for another test". The approach shortens that loop
and keeps our effort on what only we provide.

## 1. Build on Halcyon, own the VRIK

- **Halcyon's renderer is the core** (stereo, 6DOF head, first person, menu, HUD). It's GPL-3.0
  and works.
- **Our work is VRIK:** controller aim, arm and torso IK, the gun in the hand, the two-handed grip
  with aimed fire from posture, the reticle along the gun, calibration (hand angles, arm length,
  camera at Shepard's head, life-size movement).
- **Touch Halcyon's code only** to fix its bugs or to add a seam VRIK needs, marked `[MEVR]`
  and declared in `src/halcyon/le1/mevr_bridge.h`. Compare with upstream before changing any of
  its settings (CLAUDE.md, "Renderer (LE1)").
- **Offer its bug fixes back upstream** (`notes/upstream-halcyon/`), so we carry less drift. Ask
  the user before sending anything.
- **Learn from Overlord** (MW2 VR, GPL-3.0) for hands and weapons: support grip, two-hand aim,
  arm stretch, the Touch grip reference. Read its docs before designing a hand or weapon feature.

## 2. Let Claude see the game: the test loop

`src/plugin/TestLoop.cpp`, CLAUDE.md "Test loop". The aim is that Claude can verify a VRIK change
without the user wearing the headset or describing what they see.

- **Eye snapshots:**
  - What it saves: PNGs of what each eye shows (world, HUD panel, reticle, menu), plus a JSON of
    the head and controller poses and the arm IK, weapon and grip state.
  - Triggers: End, both grips + left stick click, the VR hands tab, or
    `%LOCALAPPDATA%\MEVR\snapshot.request` (`tools/snapshot.ps1`), which Claude can write while
    the user plays.
- **Pose recording and replay:**
  - Recording: Home, both grips + right stick click, or `record.request`. It records head and
    controller poses (and inputs).
  - Replay: `replay.request` / `tools/replay.ps1` feeds a recording back in as live tracking.
    Inputs replay only with `inputs=1`, so a replay doesn't fire weapons.
  - Typical use: the user records a short session once (drawing a rifle, gripping it, aiming
    around); Claude then replays it after each change and snapshots the result.
  - Limit: the headset must stay awake, since Halcyon renders nothing while it sleeps.
- **Logs stay the ground truth for numbers.** Every VRIK module logs its decisions once, plus
  heartbeat counters; snapshots show what those numbers look like.

## 3. Read the engine with tools, not by hand

- **Ghidra:**
  - `tools/ghidra/`, research/17. A local project (`%LOCALAPPDATA%\MEVR\ghidra`, project LE1)
    built from the decrypted `.text`, which the host now dumps at attach, before any hook patches
    it.
  - It is named from our known addresses, LExSDKv2's tables and the game's own native function
    table (`[Debug] dumpNatives = 1` writes every native UFunction's address once).
  - `tools/ghidra/decompile.ps1` decompiles by address or name, with callers and callees, and can
    record names and comments.
  - **GhidraMCP** (LaurieWired, Apache-2.0, release 2.0, pinned to the downloaded wheel) runs
    headless on the same project, registered locally as the `ghidra` MCP server. It and
    `decompile.ps1` can't have the project open at the same time.
- **Scripts:** the game's UnrealScript is readable with Legendary Explorer. Export the classes we
  need (BioPlayerController, SFXCameraMode_*, BioWeaponRanged, BioPawn) when a question is
  script-side.
- **Local data stays local:** dumps, spliced exes, the Ghidra project and exported game scripts
  are never committed or shared. Research notes summarize behaviour, addresses and offsets in our
  own words.

## 4. Work in parallel, verify before merging

- Independent pieces go to background agents in their own git worktrees, with the full context
  in the prompt (CLAUDE.md rules, files, prior art).
- Claude merges each one and builds all three trees: `build` (+ tests), `build-mono`,
  `build-trilogy`. It runs `ctest`, deploys, and records what to test in
  `notes/next-vr-session.md`.
- Host changes need the game closed and a redeploy; mod changes hot-reload in game.
- Decisions and open items go to `notes/todo-vrik.md`; findings with sources go to `research/`.
  Claims not yet seen in the headset are marked **[verify]**.

## Where things stand (2026-10-21)

- **Verified in the headset:** stereo with Halcyon's installer settings, controller aim, the
  gun's muzzle and barrel, arm IK with the torso, the left-trigger camera alignment.
- **Built, not yet tried in the headset:**
  - the two-handed grip, with aimed fire from posture (both hands and the gun at the shoulder;
    the left trigger is no longer aim);
  - the test loop;
  - the natives dump and the clean code dump.
- **Next:**
  1. The user records one short session.
  2. Claude rebuilds the Ghidra project from the clean dump.
  3. The research on why Halcyon's camera follows the head bone we write, whether the native
     left-hand IK runs, and where aimed-fire accuracy comes from (research/18, in progress).

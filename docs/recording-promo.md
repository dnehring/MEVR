# Recording promotional stills and video (LE1)

The spectator camera ([research/49](../research/49-spectator-camera.md)) renders a normal 16:9 picture of the VR
experience: the world from a smoothed head pose or a camera you place, with the HUD panel, the hand reticle, the sights,
the wrist and gun pieces, the interaction label and the Mako's controls drawn where the eyes see them. It replaces
Halcyon's one-eye mirror in the game's desktop window, so any screen recorder records it. It costs a third scene render a
frame, so turn it on only to record.

## Turn it on

In `Binaries\Win64\ASI\LE1MEVR.ini`:

```ini
[Spectator]
enabled = 1
width = 1920        ; 3840 and 2160 for 4K
height = 1080
fovDeg = 85         ; horizontal field of view
camera = head       ; head | free
smoothS = 0.25      ; the head camera's smoothing, seconds (0: the raw head)
```

The mod re-reads the file within a second. Or, without touching the file, from a shell in the repository:

```powershell
tools/console.ps1 "mevr spectator on"
```

`mevr spectator off` turns it off again (the console's on/off holds until `enabled` in the file changes).

For a smooth window, set `mirrorPresentEvery = 1` in `ASI\MELEVR.ini` (our deploy uses 2, which shows every other frame:
45 fps at 90 Hz). Presenting every frame costs frame rate (Halcyon's `[MIRRORTHROTTLE]` note), so put it back after recording.

## Record with OBS

- **Game Capture** of `MassEffect1.exe` records the backbuffer at its full size. The backbuffer is the per-eye render size
  (about 2824x2880 on the PS VR2), so the 16:9 picture sits in a black letterbox: crop it in OBS (Alt-drag the source's
  edges, or the source's Crop filter) to the picture's rows, and set the canvas to 1920x1080 or 3840x2160.
- **Window Capture** works too but records the window at its on-screen size, which is usually smaller than the backbuffer.
- Record at 30 or 60 fps. Recording on the same GPU costs encoder time; NVENC is the cheapest.

## Stills

```powershell
tools/snapshot.ps1 -Spectator
```

writes `%LOCALAPPDATA%\MEVR\snapshots\<stamp>-spectator.png` (the picture at its render size) beside the eye snapshots.
`-Spectator` turns the camera on through the console first if it is off.

## Camera placement

| Command | What it does |
|---|---|
| `mevr spectator head` | The smoothed head camera (the default). |
| `mevr spectator at X Y Z YAW PITCH` | A fixed camera in the world: units and degrees. `mevr loc` prints the pawn's place to start from. |
| `mevr spectator at` | Freezes the camera where it is now. |
| `mevr spectator follow RIGHT FORWARD UP [LOOKUP]` | Follows the pawn from that offset in metres in its heading, looking at the pawn LOOKUP metres above its origin (1 by default). `follow -0.6 2.2 0.4` films Shepard from the front left. |
| `mevr spectator loc` | Prints where the camera is. |
| `mevr spectator status` | What it is doing. |

Run each through `tools/console.ps1 "<command>"`.

## Filming a session from outside

1. Record the session in the headset: Home (game focused) or `tools/record.ps1 -Seconds 60`.
2. Later, replay it with its inputs, so the hands, the gun and the holster draws repeat: `tools/replay.ps1 -Inputs`.
3. Place the camera: `tools/console.ps1 "mevr spectator follow -0.8 1.6 0.3"` (hands on the gun, a holster draw, the vent),
   or `mevr spectator at ...` beside the Mako's seat for the cockpit.
4. Record the window. `tools/replay.ps1 -Stop` ends the replay.

The free camera shows Shepard with the head. The head camera hides the head meshes for its own picture's sake; the eyes never
see them anyway.

# Rendering and stereo

* [Motion blur smears each eye its own way, and keeps coming back](motion-blur-in-stereo.md) - UE3's motion blur takes its velocity from the other eye's view under Halcyon's stereo; bitten 2026-10-07, 2026-10-23 and 2026-10-28, each time under a different report.
* [BIOEngine.ini's SystemSettings win, the game rewrites them, and a level load re-arms them](system-settings-precedence.md) - DynamicShadows and MotionBlur in GamerSettings.ini are overridden; a loaded level renders with the old values; `scale set` to the stored value changes nothing.
* [The engine's dynamic vertex and index rings wrap inside a recorded frame](dynamic-ring-wraps-inside-the-recorded-tail.md) - A DISCARD map inside the recorded tail makes the replay draw from garbage: the strobing right eye.
* [d3d11 swaps per-context vtable entries, so a vtable or single-body hook misses calls](d3d11-per-context-vtable.md) - Draw, clear, copy and dispatch entries move between variants as the context's state changes.

# Engine and threading

* [Never run UnrealScript off the game thread](process-event-off-the-game-thread.md) - ProcessEvent from the Present or render thread races the script VM's process-wide globals; the 2026-10-23 crash.
* [A fault inside a render-thread hook hangs the game without a crash](render-thread-fault-hangs-the-game.md) - The engine's own handler ends the render thread, the game thread waits forever, and only first-chance log lines remain.
* [LExSDKv2's generated function wrappers corrupt the UFunction's flags](lexsdk-function-wrappers.md) - `FunctionFlags |= ~0x400` (meant `&=`) sets every flag for good; call ProcessEvent yourself.
* [Never write Shepard's Camera or God bones](camera-and-god-bones.md) - LE1's camera hangs off them through SpaceBases: the view would follow the body.
* [ProcessEvent skips every native that has an opcode of its own](process-event-skips-opcode-natives.md) - Trace never hit, Destroy never destroyed, SetLocation and SetOwner did nothing; bitten 2026-10-24, 2026-10-31 and 2026-11-03 under four names.

# Launching and measuring

* [SteamVR's room setup kills the game at the title on the fake headset](steamvr-room-setup-kills-the-game.md) - An unregistered null-driver chaperone universe makes vrmonitor launch room setup and vrserver kill the running scene app 5 s later.
* [EA's launcher refuses to start the game from inside a job object](ea-launcher-in-a-job-object.md) - EASteamLauncher's breakaway CreateProcess is denied when our shell or the EA app itself sits in a job; Steam's own launch works (`tools/launch-game.ps1 -Steam`).
* [The LE console prints less than UE3's: what works and what is silent](le-console-quirks.md) - `scale set` and `set` take effect, `getall` and the streaming stats print nothing, `god`/`ghost`/`pause` toggle.

# Code and tooling

* [Settings code: one ini reader, no C-locale number functions, no path.string()](ini-numbers-paths-and-locale.md) - A decimal-comma locale breaks `strtod`, a Cyrillic folder throws from `path.string()`, and the host and the mod must read one file one way.
* [Windows PowerShell 5.1 turns a native command's stderr into errors](powershell-5-native-stderr.md) - git progress becomes a terminating error under `$ErrorActionPreference = 'Stop'`; check `$LASTEXITCODE`.
* [A `cd` in a Bash tool call moves the whole session](bash-cd-moves-the-session.md) - The working directory persists between calls, so every later relative path and tool resolves somewhere else; use absolute paths.

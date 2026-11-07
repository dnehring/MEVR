# Update Log

## 2026-10-30
* **Init**: Created the bundle ([Open Knowledge Format 0.2](https://okf.md/spec/)): this log, the root index, [How to add a gotcha](how-to-write.md), the [gotchas](gotchas/index.md) directory and `tools/check-okf.py` (ctest `mevr-okf`).
* **Create**: [Motion blur smears each eye its own way](gotchas/motion-blur-in-stereo.md), after it bit for the third time (2026-10-07 judder, 2026-10-23 shimmer, 2026-10-28 shifting assets), with [BIOEngine.ini's SystemSettings precedence](gotchas/system-settings-precedence.md) and [the LE console's silences](gotchas/le-console-quirks.md) it depends on.
* **Create**: the gotchas CLAUDE.md already carried, as concepts with their sources: [UnrealScript off the game thread](gotchas/process-event-off-the-game-thread.md), [a render-thread fault hangs the game](gotchas/render-thread-fault-hangs-the-game.md), [d3d11's per-context vtable](gotchas/d3d11-per-context-vtable.md), [the dynamic ring wraps inside the recorded tail](gotchas/dynamic-ring-wraps-inside-the-recorded-tail.md), [LExSDKv2's function wrappers](gotchas/lexsdk-function-wrappers.md), [the Camera and God bones](gotchas/camera-and-god-bones.md), [SteamVR's room setup kills the game](gotchas/steamvr-room-setup-kills-the-game.md), [EA's launcher in a job object](gotchas/ea-launcher-in-a-job-object.md), [ini numbers, paths and the C locale](gotchas/ini-numbers-paths-and-locale.md), [PowerShell 5.1's native stderr](gotchas/powershell-5-native-stderr.md), [a `cd` in a Bash tool call moves the session](gotchas/bash-cd-moves-the-session.md) (which bit while this bundle was being written).

## 2026-10-31
* **Update**: [EA's launcher in a job object](gotchas/ea-launcher-in-a-job-object.md) bit a third time during the unattended QA deploy; the Explorer, WMI and ShellWindows restarts of the EA app are recorded as routes that do not work.

## 2026-11-03
* **Create**: [ProcessEvent skips every native that has an opcode of its own](gotchas/process-event-skips-opcode-natives.md), after Actor.Trace, Destroy and SetOwner all did nothing in one session; the 2026-10-24 and 2026-10-31 SetLocation reports were the same mechanism.

## 2026-11-04
* **Create**: [Windows.h defines macros that eat ordinary identifiers](gotchas/windows-h-macros.md), after `SetProp` broke the monolithic build and `far` broke core/vr/WorldUi.cpp the same day.
* **Create**: [The Bash tool collapses doubled backslashes in a heredoc](gotchas/bash-heredoc-backslashes.md), after two inline edit scripts lost their `\n`.
* **Update**: [a `cd` in a Bash tool call moves the session](gotchas/bash-cd-moves-the-session.md) bit again (a `cd` into the SDK headers).

## 2026-11-06

- gotcha: [A library's ONNX Runtime session runs on every core and spins unless told otherwise](gotchas/recognizer-on-every-core.md) (the voice recognizer stalled the game; one thread now).
* **Create**: [The OpenXR runtime limits a session's swapchains](gotchas/openxr-swapchain-limit.md), after the calibration's targets and then the omni-tool's rail went missing in the same session, with ten other quads.
* **Update**: [a `cd` in a Bash tool call moves the session](gotchas/bash-cd-moves-the-session.md) and [the Bash tool collapses doubled backslashes](gotchas/bash-heredoc-backslashes.md) bit again.

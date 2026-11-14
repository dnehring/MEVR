<!-- SPDX-License-Identifier: MIT -->
# Run-bound ME2 window capture

After protected preparation, launch, exact process binding and the diagnostic
producer handshake, run from the repository root:

```powershell
python tools/qa-le2/capture.py <private-run-directory> --name native-start-001
```

This writes new `captures/native-start-001/frame.png` and `capture.json` inside
the run. Omit `--name` for a unique timestamp-based directory. Existing captures
are never replaced. Outputs are private evidence; do not commit them. Pillow is
the existing dependency used by `tools/flicker-grab.py`; NumPy is not required.

The helper reuses `control.binding` and `session.load_context` for the owned lock,
active game-bound phase, PID/start/path, executable hash and mapped diagnostic
host/mod identity. Restored sessions and changed identities are refused. It
selects a unique visible top-level window belonging to that exact PID, without
title matching. Ambiguous, minimized, empty or excessive windows are refused.
Window ownership/dimensions and session identity are checked before and after.
This is bounded evidence around the capture, not continuous process monitoring.

Capture uses the existing PrintWindow client-area approach, with checked 64-bit
ctypes declarations and GDI cleanup, including deselection before
[GetDIBits](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getdibits).
It changes only its private helper thread's
DPI context. It never focuses a window, sends input, launches a game, changes
settings or retries. [PrintWindow is synchronous](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-printwindow);
a 20-second timeout terminates only
the private capture helper and records failure. No restart or focus recovery is
attempted. The game may still return stale, black or incomplete GPU content even
when PrintWindow succeeds.

The receipt records run/lease and process/build identity, UTC timestamps, method,
window dimensions, `foregroundHwnd` (including for PrintWindow), image SHA-256
and one of `captured`, `suspect-black`,
`suspect-constant`, or `failed`. Black/constant images are kept for inspection;
they do not prove that the game rendered an empty scene. A partially written
image after failure is retained and hashed as failed evidence. A failure before
session admission creates no capture directory. Exit 0 means only a nonconstant
image was obtained with the checks passing; suspect/failure returns 2.
PrintWindow permits a stable background window. Its before/after window checks
refuse a changing foreground HWND, but a readable PrintWindow image does not
establish that the game was foreground. Older receipts may lack this field;
do not infer it from their pixels.

`gameplayPassed` is always false and `visualAcceptance` stays pending. A human
must inspect the PNG and associate its contents with the selected fixture and
same-run state evidence. This is desktop-client capture, not headset/stereo
proof. The tool does not promote a scenario or silently attach acceptance.

For menu baselines, obtain a fresh `observe-save-menu` result through the
[bound control client](control.md#observe-and-open-the-normal-load-menu), capture
the same stage, and retain both receipts. Check whether the image agrees with
the reported active panel and whether a modal or transition is still present.
Capture success does not establish that asynchronous save enumeration finished
or identify the selected save file. Native menu actions and their one-entry
allowances are separate from this helper; do not repeat an uncertain action to
try to obtain a better frame.

Offline checks (synthetic images, fake window/GDI APIs and fake session binding):

```powershell
python tools/qa-le2/test-capture.py
```

These verify refusal/identity changes, exact-PID selection, handle cleanup,
output preservation and uncertainty; they make no claim about live pixels.

## Observe or explicitly activate the owned window

Reuse [focus-owned-session.ps1](../qa-hubs/focus-owned-session.ps1); it defaults
to the existing LE1 workflow, so specify `-Game LE2` for a protected LE2 run:

```powershell
# Observe only; appends the existing intervention receipt, without changing focus.
pwsh -NoProfile -File tools/qa-hubs/focus-owned-session.ps1 -Game LE2 -RunDirectory RUN

# Separate explicit focus request, only for the currently owned session.
pwsh -NoProfile -File tools/qa-hubs/focus-owned-session.ps1 -Game LE2 -RunDirectory RUN -Activate
```

The LE2 branch reads its context directly and leaves `LOCALAPPDATA` unchanged.
It requires a backed-up `game-bound` run, its canonical run directory and exact
ownership of the caller's `LOCALAPPDATA/MEVR/qa-cafeteria.lock`, and the bound
MassEffect2 PID, executable path and process start time. Restored, foreign or
replaced sessions are refused. Run it in the approved desktop host environment;
this helper verifies process/lock ownership, not loaded-module or control-channel
identity. It does not launch a game or send gameplay input.

Only `-Activate` invokes the existing Win32 foreground request. Both modes append
`launch-events.jsonl` with the request, before/after foreground PID and result.
Windows may refuse activation; that failure is recorded and returned. Exclude
explicit focus interventions from frame-comparison baselines. After a successful
request, take a fresh capture with a new name; capture performs its own checks.
Neither focus success nor a readable image establishes input consumption.

The adapter passed offline fake-process/Win32 checks, including unchanged LE1
default behavior; positive LE2 native focus acceptance remains pending at this
documentation checkpoint. Reproduce the offline checks with
`pwsh -NoProfile -File tools/qa-hubs/test-focus-owned-session.ps1`.

## Explicit foreground desktop fallback

When PrintWindow returns black GPU content, a separately requested capture can
copy the foreground game's client region from the desktop:

```powershell
python tools/qa-le2/capture.py RUN --name fresh-desktop-review --method desktop-client
```

The default remains `--method print-window`; there is no automatic fallback,
retry, focus change or input. The new capture must use a new directory name.
The helper requires the exact owned game HWND to be the foreground window,
visible and not minimized. Under its private thread's physical-pixel DPI context,
it maps both client corners with
[ClientToScreen](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-clienttoscreen),
checks that dimensions agree and the entire client lies within the
[virtual-screen bounds](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsystemmetrics),
then copies only that rectangle. It refuses partial offscreen clients instead of
clipping. Negative coordinates on secondary monitors are supported.

The GDI copy uses the desktop DC and
[BitBlt](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-bitblt)
with `SRCCOPY|CAPTUREBLT`. It reuses bounded bitmap allocation, initialization,
row-count validation and handle cleanup. Foreground HWND, owner, visibility,
minimization, size, physical client rectangle and screen bounds must match again
before and after the copy; run/process/build binding is checked independently.
Moving, resizing, minimizing or changing foreground/ownership invalidates the
capture. No window or system setting is changed.

The receipt explicitly reports
`method=BitBlt(desktop-client;SRCCOPY|CAPTUREBLT)`,
`pixelSource=composited-desktop-client-region`, `requestedMethod=desktop-client`,
the physical rectangles and `foregroundVerifiedBeforeAfter=true`.
`occlusionIndependentlyVerified` remains false: overlays/topmost windows can
appear in desktop pixels, and before/after checks cannot prove continuous
foreground ownership between samples. Review the actual image. This method does
not claim isolated game rendering, screenshot freshness beyond the capture
attempt, scene acceptance, or headset/stereo coverage. Black/constant handling
and the 20-second worker timeout remain unchanged.

Existing menu and save-evidence collectors accept the same bound receipt/image
hash contract; they do not require PrintWindow as the pixel source. Offline tests
exercise that real collector compatibility as well as desktop-only GDI calls,
negative monitor origin, offscreen/refocused/moved/minimized refusal, and cleanup.
Live desktop capture success is a separate check from these synthetic tests.

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
window dimensions, image SHA-256 and one of `captured`, `suspect-black`,
`suspect-constant`, or `failed`. Black/constant images are kept for inspection;
they do not prove that the game rendered an empty scene. A partially written
image after failure is retained and hashed as failed evidence. A failure before
session admission creates no capture directory. Exit 0 means only a nonconstant
image was obtained with the checks passing; suspect/failure returns 2.

`gameplayPassed` is always false and `visualAcceptance` stays pending. A human
must inspect the PNG and associate its contents with the selected fixture and
same-run state evidence. This is desktop-client capture, not headset/stereo
proof. The tool does not promote a scenario or silently attach acceptance.

Offline checks (synthetic images, fake window/GDI APIs and fake session binding):

```powershell
python tools/qa-le2/test-capture.py
```

These verify refusal/identity changes, exact-PID selection, handle cleanup,
output preservation and uncertainty; they make no claim about live pixels.

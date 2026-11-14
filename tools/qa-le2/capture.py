# SPDX-License-Identifier: MIT
"""Capture the bound ME2 window without focus/input. No gameplay acceptance.

PrintWindow/client-bitmap approach adapted from this repository's MIT
tools/flicker-grab.py; explicit Win32 signatures, ownership checks and cleanup
are added here. Pillow is the same existing image dependency; no NumPy needed.
"""
import argparse
import ctypes as ct
import ctypes.wintypes as wt
import datetime as dt
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid

_spec = importlib.util.spec_from_file_location("le2_capture_control", Path(__file__).with_name("control.py"))
c = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(c)
MAX_DIMENSION, MAX_PIXELS = 8192, 16777216
METHODS = ("print-window", "desktop-client")


class CaptureError(ValueError):
    def __init__(self, message, *, window=None):
        super().__init__(message)
        self.window = window


def utc():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def binding(run):
    directory, identity = c.binding(run)
    actual_run, context = c.session.load_context(run)
    if context.get("phase") != "game-bound" or context["gameProcess"]["name"] != "MassEffect2":
        raise CaptureError("Capture requires the owned game-bound ME2 session")
    process = context["gameProcess"]
    if (str(process["pid"]) != identity["pid"] or c.filetime(process["started"]) != identity["start"]
            or context["runId"] != identity["run"] or context["exeSha256"] != identity["exe"]):
        raise CaptureError("Session changed between binding and context inspection")
    return dict(runDirectory=str(actual_run), controlDirectory=str(directory),
                identity=identity, process=context["gameProcess"], planSha256=context["planSha256"])


def dimensions(width, height):
    if (width <= 0 or height <= 0 or width > MAX_DIMENSION or height > MAX_DIMENSION
            or width * height > MAX_PIXELS):
        raise CaptureError("Empty or excessive client dimensions")


class BitmapInfo(ct.Structure):
    _fields_ = [("size", wt.DWORD), ("width", wt.LONG), ("height", wt.LONG),
                ("planes", wt.WORD), ("bits", wt.WORD), ("compression", wt.DWORD),
                ("imageSize", wt.DWORD), ("xppm", wt.LONG), ("yppm", wt.LONG),
                ("used", wt.DWORD), ("important", wt.DWORD)]


class WindowsCapture:
    def __init__(self):
        if os.name != "nt":
            raise CaptureError("PrintWindow capture requires Windows")
        self.user = ct.WinDLL("user32", use_last_error=True)
        self.gdi = ct.WinDLL("gdi32", use_last_error=True)
        self.callback = ct.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
        signatures = [
            (self.user, "EnumWindows", [self.callback, wt.LPARAM], wt.BOOL),
            (self.user, "IsWindow", [wt.HWND], wt.BOOL),
            (self.user, "IsWindowVisible", [wt.HWND], wt.BOOL),
            (self.user, "IsIconic", [wt.HWND], wt.BOOL),
            (self.user, "GetWindowThreadProcessId", [wt.HWND, ct.POINTER(wt.DWORD)], wt.DWORD),
            (self.user, "GetClientRect", [wt.HWND, ct.POINTER(wt.RECT)], wt.BOOL),
            (self.user, "GetForegroundWindow", [], wt.HWND),
            (self.user, "ClientToScreen", [wt.HWND, ct.POINTER(wt.POINT)], wt.BOOL),
            (self.user, "GetSystemMetrics", [ct.c_int], ct.c_int),
            (self.user, "GetDC", [wt.HWND], wt.HDC),
            (self.user, "ReleaseDC", [wt.HWND, wt.HDC], ct.c_int),
            (self.user, "PrintWindow", [wt.HWND, wt.HDC, wt.UINT], wt.BOOL),
            (self.user, "SetThreadDpiAwarenessContext", [wt.HANDLE], wt.HANDLE),
            (self.gdi, "CreateCompatibleDC", [wt.HDC], wt.HDC),
            (self.gdi, "CreateCompatibleBitmap", [wt.HDC, ct.c_int, ct.c_int], wt.HBITMAP),
            (self.gdi, "SelectObject", [wt.HDC, wt.HANDLE], wt.HANDLE),
            (self.gdi, "DeleteObject", [wt.HANDLE], wt.BOOL),
            (self.gdi, "DeleteDC", [wt.HDC], wt.BOOL),
            (self.gdi, "PatBlt", [wt.HDC, ct.c_int, ct.c_int, ct.c_int, ct.c_int, wt.DWORD], wt.BOOL),
            (self.gdi, "BitBlt", [wt.HDC, ct.c_int, ct.c_int, ct.c_int, ct.c_int,
                                  wt.HDC, ct.c_int, ct.c_int, wt.DWORD], wt.BOOL),
            (self.gdi, "GetDIBits", [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT, ct.c_void_p,
                                    ct.POINTER(BitmapInfo), wt.UINT], ct.c_int),
        ]
        for library, name, args, result in signatures:
            function = getattr(library, name)
            function.argtypes, function.restype = args, result

    def window(self, hwnd):
        if not self.user.IsWindow(hwnd):
            raise CaptureError("Window no longer exists")
        pid = wt.DWORD()
        if not self.user.GetWindowThreadProcessId(hwnd, ct.byref(pid)):
            raise CaptureError("Cannot read window owner")
        rect = wt.RECT()
        if not self.user.GetClientRect(hwnd, ct.byref(rect)):
            raise CaptureError("Cannot read client rectangle")
        return dict(hwnd=str(hwnd), pid=pid.value, visible=bool(self.user.IsWindowVisible(hwnd)),
                    minimized=bool(self.user.IsIconic(hwnd)), width=rect.right - rect.left,
                    height=rect.bottom - rect.top,
                    # Recorded for PrintWindow too. Background capture is permitted;
                    # the existing before/after comparison refuses a focus change.
                    foregroundHwnd=str(self.user.GetForegroundWindow() or 0))

    def windows(self, pid):
        found, errors = [], []
        @self.callback
        def callback(hwnd, _):
            owner = wt.DWORD()
            if self.user.GetWindowThreadProcessId(hwnd, ct.byref(owner)) and owner.value == pid:
                try:
                    state = self.window(hwnd)
                    if state["visible"]:
                        found.append(state)
                except Exception as error:
                    errors.append(error)
                    return False
            return True
        ok = self.user.EnumWindows(callback, 0)
        if errors:
            raise errors[0]
        if not ok:
            raise CaptureError("Window enumeration failed")
        return found

    def desktop_window(self, hwnd):
        """Physical client coordinates under the worker's per-monitor DPI context."""
        state = self.window(hwnd)
        rect = wt.RECT()
        if not self.user.GetClientRect(hwnd, ct.byref(rect)):
            raise CaptureError("Cannot read desktop client rectangle", window=state)
        begin, end = wt.POINT(rect.left, rect.top), wt.POINT(rect.right, rect.bottom)
        if not self.user.ClientToScreen(hwnd, ct.byref(begin)) or not self.user.ClientToScreen(hwnd, ct.byref(end)):
            raise CaptureError("Cannot map physical client rectangle", window=state)
        x, y, width, height = (self.user.GetSystemMetrics(index) for index in (76, 77, 78, 79))
        state.update(foregroundHwnd=str(self.user.GetForegroundWindow() or 0),
                     clientScreenRect=dict(left=begin.x, top=begin.y, right=end.x, bottom=end.y),
                     virtualScreenRect=dict(left=x, top=y, right=x+width, bottom=y+height))
        return state

    def bitmap(self, hwnd, width, height, *, screen_rect=None):
        from PIL import Image
        dimensions(width, height)
        source = memory = bitmap = previous = None
        selected = False
        source_window = None if screen_rect is not None else hwnd
        try:
            source = self.user.GetDC(source_window)
            if not source:
                raise CaptureError("GetDC failed")
            memory = self.gdi.CreateCompatibleDC(source)
            if not memory:
                raise CaptureError("CreateCompatibleDC failed")
            bitmap = self.gdi.CreateCompatibleBitmap(source, width, height)
            if not bitmap:
                raise CaptureError("CreateCompatibleBitmap failed")
            previous = self.gdi.SelectObject(memory, bitmap)
            if not previous or previous == ct.c_void_p(-1).value:
                raise CaptureError("SelectObject failed")
            selected = True
            # Initialize unwritten pixels rather than exposing GDI bitmap memory.
            if not self.gdi.PatBlt(memory, 0, 0, width, height, 0x00000042):  # BLACKNESS
                raise CaptureError("Bitmap initialization failed")
            if screen_rect is None:
                if not self.user.PrintWindow(hwnd, memory, 3):  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
                    raise CaptureError("PrintWindow failed")
            elif not self.gdi.BitBlt(memory, 0, 0, width, height, source,
                                     screen_rect["left"], screen_rect["top"], 0x40CC0020):  # SRCCOPY | CAPTUREBLT
                raise CaptureError("Desktop client BitBlt failed")
            restored = self.gdi.SelectObject(memory, previous)
            if not restored or restored == ct.c_void_p(-1).value:
                raise CaptureError("Bitmap deselection failed")
            selected = False  # GetDIBits requires bitmap not selected into a DC.
            info = BitmapInfo(ct.sizeof(BitmapInfo), width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
            pixels = ct.create_string_buffer(width * height * 4)
            if self.gdi.GetDIBits(memory, bitmap, 0, height, pixels, ct.byref(info), 0) != height:
                raise CaptureError("GetDIBits did not copy every row")
            return Image.frombytes("RGB", (width, height), pixels.raw, "raw", "BGRX")
        finally:
            if selected:
                self.gdi.SelectObject(memory, previous)
            if memory:
                self.gdi.DeleteDC(memory)
            if bitmap:
                self.gdi.DeleteObject(bitmap)
            if source:
                self.user.ReleaseDC(source_window, source)


def capture_window(api, pid):
    windows = api.windows(pid)
    if len(windows) != 1:
        raise CaptureError(f"Expected one visible PID-owned top-level window; found {len(windows)}")
    before = windows[0]
    if before["pid"] != pid or not before["visible"] or before["minimized"]:
        raise CaptureError("Window ownership/visibility invalid or window minimized", window=before)
    try:
        dimensions(before["width"], before["height"])
    except CaptureError as error:
        error.window = before
        raise
    hwnd = int(before["hwnd"])
    if api.window(hwnd) != before:
        raise CaptureError("Window changed before capture")
    try:
        image = api.bitmap(hwnd, before["width"], before["height"])
    except CaptureError as error:
        error.window = before
        raise
    if api.window(hwnd) != before:
        raise CaptureError("Window changed during capture")
    if image.size != (before["width"], before["height"]):
        raise CaptureError("Image dimensions differ from the verified client")
    return before, image


def capture_desktop_client(api, pid):
    windows = api.windows(pid)
    if len(windows) != 1:
        raise CaptureError(f"Expected one visible PID-owned top-level window; found {len(windows)}")
    original = windows[0]
    hwnd = int(original["hwnd"])
    before = api.desktop_window(hwnd)
    if any(before.get(key) != value for key, value in original.items()):
        raise CaptureError("Window changed before desktop capture", window=before)
    if (before["pid"] != pid or not before["visible"] or before["minimized"]
            or before["foregroundHwnd"] != before["hwnd"]):
        raise CaptureError("Desktop capture requires the exact owned foreground window, visible and not minimized", window=before)
    rect, screen = before["clientScreenRect"], before["virtualScreenRect"]
    width, height = rect["right"]-rect["left"], rect["bottom"]-rect["top"]
    dimensions(width, height)
    if (width != before["width"] or height != before["height"]
            or screen["right"] <= screen["left"] or screen["bottom"] <= screen["top"]
            or not screen["left"] <= rect["left"] < rect["right"] <= screen["right"]
            or not screen["top"] <= rect["top"] < rect["bottom"] <= screen["bottom"]):
        raise CaptureError("Physical client rectangle is inconsistent or not fully inside the virtual screen", window=before)
    if api.desktop_window(hwnd) != before:
        raise CaptureError("Foreground window or rectangle changed before desktop capture", window=before)
    try:
        image = api.bitmap(hwnd, width, height, screen_rect=rect)
    except CaptureError as error:
        error.window = before
        raise
    if api.desktop_window(hwnd) != before:
        raise CaptureError("Foreground window, ownership or rectangle changed during desktop capture", window=before)
    if image.size != (width, height):
        raise CaptureError("Desktop image dimensions differ from verified client", window=before)
    return before, image


def worker(run, batch, method="print-window"):
    # Private child: still requires the same real session binding, not just a PID.
    if method not in METHODS:
        raise CaptureError("Unknown capture method")
    expected = binding(run)
    batch = c.session.safe_path(batch)
    if batch.parent != c.session.safe_path(run / "captures") or not batch.is_dir():
        raise CaptureError("Worker output is outside the run capture directory")
    api = WindowsCapture()
    previous_dpi = api.user.SetThreadDpiAwarenessContext(ct.c_void_p(-4))  # Per-monitor aware v2; this thread only.
    if not previous_dpi:
        raise CaptureError("Cannot establish physical-pixel DPI context")
    try:
        operation = capture_desktop_client if method == "desktop-client" else capture_window
        window, image = operation(api, int(expected["identity"]["pid"]))
    finally:
        api.user.SetThreadDpiAwarenessContext(previous_dpi)
    if binding(run) != expected:
        raise CaptureError("Session or process changed during capture")
    extrema = image.getextrema()
    black = all(high == 0 for low, high in extrema)
    constant = all(low == high for low, high in extrema)
    with (batch / "frame.png").open("xb") as stream:
        image.save(stream, format="PNG")
    result = dict(binding=expected, window=window, black=black, constant=constant,
                  capturedAtUtc=utc(), method="PrintWindow(PW_CLIENTONLY|PW_RENDERFULLCONTENT)")
    if method == "desktop-client":
        result.update(method="BitBlt(desktop-client;SRCCOPY|CAPTUREBLT)",
                      pixelSource="composited-desktop-client-region", foregroundVerifiedBeforeAfter=True,
                      occlusionIndependentlyVerified=False,
                      limitations="Desktop overlays may be present; before/after samples do not prove continuous foreground ownership")
    return result


def native_backend(run, batch, method="print-window"):
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    # PrintWindow is synchronous and can block. Timeout kills only this private
    # helper, never the game, and never starts a second attempt.
    result = subprocess.run([sys.executable, str(Path(__file__).resolve()), str(run),
                             "--_worker", str(batch), "--method", method], capture_output=True, text=True,
                            timeout=20, startupinfo=startup)
    if result.returncode:
        try:
            failure = json.loads(result.stdout)
        except ValueError:
            failure = {}
        raise CaptureError("Capture helper failed: " + failure.get("workerError", result.stderr.strip()[-2000:]),
                           window=failure.get("window"))
    return json.loads(result.stdout)


def capture(run, name=None, *, bind=binding, backend=None, method="print-window"):
    if method not in METHODS:
        raise CaptureError("Unknown capture method")
    run = c.session.safe_path(run)
    expected = bind(run)  # No output mutation before verified session admission.
    name = name or (dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + uuid.uuid4().hex[:8])
    if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,79}", name):
        raise CaptureError("Capture name must be a simple new directory name")
    parent = c.session.safe_path(run / "captures")
    parent.mkdir(exist_ok=True)
    batch = c.session.safe_path(parent / name)
    batch.mkdir()  # exclusive: no existing capture can be replaced
    receipt = dict(schemaVersion=1, source="le2-window-capture", status="attempting",
                   startedAtUtc=utc(), bindingBefore=expected, gameplayPassed=False,
                   visualAcceptance="pending-human-review", requestedMethod=method, window=None, image=None)
    receipt_path = batch / "capture.json"
    c.session.write_json(receipt_path, receipt)
    try:
        if bind(run) != expected:
            raise CaptureError("Session changed before capture helper")
        result = native_backend(run, batch, method) if backend is None else backend(run, batch)
        actual = bind(run)
        receipt["bindingAfter"] = actual
        if actual != expected or result.get("binding") != expected:
            raise CaptureError("Session or process changed; capture provenance invalid")
        image_path = c.session.safe_path(batch / "frame.png")
        if not image_path.is_file():
            raise CaptureError("Capture helper returned without an image")
        from PIL import Image
        with Image.open(image_path) as image:
            if image.format != "PNG" or image.size != (result["window"]["width"], result["window"]["height"]):
                raise CaptureError("Capture image differs from reported format/dimensions")
            image.verify()
        receipt.update(result)
        receipt["status"] = "suspect-black" if result["black"] else "suspect-constant" if result["constant"] else "captured"
    except Exception as error:
        receipt["status"] = "failed"
        receipt["error"] = f"{type(error).__name__}: {error}"
        if getattr(error, "window", None) is not None:
            receipt["window"] = error.window
    finally:
        image_path = c.session.safe_path(batch / "frame.png")
        if image_path.is_file():
            receipt["image"] = dict(path=str(image_path), sha256=c.session.digest(image_path))
        receipt["finishedAtUtc"] = utc()
        c.session.write_json(receipt_path, receipt)
    return receipt_path, receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--name")
    parser.add_argument("--method", choices=METHODS, default="print-window",
                        help="Explicit optional foreground desktop-client capture; never an automatic retry")
    parser.add_argument("--_worker", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        run = c.session.safe_path(args.run.absolute())
        if args._worker:
            print(json.dumps(worker(run, args._worker, args.method)))
            return 0
        path, receipt = capture(run, args.name, method=args.method)
        print(json.dumps(dict(receiptPath=str(path), **receipt), indent=2))
        return 0 if receipt["status"] == "captured" else 2
    except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError) as error:
        if args._worker:
            print(json.dumps(dict(workerError=str(error), window=getattr(error, "window", None))))
            return 2
        print(f"Capture refused: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

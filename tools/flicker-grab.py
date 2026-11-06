# SPDX-License-Identifier: MIT
"""Counts the picture's sharp/smeared flips in the game's desktop mirror (research/35 attempt 49, the motion blur under
Halcyon's stereo: whole surfaces flip between their detailed look and a look streaked along one direction, in runs of
1-4 frames). Grabs the game window's client area (PrintWindow through DWM: covered windows too) for `seconds` at the rate the grab allows, takes each frame's
high-frequency energy (mean |Laplacian| of the middle half), divides it by a running median of 9 frames (so the head's
slow changes of view don't count) and counts the frames that drop below `threshold` of it and the transitions in and out.
In attempt 49: 35-51 transitions in 4 s with the blur, 1-3 without (the pan's content changes). PrintWindow manages about
2 frames a second of a 2344x2392 window (2026-11-06): too slow to count flips, but `--save` keeps frames that show the
smear (near surfaces streaked and doubled, the far scene sharp); a smaller game window grabs faster.

    python tools/flicker-grab.py [seconds 4] [threshold 0.85] [--save folder]

Needs the game running with its window (the mirror); it may be covered by others. Run it while the head moves as in play: a still head shows
no blur."""
import ctypes
import ctypes.wintypes as wt
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageGrab

user32 = ctypes.windll.user32
user32.SetProcessDPIAware()


def game_window():
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        buf = ctypes.create_unicode_buffer(260)
        h = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid.value)
        if h:
            size = wt.DWORD(260)
            ctypes.windll.kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size))
            ctypes.windll.kernel32.CloseHandle(h)
        if buf.value.lower().endswith("masseffect1.exe"):
            r = wt.RECT()
            user32.GetClientRect(hwnd, ctypes.byref(r))
            if r.right - r.left > 200:
                found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def client_box(hwnd):
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    p = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(p))
    left, top, right, bottom = p.x, p.y, p.x + r.right, p.y + r.bottom
    # only what is on the virtual screen
    vx, vy = user32.GetSystemMetrics(76), user32.GetSystemMetrics(77)
    vw, vh = user32.GetSystemMetrics(78), user32.GetSystemMetrics(79)
    return max(left, vx), max(top, vy), min(right, vx + vw), min(bottom, vy + vh)


gdi32 = ctypes.windll.gdi32


def capture(hwnd):
    """The window's own content through DWM (PrintWindow with PW_RENDERFULLCONTENT), covered or not."""
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    hdc_win = user32.GetDC(hwnd)
    hdc = gdi32.CreateCompatibleDC(hdc_win)
    bmp = gdi32.CreateCompatibleBitmap(hdc_win, w, h)
    old = gdi32.SelectObject(hdc, bmp)
    user32.PrintWindow(hwnd, hdc, 3)  # PW_CLIENTONLY | PW_RENDERFULLCONTENT

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD),
                    ("biBitCount", wt.WORD), ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                    ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                    ("biClrImportant", wt.DWORD)]

    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.biWidth, bi.biHeight, bi.biPlanes, bi.biBitCount = w, -h, 1, 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(hdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi32.SelectObject(hdc, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(hdc)
    user32.ReleaseDC(hwnd, hdc_win)
    return Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")


def hf(img):
    a = np.asarray(img.convert("L"), dtype=np.float32)
    h, w = a.shape
    a = a[h // 4: 3 * h // 4, w // 4: 3 * w // 4]
    lap = a[1:-1, 1:-1] * 4 - a[:-2, 1:-1] - a[2:, 1:-1] - a[1:-1, :-2] - a[1:-1, 2:]
    return float(np.mean(np.abs(lap))), float(a.std())


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    seconds = float(args[0]) if args else 4.0
    threshold = float(args[1]) if len(args) > 1 else 0.85
    save = None
    if "--save" in sys.argv:
        save = Path(sys.argv[sys.argv.index("--save") + 1])
        save.mkdir(parents=True, exist_ok=True)
    hwnd = game_window()
    if not hwnd:
        print("no MassEffect1.exe window")
        return 1
    box = client_box(hwnd)
    values = []
    stamps = []
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < seconds:
        img = capture(hwnd)
        stamps.append(time.perf_counter() - t0)
        values.append(hf(img))
        if save is not None and len(values) <= 200:
            img.save(save / f"f{len(values):03d}.png")
    e = np.array([v[0] for v in values])
    n = len(e)
    rel = np.ones(n)
    for i in range(n):
        lo, hi = max(0, i - 4), min(n, i + 5)
        med = float(np.median(e[lo:hi]))
        rel[i] = e[i] / med if med > 0 else 1.0
    low = rel < threshold
    transitions = int(np.sum(low[1:] != low[:-1]))
    print(f"{n} frames in {stamps[-1]:.1f} s ({n / max(stamps[-1], 1e-3):.1f}/s) of a {box[2] - box[0]}x{box[3] - box[1]} grab; "
          f"high-frequency energy {e.min():.2f}..{e.max():.2f} (median {np.median(e):.2f})")
    print(f"frames below {threshold:.2f} of their neighbours' median: {int(low.sum())}; transitions: {transitions}")
    print("relative energy:", " ".join(f"{v:.2f}" for v in rel))
    return 0


if __name__ == "__main__":
    sys.exit(main())

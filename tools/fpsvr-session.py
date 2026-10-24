# SPDX-License-Identifier: MIT
"""What fpsVR recorded for the player's headset sessions (research/35 attempt 41): the CPU and GPU frame times it
showed, per session, from its own files in %LOCALAPPDATA%\fpsVR (N.json: per-frame histograms, CPU in 0.1 ms bins and
GPU in 1 ms bins, over the seconds the headset was worn; N.bin: one 30-byte record a second). fpsVR's "CPU frametime"
is SteamVR's frame timing: wall time from the frame's poses (xrWaitFrame's return) to its submit (xrEndFrame), the
"poses -> submit, wall" row of tools/perf-probe.py.

Usage: python tools/fpsvr-session.py            the sessions of this game, newest first (summary line each)
       python tools/fpsvr-session.py N          one session in full (N as the list prints it; 0 is the newest)
"""
import json
import os
import sys


def folder():
    return os.path.join(os.environ.get('LOCALAPPDATA', ''), 'fpsVR')


def load(n):
    with open(os.path.join(folder(), f'{n}.json'), encoding='utf-8-sig') as f:
        return json.load(f)


def quantiles(hist, scale):
    total = sum(hist)
    if not total:
        return None
    mean = sum(i * c for i, c in enumerate(hist)) / total * scale
    out = {}
    cum = 0
    for i, c in enumerate(hist):
        cum += c
        for q, name in ((0.5, 'median'), (0.9, 'p90'), (0.99, 'p99')):
            if name not in out and cum >= q * total:
                out[name] = (i + 0.5) * scale
    out['mean'] = mean
    out['n'] = total
    return out


def summary(j):
    cpu = quantiles(j.get('cputimes', []), 0.1)
    gpu = quantiles(j.get('gputimes', []), 1.0)
    fps = quantiles(j.get('fps_ps', []), 1.0)
    gpu_use = quantiles(j.get('gpuusage', []), 1.0)
    cpu_use = quantiles(j.get('cpuusage', []), 1.0)
    return cpu, gpu, fps, gpu_use, cpu_use


def fmt(q, unit):
    if not q:
        return 'n/a'
    return f'median {q["median"]:.1f} p90 {q["p90"]:.1f} p99 {q["p99"]:.1f} mean {q["mean"]:.1f} {unit}'


def main():
    if not os.path.isdir(folder()):
        sys.exit(f'no fpsVR folder at {folder()}')
    names = sorted((int(f[:-5]) for f in os.listdir(folder()) if f.endswith('.json') and f[:-5].isdigit()))
    if len(sys.argv) > 1:
        n = int(sys.argv[1])
        j = load(n)
        cpu, gpu, fps, gpu_use, cpu_use = summary(j)
        print(f'session {n}: {j.get("app")} on {j.get("hmd")} ({j.get("ActualHMDDriver")}) at {j.get("hz")} Hz, '
              f'{j.get("DateStart", "")[:19]} -> {j.get("DateEnd", "")[:19]}')
        print(f'  headset worn {j.get("stime", 0):.0f} s of the session; {j.get("framesused")} frames counted, '
              f'{j.get("reprojected")} reprojected, {j.get("dropped")} dropped; eye {j.get("maxX")}x{j.get("maxY")}')
        print(f'  CPU frametime (poses -> submit)  {fmt(cpu, "ms")}')
        print(f'  GPU frametime                    {fmt(gpu, "ms")}')
        print(f'  fps (per second)                 {fmt(fps, "")}')
        print(f'  GPU usage                        {fmt(gpu_use, "%")}')
        print(f'  CPU usage                        {fmt(cpu_use, "%")}')
        hist = j.get('cputimes', [])
        total = sum(hist) or 1
        print('  CPU frametime distribution (1 ms bins, share of frames):')
        bins = {}
        for i, c in enumerate(hist):
            bins[i // 10] = bins.get(i // 10, 0) + c
        for ms in sorted(bins):
            share = bins[ms] / total
            if share >= 0.005:
                print(f'    {ms:3d}-{ms + 1:<3d} ms {share * 100:5.1f}%  {"#" * int(round(share * 60))}')
        return
    print(f'{"n":>2}  {"start":<19} {"headset":<18} {"worn":>6}  {"CPU ms med/p90/mean":>21}  {"GPU ms med/p90":>15}  {"fps med":>7}  reproj')
    for n in names:
        try:
            j = load(n)
        except (OSError, ValueError):
            continue
        if 'Mass Effect' not in str(j.get('app', '')):
            continue
        cpu, gpu, fps, _, _ = summary(j)
        c = f'{cpu["median"]:5.1f} {cpu["p90"]:5.1f} {cpu["mean"]:5.1f}' if cpu else 'n/a'
        g = f'{gpu["median"]:5.1f} {gpu["p90"]:5.1f}' if gpu else 'n/a'
        f = f'{fps["median"]:5.0f}' if fps else 'n/a'
        print(f'{n:>2}  {j.get("DateStart", "")[:19]:<19} {str(j.get("hmd"))[:18]:<18} {j.get("stime", 0):5.0f}s  {c:>21}  {g:>15}  {f:>7}  '
              f'{j.get("reprojected")}/{j.get("framesused")}')


if __name__ == '__main__':
    main()

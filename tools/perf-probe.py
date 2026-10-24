# SPDX-License-Identifier: MIT
"""Where each frame's time goes, from a frame-breakdown probe capture (plugin/PerfProbe, research/22).

Reads %LOCALAPPDATA%\\MEVR\\perf\\<stamp>[-tag]-probe.json and its -frames.csv and -ticks.csv (written by the
running game when asked: tools/perf-probe.ps1, perf.request, or the VR hands tab's "Measure frame timing").

For the headset frames (Stereo 2 ones on their own when there are enough), prints the median, 90th
percentile and maximum, in milliseconds, of:
  render thread  the game's work for the left eye, the right eye and the UI; Halcyon before the wait;
                 xrWaitFrame, xrBeginFrame; our controllers and quads; the copies; xrEndFrame; the mirror;
                 wall time, and the thread's own busy time (busy << wall = waiting, busy ~ wall = working)
  GPU            how far the GPU runs behind the CPU at each point (lag ~0: it waits for the CPU; a few ms
                 and more: it is the limit), and the span of each step on the GPU (its work when the GPU
                 is the limit; when it isn't, idle time counts too: tools/perf-monitor.ps1's GPU % then)
  game thread    the engine's tick and ours, wall and busy
then which of the three uses most of the display period.

Usage: python tools/perf-probe.py [--latest | PATH-TO-probe.json] [--all]   (--all: every frame, not only VR)
"""

import argparse
import csv
import json
import os
import statistics
import sys


def perf_dir():
    return os.path.join(os.environ.get('LOCALAPPDATA', ''), 'MEVR', 'perf')


def latest_probe():
    folder = perf_dir()
    if not os.path.isdir(folder):
        return None
    probes = [os.path.join(folder, f) for f in os.listdir(folder) if f.endswith('-probe.json')]
    return max(probes, key=os.path.getmtime) if probes else None


def number(row, key):
    value = row.get(key, '')
    return float(value) if value not in ('', None) else None


def load_rows(path):
    with open(path, newline='', encoding='utf-8') as f:
        return [{k: number(row, k) for k in row} for row in csv.DictReader(f)]


def span(row, a, b, kind='t'):
    x, y = row.get(f'{kind}_{a}'), row.get(f'{kind}_{b}')
    return y - x if x is not None and y is not None else None


def stats(values):
    values = sorted(v for v in values if v is not None)
    if not values:
        return None
    p90 = values[min(len(values) - 1, int(round(0.9 * (len(values) - 1))))]
    return statistics.median(values), p90, values[-1], len(values)


def line(label, values, note=''):
    s = stats(values)
    if s is None:
        return
    print(f'  {label:<34} {s[0]:7.2f} {s[1]:7.2f} {s[2]:7.2f}   {note}')


def frame_metrics(r):
    m = {}
    m['interval'] = span(r, 'start', 'end')
    m['game_wall'] = span(r, 'start', 'present')
    m['game_busy'] = span(r, 'start', 'present', 'busy')
    if r.get('t_pass0') is not None:
        m['left_wall'] = span(r, 'start', 'pass0')
        m['left_busy'] = span(r, 'start', 'pass0', 'busy')
        m['right_wall'] = span(r, 'pass0', 'present')
        m['right_busy'] = span(r, 'pass0', 'present', 'busy')
        m['left_draws'] = span(r, 'start', 'pass0', 'draws')
        m['right_draws'] = span(r, 'pass0', 'present', 'draws')
    m['draws'] = r.get('draws_end')
    m['pre_wait'] = span(r, 'present', 'wait_begin')
    m['wait_wall'] = span(r, 'wait_begin', 'wait_end')
    m['wait_busy'] = span(r, 'wait_begin', 'wait_end', 'busy')
    m['begin_wall'] = span(r, 'begin_begin', 'begin_end')
    m['begin_busy'] = span(r, 'begin_begin', 'begin_end', 'busy')
    m['input'] = span(r, 'input_begin', 'input_end')
    m['frame_begun_rest'] = span(r, 'input_end', 'frame_begun_end')
    m['overlays'] = span(r, 'overlay_begin', 'overlay_end')
    copies = span(r, 'frame_begun_end', 'end_frame_begin')
    if copies is not None and m['overlays'] is not None:
        copies -= m['overlays']
    m['copies'] = copies
    m['end_frame'] = span(r, 'end_frame_begin', 'end_frame_end')
    m['tail'] = span(r, 'end_frame_end', 'end')
    m['swapchain_wait'] = r.get('swapchain_wait_ms')
    busy_total = r.get('busy_end')
    waits = sum(v for v in (m['wait_busy'], m['begin_busy']) if v is not None)
    m['render_busy_total'] = busy_total
    m['render_work'] = busy_total - waits if busy_total is not None else None
    # GPU: work per step (upper bound) and lag behind the CPU at each point.
    if r.get('gpu_valid'):
        points = ['start', 'pass0', 'present', 'end_frame_begin', 'end']
        g = {p: r.get(f'gpu_{p}') for p in points}
        t = {p: r.get(f't_{p}') for p in points}
        for p in points:
            if g[p] is not None and t[p] is not None:
                m[f'lag_{p}'] = g[p] - t[p]
        # A step's span: from when the GPU could start it (done with the step before, and the CPU issuing
        # it) to when it finished. The copies are issued after xrBeginFrame, so the wait isn't counted.
        issue = dict(t)
        issue['end_frame_begin'] = r.get('t_begin_end')
        chain = [p for p in points if g[p] is not None and t[p] is not None]
        total = 0.0
        for a, b in zip(chain, chain[1:]):
            starts = issue.get('end_frame_begin') if b == 'end_frame_begin' else t[a]
            work = g[b] - max(g[a], starts if starts is not None else t[a])
            m[f'gpu_{a}_{b}'] = max(work, 0.0)
            if a != 'end_frame_begin':  # after xrEndFrame the GPU can be held by the runtime: not work
                total += max(work, 0.0)
        m['gpu_work'] = total if len(chain) >= 2 else None
        m['gpu_held'] = m.get('gpu_end_frame_begin_end')
    m['display_period'] = r.get('display_period_ms') or None
    return m


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('path', nargs='?')
    parser.add_argument('--latest', action='store_true')
    parser.add_argument('--all', action='store_true', help='every frame, not only headset frames')
    args = parser.parse_args()
    path = args.path if args.path and not args.latest else latest_probe()
    if not path or not os.path.exists(path):
        sys.exit(f'No probe capture found in {perf_dir()}')
    folder = os.path.dirname(path)
    meta = json.load(open(path, encoding='utf-8'))
    frames = load_rows(os.path.join(folder, meta['files']['frames']))
    ticks = load_rows(os.path.join(folder, meta['files']['ticks']))

    print(f'{path}')
    print(f'  {meta["frames"]} frames, {meta["ticks"]} ticks in {meta["seconds"]:.1f} s; '
          f'GPU timestamps: {"yes" if meta["gpuTimestamps"] else "no"}'
          f' ({meta["gpuFramesDropped"]} frames without); {meta["accessViolations"]} access violations '
          f'({meta["accessViolations"] / max(meta["seconds"], 0.001):.0f}/s)')
    h = meta['halcyon']
    res = meta['resolution']
    print(f'  Stereo 2 {h["sfr2"]}, dynResFraction {h["dynResFraction"]}, mirrorPresentEvery {h["mirrorPresentEvery"]}, '
          f'render {res["render"][0]}x{res["render"][1]}, eye swapchain {res["eye"][0]}x{res["eye"][1]}')
    clocks = meta['clocks']
    if clocks['syncBegin']['valid'] and clocks['syncEnd']['valid']:
        drift = clocks['syncEnd']['gpuMinusCpuMs'] - clocks['syncBegin']['gpuMinusCpuMs']
        print(f'  GPU clock drift over the capture: {drift:.3f} ms (corrected)')
    busy = meta['renderThreadBusySeconds']
    if busy['fromThreadTimes'] > 0:
        print(f'  render thread busy over the capture: {busy["fromCycles"]:.2f} s from cycles, '
              f'{busy["fromThreadTimes"]:.2f} s from thread times (should agree)')

    rows = [r for r in frames[1:] if r.get('t_start') is not None and r.get('t_end') is not None]
    if not args.all:
        vr = [r for r in rows if r.get('xr_frame')]
        rows = vr or rows
    stereo = [r for r in rows if r.get('t_pass0') is not None]
    chosen = stereo if len(stereo) >= max(10, len(rows) // 2) else rows
    kind = 'Stereo 2 headset frames' if chosen is stereo else ('headset frames' if not args.all else 'frames')
    metrics = [frame_metrics(r) for r in chosen]
    # What SteamVR's frame timing (fpsVR's "CPU frametime") counts: wall time from the frame's poses (xrWaitFrame's
    # return) to its submit (xrEndFrame's return). With the early frame wait the poses come at the END of the
    # previous Present, so the span is the previous frame's wait_end -> this frame's end_frame_end: both scene
    # renders, every hold (Halcyon's cutscene cap, stereo pacing) and every idle between them, busy or not.
    for prev, cur, m in zip([None] + chosen[:-1], chosen, metrics):
        m['steamvr_cpu'] = None
        m['hold'] = span(cur, 'present', 'begin_begin')
        if cur.get('t_end_frame_end') is None:
            continue
        own_wait = span(cur, 'wait_end', 'end_frame_end')
        if own_wait is not None and own_wait >= 0:   # the wait came before the submit: upstream's order
            m['steamvr_cpu'] = own_wait
        elif prev is not None and prev.get('t_wait_end') is not None and prev.get('t_end') is not None                 and cur.get('t_start') is not None:
            m['steamvr_cpu'] = (prev['t_end'] - prev['t_wait_end']) + (cur['t_end_frame_end'] - cur['t_start'])
    get = lambda key: [m.get(key) for m in metrics]

    # The headset's own period is the shortest the runtime reported; a longer median means it throttles
    # the game (SteamVR's half-rate lock reports twice the period).
    periods = [r.get('display_period_ms') for r in frames if r.get('display_period_ms')]
    budget = min(periods) if periods else 1000.0 / 90.0
    period = stats(get('display_period'))
    throttle = period[0] / budget if period else 1.0
    interval = stats(get('interval'))
    if interval:
        print(f'\n{len(chosen)} {kind}; headset period {budget:.2f} ms ({1000.0 / budget:.1f} Hz); '
              f'frame interval median {interval[0]:.2f} ms ({1000.0 / interval[0]:.1f} fps)')
    if throttle > 1.5:
        print(f'  The runtime is throttling the game: it asks for a frame every {period[0]:.2f} ms '
              f'({throttle:.1f} headset periods; SteamVR\'s half-rate lock at 2).')
    marks = stats([r.get('pass0_marks') for r in chosen])
    if marks and marks[0] > 1:
        print(f'  The first eye is captured {marks[0]:.0f} times a frame; "to the last left-eye capture" ends at the last '
              'one, which is not the boundary between the eyes.')

    print(f'\n  {"render thread (ms)":<34} {"median":>7} {"p90":>7} {"max":>7}')
    line('frame interval', get('interval'))
    line('game: to the last left-eye capture', get('left_wall'), 'wall')
    line('  busy', get('left_busy'))
    line('game: from it to Present', get('right_wall'), 'wall')
    line('  busy', get('right_busy'))
    line('game: both eyes + UI, busy', get('game_busy'))
    line('Halcyon before the wait', get('pre_wait'))
    line('xrWaitFrame, wall', get('wait_wall'), 'waiting for the runtime')
    line('xrWaitFrame, busy', get('wait_busy'), '~wall: it spins')
    line('xrBeginFrame, wall', get('begin_wall'))
    line('our controllers (sync, locate)', get('input'))
    line('our OnFrameBegun, the rest', get('frame_begun_rest'))
    line('our quads (HUD, reticle, sights...)', get('overlays'))
    line('Halcyon copies and layers', get('copies'))
    line('  of which swapchain waits', get('swapchain_wait'))
    line('xrEndFrame', get('end_frame'))
    line('mirror present and the rest', get('tail'))
    line("Halcyon's hold before xrBeginFrame", get('hold'), 'cutscene cap / stereo pacing: idle fpsVR counts')
    line('busy, whole frame', get('render_busy_total'))
    line('busy, without the XR waits', get('render_work'), '<- the render thread\'s work')
    line('poses -> submit, wall', get('steamvr_cpu'), '<- what fpsVR / SteamVR call CPU frametime')

    print(f'\n  {"draws":<34} {"median":>7} {"p90":>7} {"max":>7}')
    line('to the last left-eye capture', get('left_draws'))
    line('from it to Present', get('right_draws'))
    line('whole frame', get('draws'))
    # Real issuing work grows with the draws; a wait doesn't. A straight line through (draws, busy).
    pairs = [(m.get('draws'), m.get('game_busy')) for m in metrics
             if m.get('draws') is not None and m.get('game_busy') is not None]
    spinning = False
    if len(pairs) >= 20:
        xs, ys = zip(*pairs)
        mx, my = statistics.mean(xs), statistics.mean(ys)
        sxx = sum((x - mx) ** 2 for x in xs)
        syy = sum((y - my) ** 2 for y in ys)
        if sxx > 0 and syy > 0:
            sxy = sum((x - mx) * (y - my) for x, y in pairs)
            slope = sxy / sxx
            r2 = sxy * sxy / (sxx * syy)
            print(f'  game busy = {my - slope * mx:.2f} ms + {slope * 1000.0:.2f} us per draw (r2 {r2:.2f}, '
                  f'draws {min(xs):.0f}-{max(xs):.0f})')
            spinning = r2 < 0.2 and (my - slope * mx) > 0.5 * my
            if spinning:
                print('  -> the busy time barely follows the draws: per-frame work that doesn\'t scale with them, or '
                      'waiting; tools/sample-render-thread.ps1 shows which')

    if meta['gpuTimestamps']:
        print(f'\n  {"GPU (ms)":<34} {"median":>7} {"p90":>7} {"max":>7}')
        line('lag behind the CPU at the 1st eye', get('lag_pass0'), '~0: the GPU waits on the CPU')
        line('lag behind the CPU at Present', get('lag_present'))
        line('lag at frame end', get('lag_end'))
        line('span: left eye', get('gpu_start_pass0'), 'its work, if the GPU lags')
        line('span: right eye + UI', get('gpu_pass0_present'))
        line('span: copies and quads', get('gpu_present_end_frame_begin'), 'after xrBeginFrame')
        line('span: whole frame', get('gpu_work'), 'its work, at most')
        line('held after xrEndFrame', get('gpu_held'), 'the runtime\'s sync (or the mirror)')

    print(f'\n  {"game thread (ms)":<34} {"median":>7} {"p90":>7} {"max":>7}')
    begins = [t['t_begin'] for t in ticks if t.get('t_begin') is not None]
    line('tick interval', [b - a for a, b in zip(begins, begins[1:])])
    line('engine tick, wall', [t.get('engine_ms') for t in ticks], 'includes waiting for the render thread')
    line('engine tick, busy', [t.get('engine_busy_ms') for t in ticks])
    line('our tick (host + mod), wall', [t.get('host_ms') for t in ticks])
    game_work = [(t.get('engine_busy_ms') or 0.0) + (t.get('host_busy_ms') or 0.0) for t in ticks]
    line('busy, engine + ours', game_work, '<- the game thread\'s work')

    print(f'\nVerdict (medians against the headset period, {budget:.2f} ms):')
    lag = stats(get('lag_present'))
    held = stats(get('gpu_held'))
    gpu_held = held is not None and held[0] > 2.0
    gpu_bound = lag is not None and lag[0] > 2.0 and not gpu_held
    candidates = []
    for name, values in (('render thread', get('render_work')), ('game thread', game_work)):
        s = stats(values)
        if s:
            candidates.append((s[0], name))
            print(f'  {name:<14} {s[0]:6.2f} ms = {100.0 * s[0] / budget:5.1f}% of {budget:.2f} ms (p90 {s[1]:.2f})')
    gpu = stats(get('gpu_work'))
    if gpu:
        print(f'  {"GPU":<14} {gpu[0]:6.2f} ms at most = {100.0 * gpu[0] / budget:5.1f}% (p90 {gpu[1]:.2f}); '
              f'{lag[0]:.2f} ms behind the CPU at Present' if lag else '')
        if gpu_bound:
            candidates.append((gpu[0], 'GPU'))
    if gpu_held:
        print(f'  -> the GPU is held {held[0]:.2f} ms after each xrEndFrame (median): the runtime\'s frame sync, '
              'not the game\'s work.')
    if throttle > 1.5:
        print('  -> Lift the throttle (SteamVR > Video > Per-Application Video Settings > Throttling Behavior: Fixed, '
              'full rate) and measure again: that shows what the frame really costs.')
    elif candidates and interval:
        worst = max(candidates)
        periods = interval[0] / budget
        if worst[0] < 0.95 * budget and periods > 1.5:
            print(f'  -> everything fits in one period (the {worst[1]}: {worst[0]:.2f} ms), yet frames take '
                  f'{periods:.1f} periods: the runtime throttles the game (SteamVR\'s half-rate lock), or the wait '
                  'holds it.')
        else:
            verdict = 'over' if worst[0] > budget else 'within'
            print(f'  -> the {worst[1]} sets the pace: {worst[0]:.2f} ms, {verdict} the {budget:.2f} ms budget.')
        wait = stats(get('wait_wall'))
        if wait:
            print(f'  -> xrWaitFrame holds the render thread {wait[0]:.2f} ms of each {interval[0]:.2f} ms frame '
                  '(median).')


if __name__ == '__main__':
    main()

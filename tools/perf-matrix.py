# SPDX-License-Identifier: MIT
"""Summarises a settings matrix run by tools/perf-matrix.ps1 (research/sequel-render-levers.md "Performance research").

For every capture (entries.json: the combination, its round, its probe and its clock window) it reads the probe's frames
and ticks with tools/perf-probe.py's own functions and takes the median, p90, p99 and max of: the frame interval, the
render thread's work (busy without the XR waits), the GPU's span, the game thread's work (engine + ours) and the draws;
and, from the game's log, the render levers' 10 s windows that lie inside the capture (tools/perf-levers-log.py): each
eye's scene render, the second eye's Umbra results read, the scene renders' cost a frame, third renders, query reads.
Then one row per combination: the median over its rounds, with the spread (lowest..highest round).

    python tools/perf-matrix.py <entries.json> --log <game log copy> [--write summary.json]
"""
import argparse
import importlib.util
import json
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load_module(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


probe = load_module("perf_probe", "perf-probe.py")
levers = load_module("perf_levers_log", "perf-levers-log.py")


def pct(values, q):
    values = sorted(v for v in values if v is not None)
    if not values:
        return None
    return values[min(len(values) - 1, int(round(q * (len(values) - 1))))]


def summary(values):
    values = [v for v in values if v is not None]
    if not values:
        return None
    return {"median": statistics.median(values), "p90": pct(values, 0.9), "p99": pct(values, 0.99), "max": max(values),
            "n": len(values)}


def capture_stats(path):
    meta = json.load(open(path, encoding="utf-8"))
    folder = os.path.dirname(path)
    frames = probe.load_rows(os.path.join(folder, meta["files"]["frames"]))
    ticks = probe.load_rows(os.path.join(folder, meta["files"]["ticks"]))
    rows = [r for r in frames[1:] if r.get("t_start") is not None and r.get("t_end") is not None]
    vr = [r for r in rows if r.get("xr_frame")]
    rows = vr or rows
    metrics = [probe.frame_metrics(r) for r in rows]
    get = lambda key: [m.get(key) for m in metrics]
    game = [(t.get("engine_busy_ms") or 0.0) + (t.get("host_busy_ms") or 0.0) for t in ticks]
    periods = [r.get("display_period_ms") for r in frames if r.get("display_period_ms")]
    out = {
        "frames": len(rows),
        "periodMs": min(periods) if periods else None,
        "interval": summary(get("interval")),
        "renderWork": summary(get("render_work")),
        "renderBusyTotal": summary(get("render_busy_total")),
        "gpuSpan": summary(get("gpu_work")),
        "gameThread": summary(game),
        "draws": summary(get("draws")),
        "leftBusy": summary(get("left_busy")),
        "rightBusy": summary(get("right_busy")),
        "frameBounds": meta.get("frameBounds", "Present"),
        "renderLevers": meta.get("renderLevers"),
    }
    # Frames over budget: how often the render thread's work alone passes the headset period.
    budget = out["periodMs"] or (1000.0 / 90.0)
    work = [v for v in get("render_work") if v is not None]
    out["overBudgetShare"] = (sum(1 for v in work if v > budget) / len(work)) if work else None
    return out


def seconds(clock):
    h, m, s = (int(x) for x in clock.split(":"))
    return h * 3600 + m * 60 + s


def windows_in(rows, start, end):
    s, e = seconds(start), seconds(end)
    return [r for r in rows if s + 10 <= seconds(r["time"]) <= e + 1]


def window_stats(ws):
    def place(p, key):
        return [w["renders"][p].get(key) for w in ws if p in w["renders"]]

    def reads(p, key):
        return [w["reads"][p].get(key) for w in ws if p in w["reads"]]

    med = lambda v: statistics.median([x for x in v if x is not None]) if any(x is not None for x in v) else None
    return {
        "windows": len(ws),
        "first": med(place(0, "ms")), "second": med(place(1, "ms")), "third": med(place(2, "ms")),
        "secondResults": med(place(1, "results")), "secondInstances": med(place(1, "instances")),
        "firstInstances": med(place(0, "instances")),
        "sceneMsPerFrame": med([w["sceneMsPerFrame"] for w in ws]),
        "thirdShare": med([w["thirdShare"] for w in ws]),
        "secondReads": med(reads(1, "reads")), "secondUnready": med(reads(1, "unready")),
        "secondWaiting": med(reads(1, "waiting")), "firstWaiting": med(reads(0, "waiting")),
        "visible": med([w.get("visible") for w in ws]),
        # ME2 only: Halcyon's same-frame retries of pass 1 a window, and those that rendered (took 100 us or more)
        "retries": med([w.get("retries") for w in ws]), "retriesRendered": med([w.get("retriesRendered") for w in ws]),
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("entries")
    ap.add_argument("--log", required=True)
    ap.add_argument("--write")
    args = ap.parse_args(argv)
    entries = json.load(open(args.entries, encoding="utf-8-sig"))
    if isinstance(entries, dict):
        entries = [entries]
    rows = []
    with open(args.log, encoding="utf-8", errors="replace") as f:
        for line in f:
            r = levers.parse(line)
            if r is not None:
                rows.append(r)
    captures = []
    for e in entries:
        c = {"combo": e["combo"], "index": e["index"], "round": e["round"], "tag": e["tag"],
             "compilers": [e.get("compilersAtStart"), e.get("compilersAtEnd"), e.get("waitedForQuietS")],
             "snapshots": e.get("snapshots") or []}
        if e.get("probe") and os.path.exists(e["probe"]):
            c["probe"] = capture_stats(e["probe"])
        c["levers"] = window_stats(windows_in(rows, e["start"], e["end"]))
        captures.append(c)

    def val(c, *path):
        x = c
        for k in path:
            if x is None:
                return None
            x = x.get(k) if isinstance(x, dict) else None
        return x

    columns = [
        ("frame med", ("probe", "interval", "median")), ("frame p90", ("probe", "interval", "p90")),
        ("frame p99", ("probe", "interval", "p99")), ("render med", ("probe", "renderWork", "median")),
        ("render p90", ("probe", "renderWork", "p90")), ("render p99", ("probe", "renderWork", "p99")),
        ("over %", ("probe", "overBudgetShare")), ("GPU med", ("probe", "gpuSpan", "median")),
        ("GPU p90", ("probe", "gpuSpan", "p90")), ("game med", ("probe", "gameThread", "median")),
        ("draws med", ("probe", "draws", "median")), ("draws p90", ("probe", "draws", "p90")),
        ("#0 ms", ("levers", "first")), ("#1 ms", ("levers", "second")), ("#1 res", ("levers", "secondResults")),
        ("#1 inst", ("levers", "secondInstances")), ("scene/fr", ("levers", "sceneMsPerFrame")),
        ("3rd %", ("levers", "thirdShare")), ("#1 unrdy", ("levers", "secondUnready")),
        ("#1 wait", ("levers", "secondWaiting")), ("visible", ("levers", "visible")),
        ("retries", ("levers", "retries")), ("retry ok", ("levers", "retriesRendered")),
    ]
    scale = {"over %": 100.0, "3rd %": 100.0}

    def fmt(x, name):
        if x is None:
            return "-"
        x = x * scale.get(name, 1.0)
        return f"{x:.0f}" if abs(x) >= 1000 or name in ("draws med", "draws p90", "#1 unrdy", "#1 wait", "visible", "retries", "retry ok") else f"{x:.2f}"

    print("per capture:")
    print(f"{'capture':24} " + " ".join(f"{n:>9}" for n, _ in columns) + "  compilers")
    for c in captures:
        print(f"{c['tag']:24} " + " ".join(f"{fmt(val(c, *p), n):>9}" for n, p in columns) + f"  {c['compilers']}")

    combos = []
    for index in sorted({c["index"] for c in captures}):
        mine = [c for c in captures if c["index"] == index]
        row = {"combo": mine[0]["combo"], "rounds": len(mine)}
        for name, path in columns:
            vs = [val(c, *path) for c in mine if val(c, *path) is not None]
            row[name] = {"median": statistics.median(vs), "low": min(vs), "high": max(vs)} if vs else None
        combos.append(row)
    print("\nper combination (median over rounds, [lowest..highest]):")
    for row in combos:
        parts = []
        for name, _ in columns:
            v = row[name]
            if v is None:
                continue
            parts.append(f"{name} {fmt(v['median'], name)} [{fmt(v['low'], name)}..{fmt(v['high'], name)}]")
        print(f"  {row['combo']} ({row['rounds']} rounds):\n    " + "; ".join(parts))
    if args.write:
        with open(args.write, "w", encoding="utf-8") as f:
            json.dump({"captures": captures, "combos": combos}, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())

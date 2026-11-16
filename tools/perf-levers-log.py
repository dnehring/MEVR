# SPDX-License-Identifier: MIT
"""Tabulates plugin/RenderLevers' 10-second lines ("render levers: last 10 s: ...") of a game's log (LE1MEVR.log,
LE2MEVR.log, LE3MEVR.log): per window the frames, how many scene renders a frame had, each render's ms by its place in
the frame (#0 the first eye, #1 the second, #2 a third), its Umbra resolve's parts, the query reads (and how many found
no result), the visible instances and the bound shader states created. Then the medians over the windows chosen.

"scene ms a frame" weights each place by the share of frames that had it: what the scene renders cost the render
thread per frame on average (a third render on 3% of the frames adds 3% of its time).

    python tools/perf-levers-log.py <log> [--from HH:MM:SS] [--to HH:MM:SS] [--last N] [--json]
    python tools/perf-levers-log.py "<ME2>\\Binaries\\Win64\\Logs\\LE2MEVR.log" --from 14:21:27 --to 14:21:50

research/sequel-render-levers.md "Performance research" uses it for every A/B: a capture's window is its probe's start
(the -probe.json's name) plus its seconds; take the windows that end inside it.
"""
import argparse
import json
import re
import statistics
import sys

LINE = re.compile(r"^\[(\d\d:\d\d:\d\d)\.\d+\] \[info\] render levers: last 10 s: (\d+) frames; (.*)$")
DIST = re.compile(r"scene renders per frame ([^;]*);")
DIST_ITEM = re.compile(r"(\d+) \(x(\d+)\)")
RENDER = re.compile(r"#(\d) ([0-9.]+) ms(?: \[Umbra resolve ([0-9.]+) ms: instances ([0-9.]+) ms \(\d+ calls\), "
                    r"occluder flush \+ queries ([0-9.]+) ms \(\d+ calls\), query results ([0-9.]+) ms \(\d+ calls\), "
                    r"other ([0-9.]+) ms \(\d+ calls\); longest results read ([0-9.]+) ms\])?")
READS = re.compile(r"query reads a frame: ([^;]*);")
READ_ITEM = re.compile(r"#(\d) (\d+) \((\d+) waiting, (\d+) without a result\)")
VISIBLE = re.compile(r"visible instances a frame (\d+), culled (\d+)")
BSS = re.compile(r"bound shader states: ([0-9.]+) created a frame")
RETRIES = re.compile(r"ME2's same-frame replay retries: (\d+) \((\d+) of them")


def parse(line):
    m = LINE.match(line.strip())
    if not m:
        return None
    clock, frames, rest = m.group(1), int(m.group(2)), m.group(3)
    row = {"time": clock, "frames": frames, "dist": {}, "renders": {}, "reads": {}}
    d = DIST.search(rest)
    if d:
        row["dist"] = {int(k): int(v) for k, v in DIST_ITEM.findall(d.group(1))}
    per = rest.split("query reads a frame:")[0]
    for r in RENDER.finditer(per):
        place = int(r.group(1))
        entry = {"ms": float(r.group(2))}
        if r.group(3) is not None:
            entry.update(resolve=float(r.group(3)), instances=float(r.group(4)), flush=float(r.group(5)),
                         results=float(r.group(6)), other=float(r.group(7)), longest=float(r.group(8)))
        row["renders"][place] = entry
    q = READS.search(rest)
    if q:
        for place, reads, waiting, unready in READ_ITEM.findall(q.group(1)):
            row["reads"][int(place)] = {"reads": int(reads), "waiting": int(waiting), "unready": int(unready)}
    v = VISIBLE.search(rest)
    if v:
        row["visible"], row["culled"] = int(v.group(1)), int(v.group(2))
    b = BSS.search(rest)
    if b:
        row["bssCreated"] = float(b.group(1))
    rt = RETRIES.search(rest)
    if rt:
        row["retries"], row["retriesRendered"] = int(rt.group(1)), int(rt.group(2))
    # Each place's share of the frames: frames that had at least place + 1 renders.
    total = sum(row["dist"].values()) or frames
    shares = {}
    for place in row["renders"]:
        shares[place] = sum(n for k, n in row["dist"].items() if k > place) / total if total else 0.0
    row["shares"] = shares
    row["sceneMsPerFrame"] = sum(row["renders"][p]["ms"] * shares[p] for p in row["renders"])
    row["thirdShare"] = shares.get(2, 0.0)
    return row


def seconds(clock):
    h, m, s = (int(x) for x in clock.split(":"))
    return h * 3600 + m * 60 + s


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--from", dest="start", help="first window's end time, HH:MM:SS (the log's clock)")
    ap.add_argument("--to", dest="end", help="last window's end time, HH:MM:SS")
    ap.add_argument("--last", type=int, default=0, help="only the last N windows (after --from/--to)")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    rows = []
    with open(args.log, encoding="utf-8", errors="replace") as f:
        for line in f:
            row = parse(line)
            if row is None:
                continue
            t = seconds(row["time"])
            if args.start and t < seconds(args.start):
                continue
            if args.end and t > seconds(args.end):
                continue
            rows.append(row)
    if args.last:
        rows = rows[-args.last:]
    if args.json:
        json.dump(rows, sys.stdout, indent=1)
        return 0
    if not rows:
        print("no 'render levers: last 10 s' lines in that window")
        return 1

    def g(row, place, key):
        e = row["renders"].get(place)
        return e.get(key) if e else None

    def r(row, place, key):
        e = row["reads"].get(place)
        return e.get(key) if e else None

    def f(x, fmt="{:.2f}"):
        return "-" if x is None else fmt.format(x)

    print(f"{'end':8} {'frames':>6} {'renders/frame':16} {'#0 ms':>6} {'#0 res':>6} {'#1 ms':>6} {'#1 res':>6} "
          f"{'#1 inst':>7} {'#2 ms':>6} {'3rd %':>5} {'scene/fr':>8} {'#1 reads':>8} {'#1 unrdy':>8} {'#1 wait':>7} "
          f"{'visible':>7} {'bss/fr':>6} {'retries':>9}")
    for row in rows:
        dist = ",".join(f"{k}:{v}" for k, v in sorted(row["dist"].items()))
        print(f"{row['time']:8} {row['frames']:>6} {dist:16} {f(g(row, 0, 'ms')):>6} {f(g(row, 0, 'results')):>6} "
              f"{f(g(row, 1, 'ms')):>6} {f(g(row, 1, 'results')):>6} {f(g(row, 1, 'instances')):>7} {f(g(row, 2, 'ms')):>6} "
              f"{row['thirdShare'] * 100:>5.1f} {row['sceneMsPerFrame']:>8.2f} {f(r(row, 1, 'reads'), '{}'):>8} "
              f"{f(r(row, 1, 'unready'), '{}'):>8} {f(r(row, 1, 'waiting'), '{}'):>7} {f(row.get('visible'), '{}'):>7} "
              f"{f(row.get('bssCreated'), '{:.1f}'):>6} "
              f"{(str(row['retries']) + '/' + str(row['retriesRendered'])) if 'retries' in row else '-':>9}")

    def med(values):
        values = [v for v in values if v is not None]
        return statistics.median(values) if values else None

    print(f"\nmedians over {len(rows)} windows: #0 {f(med(g(x, 0, 'ms') for x in rows))} ms, "
          f"#1 {f(med(g(x, 1, 'ms') for x in rows))} ms (its query results {f(med(g(x, 1, 'results') for x in rows))} ms, "
          f"instances {f(med(g(x, 1, 'instances') for x in rows))} ms), "
          f"scene renders a frame {f(med(x['sceneMsPerFrame'] for x in rows))} ms, "
          f"third renders {f(med(x['thirdShare'] * 100 for x in rows), '{:.1f}')}% of frames "
          f"({f(med(g(x, 2, 'ms') for x in rows))} ms each), "
          f"#1 query reads {f(med(r(x, 1, 'reads') for x in rows), '{:.0f}')} "
          f"({f(med(r(x, 1, 'unready') for x in rows), '{:.0f}')} without a result, "
          f"{f(med(r(x, 1, 'waiting') for x in rows), '{:.0f}')} waiting), "
          f"visible instances {f(med(x.get('visible') for x in rows), '{:.0f}')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

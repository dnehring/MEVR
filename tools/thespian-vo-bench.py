# SPDX-License-Identifier: MIT
"""Thespian mode's speech bench (docs/thespian-mode-plan.md phase 0, research/70): the recognizer over Shepard's own
recordings of the lines, then the matcher over what it heard.

  python tools/thespian-vo-bench.py [--vo scratch/thespian/vo] [--cases scratch/thespian/cases.tsv] [--arch small]
                                    [--models models/voice] [--keyterms none|global] [--interval 250] [--limit N]

Steps (each one's command is printed):
  1. mevr-voice-bench (Moonshine's streaming model, one thread as in the game) over the WAVs of <vo>/manifest.tsv
     (conversation-scan --vo-extract's: wav, seconds, banks, string id, take M|F, wheel id, card index, line), in batches;
     its "final" transcript per file is kept in <vo>/transcripts-<arch>[-keyterms].tsv (a second run reuses it).
  2. The word error rate of the transcripts against the lines, per take (the male and the female player's voice).
  3. <vo>/utterances-<arch>...tsv: wheel id <TAB> card index <TAB> transcript, and mevr-thespian-bench over them: is the
     right card decided from what the recognizer wrote, and after how many words.
--keyterms global biases the recognizer toward the game's names (the capitalised words inside the cards' sentences, the
60 most frequent). --keyterms wheel is what the game will do: each wheel's own names (mevr-thespian-bench --keyterms-out,
written to <vo>/keyterms.tsv) as the bias, one recognizer run per wheel; it re-runs only the recordings the plain run
(transcripts-<arch>.tsv, made first) did not hear word for word, and reports how many of those the matcher now decides.
The WAVs and transcripts are game data: keep them out of the repository (scratch/ is ignored).
"""
import argparse
import collections
import os
import pathlib
import re
import subprocess
import sys

LINE = re.compile(r'^(.*?\.wav) \(([\d.]+) s\): final ([\d.-]+) ms after the audio\'s end -> "(.*)"; (\d+) partials, mean ([\d.]+) ms, worst ([\d.]+) ms')


def words(text):
    return [w for w in re.sub(r"[^a-z0-9' ]+", " ", text.lower().replace("’", "'")).split() if w]


def edit_distance(a, b):
    prev = list(range(len(b) + 1))
    for i, x in enumerate(a, 1):
        cur = [i]
        for j, y in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (0 if x == y else 1)))
        prev = cur
    return prev[-1]


def global_keyterms(cases, count):
    seen = collections.Counter()
    for row in open(cases, encoding="utf-8"):
        f = row.rstrip("\n").split("\t")
        if len(f) < 7:
            continue
        for sentence in re.split(r"[.?!:]\s+", f[6]):
            for w in re.findall(r"[A-Za-z][A-Za-z']+", sentence)[1:]:
                if w[0].isupper() and len(w) >= 4 and w.lower() not in ("shepard",):
                    seen[w.rstrip("'s") if w.endswith("'s") else w] += 1
    return [w for w, n in seen.most_common(count)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vo", default="scratch/thespian/vo")
    ap.add_argument("--cases", default="scratch/thespian/cases.tsv")
    ap.add_argument("--arch", default="small")
    ap.add_argument("--models", default="models/voice")
    ap.add_argument("--keyterms", default="none", choices=["none", "global", "wheel"])
    ap.add_argument("--interval", type=int, default=250)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--bench", default="build/Release/mevr-voice-bench.exe")
    ap.add_argument("--matcher", default="build/Release/mevr-thespian-bench.exe")
    ap.add_argument("--batch", type=int, default=60)
    args = ap.parse_args()

    vo = pathlib.Path(args.vo)
    manifest = [l.rstrip("\n").split("\t") for l in open(vo / "manifest.tsv", encoding="utf-8") if l.strip()]
    if args.limit:
        manifest = manifest[:args.limit]
    tag = args.arch + ("-keyterms" if args.keyterms != "none" else "")
    kept = vo / f"transcripts-{tag}.tsv"
    heard = {}
    timing = {}
    if kept.exists():
        for l in open(kept, encoding="utf-8"):
            f = l.rstrip("\n").split("\t")
            if len(f) >= 5:
                heard[f[0]] = f[1]
                timing[f[0]] = (float(f[2]), float(f[3]), float(f[4]))
    todo = [m[0] for m in manifest if m[0] not in heard]
    if args.keyterms == "wheel":
        plain = {}
        for l in open(vo / f"transcripts-{args.arch}.tsv", encoding="utf-8"):
            f = l.rstrip("\n").split("\t")
            if len(f) >= 5:
                plain[f[0]] = (f[1], (float(f[2]), float(f[3]), float(f[4])))
        terms_path = vo / "keyterms.tsv"
        subprocess.run([str(pathlib.Path(args.matcher).resolve()), args.cases, "--keyterms-out", str(terms_path)], check=True, capture_output=True)
        terms = dict(l.rstrip("\n").split("\t", 1) for l in open(terms_path, encoding="utf-8") if "\t" in l)
        by_wheel = collections.defaultdict(list)
        misheard = 0
        for m in manifest:
            if m[0] not in plain:
                continue
            if words(plain[m[0]][0]) == words(m[7]) or not terms.get(m[5]):
                heard.setdefault(m[0], plain[m[0]][0])  # heard right, or a wheel without names: nothing to bias
                timing.setdefault(m[0], plain[m[0]][1])
            else:
                misheard += 1
                if m[0] not in heard:
                    by_wheel[m[5]].append(m[0])
        env = dict(os.environ, MOONSHINE_ORT_SINGLE_THREAD="1")
        print(f"{misheard} recordings not heard word for word on wheels with names; {sum(len(v) for v in by_wheel.values())} to run, one run per wheel", flush=True)
        with open(kept, "a", encoding="utf-8", newline="\n") as out:
            for n, (wheel, names) in enumerate(by_wheel.items(), 1):
                base = [str(pathlib.Path(args.bench).resolve()), "--models", str(pathlib.Path(args.models) / f"{args.arch}-streaming-en"),
                        "--arch", args.arch, "--interval", str(args.interval), "--keyterms", terms[wheel]]
                result = subprocess.run(base + [str(vo / name) for name in names], capture_output=True, text=True, encoding="utf-8",
                                        errors="replace", env=env)
                for line in result.stdout.splitlines():
                    m = LINE.match(line)
                    if not m:
                        continue
                    name = pathlib.Path(m.group(1)).name
                    heard[name] = m.group(4).replace(" | ", " ")
                    timing[name] = (float(m.group(3)), float(m.group(6)), float(m.group(7)))
                    out.write(f"{name}\t{heard[name]}\t{m.group(3)}\t{m.group(6)}\t{m.group(7)}\n")
                out.flush()
                if n % 50 == 0:
                    print(f"  {n} of {len(by_wheel)} wheels", flush=True)
        todo = []
    if todo:
        env = dict(os.environ, MOONSHINE_ORT_SINGLE_THREAD="1")  # one thread, as the game runs the recognizer
        base = [str(pathlib.Path(args.bench).resolve()), "--models", str(pathlib.Path(args.models) / f"{args.arch}-streaming-en"), "--arch", args.arch,
                "--interval", str(args.interval)]
        if args.keyterms == "global":
            base += ["--keyterms", ",".join(global_keyterms(args.cases, 60))]
        print(" ".join(base), f"<{len(todo)} files in batches of {args.batch}>", flush=True)
        with open(kept, "a", encoding="utf-8", newline="\n") as out:
            for start in range(0, len(todo), args.batch):
                batch = todo[start:start + args.batch]
                result = subprocess.run(base + [str(vo / name) for name in batch], capture_output=True, text=True, encoding="utf-8",
                                        errors="replace", env=env)
                for line in result.stdout.splitlines():
                    m = LINE.match(line)
                    if not m:
                        continue
                    name = pathlib.Path(m.group(1)).name
                    text = m.group(4).replace(" | ", " ")
                    heard[name] = text
                    timing[name] = (float(m.group(3)), float(m.group(6)), float(m.group(7)))
                    out.write(f"{name}\t{text}\t{m.group(3)}\t{m.group(6)}\t{m.group(7)}\n")
                out.flush()
                print(f"  {min(start + args.batch, len(todo))} of {len(todo)}", flush=True)
    # 2. Word error rate per take.
    errors = collections.Counter()
    total = collections.Counter()
    exact = collections.Counter()
    count = collections.Counter()
    utterances = {"all": [], "M": [], "F": []}
    for name, seconds, banks, ref, take, wheel, card, line in manifest:
        if name not in heard:
            continue
        want, got = words(line), words(heard[name])
        errors[take] += edit_distance(want, got)
        total[take] += len(want)
        exact[take] += 1 if want == got else 0
        count[take] += 1
        row = f"{wheel}\t{card}\t{heard[name]}"
        utterances["all"].append(row)
        utterances[take].append(row)
    print(f"\nrecognizer: {args.arch}, key terms {args.keyterms}, partials every {args.interval} ms, one thread")
    for take in sorted(count):
        print(f"  Shepard ({'male' if take == 'M' else 'female'}): {count[take]} lines, word error rate {100.0 * errors[take] / max(1, total[take]):.1f}%, "
              f"heard word for word {100.0 * exact[take] / count[take]:.1f}%")
    if timing:
        finals = sorted(t[0] for t in timing.values())
        means = sorted(t[1] for t in timing.values())
        worsts = sorted(t[2] for t in timing.values())
        print(f"  final text after the audio's end (fed faster than real time): median {finals[len(finals) // 2]:.0f} ms; "
              f"a partial costs median {means[len(means) // 2]:.0f} ms, p95 of the worst per file {worsts[len(worsts) * 95 // 100]:.0f} ms")
    # 3. The matcher over what was heard.
    for key in ("all", "M", "F"):
        if not utterances[key]:
            continue
        path = vo / f"utterances-{tag}-{key}.tsv"
        path.write_text("\n".join(utterances[key]) + "\n", encoding="utf-8")
        result = subprocess.run([str(pathlib.Path(args.matcher).resolve()), args.cases, "--foreign", "0", "--failures", "12" if key == "all" else "0", "--utterances", str(path)],
                                capture_output=True, text=True, encoding="utf-8", errors="replace")
        text = result.stdout
        at = text.find("heard: a recognizer's transcripts")
        print(f"\n[{key}] {args.matcher} {args.cases} --utterances {path}")
        print(text[at:] if at >= 0 else text)
    return 0


if __name__ == "__main__":
    sys.exit(main())

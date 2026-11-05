# SPDX-License-Identifier: MIT
"""The recognizer's accuracy and latency over a spoken corpus (research/54 step 2): runs mevr-voice-bench per model over the
WAVs of a corpus folder (tools/voice-tts-corpus.ps1's, or any folder with a lines.tsv of "<file>\t<text>"), scores each
transcript against its line (word error rate after normalisation; the squadmates' and powers' names counted on their own),
and prints a table per model: WER, name accuracy, the final's delay after the audio's end (median, p95), the partials'
cost, the load time. --realtime feeds the audio at its own pace (the latency figures of a live microphone); without it the
files are fed as fast as the models take them (accuracy only; the delays are then meaningless).

  python tools/voice-bench.py [--corpus scratch/voice-corpus] [--models tiny,small,medium] [--keyterms a,b,...]
                              [--no-keyterms] [--realtime] [--bench build/Release/mevr-voice-bench.exe] [--limit N]
"""
import argparse
import pathlib
import re
import subprocess
import sys

NAMES = {"garrus", "wrex", "liara", "tali", "ashley", "kaidan", "shepard", "overload", "sabotage", "damping", "lift", "throw",
         "singularity", "warp", "stasis", "barrier", "marksman", "carnage", "immunity", "assassination", "unity"}
DEFAULT_KEYTERMS = "Garrus,Wrex,Liara,Tali,Ashley,Kaidan,Shepard,Overload,Sabotage,Damping,Lift,Throw,Singularity,Warp,Stasis,Barrier,Neural Shock,Marksman,Carnage,Adrenaline Burst,Immunity,Assassination,Shield Boost"


def words(text):
    text = text.lower().replace("|", " ")
    text = re.sub(r"[^a-z0-9' ]+", " ", text)
    return [w for w in text.split() if w]


def edit_distance(a, b):
    prev = list(range(len(b) + 1))
    for i, x in enumerate(a, 1):
        cur = [i]
        for j, y in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (0 if x == y else 1)))
        prev = cur
    return prev[-1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", default="scratch/voice-corpus")
    ap.add_argument("--models", default="tiny,small,medium")
    ap.add_argument("--models-dir", default="models/voice")
    ap.add_argument("--bench", default="build/Release/mevr-voice-bench.exe")
    ap.add_argument("--keyterms", default=DEFAULT_KEYTERMS)
    ap.add_argument("--no-keyterms", action="store_true")
    ap.add_argument("--realtime", action="store_true")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--interval", type=int, default=150)
    ap.add_argument("--boost", default="", help="keyterm_boost (the library's default 2.0)")
    args = ap.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent  # paths are relative to the repository, wherever this runs from
    def at(p):
        q = pathlib.Path(p)
        return q if q.is_absolute() else root / q
    args.bench = str(at(args.bench))
    args.models_dir = str(at(args.models_dir))
    corpus = at(args.corpus)
    lines = {}
    for row in (corpus / "lines.tsv").read_text(encoding="utf-8-sig").splitlines():
        if "\t" in row:
            f, t = row.split("\t", 1)
            lines[f] = t
    files = sorted(lines)
    if args.limit:
        files = files[: args.limit]
    print(f"{len(files)} files from {corpus}; keyterms {'off' if args.no_keyterms else 'on'}; {'real time' if args.realtime else 'as fast as possible'}")
    for model in args.models.split(","):
        cmd = [args.bench, "--models", str(pathlib.Path(args.models_dir) / f"{model}-streaming-en"), "--arch", model, "--interval", str(args.interval)]
        if not args.no_keyterms:
            cmd += ["--keyterms", args.keyterms]
            if args.boost:
                cmd += ["--boost", args.boost]
        if args.realtime:
            cmd.append("--realtime")
        cmd += [str(corpus / f) for f in files]
        out = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if out.returncode != 0:
            print(f"{model}: the bench failed: {out.stderr.strip()}")
            continue
        load_ms = None
        finals, partial_means, partial_worsts = [], [], []
        err_words = ref_words = 0
        name_hits = name_total = 0
        exact = 0
        misses = []
        for line in out.stdout.splitlines():
            m = re.match(r"loaded \S+ \(.*\) in (\d+) ms", line)
            if m:
                load_ms = int(m.group(1))
                continue
            m = re.match(r"(.+?) \(([\d.]+) s\): final (\d+) ms after the audio's end -> \"(.*)\"; (\d+) partials, mean (\d+) ms, worst (\d+) ms", line)
            if not m:
                continue
            name = pathlib.Path(m.group(1)).name
            got = m.group(4)
            finals.append(int(m.group(3)))
            partial_means.append(int(m.group(6)))
            partial_worsts.append(int(m.group(7)))
            ref = words(lines.get(name, ""))
            hyp = words(got)
            d = edit_distance(ref, hyp)
            err_words += d
            ref_words += len(ref)
            if d == 0:
                exact += 1
            else:
                misses.append((name, lines.get(name, ""), got))
            for w in ref:
                if w in NAMES:
                    name_total += 1
                    if w in hyp:
                        name_hits += 1
        if not finals:
            print(f"{model}: nothing parsed from the bench's output:\n{out.stdout[:2000]}")
            continue
        finals.sort()
        wer = 100.0 * err_words / max(ref_words, 1)
        print(f"\n{model}: load {load_ms} ms; {len(finals)} files; WER {wer:.1f}% ({err_words}/{ref_words} words), {exact}/{len(finals)} exact; "
              f"names {name_hits}/{name_total} ({100.0 * name_hits / max(name_total, 1):.0f}%); final after the audio's end median {finals[len(finals) // 2]} ms, "
              f"p95 {finals[min(len(finals) - 1, len(finals) * 95 // 100)]} ms, worst {finals[-1]} ms; partials mean {sum(partial_means) / len(partial_means):.0f} ms, "
              f"worst {max(partial_worsts)} ms")
        for name, ref, got in misses[:12]:
            print(f"    {name}: said \"{ref}\" -> heard \"{got}\"")
        if len(misses) > 12:
            print(f"    ... {len(misses) - 12} more")
    return 0


if __name__ == "__main__":
    sys.exit(main())

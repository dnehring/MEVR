# SPDX-License-Identifier: MIT
"""The fine-tune as a repeatable pipeline (research/54 "The fine-tune"): one JSON config per run, one folder per run under
scratch/voice-model/runs/<name>/ holding the config, the datasets (regenerated only when their part of the config changed),
the training log, the LoRA, the merged model, the GGUFs, the eval and bench results, and a `done.json` per step so a
failed or interrupted run resumes where it stopped (`--force <step>` redoes one). `runs.tsv` at the runs' root collects every
run's figures for the hyperparameter table. A sweep is several configs from one base: `--sweep rank=8,16,32 lr=1e-4,2e-4`.

  python tools/voice-model/pipeline.py run --config tools/voice-model/configs/base.json [--name run1] [--force train]
  python tools/voice-model/pipeline.py sweep --config ... rank=8,16,32 lr=1e-4,2e-4
  python tools/voice-model/pipeline.py table        # runs.tsv as a table

Steps: dataset (make_dataset.py, train and eval), train (train_lora.py in the venv: LoRA, merge), gguf (llama.cpp's
convert script, q8_0 and the extra types listed), eval (train_lora's exact-match over the eval set, in the venv), bench
(mevr-llm-bench on each GGUF under the grammars: exact match and the CPU times). Nothing here needs the game.
"""
import argparse
import copy
import hashlib
import itertools
import json
import os
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
HERE = pathlib.Path(__file__).resolve().parent
RUNS = ROOT / "scratch" / "voice-model" / "runs"
VENV_PY = ROOT / ".venv-voice" / "Scripts" / "python.exe"

DEFAULT = {
    "base": "Qwen/Qwen3-0.6B",
    "dataset": {"count": 20000, "seed": 1, "noise": 0.3, "eval_count": 1000, "eval_seed": 2},
    "train": {"rank": 16, "alpha": 32, "lr": 2e-4, "epochs": 2, "batch": 16, "micro_batch": 4, "max_len": 512, "warmup": 0.03, "weight_decay": 0.01, "seed": 1},
    "gguf": {"types": ["q8_0", "q4_k_m"]},
    "bench": {"threads": 4, "limit": 300},
}


def deep_update(a, b):
    for k, v in b.items():
        if isinstance(v, dict) and isinstance(a.get(k), dict):
            deep_update(a[k], v)
        else:
            a[k] = v
    return a


def digest(obj):
    return hashlib.sha1(json.dumps(obj, sort_keys=True).encode()).hexdigest()[:10]


def done_path(run, step):
    return run / f"done-{step}.json"


def is_done(run, step, key):
    p = done_path(run, step)
    if not p.exists():
        return False
    return json.loads(p.read_text()).get("key") == key


def mark_done(run, step, key, extra=None):
    done_path(run, step).write_text(json.dumps({"key": key, "at": time.strftime("%Y-%m-%d %H:%M:%S"), **(extra or {})}, indent=1))


def sh(cmd, log=None, cwd=None):
    print("$", " ".join(str(c) for c in cmd), flush=True)
    with (open(log, "a", encoding="utf-8") if log else open(os.devnull, "w")) as out:
        out.write("$ " + " ".join(str(c) for c in cmd) + "\n")
        out.flush()
        # Unbuffered: a child's print() through a pipe is block-buffered, so run1's loss lines reached train.log 8 KB at a
        # time, long after the progress bar (stderr) showed the step.
        env = {**os.environ, "PYTHONUNBUFFERED": "1", "PYTHONIOENCODING": "utf-8"}
        proc = subprocess.Popen([str(c) for c in cmd], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", cwd=cwd, env=env)
        tail = []
        for line in proc.stdout:
            out.write(line)
            out.flush()
            tail.append(line.rstrip())
            if len(tail) > 40:
                tail.pop(0)
            if log is None or re.search(r"loss|eval:|exact|error|Error|Traceback|GGUF:|prompts:", line):
                print(line.rstrip(), flush=True)
        proc.wait()
    if proc.returncode != 0:
        raise SystemExit(f"step failed ({proc.returncode}): {' '.join(str(c) for c in cmd)}\n" + "\n".join(tail[-15:]))
    return tail


def step_dataset(run, cfg, force):
    d = cfg["dataset"]
    key = digest(d)
    if is_done(run, "dataset", key) and not force:
        return
    gen = HERE / "make_dataset.py"
    sh([sys.executable, "-I", gen, "--out", run / "train.jsonl", "--count", d["count"], "--seed", d["seed"], "--noise", d["noise"]], run / "dataset.log")
    sh([sys.executable, "-I", gen, "--out", run / "eval.jsonl", "--count", d["eval_count"], "--seed", d["eval_seed"], "--noise", d["noise"]], run / "dataset.log")
    mark_done(run, "dataset", key)


def step_train(run, cfg, force):
    t = cfg["train"]
    key = digest({"train": t, "base": cfg["base"], "dataset": cfg["dataset"]})
    if is_done(run, "train", key) and not force:
        return
    if not VENV_PY.exists():
        raise SystemExit(f"no venv at {VENV_PY}: run tools/voice-model/setup-env.ps1")
    sh([VENV_PY, HERE / "train_lora.py", "--data", run / "train.jsonl", "--out", run, "--base", cfg["base"], "--epochs", t["epochs"], "--rank", t["rank"],
        "--alpha", t["alpha"], "--lr", t["lr"], "--batch", t["batch"], "--micro-batch", t.get("micro_batch", 4), "--max-len", t["max_len"], "--warmup", t["warmup"], "--weight-decay", t["weight_decay"],
        "--seed", t["seed"]], run / "train.log")
    mark_done(run, "train", key)


def step_eval(run, cfg, force):
    key = digest({"train": cfg["train"], "base": cfg["base"], "dataset": cfg["dataset"]})
    if is_done(run, "eval", key) and not force:
        return json.loads(done_path(run, "eval").read_text()).get("result")
    tail = sh([VENV_PY, HERE / "train_lora.py", "--eval-only", run / "merged", "--eval", run / "eval.jsonl", "--out", run], run / "eval.log")
    result = next((line for line in tail if line.startswith("eval:")), "")
    mark_done(run, "eval", key, {"result": result})
    return result


def step_gguf(run, cfg, force):
    convert = ROOT / "external" / "llama-cpp" / "convert" / "convert_hf_to_gguf.py"
    if not convert.exists():
        raise SystemExit(f"no {convert}: run tools/voice-model/setup-env.ps1")
    out = []
    for t in cfg["gguf"]["types"]:
        key = digest({"train": cfg["train"], "base": cfg["base"], "dataset": cfg["dataset"], "type": t})
        gguf = run / f"model-{t}.gguf"
        out.append(gguf)
        if is_done(run, f"gguf-{t}", key) and gguf.exists() and not force:
            continue
        if t in ("q8_0", "f16", "bf16", "f32"):
            sh([VENV_PY, convert, run / "merged", "--outfile", gguf, "--outtype", t], run / "gguf.log")
        else:
            # The k-quants come from llama-quantize over the f16 file.
            f16 = run / "model-f16.gguf"
            if not f16.exists():
                sh([VENV_PY, convert, run / "merged", "--outfile", f16, "--outtype", "f16"], run / "gguf.log")
            quantize = ROOT / "external" / "llama-cpp" / "llama-quantize.exe"
            if not quantize.exists():
                print(f"no {quantize}: skipping {t}")
                out.pop()
                continue
            sh([quantize, f16, gguf, t.upper()], run / "gguf.log")
        mark_done(run, f"gguf-{t}", key)
    return out


def step_bench(run, cfg, ggufs, force):
    bench = ROOT / "build" / "Release" / "mevr-llm-bench.exe"
    results = {}
    for gguf in ggufs:
        key = digest({"gguf": gguf.name, "bench": cfg["bench"], "train": cfg["train"], "dataset": cfg["dataset"]})
        if is_done(run, f"bench-{gguf.stem}", key) and not force:
            results[gguf.name] = json.loads(done_path(run, f"bench-{gguf.stem}").read_text()).get("result")
            continue
        tail = sh([bench, "--lib", ROOT / "external" / "llama-cpp", "--model", gguf, "--data", run / "eval.jsonl", "--threads", cfg["bench"]["threads"], "--limit",
                   cfg["bench"]["limit"], "--show", 8], run / "bench.log")
        line = next((l for l in tail if " prompts: " in l), "")
        results[gguf.name] = line
        mark_done(run, f"bench-{gguf.stem}", key, {"result": line})
    return results


def record(run, cfg, eval_result, bench_results):
    RUNS.mkdir(parents=True, exist_ok=True)
    table = RUNS / "runs.tsv"
    if not table.exists():
        table.write_text("run\tbase\tcount\tnoise\trank\talpha\tlr\tepochs\tbatch\teval\tbench\n", encoding="utf-8")
    t = cfg["train"]
    bench = " | ".join(f"{k}: {v}" for k, v in bench_results.items())
    with table.open("a", encoding="utf-8") as f:
        f.write(f"{run.name}\t{cfg['base']}\t{cfg['dataset']['count']}\t{cfg['dataset']['noise']}\t{t['rank']}\t{t['alpha']}\t{t['lr']}\t{t['epochs']}\t{t['batch']}\t{eval_result}\t{bench}\n")


def run_one(cfg, name, force):
    run = RUNS / name
    run.mkdir(parents=True, exist_ok=True)
    (run / "config.json").write_text(json.dumps(cfg, indent=1), encoding="utf-8")
    print(f"== run {name} -> {run}")
    step_dataset(run, cfg, force == "dataset")
    step_train(run, cfg, force == "train")
    eval_result = step_eval(run, cfg, force == "eval")
    ggufs = step_gguf(run, cfg, force == "gguf")
    bench_results = step_bench(run, cfg, ggufs, force == "bench")
    record(run, cfg, eval_result, bench_results)
    print(f"== {name}: {eval_result}; " + "; ".join(f"{k}: {v}" for k, v in bench_results.items()))


def load_config(path):
    cfg = copy.deepcopy(DEFAULT)
    if path:
        deep_update(cfg, json.loads(pathlib.Path(path).read_text(encoding="utf-8")))
    return cfg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("command", choices=["run", "sweep", "table"])
    ap.add_argument("--config", default="")
    ap.add_argument("--name", default="")
    ap.add_argument("--force", default="", help="redo one step: dataset, train, eval, gguf, bench")
    ap.add_argument("grid", nargs="*", help="sweep: key=v1,v2 (rank, alpha, lr, epochs, batch, count, noise)")
    args = ap.parse_args()
    if args.command == "table":
        print((RUNS / "runs.tsv").read_text(encoding="utf-8") if (RUNS / "runs.tsv").exists() else "no runs yet")
        return 0
    cfg = load_config(args.config)
    if args.command == "run":
        run_one(cfg, args.name or f"run-{digest(cfg)}", args.force)
        return 0
    axes = []
    for item in args.grid:
        key, values = item.split("=", 1)
        parsed = []
        for v in values.split(","):
            parsed.append(json.loads(v) if re.fullmatch(r"-?[\d.e+-]+", v) else v)
        axes.append((key, parsed))
    for combo in itertools.product(*[v for _, v in axes]):
        c = copy.deepcopy(cfg)
        parts = []
        for (key, _), value in zip(axes, combo):
            section = "dataset" if key in ("count", "noise", "seed") else "train"
            c[section][key] = value
            parts.append(f"{key}{value}")
        run_one(c, (args.name + "-" if args.name else "") + "-".join(parts), args.force)
    return 0


if __name__ == "__main__":
    sys.exit(main())

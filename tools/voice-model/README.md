# The model tier (research/54): data, fine-tune, runtime

The rule parser (`core/vr/SquadParser`) takes the orders it can; what it leaves open (`unresolved` words, a confidence
under `[SquadOrders] minConfidence`) goes to a small language model that answers with one action line under a per-scene
grammar. This folder makes that model.

| Step | Tool | What |
|---|---|---|
| 1 | `python tools/voice-model/make_dataset.py --out scratch/voice-model/train.jsonl --count 20000` | synthetic data from the action grammar: sampled scenes, gold lines, phrasings with fillers, restarts, compounds, deictics, talk (`none`), ASR noise on names and powers. `--seed 2 --count 2000` for an eval set. |
| 2 | `python tools/voice-model/train_lora.py --data ... --out scratch/voice-model/run1 --eval ... --gguf <llama.cpp>/convert_hf_to_gguf.py` | a LoRA on Qwen3-0.6B (Apache-2.0), merged and exported to GGUF (q8_0). An hour on the RTX 4090 for 20,000 examples is the estimate; not measured. |
| 3 | `build/Release/mevr-llm-bench.exe --lib external/llama-cpp --model <gguf> --data scratch/voice-model/eval.jsonl` | accuracy and times on the CPU under the grammar (`core/voice/Llm`, the same runtime the host uses); run it on the stock Qwen3-0.6B-Q8_0 first for the zero-shot baseline. |
| 4 | `tools/deploy-asi.ps1 -Game LE1` with the GGUF at `models/voice/llm/model.gguf` | the host loads it from `ASI\voice\llm` (`[Voice] llm = 1`, `llmModel`, `llmThreads`). |

## The pipeline (repeatable runs, sweeps)

`setup-env.ps1` makes the venv (`.venv-voice`, Python 3.12, torch CUDA 12.4, transformers, peft, datasets, accelerate,
gguf) and fetches llama.cpp's convert script into `external/llama-cpp/convert`. Then one command per run:

    python tools/voice-model/pipeline.py run --config tools/voice-model/configs/base.json --name run1
    python tools/voice-model/pipeline.py sweep --config tools/voice-model/configs/base.json rank=8,16,32 lr=1e-4,2e-4
    python tools/voice-model/pipeline.py table

A run lives in `scratch/voice-model/runs/<name>/`: `config.json`, `train.jsonl` / `eval.jsonl` (regenerated only when the
dataset part of the config changes), `train.log` (the progress bar and a `{'loss': ...}` line every 20 steps; the whole
history in `log_history.json` at the end), `lora/`, `merged/`, `model-<type>.gguf`, `eval.log`, `bench.log`, and a
`done-<step>.json` per step keyed by the config's hash, so an interrupted or failed run resumes at the failed step and a
changed hyperparameter redoes only what depends on it (`--force <step>` redoes one by hand). `runs.tsv` at the runs' root
collects every run's eval exact-match and bench line for the hyperparameter table.

## Prior art the settings come from

The base config (`configs/base.json`: rank 16, alpha 32, learning rate 2e-4 with a cosine schedule and 3% warmup, 2 epochs,
batch 16, weight decay 0.01, LoRA on every attention and MLP projection) follows the common LoRA recipe rather than a tuned
result: Unsloth's hyperparameter guide recommends rank 16 or 32 with alpha equal to the rank or twice it, a learning rate
in the 1e-4 to 2e-4 range and 1-3 epochs (more overfits) ([LoRA Hyperparameters Guide](https://docs.unsloth.ai/get-started/fine-tuning-guide/lora-hyperparameters-guide),
[its encyclopedia page](https://docs.unsloth.ai/basics/lora-parameters-encyclopedia)); TinyAgent fine-tuned its 1.1B and 7B
function-calling models with LoRA for 3 epochs ([TinyAgent cards](https://huggingface.co/squeeze-ai-lab/TinyAgent-7B)), and
a later paper's LoRA baseline on the same data used 3 epochs, batch 32 and a learning rate of 7e-5 on TinyLlama 1.1B
([Natural GaLore](https://arxiv.org/pdf/2410.16029)); the narrow fine-tunes the research note cites (FunctionGemma 58% to
85%, LFM2.5-350M 34.5% to 95.9%) show the jump expected from any sensible setting on a closed task. The sweep to run when
the first result is in: `rank=8,16,32 lr=1e-4,2e-4 epochs=1,2,3`, judged by the eval exact match, then the bench's time for
the GGUF types. What matters more than the numbers here is the data: the generator's coverage of phrasings and ASR slips
(`make_dataset.py`) and, later, real transcripts from the headset.

Environment for step 2 (a venv, never the system Python): `pip install torch --index-url https://download.pytorch.org/whl/cu124`,
`pip install transformers peft datasets accelerate`, and llama.cpp's source for `convert_hf_to_gguf.py` (`pip install -r
requirements/requirements-convert_hf_to_gguf.txt` there). The base must stay Qwen (or Granite): never a Gemma model as
the base or the teacher, its terms would make ours a derivative (research/54 "Licences sort the field").

**The runtime must feed the model what it was trained on**: `train_lora.py` wraps every record in Qwen's chat template
with its `SYSTEM` line (thinking off), and `core/voice/Llm` wraps the prefix and the block in the same template
(`Settings::chatTemplate`, on by default; `[Voice] llmChat = 0` and the bench's `--raw` for a model trained on the raw
text). Run1 answered 7 of 300 raw and 285 of 300 wrapped. Change the system line in both places or in neither.

The prompt format is the mod's own: `core/vr/SquadContext`'s `Prefix` (the rules and the roster) + `Block` (NOW, PARTY,
SHEP, the E groups, the L landmarks (the level's cover: a place for a move or hold, research/54 "Landmarks"), AIM/GAZE, PT
lines, SAY) + `\nACTION:` (`core/voice/Grammar::Prompt`). `make_dataset.py` writes it
by hand, so a change to `Block` needs the same change there (the llm bench's grammar reader `GbnfFromBlock` reads the
same lines). The answer is `WHO|WEAPON|POWER|ORDER|TARGET`, several joined by ` ; `, or `none`.

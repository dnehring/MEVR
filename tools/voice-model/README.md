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

Environment for step 2 (a venv, never the system Python): `pip install torch --index-url https://download.pytorch.org/whl/cu124`,
`pip install transformers peft datasets accelerate`, and llama.cpp's source for `convert_hf_to_gguf.py` (`pip install -r
requirements/requirements-convert_hf_to_gguf.txt` there). The base must stay Qwen (or Granite): never a Gemma model as
the base or the teacher, its terms would make ours a derivative (research/54 "Licences sort the field").

The prompt format is the mod's own: `core/vr/SquadContext`'s `Prefix` (the rules and the roster) + `Block` (NOW, PARTY,
SHEP, the E groups, AIM/GAZE, PT lines, SAY) + `\nACTION:` (`core/voice/Grammar::Prompt`). `make_dataset.py` writes it
by hand, so a change to `Block` needs the same change there (the llm bench's grammar reader `GbnfFromBlock` reads the
same lines). The answer is `WHO|WEAPON|POWER|ORDER|TARGET`, several joined by ` ; `, or `none`.

# SPDX-License-Identifier: MIT
"""The model tier's fine-tune (research/54 "Rules first, a fine-tuned sub-1B model second"): a LoRA on Qwen3-0.6B
(Apache-2.0) over make_dataset.py's JSONL, exported to GGUF for llama.cpp (the host's plugin/VoiceModel loads it from
ASI\\voice\\llm). Runs on the RTX 4090 in about an hour for 20,000 examples (an estimate by analogy with TinyAgent; not
measured here: the GPU was the game's while this was written). Needs a Python environment with torch (CUDA), transformers,
peft, datasets and llama.cpp's convert script (for --gguf): see README.md next to this file. Never a Gemma model as the
base or the teacher (its terms would make ours a derivative).

  python tools/voice-model/train_lora.py --data scratch/voice-model/train.jsonl --out scratch/voice-model/lora
         [--base Qwen/Qwen3-0.6B] [--epochs 2] [--rank 16] [--eval scratch/voice-model/eval.jsonl]
         [--gguf external/llama-cpp-src/convert_hf_to_gguf.py]
"""
import argparse
import json
import os
import pathlib
import random
import subprocess
import sys

SYSTEM = "You are the squad's radio. Answer with the action line only."


def load(path):
    rows = []
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        if line.strip():
            rows.append(json.loads(line))
    return rows


def to_chat(tokenizer, row):
    messages = [{"role": "system", "content": SYSTEM}, {"role": "user", "content": row["prompt"]}, {"role": "assistant", "content": row["answer"]}]
    # Qwen3's template; thinking off: the answer is the line, nothing before it.
    return tokenizer.apply_chat_template(messages, tokenize=False, enable_thinking=False)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--base", default="Qwen/Qwen3-0.6B")
    ap.add_argument("--epochs", type=float, default=2.0)
    ap.add_argument("--rank", type=int, default=16)
    ap.add_argument("--lr", type=float, default=2e-4)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--max-len", type=int, default=768)
    ap.add_argument("--eval", default="")
    ap.add_argument("--gguf", default="", help="llama.cpp's convert_hf_to_gguf.py: merges the LoRA and writes <out>/model-q8_0.gguf")
    args = ap.parse_args()
    try:
        import torch
        from datasets import Dataset
        from peft import LoraConfig, get_peft_model
        from transformers import AutoModelForCausalLM, AutoTokenizer, DataCollatorForLanguageModeling, Trainer, TrainingArguments
    except ImportError as e:
        print(f"missing a package: {e}. See tools/voice-model/README.md for the environment.")
        return 2
    rows = load(args.data)
    random.Random(1).shuffle(rows)
    tokenizer = AutoTokenizer.from_pretrained(args.base)
    tokenizer.pad_token = tokenizer.pad_token or tokenizer.eos_token
    texts = [to_chat(tokenizer, r) for r in rows]

    def tokenize(batch):
        enc = tokenizer(batch["text"], truncation=True, max_length=args.max_len)
        return enc

    train = Dataset.from_dict({"text": texts}).map(tokenize, batched=True, remove_columns=["text"])
    model = AutoModelForCausalLM.from_pretrained(args.base, torch_dtype=torch.bfloat16, device_map="auto")
    lora = LoraConfig(r=args.rank, lora_alpha=args.rank * 2, lora_dropout=0.05, target_modules=["q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj"],
                      task_type="CAUSAL_LM")
    model = get_peft_model(model, lora)
    model.print_trainable_parameters()
    training = TrainingArguments(output_dir=args.out, num_train_epochs=args.epochs, per_device_train_batch_size=args.batch, gradient_accumulation_steps=1,
                                 learning_rate=args.lr, lr_scheduler_type="cosine", warmup_ratio=0.03, logging_steps=20, save_strategy="epoch", bf16=True,
                                 report_to=[], dataloader_num_workers=2)
    trainer = Trainer(model=model, args=training, train_dataset=train, data_collator=DataCollatorForLanguageModeling(tokenizer, mlm=False))
    trainer.train()
    model.save_pretrained(os.path.join(args.out, "lora"))
    merged = model.merge_and_unload()
    merged.save_pretrained(os.path.join(args.out, "merged"), safe_serialization=True)
    tokenizer.save_pretrained(os.path.join(args.out, "merged"))
    if args.eval:
        hits = 0
        rows_eval = load(args.eval)
        merged.eval()
        for r in rows_eval:
            prompt = tokenizer.apply_chat_template([{"role": "system", "content": SYSTEM}, {"role": "user", "content": r["prompt"]}], tokenize=False,
                                                   add_generation_prompt=True, enable_thinking=False)
            ids = tokenizer(prompt, return_tensors="pt").to(merged.device)
            out = merged.generate(**ids, max_new_tokens=48, do_sample=False)
            text = tokenizer.decode(out[0][ids["input_ids"].shape[1]:], skip_special_tokens=True).strip()
            hits += text == r["answer"]
        print(f"eval: {hits}/{len(rows_eval)} exact ({100.0 * hits / max(len(rows_eval), 1):.1f}%)")
    if args.gguf:
        gguf = os.path.join(args.out, "model-q8_0.gguf")
        subprocess.run([sys.executable, args.gguf, os.path.join(args.out, "merged"), "--outfile", gguf, "--outtype", "q8_0"], check=True)
        print(f"GGUF: {gguf} (copy it to ASI\\voice\\llm\\model.gguf, or set [Voice] llmModel)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

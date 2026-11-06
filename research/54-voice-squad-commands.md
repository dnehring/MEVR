# 54: Voice squad commands on the device: a streaming cascade under 500 ms (2026-11-04)

The user: research how speech could become a squad order with low latency ("Garrus, equip your sniper and take out those troops on the bridge" into its action), on the device, under 1 s and ideally 500 ms, with very small or fine-tuned open models; the naive speech-to-text, LLM, action cascade assumed too slow. Seven research passes (recognizers and endpointing, speech-native models, small-model parsing, context and deixis, precedents, inference beside the VR frame, LE1's squad orders in our code), every number cited inline. Nothing here was measured on our machine yet: the plan at the end says what to measure first.

A spoken order such as "Garrus, equip your sniper and take out those troops on the bridge" can reach LE1's squad AI entirely on-device in an estimated **180-400 ms after the end of speech** (about 180-290 ms when deterministic rules parse it), and the speed comes from removing waits, not from a new kind of model. The naive cascade is slow because it waits for silence, transcribes the finished clip with a non-streaming recognizer and has an uncached language model write verbose JSON; each of those waits is avoidable. The evidence favours a **streaming cascade on the CPU**: releasing a held "radio" gesture ends the utterance, a streaming recognizer such as Moonshine v2 (MIT) returns the transcript about 50-260 ms later, the mod's own code turns the scene into a few id-tagged lines and resolves "those" from where the player pointed and looked while saying it, and a deterministic parser fills the slots in about a millisecond, with a fine-tuned 0.3-1B Apache-2.0 model (Qwen3-0.6B or Granite 4.0 Nano in llama.cpp, grammar-constrained to a ten-token answer) only for what the rules leave open. Audio-native LLMs are no faster once the recognizer streams, lose more accuracy in noise and on names, and need the GPU, which LE1's D3D11 stereo frame already fills: NVIDIA's in-game inference scheduling requires a D3D12 or Vulkan game, so any GPU burst would cost late frames and spacewarp. Every timing here is extrapolated from Apple M3, laptop Zen 5 and phone measurements; none was taken on a desktop beside the running game. The larger gap is on the game side: only the weapon order is built, the attack, move, follow and power orders have known signatures but untested behaviour, and VR players cannot give the move, attack or regroup orders at all today because the d-pad that carries them is unmapped. The first prototype should trace and call those orders, then race two recognizers on the dev machine under VR load.

## The naive cascade is slow because it waits, not because it cascades

The one same-hardware comparison of speech-native and cascaded tool calling, VoiceAgentBench on an H100, measured a p90 time to first token of about **40 ms** for Qwen2.5-Omni 7B against about **800 ms** for Whisper-large-v3 feeding Qwen3 8B, and attributes the gap to the transcription step ([VoiceAgentBench](https://arxiv.org/html/2510.07978)). That step is slow by construction. Whisper's encoder processes the whole padded clip after it ends, so on an Apple M3 CPU Whisper Small takes **1,940 ms** from the end of speech to text, while the streaming Moonshine v2 Small, which encodes the audio as it arrives, takes **148 ms** at an average word error rate of 7.84 ([Moonshine v2 paper](https://arxiv.org/html/2602.12241)). The language model then adds a second avoidable wait: Qwen3-1.7B decodes at a measured 62 tokens per second on a laptop Zen 5 CPU ([Liquid AI](https://www.liquid.ai/blog/introducing-lfm2-5-the-next-generation-of-on-device-ai)), which is about 565 ms to write a 35-token JSON call and about 130 ms to write an 8-token compact line, before a few hundred tokens of uncached prompt are counted. The naive cascade's one to two seconds are three serial waits (for silence, for transcription, for verbose generation) that a streaming design removes, not a cost of having a transcript.

Skipping the transcript buys no accuracy for this use either. In Audio2Tool, cascades win exact match and slot F1 on multi-intent commands and all three of its metrics once background speech is mixed in, and the recognizer itself costs about seven points (Qwen 4B scores 80.2 with a perfect transcript and 72.9 behind Whisper-v3) ([Audio2Tool](https://arxiv.org/html/2604.22821)). VoiceAgentBench found Qwen2.5-Omni 7B picking the right tool 83% of the time but filling its parameters correctly in **1.7%** of cases, against roughly 56-61% for Whisper-plus-LLM cascades ([VoiceAgentBench](https://arxiv.org/html/2510.07978)). BFCL Audio isolates the condition that matters most here: with competing speech in the background, models preserved the intent of only **42.4%** of commands, and GPT-4o-audio fell from 60.4 on clean audio to 50.9 in noise ([BFCL Audio](https://proceedings.mlr.press/v306/mao26b.html)). Squad banter and enemy barks played by a Quest's open-ear speakers a few centimetres from its microphones are exactly that competing speech. A cascade also leaves a transcript in which a misheard "Gareth" can be corrected to Garrus against the two squadmates present; an audio model's name errors are silent.

The audio-native candidates that pass the licence and runtime filters are Gemma 4 E2B/E4B and Voxtral Mini 3B, both Apache-2.0 and both loadable through llama.cpp's multimodal path ([llama.cpp multimodal docs](https://github.com/ggml-org/llama.cpp/blob/master/docs/multimodal.md); [Gemma 4 E2B card](https://huggingface.co/google/gemma-4-E2B-it); [Voxtral Mini 3B card](https://huggingface.co/mistralai/Voxtral-Mini-3B-2507)). They are 2-5B-parameter models (Voxtral Mini needs about 9.5 GB in bf16), llama.cpp's documentation describes no streaming audio input, so the clip would be encoded only after the player stops, and no consumer-GPU latency for any of them on a 3 s clip has been published. They deserve one A/B against the cascade on the same context block and grammar, late in the prototype, and no more.

"Streaming" earns its keep when work moves into the seconds the player spends talking, and five pieces of work can happen before the last word. The game context is snapshotted when speech starts (ME3's Kinect orders latched their target when spoken, even if the cursor moved before the squadmate reacted ([Engadget](https://www.engadget.com/2012-01-18-testing-kinect-in-mass-effect-3-plus-demo-coming-february-14.html))). The recognizer encodes chunk by chunk. Each deictic word is resolved against the pose history as soon as it appears. A rule parser runs on every stable partial transcript, and a language model, where one is used, prefills those stable words into its cache. A complete, independent clause ("equip your sniper") is dispatched while the rest is still being spoken. Deployed and prototype systems confirm the split between a fast constrained path and a slow generative one. Tencent's F.A.C.U.L. squad companion averages about **613 ms** for intent and entity resolution with a BERT fast path and a Qwen2-1.5B fallback on server GPUs, ASR and TTS excluded ([F.A.C.U.L.](https://arxiv.org/html/2511.13112)); Krafton's PUBG Ally, a 2B model beside the game, spends a median 0.80 s in the language model out of a 1.62 s spoken exchange on RTX 4060-class PCs ([PUBG Ally paper](https://www.alphaxiv.org/abs/2609.29837)); a Kaldi grammar decoder returns a command 10-20 ms after the endpoint in its author's informal tests ([kaldi-active-grammar](https://github.com/daanzu/kaldi-active-grammar/blob/master/docs/models.md)). Systems that constrain answer in tens of milliseconds; systems that generate take most of a second.

## Code computes the scene; the model only picks an id

Game context decides accuracy and latency at once: it limits what the recognizer should expect, what the parser may emit and which enemy a word refers to. The evidence says to put all reasoning about that context in code and hand a model, if any, only words to match. Language models handle multi-hop spatial relations poorly ([StepGame](https://ar5iv.labs.arxiv.org/html/2204.08292)), switch frames of reference for "left of" unreliably ([COMFORT](https://arxiv.org/abs/2410.17385v2)), and SayPlan's planner failed on negation and counting over a scene graph ([SayPlan](https://proceedings.mlr.press/v229/rana23a/rana23a.pdf)). Picking an entry from an indexed list is a different task: Apple's ReALM, which serialises entities as numbered text and has the model output an index, reached **96.7%** on conversational references and 88.9% on on-screen ones with a fine-tuned **80M** model, against GPT-4's 97.0% and 90.1%, while untuned GPT-3.5 managed 34.2% on its synthetic set ([ReALM](https://ar5iv.labs.arxiv.org/html/2403.20329)). ConceptGraphs adds the matching warning: its LLM was only as good as the object captions it was given, which were right about 70% of the time ([ConceptGraphs](https://ar5iv.labs.arxiv.org/html/2309.16650)). So code clusters the hostiles into at most five or six groups with ids that stay stable through an encounter, and for each group computes its type and count, its sector relative to the head's yaw when speech began, a distance band, its elevation relative to the player, a surface word where one is known, and ordinal flags such as `nearest` and `leftmost`. "The left ones" then matches a literal word instead of requiring geometry from a 0.6B model.

### Most of the context is already read in `src/game`

`squad_weapons::SurveySquad` reads each squadmate's name, class, health, quick-slot weapons and place, and measures their angle from the left controller's ray ([src/game/SquadWeapons.cpp](../src/game/SquadWeapons.cpp)). `throwables::FindEnemy` walks `WorldInfo.PawnList`, skips the dead and keeps pawns for which `BioPawn.IsHostile` holds ([src/game/Throwables.cpp](../src/game/Throwables.cpp)). `engine/SelectionGaze` puts the head's place and forward into `CameraCache.POV` for `BioPlayerSelection`'s update on foot, so the game's own selection, combat targets included, follows the player's look ([src/engine/SelectionGaze.hpp](../src/engine/SelectionGaze.hpp); [research/40](../research/40-interaction.md)); `game/Interact` applies it in game mode 0 only, and whether the selection keeps running with the weapon drawn in combat is **[verify]** ([src/game/Interact.cpp](../src/game/Interact.cpp)). `sights::TraceSightLine` traces the gun's line every tick through `Actor.Trace` ([src/game/Sights.cpp](../src/game/Sights.cpp)). Four things are not read yet: squadmates' powers and cooldowns (`BioEpicPawnBehavior.m_powers`, `UBioPower.m_nmPower`, `GetCooldownRemaining`, `IsEnabled` in LExSDKv2's generated header `external/LExSDKv2/Src/LESDK/LE1/SFXGame_classes.hpp`, used for names only), enemies' display names (the game uses TLK string references the mod doesn't read), the squad's current orders (`BioBaseSquad.GetMemberAttackTarget`, `GetMemberFormation` and their siblings) and any names for places. The spoken-name table for powers should be built at run time from each squadmate's `m_powers`, because no reliable per-squadmate power list for LE1 was found and whether `m_nmPower` matches the script class names (Overload as `EMP`, Assassination as `SniperCrit`) is unconfirmed **[verify]**.

"On the bridge" is the hardest phrase in the example because LE1 has no labels for places. Precedents feed engine state to models as plain text (PUBG Ally's observation tools describe the player's weapon, health and surroundings in sentences ([NVIDIA developer blog](https://developer.nvidia.com/blog/how-krafton-built-pubg-ally-a-co-playable-character-powered-by-nvidia-ace/))), but no game or robotics source was found that turns static-mesh or material names into surface words. LE1's asset names are partly descriptive (Eden Prime's `BIOA_PRO10_T.pro10_bunkertrims01_diff`, noted in [research/47](../research/47-cockpit-in-game.md)), so an offline, per-level lexicon from mesh and material names with hand overrides, queried by a downward trace under each enemy, can supply "bridge", "catwalk" or "crates" where it is confident **[verify on LE1's levels]**. Geometric words computed by traces ("above", "on a walkway", "in cover") need no names and should carry most of the load, with synonyms (bridge = catwalk = walkway) in the cached prefix.

### A cached prefix, a 150-token scene block and a one-line answer

The prompt splits into a prefix that is computed once and a small block per utterance. The prefix, generated from the game's own tables and rebuilt only when the party changes, holds the instructions, all six possible squadmates with their aliases ("Ash", "the turian"), weapon and power codes, synonyms and five to eight examples, about 400-550 tokens by a character-count estimate. The scene block, about 115-160 tokens for four enemy groups, holds facts computed by code and snapshotted when speech starts, for example:

```
NOW combat
PARTY G:SR*,AR | OVR ok, SAB ok, DMP 9s
PARTY W:SG* | THR ok, WRP 4s
E1 geth trooper x3 | left far above | bridge | PT1 0.81
E2 geth destroyer x1 | ahead near level | crates | nearest
E3 geth trooper x2 | right mid level | catwalk
AIM E2 | GAZE E1
PT1 "those" = E1 .81, E3 .12
LAST G attack E3 OVR 14s
SAY Garrus, equip your sniper and take out those troops on the bridge
```

The expected answer is one line, `G|SR|-|attack|PT1`, about 10-15 tokens where a JSON object would take roughly three times as many, and output tokens are the slow part of a small model's latency. Format barely moves accuracy: a 3B Llama scored 52.7% with JSON and 48.0% with Markdown on nested data, an insignificant difference, while Markdown used 34-38% fewer tokens ([Improving Agents](https://www.improvingagents.com/blog/best-nested-data-format/)). The same structure drives the rule tier directly, so the model, when it runs at all, sees a text rendering of exactly what the rules already used, and after it answers, code validates every slot (squadmate present, weapon in a quick slot, power owned and ready) and expands the symbols into game calls.

### "Those" must be resolved when it was said

Gaze reaches a referent before it is named: in a gaze-and-speech command study the most informative fixation came about **630 ms** before speech onset, 150-1200 ms across people ([Kaur et al.](https://repository.tudelft.nl/record/uuid:305e24d3-4b79-49ca-b332-f9729b76b8eb)). In Oviatt's pen-and-speech study a gesture made in sequence with speech ended before speech began in 99% of cases, by 1.1-1.4 s on average and always within 4 s ([Oviatt et al., CHI 97](https://chi1997.acm.org/proceedings/paper/slo.html)), and a pointing hold brackets the stressed syllable of the word it goes with ([Rochet-Capellan et al.](https://www.isca-archive.org/interspeech_2007/rochetcapellan07_interspeech.pdf)). GazePointAR, which sampled gaze and pointing once after the query, named that single sample as one cause of answering only about 41% of queries satisfactorily ([GazePointAR](https://arxiv.org/html/2404.08213v1)). The mod should therefore keep a **4 s ring buffer**, filled each game tick, of the head ray, both aim rays, what each hits and the gaze selection's target, and score the candidates for each deictic word with a temporal kernel (head samples peaking a few hundred ms before the word, controller samples at it) and an angular one. The head's angular kernel must be wide, because head direction sits a median **12.6°** from where the eyes look ([Kangas et al.](https://www.frontiersin.org/journals/virtual-reality/articles/10.3389/frvir.2022.822189/full)), and head direction is all the gaze most players provide: the main audience's Quest 3 has no eye tracking, and the PS VR2's is unavailable on PC ([VGC](https://www.videogameschronicle.com/news/playstation-vr2-pc-adapter-coming-this-august-disables-numerous-headset-features)). Groups closer together than that are separated by the description ("on the bridge", "the left ones"), which is why the block passes the top two or three candidates with scores. Kernel widths and peaks are starting points to fit on recordings **[verify]**.

Word timing comes free from transducer recognizers, whose tokens are emitted on a frame grid (80 ms for FastConformer models), while Moonshine documents no timestamps ([moonshine-streaming-medium card](https://huggingface.co/UsefulSensors/moonshine-streaming-medium)). Without them, the time at which a partial transcript first contains the deictic word, minus a calibrated lag, bounds when it was said, and the word's position in the utterance is the coarse fallback **[verify which partials Moonshine's library exposes]**.

Follow-ups and corrections stay in code as well. The mod keeps the last few orders as structured records and shows the model one or two summary lines; the model emits symbols (`LAST`, `OTHER`) that code expands, and a leading "no" is detected deterministically and amends the previous order. ReALM's fine-tuned small models resolved conversational references at 96.7-97.9% when earlier entities were listed explicitly ([ReALM](https://ar5iv.labs.arxiv.org/html/2403.20329)). In combat a wrong target costs one cooldown and a two-word correction while a clarifying question costs seconds, so the mod should act on a confident best candidate, highlight it, keep the runner-up ready for "no, the other ones", and ask only when no candidate exists or the order spends something scarce. Conformal prediction gives that threshold a calibrated basis: KnowNo acts when the set of plausible options is a single one and asks otherwise ([KnowNo](https://arxiv.org/abs/2307.01928)). The precedents are blunt about guessing wrong: misread commands that sent Lifeline's heroine wandering defined its reviews ([Wikipedia](https://en.wikipedia.org/wiki/Lifeline_%28video_game%29)).

## CPU-sized recognizers and parsers clear the GPL-3.0 bar

### Moonshine v2 leads, Nemotron challenges, Windows' grammar recognizer is the baseline

| Recognizer | Size | Average WER (Open ASR sets) | Latency evidence | Word timing, biasing | Licence and GPL-3.0 verdict |
|---|---|---|---|---|---|
| Moonshine v2 Tiny / Small / Medium | 34M / 123M / 245M | 12.01 / 7.84 / 6.65 | 50 / 148 / 258 ms from end of speech segment to text on an M3 CPU; compute 8-29% of the audio's duration | none documented | MIT: compatible, can ship |
| Nemotron Speech Streaming 0.6B | 600M; int4 0.67 GB, int8 1.28 GB | 8.43 (80 ms chunks) to 6.93 (1.12 s) | real time on CPU (RTFx 2.9-7.2 on 32 EPYC cores); finalisation about one chunk (estimate) | transducer frames; hotwords via sherpa-onnx | NVIDIA Open Model License: commercial use allowed, not a free-software licence; optional separate download only **[verify the text]** |
| Parakeet realtime EOU 120M | 120M | 9.30 at 160 ms, lowercase, no punctuation | end-of-utterance token p50 / p95 160 / 320 ms | transducer frames | NVIDIA Open Model License: as above |
| Kyutai STT 1B | ~1B | not stated in text | 0.5 s built-in delay; GPU-first | word timestamps | code MIT / Apache-2.0, weights CC-BY-4.0: compatible with attribution; wrong hardware |
| Whisper tiny to large-v3 | 75 MiB-2.9 GiB on disk | not compared here | 289 ms (tiny) to 11.3 s (large-v3) on an M3, growing with clip length | soft prompt only | MIT: compatible; wrong shape |
| Vosk small en-us 0.15 | 40 MB | 9.85 on LibriSpeech clean (2020-22 models) | not measured here | run-time vocabulary | Apache-2.0: compatible |
| Windows SAPI with an SRGS grammar | part of Windows | not measured | finalises after 150 ms of silence when unambiguous, 500 ms when the grammar could continue | hard grammar | ships with Windows: nothing distributed |

Sources: [Moonshine v2 paper](https://arxiv.org/html/2602.12241), [Nemotron card](https://huggingface.co/nvidia/nemotron-speech-streaming-en-0.6b), [CPU evaluation, arXiv 2604.14493](https://arxiv.org/html/2604.14493v2), [Parakeet EOU card](https://huggingface.co/nvidia/parakeet_realtime_eou_120m-v1), [Kyutai](https://github.com/kyutai-labs/delayed-streams-modeling), [whisper.cpp](https://github.com/ggml-org/whisper.cpp), [Vosk models](https://alphacephei.com/vosk/models), [SAPI timeouts](https://msdn.microsoft.com/en-us/library/ms556365.aspx).

Moonshine v2 is the first candidate because its paper measures exactly the interval this budget cares about, from the end of a speech segment to the returned text, on a CPU, with MIT weights, and its Small and Medium sizes match larger streaming models' accuracy ([Moonshine v2 paper](https://arxiv.org/html/2602.12241)). Its gaps are real: no documented timestamps or biasing, and a warning on its model card that the models can hallucinate or repeat phrases on short or noisy audio, which is the profile of a squad order shouted in a firefight ([model card](https://huggingface.co/UsefulSensors/moonshine-streaming-medium)). Nemotron Speech Streaming 0.6B through sherpa-onnx is the challenger: 7.67 WER at 160 ms chunks, a time for every word for deixis, and sherpa-onnx's hotword boosting, which works only for transducers and only with modified beam search rather than the greedy decoding in its examples ([Nemotron card](https://huggingface.co/nvidia/nemotron-speech-streaming-en-0.6b); [sherpa-onnx hotwords](https://k2-fsa.github.io/sherpa/onnx/hotwords/index.html)). Its licence keeps it out of the default package. Parakeet's 120M end-of-utterance model is the cheapest open-mic endpointer; Kyutai's 0.5 s delay and GPU orientation rule it out, and Whisper's non-streaming shape rules it out too. Windows' grammar recognizer is the zero-download baseline: its list and SRGS constraints run on the device and already hold a semantic endpoint ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/design/input/speech-recognition)), but its acoustic models are old, and users report that Windows 11 24H2 replaced classic Windows Speech Recognition with Voice Access, so the engine's presence on players' machines needs checking **[verify]** ([Steam discussion](https://steamcommunity.com/app/611670/discussions/0/4030222664481142894)).

Proper nouns are the weak spot of every recognizer, and no source measured how much biasing helps them, so the dependable lever sits after the recognizer: fuzzy and phonetic matching of the transcript against only the squadmates present, which turns "Garrus" into a choice between two names, done by RapidFuzz (MIT, C++ core) in well under a millisecond ([RapidFuzz](https://github.com/rapidfuzz/rapidfuzz)). Hotword boosting or NeMo's phrase boosting can be layered on top and measured ([NeMo word boosting](https://docs.nvidia.com/nemo/speech/nightly/asr/asr_customization/word_boosting.html)). Per-speaker adaptation was the strongest accent fix in the precedents: Kaldi Active Grammar cut an accented speaker's command error rate from 46.98% to 9.76% with about an hour of their speech ([kaldi-active-grammar](https://github.com/daanzu/kaldi-active-grammar/blob/master/docs/models.md)). An optional "say each name" step in the mod's existing calibration flow would at least collect each player's test data.

### Rules first, a fine-tuned sub-1B model second

A closed command language (two squadmates present plus "everyone", four weapon classes, a handful of powers each, about eight orders, a few enemy groups and the deictics) is small enough that a rule tier with a lexicon, fuzzy names and clause and correction detection fills every slot of the common commands in about a millisecond, and it stays unit-testable in `src/core/vr`. Joint intent-and-slot encoders are the next step up, at 98.6% intent accuracy and 97.0 slot F1 on SNIPS for JointBERT ([JointBERT](https://ar5iv.labs.arxiv.org/html/1902.10909)) and about 8-15 ms for an INT8 DistilBERT on a CPU ([classifier card](https://huggingface.co/addyo07/distilbert-query-classifier)), but their label schemes handle compound and self-corrected commands poorly. A small generative model handles both, and in a cascade it runs only on the minority of utterances the rules leave open.

| Model | Licence and GPL-3.0 verdict | Function calling, zero-shot | Speed anchor (llama.cpp, Q4_0, CPU) | Role |
|---|---|---|---|---|
| Qwen3-0.6B | Apache-2.0: compatible | BFCL V4 non-live 71.79, irrelevance 80.84 | Q8_0 file 639 MB; desktop speed unmeasured | first fine-tune candidate; pure attention keeps cache reuse simple |
| Granite 4.0 350M / 1B (dense) | Apache-2.0: compatible | BFCL v3 39.32 / 54.82 | unmeasured | smaller alternative |
| Qwen3.5-0.8B | Apache-2.0: compatible | BFCL V4 25.3 (thinking mode) | hybrid layers; llama.cpp's cache reuse had bugs in spring 2026 | candidate once its cache path is verified |
| Qwen3-1.7B | Apache-2.0: compatible | BFCL V4 non-live 82.92 | 2,008 prefill / 62 decode tokens per second (laptop Zen 5) | headroom if accuracy demands it |
| Gemma 4 E2B | Apache-2.0: compatible | native function calling, audio input | unmeasured | the one audio-native A/B |
| FunctionGemma 270M | Gemma Terms of Use: pass-through use restrictions and remote restriction, not bundled | BFCL simple 61.6; 58% to 85% after a fine-tune | about 126 decode tokens per second on four phone cores | optional user download at most |
| LFM2.5-350M / 1.2B | LFM Open License, free only under $10M revenue: field-of-use restriction, not bundled | 1.2B BFCL v3 49.12; 350M 34.5% to 95.9% after a fine-tune | 1.2B 2,975 / 116 tokens per second | optional user download at most |
| xLAM-2, Hammer2.1, Arch-Agent | CC-BY-NC-4.0, qwen-research, katanemo-research: incompatible | top of BFCL at 1-3B | n/a | excluded |

Sources: [BFCL leaderboard](https://gorilla.cs.berkeley.edu/leaderboard.html), [Granite 4.0 card mirror](https://huggingface.co/unsloth/granite-4.0-1b), [Qwen3.5-0.8B card](https://huggingface.co/Qwen/Qwen3.5-0.8B), [Liquid AI](https://www.liquid.ai/blog/introducing-lfm2-5-the-next-generation-of-on-device-ai), [FunctionGemma card](https://huggingface.co/google/functiongemma-270m-it), [Gemma Terms of Use](https://ai.google.dev/gemma/terms), [LFM licence](https://www.liquid.ai/lfm-license), [Qwen3-0.6B GGUF](https://huggingface.co/Qwen/Qwen3-0.6B-GGUF).

Zero-shot scores matter less than how well a model fine-tunes, and narrow fine-tunes jump. FunctionGemma 270M went from 58% to 85% on seven mobile functions ([Google](https://blog.google/technology/developers/functiongemma/)); LFM2.5-350M went from 34.5% to **95.9%** tool-call equivalence on a 14-function banking assistant fed ASR transcripts, against 96.95% for its 120B teacher ([distil-labs](https://huggingface.co/distil-labs/distil-lfm25-voice-assistant)); TinyAgent-1.1B went from 12.7% to 78.9% on multi-step plans after a LoRA fine-tune on 80,000 GPT-4-generated examples that cost about $500 ([TinyAgent](https://arxiv.org/html/2409.00608v3)). The training data writes itself from the action grammar: sample legal scenes (party, unlocked powers, one to six enemy groups with descriptors), have a large teacher write spoken phrasings for each gold action, including terse forms, fillers, restarts, compounds, deictics and non-commands, then inject ASR noise aimed at names and power words, where TTS-plus-ASR augmentation gave its largest gains on rare slot values ([arXiv 2109.05092](https://arxiv.org/abs/2109.05092)). The teacher must not be a Gemma model, because the Gemma terms make a model trained on Gemma outputs a Gemma derivative ([Gemma Terms of Use](https://ai.google.dev/gemma/terms)). Tens of thousands of examples and a single RTX 4090 are enough by analogy with TinyAgent, an estimate rather than a measurement, and 85-96% call accuracy on the residue the rules leave is the realistic target.

The runtime is llama.cpp (MIT) on four to six CPU threads in its own worker. The prefix is computed once and kept in the KV cache, the scene block is prefilled again in the background whenever the scene changes between utterances, and the stable words of each partial transcript are appended as they arrive, so at the end of speech only the last few words and the decode remain. Pure-attention models make this a truncate-and-append; for hybrid models (Qwen3.5, LFM2, Granite-H) llama.cpp keeps state checkpoints and users reported full prompt reprocessing in March and April 2026 ([rig-llama-cpp checkpoint](https://docs.rs/crate/rig-llama-cpp/latest/source/src/checkpoint.rs); [r/LocalLLaMA snapshot](https://reddit.sentinel-team.org/posts/1rlu85e/snapshots/2026-03-06T19%3A04%3A08.896785Z)). A grammar rebuilt per scene lists only the legal values plus `none`, so every output parses and an invented name is impossible. Modern mask engines cost about 40-50 µs per token ([XGrammar](https://arxiv.org/html/2411.15100v3); [llguidance](https://github.com/guidance-ai/llguidance)), while llama.cpp's original GBNF sampler cost about 5 ms per token in 2023 and could blow up on ambiguous grammars ([PR 1773](https://github.com/ggerganov/llama.cpp/pull/1773); [issue 4218](https://github.com/ggerganov/llama.cpp/issues/4218)), so llguidance is the safer engine. Tokens the grammar forces are appended in one batch instead of sampled one by one, SGLang's jump-forward idea ([LMSYS](https://lmsys.org/blog/2024-02-05-compressed-fsm)); speculative decoding adds a second model for a ten-token output and is not worth it. Extrapolated from the Zen 5 and phone anchors, a compact answer costs about **40-80 ms** for a 270-350M model, **60-110 ms** for 0.6-1.2B and 150-190 ms for Qwen3-1.7B, against about 700 ms for 1.7B writing uncached JSON.

### Licences sort the field before benchmarks do

| Component | Role | Licence | GPL-3.0 verdict |
|---|---|---|---|
| llama.cpp / ggml | LLM runtime | MIT ([releases](https://github.com/ggml-org/llama.cpp/releases)) | compatible |
| ONNX Runtime | Moonshine, VAD and turn-model runtime | MIT ([releases](https://github.com/microsoft/onnxruntime/releases)) | compatible |
| sherpa-onnx | streaming transducers, hotwords, keyword spotting | Apache-2.0 ([releases](https://github.com/k2-fsa/sherpa-onnx/releases)) | compatible; each model's own licence still applies |
| whisper.cpp | not recommended | MIT ([repo](https://github.com/ggml-org/whisper.cpp)) | compatible |
| llguidance / XGrammar | grammar masks | believed MIT / Apache-2.0 **[verify]** | compatible once confirmed |
| RapidFuzz | fuzzy name matching | MIT ([repo](https://github.com/rapidfuzz/rapidfuzz)) | compatible |
| Silero VAD | voice activity | MIT ([repo](https://github.com/snakers4/silero-vad)) | compatible |
| TEN VAD | voice activity | Apache-2.0 with additional conditions ([repo](https://github.com/TEN-framework/ten-vad)) | undecided; avoid until the conditions are read |
| Smart Turn v3.x | semantic end of turn | BSD-2-Clause ([repo](https://github.com/pipecat-ai/smart-turn)) | compatible |
| LiveKit turn detector | semantic end of turn | only within LiveKit Agents ([licence](https://huggingface.co/livekit/turn-detector/blob/main/LICENSE)) | incompatible |
| openWakeWord | wake-word training | code Apache-2.0, pre-trained models CC BY-NC-SA 4.0 ([repo](https://github.com/dscripka/openWakeWord)) | code compatible, its models not: train our own |
| Picovoice Porcupine, Rhino | wake word, speech-to-intent | proprietary, per-user AccessKey; Rhino free for 3 active users a month ([pricing](https://picovoice.ai/pricing)) | incompatible |
| WebRTC audio processing (AEC3) | echo cancellation | BSD-3-Clause ([freedesktop](https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing)) | compatible |
| RNNoise, SpeexDSP | noise suppression | BSD-3-Clause, BSD-style ([RNNoise](https://github.com/xiph/rnnoise); [SpeexDSP](https://github.com/xiph/speexdsp/blob/master/COPYING)) | compatible |
| DeepFilterNet | noise suppression | MIT or Apache-2.0 ([repo](https://github.com/Rikorose/DeepFilterNet)) | compatible |
| NVIDIA NVIGI | in-game inference SDK | MIT; DirectX DLLs and NVIDIA models under their own terms ([licence](https://github.com/NVIDIA-RTX/NVIGI-Core/blob/main/LICENSE)) | code compatible, but unusable with a D3D11 game |
| Parakeet TDT 0.6B v3 | offline ASR | CC-BY-4.0 ([card](https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3)) | usable as separately loaded data with attribution (a reading) |
| Moonshine legacy non-English models | ASR | Moonshine Community License, non-commercial ([README](https://raw.githubusercontent.com/moonshine-ai/moonshine/main/README.md)) | incompatible |
| Vosk daanzu models | ASR | AGPL ([Vosk models](https://alphacephei.com/vosk/models)) | avoid |
| Qwen2.5-Omni-3B | audio LLM | qwen-research ([card](https://huggingface.co/Qwen/Qwen2.5-Omni-3B)) | incompatible |

"Compatible" here means code that can be linked into the GPL-3.0 host or mod with its notice added to `NOTICE.md`, as the project already does for permissive sources ([CLAUDE.md](../CLAUDE.md), "Licensing rules"). Model weights are data the program loads; the conservative reading is that weights under use-restricted terms (NVIDIA Open Model License, Gemma Terms of Use, LFM Open License, the Llama community licence) are offered, if at all, as an optional download under their own terms through MEVR Setup's Optional content page and never bundled in the release. This is a reading of the licence texts, not legal advice.

## Push-to-talk release makes a 300 ms budget realistic

Endpointing is where most of the latency lives. sherpa's example rule waits 1.2 s of trailing silence after decoded speech ([sherpa endpointing](https://k2-fsa.github.io/sherpa/ncnn/endpoint.html)); Pipecat recommends a 0.2 s VAD hangover followed by its Smart Turn model ([Pipecat](https://docs.pipecat.ai/server/utilities/turn-detection/smart-turn-overview)); Talon waits 300 ms by default ([Talon settings](https://deploy-preview-272--talon-wiki-refactor.netlify.app/Customization/Talon%20Framework/settings)) and Tacspeak pads 250 ms ([Tacspeak](https://github.com/Kapparina/tacspeak)). A held push-to-talk control replaces all of that with an exact signal, and the games that commanded AI by voice used one: EndWar held a button while speaking, Star Trek: Bridge Crew pressed one per command, Tacspeak defaults to hold-to-listen ([EndWar wiki](https://tomclancy.wiki.gg/wiki/Endwar:Voice_command-_Overlord_battle_command_interface); [Road to VR](https://www.roadtovr.com/hands-ibm-watson-brings-voice-commands-star-trek-vr/); [Tacspeak](https://github.com/Kapparina/tacspeak)). Push-to-talk also keeps Discord, a streamer's commentary and the room out of the recognizer.

The control cannot be a menu or system button, which a Quest over Link doesn't reliably deliver ([CLAUDE.md](../CLAUDE.md)), and the remaining inputs are taken: the left trigger opens the squad wheel, the left grip is the support grip and grenades, the right grip holds the gun, the right stick click opens the power wheel and both sticks open the mod's menu. A "radio" gesture fits: the left hand raised to the side of the head with the grip held, released to end the order. VR players know it from Onward's shoulder radio ([UploadVR](https://uploadvr.com/onward-guide-tips-strategies/)), and in an HCI study speak-to-shoulder was the experts' preferred voice-assistant gesture, detected with 98.0% accuracy and 0.10 false triggers a minute ([Yi et al.](https://pi.cs.tsinghua.edu.cn/lab/people/YuntaoWang/publications/vrw-2024-voice-assistant-hm-ds/)). The sniper rifle's holster sits over the left shoulder, for the right hand ([src/core/ik/BodySlots.hpp](../src/core/ik/BodySlots.hpp)), so the radio zone belongs at ear height above it, and its collisions with the body slots and the support grip's capsule need a check in play **[verify]**. A keyboard key covers flat testing, and an optional open-mic mode can sit in the menu's Controls page, off by default.

Two refinements matter. First, the release travels over the controller's input path while the last word's audio travels over the headset microphone's path, and their relative delay over Link and Steam Link is unmeasured, so the capture should continue for a short tail after release, until the VAD sees silence or a cap (on the order of 150-200 ms) is reached, a proposal to tune on the measured lag. Second, players release a moment after they finish, so the parser should also act as a semantic endpointer while the grip is still held: when the transcript so far parses to a complete order and 150 ms of silence follows, that clause is final, the same logic as SAPI's 150 ms for an unambiguous match against 500 ms when the grammar could continue ([SAPI timeouts](https://msdn.microsoft.com/en-us/library/ms556365.aspx)). This also delivers early dispatch for compound commands: "Garrus, equip your sniper" is complete and reversible, so the weapon switch starts during "and take out those troops on the bridge". An order that spends a cooldown waits for the release, or for a longer pause, so that "Garrus, overload, no, sabotage" doesn't fire the wrong power.

The open-mic option costs more and risks more. Silero VAD processes a 30 ms chunk in under a millisecond ([Silero VAD](https://github.com/snakers4/silero-vad)); a 200 ms hangover plus Smart Turn v3, an 8 MB int8 model at about 13-60 ms on cloud CPU instances ([Daily](https://www.daily.co/blog/announcing-smart-turn-v3-with-cpu-inference-in-just-12ms/)), judges whether the turn is finished, but its authors say it is not designed for very short segments, which squad orders are ([smart-turn](https://github.com/pipecat-ai/smart-turn)), and an "incomplete" verdict needs a shorter fallback for commands (0.6-0.8 s) than Pipecat's 3 s. Parakeet's end-of-utterance token arrives at p50 160 ms and p95 320 ms on TTS test audio ([Parakeet EOU card](https://huggingface.co/nvidia/parakeet_realtime_eou_120m-v1)) but stalls on background noise without a VAD backstop ([soniqo guide](https://soniqo.audio/guides/dictate)). Squadmate names as wake words through sherpa-onnx's keyword spotter, which decodes only listed phrases with per-keyword thresholds ([sherpa-onnx KWS](https://k2-fsa.github.io/sherpa/onnx/kws/index.html)), are undermined by ME1's own dialogue, in which squadmates say each other's names constantly; the spotter would need to stand down while the game plays dialogue.

| Stage | Push-to-talk (default) | Open mic (option) | Basis |
|---|---|---|---|
| Headset mic to the Windows device over Link or Steam Link | unknown; covered by the capture tail | unknown; inside the hangover | not measured **[verify]** |
| WASAPI capture | 10-20 ms | 10-20 ms | [Microsoft Learn](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio) |
| Deciding speech has ended | 0 at release, or 150 ms of silence after a complete parse | 200 ms hangover + 10-60 ms Smart Turn, or EOU p50 160 / p95 320 ms | Pipecat, Daily, Parakeet card |
| Final transcript | 148-258 ms (Moonshine v2 Small-Medium, M3); 50 ms for Tiny; about one chunk for a transducer | same | Moonshine v2 paper |
| Rule tier | under 1-5 ms | same | estimate |
| LLM tier, only when the rules leave a slot open | 60-110 ms (0.6-1.2B, compact, cached); 150-190 ms (1.7B) | same | extrapolated from Liquid's Zen 5 figures |
| Hand-off to the game thread | up to one tick, about 11-14 ms | same | [src/mod/ModApi.hpp](../src/mod/ModApi.hpp) |
| The game's own order delay | `m_fTacticalDelayTime`, `m_fMoveToDelayTime`: values unknown | same | config fields **[verify]** |
| End of speech to dispatch | **~180-290 ms** by rules, ~240-400 ms with the LLM, plus the mic lag | ~390-560 ms by rules, ~450-670 ms with the LLM | sums of the rows |

The push-to-talk column meets the 500 ms goal with margin, and the open-mic column meets it only by rules and only if desktop CPUs under VR load match an M3 and a laptop Zen 5, which is the first number to measure. Perception sets the rest of the target. Nielsen puts the feel of direct causation at 0.1 s and the limit of uninterrupted flow at 1 s ([NN/g](https://www.nngroup.com/videos/3-response-time-limits-interaction-design/)); ME3's Kinect needed about a second for "shotgun" and was still judged fluid ([Engadget](https://www.engadget.com/2012-01-18-testing-kinect-in-mass-effect-3-plus-demo-coming-february-14.html)); F.A.C.U.L.'s authors call 100-800 ms acceptable for squad orders ([F.A.C.U.L.](https://arxiv.org/html/2511.13112)); in VR, natural fillers improved perceived response time where spinners did nothing ([arXiv 2507.22352](https://arxiv.org/html/2507.22352v1)). The mod should therefore acknowledge within about 100 ms of release (a radio click, a haptic tick, the recognised order as wrist text), dispatch by about 300 ms, let the squadmate's own reaction serve as the natural filler, and drop any command still unresolved after 1-1.5 s with a "Say again?" rather than act late.

## The VR frame owns the GPU, so inference runs on spare cores

### LE1 cannot use NVIDIA's in-game scheduling

Work from different GPU contexts does not run concurrently: NVIDIA's MPS documentation says the compute engine time-slices contexts ([NVIDIA MPS](https://docs.nvidia.cn/deploy/mps/architecture.html)). NVIDIA's answers for inference beside a game, CUDA in Graphics in TensorRT-RTX and NVIGI's scheduling hints, both require the game's D3D12 direct queue, or Vulkan for CUDA ([TensorRT-RTX](https://docs.nvidia.com/deeplearning/tensorrt-rtx/latest/inference-library/compute-graphics.html); [NVIGI 1.6](https://docs.nvidia.com/nvigi-sdk/1.6.0/docs/nvigi_core/docs/GpuSchedulingForAI.html)). LE1 renders through a single-threaded D3D11 device ([CLAUDE.md](../CLAUDE.md)), so any CUDA, DirectML or Vulkan inference is a separate context that takes turns with the eye renders. A 20-150 ms inference burst spans **2-14 frames at 90 Hz**, and the project's GPU is already near its stereo budget ([research/22](../research/22-performance-levers.md); [research/35](../research/35-performance-attempt-log.md)). When frames keep missing, Meta's ASW drops the application to 45 Hz and synthesises the frames in between ([Meta](https://developers.meta.com/horizon/documentation/native/pc/asynchronous-spacewarp/)), and SteamVR's Motion Smoothing does the same on SteamVR headsets ([Road to VR](https://roadtovr.com/steamvr-motion-smoothing-asw-alex-vlachos/)); head tracking survives, but the hands and the gun judder, which is what a VRIK player watches. NVIDIA itself concedes a short dip in render rate when its own G-Assist runs during play ([NVIDIA](https://www.nvidia.com/en-us/geforce/news/nvidia-app-update-g-assist-new-dlss-override-and-more/)). No published measurement covers an inference burst beside a D3D11 VR game, so the GPU path stays an option for players with headroom: its device at a lowered relative priority through `IDXGIDevice::SetGPUThreadPriority` (not the idle class, which guarantees no progress) ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgidevice-setgputhreadpriority)), throttled, and judged by total frame time with the feature on and off, as NVIGI advises, not by GPU timestamps. The CPU path also costs no VRAM, against about 0.6-1.5 GB for the GPU path on 8 GB cards.

### Three to six threads, kept off the game's cores

The CPU plan needs one capture thread registered with MMCSS, one or two recognizer threads (Moonshine v2 Small's compute was 18% of the audio's duration on the M3 ([Moonshine v2 paper](https://arxiv.org/html/2602.12241)), spread over the time the player speaks) and four to six threads for the language model's burst after the end of speech, all below normal priority and never on the game or render thread. On hybrid Intel parts, Windows' EcoQoS tag schedules a thread on efficient cores ([Microsoft QoS](https://learn.microsoft.com/en-us/windows/win32/procthread/quality-of-service)), which suits the recognizer, but adding E-cores to llama.cpp's pool cut token generation by 20-30% in one optimisation guide ([carteakey](https://carteakey.dev/blog/local-inference/local-llm-optimization/)), so the language model belongs on P-cores chosen with CPU sets away from the game's busiest threads **[verify]**. On dual-CCD X3D parts the chipset driver parks the second CCD while a game runs, by forum accounts ([AVSIM](https://www.avsim.com/forums/topic/647799-7950x3d-fix-for-core-parking/)), and how it treats in-process threads is unknown. Memory bandwidth is the hidden cost: each decoded token reads roughly all of a model's weights, about 0.49 GB for a 0.5B model at Q4_K_M ([Qwen2.5-0.5B GGUF](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF)), so an 8-token answer streams about 4 GB, roughly 65-100 ms of near-saturated dual-channel DRAM shared with the game's own threads (an estimate). That is a further reason to run the rules first and keep the answer short.

The CPU runtimes fit the players' package: llama.cpp's Windows CPU build is 24.7 MB zipped and sherpa-onnx's shared build, ONNX Runtime included, 21.9 MB, while CUDA builds add 300-700 MB of runtime libraries ([llama.cpp releases](https://github.com/ggml-org/llama.cpp/releases); [sherpa-onnx releases](https://github.com/k2-fsa/sherpa-onnx/releases)). Inference DLLs should load from `ASI\` with `LoadLibraryExW` restricted to that directory and the system's, with one ONNX Runtime in the process (sherpa-onnx bundles one; Moonshine's library uses one too), and models load lazily on a background thread at session start, memory-mapped, never on a level load's critical path.

### The headset microphone needs finding, ducking and no disk

WASAPI shared-mode capture with its default 10 ms period is ample against a 500 ms target ([Microsoft Learn](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio)). Finding the headset's microphone takes a chain. On Meta's runtime, the OpenXR extension `XR_OCULUS_audio_device_guid` returns the input device through `xrGetAudioInputDeviceGuidOculus` ([Khronos](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetAudioInputDeviceGuidOculus.html)), which the host can enable on Halcyon's instance through a `[MEVR]` seam as it already does for the HP controller extension. Under SteamVR, the `audio` section of `steamvr.vrsettings` names any recording-device override (`enableRecordingDeviceOverride`, `recordingDeviceOverride`), and SteamVR can make the headset's microphone the Windows default (`setOsDefaultRecordingDevice`) ([openvr.h](https://raw.githubusercontent.com/ValveSoftware/openvr/master/headers/openvr.h)). Name matching against "Headset Microphone (Oculus Virtual Audio Device)" and "Steam Streaming Microphone" follows, and a picker with a level meter in the mod's menu ends the chain; users report the Link microphone sometimes stuck with no real audio, so the meter is not optional ([Meta forum](https://communityforums.atmeta.com/discussions/PairingConnection/quest-2-mic-not-working-after-i-link-to-pc/1341568)). Capture runs at the endpoint's format (typically 48 kHz float), resampled to 16 kHz mono on the capture worker.

Game audio will reach the microphone. Push-to-talk plus ducking the game's own audio session by 10-15 dB through `ISimpleAudioVolume` while the gesture is held is the cheap remedy, done in-process, though whether LE's audio engine honours it quickly is **[verify]**. RNNoise (BSD-3) suppresses noise at a real-time factor of 0.03 on a laptop CPU ([DeepFilterNet2 paper](https://arxiv.org/pdf/2205.05474)); WebRTC's AEC3 (BSD-3, standalone in freedesktop's `webrtc-audio-processing` 2.x) is the upgrade for an open-mic mode, fed the game's own output as its reference from inside the process ([freedesktop](https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing)), with the streaming link's variable echo delay as the risk. Windows 11 lets an application pick the echo canceller's reference endpoint only where the capture endpoint supports it ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iacousticechocancellationcontrol-setechocancellationrenderendpoint)), which a virtual Link device may not. No audio should be written to disk unless the player starts a diagnostic.

## One squad order is built; the rest await a call trace

The move, attack and regroup orders are currently out of reach in VR (squadmates' powers remain reachable through the power wheel): the d-pad that carries them has no controller binding, a known issue for players ([docs/troubleshooting.md](../docs/troubleshooting.md)), squad orders by pointing are deferred to after the first release ([docs/release-checklist.md](../docs/release-checklist.md)), and the trilogy design asks for pointing with the controller and pressing to order ([docs/trilogy-design.md](../docs/trilogy-design.md)). On the pad, the d-pad's `Console_Squad{Attack,Follow,Move1,Move2}` bindings call four parameterless exec script functions on the active game mode ([research/19](../research/19-body-holsters.md)). Beneath them, LExSDKv2's generated headers list `BioPlayerController` natives that take a squad member index, per-pawn "order" natives whose choice `ApplyTacticalOrders` then applies, per-member accessors on `BioBaseSquad` and the AI controller's own action queue. Only the weapon path has been read from bytecode and built, as `squad_weapons::Give` ([src/game/SquadWeapons.cpp](../src/game/SquadWeapons.cpp)), and its outcome in the headset (whether the AI's switch animates, and how long it takes) is still open ([notes/todo-vrik.md](../notes/todo-vrik.md)). Voice and pointing need the same calls, so proving them serves both.

| Spoken intent | Candidate game calls (game thread, through `ue3::runtime::Call`) | Status |
|---|---|---|
| "Garrus, equip your sniper" | `squad_weapons::Give`: `BioPlayerController.SquadOrderEquipWeapon(byte nWeapon, APawn* pPawn)` with the sniper rifle (3), then `ApplyTacticalOrders()` while Shepard is armed, or `BioEquipment.ChangeToQuickSlot` and `BioBaseSquad.SetMemberSwitchWeapon` while Shepard's weapon is away | built from a bytecode reading; in-headset outcome **[verify]** |
| "Squad, attack" (what I'm looking at) | `SFXGameModeBase.SquadCommand_Attack()`, the d-pad's exec, with the gaze selection active | signature known; which target it takes **[verify]** |
| "Garrus, take out those" | `QuickCommandAttackTarget(int iSquadMember, AActor* pTarget) -> bool`; or `SquadOrderShootTarget(APawn* pPawn, AActor* Target)` then `ApplyTacticalOrders()`; or `BioBaseSquad.SetMemberAttackTarget` | unproven; the meaning of `iSquadMember` **[verify]** |
| "Garrus, move up there" | `SquadCommand_Move1()` / `Move2()` or `QuickCommandMoveTo(optional int iSquadMember) -> bool`, with the camera's POV swapped to the latched ray for the call; or `SetMemberMoveLocation` plus `BioAiController.PushMoveToPoint(FVector vLoc, float PointTolerance, ...)` | how the native picks its point **[verify]** |
| "Hold position" | `BioBaseSquad.SetSquadFollowFormation(byte eFormation, optional bool bForce, optional int iSquadMember)` with HOLD | **[verify]**; the PC lists Hold apart from Move |
| "Regroup", "on me" | `SquadCommand_Follow()` or `QuickCommandFollowPlayer(optional int iSquadMember) -> bool`; `ClearMovementOrders` | signatures known; **[verify]** |
| "Liara, lift that one" | `SquadOrderUsePower(SFXName nmPower, APawn* pPawn)` then `ApplyTacticalOrders()`; or the member's `BioAiController.AddUsePower(SFXName nmPower, AActor* oTarget, bool bOrderedByPlayer, ...)`; or `SetHenchmanMappedPower` with `QuickCommandUseMappedAction(byte nSlot)` on slot 4 or 5 | all unproven; readiness from `UBioPower.IsEnabled` and `GetCooldownRemaining` **[verify]** |
| "Use grenades" | `SquadOrderUseGrenade(APawn* pPawn)` then `ApplyTacticalOrders()`; or `BioAiController.AddThrowGrenade` | unproven **[verify]** |
| "Be more aggressive with powers" | `BioGamerProfile.SetOptionEx` on the squad power use option (`m_nSquadPowerUse`, a persistent profile write), or per pawn `BioPawnBehavior.m_fPowerUsePercent` with its override flag | unproven **[verify]** |

Several engine assumptions behind this table are open. The move execs take no location, so the native most likely traces from the player camera and searches for cover with the `m_fMoveToCoverSearch*` settings, and since `CameraCache.POV` follows the body rather than the head in VR ([research/40](../research/40-interaction.md)), a voice "move there" needs the POV swapped to the latched ray around the call, as `engine/SelectionGaze` does for the selection, or the lower-level accessors **[verify]**. The attack exec probably uses the current selection or the controller's `m_oCommandTarget`, with `m_fAttackOrderFarAngle`, `m_fAttackOrderCloseAngle` and `m_fAttackOrderCloseDistance` as its cone **[verify]**. `SquadOrderUsePower` probably records a power order the way the weapon order records a weapon, applied by `ApplyTacticalOrders` after `m_fTacticalDelayTime` **[verify]**, and one community source says squadmates use Stasis only when it is picked from the power wheel ([Fextralife](https://masseffect1.wiki.fextralife.com/Abilities)), so a spoken "Liara, stasis" must take the ordered path. One discrepancy needs settling too: research/19 describes the weapon order's `eAction` value 4 as "no tactical action", while the SDK's `EBioTacticalAction` enum names 4 EQUIP and 0 NONE ([research/19](../research/19-body-holsters.md)).

The code splits along the project's existing seams. The host, which isn't hot-reloaded and already owns OpenXR and long-lived workers, owns the microphone, the VAD, the recognizer and the language-model runtime. The hot-reloaded mod owns everything likely to change during development: the context snapshot taken on the game thread at speech start, the 4 s pose buffer, the rule tier, the prompt and grammar text, validation and dispatch, so a better parser needs a rebuild, not a restart. Between them a HostServices v28 pair, for example `setVoiceContext` (mod to host) and `takeVoiceAction` (host to mod), follows the atomic slot pattern of `vr::game_menu::Slot`, in which a newer request replaces one not yet taken ([src/core/vr/GameMenuRequest.hpp](../src/core/vr/GameMenuRequest.hpp); [src/mod/ModApi.hpp](../src/mod/ModApi.hpp)), and both sides share one clock so word times map onto pose samples. Every game call runs in `MevrModTick` on the game thread, never off it, with off-thread requests queued through `plugin/GameThreadCalls` ([src/plugin/GameThreadCalls.hpp](../src/plugin/GameThreadCalls.hpp)); every class, field, function and parameter resolved through `src/ue3` needs its row in `engine/ReflectCheck` ([src/engine/ReflectCheck.cpp](../src/engine/ReflectCheck.cpp)), and the players' build contains no LExSDKv2, whose headers serve here only as a source of names. The game-independent parts (intent schema, name resolution, deixis kernels, dialogue state, gating rules such as mode 0, combat enabled, member alive, power ready) belong in `src/core/vr` with tests in `tests/Main.cpp`. Every refusal gets a log line and a "Say again?"; voice never picks dialogue choices, which ME3's reviewers found redundant and less reliable than its squad orders ([RPG Site](https://www.rpgsite.net/feature/3054-kinect-effect-commanding-shepards-squad-with-voice)); the menu's Controller guide lists what each present squadmate can be told; and whether the mod can trigger a squadmate's own acknowledgement line as the reply is **[verify]**. Test recordings (`.mevrrec`) hold poses and inputs but no audio ([src/core/vr/PoseRecording.hpp](../src/core/vr/PoseRecording.hpp)), so a sidecar audio file per recording, aligned by timestamp, lets the fake-headset rig replay spoken commands end to end without a person.

## Prove the game calls, then race recognizers under VR load

| Step | What | How (existing tools first) | Pass mark (proposed) |
|---|---|---|---|
| 1. Trace the squad orders | press a real pad's d-pad in an Eden Prime fight while tracing, then call each exec and native directly | `tools/console.ps1 "mevr calltrace 10 squadcommand quickcommand squadorder tactical usepower"`, `mevr instances SFXGameModeDefault`, `mevr call <n> SquadCommand_Attack` ([src/game/ConsoleRequest.cpp](../src/game/ConsoleRequest.cpp)); the `QuickCommand*` natives in Ghidra (`tools/ghidra/decompile.ps1`) | each order's route, target source, index meaning and order-to-reaction delay recorded in a research note, with ReflectCheck rows |
| 2. Benchmark recognizers offline | 200-500 commands: the user's own through Link and Steam Link, plus TTS with game audio mixed in | an offline tool in the style of `mevr-menu-preview`: Moonshine v2 Tiny/Small/Medium on ONNX Runtime, Nemotron 0.6B int8 at 160 and 560 ms in sherpa-onnx with and without hotwords, SAPI with an SRGS grammar | p95 from end of audio to final text under ~250 ms and name/power/weapon accuracy above ~95%, on the RTX 4090 box and a six-core mid-range CPU |
| 3. Measure the microphone path | how far the headset's audio trails its controller input on each link | tap a controller on the headset: the pose's jolt against the click in the audio | one delay per link, which sets the capture tail and the deixis offset |
| 4. A/B the frame cost | idle workers, streaming ASR and an LLM burst at Eden Prime's heaviest cells | `tools/fake-headset.ps1`, `tools/replay.ps1 -Loop`, `tools/perf-ab.ps1`, `tools/perf-probe.ps1` | no measurable rise in late frames or spacewarp against the feature off |
| 5. Build the rule tier and context block | schema, name resolution, deixis kernels, dialogue state, snapshot fields | `src/core/vr`, `tests/Main.cpp`, the mod's snapshot JSON | the share of step 2's corpus the rules parse completely |
| 6. Only then the LLM tier | llama-bench, synthetic data, LoRA fine-tune, per-scene grammar, compact output | `llama-bench -p 64,256 -n 16 -t 4,6,8` on the dev CPU with Qwen3-0.6B and Granite 4.0 350M/1B; Gemma 4 E2B as the audio A/B | accuracy on what the rules leave open; p95 under ~150 ms |
| 7. Replay spoken commands | sidecar audio per recording | `tools/record.ps1`, `tools/replay.ps1` | a recorded fight replays its orders end to end on the fake headset |

Step 1 comes first because it is cheap, needs no new code and decides whether voice has anything to drive beyond weapons: if `QuickCommandAttackTarget` or `SquadOrderUsePower` doesn't take an explicit target, the deixis machinery needs the POV-swap technique around a parameterless exec instead, and the game's own tactical delay could consume a third of the budget by itself. Steps 2 to 4 replace every extrapolated number in this report with one measured on a desktop beside the running game, the gap all seven research notes share; the pass marks are proposals to adjust once real distributions exist. Step 5's fall-through rate decides whether the language model is a necessity or a nicety: if the rules parse nine commands in ten, the LLM's speed matters only for the tenth, and shipping without it becomes an honest first release.

## Conclusion

The model is the smallest part of this problem. Every published system that answers in tens of milliseconds constrains rather than generates, and every hard case in the example ("those", "on the bridge", "equip your sniper" while Shepard's weapon is holstered) is solved by game state the mod reads and code it runs, not by a cleverer network. The context block is the main investment, and it pays three times: its names bias the recognizer, its legal values constrain the parser, and its id-tagged groups resolve references. It also serves the pointing-based squad orders already on the roadmap, so it remains useful even if voice stalls on microphone or accent problems.

Push-to-talk, a streaming CPU recognizer and the parser acting as its own endpointer put voice orders under 300 ms without touching a GPU that VR has already spent, which makes the feature viable on the mid-range rigs most Quest 3 players own rather than only on the dev machine's RTX 4090. The open questions have changed character: they are now the microphone's lag over Link, the game's own order delays and how often players say something the rules can't parse, and each is a measurement the project's test loop can take, not a research question.

## 2026-11-04, 21:58-22:04: the squad orders traced in the game

The step-1 trace, on the fake headset (`tools/launch-game.ps1 -Steam`, the VR00 quicksave: on foot at the UNC20 drop
zone beside the Mako, Ashley the one squadmate, no enemy in the level's loaded part), console only (`tools/console.ps1`,
`mevr calltrace`, `mevr call`, `mevr near`, `mevr props`). The call-trace hook goes in with hand aim, so a short replay with
tracked controllers (`tools/replay.ps1 -Path <any recording>`) was needed before the first trace.

- **The d-pad's execs are one-line script wrappers over the natives:** `SFXGameModeBase.SquadCommand_Follow` calls
  `BioPlayerController.QuickCommandFollowPlayer`; `SquadCommand_Move1` and `SquadCommand_Move2` both call
  `QuickCommandMoveTo` (so they differ only in the member index they pass, 0 and 1 by every sign: with one squadmate
  Move2 moved nobody); `SquadCommand_Attack` calls `QuickCommandAttackTarget`. All four run through `mevr call` (ProcessEvent)
  and return true.
- **Follow** (22:02:21): the native set the squad's follow formation (`BioSquadPlayer.OnSetSquadFollowFormation` fired from
  native code) and **the game's own acknowledgement bark path ran** (`BioBaseSquad.SquadHandleChatterEvent`): the squadmate's
  reply to a voice order is the game's, for free. Ashley started back within about a second (8 m covered by 22:02:23) and
  stopped 3.5 m from Shepard at 22:02:28.
- **Move** takes its point from the player controller's facing: the first `SquadCommand_Move1` (facing +X) sent Ashley 31 m
  along +X (`m_fMoveToDropDistance` is 4000 units: the point is dropped up to 40 m along the facing where the trace hits
  nothing); with `set BioPlayerController Rotation (Pitch=0,Yaw=16384,Roll=0)` (facing +Y, toward the Mako) in the same
  console batch as the call, she went 2.2 m along +Y, to the Mako's side: the trace stopped at it. So a pointed "move
  there" can set the controller's rotation (or the camera POV, as `engine/SelectionGaze` does) for the call **[verify in
  VR, where Halcyon's camera drives the view]**. A third Move1, back to +X with her still at the Mako, moved her nowhere
  within 8 s: whether a second move order while one is assigned is ignored, or the point was unreachable, is open
  **[verify]**.
- **Attack** with no enemy did nothing visible (the henchman AI's `GetNextPredictedMoveLocation` events ran), and the
  controller's `m_oCommandTarget` was Shepard's own pawn: the attack order's target comes from the selection or that field,
  to be traced with an enemy present **[verify]**.
- **The order settings, live** (`mevr props` on `BioPlayerController_1`): `m_fMoveToDelayTime` 1.5, `m_fTacticalDelayTime`
  1.0 (the game's own delay before a tactical order is applied: a third of the 500 ms budget is already spent by the game
  itself if it applies to spoken power orders **[verify]**), `m_fMoveToDropDistance` 4000, cover search radii 350 / 800 /
  1600 and ranges 1200 / 2400, `m_fAttackOrderFarAngle` 5, `m_fAttackOrderCloseAngle` 14, `m_fAttackOrderCloseDistance` 500,
  `m_aActionMap` empty.
- **Not traced:** power and weapon orders (`SquadOrderUsePower`, `SquadOrderShootTarget`) and the attack with a target need
  a save with enemies (Eden Prime's Save 7 or the tower autosave, research/50). Ghidra's project was locked by another
  session, so the natives' bodies weren't read.

**The user's requirement, the same day:** "independent of voice mode, we will need to support the previous system (flat)
of squad commands, though it may require a new gesture system." So every order path, a gesture, an ME3-style spoken "name,
power" or a model's line, ends in one struct and one dispatcher.

## The representation: `core/vr/SquadContext` (2026-11-04)

The user asked for the world and state representation before more in-game calls. [src/core/vr/SquadContext.hpp](../src/core/vr/SquadContext.hpp)
(unit-tested in `tests/Main.cpp`, `TestSquadContext`) is the game-independent model behind every squad order:

- **`Scene`:** what the mod snapshots on the game thread when speech starts (or a gesture begins): the player's pawn and
  eyes, the facing yaw, combat and Shepard's weapon, the party (letter, present, alive, weapons carried and in hand, powers
  with the seconds left), the hostiles (a stable `PawnId`, position in metres, a type word, health, rendered recently, an
  optional surface word), the gun's hit and the gaze selection. The game's left-handed frame: X forward at yaw 0, Y right,
  Z up, so bearings are + to the right.
- **`Grouper`:** single-linkage clusters (`joinM` 6, split across `splitAboveM` 2.5 of height: the bridge is its own
  group), the nearest `maxGroups` 6, ordered left to right, **ids kept across updates** by member overlap (a group keeps
  its E-number while any member lives; a wiped group's number is never reused in the encounter; `Reset` on a new level);
  per group the type word (the members' most common), count, sector (ahead within 30 degrees, behind past 120),
  band (near < 10 m, far > 30), elevation (level within 1.5 m of the player's height), surface, visibility, and the ordinal
  flags against the other groups (nearest, farthest, leftmost, rightmost, highest, biggest when strictly largest).
- **`PoseHistory` + `Resolve`:** a 4 s buffer of the head's and both hands' world rays; for a deictic word at time t, each
  source's temporal kernel (head: rises from t-1.3 to a peak at t-0.4, gone by t+0.4; hand: t-1.2 to a peak at t, gone by
  t+0.5) weights its samples, the angular kernel is a Gaussian in the angle to the group's nearest member (head sigma 11
  degrees with a 6-degree upward bias, hand 3 degrees), the group's score is the best source's (a hand held on it all along
  scores 1; a hand outweighs a head 1.5:1), the top three above 0.05 are the candidates. The kernels are research/54's
  starting values, to fit on recordings.
- **`Action`:** who (a letter, ALL, LAST), a weapon, a power code, the order (attack, move, follow, hold, regroup, use,
  cancel, repeat) and the target (E id, PT n, AIM, GAZE, LAST, OTHER, SELF), with the one-line form `G|SR|-|attack|PT1`
  both ways (`Line`, `ParseLine`: a model's answer, a rule parser's result and a gesture's order all become this).
- **`Validate`:** the action against the scene: the member present and alive, the weapon carried, the power owned (a
  cooldown is the one soft refusal: `notReady`, `readyInS`), the target resolved to a group (PT n to its best candidate with
  the runner-up kept for "no, the other ones"; none to the gun's target, then the gaze; LAST and OTHER from the command
  history, OTHER preferring the same enemy type; "repeat" the last command over, for another member if named).
- **`Block` and `Prefix`:** the dynamic text block exactly as research/54's schema (NOW, PARTY, SHEP, one line per group,
  AIM | GAZE, each PT with its candidates, LAST with ages, SAY) and the cached prefix from a roster.

Not yet built: the mod's snapshot (`game/` reads of the party's powers through `BioEpicPawnBehavior.m_powers`, the hostiles'
type words from their archetypes, a surface lexicon), the pose history fed from the host's controller frames, the rule
parser, the gesture system for the flat orders, and the dispatcher that turns a `Resolved` into the game calls above.

### Fed from the game: `game/SquadContext` (2026-11-04, later)

The user chose feeding the scene before the gesture system. [src/game/SquadContext.cpp](../src/game/SquadContext.cpp) (mod,
hot-reloaded; `[SquadContext]` in `ASI\LE1MEVR.ini`: `enabled`, `refreshS` 0.25, `joinM`, `splitAboveM`, `maxGroups`, `nearM`,
`farM`) runs in the mod's tick after `interact`: on foot with a tracking frame it pushes a pose sample every tick (the head's
line of sight from the frame, both controllers' aim rays through `vr::controllers::Latest()` and the frame, all in the world,
in metres) and every `refreshS` reads the Scene: the player's pawn and the eyes, the facing from the head's forward, combat
from `BioPawn.IsInCombatActionState`, Shepard's weapon from `weapon::HeldObject()` typed by the squad module's `RangedType`
(`squad_weapons::WeaponTypeOf`), the party as the squad menu last read it (`squad_weapons::Members()`: class id to the letter,
quick slots, the weapon in hand, health) with each member's powers from `BioEpicPawnBehavior.m_powers` (`UBioPower.m_nmPower`
as the name, `m_fCooldownRemaining` as the seconds left **[verify the field is the live cooldown; `GetCooldownRemaining` is
the alternative]**), the hostiles from `WorldInfo.PawnList` (`BioPawn.IsHostile` on Shepard's pawn, `IsDead`,
`GetPercentHealth`, `LastRenderTime` within 1 s of `TimeSeconds` for visibility) with the type word from the actor type's
`ActorGameName` (else the behaviour's; logged once per word: `squad context: a hostile of type "..."`), the gun's target
from the sight line's trace (`sights::ConvergeActor`, the hit actor kept in `WorldSight`) and the gaze target from the game's
selection (`interact::Target`). Power codes are made from the internal names (the first letters, unique per session:
`CodeOf`), the spoken names by splitting the CamelCase (`Spoken`); a table of LE1's real power names replaces both once a
session has logged them. The roster of squadmates seen this session feeds `Prefix()`. `mevr context [word [secondsAgo]]`
prints the block with `PT1 "<word>"` resolved from the pose history that many seconds ago (0.5 by default); `mevr context
prefix` the prefix. Log `squad context:` (the settings, the type words, the status block), snapshot `"mod"."squadContext"`
(party with powers, hostiles, groups with words, the aim and gaze groups, the pose samples kept). New ReflectCheck rows:
`m_powers`, `m_nmPower`, `m_fCooldownRemaining`. Built (`build-ctx`, the mod and the tests, no warnings) but **not yet run in
the game**: the first session should check the type words and the power names in the log, `mevr context those 0.5` while
pointing at a group, and the cost of the read (a `PawnList` walk with two ProcessEvents per hostile every 0.25 s).

## The flat orders: the gesture and the dispatcher (2026-11-04, later)

The user chose the flat orders next. Two pieces, built but **not yet run in the game**:

- **The rules**, [src/core/vr/SquadGesture.hpp](../src/core/vr/SquadGesture.hpp) (unit-tested, `TestSquadGesture`): a LEFT grip
  squeeze that no other feature claimed (the host's `leftGripAway` count excludes the support grip's zone; `throwables::ClaimedLeft`
  and `vent::ClaimedLeft` are checked) starts a hold; the left aim ray is the pointer; letting go gives the order from what the
  ray ends on: an enemy (within `enemyConeDeg` 8 of a hostile) = attack that group; the ground farther than `followWithinM` 2 =
  move there; the ground near Shepard, a squadmate or nothing = follow. The squadmate the ray pointed at (`memberConeDeg` 12)
  when the squeeze began gets the order alone, else everyone. A squeeze shorter than `minHoldS` 0.08 is a blip, longer than
  `maxHoldS` 8 is called off, and so is a hold that stops being usable (off foot, untracked, no squad). Haptics: a tick at the
  start, a lighter one each time what the ray ends on changes, a stronger one at the order. Every left-grip gesture of the mod
  is then: the support grip on the gun, a grenade at the chest, the vent at a hot receiver, and an order anywhere else.
- **The dispatcher**, [src/game/SquadOrders.cpp](../src/game/SquadOrders.cpp) (mod; `[SquadOrders]` in `ASI\LE1MEVR.ini`:
  `enabled`, `gesture`, `followWithinM`, `enemyConeDeg`, `memberConeDeg`, `minHoldS`, `maxHoldS`): `Give(Action)` validates any
  action against the squad context (`sc::Validate`, with the dispatcher's own history of the last three commands for LAST and
  OTHER) and calls the game on the game thread: follow (and hold, as follow until the hold order is found, and regroup) =
  `BioPlayerController.QuickCommandFollowPlayer(iSquadMember)` per recipient; attack = `QuickCommandAttackTarget(iSquadMember,
  pTarget)` with the group's nearest living member as the target; move = `QuickCommandMoveTo(iSquadMember)` with the
  controller's `Rotation` turned toward the point for the call and put back after (journalled `SquadOrders.Rotation`; the trace
  showed the native drops the point along the controller's facing), the point from the gesture's trace (`Engine.Actor.Trace`
  along the ray, 100 m, bisected when the hit point doesn't come back) or, from the console, the target group's centre.
  `iSquadMember` is the squad's own index as `squad_weapons` reads it (`MemberView::squadIndex`) **[verify: the trace's Move1
  moved the one squadmate; whether index 0 is Shepard or the first henchman decides whether Move1 = index 0 or 1]**. Powers
  (`SquadOrderUsePower`) and weapons aren't dispatched (the squad weapon menu gives weapons). The natives' return values are
  logged per call. `mevr order WHO|WEAPON|POWER|ORDER|TARGET` (e.g. `ALL|-|-|attack|E1`, `G|-|-|follow|-`, `W|-|-|move|E2`) gives
  an action line through the same path: the voice parser's output will go in there. New ReflectCheck rows: the three natives,
  their parameter blocks and parameters. Logs `squad orders:` (the settings, `pointing an order`, `let go on ...`, each order's
  line and outcome); snapshot `"mod"."squadOrders"` (holding, the pointer's hit, the last outcome, counts).

What the first session should check: the squad index (a Move for "everyone" should move both squadmates; if only one moves,
the index base is off by one), whether `QuickCommandAttackTarget` makes the squad attack the pawn given or still the game's own
selection, whether the Rotation swap lands the move point where the ray hit (and whether Halcyon's camera, not the
controller's rotation, drives the native's trace in VR), the cost of the trace while holding, and the gesture's feel against
the support grip's and the grenades' zones.

### Placement by an arc: the right B (2026-11-05, evening)

The user, once the grenade had become a gesture: "we need to use the right click and something like the teleport gesture to
place our squad mates manually". The left grip's pointing already moved them (above), but with a straight ray, no mark on the
ground, and on a grip the support grip, the grenades, the vent and the radio all share. The user picked the right B held (B
melees nothing on foot since `meleeButton = 0`) and asked this session to build it. Built in 7bd3dd7:

- **The rules**, [src/core/vr/SquadPlace.hpp](../src/core/vr/SquadPlace.hpp) (unit-tested, `tests/SquadPlaceTests.cpp`): the
  arc is a throw from the right hand's aim pose, `placeSpeedMS` 14 m/s under 9.8 m/s² (about 20 m aimed at 45 degrees, 7 m
  level from the hand's height), cut into 0.08 s segments up to 3 s or `placeRangeM` 30 m flat. Its landing is floor when the
  hit's normal is within about 45 degrees of up (`minUpNormal` 0.7), else a wall; nothing hit in reach is nothing. A press
  shorter than `placeMinHoldS` 0.15 s is a blip, one held 15 s is called off, and one held through a place it couldn't start (a
  wheel, a menu) never starts until B is let go. The squadmate the right hand's straight ray points at (`memberConeDeg`) as B
  goes down gets the order alone. Let go on floor: they go there and hold it (below), or a follow when it lands within `followWithinM` of Shepard;
  on a wall or nothing: called off.
- **The game side**, [src/game/SquadPlace.cpp](../src/game/SquadPlace.cpp) (mod): while B is held on foot it traces the arc's
  segments with `Engine.Actor.Trace` (`bTraceActors` false: level geometry only, so pawns and movable things don't stop the
  throw; the first hit ends it) every tick, and the order goes through `squad_orders::GiveAt`, the one dispatcher, so the move
  gets the Rotation swap, the acknowledgement and the standing bookkeeping like every other order. It stands down while B is the
  game's melee button (`[Throwables] meleeButton = 1`, logged once), while the squad's or the power wheel is open, and anywhere
  but on foot. `mevr place aim <pitchDeg> [yawDeg]` holds B from the head (no controllers), `mevr place release` lets go.
  Logs `squad place:`; snapshot `"mod"."squadPlace"`.
- **The drawing**, [src/plugin/SquadPlaceDraw.cpp](../src/plugin/SquadPlaceDraw.cpp) (host, HostServices v32 `setSquadPlace`):
  the mod sends the arc's points, the landing and its normal in tracking space each tick; the host draws them in a projection
  layer of its own with the galaxy hologram's renderer (additive light, 4x MSAA at 0.6 of the eye's size, only while the view is
  active and fresh): a ribbon about 0.15 degrees wide with a bead running toward the end, a ring lying on the floor (blue for
  the squad, amber for one squadmate, green for "on me") that grows in over 0.15 s, a red spark where a refused flight ends.
  The layer draws over the world (no depth test against the scene), like most games' teleport arcs. Layers `squadplace-left`/
  `-right` in snapshots.

**In the game, 2026-11-05 18:12-18:23** (fake headset, the UNC20 save, Ashley in the party; `mevr place aim`/`release`): the arc and
the ring draw as meant (a 1406x1435 layer per eye, 0.17 ms of CPU a frame for both eyes; four or five traces a tick while aiming).
Getting Ashley to the ring took four findings, all in the dispatcher's move and hold:
1. **`QuickCommandMoveTo` ignores the point in VR.** A move to a point 5.7 m ahead sent her 32 m ahead; one at 4.9 m and 40
   degrees to the side sent her 30 m straight ahead again. Turning the controller's `Rotation` toward the point (the 2026-11-04
   trace's way), also the camera's `CameraCache.POV` rotation, and setting `m_fMoveToDropDistance` to the point's distance for the
   call changed nothing: the native's point comes from the view along the head's facing (level), most likely when the order is
   applied after `m_fMoveToDelayTime` (1.5 s), after anything swapped for the call is back **[verify in Ghidra: the project was
   locked by another session]**. So every move through it lands along the head's facing, until the first rise in the ground.
2. **The HOLD formation keeps a squadmate where she stands.** `SetMemberHoldPosition` then `SetSquadFollowFormation(HOLD)` read
   back HOLD but she never walked: setting the formation takes her place as the hold position, over the one just set.
   `CallHold` now sets the formation first and the point after it.
3. **The hold doesn't walk them there either**, so placement also pushes each recipient's own AI to the point:
   `BioAiController.PushMoveToPoint(vLoc, 60 units, run, no precision)` on the pawn's `Controller`.
4. **The standing order's re-assert went through `QuickCommandMoveTo`** ("move (drifted)" every `reassertS`), dragging her back
   30 m out each time: a drifted hold under the HOLD formation now re-asserts its hold position, and a retreat with the formation
   is the hold alone (no move first); a formation hold no longer sends a move before it either.
With all four: let go 7.2 m out at 50 degrees to the left, Ashley ran there and stood 0.6 m from the ring's point, kept there
("A keeps a standing hold order until relieved, and stays"). Placement is therefore a **hold until called back**
(`squad_orders::GiveAt(..., untilCalledBack)`: the standing order's "until I say so" and "stay there"), released by a follow
(the left grip on her, `mevr order ALL|-|-|follow|-`, or dropping the arc at your feet). **Still open:** the left grip's "move
there" and spoken moves still go through `QuickCommandMoveTo` and land along the head's facing (the voice session's to decide:
the same push, or a hold); two squadmates and a crowd of hostiles weren't tried.

**The voice and gesture moves take the same road** (2026-11-05, later): the dispatcher's `Order::Move` (the left grip let go
on the floor, "Ashley, move up", a model's `A|-|-|move|PT1`) was a `QuickCommandMoveTo` behind a turned `Rotation`, which the
placement's check above shows lands along the head's facing in VR. With `holdFormation` (the default) a move is now the HOLD
formation at the point followed by the squadmate's own `PushMoveToPoint` (`squad_orders::PushMove`), and a standing hold there
until it's calm (then they follow), relieved, or "stay there"; `holdFormation = 0` keeps the old call. The afternoon's in-game
"Ashley, move up" (`QuickCommandMoveTo` true) therefore sent her up the head's line, not the hand's: not re-checked yet, the
game folder was the install test's.

#### The selector and the double tap (2026-11-05, late evening)

The user, after the first build: picking a squadmate by pointing fails exactly when it matters, out of sight, behind cover ("we
can't rely on sight alone"; there's no d-pad), and asked for "a mini selector menu ... hold b, choose between squad mates, and the
arc on the hand denotes placement", and "double b will have the squad all return to shepherd". Built in 5ff56ac:
- **The selector** ([core/vr/SquadPlace](../src/core/vr/SquadPlace.hpp), unit-tested): tiles in ME1's d-pad order, [first squadmate
  | Squad | second] with two (`MemberOfTile`; the first is `squad_weapons::Members()[0]`), [Squad] alone with one; each hold starts on
  the squad. A sideways flick of the right stick past 0.6 steps one tile, re-armed inside 0.3 (a stick already pushed at the press
  waits for the centre); holding it over never repeats. The pick is felt: one 30 ms pulse for the first squadmate, two for the second
  (90 ms apart), a 90 ms one for the squad. Choosing and aiming are one gesture: hold, flick, aim, let go.
- **The stick is the selector's while B is held:** `gamepad::Options::squadPlace` (`kFlagSquadPlace`, bit 26; an older host ignores
  it and the game's own turn still sees the stick) keeps the right stick centred for the game, and game/comfort's snap or smooth turn
  reads nothing from it.
- **The double tap:** a blip (shorter than `placeMinHoldS`) and a second press within 0.35 s of its release is a follow for everyone
  at once (`squad_orders::Give`), never a hold. A placement let go and a quick press after it isn't one (only a blip arms it).
- **The drawing** (host, `plugin/SquadPlaceDraw`): the tiles stand 9 cm above the hand across the line of sight, about the same
  angular size at any reach, the lit one larger, in its colour and underlined, the others grey; the ring's label names who goes
  (or "Regroup"). The names are the menus' Rajdhani from `ASI\fonts`. The View grew (tiles, the lit tile, names, the hand), still
  v32 (size-checked).
Pointing to pick is gone. Not yet seen in the game (the game folder went to the install test, then the user's playtest).

## The rule parser (2026-11-04, later)

Tier 1 of research/54's parser design, [src/core/vr/SquadParser.hpp](../src/core/vr/SquadParser.hpp) (pure, unit-tested,
`TestSquadParser`): `Parse(transcript, scene, groups, roster)` gives one `Action` per clause with a confidence and what it
couldn't settle, the deictic words as `PT n` with their word positions (for the pose history: the recognizer's word times, or
the position as the fallback), and the correction and cancel flags. How it reads: the present squadmates' names and aliases
match fuzzily (edit-distance similarity, a shared first letter worth a tenth, the best present name at 0.5 or better with a
0.2 margin over the next: the recognizer's "Gareth" is Garrus beside Wrex), powers by the roster's spoken names, weapons and
the orders by a vocabulary of everyday phrasings ("take out", "fall back", "on me", "hold position", "switch to"), targets
by deictics ("those", "that one", "over there": a place with a move verb), descriptions matched against the groups' own
words (sector, nearest/farthest/biggest, above/below, the surface word with synonyms (bridge = catwalk = walkway), the type
word), and the explicit kinds ("my target" AIM, "what I'm looking at" GAZE, "the others" OTHER, "same target" LAST); "you
too"/"again" are repeats; a leading "no" is a correction; "and"/"then" before a verb starts a new clause, the squadmate
carried over. A description that fits one group is the target over a deictic; fitting several it takes the nearest at 0.7
confidence; words it can't place cost 0.85 each and are listed in `unresolved`, which is where a model would take over.
The example parses to `G|SR|-|-|-` and `G|-|-|attack|E1` with `PT1 "those"` at word 7, confidence 1. Wired as **`mevr say <words>`** (`squad_orders::Say`): the words go through the parser with the live scene, groups and roster;
each clause's deictics are resolved from the pose history 0.5 s ago (typed words have no times), a place deictic with a move
traces the left hand's ray for the point, each action is validated and dispatched like the gesture's, and a parse below
`[SquadOrders] minConfidence` (0.6) is refused with the unresolved words (where a model would take over). The typed stand-in
for the recognizer: `tools/console.ps1 "mevr say Garrus, take out those troops on the bridge"`. Not yet run in the game.

## Standing orders: memory above the game's one-shot orders (2026-11-04, later)

The user, before any in-game check: "how we'd handle more complex grammar and memory, like 'hold the bridge': they would hold
at the position, but when the fighting stops they'd likely return to Shepard, or if they're getting overwhelmed they'd try to
retreat; more advanced squad AI." And: what did the research say about this kind of behaviour?

**What the research covered, and what it didn't.** The precedents all put a code layer between the language and the game.
PUBG Ally (Krafton, 2026) runs a behaviour tree for everything fast (movement, aiming, immediate combat) and gives the 2B model
only the reasoning; F.A.C.U.L. (Tencent) ranks targets in code from the player's position, facing and cover and hands the
model a classification; ME3's Kinect orders were one-shot with the target latched when spoken. ReALM and KnowNo gave the
dialogue-state rule (keep the state in code, show the model a summary, act on a confident best guess and keep corrections cheap).
None of the sources describes standing orders with conditions, a retreat rule or how a game's own squad AI fights such a layer;
the engine side found LE1's primitives (`QuickCommand*`, the per-member records in `BioBaseSquad`, the AI controller's own job
queue, `m_fPowerUsePercent`) and nothing like "hold until clear". So this design is the precedents' split (intents in code, the
model only names them) applied to LE1's primitives, not a measured result: the thresholds below are starting values, and
whether LE1's henchman AI accepts being re-ordered every few seconds, or fights it (its own follow formation, cover search
`m_fMoveToCoverSearch*`, the 1.5 s move delay), is the first thing to measure **[verify]**.

**The engine**, [src/core/vr/SquadStanding.hpp](../src/core/vr/SquadStanding.hpp) (pure, unit-tested, `TestSquadStanding`): one
`Standing` per squadmate holds an `Intent` (HoldAt a point or AttackGroup; until Calm / GroupGone / Seconds / Relieved; `returnAfter`;
`mayRetreat`) and advances on an `Observation` each tick (alive, health, position, hostiles near the point and near the squadmate,
the group alive, the player's combat state), answering with an `Issue` for the game: MoveTo when a held squadmate drifts past
`holdRadiusM` 4 (at most every `reassertS` 4), Attack again every `reassertS` while the group lives, Follow when the order ends by
itself with `returnAfter` ("then come back", the default; "and stay there" turns it off), and Follow at once when hurt
(`retreatHealth` 0.3) or overwhelmed (`overwhelmCount` 3 hostiles within 8 m) unless "no matter what". Calm means no living
hostile within `calmRadiusM` 25 of the point and no combat for `calmS` 8; a standing older than `maxAgeS` 180 is stale. `[SquadOrders]
standing = 0` makes every order one-shot, as the game's own.

**The grammar** (`core/vr/SquadParser`, `StandingHint` per clause): "until it's clear" / "until the fighting stops" (Calm), "until
they're dead" / "until they're gone" (GroupGone), "until I say so" (Relieved), "and stay there" (stay), "no matter what" / "at all
costs" (no retreat), "then come back" / "then regroup" (come back, said). "Hold the bridge" takes the place from the enemies'
surface word (the game has no place objects: the group on the bridge gives the point), "hold there" from the pointed place,
"hold here" the squadmate's own position. Short words now match exactly ("there" is one letter from "these"); the fuzz is for words
of six letters or more.

**The dispatcher** (`game/SquadOrders`): a hold moves them there (`QuickCommandMoveTo`, no longer a follow) and keeps a HoldAt
standing; "move ... and stay / until ..." keeps one too; an attack keeps an AttackGroup standing until the group is gone; follow and
regroup drop the recipient's standing; "belay that" drops them all (the game's own order can't be undone). Each tick the standings
are observed from the squad context's scene (the squadmate's health as the squad menu read it, the hostiles' places) and each
Issue becomes the game call, logged as `squad orders: <who>'s standing <kind> order: <issue> (<why>) -> <return>` and `... ends
(<why>)`; snapshot `"mod"."squadOrders"."standings"`.

**Still open:** a real hold order (LE1's `SetSquadFollowFormation` with a hold formation, or the Tactics HUD's Hold) would beat
re-asserting a move; retreat as a move to cover between the squadmate and Shepard rather than a follow; "overwhelmed" from the
squad AI's own perception (`m_aPerceivedSquads`) rather than our distance count; and whether a re-issued `QuickCommandMoveTo`
interrupts the AI's current job or queues behind it **[verify]**.

### A real hold: the game's own HOLD formation (2026-11-04, later)

The user chose the real hold over a retreat to cover. LE1's squad has it: `BioBaseSquad.EFollowFormation` (FOLLOW 1, HOLD 2),
`SetMemberHoldPosition(nIndex, vPosition)`, `SetSquadFollowFormation(eFormation, bForceFormation, iSquadMember)` (the route the
Follow native itself took in the trace, firing `OnSetSquadFollowFormation` and the acknowledgement chatter) and `GetMemberFormation
(nIndex)` to read it back. `CallHold` in `game/SquadOrders` sets the member's hold position and forces HOLD for that member on
the player's squad (Shepard's behaviour's `Squad`), after a `QuickCommandMoveTo` when the point is farther than `holdRadiusM`,
and logs the formation read back ("now HOLD"). The standing order stays on top (calm, retreat, drift) and its end releases the
hold through the follow order, which sets FOLLOW again. `[SquadOrders] holdFormation = 0` keeps the re-asserted move alone.
Whether the HOLD formation makes the AI stay put against its own follow logic, and whether a hold position far from the
squadmate makes them walk to it or needs the move first, are the next in-game checks **[verify]**. Also fixed:
`squad_weapons::WeaponTypeOf` is LE1-only (LE2/LE3 return -1), which had broken the trilogy build since b7c7e56.

### Retreat to cover, and a compound order (2026-11-05)

**Retreat to cover** (the user's pick over the squad's own perception): a squadmate who gives up a standing order no longer
runs to Shepard's heels. [src/core/vr/SquadRetreat.hpp](../src/core/vr/SquadRetreat.hpp) (`Pick`, unit-tested) finds the point:
`retreatM` 8 back along the pull toward Shepard and away from the hostiles' centre (flat; the floor decides heights), never
closer to Shepard than `stopShortM` 2.5, then the nearest cover candidate within `coverSearchM` 10 of it that is farther from
the threat than the squadmate is (behind them, as the threat sees it); without one, the open point. The standing engine turns
the retreat into a new hold at that point (until calm, then back; a second retreat only after `retreatCooldownS` 10), so
"hurt" or "overwhelmed" reads `retreat to cover (x, y)` in the log and the hold's rules carry on from there; `retreatToCover =
0` keeps the old follow. The candidates come from the level's own cover: `Engine.CoverLink` actors (not `bDisabled`), each
slot at the link's place plus its `LocationOffset` in the link's axes, gathered by a walk over the live objects only on the
tick a retreat is possible (new ReflectCheck rows: the class, `Slots`, `bDisabled`, the slot's `LocationOffset`). The retreat
is a `QuickCommandMoveTo` toward the point plus the HOLD formation there. Open: whether the AI's own cover logic takes the
slot or stands beside it, and a slot's facing (we ignore `RotationOffset`) **[verify]**.

**"Liara, target that krogan, Ashley, switch to your sniper and support Liara"** (the user's question): until tonight the
parser split clauses only at "and"/"then" before a verb it knew, had no "target" or "support", and let the last name win, so
it would have given Liara everything at low confidence. Now a squadmate's name after an order starts a new clause (a
vocative), except as the object of a support verb ("support / cover / back up / help / protect / stay with Liara") or right
after "you too" / "same"; "target" is an attack verb; "support X" is its own intent (`Clause::supportOf`), which the dispatcher
resolves at the time of the order: X's standing attack group, else X's held point (a move there), else X's last target in
the history, else X's own position, else the gun's target. The sentence parses to `L|-|-|attack|E4` (the krogan group by
its type word), `A|SR|-|-|-` (an equip, now dispatched through the squad menu's own switch, `squad_weapons::GiveWeapon`) and
`A|-|-|attack|-` supporting L, at confidence 1, and the three are given in order, so Ashley's support finds Liara's attack
standing from the first clause. Short vocabulary words match exactly now ("there" is one letter from "these").

### The corpus: what a player says, and how much of it the rules take (2026-11-05)

Research/54's plan, step 5: the share of a corpus the rule tier parses completely decides whether the model tier is a
necessity or a nicety. With the game unavailable, the corpus is written, not recorded: [tests/SquadPhrasings.cpp](../tests/SquadPhrasings.cpp)
holds 196 lines a player would say over the radio in one fight (Garrus and Wrex along, three geth groups: E1 troopers on a
bridge left and above, E2 a destroyer ahead, E3 troopers right), each with the line it must become at a confidence the
dispatcher acts on (0.6), or the reason it must be refused, or the shortfall it records. It runs with `mevr-tests` and prints
the figure. **First run: 164 of 173 orders, 18 of 18 non-orders refused.** The nine misses were vocabulary collisions, not
grammar: a weapon named with a target ("sniper, those on the bridge") was an equip without the attack; "hit him with" ate the
"him"; "pull out" meant retreat and equip at once; "get back here" followed to a place; and a target named without a verb
("Wrex, the destroyer") didn't count as an order, so the clause after it wasn't split off. **After them: 173 of 173, 18 of 18, 5
known gaps open.** What the parser learned on the way (all in `core/vr/SquadParser`, unit-tested):

- **Phrasal verbs split around their object**: "take them out", "light those guys up", "pick the troopers off", "finish him off",
  "wipe them out", "put them down" (a table of verb + particle with up to three words between).
- **Directions** are a target kind of their own, `Target::Direction` (`core/vr/SquadContext`; the line form `LEFT` / `RIGHT` /
  `AHEAD` / `BEHIND`): "go left", "flank right", "move back", "cover the left" are a point 8 m that way from the player's feet
  (`DirectionPoint`, the dispatcher's `kDirectionM`), never the group that happens to be on that side; "move up", "push",
  "go go go" with nothing else are forward; an attack that way ("hit the left") is the nearest group in that sector.
- **The owner of a power is implied**: "throw them", "overload", "hit the big group with warp" go to the one present squadmate
  who has that power (the roster's codes against the party); with two owners or none the clause still asks "who".
- **A power with an attack verb is its use**: "hit him with an overload", "overload the destroyer's shields".
- **"Stay on him" attacks, "stay on the bridge" holds**: a hold verb on a thing (a type word, a pointed thing, AIM/LAST) keeps
  attacking it until the group is gone; on a place (a surface, a direction, "here") it holds there.
- **"Cover" without a squadmate after it is a hold**: "cover the door", "watch the left", "guard the bridge", bare "take cover" /
  "get to cover" (the game has no cover order; its AI takes cover around a held point by itself); "cover me" / "watch my back"
  / "help" are follows; "cover Garrus" stays a support.
- **Two named squadmates are both** ("Garrus, Wrex, on me"; "Garrus and Wrex, attack": a conjunction splits a clause only after
  an order), "everyone" after an order is a vocative like a name.
- **"Go sniper" is an equip**, "long range" a sniper, "the three on the bridge" counts nothing.
- **Talk is refused by name, not guessed at**: "I'm hit", "they're flanking us", "nice shot", "watch out", "what the hell was that"
  (a deictic in talk with no verb is weighted down to 0.33) come back below the threshold; "Garrus, I'm hit, cover me" splits the
  talk into its own clause and keeps the order for Garrus. Orders the game has no call for ("hold fire", "cease fire",
  "medi-gel", "reload", "grenade", "heal me") are refused with "the game has no such order" so the player can be told why.
- **The noun after a deictic is pointed at, not unknown**: "that krogan" with no krogan group here is `PT1` at full confidence.

The five gaps on record, for a model or a surface lexicon: "snipe them **from** the bridge" (the description wins over the
pointer); praise before a verbless order ("nice shot, now the destroyer"); "behind the crates" where the level has no word for
the crates; "the big guy" being Wrex's own alias; a report's noun landing in the order that follows it ("I'm hit by the
destroyer, get him"). The figure is for a written corpus in one scene, by one author who also wrote the parser, so it is an
upper bound; the recorded corpus (plan step 2, the headset checklist's voice line) is what replaces it. It still says what step
5 asked: the common command language is covered by rules, and the model tier's job is the residue and the recognizer's
mistakes, not the grammar.

**The acknowledgement, from the sound sets.** `BioPawn.EBioSoundSetType` names the lines a pawn can play: `GiveCommandFollow`
(48) is in use and is what the follow trace heard (`SquadHandleChatterEvent`), `HoldingPosition` (59) exists, and
`GiveCommandAttack` / `GiveCommandMove` / `AcceptedAttack` / `AcceptedMove` / `CompletedMove` are all `UNUSED`: LE1's squadmates
voice a regroup, and nothing else the d-pad gives. So a spoken follow is answered by the game for free, a spoken attack or move
by nothing, and the standing engine's re-asserted moves and attacks can never make them chatter (its retreat is a hold, so the
only bark it can cause is the follow it falls back to with `retreatToCover = 0`). A hold could play `HoldingPosition` through
`BioPawn.PlaySoundSet(59)` if the set has lines for the squadmates **[verify]**; `BioChatterSystem.CanPlayLine` rate-limits each
set by its `m_MinTime`. For attack and move the acknowledgement has to be non-verbal: a haptic tick in the left hand when the
order is dispatched, and a brief marker on the target group (the host already draws the power lock's brackets at a pawn,
`powerlock`): the next piece of the feel.

### The voice path built: the acknowledgement, the microphone, the recognizer, the model tier (2026-11-05)

The user's direction, the same day: "there's a sound effect when moving squad mates, but they usually respond with a voice
note", and "build out everything". So the acknowledgement is the game's, and the rest of the plan was built offline in one
pass (the game machine was another session's all day: nothing here has run in the game yet, [notes/headset-checklist.md]
(../notes/headset-checklist.md) sections 1 and 7).

**The acknowledgement is the game's own line** (`game/SquadAck`). The sound-set enum (`BioPawn.EBioSoundSetType`) has the
lines the user hears: `StartMoveToConfirmation` 70 ("moving"), `MoveToSuccess` 64, `MoveToFailure` 63, `HoldingPosition` 59,
`LOSestablished` 61, `TargetDown` 2, `Allclear` 77; the `GiveCommandAttack` / `GiveCommandMove` sets marked UNUSED were a red
herring. The follow's trace showed the native raising `BioBaseSquad.SquadHandleChatterEvent`, so the move and attack natives
most likely do the same **[verify]**. The module expects an answer after every order the dispatcher gives (not the standing
re-asserts, silent on purpose), hears `SquadHandleChatterEvent` (ProcessEvent) and `BioPawn.PlaySoundSet` (both hooks), logs
which set answered and after how long (`squad orders: Ashley answered the move with sound set 70 (StartMoveToConfirmation)
after 0.31 s`), and when nothing answers within `[SquadOrders] ackFallbackS` (1 s) plays the order's own set on the squadmate
through `PlaySoundSet` (`ackMoveSet` 70, `ackHoldSet` 59, `ackAttackSet` 61 **[verify which set fits an attack]**; 0 = none;
the follow's is always the game's). The UI's own sound for a d-pad order wasn't traced: if the natives play it, our orders
have it too **[verify]**. Power orders dispatch now (`Order::Use`): `[SquadOrders] powerRoute = tactical`
(`SquadOrderUsePower` then `ApplyTacticalOrders`, the weapon order's path, applied after the game's 1 s tactical delay) or
`ai` (the member's `BioAiController.AddUsePower` as ordered by the player), the group's nearest living member as the target,
the power's game name kept by the squad context from `BioPower.m_nmPower`. Both routes [verify] in a fight.

**The microphone and the recognizer** are Moonshine Voice's own C library, not a hand-built ONNX pipeline: the research
agent found that Moonshine AI ships a prebuilt Windows library (`moonshine-voice-windows-x86_64.tar.gz`, v0.1.5, MIT: four
static libraries built /MD with MSVC 14.40, ONNX Runtime 1.23 beside them) with a streaming C API (`moonshine-c-api.h`
version 30000: load the models from a folder, `create_stream`, `add_audio_to_stream` from the capture thread,
`transcribe_stream` for a partial, `stop_stream` then one more call for the final), a built-in VAD that splits lines,
**key-term biasing** (`keyterms`, a comma-separated list compiled into a subword trie that nudges the decoder: the squadmates'
and powers' names), and **word timestamps** (`word_timestamps`: a time and a confidence per word, which is what "those" at
its word's time needs). The models are the v2 streaming ones as `.ort` files (int8, MIT: tiny 44 MB, small 136 MB, medium
257 MB on disk), mirrored on Hugging Face; `tools/fetch-voice.ps1` fetches the library into `external/moonshine-voice` and
the models into `models/voice`, both gitignored, and `tools/deploy-asi.ps1` puts them in `ASI\voice`.

The code: `MEVR-Voice.dll` ([src/voice/Voice.cpp](../src/voice/Voice.cpp), our C interface [VoiceApi.h](../src/voice/VoiceApi.h))
links the library and owns WASAPI shared-mode capture of the headset's microphone (the device whose name holds `[Voice]
device`, else the default communications device; MMCSS "Audio"; the level always measured, the audio dropped unless a
listen is on), the stream's life (`listen on`: start; `off`: `tailMs` more audio, then stop; a worker asks for a partial
every `intervalMs` and the final after the stop, then pushes an end marker), the game's own audio session ducked by `duckDb`
through `ISimpleAudioVolume` while listening **[verify that LE's audio engine honours it]**, and a file feed for the test
loop. The host ([plugin/Voice.cpp](../src/plugin/Voice.cpp)) loads the DLL from `ASI\voice` at attach on a worker
(`LoadLibraryExW` with the DLL's own folder in the search path for `onnxruntime.dll`; without the DLL or the models the game
runs without voice, one log line), listens on the mod's request or while `[Voice] key` is held, reads
`%LOCALAPPDATA%\MEVR\voice.request` ("wav <path>", "listen <s>", "keyterms", "stop"), and hands the mod the lines through
HostServices v31 (`setVoiceListen`, `takeVoiceLine`, `voiceState`, `setVoiceKeyterms`, `voiceCommand`; the structs in
[core/vr/Voice.hpp](../src/core/vr/Voice.hpp)). The mod ([game/Voice.cpp](../src/game/Voice.cpp)) owns the radio gesture
([core/vr/VoiceGesture](../src/core/vr/VoiceGesture.hpp), unit-tested: a new left squeeze with the grip within `radioRadiusM`
0.20 of the ear point, the head's left side `earSideM` 0.08 out and `earDownM` 0.03 down, that throwables and the vent didn't
claim, starts a listen; it claims the squeeze before the flat orders see it; letting go ends it; shorter than `minHoldS` a
blip, longer than `maxHoldS` ended by itself), sets the key terms from the roster every 2 s, joins a listen's lines (the
library's VAD splits "Garrus." from the order) and gives the joined text to `squad_orders::Say` at the end marker, with
"those" resolved `deicticLagS` (0.3 s) before the line's end on the host clock. **The semantic endpoint** from the plan is in:
a partial that hasn't changed for `settleMs` (250) and parses whole, confidently, with no power in it is given before the
release (`earlyDispatch`), and the final is parsed only past the words already given. A refusal is two short buzzes ("say
again?"), an order one tick. `mevr voice wav <file> | listen <s> | keyterms <a,b> | stop | status`.

**Measured on synthesized speech** (`tools/voice-tts-corpus.ps1`: Windows' two desktop voices reading 30 lines of the
parser's corpus, 60 WAVs at 16 kHz; `tools/voice-bench.py` over `mevr-voice-bench`, the audio fed 100 ms at a time; the
CPU shared with another session's running game, so the costs are pessimistic):

| Model | Load | WER | Exact lines | Names | A partial's cost (mean) | The final after the audio's end (real-time feed) |
|---|---|---|---|---|---|---|
| tiny (34 M) | 0.26 s | 9.0% | 40/60 | 64% | 38-52 ms | median 3 ms, p95 4 ms |
| small (123 M) | 0.75 s | 10.5% | 34/60 | 41% | 84-124 ms | median 1 ms, p95 521 ms (a partial in flight) |
| medium (245 M) | 0.84 s | 5.1% | 47/60 | 70% | 131-216 ms | median 3 ms, p95 6 ms |

Two things the numbers settle. The final transcript costs nothing after the release: the stream is already caught up by
the partials, so the budget after the release is the parser's millisecond and the game's own order delay; a partial's cost
is what matters, and medium's 130-220 ms under load means the text is at most that stale when the player lets go. And **the
names are the whole error**: every miss is "Rex", "Gurus", "Caress" for Wrex and Garrus (the synthesizer's own pronunciation
is part of it), which the parser's fuzzy names absorb at a confidence cost ("rex" to Wrex at 0.75, "gurus" to Garrus at
0.67) and which the key-term boost at its default didn't fix (`--boost` is there to A/B; the library's own advice is that a
higher boost puts the terms where they weren't said). The default is `model = tiny` (below: beside the running game its
partials cost a third of small's); the real microphone decides (the checklist).

**The model tier** is built as a runtime and a pipeline, not yet as a trained model. [core/voice/Grammar](../src/core/voice/Grammar.hpp)
writes the per-scene GBNF (the squadmates present and ALL, the weapon codes, the party's power codes, the eight orders, the
live groups' ids, PT1..n for the deictics said, AIM/GAZE/LAST/OTHER/SELF and the four directions, up to three clauses or
`none`; unit-tested, with a reader that rebuilds it from a text block for the bench). [core/voice/Llm](../src/core/voice/Llm.hpp)
loads llama.cpp's shared CPU build (b12128, MIT; `tools/fetch-voice.ps1 -Llm` into `external/llama-cpp`) by name at run
time (its CPU backend DLLs through `ggml_backend_load_all_from_path`), a GGUF on the CPU only, decodes the prompt's prefix
(the rules and the roster) once into the KV cache and each request's scene block after it (`llama_memory_seq_rm` back to
the prefix), and samples greedily under the grammar (`llama_sampler_init_grammar`): an invented name can't come out. The
host's [plugin/VoiceModel](../src/plugin/VoiceModel.cpp) runs it on a worker (`[Voice] llm`, `llmModel`, `llmThreads`; off
without `ASI\voice\llm\llama.dll` and a `.gguf`, and compiled out of a build without the header); the mod asks it
(HostServices v31 `askVoiceModel` / `takeVoiceModelAnswer`) whenever the rules answer "not sure", with the block, the first
deictic resolved at its time and the grammar, and dispatches each line of the answer through `squad_orders::Give`. The data
writes itself from the grammar, as planned: [tools/voice-model/make_dataset.py](../tools/voice-model/make_dataset.py)
samples scenes, draws gold lines and phrasings (fillers, restarts, compounds, deictics, talk as `none`) and injects the
recognizer's slips on names and powers, in the mod's own prompt format; [train_lora.py](../tools/voice-model/train_lora.py)
is the LoRA on Qwen3-0.6B with the GGUF export (run the same evening: "The fine-tune" below; 58 minutes on the 4090).
`mevr-llm-bench` answers a dataset under the grammars with the same runtime; llama.cpp's own `llama-bench` wasn't run here
(a downloaded executable), so the raw token rates are still the research's extrapolations.

**The zero-shot baseline, measured** (`mevr-llm-bench`, Qwen3-0.6B Q8_0, 6 threads, 40 sampled prompts, the game running
in another session): the runtime works end to end (the library and its CPU backend load, the grammar holds every answer
to legal lines), and the untuned model answers 0 of 40: it fills all three clauses with the same nonsense (`ALL|SR|IMM|
attack|SELF ; ...`), 40-48 tokens each, because nothing in the grammar let it stop after one (fixed: the root ends with a
newline; with it one clause of 14 tokens, still wrong: 0 of 10). The times are the finding: the scene block's prefill
(the prefix cached) median 331-367 ms, p95 434-555 ms, and the decode about 25 tokens a second (14 tokens in 0.55-0.69
s), so a fine-tuned answer costs roughly 0.3 + 0.6 s: above the research's 60-110 ms extrapolation and the 300 ms budget,
on a CPU the game was using. The levers, in order: a Q4_K_M
file, a shorter block (the roster's power names already sit in the cached prefix; the E lines can drop their flags), a
smaller model (Granite 4.0 350M), and threads pinned off the game's cores (research/54 "Three to six threads"); measured
again once the machine is free and the fine-tune exists. Until then the model tier stays what the plan said it would be
if the rules take nine orders in ten: a nicety for the tenth, off by default where the files aren't deployed.

**In the game, the same afternoon** (the fake headset, the UNC20 drop-zone save with Ashley, no enemy; host and mod from
49acde0 and the fixes after it):

- The path is whole: `MEVR-Voice.dll` loaded in 55 ms, the capture found "Headset Microphone (Oculus Virtual Audio
  Device)" at 48 kHz float, the small models loaded in 1.7 s on the worker; a synthesized "Ashley, move up" fed by
  `mevr voice wav` came back as `Ashley Move up.`, parsed to `A|-|-|move|AHEAD` and sent `QuickCommandMoveTo` (true);
  "everyone on me" and "Ashley, fall back" to follows. Two fixes it took: the game names the squadmates in capitals
  ("ASHLEY") and the parser compared them case-sensitively, so "Ashley" parsed to ALL at 0.85 with "ashley" unresolved
  (the roster's names are tokenized lower-case now, 1.00); and the key terms came out "ASHLEY", so they're title-cased
  for the recognizer.
- **The acknowledgement, traced** (`mevr calltrace 10 chatter soundset playsound` with the script-call hook in): a follow
  runs `BioSquadPlayer.OnSetSquadFollowFormation` -> `BioBaseSquad.SquadHandleChatterEvent` as a SCRIPT call (not an
  event: the ProcessEvent hook on it never fires) -> `BioPawn.HandleChatterEvent` on Shepard's pawn and the squadmate's ->
  `BioChatterSystem.CanPlayLine` -> `BioPawn.OnChatterLinePlayed` (an event native raises when the line starts), and the
  line that played was on `SFXPawn_Player`: **the follow's bark is Shepard's own** ("regroup!"), 0.03 s after the order.
  A hold through the HOLD formation makes the squadmate answer herself, 0.03 s after the order (the same formation
  path). A `QuickCommandMoveTo` plays nothing within a second, and nothing later either (checked with the fallback off),
  so the fallback line (`PlaySoundSet` 70) is right for moves; after it `OnChatterLinePlayed` fires on her pawn, so the
  line really plays. The module now hears `OnChatterLinePlayed` (the definitive answer, by pawn) and `HandleChatterEvent`
  (CallFunction) besides the two it started with; a line on the player's pawn is logged as Shepard's bark, not an answer.
  The game's chatter suppression after a line (`m_fChatterSuppressionDuration`) explains a hold's missing answer right
  after a move's fallback line. Attack: untested (no enemy in the save), the UI's own d-pad sound: untraced.
- **The frame cost at this spot** (`tools/perf-probe.ps1`, 10 s each): idle, frame interval 32.2 ms (the null driver
  paces at 31 fps here), the render thread's "both eyes + UI" median 2.17 ms, p90 2.66; while a file was recognized,
  32.35 ms, 2.93 median, p90 8.25, max 12.2 ms: the recognizer's threads do take a few milliseconds from the render
  thread's worst frames (ONNX Runtime's pool is at the library's default thread count, which the C API doesn't expose;
  the next lever is lowering those threads' priority from our side). xrWaitFrame was 27-29 ms either way.
- **The final's delay was mine, not the recognizer's**: a 2.3 s file came back 2.3 s after its end, because the capture
  (and the file feeder) added audio under the same lock the worker holds through a transcribe call, so each add waited
  out a partial. The library's calls are thread-safe; the add runs without the lock now, and the DLL logs each listen's
  final cost and its partials' count, costliest and sum (`voice: the listen's final transcript took ...`).
- **The recognizer beside the running game, measured** (the DLL's new log line, a 2.3 s and a 3.5 s file): the small
  model's partials cost 200-950 ms each (8 partials 1.9 s together, 10 partials 3.1 s), five times the bench's figure on
  the quiet CPU, and the last one in flight is what delays the final (308 ms once); the tiny model's cost 300 ms at worst
  (10 partials 0.76 s, 15 partials 1.16 s) with the final at 0 ms. So `model = tiny` and `intervalMs = 250` are the
  defaults now; the text came out the same ("Ashley. Move up."). The thread priority of ONNX Runtime's pool is the next
  lever (the C API doesn't set its thread count).
- **The model tier in the game**: llama.cpp and the stock Qwen3-0.6B Q8_0 deployed to `ASI\voice\llm` (deploy-asi copies
  them now), loaded in 1.6 s on the host's worker; "Ashley, you know what to do with the big one" was refused by the rules
  (0.37: "know what do" unresolved), asked of the model, and answered 1.98 s after the ask (158 prompt tokens in 434 ms,
  14 answer tokens in 466 ms, on 4 threads beside the game): `ALL|SR|ARS|attack|BEHIND`, legal by the grammar, refused by
  the validator ("a weapon or a power needs one squadmate"). The untrained model's answer is nonsense, as expected; the
  loop (the mod's ask, the host's worker, the grammar, the validator, the dispatcher) is proven, and the budget for a
  trained one is about a second at these settings.

## The fine-tune (2026-11-05, evening)

**The pipeline.** [tools/voice-model/pipeline.py](../tools/voice-model/pipeline.py) runs one config
(`configs/base.json`) as one folder under `scratch/voice-model/runs/<name>/`: the datasets, the LoRA, the merged model,
the GGUFs, the eval and the bench, each step with a `done-<step>.json` keyed by the hash of the part of the config it
depends on, so a failed step resumes and a changed hyperparameter redoes only what follows from it; `sweep` takes a grid
(`rank=8,16,32 lr=1e-4,2e-4`) from the same base, and `runs.tsv` collects every run's figures. `setup-env.ps1` makes the
venv (Python 3.12, torch 2.6 cu124, transformers 5.22, peft 0.21) and fetches llama.cpp's convert script. The settings
are the common LoRA recipe, not a tuned result (the README cites the sources: Unsloth's guide, TinyAgent, the Natural
GaLore baseline): rank 16, alpha 32, every attention and MLP projection (10.1 M trainable parameters, 1.67% of the 606 M),
learning rate 2e-4 with a cosine schedule and 3% warmup, weight decay 0.01, 2 epochs, an effective batch of 16, bf16.

**Run1.** 20,000 generated lines (seed 1, noise 0.3), 1,000 held out (seed 2), max 512 tokens a record. The first attempt
ran at 500 s a step with 23.4 GB of VRAM: a batch of 16 records of up to 768 tokens with the 151k-vocabulary logits spills
the 4090 into system memory. With micro-batches of 4 accumulated 4 times, gradient checkpointing and 512 tokens it ran at
1.4 s a step and 12 GB: 2,500 steps, 58 minutes (3,475 s, 11.5 records a second). The loss: 4.54 at step 20, 0.26 at
100, 0.21 at 500, flat at 0.20 through the second epoch (the mean over the run 0.264). It is the loss over the whole text,
prompt included (the collator trains on every token), so that floor is the scene block's own perplexity and says little
about the answer: masking the loss to the answer is the first change for the next run, before any sweep.

**Exact match: 939 of 1,000 held-out lines (93.9%)**, greedy, the merged bf16 model on the GPU, no grammar (the host's
grammar can only help). Of the 61 misses, most are the group: a description ("the left ones", "the ones on the balcony",
"the behind ones", "the ones up there") mapped to a neighbouring E id, i.e. the model has not fully learned to read the
E lines' words (`left mid above`, `balcony`) and pick by them; the rest are an elided object ("Williams, take": gold
`hold`, answered `attack E1`), a hold that dropped its point, and the second clause of a compound. Some golds are
debatable themselves ("both of you hold position" with a group as the point), so the generator's labels are part of the
error and the first real transcripts are worth more than another synthetic run. The zero-shot model answered 0 of 40;
the narrow fine-tunes the survey cited (34.5% to 95.9%, 58% to 85%) are the same jump.

**GGUF and the bench: two runtime bugs the bench found.** The merged model exported to q8_0 (633 MB) with llama.cpp's
`convert_hf_to_gguf.py` (since b12128 it needs its `conversion` package beside it; `setup-env.ps1` copies it now, run1's
export failed without it). No q4_k_m file: `llama-quantize.exe` is a downloaded executable and wasn't run here (as
`llama-bench` wasn't), so the Q4 lever from the baseline section stays open until it's built from source or the user
runs it. `mevr-llm-bench` on the q8_0 file, 4 threads, the first 300 held-out prompts, each under its scene's grammar:

| The runtime | Exact | Prefill median / p95 | Decode median / p95 | Total median |
|---|---|---|---|---|
| As built: the raw prompt | 7 of 300 (2.3%) | 334 / 482 ms | 405 / 905 ms | 739 ms |
| The chat template around the prompt | 244 of 300 (81.3%) | 390 / 541 ms | 309 / 418 ms | 699 ms |
| ... and `-` allowed as the order | **285 of 300 (95.0%)** | 333 / 450 ms | 283 / 744 ms | **616 ms** |

The first row is a format mismatch, not the weights: the trainer wraps every record in Qwen's chat template with a
system line (`train_lora.py`'s `SYSTEM`, thinking off), and the runtime fed the raw prefix and block; the model then
put a weapon on every line and ran on into three clauses. `core/voice/Llm` wraps the prompt the same way now
(`Settings::chatTemplate`, the head before the cached prefix and the turn's tail after each block, the special tokens
parsed; `[Voice] llmChat = 0` or the bench's `--raw` for a model trained without it), and the grammar's newline is
optional, so the end-of-turn token the fine-tune emits can end the answer. The second row is the grammar's hole: an
equip alone is `W|PI|-|-|-`, and `order` had no `-`, so every equip line was forced into an order ("Wrex, go sidearm"
became `attack`) and a compound's clauses merged into one. With both fixed the runtime matches the trainer's own
eval (95.0% on the first 300 against 93.9% on all 1,000, the q8_0 quantization costing nothing visible), and all 15
misses are the group: a description mapped to a neighbouring E id. The time is the baseline's prediction: 0.3 s of
prefill for the scene block and 0.3 s of decode for 8 tokens, 0.62 s median on a quiet CPU, which in play (the game's
threads, 4 of ours) was about a second for the stock model. The file is `models/voice/llm/model.gguf` now (the stock
Qwen3-0.6B-Q8_0 moved to `scratch/voice-model/stock`), so `tools/deploy-asi.ps1` ships the fine-tuned model and
`plugin/VoiceModel` loads it by its name.

**In the game** (16:36-16:40, the fake headset, the UNC20 save with Ashley, the five groups 60-80 m off and hidden; host
and mod from d3aa862 with the runtime fixes; `mevr voice say <words>` is new: a typed transcript on the whole path, the
rules and then the model, where `mevr say` stops at the rules): the model loaded from `model.gguf` in 2.1 s on the host's
worker, and five lines the rules refused (0.06-0.51) came back from the model 0.72-0.85 s after the ask (161-167 prompt
tokens in 446-463 ms, 6-9 answer tokens in 226-337 ms, 4 threads beside the running game) and went out as orders:
"Ashley, you know what to do with the big one" -> `A|-|-|attack|E1` (the biggest group; `QuickCommandAttackTarget` true),
"Ash, the thresher maw is yours" -> `attack E5`, "Williams, go sidearm" -> `A|PI|-|-|-` (the squad menu's switch: "ordered
to the pistol (was assault rifle)"), "Ash, do something clever about the guys on the ridge" -> `attack E2` (a guess among
four groups all "left far above": the block carries no "ridge"), and "Ashley, sabotage the one on the far right" ->
`A|-|AWS|use|E5` (Suppression through `SquadOrderUsePower` + `ApplyTacticalOrders`: she has no Sabotage, and the model
took the nearest power rather than refusing; the validator let it through because the power is hers). The budget the
baseline predicted holds: about 0.8 s from the release to the order when the rules can't place a line.

**Run2: the loss on the answer alone** (`--loss answer`, `configs/run2.json`, 16:48-18:10; the prompt's tokens labelled
-100, 8 labelled tokens of about 370 a record): the loss reads the answer now (3.6 at step 20, 0.01-0.02 through the second
epoch, the mean 0.054, against run1's 0.20 floor), 56 minutes, 948 of 1,000 exact on the GPU (run1 939) and 284 of 300
under llama.cpp (run1 285, the same 0.64 s median): the same model within noise, and the same misses. Reading those misses'
scene blocks settles what they are: **the generator's labels, not the model.** "The left ones" where E2 (left mid above)
and E3 (left far below) are both left, gold E2; "the behind ones" with E1 and E2 both behind, gold E1; "those thorian
creepers" with creepers at E2 and E5, gold E5; "both of you hold position" with a group as the hold's point, gold E3. Each
is a draw between two legal answers (or no right answer), so the ~5% residual is the data's ambiguity and the model's
accuracy on unambiguous lines is higher than the figure. run1 stays deployed (the one checked in the game); run2's file
is `scratch/voice-model/runs/run2/model-q8_0.gguf`.

**Next, in order:** the generator: no description that fits two groups (or a gold that names both), no hold with a group as
its point, and its talk and ASR noise widened; then real transcripts from the headset into the training data; a shorter
block for the prefill (the baseline's levers). A hyperparameter sweep is not where the next point is.

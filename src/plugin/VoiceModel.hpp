// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>

// The model tier in the host (research/54 "Rules first, a fine-tuned sub-1B model second"): core/voice/Llm (llama.cpp's
// shared build loaded from ASI\voice\llm, a GGUF next to it) on a worker of its own. The mod asks with the prompt's prefix,
// the scene block (ending in the answer's cue) and the scene's grammar (HostServices v31 askVoiceModel); a newer request
// replaces one not yet taken; the worker decodes the prefix once (cached) and the block each time, and the mod takes the
// answer (takeVoiceModelAnswer) and dispatches it. `[Voice] llm` (0: off), `llmModel` (default ASI\voice\llm\model.gguf,
// else the first .gguf there), `llmThreads` (4), `llmDir`. Off without the library or a model (one log line). Log `voice
// model:`; snapshot "voice"."model".
namespace mevr::host::voice_model {

void Init();      // after voice::Init, at attach: loads on a worker
void Shutdown();  // joins the worker

// HostServices v31.
std::uint32_t Ask(char const* prefix, char const* rest, char const* gbnf);  // the request's id, 0 when the model is off
bool TakeAnswer(std::uint32_t* id, char* text, std::uint32_t capacity, std::uint32_t* ms);

std::string StatusJson();

}  // namespace mevr::host::voice_model
